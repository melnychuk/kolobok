#pragma once
#include <math.h>
#include "eye_acting.h"

namespace eye_raster {
// All bounds/geometry supplied by acting are finite. Avoid per-pixel libm
// calls on Xtensa; a NaN value still selects hi as the original clamp did.
inline float clamp(float v,float lo,float hi) { return v<lo?lo:(v<hi?v:hi); }
inline float roundedDistance(float x,float y,float halfW,float halfH,float radius) {
 const float qx=fabsf(x)-halfW+radius,qy=fabsf(y)-halfH+radius;
 const float ax=qx>0?qx:0,ay=qy>0?qy:0;
 const float inner=qx>qy?qx:qy;
 // Exact zero-distance shortcut, not an approximate sqrt or changed contour.
 const float outer=(ax==0 && ay==0)?0:sqrtf(ax*ax+ay*ay);
 return outer+(inner<0?inner:0)-radius;
}
// Callback receives pixel coverage, not a color. Native preview and hardware
// therefore use exactly the same eye/lid/cheek geometry and antialiasing.
// Optional blink shape: a closing lid sinks the eye toward its lower edge
// (`blinkSink` of the lost height) and widens it slightly (`blinkWiden` at
// full closure). Happy arches keep their place. Zero is the exact old path.
struct NoSpans { void operator()(int,int,int) const {} };
// Conservative distance margin for interior spans: analytically every claimed
// pixel has distance <= -.5-margin, so the float evaluation (error ~1e-5px) is
// guaranteed to give alpha exactly 1. Pixels outside the span keep the exact
// per-pixel path, so expanding spans in place reproduces the original
// callback sequence bit for bit.
constexpr float kSpanMargin=.02f;
// One row of a rotated eye, along dx = x+.5-ex at row offset dy: the interval
// of dx where every pixel is certain to have alpha exactly 1 (all distances
// <= -.5-kSpanMargin). The eye's own coordinates are linear in dx:
// px = dx*c+dy*s, py = -dx*s+dy*c (the per-pixel path's rotation).
// - Rounded box: distance <= T is the core rectangle (half sizes w/2-r,
//   h/2-r) dilated by r+T, the union of two slabs and four corner disks. That
//   union is convex, so its chord is the hull of the parts' chords.
// - Lid: convex quadratic in dx (nx = px/(w/2), unclamped inside the box).
// - Cheek and lower lid: below py-base, as the unrotated spans use them.
// Endpoints are solved in float; every constraint changes by at most a few
// times the endpoint error per pixel of dx, far inside the margin.
inline bool chord(float k,float m,float bound,float& lo,float& hi) {
 if(k==0) { if(fabsf(m)>bound)return false;lo=-1e30f;hi=1e30f;return true; }
 const float a=(-bound-m)/k,b=(bound-m)/k;
 lo=a<b?a:b;hi=a<b?b:a;return true;
}
// a*dx^2+b*dx+q <= 0 with a >= 0 (stable roots).
inline bool below(float a,float b,float q,float& lo,float& hi) {
 if(a==0) {
  if(b==0) { if(q>0)return false;lo=-1e30f;hi=1e30f;return true; }
  if(b>0) { lo=-1e30f;hi=-q/b; } else { lo=-q/b;hi=1e30f; }
  return true;
 }
 const float disc=b*b-4*a*q;
 if(disc<0)return false;
 const float root=sqrtf(disc),t=-.5f*(b+(b<0?-root:root));
 float r1=t/a,r2=t!=0?q/t:r1;
 if(r1>r2) { const float swap=r1;r1=r2;r2=swap; }
 lo=r1;hi=r2;return true;
}
inline bool rotatedSpan(float dy,float c,float s,float w,float h,float radius,int side,
                        float lidBase,float lidSlope,float lidCurve,bool hasUpper,
                        bool cheek,float cheekBase,bool lowerLid,float lowerBase,float& lo,float& hi) {
 const float target=-.5f-kSpanMargin,reach=radius+target;
 if(reach<=0)return false;
 const float ax=w/2-radius,ay=h/2-radius;
 // px = c*dx+s*dy, py = -s*dx+c*dy.
 const float pm=s*dy,qm=c*dy;
 bool any=false;
 lo=1e30f;hi=-1e30f;
 const auto join=[&](bool ok,float a,float b) {
  if(!ok)return;
  if(a>b)return;
  any=true;lo=a<lo?a:lo;hi=b>hi?b:hi;
 };
 float a1=0,b1=0,a2=0,b2=0;
 // Slab parts: |px|<=ax+reach with |py|<=ay, and |px|<=ax with |py|<=ay+reach.
 bool ok=chord(c,pm,ax+reach,a1,b1)&&chord(-s,qm,ay,a2,b2);
 join(ok,a1>a2?a1:a2,b1<b2?b1:b2);
 ok=chord(c,pm,ax,a1,b1)&&chord(-s,qm,ay+reach,a2,b2);
 join(ok,a1>a2?a1:a2,b1<b2?b1:b2);
 // Corner disks; the row direction (c,-s) has unit length.
 for(int corner=0;corner<4;++corner) {
  const float dxc=pm-(corner&1?ax:-ax),dyc=qm-(corner&2?ay:-ay);
  const float along=c*dxc-s*dyc,rest=dxc*dxc+dyc*dyc-reach*reach;
  // below() writes a1/b1: call it before join() reads them.
  const bool hit=below(1,2*along,rest,a1,b1);
  join(hit,a1,b1);
 }
 if(!any)return false;
 // Lid: lidCurve*nx^2 + sign*lidSlope*nx + lidBase - py <= target.
 if(hasUpper) {
  const float half=w/2,na=c/half,nb=pm/half,sign=side?-1.0f:1.0f;
  if(!below(lidCurve*na*na,2*lidCurve*na*nb+sign*lidSlope*na+s,
            lidCurve*nb*nb+sign*lidSlope*nb+lidBase-qm-target,a1,b1))return false;
  lo=lo>a1?lo:a1;hi=hi<b1?hi:b1;
 }
 // py - base <= target, linear in dx.
 if(cheek) {
  if(!below(0,-s,qm-cheekBase-target,a1,b1))return false;
  lo=lo>a1?lo:a1;hi=hi<b1?hi:b1;
 }
 if(lowerLid) {
  if(!below(0,-s,qm-lowerBase-target,a1,b1))return false;
  lo=lo>a1?lo:a1;hi=hi<b1?hi:b1;
 }
 return hi>lo;
}
template<class Pixel,class Span>
void renderSpans(float cx,float cy,float width,float height,float gap,
                 const eye_acting::Pose& pose,Pixel pixel,Span span,bool spans,
                 float blinkSink,float blinkWiden);
template<class Pixel>
void render(float cx,float cy,float width,float height,float gap,
            const eye_acting::Pose& pose,Pixel pixel,float blinkSink=0,float blinkWiden=0) {
 renderSpans(cx,cy,width,height,gap,pose,pixel,NoSpans(),false,blinkSink,blinkWiden);
}
// Interior runs (alpha exactly 1) are reported as span(x,y,length) instead of
// per-pixel callbacks, at their position in the row order. Unrotated eyes only.
template<class Pixel,class Span>
void render(float cx,float cy,float width,float height,float gap,
            const eye_acting::Pose& pose,Pixel pixel,Span span,float blinkSink,float blinkWiden) {
 renderSpans(cx,cy,width,height,gap,pose,pixel,span,true,blinkSink,blinkWiden);
}
template<class Pixel,class Span>
void renderSpans(float cx,float cy,float width,float height,float gap,
                 const eye_acting::Pose& pose,Pixel pixel,Span span,bool spans,
                 float blinkSink,float blinkWiden) {
 const float angle=pose.tilt*.01745329252f,c=cosf(angle),s=sinf(angle);
 for(int side=0;side<2;++side) {
  const float closure=clamp(side?pose.closeR:pose.closeL,0,1);
  const float upper=(side?pose.upperR:pose.upperL)*(1-closure);
  const float lower=clamp(side?pose.lowerR:pose.lowerL,0,1)*(1-closure);
  const float slant=clamp(side?pose.slantR:pose.slantL,-.5f,.5f)*(1-closure);
  const bool hasUpper=upper>0||slant!=0;
  const float scale=clamp(1+(side?pose.scaleR:pose.scaleL),.75f,1.25f);
  const float eyeHeight=scale==1?height:height*scale;
  const float smile=pose.smile*(1-closure);
  const float plain=1-clamp(pose.smile*2,0,1);  // Plain (not happy) closure.
  const float originalWidth=blinkWiden==0?width:width*(1+blinkWiden*closure*plain);
  const float w=scale==1?originalWidth:originalWidth*scale,h=fmaxf(3,eyeHeight*(1-closure));
  const float offset=(side?1:-1)*(width+gap)/2;
  // Sink along the eye's own vertical axis; fade it out for happy arches.
  const float sink=blinkSink==0?0:(eyeHeight-h)*blinkSink*plain;
  const float ex=cx+offset*c-sink*s,ey=cy+offset*s+sink*c;
  const float radius=fminf(fminf(w,h)*.5f, fminf(w,h)*.24f+2);
  const int extent=static_cast<int>(ceilf(sqrtf(w*w+h*h)/2+2));
  const float asymmetry=fabsf(pose.upperL-pose.upperR)>.03f?1:0;
  const float happyClosed=clamp((closure-.35f)/.65f,0,1)*clamp(pose.smile*2,0,1);
  // Max(lid/cheek, box) never expands the box. A happy closure blends with
  // an arch, so include that arch's full height before rotating the union.
  const float halfW=w/2+.5f;
  const float archH=eyeHeight*.06f+2.5f;
  const float halfH=happyClosed>0?fmaxf(h/2+.5f,archH+sink):h/2+.5f;
  const float bx=fabsf(c)*halfW+fabsf(s)*halfH+1;
  const float by=fabsf(s)*halfW+fabsf(c)*halfH+1;
  const int oldX0=static_cast<int>(floorf(ex))-extent,oldX1=static_cast<int>(ceilf(ex))+extent;
  const int oldY0=static_cast<int>(floorf(ey))-extent,oldY1=static_cast<int>(ceilf(ey))+extent;
  const int tightX0=static_cast<int>(floorf(ex-bx)),tightX1=static_cast<int>(ceilf(ex+bx));
  const int tightY0=static_cast<int>(floorf(ey-by)),tightY1=static_cast<int>(ceilf(ey+by));
  const int x0=tightX0>oldX0?tightX0:oldX0,x1=tightX1<oldX1?tightX1:oldX1;
  const int y0=tightY0>oldY0?tightY0:oldY0,y1=tightY1<oldY1?tightY1:oldY1;
  // Preserve arithmetic order, but compute invariant lid/cheek products once.
  const bool normalized=hasUpper || lower>0 || smile>0 || happyClosed>0;
  const float lidBase=-h/2+upper*h*.70f;
  float lidSlope=asymmetry*upper*h*.12f;
  if(slant!=0)lidSlope+=slant*h*.30f;
  const float lidCurve=upper*h*.07f;
  const float cheekBase=h/2-smile*h*.78f,cheekCurve=smile*h*.32f;
  const float lowerBase=h/2-lower*h*.45f,lowerCurve=lower*h*.10f;
  // At EXACTLY zero rotation, normalized x is identical for every row.
  // Cache the original division (not a reciprocal) in bounded stack storage.
  // Oversized/rotated eyes keep the scalar path; no persistent pose cache.
  constexpr int columnCapacity=128;
  float columns[columnCapacity];
  const bool cachedX=normalized && c==1 && s==0 && x1-x0<columnCapacity;
  if(cachedX)for(int x=x0;x<=x1;++x)columns[x-x0]=clamp((x+.5f-ex)/(w/2),-1,1);
  const bool interiorSpans=spans && c==1 && s==0 && happyClosed==0;
  // Rotated eyes get interior spans too (rotatedSpan below), with the same
  // margin and the same exact per-pixel path for everything outside them.
  const bool rotatedSpans=spans && !(c==1 && s==0) && happyClosed==0;
  for(int y=y0;y<=y1;++y) {
   int spanFirst=x1+1,spanLast=x1;
   if(rotatedSpans) {
    float lo,hi;
    if(rotatedSpan(y+.5f-ey,c,s,w,h,radius,side,hasUpper?lidBase:0,hasUpper?lidSlope:0,
                   hasUpper?lidCurve:0,hasUpper,smile>0,cheekBase,lower>0,lowerBase,lo,hi)) {
     spanFirst=static_cast<int>(ceilf(lo+ex-.5f));spanLast=static_cast<int>(floorf(hi+ex-.5f));
     if(spanFirst<x0)spanFirst=x0;
     if(spanLast>x1)spanLast=x1;
     if(spanLast<spanFirst){spanFirst=x1+1;spanLast=x1;}
    }
   }
   if(interiorSpans) {
    // Rounded box: distance<=T <=> |px| <= hw-r+sqrt((T+r)^2-max(qy,0)^2).
    const float target=-.5f-kSpanMargin,reach=target+radius;
    const float py=y+.5f-ey,qy=fabsf(py)-h/2+radius;
    if(reach>0 && qy<=reach) {
     const float positive=qy>0?qy:0;
     const float half=w/2-radius+sqrtf(reach*reach-positive*positive);
     float lo=-half,hi=half;
     bool ok=half>0;
     // Lid is convex in nx: keep the interval between its roots of lid=T.
     if(ok && hasUpper) {
      const float qa=lidCurve,qb=lidSlope*(side?-1:1),qc=lidBase-py-target;
      if(qa>0) {
       const float disc=qb*qb-4*qa*qc;
       if(disc<0) ok=false;
       else {
        const float root=sqrtf(disc);
        lo=fmaxf(lo,(-qb-root)/(2*qa)*(w/2));hi=fminf(hi,(-qb+root)/(2*qa)*(w/2));
       }
      } else if(qb>0) hi=fminf(hi,-qc/qb*(w/2));
      else if(qb<0) lo=fmaxf(lo,-qc/qb*(w/2));
      else if(qc>0) ok=false;
     }
     // Cheek <= py-cheekBase everywhere; otherwise leave the row per-pixel.
     if(ok && smile>0 && py-cheekBase>target) ok=false;
     if(ok && lower>0 && py-lowerBase>target) ok=false;
     if(ok && hi>lo) {
      spanFirst=static_cast<int>(ceilf(lo+ex-.5f));spanLast=static_cast<int>(floorf(hi+ex-.5f));
      if(spanFirst<x0)spanFirst=x0;
      if(spanLast>x1)spanLast=x1;
      if(spanLast<spanFirst){spanFirst=x1+1;spanLast=x1;}
     }
    }
   }
   for(int x=x0;x<=x1;++x) {
    if(x==spanFirst) { span(x,y,spanLast-spanFirst+1);x=spanLast;continue; }
    const float dx=x+.5f-ex,dy=y+.5f-ey;
    const float px=dx*c+dy*s,py=-dx*s+dy*c;
    const float nx=normalized?(cachedX?columns[x-x0]:clamp(px/(w/2),-1,1)):0;
    const float lid=hasUpper?lidBase+lidSlope*(side?-nx:nx)+lidCurve*nx*nx-py:0;
    const float cheek=smile>0?py-(cheekBase+cheekCurve*nx*nx):0;
    const float lowerLid=lower>0?py-(lowerBase+lowerCurve*nx*nx):0;
    // Max(lid/cheek, box) is already transparent here. Happy arches blend
    // distance instead of intersecting, so they MUST NOT take this shortcut.
    if(happyClosed==0 && ((hasUpper && lid>=.5f) || (smile>0 && cheek>=.5f) || (lower>0 && lowerLid>=.5f)))continue;
    float distance=roundedDistance(px,py,w/2,h/2,radius);
    if(hasUpper && lid>distance)distance=lid;
    if(smile>0 && cheek>distance)distance=cheek;
    if(lower>0 && lowerLid>distance)distance=lowerLid;
    // Closed happy eyes stay curved (a 4px arch), not a sleepy flat pill.
    if(happyClosed>0) {
     const float arcY=eyeHeight*(.12f*nx*nx-.06f)-sink;
     const float a=fabsf(py-arcY)-2,b=fabsf(px)-w/2+2;
     const float arcDistance=a>b?a:b;
     distance=distance+(arcDistance-distance)*happyClosed;
    }
    const float alpha=clamp(.5f-distance,0,1);
    if(alpha>0) pixel(x,y,alpha);
   }
  }
 }
}
template<class Pixel>
void box(float cx,float cy,float w,float h,float radius,Pixel pixel) {
 for(int y=static_cast<int>(floorf(cy-h/2-1));y<=static_cast<int>(ceilf(cy+h/2+1));++y)
  for(int x=static_cast<int>(floorf(cx-w/2-1));x<=static_cast<int>(ceilf(cx+w/2+1));++x) {
   const float a=clamp(.5f-roundedDistance(x+.5f-cx,y+.5f-cy,w/2,h/2,radius),0,1);
   if(a>0) pixel(x,y,a);
  }
}
} // namespace eye_raster
