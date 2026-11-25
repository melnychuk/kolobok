#pragma once
#include <math.h>
#include "../firmware/eye_acting.h"
// Frozen pre-optimization geometry oracle. No hardware or timing claims.
// Shapes follow the current happy eyes (shallow rounded cheek, relaxed lid,
// tapered crease); per-eye scale/lower lids/slant are not modelled.

namespace reference_eye_raster {
inline float clamp(float v,float lo,float hi) { return fmaxf(lo,fminf(hi,v)); }
inline float roundedDistance(float x,float y,float halfW,float halfH,float radius) {
 const float qx=fabsf(x)-halfW+radius,qy=fabsf(y)-halfH+radius;
 return sqrtf(fmaxf(0,qx)*fmaxf(0,qx)+fmaxf(0,qy)*fmaxf(0,qy))+
        fminf(fmaxf(qx,qy),0)-radius;
}
// Callback receives pixel coverage, not a color. Native preview and hardware
// therefore use exactly the same eye/lid/cheek geometry and antialiasing.
template<class Pixel>
void render(float cx,float cy,float width,float height,float gap,
            const eye_acting::Pose& pose,Pixel pixel) {
 const float angle=pose.tilt*.01745329252f,c=cosf(angle),s=sinf(angle);
 for(int side=0;side<2;++side) {
  const float closure=clamp(side?pose.closeR:pose.closeL,0,1);
  const float upper=((side?pose.upperR:pose.upperL)+clamp(pose.smile,0,1)*.10f)*(1-closure);
  const float smile=pose.smile*(1-closure);
  const float w=width,h=fmaxf(3,height*(1-closure));
  const float offset=(side?1:-1)*(width+gap)/2;
  const float ex=cx+offset*c,ey=cy+offset*s;
  const float radius=fminf(fminf(w,h)*.5f, fminf(w,h)*.24f+2);
  const int extent=static_cast<int>(ceilf(sqrtf(w*w+h*h)/2+2));
  for(int y=static_cast<int>(floorf(ey))-extent;y<=static_cast<int>(ceilf(ey))+extent;++y) {
   for(int x=static_cast<int>(floorf(ex))-extent;x<=static_cast<int>(ceilf(ex))+extent;++x) {
    const float dx=x+.5f-ex,dy=y+.5f-ey;
    const float px=dx*c+dy*s,py=-dx*s+dy*c;
    float distance=roundedDistance(px,py,w/2,h/2,radius);
    const float nx=clamp(px/(w/2),-1,1);
    if(upper>0) {
     const float asymmetry=fabsf(pose.upperL-pose.upperR)>.03f?1:0;
     const float lid=-h/2+upper*h*.70f+asymmetry*upper*h*.12f*(side?-nx:nx)+upper*h*.07f*nx*nx;
     distance=fmaxf(distance,lid-py);
    }
    if(smile>0) {
     const float cheek=py-(h/2-smile*h*.56f+smile*h*.20f*nx*nx);
     // Rounded cheek corners: polynomial smooth max of width cheekSoft.
     const float cheekSoft=fminf(5.0f,smile*12),gapD=fabsf(cheek-distance);
     if(gapD<cheekSoft) {
      const float k=(cheekSoft-gapD)/cheekSoft;
      distance=(cheek>distance?cheek:distance)+cheekSoft*.25f*k*k;
     } else if(cheek>distance)distance=cheek;
    }
    // Closed happy eyes: a tapered crease seated a little low in the eye.
    const float happyClosed=clamp((closure-.35f)/.65f,0,1)*clamp(pose.smile*2,0,1);
    if(happyClosed>0) {
     const float bend=1-nx*nx;
     const float arcY=height*(.13f-.08f*bend);
     const float a=fabsf(py-arcY)-(.55f+height*.045f*bend),b=fabsf(px)-w/2+1.5f;
     const float arcDistance=fmaxf(a,b);
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
template<class Pixel>
void attentionMark(float cx,float seconds,Pixel pixel) {
 const float bob=sinf(seconds*3.4906585f)*1.6f;
 box(cx,10+bob,5,13,2,pixel);
 box(cx,23+bob,4.5f,4.5f,2.25f,pixel);
}
} // namespace reference_eye_raster
