#include <M5Unified.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <math.h>

#include "display_fonts.h"
#include "face_contour.h"
#include "roman_counts.h"

#include "status_sides.h"
#include "avatar_scene.h"
#include "counter_space.h"
#include "counter_labels.h"
#include "status_background.h"
#include "count_pop.h"
#include "avatar_interaction.h"
#include "eye_life.h"
#include "avatar_behavior.h"
#include "avatar_director.h"
#include "task_cues.h"
#include "request_scene.h"
#include "usb_presence.h"
#include "power_nap.h"
#include "frame_profile.h"
#include "framebuffer_raster.h"
#include "sound_startle.h"

#ifndef STICK_S3_PROFILE
#define STICK_S3_PROFILE 0
#endif
#ifndef STICK_S3_PROFILE_BUILD_ID
#define STICK_S3_PROFILE_BUILD_ID 0
#endif
// Opt-in: keep the microphone open while idle and let sudden loud noises
// startle the avatar. Levels are analyzed on-device only; no audio leaves RAM.
#ifndef STICK_S3_AMBIENT_STARTLE
#define STICK_S3_AMBIENT_STARTLE 0
#endif

namespace {

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint16_t>(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
}

constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 135;
constexpr uint32_t kFrameMs = 33;
const count_raster::Font* const kCountFonts[] = {
    &kBadgeFont, &kMediumBadgeFont, &kSmallBadgeFont, &kTinyBadgeFont, &kCompactBadgeFont};
constexpr int kCountSideWidth = 48;  // Minimum exposed background beside the face.
constexpr int kCountPadding = 16;

constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kEye = rgb565(236, 244, 255);
// Distinct, saturated status colors. Needs-input uses the task list's red.
constexpr uint16_t kWorking = rgb565(0x3B, 0x82, 0xF6);
constexpr uint16_t kBlocked = rgb565(0xF7, 0x76, 0x8E);
constexpr uint16_t kDone = rgb565(0x22, 0xC5, 0x5E);

int activeCount = 0;
int attentionCount = 0;
int doneCount = 0;
bool bridgeConnected = false;
bool voiceRecording = false;
bool micPending = false;
bool holdHandled = false;
uint32_t voiceStartedAt = 0;
int16_t micSamples[1280];
int rotation = 1;
bool attentionChimePlaying = false;
uint8_t attentionChimeStep = 0;
uint32_t lastMessageAt = 0;
usb_presence::Tracker usbPresence;
power_nap::Nap nap;
bool napSupported = false;  // StickS3 with M5PM1: wake buttons and VBUS probe known.
uint32_t napStartedAt = 0;
uint32_t nextAttentionChimeAt = 0;
uint32_t manualRotationUntil = 0;
uint32_t lastImuAt = 0;
float flatTurnDegrees = 0;

// Two full frames: render into one while the other is still being sent to the
// panel by SPI DMA (~13ms at 40MHz). Falls back to one synchronous frame if
// the second internal DMA-capable buffer cannot be allocated.
M5Canvas canvases[2] = {M5Canvas(&M5.Display), M5Canvas(&M5.Display)};
M5Canvas* frameCanvas = &canvases[0];
bool doubleBuffered = false;
bool transferOpen = false;
M5Canvas& frame() { return *frameCanvas; }
avatar_scene::Scene scene;
counter_space::State counterSpace;
counter_labels::State counterLabels;
avatar_scene::Mask faceMask;
face_physics::Surface surface;
face_physics::Reaction countReaction;
eye_acting::Player actor;
avatar_behavior::Director director;
avatar_interaction::Checks counterChecks;
avatar_interaction::CounterFlight counterFlight;
avatar_interaction::ButtonCue buttonCue;
avatar_interaction::Startle startle;
// Small hops while requesting input, below the `!` (request_scene.h).
request_scene::Hops requestHops;
float pendingStartle = 0;  // Detector output, consumed (or dropped) by updateGaze.
// Listening soundwave: loudness of the dictation blocks already being captured,
// computed here (10ms levels only); nothing new is sent, stored or logged.
listen_wave::Envelope voiceLevel;
listen_wave::Wave listenWave;
#if STICK_S3_AMBIENT_STARTLE
// Ambient level listening: two 32ms blocks alternate so capture is continuous.
// The microphone shares the ES8311 codec/I2S clocks with the speaker, so it
// pauses for the attention chime, dictation, and unplugged/battery naps.
constexpr size_t kAmbientBlock = 512;
constexpr uint32_t kAmbientRate = 16000;
int16_t ambientBuffers[2][kAmbientBlock];
uint8_t ambientQueued = 0, ambientHead = 0;
bool ambientRunning = false;
uint32_t ambientResumeAt = 0;
uint32_t lastHandlingAt = 0;  // Button clicks/handling are heard; never startle at them.
sound_startle::Detector hearing;
#endif
// Staged, noticeable count cues: a new active numeral drops in; an active->done
// completion charges, flashes and flips. Real counts still update immediately.
task_cues::Arrival taskArrival;
task_cues::Transfer taskTransfer;
task_cues::StepOver stepOver;  // Opening the last completion: cover the green, never fade it.
// Herdr may split one completion over two polls; hold the first half briefly.
task_cues::CompletionPairing completionPairing;
count_raster::Cache flyingRaster;
eye_acting::Pose actingEyes = eye_acting::p();
eye_acting::Pose actingBody = eye_acting::p();
eye_life::Life eyeLife;
eye_life::SizeSpring eyeSize;
float flowClock = 0;

using Face = avatar_behavior::Mode;

Face face = Face::offline;
bool faceApplied = false;
uint32_t celebrateUntil = 0;
uint32_t lastFrameAt = 0;
uint32_t nextFrameAt = 0;
uint32_t lastTaskAt = 0;
bool quakeVisible = false;  // Presence only, never a synthetic task/count.
uint8_t brightness = 0;

#if STICK_S3_PROFILE
frame_profile::Sample timingSample;
uint32_t timingStart=0,timingMark=0,timingPreviousStart=0,timingSequence=0;
uint32_t timingDropped=0,timingVoiceSkipped=0,timingObserver=0;
bool timingHasPrevious=false,timingPsramFallback=false;
void profileBegin(uint32_t now) {
  timingStart=micros();timingMark=timingStart;
  timingSample=frame_profile::Sample{};
  timingSample.us[frame_profile::interval]=timingHasPrevious?
      frame_profile::elapsed(timingStart,timingPreviousStart):0;
  timingPreviousStart=timingStart;timingHasPrevious=true;
  timingSample.sequence=timingSequence++;timingSample.uptime_ms=now;
  timingSample.build_id=STICK_S3_PROFILE_BUILD_ID;
  timingSample.period_us=kFrameMs*1000;
}
void profileMark(frame_profile::Metric metric) {
  const uint32_t now=micros();
  timingSample.us[metric]=frame_profile::elapsed(now,timingMark);timingMark=now;
}
void profileFinish() {
  // Blocking presentation time: waiting for the PREVIOUS frame's DMA plus
  // queueing this one. With double buffering the transfer itself overlaps the
  // next render and is no longer serialized into this stage.
  profileMark(frame_profile::flush);
  timingSample.us[frame_profile::total]=frame_profile::elapsed(timingMark,timingStart);
  const uint32_t observerStart=micros();
  timingSample.state=static_cast<uint8_t>(face);
  timingSample.flags=(avatar_scene::mouthVisibility(scene)>0?1:0)|(scene.corners>=.35f?2:0)|
                     (scene.sleep>=.5f?4:0)|(timingPsramFallback?8:0);
  timingSample.dropped=timingDropped;timingSample.voice_skipped=timingVoiceSkipped;
  timingSample.previous_observer_us=timingObserver;
  timingSample.cpu_mhz=getCpuFrequencyMhz();
  if(voiceRecording) { ++timingVoiceSkipped; }
  else if(Serial && Serial.availableForWrite()>=static_cast<int>(frame_profile::packet_size)) {
    uint8_t packet[frame_profile::packet_size];frame_profile::encode(timingSample,packet);
    if(Serial.write(packet,sizeof(packet))!=sizeof(packet))++timingDropped;
  } else { ++timingDropped; }
  // Admission is drop-on-full, but HWCDC also takes locks. Report all observer
  // time on the next sample instead of pretending telemetry is cost-free.
  timingObserver=frame_profile::elapsed(micros(),observerStart);
}
#define STICK_PROFILE_MARK(metric) profileMark(frame_profile::metric)
#else
#define STICK_PROFILE_MARK(metric) do {} while (0)
#endif

constexpr uint32_t kSleepAfterMs = 4000;
constexpr uint8_t kAwakeBrightness = 90;
constexpr uint8_t kSleepBrightness = 20;

struct Badge {
  float presence = 0;  // Background fades in while the oval makes room.
  float width = kCountSideWidth;
  float label = 0;
  int shown = 0;
  int largeWidth = 0;  // Width in the chosen Roman-numeral font.
  int fontIndex = 0;
  count_pop::Pop pop;
  avatar_interaction::CounterSpring spring;
  count_raster::Cache raster;
};

struct Indicators {
  float region = 280;  // Black oval width; idle covers the entire display.
  float side = 0;
  float single = 0;  // One count side: expand the eyes into the unused space.
  float corners = 0;  // Both counts tuck into bottom corners beneath the face.
  float offset = 0;  // Shared horizontal shift for the oval and eyes.
  float waiting = 0;
  float alert = 0;
  float boost = 0;
  float sleep = 0;  // 0 awake, 1 eyes closed and backlight dimmed.
  float eyeWidth = 50;
  float eyeHeight = 56;
  float awakeWidth = 50;
  float awakeHeight = 56;
  Badge done;
  Badge active;
} ui;

float approach(float value, float target, float seconds, float timeConstant) {
  return avatar_scene::approach(value, target, seconds, timeConstant);
}

float lerp(float from, float to, float amount) {
  return from + (to - from) * amount;
}

uint16_t blend565(uint16_t from, uint16_t to, float amount) {
  if (amount <= 0) return from;
  if (amount >= 1) return to;
  const int red = lerp((from >> 11) & 31, (to >> 11) & 31, amount) + 0.5f;
  const int green = lerp((from >> 5) & 63, (to >> 5) & 63, amount) + 0.5f;
  const int blue = lerp(from & 31, to & 31, amount) + 0.5f;
  return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

framebuffer_raster::View framePixels;
lgfx::rgb565_t statusRows[4][kScreenWidth];
uint16_t statusStored[4][kScreenWidth];  // Same rows in the sprite's swapped storage order.
status_background::RowCache statusCache;
void inkFrame(int x,int y,uint16_t color,float alpha) {
  if(framePixels.pixels)framePixels.ink(x,y,color,alpha);
  else if(alpha>=1)frame().drawPixel(x,y,color);
  else if(alpha>0)frame().drawPixel(x,y,blend565(frame().readPixel(x,y),color,alpha));
}

void sendFrame(char kind, const uint8_t* data = nullptr, uint16_t length = 0) {
  const uint8_t header[] = {'S', '3', 'V', '1', static_cast<uint8_t>(kind),
                            static_cast<uint8_t>(length & 255), static_cast<uint8_t>(length >> 8)};
  Serial.write(header, sizeof(header));
  if (length) Serial.write(data, length);
}

#if STICK_S3_AMBIENT_STARTLE
void stopAmbient(bool restoreSpeaker) {
  if (!ambientRunning) return;
  M5.Mic.end();  // Drops pending ambient blocks; nothing is retained.
  ambientRunning = false;
  ambientQueued = 0;
  if (restoreSpeaker) M5.Speaker.begin();
}

bool queueAmbient() {
  const uint8_t slot = (ambientHead + ambientQueued) & 1;
  if (!M5.Mic.record(ambientBuffers[slot], kAmbientBlock, kAmbientRate, false)) return false;
  ++ambientQueued;
  return true;
}

void updateAmbient(uint32_t now) {
  if (M5.BtnA.isPressed() || M5.BtnB.isPressed() || M5.BtnA.wasReleased() || M5.BtnB.wasReleased())
    lastHandlingAt = now;
  if (voiceRecording || attentionChimePlaying || usbPresence.unplugged) {
    stopAmbient(!voiceRecording);
    ambientResumeAt = now + 350;  // Let the chime/handling noise die away.
    return;
  }
  if (static_cast<int32_t>(now - ambientResumeAt) < 0) return;
  if (!ambientRunning) {
    M5.Speaker.end();
    if (!M5.Mic.begin()) {
      M5.Speaker.begin();
      ambientResumeAt = now + 5000;
      return;
    }
    ambientRunning = true;
    ambientQueued = 0;
    ambientHead = 0;
    hearing.reset(now);
  }
  // The capture task fills requests in order: the oldest finishes first.
  while (ambientQueued > 0 && M5.Mic.isRecording() < ambientQueued) {
    const bool handled = now - lastHandlingAt < 700;
    const float strength = hearing.feed(ambientBuffers[ambientHead], kAmbientBlock, kAmbientRate, now, handled);
    if (strength > pendingStartle) pendingStartle = strength;
    ambientHead ^= 1;
    --ambientQueued;
  }
  while (ambientQueued < 2 && queueAmbient()) {}
  if (ambientQueued == 0) {  // The capture port failed; retry later.
    stopAmbient(true);
    ambientResumeAt = now + 5000;
  }
}
#else
void stopAmbient(bool) {}
void updateAmbient(uint32_t) {}
#endif

void stopVoice(bool aborted = false) {
  M5.Mic.end();
  voiceRecording = false;
  micPending = false;
  sendFrame(aborted ? 'X' : 'E');
  if (!aborted) buttonCue.notify(millis(), true);
  M5.Speaker.begin();
#if STICK_S3_AMBIENT_STARTLE
  ambientResumeAt = millis() + 600;  // Release click and handling noise.
#endif
}

void updateVoice() {
  // Unplugged there is no host to receive audio or focus requests; queued frames
  // would be replayed on replug. Buttons then only wake the face (power_nap).
  const bool hostless = usbPresence.unplugged;
  if (M5.BtnA.pressedFor(300) && !holdHandled && !hostless) {
    holdHandled = true;
    stopAmbient(false);
    M5.Speaker.end();
    attentionChimePlaying = false;
    if (M5.Mic.begin()) {
      voiceRecording = true;
      voiceStartedAt = millis();
      voiceLevel.reset();
      buttonCue.active = false;
      sendFrame('B');
    } else {
      sendFrame('X');
      M5.Speaker.begin();
    }
  }
  if (voiceRecording) {
    if (micPending && !M5.Mic.isRecording()) {
      voiceLevel.feed(micSamples, sizeof(micSamples) / sizeof(micSamples[0]), millis());
      sendFrame('A', reinterpret_cast<uint8_t*>(micSamples), sizeof(micSamples));
      micPending = false;
    }
    if (!M5.BtnA.isPressed() || millis() - voiceStartedAt > 60000) {
      stopVoice();
    } else if (!micPending) {
      micPending = M5.Mic.record(micSamples, 1280, 16000, false);
      if (!micPending) stopVoice(true);
    }
  }
  if (M5.BtnA.wasReleased()) {
    if (!holdHandled && !hostless) {
      const char message[] = "BUTTON|focus";
      sendFrame('T', reinterpret_cast<const uint8_t*>(message), sizeof(message) - 1);
      buttonCue.notify(millis());
      lastTaskAt = millis();
    }
    holdHandled = false;
  }
}

Face desiredFace(uint32_t now) {
  if (voiceRecording || quakeVisible || activeCount > 0 || attentionCount > 0 || doneCount > 0)
    lastTaskAt = now;
  return avatar_behavior::select({activeCount,attentionCount,doneCount,bridgeConnected,voiceRecording},
      avatar_behavior::pending(now,celebrateUntil),now-lastTaskAt<kSleepAfterMs,
      usbPresence.sleepy(now)&&!nap.peek(now),startle.active);
}

const eye_life::Mood& lifeMood(Face current) {
  return avatar_behavior::mood(current);
}

void applyFace(Face next, uint32_t now) {
  face = next;
  faceApplied = true;
  if(next==Face::waiting||next==Face::listening||next==Face::offline)celebrateUntil=now;
  eyeLife.reschedule(now, lifeMood(next));
}

// Side-fit font for a count (the cue's departing/arriving string).
const count_raster::Font& fittedFont(int count) {
  char text[10];roman_counts::format(count,text);
  int f=0;
  while(f<4&&count_raster::width(counter_labels::font(f),text)>counter_labels::kSideLimit)++f;
  return counter_labels::font(f);
}

// Resting numeral center for status slot 0 (done) / 1 (active), on the
// screen side status_sides gives it, and the shared label baseline.
float counterX(int slot) {
  return counterLabels.x(slot, max(0.0f, (kScreenWidth - ui.region) / 2.0f), ui.corners);
}
float counterY() { return lerp(kScreenHeight / 2.0f, 116, ui.corners); }

bool countCueBusy(uint32_t now) {
  return counterFlight.busy(now)||taskArrival.busy(now)||taskTransfer.busy(now)||stepOver.busy(now);
}

void updateBadge(Badge& badge, int count, int side, float seconds, uint32_t now) {
  badge.pop.observe(bridgeConnected ? count : 0);
  const bool visible = bridgeConnected && count > 0 && !voiceRecording;
  if (visible) badge.shown = count;
  badge.fontIndex = counterLabels.labels[side].index;
  badge.largeWidth = counterLabels.labels[side].width;
  // Body reflow still uses side-strip widths, never the expanded bottom text.
  badge.width = approach(badge.width, max(kCountSideWidth, counterLabels.sideWidth(side) + kCountPadding),
                         seconds, 0.14f);
  badge.presence = approach(badge.presence, visible ? 1 : 0, seconds, 0.12f);
  // Fade digits after the oval has opened enough space. Keep the last count
  // during disappearance instead of flashing a zero.
  const float exposed = (kScreenWidth - ui.region) / 2.0f;
  const bool fits = ui.corners > .8f || exposed >= counterLabels.sideWidth(side) + kCountPadding / 2.0f;
  badge.label = approach(badge.label, visible && fits ? 1 : 0, seconds, 0.07f);
  badge.pop.update(now,visible && badge.label > .55f && badge.presence > .9f);
}

void updateBadges(float seconds, uint32_t now) {
  const bool sorting=countCueBusy(now);
  const int shownDone=stepOver.displayCount(now,
      taskTransfer.displayCount(now,true,counterFlight.displayCount(now,true,doneCount)));
  const int shownActive=taskTransfer.displayCount(now,false,counterFlight.displayCount(now,false,activeCount));
  // Staged cues replace count pops, which would compete with them.
  if(sorting) {
    ui.done.pop.observed=shownDone;ui.active.pop.observed=shownActive;
    ui.done.pop.running=ui.active.pop.running=false;
    ui.done.pop.pending=ui.active.pop.pending=false;
  }
  if(bridgeConnected&&!voiceRecording) {
    if(shownDone>0)ui.done.shown=shownDone;
    if(shownActive>0)ui.active.shown=shownActive;
  }
  const bool beforeSwap=sorting&&!counterFlight.swapped(now);
  // Keep the old glyph during its fade, but do not reserve bottom space for
  // a fully hidden stale count (notably a single-label request layout).
  const int measuredDone=shownDone>0||ui.done.presence>.01f?ui.done.shown:0;
  const int measuredActive=shownActive>0||ui.active.presence>.01f?ui.active.shown:0;
  counterLabels.update(measuredDone,measuredActive,ui.corners,
      sorting?(beforeSwap?doneCount:counterFlight.oldDone):-1,
      sorting?(beforeSwap?activeCount:counterFlight.oldActive):-1);
  updateBadge(ui.done, shownDone, 0, seconds, now);
  updateBadge(ui.active, shownActive, 1, seconds, now);
}

// Consume sound before selecting the face: otherwise sleeping selection and
// layout flatten the cue's open eyes. This only wakes visuals, never task state.
void updateStartle(uint32_t now) {
  const bool allowed=avatar_behavior::canStartle(face,usbPresence.unplugged,
      voiceRecording,countCueBusy(now),buttonCue.active);
  startle.update(now,!allowed);
  if(pendingStartle>0) {
    if(allowed&&!startle.active) {
      startle.notify(now,pendingStartle,random(2)?1:-1);
      lastTaskAt=now;  // Return to idle sleep after the ordinary four-second hold.
      surface.kick(0,.55f+.45f*startle.strength);
    }
    pendingStartle=0;
  }
}

void updateGaze(float seconds, uint32_t now) {
  const bool scripted=avatar_behavior::scripted(face);
  const bool taskActing=face==Face::working||face==Face::finished;
  buttonCue.update(now,face==Face::listening||face==Face::sleeping||face==Face::waiting);
  counterFlight.update(now,taskActing&&bridgeConnected&&!buttonCue.active);
  taskArrival.update(now,taskActing&&bridgeConnected&&!buttonCue.active&&!voiceRecording);
  taskTransfer.update(now,taskActing&&bridgeConnected&&!buttonCue.active&&!voiceRecording);
  // The step runs into idle (or a lingering celebration), never into a
  // request, listening or offline face.
  stepOver.update(now,bridgeConnected&&!buttonCue.active&&!voiceRecording&&attentionCount==0&&
      face!=Face::listening&&face!=Face::offline&&face!=Face::waiting);
  if(!bridgeConnected) {
    counterFlight.observe(now,0,0,false,false);
    taskArrival.observe(now,0,false,false);taskTransfer.observe(now,0,0,false,false);
    stepOver.observe(now,0,0,false,false);
  }
  const bool staged=countCueBusy(now);
  // Current face priority can change in this frame (e.g. dictation begins).
  startle.update(now,!avatar_behavior::canStartle(face,usbPresence.unplugged,
      voiceRecording,staged,buttonCue.active));
  const auto targets=avatar_behavior::targets(120+ui.offset+scene.counterShift,avatar_scene::eyeBaseline(ui.corners,ui.alert,0),
      counterX(0),counterX(1),counterY(),doneCount>0,activeCount>0);
  actor.calm=!avatar_behavior::pending(now,celebrateUntil);
  const auto performance=director.step(face,actor,now,static_cast<uint32_t>(random(0x7fffffff)),
      counterChecks.busy(now),targets,buttonCue.active||staged||startle.active);
  actingEyes=performance.eyes;actingBody=performance.body;
  const auto authoredEyes=actingEyes;
  if(performance.transitioning||buttonCue.active||staged||startle.active)countReaction.active=false;
  counterChecks.update(now,taskActing&&!performance.transitioning&&!staged&&
      !countReaction.active&&!buttonCue.active&&!startle.active,&surface,doneCount>0,activeCount>0,
      !scripted||actor.resting(now));
  actingBody=counterChecks.apply(actingBody,now,true);
  actingEyes=counterChecks.apply(actingEyes,now);
  actingBody=counterFlight.apply(actingBody,now,true);
  actingEyes=counterFlight.apply(actingEyes,now);
  actingBody=taskArrival.apply(actingBody,now,true);
  actingEyes=taskArrival.apply(actingEyes,now);
  actingBody=taskTransfer.apply(actingBody,now,true,targets.doneX,targets.doneY);
  actingEyes=taskTransfer.apply(actingEyes,now,false,targets.doneX,targets.doneY);
  actingBody=stepOver.apply(actingBody,now,true);
  actingEyes=stepOver.apply(actingEyes,now);
  actingBody=buttonCue.apply(actingBody,now,true);
  actingEyes=buttonCue.apply(actingEyes,now);
  actingBody=startle.apply(actingBody,now,true);
  actingEyes=startle.apply(actingEyes,now);
  requestHops.update(now,face==Face::waiting,!buttonCue.active&&!startle.active,
                     static_cast<uint32_t>(random(0x7fffffff)));
  if(face!=Face::listening&&face!=Face::sleeping&&face!=Face::offline) {
    actingBody=countReaction.body(actingBody,now,face==Face::waiting);
    actingEyes=countReaction.apply(actingEyes,now,face==Face::waiting);
  } else countReaction.active=false;
  const float rate=face==Face::waiting?.95f:face==Face::finished?.55f:.60f;
  flowClock=fluid_face::advance(flowClock,seconds*rate*actingBody.flow);
  if(performance.transitioning) {
    eyeLife.weight=0;eyeLife.blinking=false;
  } else {
    const auto segment=scripted?actor.segment(now):eye_acting::Segment{};
    const bool cued=!eye_acting::sameEyes(authoredEyes,actingEyes);
    const bool still=face!=Face::sleeping&&ui.sleep<.5f&&!cued&&(scripted?segment.still:true);
    actingEyes=eyeLife.apply(actingEyes,now,seconds,lifeMood(face),still,
        face!=Face::sleeping&&ui.sleep<.3f,scripted?&segment:nullptr);
  }
  director.capture(now,actingEyes,actingBody);
  // One thud when a dropped numeral lands; one when the face hops home.
  if(taskArrival.consumeImpact(now))surface.kick(0,.75f);
  if(taskTransfer.consumeHome(now))surface.kick(0,.9f);
  if(const float thud=stepOver.consumeImpact(now))surface.kick(stepOver.side,thud);
  surface.step(seconds,actingBody,ui.corners>.5f&&ui.sleep<.8f);
}

bool counterContactReady(const Badge& badge) {
  return bridgeConnected && badge.label > .9f && badge.presence > .95f &&
         (ui.corners > .95f || ui.single > .95f) &&
         !voiceRecording && face != Face::waiting;
}

void updateLayout(float seconds, uint32_t now) {
  updateBadges(seconds, now);
  const float presence = max(ui.done.presence, ui.active.presence);
  const float sideWidth = max(ui.done.width * ui.done.presence,
                              ui.active.width * ui.active.presence);
  // The wider label controls the centered oval's width. When only one count
  // remains, expand into the unused side without moving the remaining label.
  const float regionTarget = lerp(280, kScreenWidth - 2 * sideWidth, presence);
  ui.region = approach(ui.region, regionTarget, seconds, 0.16f);
  ui.side = approach(ui.side, presence, seconds, 0.16f);
  // A charging transfer keeps its old layout until the flash hides the swap.
  const int layoutDone = stepOver.displayCount(now, taskTransfer.displayCount(now, true, doneCount));
  const int layoutActive = taskTransfer.displayCount(now, false, activeCount);
  const bool doneOnly = !counterFlight.busy(now) && bridgeConnected && layoutDone > 0 && layoutActive == 0 &&
                        attentionCount == 0 && !voiceRecording;
  const bool activeOnly = !counterFlight.busy(now) && bridgeConnected && layoutActive > 0 && layoutDone == 0 &&
                          attentionCount == 0 && !voiceRecording;
  // Reclaim the disappearing numeral's space, independently of its retained
  // backdrop: that backdrop can only leave once the face has covered it.
  const float singleTarget = doneOnly ? ui.done.presence * (1 - ui.active.label) :
                             activeOnly ? ui.active.presence * (1 - ui.done.label) : 0;
  ui.single = approach(ui.single, singleTarget, seconds, avatar_scene::kShiftSeconds);
  // A single counter: the face fills the opposite side of the screen.
  const float offsetTarget = (doneOnly ? status_sides::kActive : activeOnly ? status_sides::kDone : 0) *
                             sideWidth / 2 * singleTarget;
  ui.offset = approach(ui.offset, offsetTarget, seconds, avatar_scene::kShiftSeconds);
  const bool bothCounts = bridgeConnected && !voiceRecording &&
                          ((layoutDone > 0 && layoutActive > 0) || counterFlight.busy(now));
  ui.corners = approach(ui.corners, bothCounts || face == Face::waiting ? 1 : 0,
                        seconds, avatar_scene::kReflowSeconds);
  ui.waiting = approach(ui.waiting, attentionCount > 0 ? 1 : 0, seconds, 0.12f);
  ui.alert = approach(ui.alert, face == Face::waiting ? 1 : 0, seconds, 0.18f);
  ui.boost = approach(ui.boost, voiceRecording ? 1 : 0, seconds, 0.10f);
  // Fall asleep slowly; wake up quickly.
  const bool asleep = face == Face::sleeping;
  ui.sleep = approach(ui.sleep, asleep ? 1 : 0, seconds,
                      asleep ? 0.7f : startle.active ? 0.035f : 0.18f);

  updateGaze(seconds, now);
  const float compactWidth = constrain((ui.region - 48) / 2.0f, 28.0f, 36.0f);
  const float compact = ui.side * (1 - ui.single) * (1 - ui.corners);
  ui.awakeWidth = lerp(50, compactWidth, compact);
  ui.awakeHeight = lerp(lerp(56,42,compact),44,ui.alert);
  const float sleepingBreath = sinf((now % 126000) * (2.0f * PI / 4500.0f));
  const float widthTarget = ui.awakeWidth * actingEyes.width * lerp(1, 0.65f, ui.sleep) *
                            (1 + 0.025f * sleepingBreath * ui.sleep);
  const float heightTarget = fmaxf(avatar_scene::kRequestEyeFloor*ui.alert,
      lerp(ui.awakeHeight * actingEyes.height * (1 + ui.boost * 0.16f),
           4 + 0.4f * sleepingBreath, ui.sleep));
  eyeSize.step(seconds, widthTarget, heightTarget);
  ui.eyeWidth = eyeSize.width;
  ui.eyeHeight = eyeSize.height;
  const float breath = sleepingBreath * 2 * ui.sleep;
  const float gazeRoom = lerp(1,.55f,compact) * (1-ui.sleep);
  scene.region=ui.region; scene.side=ui.side; scene.corners=ui.corners;
  scene.offset=ui.offset; scene.alert=ui.alert; scene.sleep=ui.sleep;
  scene.eyeWidth=ui.eyeWidth; scene.eyeHeight=ui.eyeHeight;
  scene.awakeWidth=ui.awakeWidth; scene.awakeHeight=ui.awakeHeight;
  scene.gap=lerp(19,12,compact)*lerp(1,.7f,ui.sleep)*lerp(1,.8f,ui.alert);
  scene.eyeX=120+ui.offset+actingEyes.x*gazeRoom+surface.x*.45f*(1-ui.sleep);
  scene.eyeY=avatar_scene::eyeBaseline(ui.corners,ui.alert,ui.sleep,breath)+
             actingEyes.y*gazeRoom+surface.y*.55f*(1-ui.sleep);
  scene.hop=requestHops.height(now)*ui.alert;
  scene.hopStretch=requestHops.stretch(now)*ui.alert;
  scene.eyes=actingEyes; scene.body=actingBody; scene.flowClock=flowClock;
  listenWave.update(seconds, now, face == Face::listening,
                    face == Face::listening ? voiceLevel.level(now) : 0);
  avatar_scene::setWave(scene, listenWave);
  scene.thinking=face==Face::working;
  scene.surface=&surface;
  counterSpace.update(scene,counterFlight.displayCount(now,true,doneCount),
      counterFlight.displayCount(now,false,activeCount),counterLabels.sideWidth(0),counterLabels.sideWidth(1),
      seconds,bridgeConnected&&!voiceRecording&&face!=Face::waiting&&face!=Face::sleeping);
  const float doneX = counterX(0);
  const float activeX = counterX(1);
  const float countY = counterY();
  // Leave/re-enter and dodge translate the whole character; zero is exact.
  taskTransfer.source(now, activeX, countY);
  taskTransfer.animate(now, seconds, 120 + ui.offset + scene.counterShift, kDone);
  // Clear the falling numeral's column: the wide paired face needs far more
  // than the authored 20px, while the outer eye stays on screen.
  float dodgeRoom = task_cues::Arrival::kDodge;
  if (taskArrival.busy(now)) {
    const count_raster::Font& toFont = *kCountFonts[ui.active.fontIndex];
    const count_raster::Font& fromFont = &fittedFont(taskArrival.from) == &fittedFont(taskArrival.to) ?
        toFont : fittedFont(taskArrival.from);
    dodgeRoom = task_cues::Arrival::room(scene, taskArrival.fallInner(fromFont, toFont, activeX),
        countY - toFont.capHeight / 2.0f);
  }
  const float arrivalDodge = taskArrival.dodge(now, dodgeRoom);
  scene.eyeX += task_cues::Arrival::eyeReturn(arrivalDodge, scene.eyeX + scene.counterShift,
      (scene.gap / 2 + scene.eyeWidth) * scene.counterScale);
  scene.away = arrivalDodge + taskTransfer.awayValue;
  stepOver.animate(now, seconds);
  scene.stepSide = stepOver.side;
  scene.stepProgress = stepOver.reachValue;
  // Sorting blobs follow the same carried numeral positions drawBadge uses.
  counterFlight.hands(now, doneX, activeX, ui.done.largeWidth, ui.active.largeWidth,
                      countY, kCountFonts[ui.done.fontIndex]->capHeight,
                      kCountFonts[ui.active.fontIndex]->capHeight, scene.hands);
  counterChecks.shape(scene, now, doneX, activeX, countY,
                      ui.done.largeWidth, ui.active.largeWidth,
                      kCountFonts[ui.done.fontIndex]->capHeight,
                      kCountFonts[ui.active.fontIndex]->capHeight,
                      counterContactReady(ui.done), counterContactReady(ui.active));
}

status_background::SortingDeparture backgroundDeparture;

status_background::State statusBackgroundState(uint32_t now) {
  // A charging completion pulses energetic green light through the blue.
  const uint16_t activeFill=blend565(blend565(kWorking,kBlocked,ui.waiting),task_cues::kEnergy,
                                     taskTransfer.glow(now));
  const bool portrait=ui.alert>.01f && !(doneCount>0 && activeCount>0);
  return {kDone,activeFill,doneCount>0?kDone:activeFill,
          backgroundDeparture.presence[0],backgroundDeparture.presence[1],portrait?ui.alert:0,
          taskTransfer.washTint,taskTransfer.washValue,
          request_scene::field(now),ui.alert};
}

uint16_t statusBackgroundAt(int x,int y) {
  return static_cast<uint16_t>(statusRows[y&3][x]);
}

void updateCounterContact(Badge& badge, int slot, float seconds, uint32_t now) {
  const float side = status_sides::slotDirection(slot);
  const float depth = avatar_interaction::contactDepth(faceMask, counterX(slot), counterY(), badge.largeWidth,
                                                       kCountFonts[badge.fontIndex]->capHeight,
                                                       ui.corners, side);
  // Flight is authored travel, never collision input. Rebaseline on landing.
  badge.spring.update(seconds, depth, counterContactReady(badge) && !countCueBusy(now), side);
}

void drawBadge(Badge& badge, int slot, uint32_t now) {
  if (badge.presence < 0.01f) return;

  const bool done = slot == status_sides::kDoneSlot;
  const float settled = constrain((badge.presence - 0.8f) / 0.2f, 0.0f, 1.0f);
  // Requests hide the counts (visually only) behind the pulsing field.
  const float alpha = badge.label * settled * taskTransfer.labelAlpha(now, done) * (1 - ui.alert);
  // A dropping numeral needs only its backdrop, not the faded-in resting label.
  const bool dropping = !done && taskArrival.busy(now);
  if (alpha < 0.02f && !(dropping && settled > 0.02f)) return;
  // Position in the exposed strip, not in the colored half hidden by the face.
  const Badge& other = done ? ui.active : ui.done;
  const float restX = counterX(slot);
  const auto flight = counterFlight.offset(now, done, restX, counterX(1 - slot),
                                           badge.largeWidth, other.largeWidth);
  const float centerX = restX + flight.x;
  const float centerY = counterY() + badge.pop.lift(now) + flight.y;
  const count_raster::Font& font = *kCountFonts[badge.fontIndex];
  char label[10];
  roman_counts::format(badge.shown, label);
  if(done&&!countCueBusy(now))director.accents(now,centerX,centerY-font.capHeight/2-2,
      [&](int x,int y,float a){inkFrame(x,y,kDone,a*alpha);});  // alpha includes the request fade
  // Restore the exact status background in the outline, including its gradient.
  // It is a backdrop to SOLID black letters, not a white border or hollow font.
  // Draw last so freely flapping face edges may overlap without hiding counts.
  if(dropping) {
    // Resting layout font for the new label; keep extensions in one font.
    const count_raster::Font& fromFont=&fittedFont(taskArrival.from)==&fittedFont(taskArrival.to)?
        font:fittedFont(taskArrival.from);
    taskArrival.draw(now,fromFont,font,centerX,centerY,
        [&](const count_raster::Font& f,const char* text,float x,float y,bool flying) {
      (flying?flyingRaster:badge.raster).render(f,text,x,y,[&](int px,int py,float coverage,bool outline) {
        if(px<0||px>=kScreenWidth||py<0||py>=kScreenHeight)return;
        inkFrame(px,py,outline?statusBackgroundAt(px,py):kBlack,settled*coverage);
      });
    },[&](int x,int y,float a){
      if(x>=0&&x<kScreenWidth&&y>=0&&y<kScreenHeight)inkFrame(x,y,task_cues::kFlash,a);
    });
    return;
  }
  const float shakeX = done ? 0 : taskTransfer.shakeX(now);
  const float shakeY = done ? 0 : taskTransfer.shakeY(now);
  // A stepping foot covers the numeral rather than the label floating above it.
  const bool stepped = faceMask.stepSide != 0;
  badge.raster.render(font,label,centerX,centerY,[&](int x,int y,float coverage,bool outline) {
    if(x<0||x>=kScreenWidth||y<0||y>=kScreenHeight)return;
    const float uncovered = stepped ? 1 - faceMask.stepCoverage(x,y) : 1;
    inkFrame(x,y,outline?statusBackgroundAt(x,y):kBlack,alpha*coverage*uncovered);
  },badge.spring.x+shakeX,badge.spring.y+shakeY);  // Slow contact springs move sub-pixel.
}

// Transfer overlays: charge crackles, the flipping done numeral and the
// cartoon starburst. All are event-only and draw nothing otherwise.
void drawTaskCues(uint32_t now) {
  if(!taskTransfer.busy(now))return;
  const auto light=[&](int x,int y,float a) {
    if(x>=0&&x<kScreenWidth&&y>=0&&y<kScreenHeight)inkFrame(x,y,task_cues::kFlash,a);
  };
  taskTransfer.crackles(now,light);
  taskTransfer.flip(now,*kCountFonts[ui.done.fontIndex],counterX(0),counterY(),
      [&](const count_raster::Font& f,const char* text,float x,float y,float scale,float a) {
    count_raster::renderScaled(f,text,x,y,scale,[&](int px,int py,float coverage,bool outline) {
      if(px<0||px>=kScreenWidth||py<0||py>=kScreenHeight)return;
      inkFrame(px,py,outline?statusBackgroundAt(px,py):kBlack,a*coverage);
    });
  },light);
  const uint16_t stored=framePixels.swapped?framebuffer_raster::swap(task_cues::kFlash):task_cues::kFlash;
  taskTransfer.burst(now,[&](int x,int y,int length) {
    if(framePixels.pixels) {
      uint16_t* row=framePixels.pixels+y*framePixels.stride+x;
      for(int i=0;i<length;++i)row[i]=stored;
    } else frame().drawFastHLine(x,y,length,task_cues::kFlash);
  },light);
}

void drawIndicators(uint32_t now) {
  // Four static dither rows remove RGB565 banding. Typed RGB565 pixels avoid
  // byte-order ambiguity; row blits keep the gradient cheap on the device.
  statusCache.update(statusBackgroundState(now),[&](int x,int y,uint16_t color) {
    statusRows[y][x]=lgfx::rgb565_t(color);
    statusStored[y][x]=framebuffer_raster::swap(color);
  });
  // Direct row copies into the checked packed sprite; 135 pushImage calls each
  // paid clipping/pixel-copy setup. The API path remains the fallback.
  if(framePixels.pixels && framePixels.swapped && framePixels.stride==kScreenWidth) {
    for(int y=0;y<kScreenHeight;++y)
      memcpy(framePixels.pixels+y*kScreenWidth,statusStored[y&3],sizeof(statusStored[0]));
    return;
  }
  for(int y=0;y<kScreenHeight;++y) frame().pushImage(0,y,kScreenWidth,1,statusRows[y&3]);
}

void drawFaceBackground() {
  // Skip whole opaque blocks while preserving individual travelling flaps
  // and exact fractional edge coverage. Counts still render above the face.
  faceMask.raster([&](int x,int y,int length) {
    // Black is 0 in either byte order: clear the opaque run in place.
    if(framePixels.pixels)memset(framePixels.pixels+y*framePixels.stride+x,0,length*sizeof(uint16_t));
    else frame().drawFastHLine(x,y,length,kBlack);
  },[&](int x,int y,float alpha) {
    inkFrame(x,y,kBlack,alpha);
  });
}

// Close the open transfer: waits for the previous frame's DMA to finish.
void finishTransfer() {
  if (!transferOpen) return;
  M5.Display.endWrite();
  transferOpen = false;
}

// Queue the finished frame and, when double buffered, return while DMA sends
// it; the next frame renders into the other buffer. A frame buffer is never
// written while DMA reads it: the previous transfer completes before its
// buffer comes back around.
void presentFrame() {
  finishTransfer();
  M5.Display.startWrite();
  transferOpen = true;
  frame().pushSprite(0, 0);
  if (doubleBuffered) frameCanvas = frameCanvas == &canvases[0] ? &canvases[1] : &canvases[0];
  else finishTransfer();
}

void renderFrame(uint32_t now) {
#if STICK_S3_PROFILE
  profileBegin(now);
#endif
  // Installed M5GFX 0.2.32 uses packed swap565 storage at rotation zero.
  // Guard every assumption; unsupported layouts retain the public API path.
  framePixels.pixels=nullptr;
  if(frame().getRotation()==0 && frame().getColorDepth()==lgfx::rgb565_2Byte &&
     frame().width()==kScreenWidth && frame().height()==kScreenHeight &&
     frame().bufferLength()>=kScreenWidth*kScreenHeight*2) {
    framePixels.pixels=static_cast<uint16_t*>(frame().getBuffer());
    framePixels.width=kScreenWidth;framePixels.height=kScreenHeight;
    framePixels.stride=kScreenWidth;framePixels.swapped=true;
  }
  const float seconds = lastFrameAt ? constrain((now - lastFrameAt) / 1000.0f, 0.0f, 0.1f) : 0;
  lastFrameAt = now;

  updateStartle(now);
  const Face next = desiredFace(now);
  if (!faceApplied || next != face) applyFace(next, now);

  updateLayout(seconds, now);
  const uint8_t nextBrightness = lroundf(lerp(kAwakeBrightness, kSleepBrightness, ui.sleep));
  if (nextBrightness != brightness) {
    brightness = nextBrightness;
    M5.Display.setBrightness(brightness);
  }
  STICK_PROFILE_MARK(update);
  const float singleOffset=max(ui.done.width*ui.done.presence,
                               ui.active.width*ui.active.presence)/2;
  const bool reclaimed=ui.corners<.05f && ui.single>.95f &&
                       fabsf(ui.offset)>.95f*singleOffset;
  backgroundDeparture.update(ui.done.presence,ui.active.presence,
      counterFlight.busy(now),doneCount>0,activeCount>0,
      reclaimed&&ui.offset*status_sides::kDone>0,reclaimed&&ui.offset*status_sides::kActive>0,
      bridgeConnected&&!voiceRecording&&attentionCount==0,seconds);
  drawIndicators(now);
  // The request `!` sits in the pulsing field; the rising head may cover it.
  if (ui.alert > .01f) {
    request_scene::sign(now,[&](int x,int y,float alpha) {
      if(x>=0 && x<kScreenWidth && y>=0 && y<kScreenHeight) inkFrame(x,y,kBlack,alpha*ui.alert);
    });
  }
  STICK_PROFILE_MARK(background);
  avatar_scene::buildMask(scene,faceMask);
  updateCounterContact(ui.done, status_sides::kDoneSlot, seconds, now);
  updateCounterContact(ui.active, status_sides::kActiveSlot, seconds, now);
  STICK_PROFILE_MARK(mask);
  drawFaceBackground();
  STICK_PROFILE_MARK(face);
  // Opaque interior eye runs are filled directly; AA edges stay per-pixel.
  avatar_scene::eyes(scene,faceMask,[&](int x,int y,float alpha) {
    inkFrame(x,y,kEye,alpha);
  },[&](int x,int y,int length) {
    if(framePixels.pixels) {
      uint16_t* row=framePixels.pixels+y*framePixels.stride+x;
      const uint16_t stored=framePixels.swapped?framebuffer_raster::swap(kEye):kEye;
      for(int i=0;i<length;++i)row[i]=stored;
    } else {
      frame().drawFastHLine(x,y,length,kEye);
    }
  });
  STICK_PROFILE_MARK(eyes);
  avatar_scene::mouth(scene,faceMask,[&](int x,int y,float alpha) {
    inkFrame(x,y,kEye,alpha);
  });
  STICK_PROFILE_MARK(mouth);
  STICK_PROFILE_MARK(attention);
  drawBadge(ui.done,status_sides::kDoneSlot,now);
  drawBadge(ui.active,status_sides::kActiveSlot,now);
  drawTaskCues(now);
  STICK_PROFILE_MARK(badges);
  presentFrame();
#if STICK_S3_PROFILE
  profileFinish();
#endif
}

void startAttentionChime() {
  attentionChimePlaying = true;
  attentionChimeStep = 0;
  nextAttentionChimeAt = millis();
}

void updateAttentionChime() {
  if (!attentionChimePlaying ||
      static_cast<int32_t>(millis() - nextAttentionChimeAt) < 0) {
    return;
  }

  if (attentionChimeStep == 0) {
    M5.Speaker.tone(1047, 70);
    attentionChimeStep = 1;
    nextAttentionChimeAt = millis() + 105;
  } else if (attentionChimeStep == 1) {
    M5.Speaker.tone(1568, 110);
    attentionChimeStep = 2;
    nextAttentionChimeAt = millis() + 130;
  } else {
    attentionChimePlaying = false;
  }
}

bool completionPairingReady(int nextAttentionCount) {
  return bridgeConnected && !voiceRecording && nextAttentionCount == 0 && attentionCount == 0 &&
         !buttonCue.active && ui.active.label > .9f && ui.active.presence > .95f;
}

void applyCounts(int nextActiveCount, int nextAttentionCount, int nextDoneCount, uint32_t now) {
  const bool attentionStarted =
      attentionCount == 0 && nextAttentionCount > 0;
  const bool sorting=counterFlight.observe(now,nextDoneCount,nextActiveCount,bridgeConnected,
      !voiceRecording && nextAttentionCount==0 && ui.corners>.95f &&
      ui.done.label>.9f && ui.active.label>.9f && ui.done.presence>.95f &&
      ui.active.presence>.95f && !ui.done.pop.running && !ui.active.pop.running &&
      !countReaction.active && !buttonCue.active);
  if(sorting) {
    counter_labels::State bounds;
    bounds.update(nextDoneCount,nextActiveCount,1,doneCount,activeCount);
    counterFlight.fit(bounds.flightWidth(0),bounds.flightWidth(1));
  }
  // Staged cues: same priority edges as sorting; recovery is a baseline.
  const bool cueReady=!voiceRecording && nextAttentionCount==0 && !buttonCue.active && !sorting;
  const bool transferred=taskTransfer.observe(now,nextDoneCount,nextActiveCount,bridgeConnected,
      cueReady && ui.active.label>.9f && ui.active.presence>.95f);
  const bool arrived=taskArrival.observe(now,nextActiveCount,bridgeConnected,
      cueReady && !transferred && !taskTransfer.busy(now));
  // The last unread completion was opened: step over the settled done strip.
  stepOver.observe(now,nextDoneCount,nextActiveCount,bridgeConnected,
      cueReady && !transferred && !arrived && !countCueBusy(now) && ui.corners<.05f &&
      ui.done.label>.9f && ui.done.presence>.95f && ui.active.presence<.01f);
  // Recovery establishes a baseline, even when the first snapshot is paired.
  if(!bridgeConnected) {
    counterFlight.observe(now,nextDoneCount,nextActiveCount,true,false);
    taskTransfer.observe(now,nextDoneCount,nextActiveCount,true,false);
    taskArrival.observe(now,nextActiveCount,true,false);
    stepOver.observe(now,nextDoneCount,nextActiveCount,true,false);
  }
  if(bridgeConnected && !sorting && !transferred && !arrived && !voiceRecording && nextAttentionCount==0) {
    // React toward the counter that changed.
    if(nextDoneCount>doneCount) {
      surface.kick(status_sides::kDone,.95f);
      countReaction.notify(status_sides::kDone,true,now);
    } else if(nextActiveCount>0 && nextActiveCount!=activeCount) {
      const bool arrival=nextActiveCount>activeCount;
      surface.kick(status_sides::kActive,arrival?.90f:.35f);
      countReaction.notify(status_sides::kActive,false,now,arrival);
    } else if(activeCount>0 && nextActiveCount==0 && nextDoneCount>0) {
      surface.kick(status_sides::kDone,.65f);
      countReaction.notify(status_sides::kDone,true,now);
    }
  }
  if (bridgeConnected && !voiceRecording && nextAttentionCount==0 && nextDoneCount > doneCount) {
    director.notifyCompletion();
    // After a transfer, celebrate once the face is home rather than offscreen.
    celebrateUntil = now + 3600 + (transferred ? task_cues::Transfer::kEnd : 0);
  }
  activeCount = nextActiveCount;
  attentionCount = nextAttentionCount;
  doneCount = nextDoneCount;
  bridgeConnected = true;
  if (attentionStarted) {
    surface.kick(0,.65f);
    countReaction.active=false;
    startAttentionChime();
  }
}

void processLine(const String& line) {
  if (!line.startsWith("C|")) return;
  int cursor = 2;
  int separator = line.indexOf('|', cursor);
  if (separator < 0) return;
  const int nextActiveCount = line.substring(cursor, separator).toInt();
  cursor = separator + 1;
  separator = line.indexOf('|', cursor);
  if (separator < 0) return;
  const int nextAttentionCount = line.substring(cursor, separator).toInt();
  cursor = separator + 1;
  separator = line.indexOf('|', cursor);
  const int nextDoneCount = line.substring(cursor, separator < 0 ? line.length() : separator).toInt();
  // Missing/unknown presence means hidden (compatible with legacy hosts).
  quakeVisible = separator >= 0 && line.substring(separator + 1) == "1";
  const uint32_t now=millis();
  // One completion can arrive as two polls (active falls, then done rises, or
  // the reverse). Present the first half only once its pair arrives or the
  // window expires, so the transfer cue sees both edges together.
  int pairedActive = nextActiveCount, pairedDone = nextDoneCount;
  completionPairing.filter(now, doneCount, activeCount, pairedDone, pairedActive,
      completionPairingReady(nextAttentionCount));
  applyCounts(pairedActive, nextAttentionCount, pairedDone, now);
  lastMessageAt = millis();
  usbPresence.hostSeen();
}


uint32_t lastImuPollAt = 0;
constexpr uint32_t kImuPollMs = 40;  // Orientation needs ~25Hz, not every loop pass.

void updateOrientation() {
  const uint32_t now = millis();
  if (now - lastImuPollAt < kImuPollMs) return;
  lastImuPollAt = now;
  if (now < manualRotationUntil || !M5.Imu.update()) {
    return;
  }

  const auto data = M5.Imu.getImuData();
#if STICK_S3_AMBIENT_STARTLE
  // Picking the Stick up or tapping it is loud at the microphone.
  if (fabsf(data.gyro.x) + fabsf(data.gyro.y) + fabsf(data.gyro.z) > 45.0f) lastHandlingAt = now;
#endif
  const float elapsedSeconds = lastImuAt ? (now - lastImuAt) / 1000.0f : 0;
  lastImuAt = now;
  int nextRotation = rotation;
  const float horizontal = abs(data.accel.x) > abs(data.accel.y)
                               ? data.accel.x
                               : data.accel.y;
  if (horizontal > 0.55f) nextRotation = 1;
  if (horizontal < -0.55f) nextRotation = 3;

  if (abs(data.accel.z) > 0.72f && abs(data.gyro.z) > 18.0f) {
    flatTurnDegrees += data.gyro.z * elapsedSeconds;
    if (abs(flatTurnDegrees) > 105.0f) {
      nextRotation = rotation == 1 ? 3 : 1;
      flatTurnDegrees = 0;
      manualRotationUntil = now + 1200;
    }
  } else if (abs(data.gyro.z) < 8.0f) {
    flatTurnDegrees *= 0.92f;
  }
  if (nextRotation != rotation) {
    rotation = nextRotation;
    finishTransfer();  // Never change MADCTL while a frame is in flight.
    M5.Display.setRotation(rotation);
  }
}

}  // namespace

bool usbHostPresent() {
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  return HWCDC::isPlugged();  // USB host start-of-frame activity.
#else
  return true;  // No SOF signal: rely on the heartbeat timeout (offline face).
#endif
}

// USB VBUS (host or charger) from the StickS3 PMIC. Unknown counts as powered,
// so a failed read never puts a possibly connected Stick to sleep.
bool hostPowerPresent() {
  uint8_t source = 0;
  if (!M5.Power.M5pm1.readRegister(0x04, &source, 1)) return true;  // PWR_SRC
  return source & (m5::M5PM1_Class::vin | m5::M5PM1_Class::vinout);
}

constexpr gpio_num_t kWakeButtons[] = {GPIO_NUM_11, GPIO_NUM_12};  // Front A, side B.

void enterNap(uint32_t now) {
  napStartedAt = now;
  attentionChimePlaying = false;
  stopAmbient(false);
  M5.Speaker.end();
  finishTransfer();  // Release the panel bus; waits for any in-flight frame.
  M5.Display.waitDMA();
  M5.Display.sleep();  // Backlight off, ST7789 sleep-in; panel RAM is kept.
}

void leaveNap() {
  const uint32_t slept = millis() - napStartedAt;
  if (slept < power_nap::kMinPanelSleepMs) delay(power_nap::kMinPanelSleepMs - slept);
  // Shows the retained sleeping frame first, so waking animates from it.
  M5.Display.wakeup();
  M5.Speaker.begin();
  lastImuAt = 0;  // Do not integrate gyro rate across the nap.
}

// Light-sleeps until a button or the poll timer; true when a button woke it.
// IDF detaches the USB-Serial/JTAG pads during light sleep and restores them.
bool lightNap() {
  for (const gpio_num_t pin : kWakeButtons) gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(power_nap::kPollMs) * 1000);
  esp_light_sleep_start();
  const bool button = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  for (const gpio_num_t pin : kWakeButtons) gpio_wakeup_disable(pin);
  return button;
}

