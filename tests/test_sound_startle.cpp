// Native checks for the opt-in loud-noise startle: detector and acting cue.
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "../firmware/sound_startle.h"
#include "../firmware/avatar_interaction.h"
#include "../firmware/avatar_behavior.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

constexpr uint32_t kRate = 16000;
constexpr size_t kBlock = 512;  // 32ms, as in firmware
constexpr uint32_t kBlockMs = 32;

struct Noise {
  uint32_t state = 0x9e3779b9u;
  float next() {  // uniform -1..1
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return (state & 0xffffff) / 8388607.5f - 1;
  }
};

// Fills one block: noise of `amplitude` plus a decaying burst starting at `burstAt`.
static void block(int16_t* out, Noise& noise, float amplitude, float burst = 0, float burstAge = 0,
                  float dc = 0) {
  for (size_t i = 0; i < kBlock; ++i) {
    const float age = burstAge + static_cast<float>(i) / kRate;
    const float env = burst > 0 ? burst * expf(-age / .045f) : 0;
    float v = dc + noise.next() * amplitude + noise.next() * env;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    out[i] = static_cast<int16_t>(v);
  }
}

struct Run {
  sound_startle::Detector d;
  Noise noise;
  uint32_t now = 0;
  int triggers = 0;
  float strongest = 0;
  Run() { d.reset(0); }
  void quiet(uint32_t ms, float amplitude = 60, float dc = 0) {
    int16_t b[kBlock];
    for (uint32_t end = now + ms; now < end; now += kBlockMs) {
      block(b, noise, amplitude, 0, 0, dc);
      tally(d.feed(b, kBlock, kRate, now));
    }
  }
  // A clap/bang: decaying burst across a few blocks over a background.
  void bang(float burst, float background = 60, bool suppressed = false) {
    int16_t b[kBlock];
    for (int k = 0; k < 6; ++k, now += kBlockMs) {
      block(b, noise, background, burst, k * kBlockMs / 1000.0f);
      tally(d.feed(b, kBlock, kRate, now, suppressed));
    }
  }
  void tally(float s) {
    CHECK(s >= 0 && s <= 1 && isfinite(s));
    if (s > 0) { ++triggers; if (s > strongest) strongest = s; }
  }
};

static void measuresLevels() {
  int16_t silence[kBlock] = {};
  CHECK(sound_startle::measure(silence, kBlock).dbfs < -85);
  CHECK(sound_startle::measure(nullptr, 0).dbfs <= -100);
  int16_t square[kBlock];
  for (size_t i = 0; i < kBlock; ++i) square[i] = (i % 2 != 0) ? 32767 : -32767;
  const auto full = sound_startle::measure(square, kBlock);
  CHECK(fabsf(full.dbfs) < .1f && full.peak == 32767);
  int16_t dc[kBlock];
  for (size_t i = 0; i < kBlock; ++i) dc[i] = 6000;
  CHECK(sound_startle::measure(dc, kBlock).dbfs < -85);  // DC is not loudness
}

static void quietRoomNeverStartles() {
  Run r; r.quiet(30000);
  CHECK(r.triggers == 0);
  Run dc; dc.quiet(10000, 60, 4000);
  CHECK(dc.triggers == 0);
}

static void clapStartlesOnce() {
  Run r; r.quiet(3000);
  r.bang(30000);
  CHECK(r.triggers == 1);
  CHECK(r.strongest >= .35f);
  r.quiet(3000);
  CHECK(r.triggers == 1);
}

static void warmupIgnoresOpeningTransient() {
  Run r;
  r.bang(30000);  // straight after (re)opening the microphone
  CHECK(r.triggers == 0);
  r.quiet(2000);
  r.bang(30000);
  CHECK(r.triggers == 1);
}

