#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "../firmware/avatar_interaction.h"
#include "../firmware/display_fonts.h"

using namespace avatar_interaction;
static const int kDone=status_sides::kDone,kActive=status_sides::kActive;
// Resting x of a counter `inset` from its status_sides screen edge.
static float at(int side,float inset) { return status_sides::fromEdge(side,inset); }
const count_raster::Font& fittingFont(const char* text) {
 const count_raster::Font* fonts[]={&kBadgeFont,&kMediumBadgeFont,&kSmallBadgeFont,
                                  &kTinyBadgeFont,&kCompactBadgeFont};
 for(int i=0;i<4;++i)if(count_raster::width(*fonts[i],text)<=56)return *fonts[i];
 return *fonts[4];
}
// A check only swells the body outward, smoothly: the facing side (or the
// lower rim) moves out, nothing else moves, the change has no corner, and the
// raster still matches per-pixel coverage.
void checkSwell(const avatar_scene::Mask& mask,const avatar_scene::Mask& rest,int side) {
 for(int y=0;y<135;++y) {
  assert(mask.left[y]<=rest.left[y]&&mask.right[y]>=rest.right[y]);
  if(side>0)assert(mask.left[y]==rest.left[y]);
  if(side<0)assert(mask.right[y]==rest.right[y]);
 }
 for(int x=0;x<240;++x)assert(mask.bottom[x]>=rest.bottom[x]);
 // Second differences of the added skin stay small: a bell, never a tent.
 for(int y=1;y<134;++y) {
  const float a=rest.left[y-1]-mask.left[y-1],b=rest.left[y]-mask.left[y],c=rest.left[y+1]-mask.left[y+1];
  const float d=mask.right[y-1]-rest.right[y-1],e=mask.right[y]-rest.right[y],f=mask.right[y+1]-rest.right[y+1];
  if(rest.left[y]>0&&mask.left[y-1]>0&&mask.left[y+1]>0)assert(fabsf(a-2*b+c)<.25f);
  if(rest.right[y]<240&&mask.right[y-1]<240&&mask.right[y+1]<240)assert(fabsf(d-2*e+f)<.25f);
 }
 for(int x=1;x<239;++x) {
  if(rest.bottom[x-1]<0||rest.bottom[x+1]<0)continue;
  const float a=mask.bottom[x-1]-rest.bottom[x-1],b=mask.bottom[x]-rest.bottom[x],c=mask.bottom[x+1]-rest.bottom[x+1];
  assert(fabsf(a-2*b+c)<.25f);
 }
 static float raster[240*135];
 memset(raster,0,sizeof(raster));
 mask.raster([&](int x,int y,int n){for(int j=0;j<n;++j)raster[y*240+x+j]=1;},
             [&](int x,int y,float a){raster[y*240+x]=a;});
 for(int y=0;y<135;++y)for(int x=0;x<240;++x) {
  const float a=mask.coverage(x,y);
  assert(isfinite(a)&&a>=0&&a<=1);
  assert(fabsf(raster[y*240+x]-a)<.0011f);
 }
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
 checkSwell(poked,rest,-1);
 // It reaches the numeral at its row and fades to nothing well away from it.
 const auto swells=avatar_scene::pokeSwells(scene);
 assert(swells.side==-1&&swells.edge.amount>=4&&swells.rim.amount==0);
 assert(poked.left[67]<rest.left[67]-swells.edge.amount+.01f);
 for(int y=0;y<135;++y)
  if(fabsf(y+.5f-67.5f)>=swells.edge.width)assert(poked.left[y]==rest.left[y]);
 assert(contactDepth(poked,24,67.5f,14,23,0,-1)>0);
 // Zero/tiny strength is continuous and keeps the optimized idle path exact.
 scene.poke.strength=0;avatar_scene::buildMask(scene,poked);
 assert(memcmp(&rest,&poked,sizeof(rest))==0);
 scene.poke.strength=1e-8f;avatar_scene::buildMask(scene,poked);
 for(int y=0;y<135;++y)for(int x=0;x<240;++x)
  assert(fabsf(poked.coverage(x,y)-rest.coverage(x,y))<.001f);
 scene.poke.strength=1;scene.alert=1;avatar_scene::buildMask(scene,poked);
 auto without=scene;without.poke.strength=0;avatar_scene::buildMask(without,rest);
 assert(memcmp(&rest,&poked,sizeof(rest))==0);

 // Reflow, rim flaps and narrow bodies: the swell stays outward-only and
 // smooth on every layout, and always adds visible skin.
 for(int side=-1;side<=1;side+=2)for(float corners:{0.0f,.05f,.25f,.5f,.75f,.95f,1.0f})
 for(float width:{.75f,1.0f,1.25f}) {
  scene=avatar_scene::Scene{};scene.corners=corners;scene.offset=-side*24*(1-corners);
  scene.body.bodyWidth=width;
  face_physics::Surface surface;scene.surface=&surface;
  for(int i=0;i<face_physics::Surface::count;++i)surface.displacement[i]=(i%5-2)*4;
  scene.poke.side=side;scene.poke.strength=1;
  scene.poke.cx=side<0?24:216;scene.poke.cy=avatar_scene::mix(67.5f,116,corners);
  scene.poke.width=14;scene.poke.height=23;
  auto plain=scene;plain.poke.strength=0;
  avatar_scene::buildMask(plain,rest);avatar_scene::buildMask(scene,mask);
  checkSwell(mask,rest,side);
  int outside=0;
  for(int y=0;y<135;++y)for(int x=0;x<240;++x)
   if(rest.coverage(x,y)==0&&mask.coverage(x,y)>.99f)++outside;
  // Unless the body already covers the numeral's whole corner of the screen.
  const int row=static_cast<int>(scene.poke.cy),col=static_cast<int>(scene.poke.cx);
  const bool room=(side<0?rest.left[row]>.5f:rest.right[row]<239.5f)||rest.bottom[col]<134;
  assert(outside>0||!room);
 }

 // Every clip, both layouts, both sides and fitting/narrow Roman widths:
 // each authored press must produce actual contact and a visible spring nudge.
 int performances=0;
 float minimumTravel=3;
 for(int paired=0;paired<2;++paired)for(int side=-1;side<=1;side+=2)
 for(int index=0;index<eye_acting::clipCount(side==kDone?eye_acting::State::done:eye_acting::State::thinking);++index)
 for(const char* text:{"I","III","VIII","LXXXVIII","XCIX+"}) {
  const bool isDone=side==kDone;  // `side` is the counter's screen side
  const auto& font=fittingFont(text);
  const float width=count_raster::width(font,text),strip=fmaxf(48,width+16);
  scene=avatar_scene::Scene{};scene.corners=paired;scene.region=240-2*strip;
  scene.offset=paired?0:-side*strip/2;
  face_physics::Surface surface;scene.surface=&surface;
  Checks checks;checks.fixed=true;CounterSpring spring;
  eye_acting::Player actor;actor.state=isDone?eye_acting::State::done:eye_acting::State::thinking;
  actor.index=index;actor.started=isDone?4400:1400;
  float maximum=0,depthMaximum=0;
  for(uint32_t t=0;t<8400;t+=33) {
   checks.update(t,true,&surface,paired||isDone,paired||!isDone);
   scene.body=checks.apply(actor.body(t),t,true);
   scene.eyes=checks.apply(actor.eyes(t),t);
   surface.step(.033f,scene.body,paired);scene.flowClock=t*.0006f;
   checks.shape(scene,t,at(kDone,strip/2),at(kActive,strip/2),paired?116:67.5f,width,width,
                font.capHeight,font.capHeight);
   // As in the frame: the membranes carry the swell and lag behind it.
   face_physics::Shape carried;avatar_scene::membraneShape(scene,carried);surface.follow(carried);
   avatar_scene::buildMask(scene,mask);
   const float depth=contactDepth(mask,side<0?strip/2:240-strip/2,paired?116:67.5f,
                                  width,font.capHeight,paired,side);
   spring.update(.033f,depth,true,side);
   assert(isfinite(spring.x)&&fabsf(spring.x)<=3&&spring.y>=-2.5f&&spring.y<=3);
   const uint32_t begin=isDone?4800:1800,end=isDone?6000:3000;
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
 const float doneX=at(kDone,24),activeX=at(kActive,24);
 scene=avatar_scene::Scene{};scene.corners=0;scene.offset=24.0f*kActive;scene.eyeY=67.5f;
 kiss.shape(scene,4400,doneX,activeX,67.5f,14,14,23,23);assert(scene.poke.kiss==0);
 kiss.shape(scene,5200,doneX,activeX,67.5f,14,14,23,23);assert(scene.poke.kiss==1);
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
  scene.offset=strip/2*kActive;scene.eyes=eyes;
  scene.body=kiss.apply(eye_acting::p(),5200,true);
  scene.eyeWidth=50*eyes.width;scene.eyeHeight=56*eyes.height;
  scene.eyeX=120+scene.offset+eyes.x;scene.eyeY=67.5f+eyes.y;
  kiss.shape(scene,5200,at(kDone,strip/2),at(kActive,strip/2),67.5f,width,width,font.capHeight,font.capHeight);
  avatar_scene::buildMask(scene,mask);
  static float raster[240*135];
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
 kiss.shape(scene,5200,doneX,activeX,116,14,14,23,23,false,true);
 assert(scene.poke.strength==0&&scene.poke.kiss==0);
 kiss.update(5300,false);
 kiss.shape(scene,5400,doneX,activeX,116,14,14,23,23);
 assert(scene.poke.strength==0&&scene.poke.kiss==0);
 kiss.update(6000,true,nullptr,false,true);
 kiss.shape(scene,8200,doneX,activeX,67.5f,14,14,23,23);assert(scene.poke.kiss==0);
 Checks wrap;wrap.fixed=true;wrap.update(UINT32_MAX-99,true,nullptr,false,true);
 wrap.shape(scene,2100,doneX,activeX,67.5f,14,14,23,23);
 assert(scene.poke.side==kActive&&scene.poke.strength>0);
}
