#pragma once
#include <stdint.h>
#include <math.h>
#include "eye_acting.h"

// Procedural life layered over authored acting. Tiny fixational saccades and
// occasional blinks happen only during genuine eye holds (or idle/rest), and
// an underdamped spring gives eye-size changes a small squash/stretch settle.
// Time-based, bounded and pose-only: no raster work, task state or clip choice.
namespace eye_life {
struct Mood {
 float micro;              // Fixational saccade amplitude multiplier.
 uint16_t blinkMin,blinkMax;
 float doubleChance;       // Chance that a procedural blink is a double blink.
 float saccadeBlink;       // Chance of blinking with a large authored gaze jump.
};
// Blinks are mostly motivated: they ride on big gaze shifts (saccadeBlink),
// while the unprompted timer only fills long holds (~one per 4-7s).
constexpr Mood kThinking={1,4200,7000,.10f,.70f};
constexpr Mood kDone={1,3600,6000,.20f,.65f};
constexpr Mood kAttention={1.15f,2600,4400,0,.6f};  // Short single closures only.
constexpr Mood kListening={.6f,3400,6000,.10f,.4f};
constexpr Mood kIdle={1,3600,6400,.15f,.6f};
constexpr Mood kOffline={.5f,3500,6000,0,0};

constexpr float kMicroX=1.8f,kMicroY=1.1f;
constexpr uint16_t kMicroMoveMs=60;
constexpr uint16_t kMicroMinMs=700,kMicroMaxMs=2200;
// A fresh fixation holds exactly where the authored move landed for this long
// before fixational drift may begin, so landings read as deliberate stops,
// not as a second, smaller step right after the saccade.
constexpr uint16_t kMicroSettleMs=450;
constexpr uint16_t kBlinkClose=70,kBlinkHold=30,kBlinkOpen=140,kBlinkLag=14;
constexpr uint32_t kBlinkLength=kBlinkClose+kBlinkHold+kBlinkOpen+kBlinkLag;
constexpr uint16_t kDoubleGap=90;
constexpr uint16_t kSaccadeBlinkPx=14;
constexpr uint16_t kBlinkRefractoryMs=1200;

inline bool reached(uint32_t now,uint32_t at) { return static_cast<int32_t>(now-at)>=0; }
// Lid falls with acceleration, holds briefly, then opens more slowly.
inline float blinkEnvelope(int32_t t) {
 if(t<=0) return 0;
 if(t<kBlinkClose) { const float s=t/static_cast<float>(kBlinkClose);return s*s; }
 t-=kBlinkClose;
 if(t<kBlinkHold) return 1;
 t-=kBlinkHold;
 if(t<kBlinkOpen) return 1-eye_acting::smooth(t/static_cast<float>(kBlinkOpen));
 return 0;
}

struct Life {
 // Separate streams: blink timing (frame-aligned) never perturbs the
 // absolute micro-saccade schedule, which stays identical at any frame rate.
 uint32_t seed=0x2545f491u,microSeed=0x9e3779b9u;
 bool primed=false,blinking=false,leftLeads=true,doubleNext=false,second=false;
 bool segmentSeen=false;
 // fixating: inside one uninterrupted still stretch, anchored at fixationAt.
 // engaged: a micro-saccade has started from that fixation's exact landing.
 bool fixating=false,engaged=false;
 float fromX=0,fromY=0,toX=0,toY=0,weight=0;
 uint32_t moveAt=0,nextMoveAt=0,blinkAt=0,nextBlinkAt=0,lastClosureAt=0,lastSegment=0;
 uint32_t fixationAt=0;

