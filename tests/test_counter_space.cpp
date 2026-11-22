// Synthetic layout fixtures only; no device access or live task changes.
#include <assert.h>
#include <math.h>
#include <initializer_list>
#include "../experiments/qemu-sticks3/measured_frame_envelopes.h"
#define main native_preview_main
#include "preview_avatar.cpp"
#undef main
#define STICK_AVATAR_WIDGET_NO_MAIN
#include "../experiments/avatar-widget/main.cpp"

static bool near(float a,float b,float tolerance=.001f) { return fabsf(a-b)<tolerance; }
// The face leans away from a wide label: toward the active side for done.
static const int kActive=status_sides::kActive;
int main() {
 assert(freopen("/dev/null","wb",stdout));
 assert(counter_space::length(0)==0&&counter_space::length(8)==4);
 assert(counter_space::length(18)==5&&counter_space::length(100)==5);
 const auto doneOnly=counter_space::target(8,0,56,0,0,true);
 const auto activeOnly=counter_space::target(0,8,0,56,0,true);
 assert(doneOnly.x==10*kActive&&activeOnly.x==-10*kActive&&doneOnly.scale==1&&activeOnly.scale==1);
 assert(counter_space::target(18,0,56,0,0,true).scale==.94f);
 assert(counter_space::target(18,18,56,56,1,true).x==0);
 assert(counter_space::target(18,18,56,56,1,true).scale==1);
 assert(counter_space::target(18,8,56,45,1,true).x==0);
 for(int done=0;done<=150;++done)for(int active=0;active<=150;++active) {
  const auto goal=counter_space::target(done,active,fittedLabelWidth(done),fittedLabelWidth(active),
                                        done&&active?1:0,true);
  assert(isfinite(goal.x)&&fabsf(goal.x)<=10&&goal.scale>=.94f&&goal.scale<=1);
  assert((goal.scale<1)==(!(done&&active)&&(counter_space::length(done)>=5||counter_space::length(active)>=5)));
  const auto reversed=counter_space::target(active,done,fittedLabelWidth(active),fittedLabelWidth(done),
                                           done&&active?1:0,true);
  assert(near(goal.x,-reversed.x)&&goal.scale==reversed.scale);
 }
 for(int cadence: {20,33,50}) {
  counter_space::State state;avatar_scene::Scene scene;scene.corners=0;
  float oldX=0,oldScale=1;
  for(int ms=0;ms<5000;ms+=cadence) {
   state.update(scene,18,0,56,0,cadence/1000.0f,true);
   const float shift=scene.counterShift*kActive;
   assert(shift>=oldX&&scene.counterScale<=oldScale);
   assert(shift<=10&&scene.counterScale>=.94f);
   if(shift<6.5f)assert(scene.counterScale==1); // shift first
   oldX=shift;oldScale=scene.counterScale;
  }
  assert(scene.counterShift==10*kActive&&scene.counterScale==.94f);
  const auto g=avatar_scene::geometry(scene);
  auto original=scene;original.counterShift=0;original.counterScale=1;
  const auto base=avatar_scene::geometry(original);
  assert(near(g.cx-base.cx,10*kActive)&&near(g.rx/base.rx,.94f)&&near(g.ry/base.ry,.94f));
  assert(near(avatar_scene::renderedEyeHeight(scene)/avatar_scene::renderedEyeHeight(original),.94f));
  // Request/listening/offline/sleep adapters disable the accommodation softly.
  for(int ms=0;ms<5000;ms+=cadence)state.update(scene,18,0,56,0,cadence/1000.0f,false);
  assert(scene.counterShift==0&&scene.counterScale==1);
 }
 // The smaller body must still cover its unused edge in every authored
 // single-counter clip, not just the quiet poses in the contact sheet.
 // `side` is the face's screen shift: toward the active side when only done.
 for(int side=-1;side<=1;side+=2)for(int count: {18,88}) {
  const auto acting=side==kActive?eye_acting::State::done:eye_acting::State::thinking;
  for(int clip=0;clip<eye_acting::clipCount(acting);++clip) {
   eye_acting::Player player;player.state=acting;player.index=clip;
   face_physics::Surface surface;
   for(uint32_t ms=0;ms<8000;ms+=33) {
    avatar_scene::Scene scene;scene.corners=0;scene.region=240-2*stripWidth(count);
    scene.offset=side*stripWidth(count)/2.0f;scene.counterScale=.94f;
    scene.counterShift=side*10;scene.eyes=player.eyes(ms);scene.body=player.body(ms);
    scene.eyeX=120+scene.offset+scene.eyes.x;scene.eyeY=67.5f;
    surface.step(.033f,scene.body,false);scene.surface=&surface;
    scene.flowClock=fluid_face::advance(0,ms*.0006f);
    avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);
    for(int y=0;y<135;++y)for(int dx=0;dx<16;++dx)
     assert(mask.baseCoverage(side>0?239-dx:dx,y)==1);
   }
  }
 }
 // New layout/source is intentionally outside historical timing calibration.
 for(const auto* entry: stick_measured::all) {
  const char* source="3408689d38edf21e1640ab9c90fb6d3494b4be9a583a0f8e8b5199402f245525";
  const char* binary="619f752f789e1085aac0739138e24dc9173db236bc04e56c18db1530575c7dfc";
  assert(!stick_measured::findEnvelope(source,binary,"O2",entry->mode));
  assert(!stick_measured::findEnvelope(source,entry->binarySha256,
                                     entry->optimization,entry->mode));
 }
 // Full compositor: fitting, long Romans, both sides, restored size and
 // unused-edge coverage all use the SAME layout helper as firmware.
 HeaderAvatar avatar;
 uint32_t now=0;
 for(int side=-1;side<=1;side+=2)for(int count: {8,18,38,88,100}) {
  const bool doneOnly=side==kActive;
  for(int frame=0;frame<170;++frame,now+=33)
   avatar.frame(now,doneOnly?0:count,0,doneOnly?count:0,false,true);
  const auto& scene=avatar.stick.scene;
  assert(side*scene.counterShift>0);
  assert(scene.counterScale==(counter_space::length(count)>=5?.94f:1.0f));
  avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);
  for(int y=0;y<135;++y)for(int dx=0;dx<16;++dx)
   assert(mask.baseCoverage(side>0?239-dx:dx,y)==1);
  assert(avatar.stick.shownDone==(doneOnly?count:0));
  assert(avatar.stick.shownActive==(doneOnly?0:count));
 }
 for(int frame=0;frame<170;++frame,now+=33)avatar.frame(now,18,0,18,false,true);
 assert(avatar.stick.scene.counterShift==0&&avatar.stick.scene.counterScale==1);
 for(int frame=0;frame<170;++frame,now+=33)avatar.frame(now,18,1,18,false,true);
 assert(avatar.stick.scene.counterShift==0&&avatar.stick.scene.counterScale==1);
 for(int frame=0;frame<170;++frame,now+=33)avatar.frame(now,8,0,0,false,true);
 assert(avatar.stick.scene.counterScale==1);
 return 0;
}
