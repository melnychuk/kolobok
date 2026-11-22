#pragma once
// The Stick avatar's whole frame: from the task counts, buttons and the clock
// to the pixels. The device (firmware.ino) owns the hardware around it: the
// panel, USB serial, microphone, speaker, IMU and naps. Everything that decides
// what a frame looks like lives here, so the device and its host previews can
// run the same code.
//
// Drawing goes through a Target, which the caller supplies:
//   void ink(int x,int y,uint16_t color,float alpha)  blend one RGB565 pixel
//   void run(int x,int y,int length,uint16_t color)   an opaque horizontal run
//   void backdrop(int x,int y,uint16_t color)         one pixel of the four
//                                                     repeating status rows changed
//   void fillBackdrop()                               paint the rows over the frame
//   void mark(frame_profile::Metric)                  a drawing stage finished
#include <math.h>
#include <stdint.h>
#include <algorithm>

#include "display_fonts.h"
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
#include "avatar_palette.h"
#include "frame_profile.h"

namespace avatar_frame {

constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 135;
constexpr int kCountSideWidth = 48;  // Minimum exposed background beside the face.
constexpr int kCountPadding = 16;
constexpr uint16_t kBlack = 0x0000;
constexpr uint32_t kSleepAfterMs = 4000;
// Arduino's PI, which the sleeping breath was tuned with.
constexpr double kPi = 3.1415926535897932384626433832795;

inline const count_raster::Font& countFont(int index) {
  static const count_raster::Font* const fonts[] = {
      &kBadgeFont, &kMediumBadgeFont, &kSmallBadgeFont, &kTinyBadgeFont, &kCompactBadgeFont};
  return *fonts[index];
}

using avatar_palette::Palette;
using Face = avatar_behavior::Mode;

inline float approach(float value, float target, float seconds, float timeConstant) {
  return avatar_scene::approach(value, target, seconds, timeConstant);
}

inline float lerp(float from, float to, float amount) {
  return from + (to - from) * amount;
}

// Arduino's constrain macro, as a function (Arduino defines that name as a macro).
inline float constrained(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

inline uint16_t blend565(uint16_t from, uint16_t to, float amount) {
  if (amount <= 0) return from;
  if (amount >= 1) return to;
  const int red = lerp((from >> 11) & 31, (to >> 11) & 31, amount) + 0.5f;
  const int green = lerp((from >> 5) & 63, (to >> 5) & 63, amount) + 0.5f;
  const int blue = lerp(from & 31, to & 31, amount) + 0.5f;
  return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

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
};

struct Avatar {
  // Set by the host before each frame or event.
  bool voiceRecording = false;  // Dictation is being captured (the listening face).
  bool unplugged = false;  // No USB host: no startle while unplugged.
  bool drowsy = false;  // USB says nobody's there and no button woke it: doze.
  // random(n): 0 .. n-1. Without one (desktop), a seeded generator keeps
  // frames reproducible.
  long (*random)(long) = nullptr;
  uint32_t randomState = 0x6d2b79f5u;
  float pendingStartle = 0;  // Detector output, consumed (or dropped) by the next frame.
  int startleSide = 0;  // -1 or 1 to choose the startle's direction; 0 picks one at random.
  // Loudness of the dictation being captured (10ms levels only).
  listen_wave::Envelope voiceLevel;
  float voiceLevelOverride = -1;  // A level to use instead (no microphone); -1: voiceLevel.

  bool lightMode = false;
  const Palette* palette;

  // Task counts as last reported, and whether a host is reporting them.
  int activeCount = 0;
  int attentionCount = 0;
  int doneCount = 0;
  bool bridgeConnected = false;
  bool quakeVisible = false;  // Presence only, never a synthetic task/count.

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
  listen_wave::Wave listenWave;
  // Staged, noticeable count cues: a new active numeral drops in; an active->done
  // completion charges, flashes and flips. Real counts still update immediately.
  task_cues::Arrival taskArrival;
  task_cues::Transfer taskTransfer;
  task_cues::StepOver stepOver;  // Opening the last completion: cover the green, never fade it.
  // Herdr may split one completion over two polls; hold the first half briefly.
  task_cues::CompletionPairing completionPairing;
  count_raster::Cache flyingRaster;
  eye_acting::Pose actingEyes;
  eye_acting::Pose actingBody;
  eye_life::Life eyeLife;
  eye_life::SizeSpring eyeSize;
  float flowClock = 0;

  Face face;
  bool faceApplied = false;
  uint32_t celebrateUntil = 0;
  uint32_t lastFrameAt = 0;
  uint32_t lastTaskAt = 0;

  Indicators ui;
  // The counts on screen: real counts held back while a cue plays.
  int shownDone = 0;
  int shownActive = 0;
  // The four repeating status rows under the face, as RGB565.
  uint16_t statusRows[4][kScreenWidth] = {};
  status_background::RowCache statusCache;
  status_background::SortingDeparture backgroundDeparture;

  Avatar()
      : palette(&avatar_palette::get(false)),
        actingEyes(eye_acting::p()),
        actingBody(eye_acting::p()),
        face(Face::offline) {}
  Avatar(const Avatar&) = delete;  // The scene points at its own surface.
  Avatar& operator=(const Avatar&) = delete;

  long nextRandom(long limit) {
    if (random) return random(limit);
    uint32_t& x = randomState;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return static_cast<long>(x % static_cast<uint32_t>(limit));
  }

  void setTheme(bool light) {
    lightMode = light;
    palette = &avatar_palette::get(light);
  }

  Face desiredFace(uint32_t now) {
    if (voiceRecording || quakeVisible || activeCount > 0 || attentionCount > 0 || doneCount > 0)
      lastTaskAt = now;
    return avatar_behavior::select({activeCount,attentionCount,doneCount,bridgeConnected,voiceRecording},
        avatar_behavior::pending(now,celebrateUntil),now-lastTaskAt<kSleepAfterMs,
        drowsy,startle.active);
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
    return counterLabels.x(slot, std::max(0.0f, (kScreenWidth - ui.region) / 2.0f), ui.corners);
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
    badge.width = approach(badge.width, std::max(kCountSideWidth, counterLabels.sideWidth(side) + kCountPadding),
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
    this->shownDone=shownDone;this->shownActive=shownActive;
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
    const bool allowed=avatar_behavior::canStartle(face,unplugged,
        voiceRecording,countCueBusy(now),buttonCue.active);
    startle.update(now,!allowed);
    if(pendingStartle>0) {
      if(allowed&&!startle.active) {
        startle.notify(now,pendingStartle,startleSide?startleSide:(nextRandom(2)?1:-1));
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
    startle.update(now,!avatar_behavior::canStartle(face,unplugged,
        voiceRecording,staged,buttonCue.active));
    const auto targets=avatar_behavior::targets(120+ui.offset+scene.counterShift,avatar_scene::eyeBaseline(ui.corners,ui.alert,0),
        counterX(0),counterX(1),counterY(),doneCount>0,activeCount>0);
    actor.calm=!avatar_behavior::pending(now,celebrateUntil);
    const auto performance=director.step(face,actor,now,static_cast<uint32_t>(nextRandom(0x7fffffff)),
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
                       static_cast<uint32_t>(nextRandom(0x7fffffff)));
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
    const float presence = std::max(ui.done.presence, ui.active.presence);
    const float sideWidth = std::max(ui.done.width * ui.done.presence,
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
    const float compactWidth = constrained((ui.region - 48) / 2.0f, 28.0f, 36.0f);
    const float compact = ui.side * (1 - ui.single) * (1 - ui.corners);
    ui.awakeWidth = lerp(50, compactWidth, compact);
    ui.awakeHeight = lerp(lerp(56,42,compact),44,ui.alert);
    const float sleepingBreath = sinf((now % 126000) * (2.0f * kPi / 4500.0f));
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
                      face == Face::listening ?
                          (voiceLevelOverride >= 0 ? voiceLevelOverride : voiceLevel.level(now)) : 0);
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
    taskTransfer.animate(now, seconds, 120 + ui.offset + scene.counterShift, palette->done);
    // Clear the falling numeral's column: the wide paired face needs far more
    // than the authored 20px, while the outer eye stays on screen.
    float dodgeRoom = task_cues::Arrival::kDodge;
    if (taskArrival.busy(now)) {
      const count_raster::Font& toFont = countFont(ui.active.fontIndex);
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
                        countY, countFont(ui.done.fontIndex).capHeight,
                        countFont(ui.active.fontIndex).capHeight, scene.hands);
    counterChecks.shape(scene, now, doneX, activeX, countY,
                        ui.done.largeWidth, ui.active.largeWidth,
                        countFont(ui.done.fontIndex).capHeight,
                        countFont(ui.active.fontIndex).capHeight,
                        counterContactReady(ui.done), counterContactReady(ui.active));
  }

  status_background::State statusBackgroundState(uint32_t now) {
    // A charging completion pulses energetic green light through the blue.
    const Palette& colors=*palette;
    const uint16_t activeFill=blend565(blend565(colors.working,colors.blocked,ui.waiting),task_cues::kEnergy,
                                       taskTransfer.glow(now));
    const bool portrait=ui.alert>.01f && !(doneCount>0 && activeCount>0);
    return {colors.done,activeFill,doneCount>0?colors.done:activeFill,
            backgroundDeparture.presence[0],backgroundDeparture.presence[1],portrait?ui.alert:0,
            taskTransfer.washTint,taskTransfer.washValue,
            request_scene::field(now,colors.request,colors.requestPeak),ui.alert,colors.body};
  }

  uint16_t statusBackgroundAt(int x,int y) {
    return statusRows[y&3][x];
  }

  void updateCounterContact(Badge& badge, int slot, float seconds, uint32_t now) {
    const float side = status_sides::slotDirection(slot);
    const float depth = avatar_interaction::contactDepth(faceMask, counterX(slot), counterY(), badge.largeWidth,
                                                         countFont(badge.fontIndex).capHeight,
                                                         ui.corners, side);
    // Flight is authored travel, never collision input. Rebaseline on landing.
    badge.spring.update(seconds, depth, counterContactReady(badge) && !countCueBusy(now), side);
  }

  template <class Target>
  void drawBadge(Target& target, Badge& badge, int slot, uint32_t now) {
    if (badge.presence < 0.01f) return;

    const bool done = slot == status_sides::kDoneSlot;
    const float settled = constrained((badge.presence - 0.8f) / 0.2f, 0.0f, 1.0f);
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
    const count_raster::Font& font = countFont(badge.fontIndex);
    char label[10];
    roman_counts::format(badge.shown, label);
    if(done&&!countCueBusy(now))director.accents(now,centerX,centerY-font.capHeight/2-2,
        [&](int x,int y,float a){target.ink(x,y,palette->done,a*alpha);});  // alpha includes the request fade
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
          target.ink(px,py,outline?statusBackgroundAt(px,py):palette->ink,settled*coverage);
        });
      },[&](int x,int y,float a){
        if(x>=0&&x<kScreenWidth&&y>=0&&y<kScreenHeight)target.ink(x,y,task_cues::kFlash,a);
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
      target.ink(x,y,outline?statusBackgroundAt(x,y):palette->ink,alpha*coverage*uncovered);
    },badge.spring.x+shakeX,badge.spring.y+shakeY);  // Slow contact springs move sub-pixel.
  }

  // Transfer overlays: charge crackles, the flipping done numeral and the
  // cartoon starburst. All are event-only and draw nothing otherwise.
  template <class Target>
  void drawTaskCues(Target& target, uint32_t now) {
    if(!taskTransfer.busy(now))return;
    const auto light=[&](int x,int y,float a) {
      if(x>=0&&x<kScreenWidth&&y>=0&&y<kScreenHeight)target.ink(x,y,task_cues::kFlash,a);
    };
    taskTransfer.crackles(now,light);
    taskTransfer.flip(now,countFont(ui.done.fontIndex),counterX(0),counterY(),
        [&](const count_raster::Font& f,const char* text,float x,float y,float scale,float a) {
      count_raster::renderScaled(f,text,x,y,scale,[&](int px,int py,float coverage,bool outline) {
        if(px<0||px>=kScreenWidth||py<0||py>=kScreenHeight)return;
        target.ink(px,py,outline?statusBackgroundAt(px,py):palette->ink,a*coverage);
      });
    },light);
    taskTransfer.burst(now,[&](int x,int y,int length) {
      target.run(x,y,length,task_cues::kFlash);
    },light);
  }

  // Four static dither rows remove RGB565 banding.
  template <class Target>
  void drawIndicators(Target& target, uint32_t now) {
    statusCache.update(statusBackgroundState(now),[&](int x,int y,uint16_t color) {
      statusRows[y][x]=color;
      target.backdrop(x,y,color);
    });
    target.fillBackdrop();
  }

  template <class Target>
  void drawFaceBackground(Target& target) {
    // Skip whole opaque blocks while preserving individual travelling flaps
    // and exact fractional edge coverage. Counts still render above the face.
    const uint16_t body=palette->body;
    faceMask.raster([&](int x,int y,int length) {
      target.run(x,y,length,body);
    },[&](int x,int y,float alpha) {
      target.ink(x,y,body,alpha);
    });
  }

  // A report from the host: counts of active, waiting-for-you and done tasks.
  // One completion can arrive as two polls (active falls, then done rises, or
  // the reverse). Present the first half only once its pair arrives or the
  // window expires, so the transfer cue sees both edges together. True when a
  // request (attention) has just started: the device chimes.
  bool report(int nextActiveCount, int nextAttentionCount, int nextDoneCount, uint32_t now) {
    int pairedActive = nextActiveCount, pairedDone = nextDoneCount;
    completionPairing.filter(now, doneCount, activeCount, pairedDone, pairedActive,
        completionPairingReady(nextAttentionCount));
    return applyCounts(pairedActive, nextAttentionCount, pairedDone, now);
  }

  // Release a held half completion once its window expires or staging stops
  // being appropriate (button, recording); counts are never dropped. True when
  // a request has just started.
  bool releasePairing(uint32_t now) {
    if (!completionPairing.due(now, completionPairingReady(attentionCount))) return false;
    completionPairing.reset();
    return applyCounts(completionPairing.active, attentionCount, completionPairing.done, now);
  }

  // The host stopped reporting.
  void disconnect() {
    bridgeConnected = false;
    completionPairing.reset();
    activeCount = 0;
    attentionCount = 0;
    doneCount = 0;
  }

  bool completionPairingReady(int nextAttentionCount) {
    return bridgeConnected && !voiceRecording && nextAttentionCount == 0 && attentionCount == 0 &&
           !buttonCue.active && ui.active.label > .9f && ui.active.presence > .95f;
  }

  bool applyCounts(int nextActiveCount, int nextAttentionCount, int nextDoneCount, uint32_t now) {
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
    }
    return attentionStarted;
  }

