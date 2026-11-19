#pragma once
#include <math.h>
#include "eye_acting.h"

// A soft supported body plus a lightly damped spring-chain rim. Local impulses
// propagate as wave packets; there is no sinusoidal contour animation.
namespace face_physics {
inline float clamp(float value,float lo,float hi) { return fmaxf(lo,fminf(hi,value)); }
// Side membranes for vertical layouts. Nodes span fixed screen rows from above
// to below the display, so both free ends stay offscreen. Values are horizontal
// deviations from the rigid silhouette: inertial lag, follow-through and
// travelling ripples, never periodic sine bends or random destinations.
constexpr int kEdgeNodes=20;
constexpr float kEdgeTop=-20,kEdgeSpan=175;
inline float edgeRow(int i) { return kEdgeTop+i*kEdgeSpan/(kEdgeNodes-1); }
struct Surface {
 static constexpr int count=32;
 float displacement[count]={},velocity[count]={};
 float x=0,y=0,vx=0,vy=0,impact=0,accumulator=0;
 float lastLean=0,lastLift=0,lastStretch=1;
 // [0] left edge, [1] right edge; screen-x deviation in pixels.
 float edge[2][kEdgeNodes]={},edgeVelocity[2][kEdgeNodes]={};
 float lastWidth=1,lastEdgeStretch=1;
 bool edgePrimed=false;
 void kick(float direction,float strength) {
  // The cued side swells outward near the core and the swell travels away.
  for(int s=0;s<2;++s) {
   const float sign=2.0f*s-1;
   if(direction*sign<0)continue;
   for(int i=0;i<kEdgeNodes;++i) {
    const float distance=(edgeRow(i)-62)/26;
    edgeVelocity[s][i]+=sign*120*strength*expf(-distance*distance*.5f);
   }
  }
  vx+=direction*95*strength;vy-=65*strength;
  float center=15.5f;
  if(direction<0)center=4;
  if(direction>0)center=27;
  for(int i=0;i<count;++i) {
   const float distance=(i-center)/2.8f;
   velocity[i]+=95*strength*expf(-distance*distance*.5f);
  }
 }
 void step(float seconds,const eye_acting::Pose& pose,bool supported) {
  const float stretch=pose.height*pose.bodyHeight;
  const float motion=(pose.lift-lastLift)*-2.5f+(stretch-lastStretch)*-55;
  const float turn=(pose.lean-lastLean)*1.8f;
  for(int i=0;i<count;++i) {
   const float u=i/static_cast<float>(count-1),edge=fabsf(u-.5f)*2;
   velocity[i]+=(motion+turn*(u-.5f))*edge;
  }
  lastLean=pose.lean;lastLift=pose.lift;lastStretch=stretch;
  // Shape changes move the rigid side envelope at once; the membrane keeps
  // part of its old position and catches up with overlap. Nominal vertical
  // geometry is enough for excitation and is frame-rate independent.
  const float width=pose.width*pose.bodyWidth;
  if(!edgePrimed){lastWidth=width;lastEdgeStretch=stretch;edgePrimed=true;}
  const float grow=width-lastWidth,tall=stretch-lastEdgeStretch;
  lastWidth=width;lastEdgeStretch=stretch;
  if(grow!=0||tall!=0)for(int i=0;i<kEdgeNodes;++i) {
   const float ny=clamp((edgeRow(i)-67.5f)/kEdgeRadiusY,-.95f,.95f);
   const float across=sqrtf(1-ny*ny);
   const float change=kEdgeRadiusX*(grow*across+tall*ny*ny/across)*
                      kEdgeShapeDrag*(1-.5f*core(i));
   edge[0][i]+=change;edge[1][i]-=change;
  }
  accumulator+=clamp(seconds,0,.1f);
  constexpr float dt=1.0f/120;
  while(accumulator+0.000001f>=dt) {
   accumulator=fmaxf(0,accumulator-dt);
   // 1.3x translation frequency; scale damping too to retain the same soft
   // overshoot instead of turning quicker travel into extra oscillation.
   constexpr float speed=1.3f;
   vx+=(-22*speed*speed*(x-pose.lean)-4.8f*speed*vx)*dt;
   vy+=(-28*speed*speed*(y-pose.lift)-4.2f*speed*vy)*dt;
   const float oldX=x,oldY=y;
   x+=vx*dt;y+=vy*dt;
   x=clamp(x,-22,22);y=clamp(y,-22,14);
   if(supported&&y>0) {
    impact=fmaxf(impact,clamp(vy/80,0,1));
    y=0;if(vy>0)vy=-vy*.30f;
   }
   stepEdges(dt,x-oldX,y-oldY);
   impact*=expf(-dt/0.20f);
   float acceleration[count];
   for(int i=0;i<count;++i) {
    const float left=displacement[i?i-1:i],right=displacement[i+1<count?i+1:i];
    acceleration[i]=190*(left+right-2*displacement[i])-7*displacement[i]-1.35f*velocity[i];
   }
   for(int i=0;i<count;++i) {
    velocity[i]+=acceleration[i]*dt;
    displacement[i]+=velocity[i]*dt;
    if(fabsf(displacement[i])>18) {
     displacement[i]=clamp(displacement[i],-18,18);velocity[i]*=.25f;
    }
    // Only the few actual contact points rest on the support. Free ends can
    // flap below it and overlap the foreground numbers.
    if(supported&&i>=13&&i<=18&&displacement[i]+y>0) {
     displacement[i]=-y;
     if(velocity[i]>0)velocity[i]*=-.18f;
    }
   }
  }
 }
 // Nominal vertical body used only to excite the side membranes. The core
 // is the body itself, so it only partly lags its own shape changes.
 static constexpr float kEdgeRadiusX=90,kEdgeRadiusY=125,kEdgeDrag=1.0f,kEdgeShapeDrag=.75f;
 static constexpr float kEdgeSoftLimit=10,kEdgeHardLimit=20;
 void stepEdges(float dt,float dx,float dy) {
  float force[kEdgeNodes];
  for(int s=0;s<2;++s) {
   const float sign=2.0f*s-1;
   float* d=edge[s];float* v=edgeVelocity[s];
   for(int i=0;i<kEdgeNodes;++i) {
    // Translation leaves the loose upper/lower flesh behind; a vertical bob
    // slides the curved envelope along the rows and rolls a ripple along it.
    const float ny=clamp((edgeRow(i)-67.5f)/kEdgeRadiusY,-.95f,.95f);
    const float slide=sign*kEdgeRadiusX*ny/(kEdgeRadiusY*sqrtf(1-ny*ny))*dy;
    d[i]-=kEdgeDrag*(dx+slide)*(1-.4f*core(i));
   }
   for(int i=0;i<kEdgeNodes;++i) {
    const int above=i?i-1:i,below=i+1<kEdgeNodes?i+1:i;
    // Strong tension keeps broad, smooth sloshes; viscosity on curvature
    // velocity kills short ripples first, so no kinks or wiggly noise appear.
    // Uniform anchoring: a stiffer core would bend the edge into a tent.
    force[i]=900*(d[above]+d[below]-2*d[i])+6*(v[above]+v[below]-2*v[i])-
             32*d[i]-3.4f*v[i];
   }
   for(int i=0;i<kEdgeNodes;++i) {
    v[i]+=force[i]*dt;d[i]+=v[i]*dt;
    // Safety net only; visible saturation is soft in edgeSample().
    if(fabsf(d[i])>kEdgeHardLimit) {
     d[i]=clamp(d[i],-kEdgeHardLimit,kEdgeHardLimit);v[i]*=.25f;
    }
   }
  }
 }
 static float core(int i) {
  const float distance=(edgeRow(i)-67.5f)/55;
  return expf(-distance*distance*.5f);
 }
 float edgeSample(int side,float row) const {
  const float at=clamp((row-kEdgeTop)/kEdgeSpan,0,1)*(kEdgeNodes-1);
  const int i=static_cast<int>(floorf(at));const float t=at-i;
  const float* d=edge[side?1:0];
  const float a=d[i>0?i-1:i],b=d[i];
  const float c=d[i+1<kEdgeNodes?i+1:i],e=d[i+2<kEdgeNodes?i+2:kEdgeNodes-1];
  const float value=.5f*((2*b)+(-a+c)*t+(2*a-5*b+4*c-e)*t*t+(-a+3*b-3*c+e)*t*t*t);
  // Near-linear for small sloshes; large peaks round off instead of bouncing
  // off a hard wall, and the visible deviation stays below kEdgeSoftLimit.
  return value/sqrtf(1+value*value*(1/(kEdgeSoftLimit*kEdgeSoftLimit)));
 }
 float sample(float u) const {
  const float at=clamp(u,0,1)*(count-1);
  const int i=static_cast<int>(floorf(at));const float t=at-i;
  const float a=displacement[i>0?i-1:i],b=displacement[i];
  const float c=displacement[i+1<count?i+1:i],d=displacement[i+2<count?i+2:count-1];
  // Catmull-Rom makes round, asymmetric cartoon crests rather than a regular
  // oscillating sine curve. Clamp overshoot to keep collision bounds stable.
  const float value=.5f*((2*b)+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t*t*t);
  return clamp(value,-20,20);
 }
};
struct Reaction {
 uint32_t started=0;
 float direction=0;
 bool active=false,completed=false,excited=true;
 void notify(float side,bool finished,uint32_t now,bool arrival=true) {
  direction=side;completed=finished;excited=arrival||finished;started=now;active=true;
 }
 eye_acting::Pose apply(const eye_acting::Pose& pose,uint32_t now,bool requesting) {
  if(requesting){active=false;return pose;}
  if(!active)return pose;
  const uint32_t elapsed=now-started;
  if(elapsed>=1290){active=false;return pose;}
  return sample(pose,elapsed);
 }
 eye_acting::Pose sample(const eye_acting::Pose& pose,uint32_t elapsed) const {
  const float enter=eye_acting::smooth(elapsed/130.0f);
  const float leave=1-eye_acting::smooth(elapsed>850?(elapsed-850)/350.0f:0);
  // Register the changed side, spring upward, then hold an unmistakable happy
  // expression. A curved closure is a joyful beat, never a sleepy flat blink.
  auto look=eye_acting::p(direction*14,excited?-5:7,1.08f,excited?1.18f:1.08f,
                          0,0,excited?.78f:0,0,0,1.04f,1.06f,direction*4,-4,1.1f);
  if(excited && elapsed<240) {
   const float release=eye_acting::smooth(elapsed/240.0f);
   const float movement=eye_acting::travel(elapsed/240.0f);
   look.y=3-8*movement;look.height=.92f+.26f*release;
   look.bodyHeight=.94f+.12f*release;look.lift=2-6*movement;
  }
  if(excited && elapsed>=450&&elapsed<680) {
   const float closure=elapsed<530?eye_acting::smooth((elapsed-450)/80.0f):
                       1-eye_acting::smooth((elapsed-530)/150.0f);
   look.closeL=look.closeR=closure;
  }
  auto result=eye_acting::mix(pose,look,enter*leave);
  const auto movement=eye_acting::mix(pose,look,eye_acting::travel(elapsed/130.0f)*leave);
  result.x=movement.x;result.y=movement.y;
  result.lean=movement.lean;result.lift=movement.lift;
  return result;
 }
 eye_acting::Pose body(const eye_acting::Pose& pose,uint32_t now,bool requesting) const {
  if(!active||requesting||now-started<90)return pose;
  auto result=sample(pose,now-started-90);
  result.closeL=result.closeR=0; // Blinks never collapse the body or flap lips.
  return result;
 }
};
} // namespace face_physics
