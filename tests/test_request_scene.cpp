// Input-request layout: pulsing field, `!` sign, low dome and small hops.
#include <assert.h>
#include <initializer_list>
#include <math.h>
#include "../firmware/avatar_scene.h"
#include "../firmware/status_background.h"
#include "../firmware/request_scene.h"

namespace {
avatar_scene::Scene requestScene(const eye_acting::Pose& eyes,const eye_acting::Pose& body) {
 avatar_scene::Scene scene;
 scene.corners=1;scene.alert=1;scene.side=1;scene.region=144;
 scene.awakeWidth=50;scene.awakeHeight=avatar_scene::kRequestAwakeHeight;scene.gap=15.2f;
 scene.eyes=eyes;scene.body=body;
 scene.eyeWidth=50*eyes.width;scene.eyeHeight=fmaxf(avatar_scene::kRequestEyeFloor,44*eyes.height);
 scene.eyeX=120+eyes.x;scene.eyeY=avatar_scene::eyeBaseline(1,1,0)+eyes.y;
 return scene;
}
float eyeInk(const avatar_scene::Scene& scene,bool clipped) {
 float ink=0;
 if(clipped)avatar_scene::eyes(scene,[&](int,int,float a){ink+=a;});
 else eye_raster::render(scene.eyeX,avatar_scene::eyeCenterY(scene),scene.eyeWidth,
                         avatar_scene::renderedEyeHeight(scene),scene.gap,scene.eyes,
                         [&](int x,int y,float a){if(x>=0&&x<240&&y>=0&&y<135)ink+=a;},
                         avatar_scene::kBlinkSink,avatar_scene::kBlinkWiden);
 return ink;
}
}

int main() {
 // Zero alert keeps the established baseline exactly.
 for(float c:{0.0f,.37f,1.0f})for(float s:{0.0f,.5f})
  assert(avatar_scene::eyeBaseline(c,0,s)==67.5f-20*c+8*s);
 // Slow orange-yellow pulse with an exact wrap and no flashing step.
 assert(request_scene::field(0)==request_scene::kYellow);
 assert(request_scene::field(request_scene::kPulsePeriod/2)==request_scene::kOrange);
 assert(request_scene::field(request_scene::kPulsePeriod)==request_scene::field(0));
 for(uint32_t ms=0;ms<2*request_scene::kPulsePeriod;ms+=33)
  assert(fabsf(request_scene::pulse(ms+33)-request_scene::pulse(ms))<.06f);
 // The field floods the whole backdrop; zero presence is bit-identical.
 const uint16_t green=((34&248)<<8)|((197&252)<<3)|(94>>3),blue=((59&248)<<8)|((130&252)<<3)|(246>>3);
 for(int y=0;y<4;++y)for(int x=0;x<240;x+=7) {
  assert(status_background::color(x,y,green,blue,1,1,0,0,0,0,request_scene::kOrange,1)==request_scene::kOrange);
  assert(status_background::color(x,y,green,blue,1,1,0,0,0,0,request_scene::kOrange,0)==
         status_background::color(x,y,green,blue,1,1));
 }
 // The `!` stays centred in the field above the head.
 for(uint32_t ms=0;ms<1800;ms+=33)request_scene::sign(ms,[&](int x,int y,float a) {
  assert(x>=113&&x<=127&&y>=0&&y<46&&a>0&&a<=1);
 });
 // Only the top of the blob shows: no rows above the sign's band, the lower
 // rim stays offscreen and the bottom row is broadly covered.
 {
  auto scene=requestScene(eye_acting::p(0,0,1.10f,1.15f),eye_acting::p(0,0,1.10f,1.15f));
  avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);
  int top=135;
  for(int y=0;y<135;++y)if(mask.right[y]>mask.left[y]){top=y;break;}
  assert(top>=42&&top<=56);
  for(int x=20;x<220;++x)assert(mask.baseCoverage(x,134)==1);
  assert(mask.baseCoverage(120,top-3)==0);
 }
 // Hops: a greeting double hop, bounded height, then a later scheduled hop.
 request_scene::Hops hops;
 float peak=0;uint32_t firstAir=0,lastAir=0;int launches=0;bool airborne=false;
 for(uint32_t ms=1000;ms<20000;ms+=10) {
  hops.update(ms,true,true,ms*2654435761u);
  const float h=hops.height(ms);
  assert(h>=0&&h<=request_scene::Hops::kHeight+1e-4f);
  assert(fabsf(hops.stretch(ms))<=.08f+1e-4f);
  peak=fmaxf(peak,h);
  if(h>0&&!airborne){++launches;if(!firstAir)firstAir=ms;lastAir=ms;}
  airborne=h>0;
 }
 assert(peak>8&&firstAir>=1000+request_scene::Hops::kEntryDelay&&firstAir<1000+request_scene::Hops::kEntryDelay+200);
 assert(launches>=4&&lastAir>firstAir+request_scene::Hops::kMinGap);
 // Ending the request stops at once; disallowed cues pause without hopping.
 hops.update(30000,false,true,1);assert(hops.height(30000)==0&&hops.stretch(30000)==0);
 request_scene::Hops paused;
 for(uint32_t ms=0;ms<3000;ms+=10){paused.update(ms,true,false,ms);assert(paused.height(ms)==0);}
 // The dome stretches instead of clipping the eyes: every request clip,
 // every hop phase, eyes are fully inside the body.
 for(int i=0;i<eye_acting::clipCount(eye_acting::State::attention);++i) {
  const auto& c=eye_acting::clip(eye_acting::State::attention,i);
  for(uint32_t ms=0;ms<=eye_acting::duration(c);ms+=40) {
   auto scene=requestScene(eye_acting::sample(c,ms),eye_acting::sample(c,ms>90?ms-90:0));
   request_scene::Hops hop;hop.update(0,true,true,0);hop.update(request_scene::Hops::kEntryDelay,true,true,0);
   const uint32_t phase=request_scene::Hops::kEntryDelay+(ms*7)%(2*request_scene::Hops::kCycle);
   scene.hop=hop.height(phase);scene.hopStretch=hop.stretch(phase);
   const float free=eyeInk(scene,false);
   assert(free>0&&eyeInk(scene,true)>.995f*free);
  }
 }
 return 0;
}
