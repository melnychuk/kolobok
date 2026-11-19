#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "../firmware/avatar_interaction.h"
#include "../firmware/display_fonts.h"

using namespace avatar_interaction;
const count_raster::Font& fittingFont(const char* text) {
 const count_raster::Font* fonts[]={&kBadgeFont,&kMediumBadgeFont,&kSmallBadgeFont,
                                  &kTinyBadgeFont,&kCompactBadgeFont};
 for(int i=0;i<4;++i)if(count_raster::width(*fonts[i],text)<=56)return *fonts[i];
 return *fonts[4];
}
void checkAttachedPixels(const avatar_scene::Mask& mask) {
 const auto& h=mask.hand;
 assert(h.radius>0);
 const int x0=static_cast<int>(fmaxf(0,floorf(h.left)-1));
 const int x1=static_cast<int>(fminf(239,ceilf(h.right)+1));
 const int y0=static_cast<int>(fmaxf(0,floorf(h.top)-1));
 const int y1=static_cast<int>(fminf(134,ceilf(h.bottom)+1));
 bool seen[240*135]={};int queue[240*135];int begin=0,end=0;
 const int rootX=static_cast<int>(h.ax),rootY=static_cast<int>(h.ay);
 assert(mask.baseCoverage(rootX,rootY)>.99f);
 queue[end++]=rootY*240+rootX;seen[queue[0]]=true;
 while(begin<end) {
  const int index=queue[begin++],x=index%240,y=index/240;
  for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
   const int xx=x+dx,yy=y+dy;
   if(xx<x0||xx>x1||yy<y0||yy>y1)continue;
   const int next=yy*240+xx;
   if(seen[next]||mask.coverage(xx,yy)<=.5f)continue;
   seen[next]=true;queue[end++]=next;
  }
 }
 for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)
  if(mask.hand.coverage(x,y)>.5f)assert(seen[y*240+x]);
}
int main() {
 // Saturated full-label overlaps must still detect a locally advancing edge.
 avatar_scene::Mask mask;
 for(int y=0;y<135;++y){mask.left[y]=10;mask.right[y]=230;}
 for(int x=0;x<240;++x)mask.bottom[x]=130;
 const float sideBefore=contactDepth(mask,24,67.5f,14,23,0,-1);
 const float rimBefore=contactDepth(mask,24,116,14,23);
 for(int y=60;y<76;++y)mask.left[y]-=3;
 for(int x=20;x<28;++x)mask.bottom[x]+=3;
 assert(contactDepth(mask,24,67.5f,14,23,0,-1)>sideBefore);
 assert(contactDepth(mask,24,116,14,23)>rimBefore);
 CounterSpring staticOverlap;staticOverlap.update(.033f,sideBefore,true,-1);
 for(int i=0;i<200;++i)staticOverlap.update(.033f,sideBefore,true,-1);
 assert(staticOverlap.x==0&&staticOverlap.y==0);

 // Local deformation changes only the contacted edge, not a global overlay.
 avatar_scene::Scene scene;scene.corners=0;scene.offset=24;scene.eyeY=67.5f;
 avatar_scene::Mask rest,poked;
 avatar_scene::buildMask(scene,rest);
 scene.poke.side=-1;scene.poke.cx=24;scene.poke.cy=67.5f;
 scene.poke.width=14;scene.poke.height=23;scene.poke.strength=1;
 avatar_scene::buildMask(scene,poked);
 assert(poked.hand.radius>0);
 const int tipX=static_cast<int>(poked.hand.bx-poked.hand.radius+1),tipY=static_cast<int>(poked.hand.by);
 assert(poked.coverage(tipX,tipY)>.99f&&rest.coverage(tipX,tipY)==0);
 assert(poked.hand.radius>=27&&fabsf(poked.hand.bx-poked.hand.ax)<=20);
 assert(memcmp(poked.left,rest.left,sizeof(rest.left))==0);
 assert(memcmp(poked.right,rest.right,sizeof(rest.right))==0);
 assert(memcmp(poked.bottom,rest.bottom,sizeof(rest.bottom))==0);
 assert(contactDepth(poked,24,67.5f,14,23,0,-1)>0);
 // Raster span classification still matches the deformed mask's AA oracle.
 float raster[240*135]={};
 poked.raster([&](int x,int y,int n){for(int j=0;j<n;++j)raster[y*240+x+j]=1;},
              [&](int x,int y,float a){raster[y*240+x]=a;});
 for(int y=0;y<135;++y)for(int x=0;x<240;++x) {
  const float a=poked.coverage(x,y);
  assert(isfinite(a)&&a>=0&&a<=1);
  assert(fabsf(raster[y*240+x]-a)<.0011f);
 }
 // Zero/tiny strength is continuous and keeps the optimized idle path exact.
 scene.poke.strength=0;avatar_scene::buildMask(scene,poked);
 assert(memcmp(&rest,&poked,sizeof(rest))==0);
 scene.poke.strength=1e-8f;avatar_scene::buildMask(scene,poked);
 for(int y=0;y<135;++y)for(int x=0;x<240;++x)
  assert(fabsf(poked.coverage(x,y)-rest.coverage(x,y))<.001f);
 scene.poke.strength=1;scene.alert=1;avatar_scene::buildMask(scene,poked);
 auto without=scene;without.poke.strength=0;avatar_scene::buildMask(without,rest);
 assert(memcmp(&rest,&poked,sizeof(rest))==0);

 // Reflow, asymmetric rim dips and narrow body support must not crop or split
 // off the hand. Flood-fill every visible hand pixel back to its opaque root.
 for(int side=-1;side<=1;side+=2)for(float corners:{0.0f,.05f,.25f,.5f,.75f,.95f,1.0f})
 for(float width:{.75f,1.0f,1.25f}) {
  scene=avatar_scene::Scene{};scene.corners=corners;scene.offset=-side*24*(1-corners);
  scene.body.bodyWidth=width;
  face_physics::Surface surface;scene.surface=&surface;
  for(int i=0;i<face_physics::Surface::count;++i)surface.displacement[i]=(i%5-2)*4;
  scene.poke.side=side;scene.poke.strength=1;
  scene.poke.cx=side<0?24:216;scene.poke.cy=avatar_scene::mix(67.5f,116,corners);
  scene.poke.width=14;scene.poke.height=23;
  avatar_scene::buildMask(scene,mask);checkAttachedPixels(mask);
  assert(mask.hand.radius>=27);
  assert(fabsf(mask.hand.bx-mask.hand.ax)<=20&&fabsf(mask.hand.by-mask.hand.ay)<=16);
  memset(raster,0,sizeof(raster));
  mask.raster([&](int x,int y,int n){for(int j=0;j<n;++j)raster[y*240+x+j]=1;},
              [&](int x,int y,float a){raster[y*240+x]=a;});
  int outside=0;
  for(int y=0;y<135;++y)for(int x=0;x<240;++x) {
   assert(fabsf(raster[y*240+x]-mask.coverage(x,y))<.0011f);
   if(mask.baseCoverage(x,y)==0&&raster[y*240+x]>.99f)++outside;
  }
  assert(outside>0);
 }

 // Every clip, both layouts, both sides and fitting/narrow Roman widths:
 // each authored press must produce actual contact and a visible spring nudge.
 int performances=0;
 float minimumTravel=3;
 for(int paired=0;paired<2;++paired)for(int side=-1;side<=1;side+=2)
 for(int index=0;index<eye_acting::clipCount(side<0?eye_acting::State::done:eye_acting::State::thinking);++index)
 for(const char* text:{"I","III","VIII","LXXXVIII","XCIX+"}) {
  const auto& font=fittingFont(text);
  const float width=count_raster::width(font,text),strip=fmaxf(48,width+16);
  scene=avatar_scene::Scene{};scene.corners=paired;scene.region=240-2*strip;
  scene.offset=paired?0:-side*strip/2;
  face_physics::Surface surface;scene.surface=&surface;
  Checks checks;checks.fixed=true;CounterSpring spring;
  eye_acting::Player actor;actor.state=side<0?eye_acting::State::done:eye_acting::State::thinking;
  actor.index=index;actor.started=side<0?4400:1400;
  float maximum=0,depthMaximum=0;
  for(uint32_t t=0;t<8400;t+=33) {
   checks.update(t,true,&surface,paired||side<0,paired||side>0);
   scene.body=checks.apply(actor.body(t),t,true);
   scene.eyes=checks.apply(actor.eyes(t),t);
   surface.step(.033f,scene.body,paired);scene.flowClock=t*.0006f;
   checks.shape(scene,t,strip/2,240-strip/2,paired?116:67.5f,width,width,
                font.capHeight,font.capHeight);
   avatar_scene::buildMask(scene,mask);
   if(mask.hand.radius>1) {
    assert(fabsf(mask.hand.bx-mask.hand.ax)<=20&&fabsf(mask.hand.by-mask.hand.ay)<=16);
    assert(mask.hand.radius>=1.35f*hypotf(mask.hand.dx,mask.hand.dy));
    assert(mask.baseCoverage(static_cast<int>(mask.hand.ax),static_cast<int>(mask.hand.ay))>.99f);
    // Sample the entire hand's spine: no clipped gap anywhere between root/tip.
    for(int n=0;n<=40;++n) {
     const int x=static_cast<int>(avatar_scene::mix(mask.hand.ax,mask.hand.bx,n/40.0f));
     const int y=static_cast<int>(avatar_scene::mix(mask.hand.ay,mask.hand.by,n/40.0f));
     if(x>=0&&x<240&&y>=0&&y<135)assert(mask.coverage(x,y)>.5f);
    }
   }
   const float depth=contactDepth(mask,side<0?strip/2:240-strip/2,paired?116:67.5f,
                                  width,font.capHeight,paired,side);
   spring.update(.033f,depth,true,side);
   assert(isfinite(spring.x)&&fabsf(spring.x)<=3&&spring.y>=-2.5f&&spring.y<=3);
   const uint32_t begin=side<0?4800:1800,end=side<0?6000:3000;
   if(t>=begin&&t<end) {
    maximum=fmaxf(maximum,paired?spring.y:side*spring.x);
    depthMaximum=fmaxf(depthMaximum,depth);
   }
  }
  if(maximum<=1.0f||depthMaximum<=0)
   fprintf(stderr,"Missed poke paired=%d side=%d clip=%d label=%s motion=%f depth=%f\n",
           paired,side,index,text,maximum,depthMaximum);
  assert(maximum>1.0f&&depthMaximum>0);++performances;
  minimumTravel=fminf(minimumTravel,maximum);
 }
 printf("Actual pokes with visible counter travel: %d (minimum %.2f px)\n",performances,minimumTravel);

 // Authored kiss is occasional, completed-only, eye-led and mouth-clipped.
 Checks kiss;kiss.fixed=true;kiss.actionCycle=1;kiss.update(0,true,nullptr,true,false);
 scene=avatar_scene::Scene{};scene.corners=0;scene.offset=24;scene.eyeY=67.5f;
 kiss.shape(scene,4400,24,216,67.5f,14,14,23,23);assert(scene.poke.kiss==0);
 kiss.shape(scene,5200,24,216,67.5f,14,14,23,23);assert(scene.poke.kiss==1);
 const auto eyes=kiss.apply(eye_acting::p(),5110);
 assert(eyes.smile>.6f&&eyes.closeL>.8f&&eyes.closeR>.8f);
 scene.eyes=eyes;scene.body=kiss.apply(eye_acting::p(),5200,true);
 scene.eyeX=120+scene.offset+eyes.x;
 avatar_scene::buildMask(scene,mask);
 int white=0;
 avatar_scene::mouth(scene,mask,[&](int,int,float){++white;});
 assert(white==0); // Kiss is black silhouette + smiling eyes, no white lips.
 for(const char* text:{"I","III","VIII","LXXXVIII","XCIX+"}) {
  const auto& font=fittingFont(text);
  const float width=count_raster::width(font,text),strip=fmaxf(48,width+16);
  scene=avatar_scene::Scene{};scene.corners=0;scene.region=240-2*strip;
  scene.offset=strip/2;scene.eyes=eyes;
  scene.body=kiss.apply(eye_acting::p(),5200,true);
  scene.eyeWidth=50*eyes.width;scene.eyeHeight=56*eyes.height;
  scene.eyeX=120+scene.offset+eyes.x;scene.eyeY=67.5f+eyes.y;
  kiss.shape(scene,5200,strip/2,240-strip/2,67.5f,width,width,font.capHeight,font.capHeight);
  avatar_scene::buildMask(scene,mask);
  memset(raster,0,sizeof(raster));
  avatar_scene::eyes(scene,mask,[&](int x,int y,float a){raster[y*240+x]=a;});
  int mouthPixels=0;
  avatar_scene::mouth(scene,mask,[&](int,int,float){++mouthPixels;});
  assert(mouthPixels==0);
  for(int y=0;y<135;++y)for(int x=0;x<240;++x)
   if(mask.baseCoverage(x,y)==0)assert(raster[y*240+x]==0);
 }
 scene.corners=1;avatar_scene::buildMask(scene,mask);
 int pixels=0;avatar_scene::mouth(scene,mask,[&](int,int,float){++pixels;});
 assert(pixels==0); // Paired kiss acts through rim/eyes, never a wide mouth.
 kiss.shape(scene,5200,24,216,116,14,14,23,23,false,true);
 assert(scene.poke.strength==0&&scene.poke.kiss==0);
 kiss.update(5300,false);
 kiss.shape(scene,5400,24,216,116,14,14,23,23);
 assert(scene.poke.strength==0&&scene.poke.kiss==0);
 kiss.update(6000,true,nullptr,false,true);
 kiss.shape(scene,8200,24,216,67.5f,14,14,23,23);assert(scene.poke.kiss==0);
 Checks wrap;wrap.fixed=true;wrap.update(UINT32_MAX-99,true,nullptr,false,true);
 wrap.shape(scene,2100,24,216,67.5f,14,14,23,23);
 assert(scene.poke.side==1&&scene.poke.strength>0);
}
