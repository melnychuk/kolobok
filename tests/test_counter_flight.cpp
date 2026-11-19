#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <initializer_list>
#include "../firmware/avatar_interaction.h"
#include "../firmware/display_fonts.h"
#include "../firmware/roman_counts.h"
#include "../experiments/qemu-sticks3/measured_frame_envelopes.h"

using avatar_interaction::CounterFlight;
const count_raster::Font* fonts[]={&kBadgeFont,&kMediumBadgeFont,&kSmallBadgeFont,&kTinyBadgeFont,&kCompactBadgeFont};
float fitted(int count) {
 char label[10];roman_counts::format(count,label);
 for(int i=0;i<5;++i) {
  const float w=count_raster::width(*fonts[i],label);
  if(w<=56||i==4)return w;
 }
 return 0;
}
void start(CounterFlight& flight,uint32_t at,int done=2,int active=3) {
 assert(!flight.observe(at-100,done,active,true,true)); // baseline
 assert(flight.observe(at,done+1,active-1,true,true));
 assert(flight.busy(at));
}
void trajectory(uint32_t at,int done,int active) {
 CounterFlight flight;start(flight,at,done,active);
 // Fit separation for BOTH old and new strings, including zero/99+.
 const float dw=fmaxf(fitted(done),fitted(done+1));
 const float aw=fmaxf(fitted(active),fitted(active-1));
 flight.fit(dw,aw);
 const float dx=fmaxf(48,dw+16)/2,ax=240-fmaxf(48,aw+16)/2;
 float maxX=0,minY=0;
 auto previousLeft=flight.offset(at,true,dx,ax,dw,aw);
 auto previousRight=flight.offset(at,false,ax,dx,aw,dw);
 for(uint32_t elapsed=0;elapsed<34000;elapsed+=5) {
  const uint32_t now=at+elapsed;
  assert(!flight.observe(now,done+1,active-1,true,true));
  flight.update(now,true);assert(flight.started==at);
  const auto left=flight.offset(now,true,dx,ax,dw,aw);
  const auto right=flight.offset(now,false,ax,dx,aw,dw);
  assert(isfinite(left.x)&&isfinite(right.y));
  assert(left.x>=0&&right.x<=0);
  assert(left.y>=-CounterFlight::kCarry-.001f&&left.y<=2.001f);
  assert(right.y>=-CounterFlight::kCarry-.001f&&right.y<=2.001f);
  assert(ax+right.x-aw/2-(dx+left.x+dw/2)>=9.99f);
  assert(fabsf(left.x-previousLeft.x)<1.3f&&fabsf(right.x-previousRight.x)<1.3f);
  assert(fabsf(left.y-previousLeft.y)<.9f&&fabsf(right.y-previousRight.y)<.9f);
  assert(left.y==right.y); // simultaneous pickup, even with unequal fitted widths
  if(!flight.busy(now))assert(left.x==0&&left.y==0&&right.x==0&&right.y==0);
  maxX=fmaxf(maxX,left.x);minY=fminf(minY,left.y);
  previousLeft=left;previousRight=right;
 }
 assert(maxX>40&&minY<-CounterFlight::kCarry+1);
 for(uint32_t step: {16u,33u,50u,100u}) {
  CounterFlight sampled;start(sampled,at,done,active);sampled.fit(dw,aw);
  for(uint32_t t=0;t<CounterFlight::kSwap;t+=step)sampled.update(at+t,true);
  const auto a=sampled.offset(at+CounterFlight::kSwap,true,dx,ax,dw,aw);
  CounterFlight reference;start(reference,at,done,active);reference.fit(dw,aw);
  const auto b=reference.offset(at+CounterFlight::kSwap,true,dx,ax,dw,aw);
  assert(a.x==b.x&&a.y==b.y);
 }
}
// Blobs are the lower rim itself: zero reach is exact, a full lobe reaches
// its tip over the numeral and never floats or lowers past the tip, and the
// raster (runs + AA fringe) still matches per-pixel coverage.
void blobs() {
 avatar_scene::Scene scene;
 avatar_scene::Mask plain,mask;
 avatar_scene::buildMask(scene,plain);
 for(float reach: {.05f,.4f,1.0f})for(float x: {4.0f,24.0f,90.0f})for(float tip: {96.0f,109.0f}) {
  scene.hands.reach[0]=scene.hands.reach[1]=reach;
  scene.hands.x[0]=x;scene.hands.x[1]=240-x;
  scene.hands.y[0]=scene.hands.y[1]=tip;
  scene.hands.halfWidth[0]=scene.hands.halfWidth[1]=14;
  avatar_scene::buildMask(scene,mask);
  for(int c=0;c<240;++c) {
   assert(mask.bottom[c]>=plain.bottom[c]);
   assert(mask.bottom[c]<=fmaxf(plain.bottom[c],tip)+avatar_scene::kLobeFillet*.25f+.01f);
   const float far=fminf(fabsf(c+.5f-x),fabsf(c+.5f-(240-x)));
   if(far>14+avatar_scene::kLobeFillet+1)assert(mask.bottom[c]==plain.bottom[c]);
  }
  if(reach==1)for(float cx: {x,240-x}) {
   const int c=static_cast<int>(cx);
   assert(fabsf(mask.bottom[c]-fmaxf(plain.bottom[c],tip))<avatar_scene::kLobeFillet*.25f+.6f);
   // Opaque from the rim down to the tip: one body, no detached blob.
   for(int y=static_cast<int>(plain.bottom[c])-2;y<static_cast<int>(tip)-1;++y)
    assert(mask.coverage(c,y)==1);
  }
  static float drawn[135][240];
  for(auto& row:drawn)for(float& v:row)v=0;
  mask.raster([&](int x0,int y0,int n){for(int k=0;k<n;++k)drawn[y0][x0+k]=1;},
              [&](int x0,int y0,float a){drawn[y0][x0]=a;});
  for(int py=0;py<135;++py)for(int px=0;px<240;++px) {
   const float c=mask.coverage(px,py);
   if(c>=.999f)assert(drawn[py][px]==1);
   else assert(fabsf(drawn[py][px]-c)<1e-6f);
  }
 }
 scene.hands=avatar_scene::Hands{};
 avatar_scene::buildMask(scene,mask);
 for(int c=0;c<240;++c)assert(mask.bottom[c]==plain.bottom[c]);
}
int main() {
 blobs();
 for(int done: {1,2,3,8,39,88,99,120})for(int active: {1,3,39,88,120})
  trajectory(100,done,active);
 trajectory(0xfffff000u,99,88);
 CounterFlight flight;start(flight,100);
 const uint32_t swap=100+CounterFlight::kSwap,reach=100+CounterFlight::kReach;
 assert(flight.displayCount(swap-1,true,3)==2);
 assert(flight.displayCount(swap-1,false,2)==3);
 assert(flight.displayCount(swap,true,3)==3);
 assert(flight.displayCount(swap,false,2)==2);
 // Hands reach out first: numerals stay put until both are gripped.
 for(uint32_t t=100;t<reach;t+=10) {
  const auto still=flight.offset(t,true,24,216,40,40);
  assert(still.x==0&&still.y==0);
  assert(flight.handReach(t,true)==flight.handReach(t,false));
 }
 assert(flight.handReach(reach,true)==1&&flight.handReach(reach,false)==1);
 // Swap occurs during the still apex, BEFORE either numeral returns.
 const auto before=flight.offset(swap-1,true,24,216,40,40);
 const auto swapped=flight.offset(swap,true,24,216,40,40);
 const auto after=flight.offset(swap+199,true,24,216,40,40);
 assert(before.x==swapped.x&&swapped.x==after.x&&swapped.y==-CounterFlight::kCarry);
 assert(flight.offset(swap+300,true,24,216,40,40).x<swapped.x);
 // Both numerals are set down before either hand lets go.
 const uint32_t release=100+CounterFlight::kRelease;
 for(bool side: {true,false}) {
  const auto home=flight.offset(release,side,24,216,40,40);
  assert(home.x==0&&home.y==0);
  assert(flight.handReach(release-1,side)==1&&flight.handReach(release+CounterFlight::kRetract,side)==0);
 }
 // The body leads the carried pair up and is home when they are released.
 assert(flight.rise(CounterFlight::kSwap-100)<-CounterFlight::kRise+.01f);
 assert(flight.rise(CounterFlight::kRelease-100)==0&&flight.rise(CounterFlight::kReach)==0);
 for(uint32_t t=0;t<CounterFlight::kEnd;t+=5)
  assert(fabsf(flight.rise(t+5)-flight.rise(t))<.5f&&flight.rise(t)<=1.5f);
 avatar_scene::Hands hands;
 flight.hands(swap,24,216,40,40,116,23,17,hands);
 assert(hands.x[0]==24+swapped.x&&hands.x[1]==216-swapped.x); // over each numeral
 assert(hands.y[0]==116-CounterFlight::kCarry-23/2.0f+CounterFlight::kGrip);
 assert(hands.y[1]==116-CounterFlight::kCarry-17/2.0f+CounterFlight::kGrip);
 assert(hands.halfWidth[0]==CounterFlight::kLobeMax&&CounterFlight::lobeRadius(2)==CounterFlight::kLobeMin&&
        CounterFlight::lobeRadius(20)==10+CounterFlight::kLobePad&&hands.reach[0]==1&&hands.reach[1]==1);
 // The blobs set the numerals down in their new places before letting go.
 flight.hands(release-1,24,216,40,40,116,23,23,hands);
 assert(hands.x[0]==24&&hands.x[1]==216&&hands.reach[0]==1&&hands.reach[1]==1);
 flight.fit(50,56);
 const auto oldLabel=flight.offset(swap-1,true,24,216,20,40);
 const auto newLabel=flight.offset(swap,true,30,210,50,56);
 assert(24+oldLabel.x==30+newLabel.x); // font/strip change cannot shift the apex
 const auto base=eye_acting::p(2,-1,1,.9f,.1f,.2f,.1f);
 const auto eyes=flight.apply(base,1200),body=flight.apply(base,1290,true);
 assert(fabsf(eyes.x-body.x)<.001f&&fabsf(eyes.y-body.y)<.001f);
 assert(body.closeL==0&&body.closeR==0);
 const auto strict=flight.apply(base,reach+600),warm=flight.apply(base,reach+2100);
 assert(strict.smile==0&&strict.upperL>=.22f&&strict.upperR>=.24f);
 assert(warm.smile>.6f&&warm.upperL==0&&warm.upperR==0);
 flight.update(1600,false);assert(!flight.busy(1600));
 assert(flight.handReach(1600,true)==0);
 flight.hands(1600,24,216,40,40,116,23,23,hands);assert(hands.reach[0]==0&&hands.reach[1]==0);
 assert(flight.displayCount(1600,true,3)==3);
 assert(!flight.observe(1800,3,2,true,true)); // no replay after preemption
 assert(!flight.busy(1800));
 // Other snapshot edges never imply sorting. Suppressed edges are consumed.
 for(auto values: {std::initializer_list<int>{3,3}, {2,2}, {1,4}, {2,3}}) {
  CounterFlight f;f.observe(0,2,3,true,true);
  auto it=values.begin();const int d=*it++;const int a=*it;
  assert(!f.observe(100,d,a,true,true));assert(!f.busy(100));
 }
 CounterFlight suppressed;suppressed.observe(0,2,3,true,true);
 assert(!suppressed.observe(100,3,2,true,false));
 assert(!suppressed.observe(200,3,2,true,true));
 suppressed.observe(300,0,0,false,false);
 assert(!suppressed.observe(400,4,1,true,true)); // recovery baseline
 CounterFlight superseded;start(superseded,100);
 assert(!superseded.observe(500,1,2,true,true));assert(!superseded.busy(500));
 assert(superseded.displayCount(500,true,1)==1);
 CounterFlight last;start(last,100,1,1);
 assert(last.displayCount(swap,false,0)==0); // zero stays hidden, not Roman I
 // Its now-empty hand lets go at the swap; the done hand still sets down.
 assert(last.handReach(swap+CounterFlight::kRetract,false)==0);
 assert(last.handReach(swap+CounterFlight::kRetract,true)==1);
 CounterFlight first;first.observe(0,0,3,true,true);
 assert(!first.observe(100,1,2,true,true)); // no old done label to pick up
 // Multi-completion snapshots use exact values, never a fabricated +/- one.
 CounterFlight batch;batch.observe(0,2,8,true,true);
 assert(batch.observe(100,5,5,true,true));
 assert(batch.displayCount(swap,true,5)==5&&batch.displayCount(swap,false,5)==5);
 for(const auto* entry: stick_measured::all) {
  assert(!stick_measured::findEnvelope(entry->sourceSha256,entry->binarySha256,
         entry->optimization,"working/flags-2"));
  assert(!stick_measured::findEnvelope("completion-sort-unmeasured",entry->binarySha256,
         entry->optimization,entry->mode));
  // Hash-verified completion sorting + default-off mouth has no physical
  // occupancy capture. Deployment alone must never activate an older preset.
  assert(!stick_measured::findEnvelope(
         "71097432d715793a98d63e7c60b0ff4b265102a885bc9eb1538547ea90f65be2",
         "d06cafb996b3b3c51ab036a8d716725ecefc143a5cda0cba32418b5e5d9997fd",
         entry->optimization,entry->mode));
 }
 puts("Completion sorting: real edges, blobs reach/grip/set down/release, body rise, simultaneous pickup, apex swap before return, priority, wrap, cadence and envelope rejection passed");
}
