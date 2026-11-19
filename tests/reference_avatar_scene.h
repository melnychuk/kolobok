#pragma once
#include <math.h>
#include "../firmware/fluid_face.h"
#include "../firmware/eye_raster.h"
#include "../firmware/mouth_raster.h"
#include "../firmware/face_physics.h"
// Frozen pre-round-3 mask oracle. Do not optimize with production code.

namespace reference_avatar_scene {
constexpr float kAlertBandHeight=28;
// Shared with firmware and native event reflow: quicker travel, smooth tails.
constexpr float kShiftSeconds=.15f;
constexpr float kReflowSeconds=.21f;
inline float eyeBaseline(float corners,float alert,float sleep,float breath=0) {
 return 67.5f-20*corners+21*alert+8*sleep+breath;
}
inline float mix(float a,float b,float t) { return a+(b-a)*t; }
using ActingPose=eye_acting::Pose;
using Surface=face_physics::Surface;
constexpr ActingPose neutralPose=eye_acting::p();
struct Scene {
 float region=144,side=1,corners=1,offset=0,alert=0,sleep=0;
 float eyeWidth=50,eyeHeight=56,awakeWidth=50,awakeHeight=56,gap=19;
 float eyeX=120,eyeY=47.5f,flowClock=0;
 ActingPose eyes=neutralPose,body=neutralPose;
 bool thinking=false;
 const Surface* surface=nullptr;
};
struct Geometry { float cx,cy,rx,ry,power,floor; };
struct Mask {
 float left[135]={},right[135]={},bottom[240]={};
 float coverage(int x,int y) const {
  if(x<0||x>=240||y<0||y>=135)return 0;
  // Almost every pixel is fully outside or inside. Only the AA fringe needs
  // libm min/max; preserve the original area-intersection formula there.
  if(x+1<=left[y] || x>=right[y] || bottom[x]<=y)return 0;
  if(x>=left[y] && x+1<=right[y] && bottom[x]>=y+1)return 1;
  return fminf(face_contour::coverage(left[y],right[y],x),
               fmaxf(0,fminf(1,bottom[x]-y)));
 }
 // Produce the same .999-threshold opaque runs as firmware's old pixel scan.
 // Block minima are recomputed from bottom[] so manually edited masks are safe.
 template<class Span,class Pixel>
 void raster(Span span,Pixel pixel) const {
  float minimum[15];
  for(int b=0;b<15;++b) {
   minimum[b]=bottom[b*16];
   for(int j=1;j<16;++j)if(bottom[b*16+j]<minimum[b])minimum[b]=bottom[b*16+j];
  }
  for(int y=0;y<135;++y) {
   int first=static_cast<int>(floorf(left[y]));
   int end=static_cast<int>(ceilf(right[y]));
   if(first<0)first=0;
   if(end>240)end=240;
   const int innerFirst=static_cast<int>(ceilf(left[y]));
   const int innerEnd=static_cast<int>(floorf(right[y]));
   int run=-1;
   for(int x=first;x<end;) {
    if((x&15)==0 && x>=innerFirst && x+16<=innerEnd && minimum[x>>4]>=y+1) {
     if(run<0)run=x;
     x+=16;continue;
    }
    const float alpha=coverage(x,y);
    if(alpha>=.999f) { if(run<0)run=x; }
    else {
     if(run>=0){span(run,y,x-run);run=-1;}
     if(alpha>0)pixel(x,y,alpha);
    }
    ++x;
   }
   if(run>=0)span(run,y,end-run);
  }
 }
};
inline Geometry geometry(const Scene& scene) {
 const float radius=scene.region/2+fabsf(scene.offset);
 const float baseX=mix(radius,152,scene.corners);
 const float baseY=mix(fmaxf(mix(160,96,scene.side),radius*1.32f),180,scene.corners);
 const float wr=mix(scene.body.width,scene.eyeWidth/scene.awakeWidth,scene.sleep);
 const float hr=mix(scene.body.height,scene.eyeHeight/scene.awakeHeight,scene.sleep);
 const auto size=face_contour::bodySize(baseX,baseY,scene.awakeWidth*wr,
                                       scene.awakeHeight*hr,scene.awakeWidth,scene.awakeHeight);
 const float impact=scene.surface?scene.surface->impact:0;
 const float rx=size.x*scene.body.bodyWidth*(1+.07f*impact);
 const float ry=size.y*scene.body.bodyHeight*(1-.05f*impact);
 const float px=scene.surface?scene.surface->x:scene.body.lean;
 const float py=scene.surface?scene.surface->y:scene.body.lift;
 const float floor=104+4*scene.alert;
 return {120+scene.offset+px,mix(67.5f,floor-ry,scene.corners)+py,
         rx,ry,mix(2,4,scene.corners),floor};
}
inline bool sideBounds(const Scene& scene,const Geometry& g,int y,float& left,float& right) {
 left=right=0;
 if(y+.5f<kAlertBandHeight*scene.alert)return false;
 float ny=(y+.5f-g.cy)/g.ry;
 // The lower spring rim, not a rigid ellipse cap, decides which pixels exist
 // around the support. This permits free overhangs below the contact plane.
 if(ny>.85f)ny=mix(ny,.85f,scene.corners);
 if(fabsf(ny)>=1)return false;
 const float half=g.rx*powf(fmaxf(0,1-powf(fabsf(ny),g.power)),1/g.power);
 const float bend=scene.surface?scene.surface->sample(y/280.0f)*.18f:0;
 const float other=scene.surface?scene.surface->sample(1-y/280.0f)*.18f:0;
 const float flow=(1-.8f*scene.sleep)*scene.side;
 left=g.cx-half+bend+1.8f*flow*fluid_face::field(g.cx-half,y,scene.flowClock);
 right=g.cx+half+other+1.8f*flow*fluid_face::field(g.cx+half,y,scene.flowClock);
 left=fmaxf(0,fminf(240,left));right=fmaxf(0,fminf(240,right));
 return right>left;
}
// Compatibility/read handle for tests of the side envelope. The complete face
// mask also includes the freely moving lower rim and may have multiple runs.
inline bool bounds(const Scene& scene,int y,float& left,float& right) {
 return sideBounds(scene,geometry(scene),y,left,right);
}
inline void buildMask(const Scene& scene,Mask& mask) {
 const Geometry g=geometry(scene);
 for(int y=0;y<135;++y)sideBounds(scene,g,y,mask.left[y],mask.right[y]);
 for(int x=0;x<240;++x) {
  const float nx=fabsf((x+.5f-g.cx)/g.rx);
  if(nx>=1){mask.bottom[x]=-1000;continue;}
  const float base=g.cy+g.ry*powf(1-powf(nx,g.power),1/g.power);
  const float flap=scene.surface?scene.surface->sample(x/239.0f):0;
  const float drift=1.4f*fluid_face::field(x,g.floor,scene.flowClock)*scene.corners;
  mask.bottom[x]=base+(flap+drift)*(1-.8f*scene.sleep);
 }
}
template<class Pixel>
void eyes(const Scene& scene,const Mask& mask,Pixel pixel) {
 eye_raster::render(scene.eyeX,scene.eyeY,scene.eyeWidth,scene.eyeHeight,scene.gap,
                    scene.eyes,[&](int x,int y,float alpha) {
  const float coverage=mask.coverage(x,y);
  if(coverage>0)pixel(x,y,alpha*coverage);
 });
}
template<class Pixel>
void eyes(const Scene& scene,Pixel pixel) {
 Mask mask;buildMask(scene,mask);eyes(scene,mask,pixel);
}
inline float mouthVisibility(const Scene& scene) {
 // Disappear early in the move into the wide two-counter/request layout.
 // Fade back only as the face becomes vertical; sleep remains eyes-only.
 return (1-eye_acting::smooth(fmaxf(scene.corners,scene.alert)/.35f))*
        (1-eye_acting::smooth(scene.sleep/.5f));
}
template<class Pixel>
void mouth(const Scene& scene,const Mask& mask,Pixel pixel) {
 const float opacity=mouthVisibility(scene);
 if(opacity<=0)return;
 const Geometry g=geometry(scene);
 // The 90ms-delayed body pose supplies secondary acting; blinks do not make
 // the mouth flap. Anchor to the body with only partial gaze follow-through.
 const auto shape=mouth_raster::expression(scene.body,scene.thinking);
 const float cx=mix(g.cx,scene.eyeX,.60f);
 const float cy=fminf(126-shape.curve-shape.opening-3*shape.purse,
                     scene.eyeY+fmaxf(scene.eyeHeight,scene.awakeHeight)/2+17);
 mouth_raster::render(cx,cy,shape,opacity,[&](int x,int y,float alpha) {
  const float coverage=mask.coverage(x,y);
  if(coverage>0)pixel(x,y,alpha*coverage);
 });
}
} // namespace reference_avatar_scene
