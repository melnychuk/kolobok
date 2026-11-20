#include <assert.h>
#include <math.h>
#include <string.h>
#include <initializer_list>
#include "../firmware/avatar_interaction.h"
#include "../firmware/display_fonts.h"

using namespace avatar_interaction;
// Screen-side helpers: counters sit on their status_sides side.
static float at(int side,float inset) { return status_sides::fromEdge(side,inset); }
// Upper lid of the eye nearer to / farther from screen side `side`.
static float nearLid(const eye_acting::Pose& p,int side) { return side<0?p.upperL:p.upperR; }
static float farLid(const eye_acting::Pose& p,int side) { return side<0?p.upperR:p.upperL; }
int main() {
 const int kDone=status_sides::kDone,kActive=status_sides::kActive;
 const auto neutral=eye_acting::p();
 for(int state=0;state<4;++state) {
  eye_acting::Player actor;
  uint16_t seen=0;uint32_t now=0;
  const int count=eye_acting::clipCount(static_cast<eye_acting::State>(state));
  for(int i=0;i<count;++i) {
   actor.update(static_cast<eye_acting::State>(state),now,i*7919);
   assert(!(seen&(1<<actor.index)));seen|=1<<actor.index;
   now+=eye_acting::duration(eye_acting::clip(actor.state,actor.index))+eye_acting::Player::kMaxPause;
  }
  assert(seen==(1u<<count)-1);
 }
 eye_acting::Player actor;actor.update(eye_acting::State::attention,100,3);
 const auto entry=actor.eyes(150);
 actor.update(eye_acting::State::listening,150,4);
 assert(actor.state==eye_acting::State::listening);
 assert(actor.eyes(150).x==entry.x);
 for(int i=0;i<7;++i) {
  const auto& clip=eye_acting::clip(eye_acting::State::listening,i);
  assert(clip.count>=6&&eye_acting::duration(clip)>2000);
  int closed=0;
  for(uint32_t t=0;t<eye_acting::duration(clip);t+=10) {
   const auto pose=eye_acting::sample(clip,t);
   assert(isfinite(pose.x)&&pose.height>=1&&pose.height<1.3f);
   assert(pose.upperL<=.05f&&pose.upperR<=.05f);
   if(pose.closeL>.9f&&pose.closeR>.9f)++closed;
  }
  assert(closed<20); // No sleepy holds or repetitive rapid blinks.
 }
 ButtonCue button;button.notify(100);
 assert(button.apply(neutral,100).x==0);
 // A happy wink toward the completed counter, with the eye on that side.
 const auto tap=button.apply(neutral,440);
 const float tapNear=kDone<0?tap.closeL:tap.closeR,tapFar=kDone<0?tap.closeR:tap.closeL;
 assert(tap.x*kDone>0&&tap.smile>.5f&&tapNear>.99f&&tapFar==0);
 const auto tapBody=button.apply(neutral,530,true);
 assert(tapBody.closeL==0&&tapBody.closeR==0);
 button.update(1400,false);assert(!button.active);
 button.notify(2000,true);
 assert(button.apply(neutral,2470).smile>.3f);
 button.update(2480,true);assert(!button.active);
 button.notify(UINT32_MAX-99);assert(button.apply(neutral,240).x*kDone>0);
 Checks checks;checks.fixed=true;checks.update(100,true);
 const auto activeLook=checks.apply(neutral,2400);
 const auto doneLook=checks.apply(neutral,5400);
 assert(activeLook.x*kActive>10&&activeLook.y>8&&activeLook.smile==0);
 assert(farLid(activeLook,kActive)>=.22f&&nearLid(activeLook,kActive)>=.24f);
 assert(activeLook.height<.95f&&activeLook.closeL==0&&activeLook.closeR==0);
 assert(doneLook.x*kDone>10&&doneLook.y>8&&doneLook.smile>.6f&&doneLook.upperL==0&&doneLook.upperR==0);
 assert(checks.apply(neutral,100).x==0);
 checks.update(5500,false);assert(checks.apply(neutral,5600).x==0);
 checks.update(7000,true);assert(checks.apply(neutral,7000).x==0);
 // Live scheduler: occasional, varied checks only between clips, holding the
 // player; never the old fixed 8.4s rhythm.
 {
  eye_acting::Player player;Checks live;
  face_physics::Surface surface;avatar_scene::Scene scene;
  uint32_t previousStart=0;int starts=0,glances=0,presses=0,activeSides=0,doneSides=0,kisses=0;
  bool firstPress=false,sawLongGap=false,sawShortGap=false;
  int previousSide=0;uint32_t lastClipStart=0;
  for(uint32_t t=0;t<600000;t+=33) {
   const int before=live.side;
   player.update(eye_acting::State::thinking,t,t*2654435761u,live.busy(t));
   if(player.started!=lastClipStart) {
    lastClipStart=player.started;
    assert(!live.busy(t)); // A held clip never restarts during a check.
   }
   live.update(t,true,&surface,true,true,player.resting(t));
   if(live.side!=0&&before==0) {
    assert(player.resting(t)); // Begins only between clips.
    if(starts) {
     const uint32_t gap=t-previousStart;
     assert(gap>=Checks::kActiveLength+Checks::kGapMin);
     if(gap<10500)sawShortGap=true;
     if(gap>13500)sawLongGap=true;
    } else firstPress=live.press;
    previousStart=t;++starts;
    if(live.press)++presses;else ++glances;
    if(live.side==kActive)++activeSides;else ++doneSides;
    if(live.kissing)++kisses;
    (void)previousSide;previousSide=live.side;
   }
   if(live.busy(t)) {
    const auto look=live.apply(eye_acting::p(),t);
    assert(isfinite(look.x)&&fabsf(look.x)<=24);
    live.shape(scene,t,at(kDone,30),at(kActive,30),116,30,30,23,23);
    if(!live.press)assert(scene.poke.strength==0);
   }
  }
  assert(starts>=35&&starts<=110&&firstPress);
  assert(glances>=5&&presses>=glances&&activeSides>=8&&doneSides>=8);
  assert(kisses>=1&&kisses*2<doneSides);
  assert(sawShortGap&&sawLongGap);
  // Single counters only check the side that exists.
  Checks single;eye_acting::Player solo;
  for(uint32_t t=0;t<120000;t+=33) {
   solo.update(eye_acting::State::done,t,t*7919u,single.busy(t));
   single.update(t,true,nullptr,true,false,solo.resting(t));
   assert(single.side==0||single.side==kDone);
  }
  assert(single.count>=4);
  // Disabling or a presence change cancels the running check at once.
  single.update(120033,false);assert(!single.busy(120033)&&single.side==0);
 }
 CounterSpring spring;
 spring.update(.033f,5,true,-1);assert(spring.x==0&&spring.y==0);
 spring.update(.033f,8,true,-1);assert(spring.x<0&&spring.y>0);
 for(int i=0;i<200;++i)spring.update(.033f,8,true,-1);
 assert(spring.x==0&&spring.y==0&&spring.vx==0&&spring.vy==0); // Settles exactly (crisp label).
 assert(fabsf(spring.x)<.001f&&fabsf(spring.y)<.001f);
 CounterSpring a,b;
 for(int i=0;i<100;++i)a.update(.033f,fminf(8,i*.264f),true,1);
 for(int i=0;i<200;++i)b.update(.0165f,fminf(8,i*.132f),true,1);
 assert(fabsf(a.x-b.x)<.1f&&fabsf(a.y-b.y)<.1f);
 for(int i=0;i<2000;++i) {
  spring.update(.033f,i%2?1000:0,true,-1);
  assert(isfinite(spring.x)&&isfinite(spring.y));
  assert(fabsf(spring.x)<=3&&spring.y>=-2.5f&&spring.y<=3);
 }
 // Forced real rim contact: deeper face mask pushes numerals, not task events.
 avatar_scene::Scene scene;face_physics::Surface surface;scene.surface=&surface;
 avatar_scene::Mask mask;
 for(int i=1;i<10;++i)surface.displacement[i]=4;
 avatar_scene::buildMask(scene,mask);
 const float initial=contactDepth(mask,24,116,14,23);
 CounterSpring contact;contact.update(.033f,initial,true,-1);
 for(int i=1;i<10;++i)surface.displacement[i]=16;
 avatar_scene::buildMask(scene,mask);
 const float pushed=contactDepth(mask,24,116,14,23);
 assert(pushed>initial&&pushed>0);
 contact.update(.033f,pushed,true,-1);
 assert(contact.x<0&&contact.y>0);
 // All interior strokes remain solid black and outlined after displacement.
 int interiors=0,outlines=0;
 count_raster::render(kBadgeFont,"I",24+contact.x,116+contact.y,
  [&](int x,int y,float alpha,bool outline) {
   assert(x>=0&&x<240&&y>=0&&y<135);
   if(alpha>.99f){if(outline)++outlines;else ++interiors;}
  });
 assert(interiors>20&&outlines>interiors);
 // The complete authored mixed check produces actual moving contacts too.
 surface=face_physics::Surface{};
 Checks mixed;mixed.fixed=true;mixed.update(0,true);CounterSpring done,active;
 float maxDone=0,maxActive=0;
 const float doneX=at(kDone,32),activeX=at(kActive,24);
 for(uint32_t t=0;t<8400;t+=33) {
  mixed.update(t,true,&surface);
  scene.body=mixed.apply(neutral,t,true);
  surface.step(.033f,scene.body,true);
  scene.flowClock=t*.0006f;
  mixed.shape(scene,t,doneX,activeX,116,30,30,23,23);
  avatar_scene::buildMask(scene,mask);
  done.update(.033f,contactDepth(mask,doneX,116,30,23),true,kDone);
  active.update(.033f,contactDepth(mask,activeX,116,30,23),true,kActive);
  maxDone=fmaxf(maxDone,fabsf(done.y));maxActive=fmaxf(maxActive,fabsf(active.y));
 }
 assert(maxDone>.2f&&maxActive>.2f);
 // Single-counter layouts reach the real side edge, including narrow I.
 for(int side=-1;side<=1;side+=2)for(const char* text:{"I","III","LXXXVIII"}) {
  const auto& font=strcmp(text,"LXXXVIII")==0?kCompactBadgeFont:kBadgeFont;
  const float width=count_raster::width(font,text);
  const float strip=fmaxf(48,width+16);
  scene=avatar_scene::Scene{};surface=face_physics::Surface{};
  scene.surface=&surface;scene.region=240-2*strip;scene.offset=-side*strip/2;
  scene.corners=0;scene.eyeY=67.5f;
  // `side` is the screen side of the only counter; its status follows.
  const bool isDone=side==kDone;
  Checks single;single.fixed=true;CounterSpring numeral;
  float maxMotion=0,maxDepth=0;
  for(uint32_t t=0;t<8400;t+=33) {
   single.update(t,true,&surface,isDone,!isDone);
   scene.body=single.apply(neutral,t,true);
   const auto eyes=single.apply(neutral,t);
   assert(side*eyes.x>=0); // Never inspect the absent counter.
   if(isDone)assert(eyes.upperL==0&&eyes.upperR==0);
   else assert(eyes.smile==0);
   surface.step(.033f,scene.body,false);scene.flowClock=t*.0006f;
   single.shape(scene,t,at(kDone,strip/2),at(kActive,strip/2),67.5f,width,width,font.capHeight,font.capHeight);
   avatar_scene::buildMask(scene,mask);
   const float depth=contactDepth(mask,side<0?strip/2:240-strip/2,67.5f,
                                  width,font.capHeight,0,side);
   numeral.update(.033f,depth,true,side);
   maxDepth=fmaxf(maxDepth,depth);maxMotion=fmaxf(maxMotion,fabsf(numeral.x));
   assert(isfinite(numeral.x)&&fabsf(numeral.x)<=3&&numeral.y>=-2.5f&&numeral.y<=3);
  }
  assert(maxDepth>0&&maxMotion>.2f);
  for(int i=0;i<200;++i)numeral.update(.033f,0,false,side);
  assert(fabsf(numeral.x)<.001f&&fabsf(numeral.y)<.001f);
  // Counter disappearance/reflow resets the check, rather than teleporting it.
  single.update(8500,true,&surface,!isDone,isDone);
  assert(single.apply(neutral,8500).x==0);
  single.update(8600,false,&surface,!isDone,isDone);
  assert(single.apply(neutral,8700).x==0);
  single.update(8800,true,&surface,false,false);assert(!single.enabled);
 }
}
