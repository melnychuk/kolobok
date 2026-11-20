#pragma once
#include "avatar_behavior.h"

// A pose-only director, shared by firmware and the live header. Status indicators
// never wait for it. No queues: a new mode redirects from the displayed pose.
namespace avatar_behavior {
inline float bounded(float v,float lo,float hi) { return v<lo?lo:v>hi?hi:v; }
// A glance toward a counter on screen side `sign`, never past the center.
inline float toward(float sign,float v,float limit) {
 return sign<0?bounded(v,-limit,0):bounded(v,0,limit);
}
// Gaze offsets toward each counter's screen position (see status_sides).
struct Targets {
 float doneX=12.0f*status_sides::kDone,doneY=0,activeX=12.0f*status_sides::kActive,activeY=0;
};
inline Targets targets(float faceX,float faceY,float doneX,float activeX,float countY,
                       bool done,bool active) {
 Targets t;
 t.doneX=done?toward(status_sides::kDone,(doneX-faceX)*.15f,14):0;
 t.activeX=active?toward(status_sides::kActive,(activeX-faceX)*.15f,14):0;
 t.doneY=done?bounded((countY-faceY)*.18f,-10,10):0;
 t.activeY=active?bounded((countY-faceY)*.18f,-10,10):0;
 return t;
}
enum class Beat : uint8_t { orient,request,resume,complete,listen,release,doze,wake,disconnect,reconnect };
struct Performance { eye_acting::Pose eyes,body; bool transitioning; };
struct Director {
 Mode mode=Mode::offline;
 Beat beat=Beat::orient;
 bool initialized=false,captured=false,running=false,completion=false,yielded=false;
 uint32_t started=0,capturedAt=0,seed=0x8e5b7a19u;
 bool pairReturn=false,quietHold=false,accentActive=false;
 uint32_t accentAt=0;
 uint32_t draw() {
  seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;
 }
 eye_acting::Pose visibleEyes=eye_acting::p(),visibleBody=eye_acting::p();
 eye_acting::Pose velocityEyes=eye_acting::p(),velocityBody=eye_acting::p();
 eye_acting::Key keys[4]={};
 uint16_t length=0;
 void notifyCompletion() { completion=true; }
 bool busy(uint32_t now) const { return running&&now-started<length+90u; }
 static Beat reason(Mode from,Mode to,bool fresh) {
  if(to==Mode::listening)return Beat::listen;
  if(to==Mode::waiting)return Beat::request;
  if(to==Mode::offline)return Beat::disconnect;
  if(to==Mode::sleeping)return Beat::doze;
  if(from==Mode::sleeping)return Beat::wake;
  if(from==Mode::offline)return Beat::reconnect;
  if(from==Mode::listening)return Beat::release;
  if(to==Mode::finished&&fresh)return Beat::complete;
  if(from==Mode::waiting&&to==Mode::working)return Beat::resume;
  return Beat::orient;
 }
 // Capture the composed pose AFTER cues/life, not the underlying clip. Only
 // translational momentum is inherited; eye-size springs/rim retain their own
 // velocities. Caps bound recovery from sparse/stalled frame sampling.
 void capture(uint32_t now,const eye_acting::Pose& eyes,const eye_acting::Pose& body) {
  const uint32_t elapsed=now-capturedAt;
  const bool measure=captured&&elapsed>0&&elapsed<=100;
  const float seconds=elapsed*.001f;
#define VELOCITY(field,cap) \
  velocityEyes.field=measure?bounded((eyes.field-visibleEyes.field)/seconds,-cap,cap):0; \
  velocityBody.field=measure?bounded((body.field-visibleBody.field)/seconds,-cap,cap):0
  VELOCITY(x,120);VELOCITY(y,120);VELOCITY(lean,60);VELOCITY(lift,60);
#undef VELOCITY
  visibleEyes=eyes;visibleBody=body;capturedAt=now;captured=true;
 }
 void begin(Mode next,uint32_t now,const Targets& t,bool fresh) {
  beat=reason(mode,next,fresh);
  mode=next;started=now;running=true;pairReturn=false;quietHold=false;
  auto notice=rest(next),respond=rest(next),settle=rest(next);
  uint16_t a=180,b=440;length=760;
  if(beat==Beat::request) {
    a=130;b=300;length=500;
    notice=eye_acting::withEyeScale(eye_acting::withGaze(notice,0,-2),.985f,1.025f);notice.height=1.20f;
    notice.slantL=.03f;notice.slantR=-.04f;
    respond.height=1.24f;respond.width=1.13f;respond.lift=-2;
  } else if(beat==Beat::listen) {
    a=110;b=230;length=380;
    notice.height=1.14f;notice.y=-3;
    respond.height=1.16f;respond.lift=-1;respond.flow=.45f;
    respond.lowerL=respond.lowerR=.035f;
  } else if(beat==Beat::resume) {
    a=160;b=360;length=680;
    notice=eye_acting::withGaze(notice,t.activeX*.45f,t.activeY*.45f);
    respond=eye_acting::withLids(eye_acting::withGaze(respond,t.activeX,t.activeY),.12f,.18f);
    respond.lean=t.activeX*.22f;respond.flow=.5f;
    respond.lowerL=.10f;respond.lowerR=.07f;respond.slantL=.04f;respond.slantR=.02f;
    // Per-eye lids were authored with active work on the right.
    if(status_sides::kMirrorAuthored)respond=eye_acting::swapEyes(respond);
  } else if(beat==Beat::complete) {
    a=180;b=470;length=940;
    notice=eye_acting::withGaze(notice,t.doneX*.35f,t.doneY*.35f);notice.height=1.06f;
    respond=eye_acting::withGaze(respond,t.doneX,t.doneY);
    respond.smile=.72f;respond.height=1.08f;respond.lift=-2;respond.lean=t.doneX*.2f;
  } else if(beat==Beat::doze) {
    a=220;b=500;length=1050;
    notice=eye_acting::withBody(notice,0,2);notice.y=3;notice.closeL=notice.closeR=.65f;
    respond.y=4;respond.height=.94f;respond.closeL=respond.closeR=.18f;respond.lift=3;
    settle.y=4;settle.closeL=settle.closeR=1;settle.flow=.35f;
  } else if(beat==Beat::wake) {
    a=180;b=400;length=760;
    notice=eye_acting::withEyeScale(notice,1.02f,.985f);notice.height=1.08f;notice.y=-2;
    respond.height=1.12f;respond.bodyHeight=1.04f;respond.lift=-2;
    respond.x=next==Mode::finished?t.doneX*.6f:t.activeX*.6f;
    respond.y=next==Mode::finished?t.doneY*.6f:t.activeY*.6f;
  } else if(beat==Beat::disconnect) {
    a=220;b=480;length=820;
    notice=eye_acting::withGaze(visibleEyes,0,1);notice.smile=0;
    respond=rest(next);respond.y=5;respond.flow=.35f;
  } else if(beat==Beat::release) {
    // The physical release cue owns the acknowledgment. This is only a calm
    // redirection, never a claim that ASR/submission succeeded.
    a=180;b=380;length=680;
    notice=eye_acting::withGaze(rest(Mode::listening),0,0);
    respond=rest(next);respond.y=-1;
  } else if(beat==Beat::reconnect) {
    a=160;b=360;length=680;
    notice.height=1.06f;notice.y=-1;
    respond=rest(next); // Baseline recovery: no bounce or completion accent.
  } else {
    notice=eye_acting::withGaze(visibleEyes,visibleEyes.x*.5f,visibleEyes.y*.5f);
    respond=rest(next);
    if(next==Mode::working) {
     respond=eye_acting::withGaze(respond,t.activeX*.6f,t.activeY*.6f);
     respond.upperL=.10f;respond.upperR=.16f;
     if(status_sides::kMirrorAuthored)respond=eye_acting::swapEyes(respond);
    }
  }
  keys[0]={0,visibleEyes};keys[1]={a,notice};keys[2]={b,respond};keys[3]={length,settle};
 }
 eye_acting::Pose sample(uint32_t elapsed,bool body) const {
  const auto& entry=body?sourceBody:sourceEyes;
  auto pose=keys[3].pose;
  for(int i=1;i<4;++i)if(elapsed<keys[i].ms) {
   const auto& from=i==1?entry:keys[i-1].pose;
   const float t=(elapsed-keys[i-1].ms)/static_cast<float>(keys[i].ms-keys[i-1].ms);
   // Transition tangents are explicit: do not infer a snap from distance.
   pose=eye_acting::mix(from,keys[i].pose,eye_acting::smooth(t));break;
  }
  // Quintic interpolation has zero initial velocity. Add a bounded Hermite
  // tangent that is zero at both ends, including zero derivative on arrival.
  if(elapsed<keys[1].ms) {
   const float s=elapsed/static_cast<float>(keys[1].ms);
   const float weight=s*(1-s)*(1-s)*keys[1].ms*.001f;
   const auto& velocity=body?tangentBody:tangentEyes;
   pose.x+=velocity.x*weight;pose.y+=velocity.y*weight;
   pose.lean+=velocity.lean*weight;pose.lift+=velocity.lift*weight;
  }
  return pose;
 }
 Performance step(Mode next,eye_acting::Player& player,uint32_t now,uint32_t randomValue,
                  bool hold,const Targets& t,bool external=false) {
  if(!initialized) {
   initialized=true;mode=next;seed^=randomValue;if(!seed)seed=1;
   if(!captured)visibleEyes=visibleBody=rest(next);
   player.entryEyes=visibleEyes;player.entryBody=visibleBody;player.index=-1;
  } else if(next!=mode||(completion&&next==Mode::finished)) {
   begin(next,now,t,completion);
   // Freeze transition source/tangents: later capture calls only update the
   // next interruption's source, never the active curve (see frozen fields).
   sourceEyes=visibleEyes;sourceBody=visibleBody;tangentEyes=velocityEyes;tangentBody=velocityBody;
   player.index=-1;
  }
  if(completion&&next==Mode::finished&&!external) { accentActive=true;accentAt=now; }
  if(next==Mode::waiting||next==Mode::listening||next==Mode::offline||next==Mode::sleeping||external)
   accentActive=false;
  if(accentActive&&now-accentAt>=520)accentActive=false;
  completion=false; // Never defer an event through a higher-priority mode.
  if(external) { running=false;yielded=true;pairReturn=false;quietHold=false; }
  else if(yielded) {
   yielded=false;player.index=-1;
   player.entryEyes=visibleEyes;player.entryBody=visibleBody;
  }
  if(running) {
   const uint32_t elapsed=now-started;
   Performance output={sample(elapsed,false),sample(elapsed>90?elapsed-90:0,true),busy(now)};
   if(!output.transitioning) {
    running=false;player.entryEyes=output.eyes;player.entryBody=output.body;player.index=-1;
   }
   return output;
  }
  if(scripted(next)) {
   const bool due=player.index<0||player.state!=state(next)||
       (!hold&&now-player.started>=eye_acting::duration(eye_acting::clip(player.state,player.index))+player.pause);
   // Draw only at clip boundaries: frame cadence/life/check RNG never consume
   // the performance stream. Existing per-state no-repeat bags stay authoritative.
   player.update(state(next),now,due?draw():0,hold);
   if(due) {
    quietHold=false;
    if(next==Mode::working||(next==Mode::finished&&player.calm)) {
     const uint32_t choice=draw();
     if(pairReturn) { player.pause+=2200;pairReturn=false;quietHold=true; }
     else if(choice%5==0) { player.pause=700;pairReturn=true; }
     else if(choice%3==0) {
      player.pause=static_cast<uint16_t>(player.pause+2400+choice%2400);quietHold=true;
     }
    }
   }
   auto eyes=player.eyes(now);
   if(quietHold&&next==Mode::working&&player.resting(now)) {
    const uint32_t age=now-player.started-eye_acting::duration(eye_acting::clip(player.state,player.index));
    const float weight=eye_acting::smooth(fminf(age/260.0f,
        age<player.pause?(player.pause-age)/260.0f:0));
    eyes.lowerL+=.07f*weight;eyes.lowerR+=.04f*weight;
    eyes.scaleL+=.01f*weight;eyes.scaleR-=.01f*weight;
   }
   return {eyes,player.body(now),false};
  }
  const auto pose=rest(next);
  return {pose,pose,false};
 }
 // Two tiny green glints above the completion label. A single real edge owns
 // a 520ms fade; no particle system, repeated timer, sound or label overlap.
 template<class Pixel> void accents(uint32_t now,float labelX,float labelTop,Pixel pixel) const {
  if(!accentActive||now-accentAt>=520)return;
  const float ms=now-accentAt;
  const float alpha=.65f*eye_acting::smooth(ms/120)*(1-eye_acting::smooth((ms-140)/380));
  if(alpha<=0)return;
  for(int star=0;star<2;++star) {
   const int x=static_cast<int>(labelX)+(star?9:-8);
   const int y=static_cast<int>(labelTop)-(star?16:9);
   if(y<3)continue;
   pixel(x,y,alpha);
   for(int arm=1;arm<=2;++arm) {
    const float a=alpha*(arm==1?.8f:.4f);
    pixel(x-arm,y,a);pixel(x+arm,y,a);pixel(x,y-arm,a);pixel(x,y+arm,a);
   }
  }
 }
 // Active curves keep their entry immutable while capture tracks interruptions.
 eye_acting::Pose sourceEyes=eye_acting::p(),sourceBody=eye_acting::p();
 eye_acting::Pose tangentEyes=eye_acting::p(),tangentBody=eye_acting::p();
};
} // namespace avatar_behavior
