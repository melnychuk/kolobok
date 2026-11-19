#include <assert.h>
#include <chrono>
#include <initializer_list>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../firmware/avatar_scene.h"
#include "reference_round4_eye_raster.h"

struct Pixel {
 int x,y;float alpha;
 bool operator==(const Pixel& other) const {
  return x==other.x && y==other.y && alpha==other.alpha;
 }
};
unsigned long comparisons=0,spanPixels=0,spanTotal=0;
volatile float benchmarkSink=0;
void check(float cx,float cy,float width,float height,float gap,const eye_acting::Pose& pose) {
 std::vector<Pixel> actual,expected;
 eye_raster::render(cx,cy,width,height,gap,pose,[&](int x,int y,float alpha) {
  assert(isfinite(alpha)&&alpha>0&&alpha<=1);actual.push_back({x,y,alpha});
 });
 reference_round4_eye_raster::render(cx,cy,width,height,gap,pose,[&](int x,int y,float alpha) {
  expected.push_back({x,y,alpha});
 });
 // Include offscreen and repeated/overlapping eye callbacks, not just a max mask.
 assert(actual==expected);comparisons+=actual.size();
 // Interior spans expanded in place reproduce the exact ordered sequence.
 std::vector<Pixel> spanned;
 eye_raster::render(cx,cy,width,height,gap,pose,[&](int x,int y,float alpha) {
  spanned.push_back({x,y,alpha});
 },[&](int x,int y,int length) {
  assert(length>0);
  for(int i=0;i<length;++i)spanned.push_back({x+i,y,1});
  spanPixels+=length;
 },0,0);
 assert(spanned==expected);comparisons+=spanned.size();spanTotal+=expected.size();
 // Same with the production blink shape (no frozen oracle: self-consistency).
 std::vector<Pixel> shaped,shapedSpans;
 eye_raster::render(cx,cy,width,height,gap,pose,[&](int x,int y,float a){shaped.push_back({x,y,a});},
                    avatar_scene::kBlinkSink,avatar_scene::kBlinkWiden);
 eye_raster::render(cx,cy,width,height,gap,pose,[&](int x,int y,float a){shapedSpans.push_back({x,y,a});},
                    [&](int x,int y,int n){for(int i=0;i<n;++i)shapedSpans.push_back({x+i,y,1});},
                    avatar_scene::kBlinkSink,avatar_scene::kBlinkWiden);
 assert(shaped==shapedSpans);comparisons+=shaped.size();
}
int main(int argc,char** argv) {
 avatar_scene::Scene scene;
 assert(fabsf(avatar_scene::renderedEyeHeight(scene)-45.36f)<.00001f);
 scene.eyeHeight=42;assert(fabsf(avatar_scene::renderedEyeHeight(scene)-34.02f)<.00001f);
 // Requests scale (not floor) the eyes: the authored rest pose renders at
 // 48px while squash/stretch remains visible around it.
 scene.alert=1;scene.eyeHeight=44*1.15f;assert(fabsf(avatar_scene::renderedEyeHeight(scene)-48)<.0001f);
 scene.eyeHeight=44*1.25f;assert(avatar_scene::renderedEyeHeight(scene)>52);
 scene.eyeHeight=44*.88f;assert(avatar_scene::renderedEyeHeight(scene)<37);
 scene.eyeHeight=42;
 scene.alert=0;scene.eyeHeight=4;scene.sleep=1;
 assert(avatar_scene::renderedEyeHeight(scene)==4);
 scene.sleep=0;scene.eyeHeight=42;
 float previous=avatar_scene::renderedEyeHeight(scene);
 for(int i=0;i<=100;++i) {
  scene.alert=i/100.0f;
  const float height=avatar_scene::renderedEyeHeight(scene);
  assert(height>=34 && height<=40 && fabsf(height-previous)<=.48001f);previous=height;
 }
 for(int state=0;state<4;++state)for(int clip=0;clip<eye_acting::clipCount(static_cast<eye_acting::State>(state));++clip) {
  eye_acting::Player actor;actor.state=static_cast<eye_acting::State>(state);actor.index=clip;
  const auto& performance=eye_acting::clip(actor.state,clip);
  for(unsigned ms=0;ms<=eye_acting::duration(performance)+700u;ms+=137) {
   const auto pose=actor.eyes(ms);
   for(float scale:{1.0f,.9f,.81f}) {
    check(120+pose.x,67.5f+pose.y,50*pose.width,56*pose.height*scale,19,pose);
    check(119.3f+pose.x,88.5f+pose.y,36*pose.width,fmaxf(48,44*pose.height*scale),12,pose);
   }
  }
 }
 // Tiny happy blends must not use the lid/cheek transparent shortcut. Also
 // cover full happy arches, sleep pills, unilateral winks and extreme rotations.
 for(int i=0;i<1000;++i) {
  auto pose=eye_acting::p();
  pose.tilt=(i%181)-90;pose.closeL=(i%11)/10.0f;pose.closeR=((i*3)%11)/10.0f;
  pose.smile=i%13==0?1e-8f:(i%7)/6.0f;
  pose.upperL=(i%5)/5.0f;pose.upperR=(i%3)/3.0f;
  const float width=2+(i*37)%123,height=3+(i*17)%118;
  check(i%5==0?-7.3f:i%5==1?247.7f:117.3f,
        i%7==0?-3.1f:i%7==1?137.1f:64.9f,width,height,17,pose);
 }
 for(float width:{124.0f,125.0f,126.0f,127.0f,128.0f,129.0f,132.0f,180.0f})
  for(float tilt:{0.0f,-0.0f,1e-8f,35.0f})
   for(float closure:{0.0f,.35000003f,.8f,1.0f}) {
    auto pose=eye_acting::p();pose.tilt=tilt;pose.closeL=closure;pose.smile=.72f;
    pose.upperL=.3f;pose.upperR=.1f;
    check(117.3f,64.9f,width,50.4f,-3,pose);
   }
 // Scene-level fill: runs fully inside the face mask become fills; the
 // expanded sequence equals the per-pixel masked callbacks exactly.
 for(int state=0;state<4;++state)for(int clip=0;clip<eye_acting::clipCount(static_cast<eye_acting::State>(state));++clip) {
  eye_acting::Player actor;actor.state=static_cast<eye_acting::State>(state);actor.index=clip;
  for(unsigned ms=0;ms<=eye_acting::duration(eye_acting::clip(actor.state,clip));ms+=211)
  for(int layout=0;layout<3;++layout) {
   avatar_scene::Scene scene;const auto pose=actor.eyes(ms);
   scene.corners=layout==1;scene.alert=layout==2;scene.offset=layout==0?28:0;
   scene.region=layout==0?184:144;scene.awakeHeight=layout==2?44:56;
   scene.eyes=pose;scene.body=actor.body(ms);
   scene.eyeWidth=50*pose.width;scene.eyeHeight=scene.awakeHeight*pose.height;
   scene.eyeX=120+scene.offset+pose.x;
   scene.eyeY=avatar_scene::eyeBaseline(scene.corners,scene.alert,0)+pose.y;
   scene.flowClock=ms*.0006f;
   avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);
   std::vector<Pixel> plain,filled;
   avatar_scene::eyes(scene,mask,[&](int x,int y,float a){plain.push_back({x,y,a});});
   avatar_scene::eyes(scene,mask,[&](int x,int y,float a){filled.push_back({x,y,a});},
                      [&](int x,int y,int n){
    for(int i=0;i<n;++i){assert(mask.baseCoverage(x+i,y)==1);filled.push_back({x+i,y,1});}
   });
   assert(plain==filled);comparisons+=plain.size();
  }
 }
 printf("Eye-pass exact ordered coverage callbacks: %lu (interior spans cover %.1f%% of oracle pixels)\n",
        comparisons,100.0*spanPixels/spanTotal);
 if(argc==2 && strcmp(argv[1],"--benchmark")==0) {
  constexpr int iterations=2800;
  for(int version=0;version<3;++version) {
   const auto start=std::chrono::steady_clock::now();
   for(int i=0;i<iterations;++i) {
    const auto& clip=eye_acting::clip(static_cast<eye_acting::State>((i/7)%4),i%7);
    const auto pose=eye_acting::sample(clip,(i*137)%(eye_acting::duration(clip)+700));
    const float height=56*pose.height*(version==0?1:avatar_scene::kEyeHeightScale);
    float sum=0;const auto pixel=[&](int,int,float alpha){sum+=alpha;};
    if(version==2)eye_raster::render(120.3f,67.7f,50*pose.width,height,19,pose,pixel);
    else reference_round4_eye_raster::render(120.3f,67.7f,50*pose.width,height,19,pose,pixel);
    benchmarkSink+=sum;
   }
   const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
   printf("HOST ONLY eyes %s: %.2fms / %d iterations (not device FPS)\n",
          version==0?"old height/reference":version==1?"new height/reference":"new height/optimized",ms,iterations);
  }
 }
}
