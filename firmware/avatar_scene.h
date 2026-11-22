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
// Input request ("sunrise") layout: the screen becomes a slowly pulsing
// orange-yellow field with the `!` above, and the body sits below it with only
// the top of its blob showing. The dome's centre is below the display, so the
// lower rim never shows; stretching raises the top, a hop lifts the whole dome.
constexpr float kRequestDomeX=120,kRequestDomeY=172,kRequestDomeRadiusX=126,kRequestDomeRadiusY=124;
constexpr float kRequestEyeY=94;
// The skin keeps at least this much body above the eyes: looking up at the
// `!` stretches the dome instead of clipping flat eye tops.
constexpr float kRequestEyeHeadroom=9;
// Eyes follow this share of the dome top's hop squash/stretch.
constexpr float kRequestHopEyeFollow=.6f;
// The authored request rest pose (eye/body width 1.10, height 1.15) is the
// dome's neutral size; acting squash/stretch reaches it only partly.
constexpr float kRequestRestWidth=1.10f,kRequestRestHeight=1.15f;
constexpr float kRequestDomeStretchX=.35f,kRequestDomeStretchY=.30f;
// Shared with firmware and native event reflow: quicker travel, smooth tails.
constexpr float kShiftSeconds=.15f;
constexpr float kReflowSeconds=.21f;
inline float mix(float a,float b,float t) { return a+(b-a)*t; }
// Zero alert is exactly the original 67.5-20*corners baseline.
inline float eyeBaseline(float corners,float alert,float sleep,float breath=0) {
 return mix(67.5f-20*corners,kRequestEyeY,alert)+8*sleep+breath;
}
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
 // Request hop (request_scene.h): pixels the dome and eyes are lifted, and
 // the dome's signed squash(-)/stretch(+). Zero keeps every raster exact.
 float hop=0,hopStretch=0;
 Hands hands;
 // Listening soundwave: 0 keeps the eye raster exact; see listen_wave.h.
 float wave=0;
 float waveBars[listen_wave::kBars]={};
 ActingPose eyes=neutralPose,body=neutralPose;
 bool thinking=false;
 const Surface* surface=nullptr;
};
struct Geometry { float cx,cy,rx,ry,power,floor; };
inline float renderedEyeHeight(const Scene& scene);
inline float smoothMax(float a,float b,float k) {
 const float h=fmaxf(k-fabsf(a-b),0)/k;
 return fmaxf(a,b)+h*h*k*.25f;
}
// Eye centre including the request hop; zero hop is exactly scene.eyeY.
inline float eyeCenterY(const Scene& scene) {
 if(scene.hop==0&&scene.hopStretch==0)return scene.eyeY;
 return scene.eyeY-scene.hop-kRequestHopEyeFollow*kRequestDomeRadiusY*scene.hopStretch;
}
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
// Local swellings: a smooth bell of outward pixels along one edge. It has
// gentle inflections where it rejoins the body (no tangent lines, no union
// crease) and is exactly zero outside its width, so idle masks stay exact.
inline float bell(float t) {
 t*=t;
 if(t>=1)return 0;
 const float u=1-t;
 return u*u*u;
}
struct Swell {
 float amount=0,center=0,width=1;
 float at(float position) const {
  if(amount==0)return 0;
  return amount*bell((position-center)/width);
 }
};
// A counter check's reach: the side facing the numeral swells toward it in
// vertical layouts, the lower rim swells down onto it in the paired layout
// (reflow blends the two). `side` is -1 (left) or +1 (right).
struct Swells {
 int side=0;
 Swell edge,rim;
};
// Coverage of pixel column x by the horizontal span [left, right).
inline float spanCoverage(float left,float right,int x) {
 return face_contour::coverage(left,right,x);
}
struct Mask {
 float left[135]={},right[135]={},bottom[240]={};
 // Step-over foot: per row, a span from the outer edge to an opaque body root.
 float stepEdge[135]={},stepRoot[135]={};
 int stepSide=0,stepTop=135,stepBottom=-1;
 float stepCoverage(int x,int y) const {
  if(!stepSide||y<stepTop||y>stepBottom)return 0;
  if(stepSide<0)return spanCoverage(stepEdge[y],stepRoot[y],x);
  return spanCoverage(stepRoot[y],stepEdge[y],x);
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
  if(base==1||!stepSide||x<0||x>=240||y<0||y>=135)return base;
  return fmaxf(base,stepCoverage(x,y));
 }
 float bottomAt(int x) const { return bottom[x]; }
 void rowBounds(int y,float& lo,float& hi) const {
  lo=left[y];hi=right[y];
  if(stepSide<0&&y>=stepTop&&y<=stepBottom&&stepEdge[y]<stepRoot[y])lo=fminf(lo,stepEdge[y]);
  if(stepSide>0&&y>=stepTop&&y<=stepBottom&&stepEdge[y]>stepRoot[y])hi=fmaxf(hi,stepEdge[y]);
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
 Geometry g={120+scene.offset+scene.counterShift+scene.away+px,mix(67.5f,floor-ry,scene.corners)+py,
             rx,ry,mix(2,4,scene.corners),floor};
 // Exact zero keeps every established layout bit-identical.
 if(scene.alert>0) {
  const float wide=scene.body.width*scene.body.bodyWidth/kRequestRestWidth-1;
  const float tall=scene.body.height*scene.body.bodyHeight/kRequestRestHeight-1;
  const float domeX=kRequestDomeRadiusX*(1+kRequestDomeStretchX*wide)*(1+.07f*impact)*
                    (1-.7f*scene.hopStretch);
  const float centre=kRequestDomeY+py-scene.hop;
  float domeY=kRequestDomeRadiusY*(1+kRequestDomeStretchY*tall)*(1-.05f*impact)*(1+scene.hopStretch);
  // Soft constraint: the top never cuts through the eyes; it stretches up.
  domeY=smoothMax(domeY,centre-(eyeCenterY(scene)-renderedEyeHeight(scene)/2-kRequestEyeHeadroom),6);
  g.cx=mix(g.cx,kRequestDomeX+scene.away+px,scene.alert);
  g.cy=mix(g.cy,centre,scene.alert);
  g.rx=mix(g.rx,domeX,scene.alert);g.ry=mix(g.ry,domeY,scene.alert);
  g.power=mix(g.power,2,scene.alert);
 }
 return g;
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
// Rigid half width of the body at row centre `at` (no membrane, flow or
// swell): what a reach measures its distance from. Negative outside the body.
inline float rigidHalf(const Scene& scene,const Geometry& g,float soft,float at) {
 float ny=(at-g.cy)/g.ry;
 // The lower spring rim, not a rigid ellipse cap, decides which pixels exist
 // around the support. This permits free overhangs below the contact plane.
 if(ny>.85f)ny=mix(ny,.85f,scene.corners);
 if(fabsf(ny)>=1)return -1;
 float half=g.rx*profile(fabsf(ny),g.power);
 // A standing blob settles: narrower shoulders and a heavier lower belly,
 // so the side reads as a drop rather than a rigid parenthesis.
 if(soft!=0)half*=1+.11f*soft*(ny+.35f*(1-ny*ny)-.35f);
 return half;
}
// Rigid lower envelope at column centre `at`, before flaps, drift and
// swells. `widen`/`stretch` are buildMask's; -1000 outside the body.
inline float rigidBottom(const Geometry& g,float widen,float stretch,float at) {
 float nx=fabsf((at-g.cx)/g.rx);
 if(widen!=1)nx/=widen;
 // Use the same one-sided stretch for the lower envelope; extending only
 // row bounds would leave a curved blue sliver clipped by the old oval bottom.
 if(stretch*(at-g.cx)>0)nx/=1+fabsf(stretch);
 if(nx>=1)return -1000;
 return g.cy+g.ry*profile(nx,g.power);
}
// A counter check swells the body toward its numeral: the facing side in a
// vertical layout, the lower rim in the paired layout, blended through reflow.
// The reach is measured from the rigid outline, so the membrane's own motion
// never feeds back into it, and a fixed width per check lets the bell grow in
// place instead of sliding. Zero strength is exactly no swell.
// The bell widens with the reach: a narrow bell added at the oval's widest
// point concentrates the bend at the numeral and leaves straight flanks.
constexpr float kSwellBase=40,kSwellGrowth=5;
// The paired rim meets the numeral on its rising flank, not at its extreme,
// so a narrower bell already reads as a soft swelling there.
constexpr float kRimSwellBase=30,kRimSwellGrowth=3;
inline Swells pokeSwells(const Scene& scene,const Geometry& g,float soft,float widen,float stretch) {
 Swells out;
 const auto& p=scene.poke;
 const float strength=fmaxf(0,p.strength)*(1-scene.alert)*(1-scene.sleep);
 if(strength==0||p.side==0)return out;
 out.side=p.side<0?-1:1;
 const float narrow=mix(1,.9f,p.kiss);  // an affectionate check is a little more pointed
 const float lateral=strength*(1-scene.corners),vertical=strength*scene.corners;
 if(lateral>0) {
  const float half=rigidHalf(scene,g,soft,p.cy);
  if(half>0) {
   const float edge=g.cx+out.side*half;
   const float reach=face_physics::clamp(out.side*(p.cx-out.side*p.width/2-edge)+4,4,18);
   out.edge.amount=reach*lateral;out.edge.center=p.cy;
   out.edge.width=(kSwellBase+kSwellGrowth*reach)*narrow;
  }
 }
 if(vertical>0) {
  const float rim=rigidBottom(g,widen,stretch,p.cx);
  if(rim>-1000) {
   const float lower=face_physics::clamp(p.cy-p.height/2-rim+4,4,14);
   out.rim.amount=lower*vertical;out.rim.center=p.cx;
   out.rim.width=(kRimSwellBase+kRimSwellGrowth*lower)*narrow;
  }
 }
 return out;
}
inline Swells pokeSwells(const Scene& scene) {
 const float soft=softSides(scene);
 return pokeSwells(scene,geometry(scene),soft,1+.4f*soft,unusedSideStretch(scene));
}
// `soft` and `stretch` are softSides(scene) and unusedSideStretch(scene), the
// same for every row; `swells` is pokeSwells for the same scene.
inline bool sideBounds(const Scene& scene,const Geometry& g,const fluid_face::Frame& fluid,
                       float soft,float stretch,const Swells& swells,int y,float& left,float& right) {
 left=right=0;
 const float half=rigidHalf(scene,g,soft,y+.5f);
 if(half<0)return false;
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
 if(swells.side<0)left-=swells.edge.at(y+.5f);
 else if(swells.side>0)right+=swells.edge.at(y+.5f);
 if(stretch>0)right+=(right-g.cx)*stretch;
 else if(stretch<0)left+=(g.cx-left)*stretch;
 left=fmaxf(0,fminf(240,left));right=fmaxf(0,fminf(240,right));
 return right>left;
}
// Compatibility/read handle for tests of the side envelope. The complete face
// mask also includes the freely moving lower rim and may have multiple runs.
inline bool bounds(const Scene& scene,int y,float& left,float& right) {
 const Geometry g=geometry(scene);
 const float soft=softSides(scene),stretch=unusedSideStretch(scene);
 return sideBounds(scene,g,fluid_face::Frame(scene.flowClock),soft,stretch,
                   pokeSwells(scene,g,soft,1+.4f*soft,stretch),y,left,right);
}
// Step-over foot. The front is a rounded nose (lower body first, rising a
// little mid-stride) that sweeps out and plants past the screen edge; upper
// rows follow. Every row attaches to opaque BASE coverage; it is unioned with
// the base, and never moves the eyes' clipping body.
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
// Sorting blobs grow out of the lower rim as pseudopods, not pegs. The rim
// around each one sags with it (a bell of kLobeSag of its drop, wider as it
// gets longer). The blob has a round tip as wide as the numeral plus a margin;
// below the flank angle the tip circle hands over to a concave skirt that
// leaves along the circle's tangent and meets the rim tangentially, so a long
// blob widens into the body instead of hanging from it on parallel walls.
// Everything is measured from the local rim and only lowers existing body
// columns, so a blob can never float or detach.
constexpr float kLobeSag=.25f,kLobeFillet=7;
// Where the tip circle hands over to the skirt: 64 degrees from straight
// down (sin, cos, tan), and the skirt's width per pixel of its height.
constexpr float kLobeFlareSin=.8988f,kLobeFlareCos=.4384f,kLobeFlareTan=2.0503f;
constexpr float kLobeSkirt=2.2f/kLobeFlareTan;
inline Swell lobeSag(const Scene& scene,const Geometry& g,float widen,float stretch,int i) {
 Swell sag;
 const float reach=fminf(1,scene.hands.reach[i]),radius=scene.hands.halfWidth[i];
 if(reach<=0||radius<=0)return sag;
 const float rim=rigidBottom(g,widen,stretch,scene.hands.x[i]);
 if(rim<=-1000)return sag;
 const float drop=fmaxf(0,scene.hands.y[i]-rim)*eye_acting::smooth(reach);
 sag.amount=kLobeSag*drop;sag.center=scene.hands.x[i];sag.width=2*radius+drop;
 return sag;
}
// How far below the local rim the blob reaches at distance d from its axis.
// `drop` is the tip's depth below the rim at the axis; negative while the tip
// is still inside the body. Beyond the skirt it keeps falling away, so the
// small emergence fillet below always closes inside the blob's window.
inline float lobeDepth(float d,float drop,float radius) {
 const float shoulder=radius*kLobeFlareSin;
 const float joint=drop-radius*(1-kLobeFlareCos);  // depth where the skirt starts
 if(d<shoulder)return drop-radius+sqrtf(radius*radius-d*d);
 if(joint<=0)return joint-(d-shoulder)*kLobeFlareTan;
 // Cubic Hermite from (joint, circle tangent) to (0, flat): concave, no
 // overshoot (|end slope| stays below 3 in unit coordinates).
 const float width=kLobeSkirt*joint,along=d-shoulder,t=along/width;
 // Past the skirt: flat at first, approaching the no-skirt branch's line as
 // the skirt shrinks to nothing, so the shape is continuous as it appears.
 if(t>=1)return -kLobeFlareTan*(along-width)*(along-width)/along;
 const float slope=-kLobeFlareTan*width/joint;
 return joint*((2*t-3)*t*t+1+slope*((t-2)*t+1)*t);
}
inline void lobeMask(const Scene& scene,Mask& mask) {
 for(int i=0;i<2;++i) {
  const float reach=fminf(1,scene.hands.reach[i]),radius=scene.hands.halfWidth[i];
  if(reach<=0||radius<=0)continue;
  const float cx=scene.hands.x[i];
  const int center=static_cast<int>(face_physics::clamp(cx,0,239));
  if(mask.bottom[center]<0)continue;
  // Grow out of the rim: the circular tip starts hidden inside the body.
  const float rim=mask.bottom[center];
  const float drop=mix(-radius,scene.hands.y[i]-rim,eye_acting::smooth(reach));
  // A small fillet softens the crease while only the tip cap shows; it
  // fades out as the skirt (which already meets the rim smoothly) takes over.
  const float joint=drop-radius*(1-kLobeFlareCos);
  const float fillet=kLobeFillet*face_physics::clamp(1-joint/radius,0,1);
  const float skirt=kLobeSkirt*fmaxf(0,joint);
  const float extent=radius+skirt+fillet+sqrtf(fillet*(skirt+fillet)/kLobeFlareTan)+1;
  const int first=static_cast<int>(fmaxf(0,floorf(cx-extent)));
  const int last=static_cast<int>(fminf(239,ceilf(cx+extent)));
  for(int x=first;x<=last;++x) {
   if(mask.bottom[x]<0)continue;
   const float depth=lobeDepth(fabsf(x+.5f-cx),drop,radius);
   mask.bottom[x]+=fillet>0?smoothMax(0,depth,fillet):fmaxf(0,depth);
  }
 }
}
// The authored deformations the membranes carry (face_physics::Surface::
// follow): the check swells and the sorting sags, sampled at the nodes.
inline void membraneShape(const Scene& scene,face_physics::Shape& shape) {
 shape=face_physics::Shape{};
 const Geometry g=geometry(scene);
 const float soft=softSides(scene),stretch=unusedSideStretch(scene),widen=1+.4f*soft;
 const Swells swells=pokeSwells(scene,g,soft,widen,stretch);
 if(swells.side)for(int i=0;i<face_physics::kEdgeNodes;++i)
  shape.side[swells.side>0][i]=swells.edge.at(face_physics::edgeRow(i));
 const Swell sags[2]={lobeSag(scene,g,widen,stretch,0),lobeSag(scene,g,widen,stretch,1)};
 for(int i=0;i<face_physics::kRimNodes;++i) {
  const float at=face_physics::rimX(i)+.5f;
  shape.rim[i]=swells.rim.at(at)+sags[0].at(at)+sags[1].at(at);
 }
}
inline void buildMask(const Scene& scene,Mask& mask) {
 const Geometry g=geometry(scene);
 const float stretch=unusedSideStretch(scene);
 // Soft sides may bulge past the rigid oval; keep the lower envelope out of
 // their way, or the two curves meet at a visible corner. Zero when paired.
 const float soft=softSides(scene);
 const float widen=1+.4f*soft;
 const fluid_face::Frame fluid(scene.flowClock);
 const Swells swells=pokeSwells(scene,g,soft,widen,stretch);
 for(int y=0;y<135;++y)sideBounds(scene,g,fluid,soft,stretch,swells,y,mask.left[y],mask.right[y]);
 const Swell sags[2]={lobeSag(scene,g,widen,stretch,0),lobeSag(scene,g,widen,stretch,1)};
 for(int x=0;x<240;++x) {
  const float base=rigidBottom(g,widen,stretch,x+.5f);
  if(base<=-1000){mask.bottom[x]=-1000;continue;}
  const float flap=scene.surface?scene.surface->sample(x/239.0f):0;
  // Settled vertical layouts have no lower-rim drift; do not evaluate unused
  // noise. No epsilon/threshold gate that could make the reflow silhouette pop.
  const float drift=scene.corners==0?0:
                    1.4f*fluid_face::field(x,g.floor,fluid)*scene.corners;
  mask.bottom[x]=base+(flap+drift)*(1-.8f*scene.sleep)+
                 swells.rim.at(x+.5f)+sags[0].at(x+.5f)+sags[1].at(x+.5f);
 }
 lobeMask(scene,mask);
 stepMask(scene,mask);
}
// Visual-only height trim: leave body/mouth anchors and authored clocks intact.
// Preserve the engaged request floor and fade back to original sleep-line size.
constexpr float kEyeHeightScale=.81f;
// Requests keep large open eyes: the authored request rest pose (1.15 of the
// 44px request height) renders at 48px. The scale, not a hard floor, carries
// the request squash/stretch acting (about 37..52px) instead of flattening it.
constexpr float kRequestEyeHeight=48,kRequestAwakeHeight=44;  // kRequestRestHeight: above
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
 listen_wave::bars(scene.eyeX+scene.counterShift+scene.away,eyeCenterY(scene),scene.eyeWidth*scene.counterScale,
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
 eye_raster::render(scene.eyeX+scene.counterShift+scene.away,eyeCenterY(scene),scene.eyeWidth*scene.counterScale,
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
 eye_raster::render(scene.eyeX+scene.counterShift+scene.away,eyeCenterY(scene),scene.eyeWidth*scene.counterScale,
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