  // Advances everything to `now` and returns the seconds since the last frame.
  float update(uint32_t now) {
    const float seconds = lastFrameAt ? constrained((now - lastFrameAt) / 1000.0f, 0.0f, 0.1f) : 0;
    lastFrameAt = now;

    updateStartle(now);
    const Face next = desiredFace(now);
    if (!faceApplied || next != face) applyFace(next, now);

    updateLayout(seconds, now);
    return seconds;
  }

  // Draws the frame `update` prepared.
  template <class Target>
  void draw(Target& target, uint32_t now, float seconds) {
    const float singleOffset=std::max(ui.done.width*ui.done.presence,
                                      ui.active.width*ui.active.presence)/2;
    const bool reclaimed=ui.corners<.05f && ui.single>.95f &&
                         fabsf(ui.offset)>.95f*singleOffset;
    backgroundDeparture.update(ui.done.presence,ui.active.presence,
        counterFlight.busy(now),doneCount>0,activeCount>0,
        reclaimed&&ui.offset*status_sides::kDone>0,reclaimed&&ui.offset*status_sides::kActive>0,
        bridgeConnected&&!voiceRecording&&attentionCount==0,seconds);
    drawIndicators(target, now);
    // The request `!` sits in the pulsing field; the rising head may cover it.
    if (ui.alert > .01f) {
      request_scene::sign(now,[&](int x,int y,float alpha) {
        if(x>=0 && x<kScreenWidth && y>=0 && y<kScreenHeight) target.ink(x,y,palette->ink,alpha*ui.alert);
      });
    }
    target.mark(frame_profile::background);
    avatar_scene::buildMask(scene,faceMask);
    updateCounterContact(ui.done, status_sides::kDoneSlot, seconds, now);
    updateCounterContact(ui.active, status_sides::kActiveSlot, seconds, now);
    target.mark(frame_profile::mask);
    drawFaceBackground(target);
    target.mark(frame_profile::face);
    // Opaque interior eye runs are filled directly; AA edges stay per-pixel.
    const uint16_t eye=palette->eye;
    avatar_scene::eyes(scene,faceMask,[&](int x,int y,float alpha) {
      target.ink(x,y,eye,alpha);
    },[&](int x,int y,int length) {
      target.run(x,y,length,eye);
    });
    target.mark(frame_profile::eyes);
    avatar_scene::mouth(scene,faceMask,[&](int x,int y,float alpha) {
      target.ink(x,y,eye,alpha);
    });
    target.mark(frame_profile::mouth);
    target.mark(frame_profile::attention);
    drawBadge(target,ui.done,status_sides::kDoneSlot,now);
    drawBadge(target,ui.active,status_sides::kActiveSlot,now);
    drawTaskCues(target,now);
    target.mark(frame_profile::badges);
  }
};

}  // namespace avatar_frame
