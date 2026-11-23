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
// A check grows a round cap: only the facing side (or the lower rim) moves,
// only outward, only near the numeral, every opaque pixel stays connected to
// the body, and the raster still matches per-pixel coverage.
static float worstBend=0;
// Largest turn a poke adds to the outline between neighbouring rows/columns,
// in degrees. A right-angled crease (a cap stuck on without a tangent fillet)
// measures 110-120; the round cap meeting the steep paired rim near a screen
// corner in a soft valley measures about 73.
static const float kMaxTurn=80;
void checkPoke(const avatar_scene::Mask& mask,const avatar_scene::Mask& rest,int side,
               float windowCenterY=-1,float windowCenterX=-1,float window=0) {
 for(int y=0;y<135;++y) {
  assert(mask.left[y]<=rest.left[y]&&mask.right[y]>=rest.right[y]);
  if(side>0)assert(mask.left[y]==rest.left[y]);
  if(side<0)assert(mask.right[y]==rest.right[y]);
  if(window>0&&fabsf(y+.5f-windowCenterY)>=window)
   assert(mask.left[y]==rest.left[y]&&mask.right[y]==rest.right[y]);
 }
 for(int x=0;x<240;++x) {
  assert(mask.bottom[x]>=rest.bottom[x]);
  if(window>0&&fabsf(x+.5f-windowCenterX)>=window)assert(mask.bottom[x]==rest.bottom[x]);
 }
 // No creases or spikes: a round dome on a soft fillet. A second difference
 // is no measure here (a round dome's sides are steep near its base), so
 // bound how much more sharply the outline turns between neighbouring edge
 // segments than the resting outline does there.
 const auto turn=[](float a,float b,float c) {
  const float first=atan2f(b-a,1),second=atan2f(c-b,1);
  return fabsf(second-first)*57.29578f;
 };
 for(int y=1;y<134;++y) {
  if(mask.left[y]==rest.left[y]&&mask.right[y]==rest.right[y]&&
     mask.left[y-1]==rest.left[y-1]&&mask.left[y+1]==rest.left[y+1]&&
     mask.right[y-1]==rest.right[y-1]&&mask.right[y+1]==rest.right[y+1])continue;
  const bool rows=mask.right[y-1]>mask.left[y-1]&&mask.right[y+1]>mask.left[y+1];
  if(rows&&mask.left[y]>0&&mask.left[y-1]>0&&mask.left[y+1]>0)
   worstBend=fmaxf(worstBend,turn(mask.left[y-1],mask.left[y],mask.left[y+1])-turn(rest.left[y-1],rest.left[y],rest.left[y+1]));
  if(rows&&mask.right[y]<240&&mask.right[y-1]<240&&mask.right[y+1]<240)
   worstBend=fmaxf(worstBend,turn(mask.right[y-1],mask.right[y],mask.right[y+1])-turn(rest.right[y-1],rest.right[y],rest.right[y+1]));
 }
 for(int x=1;x<239;++x) {
  if(rest.bottom[x-1]<0||rest.bottom[x+1]<0)continue;
  if(mask.bottom[x-1]==rest.bottom[x-1]&&mask.bottom[x]==rest.bottom[x]&&mask.bottom[x+1]==rest.bottom[x+1])continue;
  worstBend=fmaxf(worstBend,turn(mask.bottom[x-1],mask.bottom[x],mask.bottom[x+1])-turn(rest.bottom[x-1],rest.bottom[x],rest.bottom[x+1]));
 }
 assert(worstBend<kMaxTurn);
 static float raster[240*135];
 memset(raster,0,sizeof(raster));
 mask.raster([&](int x,int y,int n){for(int j=0;j<n;++j)raster[y*240+x+j]=1;},
             [&](int x,int y,float a){raster[y*240+x]=a;});
 for(int y=0;y<135;++y)for(int x=0;x<240;++x) {
  const float a=mask.coverage(x,y);
  assert(isfinite(a)&&a>=0&&a<=1);
  assert(fabsf(raster[y*240+x]-a)<.0011f);
 }
 // Every opaque pixel the cap adds is connected to the opaque body:
 // a tip never floats free of the outline.
 static unsigned char seen[240*135];
 static int queue[240*135];
 memset(seen,0,sizeof(seen));
 int head=0,tail=0;
 for(int y=0;y<135;++y)for(int x=0;x<240;++x)
  if(rest.coverage(x,y)>=.5f&&mask.coverage(x,y)>=.5f){seen[y*240+x]=1;queue[tail++]=y*240+x;}
 while(head<tail) {
  const int at=queue[head++],x=at%240,y=at/240;
  // 8-connected: an anti-aliased fringe pixel may touch only diagonally.
  const int next[8][2]={{x-1,y},{x+1,y},{x,y-1},{x,y+1},{x-1,y-1},{x+1,y-1},{x-1,y+1},{x+1,y+1}};
  for(const auto& n:next) {
   if(n[0]<0||n[0]>=240||n[1]<0||n[1]>=135)continue;
   const int i=n[1]*240+n[0];
   if(!seen[i]&&mask.coverage(n[0],n[1])>=.5f){seen[i]=1;queue[tail++]=i;}
  }
 }
 for(int i=0;i<240*135;++i)if(mask.coverage(i%240,i/240)>=.5f&&!seen[i]){fprintf(stderr,"floating x=%d y=%d cov=%.2f rest=%.2f left=%.2f right=%.2f bottom=%.2f restbottom=%.2f\n",i%240,i/240,mask.coverage(i%240,i/240),rest.coverage(i%240,i/240),mask.left[i/240],mask.right[i/240],mask.bottom[i%240],rest.bottom[i%240]);assert(seen[i]);}
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

 // A round blob, not a bump: a dome at the numeral's row whose round nose
 // rests kPokeGap short of the numeral, nothing changed away from it.
 avatar_scene::Scene scene;scene.corners=0;scene.offset=24;scene.eyeY=67.5f;
 avatar_scene::Mask rest,poked;
 avatar_scene::buildMask(scene,rest);
 scene.poke.side=-1;scene.poke.cx=24;scene.poke.cy=67.5f;
 scene.poke.width=14;scene.poke.height=23;scene.poke.strength=1;
 avatar_scene::buildMask(scene,poked);
 const auto swells=avatar_scene::pokeSwells(scene);
 assert(swells.side==-1&&swells.edge.amount>0&&swells.rim.amount==0);
 checkPoke(poked,rest,-1,67.5f,-1,swells.edge.width);
 assert(fabsf(poked.left[67]-(24+7+avatar_scene::kPokeGap))<.5f);
 const float length=rest.left[67]-poked.left[67];
 assert(length>=avatar_scene::kPokeMinLength);
 // Its root swell is a small fraction of it, and it is a localized dome
 // about as wide as it is long, not a side-long bell.
 assert(swells.edge.amount<.2f*length);
 int thick=0;
 for(int y=0;y<135;++y)if(rest.left[y]-poked.left[y]>length/2)++thick;
 assert(thick>=length&&thick<=2*avatar_scene::kPokeRadiusMax+length);
 // A jab brings the nose into contact, pushing into the numeral.
 const float resting=contactDepth(poked,24,67.5f,14,23,0,-1);
 scene.poke.jab=avatar_scene::kPokeGap+3;avatar_scene::Mask jabbed;avatar_scene::buildMask(scene,jabbed);
 assert(jabbed.left[67]<24+7-2.5f);
 assert(contactDepth(jabbed,24,67.5f,14,23,0,-1)>resting);
 scene.poke.jab=0;
 // Paired: the same dome grows down out of the lower rim.
 {
  avatar_scene::Scene paired;paired.corners=1;
  avatar_scene::Mask flat,down;avatar_scene::buildMask(paired,flat);
  paired.poke=scene.poke;paired.poke.cy=116;avatar_scene::buildMask(paired,down);
  const auto rim=avatar_scene::pokeSwells(paired);
  assert(rim.edge.amount==0&&rim.rim.amount>0);
  checkPoke(down,flat,-1,-1,24,rim.rim.width);
  const float drop=down.bottom[24]-flat.bottom[24];
  assert(drop>=avatar_scene::kPokeMinLength);
  // At least to its resting nose position (further if the minimum length
  // clamps a rim that already sits close to the numeral).
  assert(down.bottom[24]>=116-11.5f-avatar_scene::kPokeGap-.5f);
  int wide=0;
  for(int x=0;x<240;++x)if(down.bottom[x]-flat.bottom[x]>drop/2)++wide;
  assert(wide>=1.5f*avatar_scene::kPokeRadiusMin&&wide<=2*avatar_scene::kPokeRadiusMax+drop);
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

 // Reflow, rim flaps and narrow bodies: the cap stays outward-only,
 // connected and kink-free on every layout, and adds visible skin whenever
 // the layout is (nearly) settled.
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
  checkPoke(mask,rest,side);
  int outside=0;
  for(int y=0;y<135;++y)for(int x=0;x<240;++x)
   if(rest.coverage(x,y)==0&&mask.coverage(x,y)>.99f)++outside;
  // Unless the body already covers the numeral's whole corner of the screen.
  const int row=static_cast<int>(scene.poke.cy),col=static_cast<int>(scene.poke.cx);
  const bool room=(side<0?rest.left[row]>.5f:rest.right[row]<239.5f)||rest.bottom[col]<134;
  // Mid-reflow a narrow body's side may not reach the numeral's column yet;
  // live checks restart 1.4s after any presence change, long after reflow.
  const bool settled=corners<=.05f||corners>=.95f;
  if(settled&&room&&outside==0)
   fprintf(stderr,"No visible poke side=%d corners=%.2f width=%.2f\n",side,corners,width);
  assert(outside>0||!room||!settled);
 }

 // Every clip, both layouts, both sides and fitting/narrow Roman widths:
 // each authored press must produce actual contact and a visible spring nudge.
 int performances=0,glances=0;
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
  bool poked=false;
  for(uint32_t t=0;t<8400;t+=33) {
   checks.update(t,true,&surface,paired||isDone,paired||!isDone);
   scene.body=checks.apply(actor.body(t),t,true);
   scene.eyes=checks.apply(actor.eyes(t),t);
   surface.step(.033f,scene.body,paired);scene.flowClock=t*.0006f;
   checks.shape(scene,t,at(kDone,strip/2),at(kActive,strip/2),paired?116:67.5f,width,width,
                font.capHeight,font.capHeight);
   if(scene.poke.side==side&&(scene.poke.strength>0||scene.poke.kiss>0))poked=true;
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
  // With both counters on screen, completed counters only get a glance.
  if(paired&&isDone) { assert(!poked);++glances;continue; }
  assert(poked);
  if(maximum<=1.0f||depthMaximum<=0)
   fprintf(stderr,"Missed poke paired=%d side=%d clip=%d label=%s motion=%f depth=%f\n",
           paired,side,index,text,maximum,depthMaximum);
  assert(maximum>1.0f&&depthMaximum>0);++performances;
  minimumTravel=fminf(minimumTravel,maximum);
 }
 assert(performances>0&&glances>0);
 printf("Actual pokes with visible counter travel: %d (minimum %.2f px), paired done glances: %d; "
        "sharpest outline turn %.1f deg\n",performances,minimumTravel,glances,worstBend);

 // Authored kiss is occasional, completed-only, eye-led and mouth-clipped.
 Checks kiss;kiss.fixed=true;kiss.actionCycle=1;kiss.update(0,true,nullptr,true,false);
 const float doneX=at(kDone,24),activeX=at(kActive,24);
 scene=avatar_scene::Scene{};scene.corners=0;scene.offset=24.0f*kActive;scene.eyeY=67.5f;
 kiss.shape(scene,4400,doneX,activeX,67.5f,14,14,23,23);assert(scene.poke.kiss==0);
 kiss.shape(scene,5200,doneX,activeX,67.5f,14,14,23,23);assert(scene.poke.kiss==1);
 const auto eyes=kiss.apply(eye_acting::p(),5110);
 assert(eyes.smile>.6f&&eyes.closeL>.8f&&eyes.closeR>.8f);
 // The kiss is a cheek nuzzle, not a poke: no blob and no jab, the facing
 // side swells softly (a broad bell) into the numeral, and the body leans in.
 assert(scene.poke.strength==0&&scene.poke.jab==0);
 {
  const auto body=kiss.apply(eye_acting::p(),5200,true);
  assert(body.lean*kDone>=Checks::kNuzzleLean-.01f);
  avatar_scene::Scene cheek;cheek.corners=0;cheek.offset=24.0f*kActive;cheek.eyeY=67.5f;
  avatar_scene::Mask plain,nuzzled;avatar_scene::buildMask(cheek,plain);
  kiss.shape(cheek,5200,doneX,activeX,67.5f,14,14,23,23);
  avatar_scene::buildMask(cheek,nuzzled);
  const auto bell=avatar_scene::pokeSwells(cheek);
  assert(bell.side==kDone&&bell.edge.width==avatar_scene::kCheekWidth&&bell.edge.amount>0);
  checkPoke(nuzzled,plain,kDone);
  assert(contactDepth(nuzzled,doneX,67.5f,14,23,0,kDone)>0);
  // Broad and soft: a numeral's height away the added skin is still more
  // than a third of its peak, unlike a round blob.
  float peak=0;
  for(int y=0;y<135;++y)peak=fmaxf(peak,kDone<0?plain.left[y]-nuzzled.left[y]:nuzzled.right[y]-plain.right[y]);
  const int away=67+23;
  const float there=kDone<0?plain.left[away]-nuzzled.left[away]:nuzzled.right[away]-plain.right[away];
  assert(peak>0&&there>peak/3);
 }
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
