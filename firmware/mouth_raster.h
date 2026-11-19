#pragma once
#include <math.h>
#include "eye_acting.h"

// A small supporting expression, not a speech/lip-sync oscillator. The caller
// supplies the delayed acting pose; line, smile and open oval share one contour.
namespace mouth_raster {
// Finite contour inputs: avoid repeated libm clamps in the pixel/segment loop.
inline float clamp(float t) { return t<0?0:(t<1?t:1); }
struct Shape { float halfWidth,curve,opening,tilt,skew,purse; };
inline Shape expression(const eye_acting::Pose& pose,bool thinking=false) {
 const float smile=clamp(pose.smile/.75f);
 const float focus=clamp(fmaxf(pose.upperL,pose.upperR));
 const float opening=eye_acting::smooth((pose.height-1.10f)/.14f)*(1-smile);
 const float width=(14+4*smile)*(1-.75f*focus)*fmaxf(.7f,fminf(1.3f,pose.width));
 // A held side look draws one lip corner along with it: restrained thought
 // asymmetry / playful smirk, not autonomous chewing or a periodic wiggle.
 const float intent=fmaxf(-1,fminf(1,pose.x/12+(pose.upperL-pose.upperR)*2));
 const float purse=thinking?(.4f+.6f*eye_acting::smooth(focus/.18f))*(1-opening)*(1-smile):0;
 const float pinched=width+(8-width)*purse;
 return {pinched+(7-pinched)*opening,6*smile+.8f*focus,6*opening,
         pose.tilt+3*intent,1.8f*intent*(1-.4f*smile),purse};
}
struct Point { float x,y; };
struct Segment {
 Point start;
 float dx,dy,length;
 Segment(const Point& a,const Point& b):start(a),dx(b.x-a.x),dy(b.y-a.y),
                                        length(dx*dx+dy*dy) {}
 Segment():start{0,0},dx(0),dy(0),length(0) {}
};
inline float distanceSquared(float x,float y,const Segment& segment) {
 // Retain division and operation order; no approximate reciprocal or sqrt.
 const float t=segment.length>1e-6f?
  clamp(((x-segment.start.x)*segment.dx+(y-segment.start.y)*segment.dy)/segment.length):0;
 const float px=x-segment.start.x-t*segment.dx,py=y-segment.start.y-t*segment.dy;
 return px*px+py*py;
}
inline float distanceSquared(float x,float y,const Point& a,const Point& b) {
 return distanceSquared(x,y,Segment(a,b));
}
template<class Pixel>
void render(float cx,float cy,const Shape& shape,float opacity,Pixel pixel) {
 if(opacity<=0)return;
 constexpr int segments=16;
 constexpr float pi=3.14159265358979323846f;
 const float angle=shape.tilt*pi/180,cs=cosf(angle),sn=sinf(angle);
 Point points[segments+1];
 float left=240,right=0,top=135,bottom=0;
 for(int i=0;i<=segments;++i) {
  const float theta=2*pi*i/segments,u=cosf(theta);
  const float x=shape.halfWidth*u;
  const float lips=shape.opening+3*shape.purse*(.5f+.5f*fabsf(u));
  const float y=(shape.curve+shape.skew*u)*(1-u*u)+lips*sinf(theta);
  points[i]={cx+x*cs-y*sn,cy+x*sn+y*cs};
  left=fminf(left,points[i].x);right=fmaxf(right,points[i].x);
  top=fminf(top,points[i].y);bottom=fmaxf(bottom,points[i].y);
 }
 Segment edges[segments];
 for(int i=0;i<segments;++i)edges[i]=Segment(points[i],points[i+1]);
 const float coverage=clamp(opacity);
 const int x0=static_cast<int>(fmaxf(0,floorf(left-3)));
 const int x1=static_cast<int>(fminf(239,ceilf(right+3)));
 const int y0=static_cast<int>(fmaxf(0,floorf(top-3)));
 const int y1=static_cast<int>(fminf(134,ceilf(bottom+3)));
 for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
  float best=1e6f;
  for(int i=0;i<segments;++i) {
   const float distance=distanceSquared(x+.5f,y+.5f,edges[i]);
   if(distance<best)best=distance;
  }
  float alpha=clamp(2.15f-sqrtf(best))*coverage;
  if(shape.purse>0 && alpha>0) {
   const float px=x+.5f-cx,py=y+.5f-cy;
   const float normalized=(px*cs+py*sn)/shape.halfWidth;
   const float u=normalized<-1?-1:(normalized<1?normalized:1);
   const float center=(shape.curve+shape.skew*u)*(1-u*u);
   const float crease=clamp(1-fabsf(-px*sn+py*cs-center))*clamp(1-u*u);
   alpha*=1-.55f*shape.purse*crease;
  }
  if(alpha>0)pixel(x,y,alpha);
 }
}
} // namespace mouth_raster