// Returns true while napping, after sleeping for this loop iteration.
bool updateNap(uint32_t now) {
  if (!napSupported) return false;
  const bool unplugged = usbPresence.unplugged;
  const bool held = M5.BtnA.isPressed() || M5.BtnB.isPressed();
  if (unplugged && (held || M5.BtnA.wasReleased() || M5.BtnB.wasReleased())) nap.poke(now);
  const bool wasNapping = nap.napping;
  const bool asleep = face == Face::sleeping && ui.sleep >= power_nap::kAsleep;
  if (!nap.update(now, unplugged, held || voiceRecording, asleep, hostPowerPresent)) {
    if (wasNapping) leaveNap();
    return false;
  }
  if (!wasNapping) enterNap(now);
  if (lightNap()) {
    nap.poke(millis());
    leaveNap();
  }
  return true;
}

void setup() {
  auto config = M5.config();
  config.serial_baudrate = 115200;
  config.fallback_board = m5::board_t::board_M5StickS3;
  config.internal_spk = true;
  config.internal_mic = true;
  M5.begin(config);
#if STICK_S3_AMBIENT_STARTLE
  {  // Keep the capture task off the render core (loopTask, priority 1, core 1).
    auto micConfig = M5.Mic.config();
    micConfig.task_pinned_core = 0;
    M5.Mic.config(micConfig);
  }
#endif
  napSupported = M5.getBoard() == m5::board_t::board_M5StickS3 &&
                 M5.Power.getType() == m5::Power_Class::pmic_m5pm1;

  M5.Speaker.setVolume(128);
  M5.Display.setRotation(rotation);
  M5.Display.setBrightness(kAwakeBrightness);
  M5.Display.fillScreen(kBlack);
  eyeLife.seed = esp_random() | 1u;  // xorshift state must be nonzero.
  eyeLife.microSeed = esp_random() | 1u;
  counterChecks.seed = esp_random() | 1u;

  for (M5Canvas& canvas : canvases) {
    canvas.setColorDepth(16);
    canvas.setTextWrap(false);
    canvas.setPsram(false);
  }
  bool firstInPsram = false;
  if (!canvases[0].createSprite(kScreenWidth, kScreenHeight)) {
#if STICK_S3_PROFILE
    timingPsramFallback=true;
#endif
    firstInPsram = true;
    canvases[0].setPsram(true);
    canvases[0].createSprite(kScreenWidth, kScreenHeight);
  }
  // Overlap needs both frames in internal DMA-capable RAM (setPsram(false)
  // allocates only there); otherwise keep the synchronous single buffer.
  doubleBuffered = !firstInPsram && canvases[1].createSprite(kScreenWidth, kScreenHeight) != nullptr;
  if (!doubleBuffered) canvases[1].deleteSprite();
  nextFrameAt = millis();
  renderFrame(millis());
}

