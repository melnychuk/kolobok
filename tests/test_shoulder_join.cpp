#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "../firmware/avatar_interaction.h"

// Signed inward deviation from a 24px chord. A hard circle/body union makes
// an inward corner here; a convex skin join does not. Leave organic base motion.
static float dent(const avatar_scene::Mask& mask,int side,bool raw=false,bool base=false) {
 float edge[135],worst=0;
 for(int y=0;y<135;++y) {
  float lo=mask.left[y],hi=mask.right[y],a,b;
  if(!base) {
   if(raw) {
    if(mask.hand.span(y+.5f,false,a,b)) { lo=fminf(lo,a);hi=fmaxf(hi,b); }
   } else mask.rowBounds(y,lo,hi);
  }
  edge[y]=(side<0?lo:-hi);
 }
 for(int y=12;y<123;++y)worst=fmaxf(worst,edge[y]-(edge[y-12]+edge[y+12])*.5f);
 return worst;
}
static void pixels(const avatar_scene::Mask& mask,const avatar_scene::Mask& base) {
 assert(mask.hand.radius>0);
 float raster[240*135]={};bool seen[240*135]={};int queue[240*135],begin=0,end=0;
 mask.raster([&](int x,int y,int n){for(int i=0;i<n;++i)raster[y*240+x+i]=1;},
             [&](int x,int y,float a){raster[y*240+x]=a;});
 const int root=static_cast<int>(mask.hand.ay)*240+static_cast<int>(mask.hand.ax);
 queue[end++]=root;seen[root]=true;
 while(begin<end) {
  const int at=queue[begin++],x=at%240,y=at/240;
  for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
   const int xx=x+dx,yy=y+dy;
   if(xx<0||xx>=240||yy<0||yy>=135)continue;
   const int next=yy*240+xx;
   if(!seen[next]&&mask.coverage(xx,yy)>.5f) {seen[next]=true;queue[end++]=next;}
  }
 }
 for(int y=0;y<135;++y)for(int x=0;x<240;++x) {
  const float a=mask.coverage(x,y);
  assert(mask.baseCoverage(x,y)==base.coverage(x,y)); // eye clip is untouched
  assert(a+1e-6f>=fmaxf(base.coverage(x,y),mask.hand.coverage(x,y))); // expansion-only
  assert(fabsf(a-raster[y*240+x])<.0011f);
  if(a>.5f&&!seen[y*240+x]) {
   fprintf(stderr,"Detached (%d,%d) base=%f hand=%f shoulder=%f side=%d root=%d\n",
           x,y,base.coverage(x,y),mask.hand.coverage(x,y),mask.shoulderCoverage(x,y),mask.shoulderSide,root);
   assert(seen[y*240+x]);
  } // skin/hand never detaches
  if(mask.shoulderCoverage(x,y)>.5f)assert(mask.bottomAt(x)>=y+1);
 }
}
int main() {
 unsigned frames=0,retreatFrames=0,pixelChecks=0;
 float before=0,after=0,extraDent=0;
 for(int side: {-1,1})for(int kiss: {0,1})for(int strip: {48,72})
 for(uint32_t cadence: {17u,33u,50u}) {
  // `side` is the counter's screen side; its status follows status_sides.
  const bool isDone=side==status_sides::kDone;
  const auto state=isDone?eye_acting::State::done:eye_acting::State::thinking;
  for(int clip=0;clip<eye_acting::clipCount(state);++clip) {
   avatar_interaction::Checks checks;checks.fixed=true;checks.actionCycle=kiss;
   face_physics::Surface surface;
   eye_acting::Player actor;actor.state=state;actor.index=clip;
   for(uint32_t ms=0;ms<8400;ms+=cadence) {
    checks.update(ms,true,&surface,isDone,!isDone);
    avatar_scene::Scene scene;scene.corners=0;scene.region=240-2*strip;
    scene.offset=-side*strip/2.0f;scene.eyeY=67.5f;
    scene.body=checks.apply(actor.body(ms),ms,true);scene.eyes=checks.apply(actor.eyes(ms),ms);
    surface.step(cadence*.001f,scene.body,false);scene.surface=&surface;
    scene.flowClock=ms*.0006f;
    checks.shape(scene,ms,status_sides::fromEdge(status_sides::kDone,strip/2.0f),
                 status_sides::fromEdge(status_sides::kActive,strip/2.0f),67.5f,14,14,23,23);
    avatar_scene::Mask mask,base;avatar_scene::buildMask(scene,mask);
    auto plain=scene;plain.poke.strength=0;avatar_scene::buildMask(plain,base);
    assert(memcmp(mask.left,base.left,sizeof(mask.left))==0);
    assert(memcmp(mask.right,base.right,sizeof(mask.right))==0);
    assert(memcmp(mask.bottom,base.bottom,sizeof(mask.bottom))==0);
    const auto moment=checks.moment(ms,true);
    if(moment.side==side&&moment.local>=950&&moment.local<=1400) {
     const float raw=dent(mask,side,true),fixed=dent(mask,side),natural=dent(mask,side,false,true);
     before=fmaxf(before,raw);after=fmaxf(after,fixed);extraDent=fmaxf(extraDent,fixed-natural);
     assert(fixed<=natural+.05f);
     ++retreatFrames;
     if(cadence==33&&clip==0&&moment.local>=1100&&moment.local<1140) {
      pixels(mask,base);++pixelChecks;
     }
    }
    if(mask.hand.radius==0)assert(memcmp(&mask,&base,sizeof(mask))==0);
    ++frames;
   }
  }
 }
 assert(before>.5f&&retreatFrames>1000&&pixelChecks>0);
 printf("Shoulder retreat: %u frames / %u retreat frames; inward chord error %.3f -> %.3f px "
        "(extra over organic base %.4f); %u whole-mask connectivity/AA/clip checks\n",
        frames,retreatFrames,before,after,extraDent,pixelChecks);
 // Reflow remains continuous and exactly paired/request/sleep when suppressed.
 for(float corners: {0.f,1e-6f,.2f,.8f,.999999f,1.f}) {
  avatar_scene::Scene scene;scene.corners=corners;scene.offset=24*(1-corners);
  scene.poke.side=-1;scene.poke.strength=1;scene.poke.cx=24;
  scene.poke.cy=avatar_scene::mix(67.5f,116,corners);scene.poke.width=14;scene.poke.height=23;
  avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);
  if(corners==1)assert(mask.shoulderSide==0);
  if(corners>0)assert(mask.shoulderTop>=0&&mask.shoulderBottom<135);
 }
}
