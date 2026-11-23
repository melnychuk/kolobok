#pragma once
#include "avatar_scene.h"

// Shared device/native interaction acting and small, bounded numeral springs.
// No task mutation or audio analysis belongs in this visual kernel.
namespace avatar_interaction {
constexpr eye_acting::Key tapKeys[] = {
 {0,eye_acting::p()}, {180,eye_acting::p(-12,3,1.05f,1.08f,0,0,.65f,0,0,1.04f,1.03f,-4,1,.7f)},
 {340,eye_acting::p(-12,3,1.05f,1.08f,0,0,.65f,1,0,1.04f,1.03f,-4,1,.7f)},
 {520,eye_acting::p(-12,3,1.05f,1.08f,0,0,.65f)},
 {800,eye_acting::p(0,-2,1,1.08f,0,0,.45f)}, {1200,eye_acting::p()} };
constexpr eye_acting::Key releaseKeys[] = {
 {0,eye_acting::p(0,-2,1,1.08f)}, {230,eye_acting::p(0,4,1.04f,1.02f,0,0,.3f,0,0,1.04f,.98f,0,2,.5f)},
 {470,eye_acting::p(0,-3,1,1.12f,0,0,.4f,0,0,1,1.03f,0,-2,.5f)},
 {780,eye_acting::p(0,-3,1,1.12f,0,0,.4f)}, {1200,eye_acting::p()} };
// A happy wink toward the completed counter (authored with it on the left).
constexpr eye_acting::Clip tapClip=eye_acting::makeClip("Open completed",tapKeys,eye_acting::authoredDoneLeft);
constexpr eye_acting::Clip releaseClip={"Dictation released",releaseKeys,5};
struct ButtonCue {
 uint32_t started=0;
 bool active=false,released=false;
 void notify(uint32_t now,bool voiceEnd=false) { started=now;active=true;released=voiceEnd; }
 eye_acting::Pose apply(const eye_acting::Pose& base,uint32_t now,bool body=false) const {
  if(!active)return base;
  const uint32_t elapsed=now-started;
  const uint32_t t=body?(elapsed>90?elapsed-90:0):elapsed;
  const float weight=eye_acting::smooth(t/140.0f)*(1-eye_acting::smooth(t>800?(t-800)/400.0f:0));
  auto result=eye_acting::mix(base,eye_acting::sample(released?releaseClip:tapClip,t),weight);
  if(body)result.closeL=result.closeR=0;
  return result;
 }
 void update(uint32_t now,bool suppressed) { if(suppressed||now-started>=1290)active=false; }
};

// Startled by a sudden loud noise: an instant wide-eyed jolt and hop with a
// short shiver, a double take (blink), then a wary look to both sides before
// relaxing. Event-only (the caller supplies an external trigger); never idle.
// `side` mirrors the glance; a mono microphone cannot localize the sound.
constexpr eye_acting::Key startleKeys[] = {
 {0,eye_acting::p()},
 {70,eye_acting::p(0,-7,.84f,1.40f,0,0,0,0,0,.93f,1.10f,0,-7,1.5f)},
 {300,eye_acting::p(0,-6,.86f,1.36f,0,0,0,0,0,.94f,1.08f,0,-5,1.3f)},
 {380,eye_acting::p(0,-2,1,1,0,0,0,1,1,1.03f,.95f,0,1,1.1f)},
 {470,eye_acting::p(0,-3,.92f,1.20f,0,0,0,0,0,.97f,1.04f,0,-2,1.0f)},
 {700,eye_acting::p(14,-2,.95f,1.10f,.14f,.14f,0,0,0,1,1,3,0,.9f)},
 {960,eye_acting::p(-13,-1,.96f,1.08f,.18f,.18f,0,0,0,1,1,-3,0,.9f)},
 {1100,eye_acting::p(-6,0,1,1.02f,.14f,.14f,0,1,1,1.02f,.97f,-1,1,.9f)},
 {1190,eye_acting::p(0,0,1,1.04f,.08f,.08f)},
 {1500,eye_acting::p()} };
constexpr eye_acting::Clip startleClip={"Startled by a loud noise",startleKeys,10};
struct Startle {
 static constexpr uint32_t kEnd=1500,kBodyLag=90;
 uint32_t started=0;
 bool active=false;
 float side=1,strength=1;
 void notify(uint32_t now,float amount,float glanceSide) {
  started=now;active=true;
  strength=fmaxf(0.0f,fminf(1.0f,amount));
  side=copysignf(1.0f,glanceSide);
 }
 eye_acting::Pose apply(const eye_acting::Pose& base,uint32_t now,bool body=false) const {
  if(!active)return base;
  const uint32_t elapsed=now-started;
  const uint32_t t=body?(elapsed>kBodyLag?elapsed-kBodyLag:0):elapsed;
  if(t>=kEnd)return base;
  // A startle must read at once: 50ms entry, relax over the last 350ms.
  const float weight=eye_acting::smooth(t/50.0f)*
      (1-eye_acting::smooth(t>kEnd-350?(t-(kEnd-350))/350.0f:0));
  auto look=eye_acting::sample(startleClip,t);
  look.x*=side;look.lean*=side;
  if(t>=70&&t<330) {  // frightened shiver while frozen wide-eyed
   const float shiver=sinf(t*.13f)*(t<250?1:1-(t-250)/80.0f);
   look.x+=1.1f*shiver;look.lean+=.6f*shiver;
  }
  auto result=eye_acting::mix(base,look,weight*(.6f+.4f*strength));
  if(body)result.closeL=result.closeR=0;
  return result;
 }
 void update(uint32_t now,bool suppressed) { if(suppressed||now-started>=kEnd+kBodyLag)active=false; }
};

// Occasionally inspect the counters that actually exist. Active is focused;
// completed is warm/happy. Single-counter checks reach across the vertical face.
// A check's `side` is the screen direction of the inspected counter
// (status_sides::kDone or kActive), so looks, presses and rim excitation all
// point at that counter wherever the layout puts it.
//
// Scheduling. Live firmware/widget checks are occasional events: one check at a
// time, only while the acting player rests between clips (the player holds its
// next clip until the check finishes), separated by random 5.2-12.8s gaps, with
// sides chosen randomly when both counters exist. Each check varies: about a
// third are a glance without the shoulder press, and the look target jitters by
// a few pixels. The first check after enabling is always a full press.
// `fixed` keeps the original deterministic 8.4s fixture timeline (active window
// 1400-3500ms, completed window 4400-7000ms, kiss on cycle%3==1) for event
// previews and contact regressions; it is not used live.
struct Checks {
 static constexpr uint32_t kCycle=8400,kActiveLength=2100,kDoneLength=2600,kBodyLag=90;
 static constexpr uint32_t kFirstDelay=1400,kGapMin=5200,kGapMax=12800;
 uint32_t started=0,previousPhase=0,actionCycle=0;
 bool enabled=false,donePresent=true,activePresent=true;
 bool fixed=false;
 // Scheduled-mode state.
 uint32_t checkStart=0,nextAt=0,seed=0x6d2b79f5u,count=0;
 int side=0,lastSide=0;
 bool press=true,kissing=false;
 float jitterX=0,jitterY=0;
 struct Moment { int side=0;uint32_t local=0;bool press=true,kiss=false;float jx=0,jy=0; };
 static uint32_t length(int which) { return which==status_sides::kDone?kDoneLength:kActiveLength; }
 bool pairedDone(int which) const { return which==status_sides::kDone&&donePresent&&activePresent; }
 float unit() {
  if(!seed)seed=1;
  seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
  return (seed>>8)*(1.0f/16777216.0f);
 }
 // True while a scheduled check (including its 90ms body follow-through) runs.
 bool busy(uint32_t now) const {
  return enabled&&!fixed&&side!=0&&now-checkStart<length(side)+kBodyLag;
 }
 void begin(uint32_t now,int which) {
  side=which;checkStart=now;
  press=count==0||unit()>=.34f;
  // With both counters on screen only active work gets poked; a check of
  // the completed counter is a warm glance.
  if(pairedDone(which))press=false;
  kissing=which==status_sides::kDone&&press&&actionCycle%3==1;
  jitterX=(unit()*2-1)*3;jitterY=(unit()*2-1)*2.5f;
  ++count;
 }
 // `ready`: the acting player rests between clips (always true in fixtures).
 // `surface` is unused: a press used to kick the nearby rim, which read as a
 // bump; the round cap (shape()) is now the whole gesture.
 void update(uint32_t now,bool allowed,face_physics::Surface* surface=nullptr,
             bool done=true,bool active=true,bool ready=true) {
  (void)surface;
  allowed=allowed&&(done||active);
  if(allowed&&(!enabled||done!=donePresent||active!=activePresent)) {
   started=now;previousPhase=0;side=0;nextAt=now+kFirstDelay;
  }
  donePresent=done;activePresent=active;
  enabled=allowed;
  if(!enabled){side=0;return;}
  if(fixed) {
   const uint32_t phase=(now-started)%kCycle;
   if(phase<previousPhase)++actionCycle;
   previousPhase=phase;
   return;
  }
  if(side!=0) {
   const uint32_t local=now-checkStart;
   if(local<length(side)+kBodyLag)return;
   if(side==status_sides::kDone&&press)++actionCycle;
   lastSide=side;side=0;
   nextAt=now+kGapMin+static_cast<uint32_t>(unit()*(kGapMax-kGapMin));
  }
  if(!ready||static_cast<int32_t>(now-nextAt)<0)return;
  int which=activePresent?status_sides::kActive:status_sides::kDone;
  if(activePresent&&donePresent&&count>0)which=unit()<.7f?-lastSide:lastSide;
  if(which==0)which=status_sides::kActive;
  begin(now,which);
 }
 Moment moment(uint32_t now,bool body) const {
  Moment m;
  if(!enabled)return m;
  if(fixed) {
   const uint32_t elapsed=now-started;
   const uint32_t t=(body?(elapsed>kBodyLag?elapsed-kBodyLag:0):elapsed)%kCycle;
   const bool done=t>=4400;
   const uint32_t begin=done?4400:1400,end=done?7000:3500;
   if(t<begin||t>=end)return m;
   m.side=done?status_sides::kDone:status_sides::kActive;m.local=t-begin;m.kiss=done&&actionCycle%3==1;
   if(pairedDone(m.side))m.press=m.kiss=false;
   return m;
  }
  if(side==0)return m;
  const uint32_t elapsed=now-checkStart;
  const uint32_t local=body?(elapsed>kBodyLag?elapsed-kBodyLag:0):elapsed;
  if(local>=length(side))return m;
  m.side=side;m.local=local;m.press=press;m.kiss=kissing;m.jx=jitterX;m.jy=jitterY;
  if(pairedDone(m.side))m.press=m.kiss=false;
  return m;
 }
 eye_acting::Pose apply(const eye_acting::Pose& base,uint32_t now,bool body=false) const {
  const Moment m=moment(now,body);
  if(!m.side)return base;
  const bool done=m.side==status_sides::kDone;
  if(done?!donePresent:!activePresent)return base;
  const uint32_t end=length(m.side),local=m.local;
  const float toward=static_cast<float>(m.side);
  const float weight=eye_acting::smooth(local/450.0f)*
                     (1-eye_acting::smooth(local>end-500?(local-(end-500))/500.0f:0));
  // Lid asymmetry is authored for the original right-hand active counter.
  auto look=done?eye_acting::p(15*toward,13,1.04f,1.06f,0,0,.72f,0,0,1.06f,1.02f,10*toward,3,.65f):
                 eye_acting::p(15*toward,13,1.05f,.94f,.22f,.24f,0,0,0,1.06f,1.02f,10*toward,3,.45f);
  if(!done&&status_sides::kMirrorAuthored)look=eye_acting::swapEyes(look);
  if(!(donePresent&&activePresent)) {
   // The number is beside the tall face, not below it. Reach sideways without
   // widening the layout or losing the vertical mouth / positive done acting.
   look.x=20*toward;look.y=3;
   look.lean=6*toward;look.lift=0;look.bodyWidth=1.02f;
  }
  look.x+=m.jx;look.y+=m.jy;
  // A poke is the round cap's job (shape()). The body does not lunge with
  // it (a whole-body lean and press read as a bump): it braces back a little,
  // away from the numeral (up, when the numerals are below), so the round
  // cap visibly reaches across the gap.
  if(m.press&&!m.kiss) {
   const float brace=pokeReach(local);
   const bool paired=donePresent&&activePresent;
   look.lean=look.lean*kPokeLean-(paired?0:kPokeBrace)*toward*brace;
   if(paired) {
    look.lift=avatar_scene::mix(look.lift,-kPokeLift,brace);
    // A wider body would lower the rim's corners again, onto the numeral.
    look.bodyWidth=avatar_scene::mix(look.bodyWidth,1,brace);
   }
  }
  // Every third completed press is an affectionate kiss, not a poke: a cheek
  // nuzzle. Happy curved closures held while the body leans in, the facing
  // side swells softly against the numeral (shape()) and rubs it slowly up
  // and down, then eases back. No task state changes.
  if(m.kiss) {
   const float nuzzle=nuzzleWeight(local);
   look.closeL=look.closeR=.9f*nuzzle;
   look.smile=fmaxf(look.smile,.72f);
   look.lean=avatar_scene::mix(look.lean,kNuzzleLean*toward,nuzzle);
   look.lift+=kNuzzleRub*nuzzleRub(local);
  }
  if(done&&!m.kiss&&local>=1550&&local<1770) {
   const float closure=local<1630?eye_acting::smooth((local-1550)/80.0f):
                        1-eye_acting::smooth((local-1630)/140.0f);
   look.closeL=look.closeR=closure;
  }
  auto result=eye_acting::mix(base,look,weight);
  if(body)result.closeL=result.closeR=0;
  return result;
 }
 // Poke timeline (body time, ms): the blob rises out toward the numeral by
 // kPokeGrow, jabs it twice, and flows back into the body from kPokeRetract
 // over kPokeRetractLength.
 static constexpr uint32_t kPokeGrow=380,kPokeRetract=1100,kPokeRetractLength=420;
 static constexpr float kPokeJab=7,kPokeLean=0,kPokeBrace=7,kPokeLift=10;
 // Kiss timeline: lean in and close the eyes (150..650ms), nuzzle with a
 // slow cheek rub (two strokes, 650..1350ms), ease back by 1800ms.
 static constexpr uint32_t kNuzzleIn=150,kNuzzleTouch=650,kNuzzleHold=1350,kNuzzleOut=1800;
 static constexpr float kNuzzleLean=5,kNuzzleRub=2.2f;
 static float pulse(uint32_t t,uint32_t start,uint32_t peak,uint32_t hold,uint32_t end) {
  if(t<=start||t>=end)return 0;
  if(t<peak)return eye_acting::smooth((t-start)/static_cast<float>(peak-start));
  if(t<hold)return 1;
  return 1-eye_acting::smooth((t-hold)/static_cast<float>(end-hold));
 }
 static float pokeReach(uint32_t local) {
  return eye_acting::smooth(local/static_cast<float>(kPokeGrow))*
         (1-eye_acting::smooth(local>kPokeRetract?(local-kPokeRetract)/static_cast<float>(kPokeRetractLength):0));
 }
 static float pokeJab(uint32_t local) {
  return kPokeJab*(pulse(local,430,520,560,700)+pulse(local,740,830,870,1020));
 }
 static float nuzzleWeight(uint32_t local) {
  return pulse(local,kNuzzleIn,kNuzzleTouch,kNuzzleHold,kNuzzleOut);
 }
 // Two slow up-and-down strokes while touching, starting and ending at rest.
 static float nuzzleRub(uint32_t local) {
  if(local<=kNuzzleTouch||local>=kNuzzleHold)return 0;
  const float u=(local-kNuzzleTouch)/static_cast<float>(kNuzzleHold-kNuzzleTouch);
  return sinf(u*4*3.14159265f)*sinf(u*3.14159265f);
 }
 // Supply a localized shape performance after the scene's eased layout has
 // been composed. Coordinates are the same RESTING bounds used for contact.
 void shape(avatar_scene::Scene& scene,uint32_t now,float doneX,float activeX,float cy,
            float doneWidth,float activeWidth,float doneHeight,float activeHeight,
            bool doneReadable=true,bool activeReadable=true) const {
  scene.poke=avatar_scene::Poke{};
  const Moment m=moment(now,true);
  if(!m.side||!m.press)return;
  const bool done=m.side==status_sides::kDone;
  if(done?(!donePresent||!doneReadable):(!activePresent||!activeReadable))return;
  const uint32_t local=m.local;
  const float reach=m.kiss?nuzzleWeight(local):pokeReach(local);
  if(reach<=0)return;
  scene.poke.side=static_cast<float>(m.side);
  if(m.kiss)scene.poke.kiss=reach;
  else { scene.poke.strength=reach;scene.poke.jab=pokeJab(local); }
  scene.poke.cx=done?doneX:activeX;scene.poke.cy=cy;
  scene.poke.width=done?doneWidth:activeWidth;scene.poke.height=done?doneHeight:activeHeight;
 }
};

// Completion sorting: gather the OLD counters together, reveal the authoritative
// snapshot at the apex, then return the NEW counters to their own status corners.
// Aggregate counts cannot identify a task; require both directional edges rather
// than inventing a transfer on an idle timer or on a done-only increase.
struct CounterFlight {
 struct Offset { float x=0,y=0; };
 // The lower rim first droops into a blob over each numeral, overlapping its
 // top, then the authored pickup/gather/apex-swap/return runs kReach later
 // with each numeral hanging from its blob. Numerals are carried only kCarry
 // high while the body rises kRise, so they travel over newly revealed status
 // background, are set back down in their new places, and only then does
 // each blob let go and flow back into the rim.
 static constexpr uint32_t kReach=380,kSwap=kReach+1400,kRelease=kReach+2500,
                           kRetract=380,kEnd=kReach+2950;
 static constexpr float kCarry=14,kRise=20;
 uint32_t started=0;
 bool enabled=false,initialized=false;
 int previousDone=0,previousActive=0,oldDone=0,oldActive=0;
 float reservedDoneWidth=0,reservedActiveWidth=0;
 void fit(float doneWidth,float activeWidth) {
  reservedDoneWidth=doneWidth;reservedActiveWidth=activeWidth;
 }
 bool observe(uint32_t now,int done,int active,bool healthy,bool ready) {
  if(!healthy) { initialized=false;enabled=false;return false; }
  const bool changed=done!=previousDone||active!=previousActive;
  const bool completion=initialized&&changed&&done>previousDone&&active<previousActive&&
                        previousDone>0&&previousActive>0;
  // A fresh update during sorting supersedes the visual staging. No stale replay
  // or second pickup from numerals that are already in the air.
  const bool start=completion&&ready&&!busy(now);
  if(changed)enabled=false;
  if(start) {
   oldDone=previousDone;oldActive=previousActive;started=now;enabled=true;
   reservedDoneWidth=reservedActiveWidth=0;
  }
  previousDone=done;previousActive=active;initialized=true;
  return start;
 }
 void update(uint32_t now,bool allowed) {
  if(!allowed||now-started>=kEnd)enabled=false;
 }
 uint32_t phase(uint32_t now) const { return now-started; }
 bool busy(uint32_t now) const { return enabled&&phase(now)<kEnd; }
 bool swapped(uint32_t now) const { return phase(now)>=kSwap; }
 int displayCount(uint32_t now,bool done,int actual) const {
  return busy(now)&&!swapped(now)?(done?oldDone:oldActive):actual;
 }
 Offset offset(uint32_t now,bool done,float cx,float otherX,float width,float otherWidth) const {
  Offset result;
  (void)otherX;
  if(!busy(now))return result;
  // Both are picked up together; neither status crosses the center.
  const uint32_t elapsed=phase(now);
  if(elapsed<kReach||elapsed>=kRelease)return result;
  const uint32_t t=elapsed-kReach;
  // The grip presses both numerals down a touch before lifting.
  if(t<260) { result.y=2*eye_acting::smooth(t/260.0f);return result; }
  if(t<2260) {
   // A readable together-hold (1050..1600ms) surrounds the 1400ms swap.
   // Return cannot start until both labels carry the new snapshot.
   const float travel=eye_acting::smooth((t-260)/790.0f)*
                      (1-eye_acting::smooth(t>1600?(t-1600)/660.0f:0));
   // Keep at least ten pixels between full fitted labels even if both peak.
   const float dw=reservedDoneWidth>0?reservedDoneWidth:(done?width:otherWidth);
   const float aw=reservedActiveWidth>0?reservedActiveWidth:(done?otherWidth:width);
   // Fixed world-space apex: changing font/label width cannot move the gathered
   // centers during the swap. Reserve both old/new fitted widths before pickup.
   const float midpoint=120;
   // Centre the whole pair, not just its centres: unequal wide bottom labels
   // must fit on screen as well as retain their ten-pixel internal gap.
   const float toward=static_cast<float>(status_sides::direction(done));
   const float target=midpoint+toward*((done?aw:dw)+10)/2;
   const float reach=toward<0?fmaxf(0,target-cx):fminf(0,target-cx);
   result.x=reach*travel;
   result.y=-kCarry*travel+2*(1-eye_acting::smooth((t-260)/120.0f));
  } else {
   // Set down: a small hop as the hands place each numeral home.
   const float u=(t-2260)/240.0f;
   result.y=2*eye_acting::smooth(4*u*(1-u));
  }
  return result;
 }
 // Blob reach for one side: the rim droops onto the numeral, stays on it
 // while it is carried and set back down in its new place, then lets go. A
 // side whose new count is zero (the last active task) vanishes at the swap,
 // so that blob lets go there.
 float handReach(uint32_t now,bool done) const {
  if(!busy(now))return 0;
  const uint32_t t=phase(now);
  if(t<kReach)return eye_acting::smooth(t/static_cast<float>(kReach));
  uint32_t release=kRelease;
  if(!done&&previousActive==0)release=kSwap;
  if(t<release)return 1;
  return 1-eye_acting::smooth(fminf(1,(t-release)/static_cast<float>(kRetract)));
 }
 // Each lobe is centred over its carried numeral and overlaps its top by
 // kGrip, so the numeral visibly hangs from the blob the whole way.
 // Round blobs, not fingers or plateaus: a narrow I gets a soft round drop,
 // a long Roman is held at its middle.
 static constexpr float kGrip=4,kLobePad=3,kLobeMin=10,kLobeMax=16;
 static float lobeRadius(float width) {
  return fminf(kLobeMax,fmaxf(kLobeMin,width/2+kLobePad));
 }
 void hands(uint32_t now,float doneX,float activeX,float doneWidth,float activeWidth,
            float cy,float doneHeight,float activeHeight,avatar_scene::Hands& out) const {
  out=avatar_scene::Hands{};
  if(!busy(now))return;
  const auto done=offset(now,true,doneX,activeX,doneWidth,activeWidth);
  const auto active=offset(now,false,activeX,doneX,activeWidth,doneWidth);
  out.reach[0]=handReach(now,true);out.reach[1]=handReach(now,false);
  out.x[0]=doneX+done.x;out.y[0]=cy+done.y-doneHeight/2+kGrip;
  out.x[1]=activeX+active.x;out.y[1]=cy+active.y-activeHeight/2+kGrip;
  out.halfWidth[0]=lobeRadius(doneWidth);out.halfWidth[1]=lobeRadius(activeWidth);
 }
 // Body lift in pixels (negative is up); the physics surface adds the lag.
 float rise(uint32_t t) const {
  if(t<kReach||t>=kRelease)return 0;
  const uint32_t u=t-kReach;
  const float dip=1.5f*eye_acting::smooth(4*fminf(1,u/260.0f)*(1-fminf(1,u/260.0f)));
  const float up=eye_acting::smooth(u>180?(u-180)/620.0f:0)*
                 (1-eye_acting::smooth(u>1600?(u-1600)/700.0f:0));
  return dip-kRise*up;
 }
 eye_acting::Pose apply(const eye_acting::Pose& base,uint32_t now,bool body=false) const {
  // Eye-led tracking, with the same 90ms authored body follow-through.
  const uint32_t elapsed=now-started;
  const uint32_t at=body?started+(elapsed>90?elapsed-90:0):now;
  if(!busy(at))return base;
  const uint32_t t=phase(at);
  const float weight=eye_acting::smooth(t/260.0f)*
                     (1-eye_acting::smooth(t>kRelease?(t-kRelease)/450.0f:0));
  // Nominal resting corners, 96px either side of the center.
  const float doneAt=120+96.0f*status_sides::kDone,activeAt=120+96.0f*status_sides::kActive;
  const auto done=offset(at,true,doneAt,activeAt,40,40);
  const auto active=offset(at,false,activeAt,doneAt,40,40);
  const float follow=eye_acting::smooth(t>kReach+1350?(t-kReach-1350)/450.0f:0);
  const float doneWeight=follow;
  auto look=eye_acting::p();
  look.x=avatar_scene::mix((96.0f*status_sides::kActive+active.x)/5,
                           (96.0f*status_sides::kDone+done.x)/5,doneWeight);
  look.y=13+avatar_scene::mix(active.y,done.y,doneWeight)*.22f;
  // Firm scrutiny for active work; soften only when following done. No angry
  // tilt or smile on the active beat, and no change to the flight/gaze clocks.
  look.smile=.63f*doneWeight;look.width=1.04f;
  look.height=avatar_scene::mix(.94f,1.04f,doneWeight);
  look.upperL=.22f*(1-doneWeight);look.upperR=.24f*(1-doneWeight);
  if(status_sides::kMirrorAuthored)look=eye_acting::swapEyes(look);  // authored for active right
  look.lean=look.x*.16f;look.lift=-1;look.flow=.55f;
  auto result=eye_acting::mix(base,look,weight);
  if(body) {
   result.closeL=result.closeR=0;
   // The body leads the carried numerals up (after a small grip dip), so
   // they travel over newly revealed background, and settles with them.
   result.lift+=rise(phase(now));
  }
  return result;
 }
};

// Bounding-box contact with the actual face mask, not a fabricated event.
// Probe resting bounds so count-pop hops don't kick themselves. Corner labels
// receive downward rim contact; vertical labels receive sideways face contact.
inline float contactDepth(const avatar_scene::Mask& mask,float cx,float cy,float width,float height,
                          float corners=1,float side=-1) {
 const int top=static_cast<int>(floorf(cy-height/2));
 if(top<0||top>=135)return 0;
 float depth=0;
 // Exactly zero corner reflow uses lateral contact only. In particular do
 // not scan the new skin fillet's bottom for an unused vertical contribution.
 if(corners!=0) {
  float topLeft,topRight;mask.rowBounds(top,topLeft,topRight);
  int samples=0;
  int left=static_cast<int>(floorf(cx-width/2)),right=static_cast<int>(ceilf(cx+width/2));
  if(left<0)left=0;
  if(right>239)right=239;
  for(int x=left;x<=right;++x) {
   ++samples;
   if(x+1>topLeft&&x<topRight&&mask.coverage(x,top)>0)
    depth+=face_physics::clamp(mask.bottomAt(x)-top,0,80);
  }
  if(samples)depth/=samples;
 }
 float lateral=0;
 if(corners<1) {
  const float labelLeft=cx-width/2,labelRight=cx+width/2;
  const int end=static_cast<int>(ceilf(cy+height/2));
  for(int y=top;y<end&&y<135;++y) {
   float faceLeft,faceRight;mask.rowBounds(y,faceLeft,faceRight);
   if(faceRight<=faceLeft)continue;
   const float overlapLeft=fmaxf(labelLeft,faceLeft);
   const float overlapRight=fminf(labelRight,faceRight);
   if(overlapRight<=overlapLeft)continue;
   const int x=static_cast<int>(fmaxf(0,fminf(239,(overlapLeft+overlapRight)/2)));
   if(mask.coverage(x,y)<=0)continue;
   const float penetration=side<0?labelRight-faceLeft:faceRight-labelLeft;
   lateral+=face_physics::clamp(penetration,0,80);
  }
  if(end>top)lateral/=(end-top);
 }
 // Average the advancing contour, not its maximum. A single deep overlap used
 // to saturate at glyph width/height and hide the rest of the authored poke.
 return avatar_scene::mix(lateral,depth,corners);
}
struct CounterSpring {
 float x=0,y=0,vx=0,vy=0,accumulator=0,previousDepth=0;
 bool touching=false;
 void update(float seconds,float depth,bool readable,float side) {
  if(!readable) { previousDepth=0;touching=false; }
  else {
   // First overlap establishes a baseline, preserving intentional static
   // overlaps. Only an advancing contact transfers an impulse to the label.
   if(touching) {
    const float advance=fmaxf(0,depth-previousDepth);
    const float impulse=fminf(24,advance*18);
    vx+=side*impulse*.65f;vy+=impulse;
   }
   previousDepth=depth;touching=true;
  }
  vx=face_physics::clamp(vx,-55,55);vy=face_physics::clamp(vy,-55,55);
  accumulator+=face_physics::clamp(seconds,0,.1f);
  constexpr float dt=1.0f/120;
  while(accumulator+0.000001f>=dt) {
   accumulator=fmaxf(0,accumulator-dt);
   vx+=(-105*x-12*vx)*dt;vy+=(-105*y-12*vy)*dt;
   x+=vx*dt;y+=vy*dt;
   x=face_physics::clamp(x,-3,3);y=face_physics::clamp(y,-2.5f,3);
  }
  // Settle EXACTLY at rest: the label then renders on the crisp integer path
  // instead of a lingering sub-pixel resample of an invisible residual.
  if(fabsf(x)<.01f&&fabsf(vx)<.05f)x=vx=0;
  if(fabsf(y)<.01f&&fabsf(vy)<.05f)y=vy=0;
 }
};
} // namespace avatar_interaction