static void gradualLoudnessDoesNotStartle() {
  Run r; r.quiet(2000);
  int16_t b[kBlock];
  // 54dB over 4s, then sustained near full scale for 10s.
  for (uint32_t t = 0; t < 14000; t += kBlockMs, r.now += kBlockMs) {
    const float db = t < 4000 ? -54 * (1 - t / 4000.0f) : 0;
    block(b, r.noise, 28000 * powf(10, db / 20));
    r.tally(r.d.feed(b, kBlock, kRate, r.now));
  }
  CHECK(r.triggers == 0);
}

static void quietClapIsNotAStartle() {
  Run r; r.quiet(3000);
  r.bang(2500);  // soft tap: below the peak/absolute loudness gates
  CHECK(r.triggers == 0);
}

static void loudRoomNeedsMuchLouderBang() {
  Run r; r.quiet(2000);
  r.quiet(10000, 4000);       // sustained loud background
  r.bang(9000, 4000);         // modestly louder bang: not sudden enough
  CHECK(r.triggers == 0);
}

static void handlingIsSuppressedButStillAdapts() {
  Run r; r.quiet(3000);
  r.bang(30000, 60, true);
  CHECK(r.triggers == 0);
  r.quiet(1500);
  r.bang(30000);
  CHECK(r.triggers == 1);
}

static void refractoryAndHabituation() {
  Run r; r.quiet(3000);
  for (int i = 0; i < 4; ++i) { r.bang(30000); r.quiet(800); }  // clap train
  CHECK(r.triggers == 1);

  // Moderate bangs every 6s habituate, then sensitivity recovers in quiet.
  sound_startle::Detector d; d.reset(0);
  uint32_t now = 0;
  const sound_startle::Level quiet{-58, 60}, moderate{-25, 12000};
  for (; now < 2000; now += kBlockMs) d.feedLevel(quiet, kBlock, kRate, now);
  int hits = 0;
  for (int i = 0; i < 6; ++i) {
    for (uint32_t end = now + 6000; now < end; now += kBlockMs) d.feedLevel(quiet, kBlock, kRate, now);
    hits += d.feedLevel(moderate, kBlock, kRate, now) > 0; now += kBlockMs;
  }
  CHECK(hits >= 2 && hits < 6);
  CHECK(d.habituation <= d.config.maxHabituationDb);
  for (uint32_t end = now + 70000; now < end; now += kBlockMs) d.feedLevel(quiet, kBlock, kRate, now);
  CHECK(d.habituation == 0);
  CHECK(d.feedLevel(moderate, kBlock, kRate, now) > 0);
}

static void slightlyLowerSensitivity() {
  const sound_startle::Level quiet{-58,60};
  // Previously sufficient sounds near each old gate are now rejected.
  const sound_startle::Level marginal[]={{-28,12000},{-25,9500}};
  for(const auto& level:marginal) {
    sound_startle::Detector old,current;
    old.config.riseDb=20;old.config.minDbfs=-30;old.config.peak=9000;
    old.reset(0);current.reset(0);
    for(uint32_t t=0;t<2000;t+=kBlockMs) {
      old.feedLevel(quiet,kBlock,kRate,t);current.feedLevel(quiet,kBlock,kRate,t);
    }
    CHECK(old.feedLevel(level,kBlock,kRate,2016)>0);
    CHECK(current.feedLevel(level,kBlock,kRate,2016)==0);
  }
  // Relative gate also tightens by 3dB even for an absolutely loud onset.
  sound_startle::Detector d;d.reset(0);
  for(uint32_t t=0;t<2000;t+=kBlockMs)d.feedLevel({-45,400},kBlock,kRate,t);
  CHECK(d.feedLevel({-23,16000},kBlock,kRate,2016)==0);
}

