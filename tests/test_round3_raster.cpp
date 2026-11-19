#include <assert.h>
#include <chrono>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../firmware/avatar_scene.h"
#include "reference_mouth_raster.h"
#include "reference_avatar_scene.h"

unsigned long comparisons=0;
// The superellipse roots use correctly rounded sqrtf for exact p=2/p=4 instead
// of powf (within an ulp). Frozen-oracle mask edges may therefore differ by a
// few float ulps (~1e-5px); 2e-4px is far below any 8-bit coverage step.
constexpr float kRootTolerance=2e-4f;
float worstRoot=0;
bool near(float a,float b) { worstRoot=fmaxf(worstRoot,fabsf(a-b));return fabsf(a-b)<=kRootTolerance; }
volatile float benchmarkSink=0;
void checkMouth(const mouth_raster::Shape& shape,float cx,float cy,float opacity) {
 std::vector<float> actual(240*135,0),expected(240*135,0);
 mouth_raster::render(cx,cy,shape,opacity,[&](int x,int y,float a) {
  assert(x>=0&&x<240&&y>=0&&y<135&&isfinite(a)&&a>0&&a<=1);
  actual[y*240+x]=a;
 });
 const reference_mouth_raster::Shape original={shape.halfWidth,shape.curve,shape.opening,
                                              shape.tilt,shape.skew,shape.purse};
 reference_mouth_raster::render(cx,cy,original,opacity,[&](int x,int y,float a) {
  expected[y*240+x]=a;
 });
 assert(actual==expected);comparisons+=actual.size();
}
reference_avatar_scene::Scene originalScene(const avatar_scene::Scene& s) {
 reference_avatar_scene::Scene r;
 r.region=s.region;r.side=s.side;r.corners=s.corners;r.offset=s.offset;
 r.alert=s.alert;r.sleep=s.sleep;r.eyeWidth=s.eyeWidth;r.eyeHeight=s.eyeHeight;
 r.awakeWidth=s.awakeWidth;r.awakeHeight=s.awakeHeight;r.gap=s.gap;
 r.eyeX=s.eyeX;r.eyeY=s.eyeY;r.flowClock=s.flowClock;
 r.eyes=s.eyes;r.body=s.body;r.thinking=s.thinking;r.surface=s.surface;
 return r;
}
void checkMask(const avatar_scene::Scene& scene) {
 avatar_scene::Mask actual;
 reference_avatar_scene::Mask expected;
 avatar_scene::buildMask(scene,actual);
 reference_avatar_scene::buildMask(originalScene(scene),expected);
 const float stretch=avatar_scene::unusedSideStretch(scene);
 const float center=avatar_scene::geometry(scene).cx;
 const float soft=avatar_scene::softSides(scene);
 // Soft vertical sides intentionally move both ways: membrane (soft-limited),
 // settled-drop profile, a little more flow and its shifted sample point.
 const float allowed=(1+fabsf(stretch))*(soft*(face_physics::Surface::kEdgeSoftLimit+1+
                     .11f*1.35f*avatar_scene::geometry(scene).rx)+3.6f)+1e-3f;
 for(int y=0;y<135;++y) {
  if(soft!=0) {
   assert(fabsf(actual.left[y]-expected.left[y])<=allowed);
   assert(fabsf(actual.right[y]-expected.right[y])<=allowed);
   comparisons+=2;continue;
  }
  // Single-counter unused halves intentionally extend beyond the old oval.
  // Preserve the frozen oracle and exact parity on every unaffected edge.
  assert(stretch<0?actual.left[y]<=expected.left[y]+kRootTolerance:near(actual.left[y],expected.left[y]));
  assert(stretch>0?actual.right[y]>=expected.right[y]-kRootTolerance:near(actual.right[y],expected.right[y]));
  comparisons+=2;
 }
 for(int x=0;x<240;++x) {
  // The lower envelope only ever widens: unused-side stretch and soft sides.
  assert(stretch*(x+.5f-center)>0||soft!=0?actual.bottom[x]>=expected.bottom[x]-kRootTolerance:
                               near(actual.bottom[x],expected.bottom[x]));
  ++comparisons;
 }
}
int main(int argc,char** argv) {
 const mouth_raster::Point point={2.3f,4.7f};
 const auto zero=mouth_raster::Segment(point,point);
 assert(mouth_raster::distanceSquared(1,2,zero)==
        reference_mouth_raster::distanceSquared(1,2,{point.x,point.y},{point.x,point.y}));
 for(int state=0;state<3;++state)for(int clip=0;clip<eye_acting::clipCount(static_cast<eye_acting::State>(state));++clip) {
  eye_acting::Player actor;actor.state=static_cast<eye_acting::State>(state);actor.index=clip;
  for(unsigned ms=0;ms<4000;ms+=137) {
   const auto pose=actor.body(ms);
   for(int thinking=0;thinking<2;++thinking)
    checkMouth(mouth_raster::expression(pose,thinking),119.3f,110.7f,(ms%5+1)/5.0f);
   avatar_scene::Scene scene;scene.body=pose;scene.flowClock=ms*.0006f;
   for(float corner: {0.0f,1e-8f,.15f,.5f,1.0f}) {
    scene.corners=corner;checkMask(scene);
   }
  }
 }
 // Arbitrary still contours, clipped edges, full rotations and opacity bounds.
 for(int i=0;i<200;++i) {
  mouth_raster::Shape shape={2+(i%23)*.7f,(i%7)*1.1f,(i%11)*.6f,
                            static_cast<float>(i%181-90),(i%9-4)*.5f,(i%11)/10.0f};
  checkMouth(shape,(i%5==0?0:i%5==1?239:120)+.3f,
                   (i%7==0?0:i%7==1?134:111)+.7f,(i%13-1)/10.0f);
  avatar_scene::Scene scene;scene.side=i%2?0:1e-8f;scene.sleep=(i%11)/10.0f;
  scene.corners=i%3?0:.3f;scene.offset=(i%7-3)*15;scene.flowClock=i*.012f;
  face_physics::Surface surface;surface.kick(i%2?1:-1,.7f);
  surface.step(.05f,eye_acting::p(),true);scene.surface=&surface;
  checkMask(scene);
 }
 printf("Round-3 exact mouth pixels/mask coordinates: %lu (worst root ulp drift %.2gpx)\n",comparisons,worstRoot);
 if(argc==2 && strcmp(argv[1],"--benchmark")==0) {
  constexpr int iterations=2000;
  const auto shape=mouth_raster::expression(eye_acting::p(7,0,1,.9f,.18f,.18f),true);
  const reference_mouth_raster::Shape original={shape.halfWidth,shape.curve,shape.opening,
                                               shape.tilt,shape.skew,shape.purse};
  for(int version=0;version<2;++version) {
   const auto start=std::chrono::steady_clock::now();
   for(int i=0;i<iterations;++i) {
    float sum=0;
    const auto pixel=[&](int,int,float a){sum+=a;};
    if(version)mouth_raster::render(120.3f,110.7f,shape,1,pixel);
    else reference_mouth_raster::render(120.3f,110.7f,original,1,pixel);
    benchmarkSink+=sum;
   }
   const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
   printf("HOST ONLY mouth %s: %.2fms / %d iterations (not device FPS)\n",
          version?"optimized":"reference",ms,iterations);
  }
  for(int version=0;version<2;++version) {
   const auto start=std::chrono::steady_clock::now();
   for(int i=0;i<iterations;++i) {
    avatar_scene::Scene scene;scene.corners=0;scene.flowClock=i*.001f;
    if(version){avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);benchmarkSink+=mask.bottom[100];}
    else {reference_avatar_scene::Mask mask;reference_avatar_scene::buildMask(originalScene(scene),mask);benchmarkSink+=mask.bottom[100];}
   }
   const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
   printf("HOST ONLY vertical mask %s: %.2fms / %d iterations (not device FPS)\n",
          version?"optimized":"reference",ms,iterations);
  }
 }
}
