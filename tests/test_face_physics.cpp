#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <initializer_list>
#include "../firmware/roman_counts.h"
#include "../firmware/avatar_scene.h"
#include "../firmware/display_fonts.h"
#include "../firmware/status_background.h"
#include "../firmware/count_pop.h"

float energy(const face_physics::Surface& surface) {
 float result=0;
 for(int i=0;i<surface.count;++i)result+=surface.displacement[i]*surface.displacement[i]+.05f*surface.velocity[i]*surface.velocity[i];
 return result;
}
int main() {
 using namespace face_physics;
 const auto neutral=eye_acting::p();
 Surface a,b;a.kick(1,.7f);b.kick(1,.7f);
 for(int frame=0;frame<100;++frame)a.step(.033f,neutral,true);
 for(int frame=0;frame<200;++frame)b.step(.0165f,neutral,true);
 assert(fabsf(a.x-b.x)<.1f&&fabsf(a.y-b.y)<.1f);
 for(int i=0;i<a.count;++i)assert(fabsf(a.displacement[i]-b.displacement[i])<.1f);
 for(int s=0;s<2;++s)for(int i=0;i<kEdgeNodes;++i)
  assert(fabsf(a.edge[s][i]-b.edge[s][i])<.1f);

 // Side membranes: a count cue swells only the cued side, then travels and
 // settles. Starting in a wide/tall pose must not fling the sides on frame one.
 {
  Surface cue;cue.kick(1,.9f);
  // The swell impulse is one-sided; the other side only drags with the body.
  for(int i=0;i<kEdgeNodes;++i)assert(cue.edgeVelocity[0][i]==0&&cue.edgeVelocity[1][i]>=0);
  for(int tick=0;tick<12;++tick)cue.step(1.0f/120,neutral,false);
  // Elastic lunge: the cued side swells past the rigid edge while the
  // trailing side drags behind the moving body.
  assert(cue.edge[1][9]>1&&cue.edge[0][9]<-1);
  for(int tick=0;tick<720;++tick)cue.step(1.0f/120,neutral,false);
  for(int s=0;s<2;++s)for(int i=0;i<kEdgeNodes;++i)
   assert(fabsf(cue.edge[s][i])<.05f&&fabsf(cue.edgeVelocity[s][i])<.5f);
  Surface primed;auto wide=neutral;wide.width=1.12f;wide.bodyHeight=1.1f;
  primed.step(.033f,wide,false);
  for(int s=0;s<2;++s)for(int i=0;i<kEdgeNodes;++i)assert(primed.edge[s][i]==0);
  // Swaying right leaves loose upper/lower flesh behind more than the core.
  Surface sway;auto right=neutral;right.lean=12;
  for(int tick=0;tick<10;++tick)sway.step(1.0f/120,right,false);
  for(int s=0;s<2;++s)assert(sway.edge[s][0]<sway.edge[s][9]&&sway.edge[s][9]<0);
  // A widening body lags at first: the right side is briefly inside the
  // rigid oval and the left side outside, then both catch up.
  Surface grow;grow.step(.033f,neutral,false);
  auto broad=neutral;broad.width=1.1f;grow.step(1.0f/120,broad,false);
  assert(grow.edge[1][9]<-2&&grow.edge[0][9]>2);
  // Visible deviations saturate softly instead of hitting a hard wall.
  Surface wild;
  for(int i=0;i<kEdgeNodes;++i)wild.edge[1][i]=40;
  for(float row=-30;row<170;row+=.5f) {
   const float value=wild.edgeSample(1,row);
   assert(isfinite(value)&&value<Surface::kEdgeSoftLimit&&value>8);
  }
  for(int frame=0;frame<400;++frame) {
   auto pose=neutral;pose.lean=(frame/37%2?-14:14);pose.width=frame/23%2?.8f:1.2f;
   pose.bodyHeight=frame/29%2?.85f:1.15f;pose.lift=frame/31%2?-10:4;
   if(frame%90==0)wild.kick(frame%180?1:-1,1);
   wild.step(.033f,pose,false);
   for(int s=0;s<2;++s)for(int i=0;i<kEdgeNodes;++i) {
    assert(isfinite(wild.edge[s][i])&&isfinite(wild.edgeVelocity[s][i]));
    assert(fabsf(wild.edge[s][i])<=Surface::kEdgeHardLimit+.001f);
   }
  }
 }
 // Side/up translation beats the old spring timing, with the same damping
 // ratio and no acceleration of the separate rim simulation.
 Surface quick;auto goal=neutral;goal.lean=12;goal.lift=-8;
 float oldX=0,oldY=0,oldVx=0,oldVy=0;
 for(int tick=0;tick<36;++tick) {
  constexpr float dt=1.0f/120;
  quick.step(dt,goal,false);
  oldVx+=(-22*(oldX-goal.lean)-4.8f*oldVx)*dt;
  oldVy+=(-28*(oldY-goal.lift)-4.2f*oldVy)*dt;
  oldX+=oldVx*dt;oldY+=oldVy*dt;
 }
 assert(quick.x>oldX*1.25f&&quick.y<oldY*1.25f);
 for(int tick=0;tick<1200;++tick)quick.step(1.0f/120,goal,false);
 assert(fabsf(quick.x-goal.lean)<.01f&&fabsf(quick.y-goal.lift)<.01f);

 // Local disturbances travel along the rim, rather than each point merely
 // following its own periodic oscillator. Then the unforced system settles.
 Surface wave;wave.velocity[4]=90;
 const float initial=energy(wave);
 for(int tick=0;tick<60;++tick)wave.step(1.0f/120,neutral,false);
 assert(fabsf(wave.displacement[10])>.01f);
 for(int tick=0;tick<960;++tick)wave.step(1.0f/120,neutral,false);
 assert(energy(wave)<initial*.05f);
 Surface repeat;repeat.kick(-1,.95f);
 for(int frame=0;frame<600;++frame) {
  repeat.step(.033f,neutral,true);
  assert(isfinite(repeat.x)&&isfinite(repeat.y)&&isfinite(repeat.impact));
  assert(repeat.x>=-22&&repeat.x<=22&&repeat.y<=0&&repeat.y>=-22);
  for(int i=0;i<repeat.count;++i) {
   assert(isfinite(repeat.displacement[i])&&isfinite(repeat.velocity[i]));
   assert(fabsf(repeat.displacement[i])<=18.001f);
  }
 }
 Reaction reaction;reaction.notify(1,false,100);
 const auto active=reaction.apply(neutral,410,false);
 assert(active.x>8&&active.y<-3&&active.smile>.7f&&active.height>1.15f);
 const auto happyBody=reaction.body(neutral,410,false);
 assert(happyBody.smile>.7f&&happyBody.lift<-3);
 assert(reaction.body(neutral,180,false).smile==0);
 const auto early=reaction.apply(neutral,280,false);
 assert(early.x==14&&early.y==-5); // Upward release arrives by 180ms, not 240ms.
 assert(early.height<1.18f); // Happy shape still follows its original clock.
 const auto happyBlink=reaction.apply(neutral,630,false);
 assert(happyBlink.closeL>.99f&&happyBlink.smile>.7f);
 const auto blinkBody=reaction.body(neutral,720,false);
 assert(blinkBody.closeL==0&&blinkBody.closeR==0);
 reaction.notify(1,false,100,false); // Decreases acknowledge; only arrivals excite.
 assert(reaction.apply(neutral,410,false).smile==0);
 reaction.notify(-1,true,500);
 const auto completed=reaction.apply(neutral,820,false);
 assert(completed.x<-8&&completed.smile>.4f);
 const auto request=reaction.apply(neutral,830,true);
 assert(request.x==neutral.x&&request.y==neutral.y&&request.smile==neutral.smile);
 assert(!reaction.active); // Request cancels, rather than replaying later.
 reaction.notify(1,false,500);
 reaction.apply(neutral,1800,false);assert(!reaction.active);
 // Happy hold is readable and the blend returns continuously to base acting.
 reaction.notify(1,false,0);
 assert(reaction.apply(neutral,800,false).smile>.7f);
 assert(reaction.apply(neutral,1200,false).smile==0);

 // Per-frame sliced field evaluation is bit-identical to the plain formula.
 {
  const auto plainNoise=[](float x,float y,float z) {
   const int ix=static_cast<int>(floorf(x)),iy=static_cast<int>(floorf(y)),iz=static_cast<int>(floorf(z));
   const float u=fluid_face::fade(x-ix),v=fluid_face::fade(y-iy),w=fluid_face::fade(z-iz);
   using fluid_face::hash;using fluid_face::mix;
   const float a=mix(mix(hash(ix,iy,iz),hash(ix+1,iy,iz),u),mix(hash(ix,iy+1,iz),hash(ix+1,iy+1,iz),u),v);
   const float b=mix(mix(hash(ix,iy,iz+1),hash(ix+1,iy,iz+1),u),mix(hash(ix,iy+1,iz+1),hash(ix+1,iy+1,iz+1),u),v);
   return mix(a,b,w);
  };
  for(float t=0;t<40;t+=.613f) {
   const fluid_face::Frame frame(t);
   for(int x=-20;x<260;x+=3)for(int y=-10;y<150;y+=7) {
    const float px=x*.018f+t*.12f,py=y*.018f-t*.06f;
    const float qx=plainNoise(px*.6f,py*.6f,t*.08f+17)*.80f;
    const float qy=plainNoise(px*.6f+9,py*.6f-7,t*.08f)*.80f;
    const float a=px+qx,b=py+qy;
    const float expected=.67f*plainNoise(a,b,t*.17f)+.33f*plainNoise(1.6f*a-1.2f*b+13,1.2f*a+1.6f*b-5,t*.26f);
    assert(fluid_face::field(x,y,frame)==expected&&fluid_face::field(x,y,t)==expected);
   }
  }
 }
 // noise() floors without floorf: the same integer for any finite value in range,
 // including -0, values just either side of integers and the extremes.
 {
  const auto same=[](float x){return fluid_face::floorInt(x)==static_cast<int>(floorf(x));};
  assert(same(-0.0f)&&same(0.0f)&&same(2147483520.0f)&&same(-2147483648.0f));
  for(int i=-70000;i<=70000;++i) {
   const float f=static_cast<float>(i);
   assert(same(f)&&same(nextafterf(f,-1e9f))&&same(nextafterf(f,1e9f)));
  }
  for(float x=-600;x<600;x+=.0137f)assert(same(x));
 }
 // The fluid flow clock wraps at an exact period of the field: it stays precise
 // for any uptime and the wrap is invisible.
 {
  float worst=0;
  for(float t=0;t<30;t+=.53f)for(int x=0;x<240;x+=11)for(int y=0;y<135;y+=13)
   worst=fmaxf(worst,fabsf(fluid_face::field(x,y,t)-fluid_face::field(x,y,t+fluid_face::kFlowPeriod)));
  assert(worst<2e-3f);
  float clock=fluid_face::kFlowPeriod-.01f;clock=fluid_face::advance(clock,.02f);
  assert(clock>=0&&clock<.02f);
  // A week of 30fps frames at the slowest flow stays bounded and advancing.
  float slow=0;for(int i=0;i<7*24*3600*30;i+=1000)slow=fluid_face::advance(slow,.35f*.55f/30*1000);
  assert(slow>=0&&slow<fluid_face::kFlowPeriod);
  const float before=7999.9f;assert(fluid_face::advance(before,.0064f)>before);
 }
 count_pop::Pop pop;
 pop.observe(1);pop.update(100,false);
 assert(pop.pending&&!pop.running); // Arrival waits until faded digits can be read.
 pop.update(500,true);assert(pop.running);
 assert(pop.lift(570)>1.9f&&pop.lift(700)<-9.9f);
 pop.observe(1);pop.update(800,true);assert(pop.started==500); // Heartbeat is inert.
 pop.update(1120,true);assert(!pop.running&&pop.lift(1120)==0);
 pop.observe(2);pop.update(1200,true);assert(pop.started==1200);
 pop.observe(1);pop.update(1300,true);assert(pop.started==1300); // Decrease pops too.
 pop.observe(0);pop.update(1400,true);assert(!pop.pending&&!pop.running);
 // Unsigned elapsed arithmetic survives the millis() wrap.
 pop.observe(3);pop.update(UINT32_MAX-99,true);
 assert(pop.lift(100)<-9.9f);pop.update(600,true);assert(!pop.running);
 for(int count: {1,2,4,8,38,88,99}) {
  char text[10];roman_counts::format(count,text);
  for(const auto* font: {&kBadgeFont,&kMediumBadgeFont,&kSmallBadgeFont,
                         &kTinyBadgeFont,&kCompactBadgeFont}) {
   int solid=0;
   count_raster::render(*font,text,120,106,[&](int,int y,float alpha,bool outline) {
    assert(y>=0&&y<135);if(!outline&&alpha>.99f)++solid;
   });
   assert(solid>0);
  }
 }

 // Force a real flap to overlap the count. It is no longer artificially
 // excluded from the text band; the final outline-and-fill pass stays legible.
 Surface flap;
 for(int i=2;i<=7;++i)flap.displacement[i]=16;
 avatar_scene::Scene scene;scene.surface=&flap;
 avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);
 assert(mask.coverage(24,110)>.9f);
 // The sitting layout keeps its exact contour whatever the side membranes do;
 // vertical single layouts let them move the edge facing the counter.
 {
  Surface calm,sloshed;
  for(int i=0;i<kEdgeNodes;++i){sloshed.edge[0][i]=7-i*.5f;sloshed.edge[1][i]=i*.6f-5;}
  avatar_scene::Scene paired;paired.corners=1;
  avatar_scene::Mask still,moved;
  paired.surface=&calm;avatar_scene::buildMask(paired,still);
  paired.surface=&sloshed;avatar_scene::buildMask(paired,moved);
  assert(memcmp(&still,&moved,sizeof(still))==0);
  avatar_scene::Scene single;single.corners=0;single.region=144;single.offset=-24;
  single.surface=&calm;avatar_scene::buildMask(single,still);
  single.surface=&sloshed;avatar_scene::buildMask(single,moved);
  assert(fabsf((moved.right[67]-still.right[67])-sloshed.edgeSample(1,67.5f))<.01f);
  single.side=0;assert(avatar_scene::softSides(single)==0); // listening/idle
 }
 const uint16_t green=0x262b,blue=0x3c1e,coral=0xf3b1;
 struct Fixture { int center;float done,active;uint16_t fill,portrait;float portraitPresence; };
 // Labels sit on their status_sides screen side.
 const int doneAt=status_sides::kDone<0?24:216,activeAt=240-doneAt;
 const Fixture fixtures[]={
  {doneAt,1,1,blue,0,0},{120,1,1,blue,0,0},{activeAt,1,1,blue,0,0},
  {doneAt,1,0,blue,0,0},{activeAt,0,1,blue,0,0},{120,1,1,coral,0,0},
  {doneAt,1,0,coral,green,1},{activeAt,0,1,coral,coral,1}};
 for(const auto& fixture:fixtures) {
  const auto background=[&](int x,int y) {
   return status_background::color(x,y,green,fixture.fill,fixture.done,fixture.active,
                                   fixture.portrait,fixture.portraitPresence);
  };
  uint16_t pixels[240*135]={}; // Worst case: completely black behind the label.
  int outlined=0,filled=0;
  count_raster::render(kBadgeFont,"I",fixture.center,116,[&](int x,int y,float alpha,bool outline) {
   assert(x>=0&&x<240&&y>=0&&y<135&&alpha>0&&alpha<=1);
   if(alpha>.99f) {
    pixels[y*240+x]=outline?background(x,y):0;
    if(outline)++outlined;else ++filled;
   }
  });
  assert(outlined>filled&&filled>20);
  assert(pixels[116*240+fixture.center]==0); // Solid black interior, never hollow.
  int visible=0;
  for(int y=0;y<135;++y)for(int x=0;x<240;++x) {
   const auto color=pixels[y*240+x];
   if(color) { ++visible;assert(color==background(x,y)&&color!=0xffff); }
  }
  assert(visible>20); // Status-colored halo survives overlap, never a white border.
 }
}