static void connectedSleepWakesAndReturns() {
  using namespace avatar_behavior;
  Status idle={0,0,0,true,false};
  CHECK(select(idle,false,false)==Mode::sleeping);
  CHECK(canStartle(Mode::sleeping,false,false,false,false));
  Run r;r.quiet(3000);r.bang(30000);
  CHECK(r.triggers==1);
  avatar_interaction::Startle cue;
  const uint32_t now=10000;
  cue.notify(now,r.strongest,1);
  CHECK(select(idle,false,true,false,cue.active)==Mode::idle);
  CHECK(cue.apply(rest(Mode::idle),now+70).height>1.2f);
  cue.update(now+cue.kEnd+cue.kBodyLag,false);
  CHECK(!cue.active);
  CHECK(select(idle,false,true,false,cue.active)==Mode::idle);
  CHECK(select(idle,false,false,false,cue.active)==Mode::sleeping);
  // During connection grace, wake with honest offline semantics, not fake idle.
  idle.healthy=false;
  CHECK(select(idle,false,false,true,true)==Mode::offline);
  CHECK(select(idle,false,false,true,false)==Mode::sleeping);
  CHECK(!canStartle(Mode::sleeping,true,false,false,false));
  CHECK(!canStartle(Mode::sleeping,false,true,false,false));
  CHECK(!canStartle(Mode::listening,false,false,false,false));
  CHECK(!canStartle(Mode::sleeping,false,false,true,false));
  CHECK(!canStartle(Mode::sleeping,false,false,false,true));
  cue.notify(now,1,1);
  cue.update(now+100,!canStartle(Mode::sleeping,true,false,false,false));
  CHECK(!cue.active);
  CHECK(idle.active==0&&idle.done==0&&idle.attention==0);
}

static void cueActsAndSettles() {
  using avatar_interaction::Startle;
  const auto base = eye_acting::p(3, 1, 1, 1, .1f, .1f, .2f);
  Startle s;
  CHECK(eye_acting::sameEyes(s.apply(base, 500), base));  // inactive: untouched
  s.notify(1000, 1, 1);
  const auto jolt = s.apply(base, 1070);
  CHECK(jolt.height > 1.3f && jolt.y < base.y - 4);
  CHECK(s.apply(base, 1070, true).lift > -1);           // body follows 90ms later
  CHECK(s.apply(base, 1250, true).lift < -4);
  const auto right = s.apply(base, 1700);
  Startle m; m.notify(1000, 1, -1);
  const auto left = m.apply(base, 1700);
  CHECK(right.x > base.x + 6 && left.x < base.x - 6);   // mirrored wary glance
  for (uint32_t t = 1000; t < 2700; t += 5) {
    for (int body = 0; body < 2; ++body) {
      const auto pose = s.apply(base, t, body);
      CHECK(isfinite(pose.x) && isfinite(pose.y) && isfinite(pose.height) && isfinite(pose.lift));
      CHECK(fabsf(pose.x) < 20 && fabsf(pose.lift) < 9 && pose.height < 1.45f && pose.width > .8f);
      if (body) CHECK(pose.closeL == 0 && pose.closeR == 0);
    }
  }
  CHECK(eye_acting::sameEyes(s.apply(base, 1000 + Startle::kEnd), base));
  s.update(1000 + Startle::kEnd + Startle::kBodyLag, false);
  CHECK(!s.active);
  Startle cut; cut.notify(0, .5f, 1);
  cut.update(100, true);
  CHECK(!cut.active && eye_acting::sameEyes(cut.apply(base, 120), base));
  Startle weak; weak.notify(0, 0, 1);  // even the weakest trigger reads
  CHECK(weak.apply(base, 70).height > 1.2f);
}

int main() {
  measuresLevels();
  quietRoomNeverStartles();
  clapStartlesOnce();
  warmupIgnoresOpeningTransient();
  gradualLoudnessDoesNotStartle();
  quietClapIsNotAStartle();
  loudRoomNeedsMuchLouderBang();
  handlingIsSuppressedButStillAdapts();
  refractoryAndHabituation();
  slightlyLowerSensitivity();
  connectedSleepWakesAndReturns();
  cueActsAndSettles();
  if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
  printf("sound startle: all checks passed\n");
  return 0;
}