void loop() {
  M5.update();

  static String input;
  while (Serial.available()) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\n') {
      processLine(input);
      input = "";
    } else if (value != '\r' && input.length() < 160) {
      input += value;
    }
  }

  // A sustained USB unplug drops the bridge at once instead of after the
  // heartbeat timeout, so the avatar falls asleep about a second after unplug.
  const bool unpluggedNow = usbPresence.update(usbHostPresent(), millis());
  // Like a button wake, stay awake (offline face) briefly after an unplug.
  if (unpluggedNow && napSupported) nap.poke(millis());
  // Release a held half completion once its window expires or staging stops
  // being appropriate (button, recording); counts are never dropped.
  if (completionPairing.due(millis(), completionPairingReady(attentionCount))) {
    completionPairing.reset();
    applyCounts(completionPairing.active, attentionCount, completionPairing.done, millis());
  }
  if (bridgeConnected && (unpluggedNow || (lastMessageAt && millis() - lastMessageAt > 8000))) {
    bridgeConnected = false;
    completionPairing.reset();
    activeCount = 0;
    attentionCount = 0;
    doneCount = 0;
  }

  // On battery, a settled sleeping face turns the panel off and light-sleeps.
  if (updateNap(millis())) return;

  updateVoice();
  updateOrientation();
  updateAmbient(millis());  // Before the chime: it hands the codec back to the speaker.
  if (!voiceRecording) updateAttentionChime();

  // Fixed-cadence deadlines instead of "33ms since the last start": when a
  // frame runs late the next one is not pushed later as well, and a long
  // stall resynchronises instead of bursting catch-up frames.
  const uint32_t now = millis();
  const int32_t late = static_cast<int32_t>(now - nextFrameAt);
  if (late >= 0) {
    nextFrameAt = late >= static_cast<int32_t>(kFrameMs) ? now + kFrameMs : nextFrameAt + kFrameMs;
    renderFrame(now);
  } else if (-late > 2) {
    delay(1);
  }
}
