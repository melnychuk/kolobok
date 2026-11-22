// Step-over: opening the last unread completion covers the green strip with a
// stepping foot instead of fading it. Synthetic aggregates only.
#include <assert.h>
#include <stdio.h>
#define main native_preview_main
#include "preview_avatar.cpp"
#undef main

namespace {
avatar_scene::Scene doneOnly(float clock) {
 avatar_scene::Scene scene;
 const float side=stripWidth(2);
 scene.region=240-2*side;scene.side=1;scene.corners=0;scene.offset=side/2;
 scene.flowClock=clock;
 return scene;
}
void policy() {
 task_cues::StepOver step;
 // Startup/recovery is a baseline, never a step.
 assert(!step.observe(0,0,0,true,true));
 assert(!step.observe(100,2,0,true,true));
 assert(step.observe(200,0,0,true,true));
 assert(step.busy(200)&&step.oldDone==2);
 // The old count (strip and layout) stays until the foot has planted.
 assert(step.displayCount(200+task_cues::StepOver::kPlant-1,0)==2);
 assert(step.displayCount(200+task_cues::StepOver::kPlant,0)==0);
 assert(step.reachTarget(200)==0&&step.reachTarget(200+task_cues::StepOver::kPlant)==1);
 float last=0;
 for(uint32_t t=0;t<=task_cues::StepOver::kPlant;t+=10) {
  const float reach=step.reachTarget(200+t);
  assert(reach>=last&&reach<=1);last=reach;
 }
 // Exactly one plant and one settle thud.
 int thuds=0;
 for(uint32_t t=200;t<200+task_cues::StepOver::kEnd;t+=33)if(step.consumeImpact(t)>0)++thuds;
 assert(thuds==2);
 assert(!step.busy(200+task_cues::StepOver::kEnd));
 // Not when work remains, not from a paired layout, not when not ready.
 task_cues::StepOver other;
 other.observe(0,2,1,true,true);
 assert(!other.observe(10,0,1,true,true));
 other.observe(20,2,0,true,true);
 assert(!other.observe(30,0,0,true,false));
 other.observe(40,3,0,true,true);
 assert(!other.observe(50,1,0,true,true)); // still unread work: not a clear
 // Recovery is a baseline even across a vanished count.
 other.observe(60,2,0,true,true);other.observe(70,0,0,false,true);
 assert(!other.observe(80,0,0,true,true));
 // A superseding change reveals the current data and retracts the foot.
 task_cues::StepOver cancelled;
 cancelled.observe(0,2,0,true,true);cancelled.observe(10,0,0,true,true);
 for(uint32_t t=10;t<500;t+=33)cancelled.animate(t,.033f);
 assert(cancelled.reachValue>0);
 cancelled.observe(500,0,1,true,true);
 assert(!cancelled.busy(500)&&cancelled.displayCount(500,0)==0);
 for(uint32_t t=500;t<2000;t+=33)cancelled.animate(t,.033f);
 assert(cancelled.reachValue==0);
}
void mask() {
 for(float clock=0;clock<16;clock+=1.37f) {
  avatar_scene::Scene scene=doneOnly(clock);
  avatar_scene::Mask plain,zero;
  avatar_scene::buildMask(scene,plain);
  scene.stepSide=-1;scene.stepProgress=0;
  avatar_scene::buildMask(scene,zero);
  assert(zero.stepSide==0);
  for(int y=0;y<135;++y)for(int x=0;x<240;++x)assert(plain.coverage(x,y)==zero.coverage(x,y));
  float previous[135*240];
  for(int i=0;i<135*240;++i)previous[i]=plain.coverage(i%240,i/240);
  for(float progress=.05f;progress<=1.0001f;progress+=.05f) {
   scene.stepProgress=progress;
   avatar_scene::Mask m;avatar_scene::buildMask(scene,m);
   for(int y=0;y<135;++y) {
    if(m.stepSide&&y>=m.stepTop&&y<=m.stepBottom&&m.stepEdge[y]<m.stepRoot[y])
     assert(m.baseCoverage(static_cast<int>(m.stepRoot[y])-1,y)>=.999f); // attached
    for(int x=0;x<240;++x) {
     const float c=m.coverage(x,y);
     assert(c+1e-5f>=previous[y*240+x]);  // the foot only advances
     previous[y*240+x]=c;
    }
   }
   // Opaque runs agree with per-pixel coverage, including the foot.
   static float raster[135][240];
   for(auto& row:raster)for(float& v:row)v=0;
   m.raster([&](int x,int y,int n){for(int i=0;i<n;++i)raster[y][x+i]=1;},
            [&](int x,int y,float a){raster[y][x]=a;});
   for(int y=0;y<135;++y)for(int x=0;x<240;++x) {
    const float c=m.coverage(x,y);
    assert(c>=.999f?raster[y][x]==1:raster[y][x]==c);
   }
  }
  // Planted: the whole vacated green half is opaque.
  for(int y=0;y<135;++y)for(int x=0;x<120;++x)assert(previous[y*240+x]==1);
 }
}
void compositor() {
 HeaderAvatar avatar;
 for(uint32_t ms=0;ms<5000;ms+=33)avatar.frame(ms,0,0,2,false,true);
 auto& preview=avatar.stick;
 assert(preview.backgroundDeparture.presence[0]==1);
 const uint32_t opened=5016;
 avatar.frame(opened,0,0,0,false,true);
 assert(preview.stepOver.busy(opened));
 bool planted=false,released=false,covered=false;
 for(uint32_t ms=opened+33;ms<opened+4000;ms+=33) {
  avatar.frame(ms,0,0,0,false,true);
  avatar_scene::Mask m;avatar_scene::buildMask(preview.scene,m);
  const float green=preview.backgroundDeparture.presence[0];
  // The complaint: never show a half-faded green. Any pixel of the left
  // half that is not fully covered must be full green or fully gone.
  bool exposed=false;
  for(int y=0;y<135&&!exposed;++y)for(int x=0;x<120;++x)if(m.coverage(x,y)<1){exposed=true;break;}
  if(green>0&&green<1)assert(!exposed);
  if(green<1)planted=true;
  if(green==0)released=true;
  if(preview.stepOver.busy(ms)&&preview.stepOver.phase(ms)>=task_cues::StepOver::kPlant)
   covered=covered||!exposed;
  if(ms<opened+task_cues::StepOver::kPlant)assert(preview.shownDone==2&&green==1);
 }
 assert(planted&&released&&covered);
 assert(!preview.stepOver.busy(opened+4000)&&preview.stepOver.reachValue==0);
 assert(preview.face==avatar_behavior::Mode::sleeping||preview.face==avatar_behavior::Mode::idle);
 // Listening cancels the step at once and reveals current data.
 HeaderAvatar listening;
 for(uint32_t ms=0;ms<5000;ms+=33)listening.frame(ms,0,0,1,false,true);
 listening.frame(5016,0,0,0,false,true);
 assert(listening.stick.stepOver.busy(5016));
 listening.frame(5100,0,0,0,true,true);
 assert(!listening.stick.stepOver.busy(5100));
 // The shown count follows on the next frame, as on the device.
 listening.frame(5133,0,0,0,true,true);
 assert(listening.stick.shownDone==0);
 // Done disappearing while work remains is not a step over.
 HeaderAvatar working;
 for(uint32_t ms=0;ms<5000;ms+=33)working.frame(ms,1,0,1,false,true);
 working.frame(5016,1,0,0,false,true);
 assert(!working.stick.stepOver.busy(5016));
}
} // namespace
int main() {
 assert(freopen("/dev/null","wb",stdout));
 policy();
 mask();
 compositor();
 fprintf(stderr,"step over: policy, mask and compositor checks passed\n");
 return 0;
}