 static uint32_t next(uint32_t& state) {
  if(!state)state=1;
  state^=state<<13;state^=state>>17;state^=state<<5;return state;
 }
 static float unit(uint32_t& state) { return (next(state)>>8)*(1.0f/16777216.0f); }
 static uint32_t between(uint32_t& state,uint32_t lo,uint32_t hi) {
  return lo+static_cast<uint32_t>(unit(state)*(hi-lo));
 }
 float unit() { return unit(seed); }
 uint32_t between(uint32_t lo,uint32_t hi) { return between(seed,lo,hi); }
 void reschedule(uint32_t now,const Mood& mood) {
  nextBlinkAt=now+between(mood.blinkMin,mood.blinkMax);second=false;doubleNext=false;
 }
 void micro(uint32_t now,float& x,float& y) const {
  const float m=eye_acting::snap((now-moveAt)/static_cast<float>(kMicroMoveMs));
  x=fromX+(toX-fromX)*m;y=fromY+(toY-fromY)*m;
 }
 void startBlink(uint32_t at,const Mood& mood) {
  blinking=true;blinkAt=at;leftLeads=next(seed)&1;
  doubleNext=!second&&unit()<mood.doubleChance;
 }
 // `still`: authored eyes are holding and no cue is moving them.
 // `segment`: optional authored interval, used for blinks on big saccades.
 eye_acting::Pose apply(eye_acting::Pose eyes,uint32_t now,float seconds,const Mood& mood,
                        bool still,bool allowBlink,const eye_acting::Segment* segment=nullptr) {
  if(!primed) {
   primed=true;moveAt=now;nextMoveAt=now+between(microSeed,400,900);
   lastClosureAt=now-kBlinkRefractoryMs;  // Startup is not a closure.
   reschedule(now,mood);
  }
  // A fixation starts when the eyes become still; an authored hold anchors at
  // its exact segment start, so the settle delay is frame-rate independent.
  if(!still) fixating=engaged=false;
  else if(!fixating) {
   fixating=true;engaged=false;
   fixationAt=segment&&segment->valid?segment->start:now;
  }
  // Fixational micro-saccades on an absolute schedule (frame-rate independent).
  if(static_cast<int32_t>(now-nextMoveAt)>3000) nextMoveAt=now;  // Long stall.
  while(reached(now,nextMoveAt)) {
   micro(nextMoveAt,fromX,fromY);
   // The first drift of a settled fixation leaves from its exact landing.
   if(fixating&&!engaged&&reached(nextMoveAt,fixationAt+kMicroSettleMs)) {
    engaged=true;fromX=fromY=0;
   }
   if(unit(microSeed)<.35f) { toX=toY=0; }  // Re-centre now and then; never wander off.
   else { toX=(unit(microSeed)*2-1)*kMicroX;toY=(unit(microSeed)*2-1)*kMicroY; }
   moveAt=nextMoveAt;nextMoveAt=moveAt+between(microSeed,kMicroMinMs,kMicroMaxMs);
  }
  // Engaging needs no fade-in (the drift starts from zero offset); leaving a
  // hold still fades the offset out quickly under the authored move.
  if(engaged) weight=1;
  else weight*=expf(-fmaxf(0,seconds)/.05f);
  float mx,my;micro(now,mx,my);
  eyes.x+=mx*mood.micro*weight;eyes.y+=my*mood.micro*weight;

  // Authored closures (blinks, winks, happy holds) reset the procedural timer.
  const float authored=fmaxf(eyes.closeL,eyes.closeR);
  if(authored>.3f) {
   lastClosureAt=now;
   if(!blinking&&static_cast<int32_t>(nextBlinkAt-now)<static_cast<int32_t>(mood.blinkMin))
    reschedule(now,mood);
  }
  // Blink with a large authored saccade unless the clip already punctuates it.
  if(segment&&segment->valid&&(!segmentSeen||segment->start!=lastSegment)) {
   segmentSeen=true;lastSegment=segment->start;
   if(segment->saccade&&!segment->closure&&segment->distance>=kSaccadeBlinkPx&&allowBlink&&
      !blinking&&now-segment->start<100&&now-lastClosureAt>=kBlinkRefractoryMs&&
      unit()<mood.saccadeBlink) startBlink(segment->start,mood);
  }
  if(!blinking&&allowBlink&&(still||second)&&authored<.05f&&reached(now,nextBlinkAt))
   startBlink(now,mood);
  if(blinking) {
   const int32_t t=static_cast<int32_t>(now-blinkAt);
   const float lead=blinkEnvelope(t),lag=blinkEnvelope(t-kBlinkLag);
   eyes.closeL=fmaxf(eyes.closeL,leftLeads?lead:lag);
   eyes.closeR=fmaxf(eyes.closeR,leftLeads?lag:lead);
   if(t>=static_cast<int32_t>(kBlinkLength)) {
    blinking=false;lastClosureAt=now;
    if(doubleNext) { doubleNext=false;second=true;nextBlinkAt=blinkAt+kBlinkLength+kDoubleGap; }
    else reschedule(now,mood);
   }
  }
  return eyes;
 }
};

// Underdamped follower for rendered eye width/height. Fixed 240Hz substeps
// with linearly interpolated targets keep 20/30/60fps motion consistent.
struct SizeSpring {
 static constexpr float kFrequency=5.5f,kDamping=.5f;
 float width=50,height=56,vw=0,vh=0,targetW=50,targetH=56,accumulator=0;
 bool primed=false;
 void reset(float w,float h) { width=targetW=w;height=targetH=h;vw=vh=0;accumulator=0;primed=true; }
 void step(float seconds,float w,float h) {
  if(!primed) { reset(w,h);return; }
  seconds=fmaxf(0,fminf(.1f,seconds));
  const float startW=targetW,startH=targetH;
  constexpr float dt=1.0f/240;
  constexpr float omega=2*3.14159265f*kFrequency;
  constexpr float k=omega*omega,c=2*kDamping*omega;
  const float total=accumulator+seconds;
  float consumed=0;
  accumulator=total;
  while(accumulator+1e-6f>=dt) {
   accumulator=fmaxf(0,accumulator-dt);consumed+=dt;
   const float f=total>0?fminf(1,consumed/total):1;
   const float tw=startW+(w-startW)*f,th=startH+(h-startH)*f;
   vw+=(k*(tw-width)-c*vw)*dt;width+=vw*dt;
   vh+=(k*(th-height)-c*vh)*dt;height+=vh*dt;
  }
  targetW=w;targetH=h;
  // Underdamping alone bounds the settle (~16% of a step); never invert.
  width=fmaxf(1,width);height=fmaxf(1,height);
 }
};
} // namespace eye_life
