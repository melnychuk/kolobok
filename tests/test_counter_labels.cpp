// Full-width bottom numerals, side fitting, reflow and sorting; synthetic only.
#include <assert.h>
#include <initializer_list>
#include "../experiments/qemu-sticks3/measured_frame_envelopes.h"
#define STICK_AVATAR_WIDGET_NO_MAIN
#include "../experiments/avatar-widget/main.cpp"

static void rasterBounds(const counter_labels::State& plan,float exposed,float corners) {
 for(int side=0;side<2;++side) {
  if(plan.counts[side]<=0)continue;
  const auto& text=plan.labels[side];char roman[10];roman_counts::format(plan.counts[side],roman);
  const float inset=plan.inset(side,exposed,corners);
  for(float shift: {-3.0f,0.0f,3.0f})
   count_raster::render(counter_labels::font(text.index),roman,(side?240-inset:inset)+shift,116,
       [](int x,int y,float,bool){assert(x>=0&&x<240&&y>=0&&y<135);});
 }
}
int main() {
 assert(freopen("/dev/null","wb",stdout));
 counter_labels::State plan,mirror;
 for(int done=0;done<=120;++done)for(int active=0;active<=120;++active) {
  plan.update(done,active,1);mirror.update(active,done,1);
  assert(plan.labels[0].width+plan.labels[1].width<=counter_labels::kBottomBudget);
  assert(plan.labels[0].index==mirror.labels[1].index&&plan.labels[1].index==mirror.labels[0].index);
  char left[10],right[10];roman_counts::format(done,left);roman_counts::format(active,right);
  const int fullLeft=count_raster::width(kBadgeFont,left),fullRight=count_raster::width(kBadgeFont,right);
  if(fullLeft+fullRight<=counter_labels::kBottomBudget)
   assert(plan.labels[0].index==0&&plan.labels[1].index==0); // never shrink without a collision
  if(done&&active) {
   const float gap=240-plan.inset(1,72,1)-plan.labels[1].width/2.0f-
                        (plan.inset(0,72,1)+plan.labels[0].width/2.0f);
   assert(gap>=counter_labels::kGap);
  }
  plan.update(done,active,0);
  assert(plan.labels[0].width==fittedLabelWidth(done)&&plan.labels[1].width==fittedLabelWidth(active));
 }
 plan.update(18,18,1);
 assert(plan.labels[0].index==0&&plan.labels[0].width==84&&plan.labels[1].width==84);
 assert(plan.inset(0,72,1)==50&&plan.inset(1,72,1)==50);
 plan.update(1,88,1);assert(plan.labels[0].index==0&&plan.labels[1].index==0); // share spare width
 plan.update(88,88,1);assert(plan.labels[0].index>0&&plan.labels[0].index==plan.labels[1].index);
 // Reflow font promotions and full outline + +/-3px contact travel stay on screen.
 for(int done: {1,8,18,38,88,99,100})for(int active: {1,8,18,38,88,99,100})
  for(int step=0;step<=20;++step) {
   const float corners=step/20.0f;plan.update(done,active,corners);
   const float exposed=fmaxf(stripWidth(done),stripWidth(active));
   rasterBounds(plan,exposed,corners);
  }
 // Reserve OLD and NEW widths jointly before pickup, then centre the whole
 // gathered group. Unequal full-size labels must not travel off screen.
 for(int oldDone: {1,8,17,37,87,99})for(int oldActive: {1,9,19,39,89,100}) {
  const int newDone=oldDone+1,newActive=oldActive-1;
  counter_labels::State old,newer;
  old.update(oldDone,oldActive,1,newDone,newActive);
  newer.update(newDone,newActive,1,oldDone,oldActive);
  assert(old.labels[0].index==newer.labels[0].index&&old.labels[1].index==newer.labels[1].index);
  assert(old.flightWidth(0)==newer.flightWidth(0)&&old.flightWidth(1)==newer.flightWidth(1));
  avatar_interaction::CounterFlight flight;flight.observe(0,oldDone,oldActive,true,true);
  assert(flight.observe(100,newDone,newActive,true,true));
  flight.fit(old.flightWidth(0),old.flightWidth(1));
  float before[2]={},after[2]={};
  const int swap=static_cast<int>(avatar_interaction::CounterFlight::kSwap);
  for(int elapsed=0;elapsed<=static_cast<int>(avatar_interaction::CounterFlight::kEnd);elapsed+=10) {
   const auto& frame=elapsed<swap?old:newer;
   float centers[2]={};
   for(int side=0;side<2;++side) {
    const float inset=frame.inset(side,72,1),rest=side?240-inset:inset;
    const auto offset=flight.offset(100+elapsed,side==0,rest,240-rest,
                                  frame.labels[side].width,frame.labels[1-side].width);
    centers[side]=rest+offset.x;
    assert(centers[side]-frame.labels[side].width/2>=7.99f);
    assert(centers[side]+frame.labels[side].width/2<=232.01f);
    if(elapsed==swap-10)before[side]=centers[side];
    if(elapsed==swap)after[side]=centers[side];
   }
   assert(centers[1]-frame.labels[1].width/2-(centers[0]+frame.labels[0].width/2)>=9.99f);
  }
  assert(fabsf(before[0]-after[0])<.001f&&fabsf(before[1]-after[1])<.001f);
 }
 // Larger bottom glyphs are deliberately outside older physical occupancy
 // captures; retain fail-closed provenance instead of assigning old FPS.
 for(const auto* entry: stick_measured::all) {
  const char* source="78021a26d0f53f156b9f987f17c8abad79ca9213d3afc7bab84de100d2239033";
  const char* binary="4b058a33068f11a47252efc65b33fc1d36d6e3d7b374d8797d1de8d41760fe6d";
  assert(!stick_measured::findEnvelope(source,binary,"O2",entry->mode));
  assert(!stick_measured::findEnvelope(source,entry->binarySha256,entry->optimization,entry->mode));
 }
 // Actual shared header compositor, including short -> long and side -> paired.
 HeaderAvatar avatar;uint32_t now=0;
 for(int frame=0;frame<170;++frame,now+=33)avatar.frame(now,0,0,18,false,true);
 assert(avatar.preview.scene.counterScale==.94f);
 for(int frame=0;frame<170;++frame,now+=33)avatar.frame(now,18,0,18,false,true);
 assert(avatar.preview.scene.counterScale==1&&avatar.preview.scene.counterShift==0);
 assert(avatar.preview.counterLabels.labels[0].index==0&&avatar.preview.counterLabels.labels[1].index==0);
 for(int frame=0;frame<170;++frame,now+=33)avatar.frame(now,88,0,1,false,true);
 assert(avatar.preview.counterLabels.labels[0].index==0&&avatar.preview.counterLabels.labels[1].index==0);
 assert(avatar.preview.scene.counterScale==1&&avatar.preview.scene.counterShift==0);
 for(int frame=0;frame<170;++frame,now+=33)avatar.frame(now,88,1,0,false,true);
 assert(avatar.preview.counterLabels.labels[0].width==0);
 assert(avatar.preview.counterLabels.labels[1].index==0); // hidden count reserves no bottom space
 return 0;
}
