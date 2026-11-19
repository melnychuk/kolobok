#pragma once
#include <math.h>
#include "fluid_face.h"
#include "eye_raster.h"
#include "mouth_raster.h"
#include "face_physics.h"
#include "listen_wave.h"

// Optional secondary artwork. Keep the implementation available for later,
// but ship an eyes-only character unless explicitly enabled at compile time.
#ifndef STICK_S3_ENABLE_MOUTH
#define STICK_S3_ENABLE_MOUTH 0
#endif

namespace avatar_scene {
constexpr float kAlertBandHeight=28;
// Shared with firmware and native event reflow: quicker travel, smooth tails.
constexpr float kShiftSeconds=.15f;
constexpr float kReflowSeconds=.21f;
inline float eyeBaseline(float corners,float alert,float sleep,float breath=0) {
 return 67.5f-20*corners+21*alert+8*sleep+breath;
}
inline float mix(float a,float b,float t) { return a+(b-a)*t; }
// Exponential ease that actually ARRIVES. A plain exponential never reaches its
// target in float (it stalls a denormal/ulp away), which silently defeats every
// exact-zero fast path and keeps exact-keyed caches missing. 1e-4 is far below
// a visible pixel or colour step for every eased layout quantity.
inline float approach(float value,float target,float seconds,float timeConstant) {
 const float next=value+(target-value)*(1.0f-expf(-seconds/timeConstant));
 return fabsf(target-next)<1e-4f?target:next;
}
using ActingPose=eye_acting::Pose;
using Surface=face_physics::Surface;
constexpr ActingPose neutralPose=eye_acting::p();
// An authored local reach, anchored to a resting numeral (never its own hop).
struct Poke {
 float side=0,strength=0,cx=0,cy=0,width=0,height=0,kiss=0;
};
// Completion-sorting blobs: the lower rim droops into a soft lobe over each
// numeral (0 done, 1 active), its tip at y, centred at x, halfWidth wide.
// Zero reach keeps every raster exact.
struct Hands {
 float reach[2]={0,0},x[2]={0,0},y[2]={0,0},halfWidth[2]={0,0};
};
struct Scene {
 Poke poke;
 float region=144,side=1,corners=1,offset=0,alert=0,sleep=0;
 float eyeWidth=50,eyeHeight=56,awakeWidth=50,awakeHeight=56,gap=19;
 float eyeX=120,eyeY=47.5f,flowClock=0;
 // Width-aware numeral accommodation; defaults preserve the original raster.
 float counterShift=0,counterScale=1;
 // Whole-character translation for task cues (dodge, leave/re-enter). Zero
 // keeps every established raster exact; the face may go fully offscreen.
 float away=0;
 // Step-over: a rounded foot leads out of one side (-1 left, +1 right) and
 // plants past the screen edge. Zero progress keeps every raster exact.
 float stepSide=0,stepProgress=0;
 Hands hands;
 // Listening soundwave: 0 keeps the eye raster exact; see listen_wave.h.
 float wave=0;
 float waveBars[listen_wave::kBars]={};
 ActingPose eyes=neutralPose,body=neutralPose;
 bool thinking=false;
 const Surface* surface=nullptr;
};
struct Geometry { float cx,cy,rx,ry,power,floor; };
// Superellipse profile (1-|n|^p)^(1/p). Settled layouts use exactly p=2
// (vertical) or p=4 (paired/request) now that eased layout values arrive, so
// libm powf - two calls per row and per column, ~750 per frame - is needed
// only during reflow. x^2 is bit-identical to powf; the roots are correctly
// rounded sqrtf, within an ulp of powf (sub-1e-4px edge differences).
inline float profile(float n,float power) {
 if(power==2) return sqrtf(fmaxf(0,1-n*n));
 if(power==4) { const float n2=n*n;return sqrtf(sqrtf(fmaxf(0,1-n2*n2))); }
 return powf(fmaxf(0,1-powf(n,power)),1/power);
}
// A rounded, body-attached extension, unioned with (not clipped by) the base
// mask. Geometry is prepared once; only its AA fringe needs a square root.
struct Extrusion {
 float ax=0,ay=0,bx=0,by=0,radius=0,dx=0,dy=0,inverseLength=0;
 float left=0,right=0,top=0,bottom=0,nx=0,ny=0;
 void set(float x0,float y0,float x1,float y1,float r) {
  ax=x0;ay=y0;bx=x1;by=y1;radius=r;dx=bx-ax;dy=by-ay;
  const float length=dx*dx+dy*dy;
  inverseLength=0;
  float scale=0;
  if(length>0) { inverseLength=1/length;scale=r/sqrtf(length); }
  nx=-dy*scale;ny=dx*scale;
  left=fminf(ax,bx)-r-.5f;right=fmaxf(ax,bx)+r+.5f;
  top=fminf(ay,by)-r-.5f;bottom=fmaxf(ay,by)+r+.5f;
 }
 float coverage(int x,int y) const {
  if(radius==0||x+1<=left||x>=right||y+1<=top||y>=bottom)return 0;
  const float t=face_physics::clamp(((x+.5f-ax)*dx+(y+.5f-ay)*dy)*inverseLength,0,1);
  const float px=x+.5f-ax-t*dx,py=y+.5f-ay-t*dy;
  const float squared=px*px+py*py;
  if(radius>=.5f&&squared<=(radius-.5f)*(radius-.5f))return 1;
  if(squared>=(radius+.5f)*(radius+.5f))return 0;
  return face_physics::clamp(radius+.5f-sqrtf(squared),0,1);
 }
 // Exact geometric cross-section for contact, without using the label's hop
 // or the capsule's rectangular candidate bounds as collision evidence.
 bool span(float at,bool vertical,float& lo,float& hi) const {
  if(radius==0)return false;
  const float a=vertical?ax:ay,b=vertical?bx:by;
  const float u=vertical?ay:ax,v=vertical?by:bx;
  const float n=vertical?nx:ny,m=vertical?ny:nx;
  lo=1000;hi=-1000;
  for(int i=0;i<2;++i) {
   const float distance=at-(i?b:a);
   if(fabsf(distance)>radius)continue;
   const float half=sqrtf(fmaxf(0,radius*radius-distance*distance));
   lo=fminf(lo,(i?v:u)-half);hi=fmaxf(hi,(i?v:u)+half);
  }
  const float xs[]={a+n,b+n,b-n,a-n};
  const float ys[]={u+m,v+m,v-m,u-m};
  for(int i=0;i<4;++i) {
   const int j=(i+1)&3;
   if(at<fminf(xs[i],xs[j])||at>fmaxf(xs[i],xs[j]))continue;
   if(xs[i]==xs[j]) { lo=fminf(lo,fminf(ys[i],ys[j]));hi=fmaxf(hi,fmaxf(ys[i],ys[j])); }
   else {
    const float value=ys[i]+(at-xs[i])/(xs[j]-xs[i])*(ys[j]-ys[i]);
    lo=fminf(lo,value);hi=fmaxf(hi,value);
   }
  }
  return hi>=lo;
 }
};
struct Mask {
 float left[135]={},right[135]={},bottom[240]={};
 Extrusion hand;
 // A row-prepared skin fillet joins a side reach without changing the base
 // used to clip the eyes. Zero side keeps the original no-hand fast path.
 float shoulder[135]={};
 int shoulderSide=0,shoulderTop=135,shoulderBottom=-1;
 // Step-over foot: per row, a span from the outer edge to an opaque body root.
 float stepEdge[135]={},stepRoot[135]={};
 int stepSide=0,stepTop=135,stepBottom=-1;
 float stepCoverage(int x,int y) const {
  if(!stepSide||y<stepTop||y>stepBottom)return 0;
  return stepSide<0?face_contour::coverage(stepEdge[y],stepRoot[y],x):
                    face_contour::coverage(stepRoot[y],stepEdge[y],x);
 }
 float shoulderCoverage(int x,int y) const {
  if(!shoulderSide||y<shoulderTop||y>shoulderBottom)return 0;
  if(shoulderSide<0&&shoulder[y]<left[y])
   return face_contour::coverage(shoulder[y],left[y]+2,x);
  if(shoulderSide>0&&shoulder[y]>right[y])
   return face_contour::coverage(right[y]-2,shoulder[y],x);
  return 0;
 }
 float baseCoverage(int x,int y) const {
  if(x<0||x>=240||y<0||y>=135)return 0;
  // Almost every pixel is fully outside or inside. Only the AA fringe needs
  // libm min/max; preserve the original area-intersection formula there.
  if(x+1<=left[y] || x>=right[y] || bottom[x]<=y)return 0;
  if(x>=left[y] && x+1<=right[y] && bottom[x]>=y+1)return 1;
  return fminf(face_contour::coverage(left[y],right[y],x),
               fmaxf(0,fminf(1,bottom[x]-y)));
 }
 float coverage(int x,int y) const {
  const float base=baseCoverage(x,y);
  if(base==1||(hand.radius==0&&!stepSide)||x<0||x>=240||y<0||y>=135)return base;
  float value=base;
  if(hand.radius!=0)value=fmaxf(fmaxf(value,hand.coverage(x,y)),shoulderCoverage(x,y));
  return stepSide?fmaxf(value,stepCoverage(x,y)):value;
 }
 float bottomAt(int x) const {
  float lo,hi;
  float value=hand.span(x+.5f,true,lo,hi)?fmaxf(bottom[x],hi):bottom[x];
  // Contact follows the actual added skin too, never a label's own hop.
  if(shoulderSide)for(int y=shoulderTop;y<=shoulderBottom;++y)
   if(shoulderCoverage(x,y)>.5f)value=fmaxf(value,y+1.0f);
  return value;
 }
 void rowBounds(int y,float& lo,float& hi) const {
  lo=left[y];hi=right[y];
  if(shoulderSide<0&&y>=shoulderTop&&y<=shoulderBottom)lo=fminf(lo,shoulder[y]);
  if(shoulderSide>0&&y>=shoulderTop&&y<=shoulderBottom)hi=fmaxf(hi,shoulder[y]);
  if(stepSide<0&&y>=stepTop&&y<=stepBottom&&stepEdge[y]<stepRoot[y])lo=fminf(lo,stepEdge[y]);
  if(stepSide>0&&y>=stepTop&&y<=stepBottom&&stepEdge[y]>stepRoot[y])hi=fmaxf(hi,stepEdge[y]);
  float a,b;
  if(hand.span(y+.5f,false,a,b)) {
   if(hi<=lo){lo=a;hi=b;}
   else {lo=fminf(lo,a);hi=fmaxf(hi,b);}
  }
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
   if(hand.radius>0&&y+1>hand.top&&y<hand.bottom) {
    first=static_cast<int>(fminf(first,floorf(hand.left)));
    end=static_cast<int>(fmaxf(end,ceilf(hand.right)));
   }
   if(y>=shoulderTop&&y<=shoulderBottom) {
    if(shoulderSide<0)first=static_cast<int>(fminf(first,floorf(shoulder[y])));
    if(shoulderSide>0)end=static_cast<int>(fmaxf(end,ceilf(shoulder[y])));
   }
   if(y>=stepTop&&y<=stepBottom) {
    if(stepSide<0)first=static_cast<int>(fminf(first,floorf(stepEdge[y])));
    if(stepSide>0)end=static_cast<int>(fmaxf(end,ceilf(stepEdge[y])));
   }
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
 const float rx=size.x*scene.body.bodyWidth*(1+.07f*impact)*scene.counterScale;
 const float ry=size.y*scene.body.bodyHeight*(1-.05f*impact)*scene.counterScale;
 const float px=scene.surface?scene.surface->x:scene.body.lean;
 const float py=scene.surface?scene.surface->y:scene.body.lift;
 const float floor=104+4*scene.alert;
 return {120+scene.offset+scene.counterShift+scene.away+px,mix(67.5f,floor-ry,scene.corners)+py,
         rx,ry,mix(2,4,scene.corners),floor};
}
// Stretch only the unused half of a single-counter body past the display.
// Keep geometry/eye/mouth anchors and the counter-facing contour unchanged.
// Signed strength follows existing reflow/offset easing, with no new clock or
// threshold pop; paired, request and sleeping layouts retain their old shape.
inline float unusedSideStretch(const Scene& scene) {
 const float shift=fminf(1,fabsf(scene.offset)/fmaxf(1,(240-scene.region)*.25f));
 const float strength=2*eye_acting::smooth(shift)*(1-scene.corners)*
                      (1-scene.alert)*(1-scene.sleep)*scene.side;
 return scene.offset>0?strength:scene.offset<0?-strength:0;
}
// Soft vertical sides: zero in the paired/request layouts (their exact contour
// is preserved) and eased continuously by the existing reflow/sleep values.
inline float softSides(const Scene& scene) {
 return (1-scene.corners)*(1-scene.alert)*(1-.8f*scene.sleep)*scene.side;
}
inline bool sideBounds(const Scene& scene,const Geometry& g,const fluid_face::Frame& fluid,
                       int y,float& left,float& right) {
 left=right=0;
 if(y+.5f<kAlertBandHeight*scene.alert)return false;
 float ny=(y+.5f-g.cy)/g.ry;
 // The lower spring rim, not a rigid ellipse cap, decides which pixels exist
 // around the support. This permits free overhangs below the contact plane.
 if(ny>.85f)ny=mix(ny,.85f,scene.corners);
 if(fabsf(ny)>=1)return false;
 float half=g.rx*profile(fabsf(ny),g.power);
 const float soft=softSides(scene);
 // A standing blob settles: narrower shoulders and a heavier lower belly,
 // so the side reads as a drop rather than a rigid parenthesis.
 if(soft!=0)half*=1+.11f*soft*(ny+.35f*(1-ny*ny)-.35f);
 const float bend=scene.surface?scene.surface->sample(y/280.0f)*.18f:0;
 const float other=scene.surface?scene.surface->sample(1-y/280.0f)*.18f:0;
 const float flow=(1-.8f*scene.sleep)*scene.side;
 // Exact-zero only: preserve every nonzero transition, including tiny flows.
 const float flowAmount=1.8f+1.0f*soft;
 const float leftFlow=flow==0?0:flowAmount*flow*fluid_face::field(g.cx-half,y,fluid);
 const float rightFlow=flow==0?0:flowAmount*flow*fluid_face::field(g.cx+half,y,fluid);
 left=g.cx-half+bend+leftFlow;
 right=g.cx+half+other+rightFlow;
 if(soft!=0&&scene.surface) {
  left+=soft*scene.surface->edgeSample(0,y+.5f);
  right+=soft*scene.surface->edgeSample(1,y+.5f);
 }
 const float stretch=unusedSideStretch(scene);
 if(stretch>0)right+=(right-g.cx)*stretch;
 else if(stretch<0)left+=(g.cx-left)*stretch;
 left=fmaxf(0,fminf(240,left));right=fmaxf(0,fminf(240,right));
 return right>left;
}
// Compatibility/read handle for tests of the side envelope. The complete face
// mask also includes the freely moving lower rim and may have multiple runs.
inline bool bounds(const Scene& scene,int y,float& left,float& right) {
 return sideBounds(scene,geometry(scene),fluid_face::Frame(scene.flowClock),y,left,right);
}
// Lower convex envelope of row samples, in O(rows), without a pixel loop.
// A tangent bridge fills a hard circle/body union's inward crease. Subtract
// the BASE envelope below so existing organic inflections are not flattened.
inline void convexEnvelope(float* edge) {
 int rows[135],count=0;
 for(int y=0;y<135;++y) {
  while(count>=2) {
   const int a=rows[count-2],b=rows[count-1];
   if((edge[b]-edge[a])*(y-b)<(edge[y]-edge[b])*(b-a))break;
   --count;
  }
  rows[count++]=y;
 }
 for(int i=1;i<count;++i) {
  const int a=rows[i-1],b=rows[i];
  const float first=edge[a],last=edge[b];
  for(int y=a+1;y<b;++y)edge[y]=mix(first,last,(y-a)/static_cast<float>(b-a));
 }
}
inline void roundShoulder(const Scene& scene,Mask& mask) {
 const float weight=(1-scene.corners)*(1-scene.alert)*(1-scene.sleep);
 if(weight<=0||mask.hand.radius==0)return; // paired/request/sleep stay exact
 const int side=scene.poke.side<0?-1:1;
 const float sign=side<0?1.0f:-1.0f;
 const float* original=side<0?mask.left:mask.right;
 float base[135];
 for(int y=0;y<135;++y) {
  base[y]=mask.shoulder[y]=original[y]*sign;
  float lo,hi;
  if(mask.hand.span(y+.5f,false,lo,hi))
   mask.shoulder[y]=fminf(mask.shoulder[y],(side<0?lo:hi)*sign);
 }
 convexEnvelope(base);convexEnvelope(mask.shoulder);
 for(int y=0;y<135;++y) {
  float outward=fmaxf(0,base[y]-mask.shoulder[y])*weight;
  // Every added row attaches to opaque BODY coverage, including during
  // reflow. Do not bridge a lower-rim hole or emit floating skin/white detail.
  const int root=static_cast<int>(side<0?ceilf(original[y])+1:floorf(original[y])-2);
  if(root<0||root>=240||mask.baseCoverage(root,y)<.999f)outward=0;
  mask.shoulder[y]=original[y]-sign*outward;
  if(outward>0) {
   mask.shoulderSide=side;
   if(y<mask.shoulderTop)mask.shoulderTop=y;
   mask.shoulderBottom=y;
  }
 }
}
inline void deformMask(const Scene& scene,Mask& mask) {
 mask.hand=Extrusion{};
 mask.shoulderSide=0;mask.shoulderTop=135;mask.shoulderBottom=-1;
 for(float& edge:mask.shoulder)edge=0;
 const auto& p=scene.poke;
 const float strength=fmaxf(0,p.strength)*(1-scene.alert)*(1-scene.sleep);
 if(strength==0||p.side==0)return;
 // A resistant body makes a broad shoulder, not a long thin finger. Keep the
 // radius larger than the permitted outward travel, even on affectionate checks.
 const float radius=mix(30,27,p.kiss);
 const int row=static_cast<int>(fmaxf(0,fminf(134,p.cy)));
 const int col=static_cast<int>(fmaxf(0,fminf(239,p.cx)));
 const float edge=p.side<0?mask.left[row]:mask.right[row];
 const float reach=face_physics::clamp(p.side*(p.cx-p.side*p.width/2-edge)+4,4,18);
 const float lower=face_physics::clamp(p.cy-p.height/2-mask.bottom[col]+4,4,14);
 // Blend attachment direction through reflow. The root is inside the actual
 // base silhouette, not on a guessed ellipse edge or underneath a clipped rim.
 float rootX=mix(edge-p.side*(radius+2),p.cx,scene.corners);
 float rootY=mix(p.cy,mask.bottom[col]-radius-2,scene.corners);
 rootX=face_physics::clamp(rootX,0,239);
 rootY=face_physics::clamp(rootY,0,134);
 // Find an opaque attachment toward the body center if noise, a flap, narrow
 // geometry or reflow put the first root outside. Never emit a detached hand.
 const Geometry g=geometry(scene);
 for(int step=0;step<80&&mask.baseCoverage(static_cast<int>(rootX),static_cast<int>(rootY))<.999f;++step) {
  rootX+=face_physics::clamp(g.cx-rootX,-1,1);
  rootY+=face_physics::clamp(g.cy-rootY,-1,1);
  rootX=face_physics::clamp(rootX,0,239);rootY=face_physics::clamp(rootY,0,134);
 }
 if(mask.baseCoverage(static_cast<int>(rootX),static_cast<int>(rootY))<.999f)return;
 const float tipX=rootX+p.side*(2+reach)*strength*(1-scene.corners);
 const float tipY=rootY+(2+lower)*strength*scene.corners;
 mask.hand.set(rootX,rootY,tipX,tipY,radius*eye_acting::smooth(strength/.25f));
 roundShoulder(scene,mask);
}
// Step-over foot. The front is a rounded nose (lower body first, rising a
// little mid-stride) that sweeps out and plants past the screen edge; upper
// rows follow. Every row attaches to opaque BASE coverage; it is unioned with
// the base like the shoulder, and never moves the eyes' clipping body.
constexpr float kStepLag=.6f,kStepPlant=8;
inline float stepWeight(float progress,int y) {
 if(progress<=0)return 0;
 const float u=progress<1?progress:1;
 // The toe lifts over the numeral's height mid-stride, then plants low.
 const float nose=112-38*4*u*(1-u);
 const float n=(y+.5f-nose)/72;
 const float lead=fminf(1,n*n);
 return eye_acting::smooth(progress*(1+kStepLag)-kStepLag*lead);
}
inline void stepMask(const Scene& scene,Mask& mask) {
 mask.stepSide=0;mask.stepTop=135;mask.stepBottom=-1;
 if(scene.stepProgress<=0||scene.stepSide==0)return;
 const int side=scene.stepSide<0?-1:1;
 const float target=side<0?-kStepPlant:240+kStepPlant;
 for(int y=0;y<135;++y) {
  const float w=stepWeight(scene.stepProgress,y);
  float root=side<0?mask.left[y]:mask.right[y];
  mask.stepEdge[y]=mask.stepRoot[y]=root;
  if(w<=0||mask.right[y]<=mask.left[y])continue;
  // Walk inward to the first opaque body pixel: the lower rim may cut the
  // row's corner, and the foot must never float.
  int x=static_cast<int>(side<0?ceilf(mask.left[y]):floorf(mask.right[y])-1);
  int steps=0;
  while(steps<48&&x>=0&&x<240&&mask.baseCoverage(x,y)<.999f){x-=side;++steps;}
  if(x<0||x>=240||mask.baseCoverage(x,y)<.999f)continue;
  root=side<0?x+1.0f:static_cast<float>(x);
  const float edge=mix(side<0?mask.left[y]:mask.right[y],target,w);
  mask.stepRoot[y]=root;
  mask.stepEdge[y]=side<0?fminf(edge,root):fmaxf(edge,root);
  if(mask.stepEdge[y]!=root) {
   mask.stepSide=side;
   if(y<mask.stepTop)mask.stepTop=y;
   mask.stepBottom=y;
  }
 }
}
// Sorting blobs: a rounded U-shaped lobe (circular tip, as wide as the
// numeral plus a margin) grows down out of the lower rim, joined to it by a
// smooth-max fillet, so it is one soft body with no crease or detached part.
// It only lowers existing body columns: a lobe can never float.
constexpr float kLobeFillet=7;
inline float smoothMax(float a,float b,float k) {
 const float h=fmaxf(k-fabsf(a-b),0)/k;
 return fmaxf(a,b)+h*h*k*.25f;
}
inline void lobeMask(const Scene& scene,Mask& mask) {
 for(int i=0;i<2;++i) {
  const float reach=fminf(1,scene.hands.reach[i]),radius=scene.hands.halfWidth[i];
  if(reach<=0||radius<=0)continue;
  const float cx=scene.hands.x[i];
  const int center=static_cast<int>(face_physics::clamp(cx,0,239));
  if(mask.bottom[center]<0)continue;
  // Grow out of the rim: the circular tip starts hidden inside the body.
  const float tip=mix(mask.bottom[center]-radius,scene.hands.y[i],eye_acting::smooth(reach));
  const int first=static_cast<int>(fmaxf(0,floorf(cx-radius-kLobeFillet)));
  const int last=static_cast<int>(fminf(239,ceilf(cx+radius+kLobeFillet)));
  for(int x=first;x<=last;++x) {
   if(mask.bottom[x]<0)continue;
   const float d=x+.5f-cx;
   const float lobe=fabsf(d)<radius?tip-radius+sqrtf(radius*radius-d*d):-1000;
   mask.bottom[x]=fmaxf(mask.bottom[x],smoothMax(mask.bottom[x],lobe,kLobeFillet));
  }
 }
}
inline void buildMask(const Scene& scene,Mask& mask) {
 const Geometry g=geometry(scene);
 const float stretch=unusedSideStretch(scene);
 // Soft sides may bulge past the rigid oval; keep the lower envelope out of
 // their way, or the two curves meet at a visible corner. Zero when paired.
 const float widen=1+.4f*softSides(scene);
 const fluid_face::Frame fluid(scene.flowClock);
 for(int y=0;y<135;++y)sideBounds(scene,g,fluid,y,mask.left[y],mask.right[y]);
 for(int x=0;x<240;++x) {
  float nx=fabsf((x+.5f-g.cx)/g.rx);
  if(widen!=1)nx/=widen;
  // Use the same one-sided stretch for the lower envelope; extending only
  // row bounds would leave a curved blue sliver clipped by the old oval bottom.
  if(stretch*(x+.5f-g.cx)>0)nx/=1+fabsf(stretch);
  if(nx>=1){mask.bottom[x]=-1000;continue;}
  const float base=g.cy+g.ry*profile(nx,g.power);
  const float flap=scene.surface?scene.surface->sample(x/239.0f):0;
  // Settled vertical layouts have no lower-rim drift; do not evaluate unused
  // noise. No epsilon/threshold gate that could make the reflow silhouette pop.
  const float drift=scene.corners==0?0:
                    1.4f*fluid_face::field(x,g.floor,fluid)*scene.corners;
  mask.bottom[x]=base+(flap+drift)*(1-.8f*scene.sleep);
 }
 lobeMask(scene,mask);
 deformMask(scene,mask);
 stepMask(scene,mask);
}
// Visual-only height trim: leave body/mouth anchors and authored clocks intact.
// Preserve the engaged request floor and fade back to original sleep-line size.
constexpr float kEyeHeightScale=.81f;
// Requests keep large open eyes: the authored request rest pose (1.15 of the
// 44px request height) renders at 48px. The scale, not a hard floor, carries
// the request squash/stretch acting (about 37..52px) instead of flattening it.
constexpr float kRequestEyeHeight=48,kRequestAwakeHeight=44,kRequestRestHeight=1.15f;
constexpr float kRequestEyeScale=kRequestEyeHeight/(kRequestAwakeHeight*kRequestRestHeight);
// Eye-size target floor while requesting: open, but below the authored squash.
constexpr float kRequestEyeFloor=36;
inline float renderedEyeHeight(const Scene& scene) {
 return scene.eyeHeight*mix(mix(kEyeHeightScale,1,scene.sleep),kRequestEyeScale,scene.alert)*scene.counterScale;
}
// Cartoon blink: the closing lid sinks toward the eye's lower edge and the
// closed line widens a little. Pose-independent constants; plain blinks only.
constexpr float kBlinkSink=.22f,kBlinkWiden=.06f;
inline void setWave(Scene& scene,const listen_wave::Wave& wave) {
 scene.wave=wave.amount;
 for(int i=0;i<listen_wave::kBars;++i)scene.waveBars[i]=wave.bar(i);
}
// First part of the listening morph: the eyes close into the blink line
// (smile/lids fading with the closure so the line is the plain one).
inline ActingPose waveClosing(const Scene& scene) {
 ActingPose pose=scene.eyes;
 const float close=listen_wave::unit(scene.wave/listen_wave::kClose);
 pose.closeL=fmaxf(pose.closeL,close);pose.closeR=fmaxf(pose.closeR,close);
 pose.smile*=1-close;
 return pose;
}
// Second part: bars from that line, clipped to the face like the eyes.
template<class Pixel>
void waveBars(const Scene& scene,const Mask& mask,Pixel pixel) {
 const auto& e=scene.eyes;
 const auto scale=[](float delta){return eye_raster::clamp(1+delta,.75f,1.25f);};
 listen_wave::bars(scene.eyeX+scene.counterShift+scene.away,scene.eyeY,scene.eyeWidth*scene.counterScale,
     renderedEyeHeight(scene),scene.gap*scene.counterScale,scene.wave,scale(e.scaleL),scale(e.scaleR),
     e.tilt,e.closeL,e.closeR,kBlinkSink,kBlinkWiden,
     [&](int i){return scene.waveBars[i];},
     [&](float x,float y,float w,float h,float r) {
  eye_raster::box(x,y,w,h,r,[&](int px,int py,float alpha) {
   const float coverage=mask.baseCoverage(px,py);
   if(coverage>0)pixel(px,py,alpha*coverage);
  });
 });
}
template<class Pixel>
void eyes(const Scene& scene,const Mask& mask,Pixel pixel) {
 if(scene.wave>=listen_wave::kClose) { waveBars(scene,mask,pixel);return; }
 eye_raster::render(scene.eyeX+scene.counterShift+scene.away,scene.eyeY,scene.eyeWidth*scene.counterScale,
                    renderedEyeHeight(scene),scene.gap*scene.counterScale,
                    scene.wave>0?waveClosing(scene):scene.eyes,[&](int x,int y,float alpha) {
  const float coverage=mask.baseCoverage(x,y);
  if(coverage>0)pixel(x,y,alpha*coverage);
 },kBlinkSink,kBlinkWiden);
}
// Span variant: interior eye runs fully inside the face arrive as fill(x,y,n);
// everything else, including runs crossing the face edge, stays per-pixel and
// in the original order.
template<class Pixel,class Fill>
void eyes(const Scene& scene,const Mask& mask,Pixel pixel,Fill fill) {
 const auto edge=[&](int x,int y,float alpha) {
  const float coverage=mask.baseCoverage(x,y);
  if(coverage>0)pixel(x,y,alpha*coverage);
 };
 if(scene.wave>=listen_wave::kClose) { waveBars(scene,mask,pixel);return; }
 eye_raster::render(scene.eyeX+scene.counterShift+scene.away,scene.eyeY,scene.eyeWidth*scene.counterScale,
                    renderedEyeHeight(scene),scene.gap*scene.counterScale,
                    scene.wave>0?waveClosing(scene):scene.eyes,edge,[&](int x,int y,int length) {
  const bool row=y>=0&&y<135&&x>=0&&x+length<=240&&x>=mask.left[y]&&x+length<=mask.right[y];
  int run=-1;
  for(int i=x;i<x+length;++i) {
   if(row&&mask.bottom[i]>=y+1) { if(run<0)run=i;continue; }
   if(run>=0){fill(run,y,i-run);run=-1;}
   edge(i,y,1);
  }
  if(run>=0)fill(run,y,x+length-run);
 },kBlinkSink,kBlinkWiden);
}
template<class Pixel>
void eyes(const Scene& scene,Pixel pixel) {
 Mask mask;buildMask(scene,mask);eyes(scene,mask,pixel);
}
inline float mouthVisibility(const Scene& scene) {
#if !STICK_S3_ENABLE_MOUTH
 (void)scene;
 return 0;
#else
 // Disappear early in the move into the wide two-counter/request layout.
 // Fade back only as the face becomes vertical; sleep remains eyes-only.
 return (1-eye_acting::smooth(fmaxf(scene.corners,scene.alert)/.35f))*
        (1-eye_acting::smooth(scene.sleep/.5f));
#endif
}
template<class Pixel>
void mouth(const Scene& scene,const Mask& mask,Pixel pixel) {
 const float opacity=mouthVisibility(scene);
 if(opacity<=0)return;
 const Geometry g=geometry(scene);
 // The 90ms-delayed body pose supplies secondary acting; blinks do not make
 // the mouth flap. Anchor to the body with only partial gaze follow-through.
 const auto shape=mouth_raster::expression(scene.body,scene.thinking);
 const float cx=mix(g.cx,scene.eyeX+scene.counterShift+scene.away,.60f);
 const float cy=fminf(126-shape.curve-shape.opening-3*shape.purse,
                     scene.eyeY+fmaxf(scene.eyeHeight,scene.awakeHeight)/2+17);
 // Pokes/kisses use only black silhouette acting. No white lip/hand detail;
 // the ordinary mouth stays on the original body and rests during the press.
 const float quiet=1-eye_acting::smooth(fmaxf(0,scene.poke.strength)/.25f);
 mouth_raster::render(cx,cy,shape,opacity*quiet,[&](int x,int y,float alpha) {
  const float coverage=mask.baseCoverage(x,y);
  if(coverage>0)pixel(x,y,alpha*coverage);
 });
}
} // namespace avatar_scene
