#include <assert.h>
#include <math.h>
#include <vector>
#include <initializer_list>
#include "../firmware/eye_raster.h"
#include "../firmware/avatar_director.h"
struct Pixel { int x,y;float a; };
static std::vector<Pixel> raster(const eye_acting::Pose& pose,bool spans,
                                 eye_raster::Style style=eye_raster::Style::legacy) {
 std::vector<Pixel> result;
 auto pixel=[&](int x,int y,float a){assert(isfinite(a)&&a>0&&a<=1);result.push_back({x,y,a});};
 if(spans)eye_raster::render(120,67,50,45.36f,19,pose,pixel,
     [&](int x,int y,int n){for(int i=0;i<n;++i)pixel(x+i,y,1);},.22f,.06f,style);
 else eye_raster::render(120,67,50,45.36f,19,pose,pixel,.22f,.06f,style);
 return result;
}
int main() {
 unsigned cases=0;
 for(float tilt: {0.f,-7.f,7.f})for(float lower: {0.f,.08f,.3f})
 for(float slant: {-.2f,0.f,.2f})for(float scale: {.96f,1.f,1.04f})
 for(float closure: {0.f,.4f,1.f})for(float smile: {0.f,.6f}) {
  auto p=eye_acting::withSlant(eye_acting::withLowerLids(
      eye_acting::withEyeScale(eye_acting::p(),scale,2-scale),lower,lower*.5f),slant,-slant*.5f);
  p.tilt=tilt;p.closeL=closure;p.closeR=closure*.6f;p.smile=smile;
  for(auto style: {eye_raster::Style::legacy,eye_raster::Style::natural}) {
   const auto scalar=raster(p,false,style),spans=raster(p,true,style);
   assert(scalar.size()==spans.size());
   for(unsigned i=0;i<scalar.size();++i)
    assert(scalar[i].x==spans[i].x&&scalar[i].y==spans[i].y&&scalar[i].a==spans[i].a);
  }
  ++cases;
 }
 assert(cases==486);
 // Natural style: neutral, lidded and plain-blink poses are untouched; only
 // smiles (shallower, rounded cheek) and happy closures (tapered crease) move.
 {
  using eye_raster::Style;
  auto lidded=eye_acting::p(3,-2,1,1,.2f,.1f);lidded.closeL=.6f;
  for(const auto& pose: {eye_acting::p(),lidded,eye_acting::p(0,0,1,1,0,0,0,1,1)}) {
   const auto a=raster(pose,false,Style::legacy),b=raster(pose,false,Style::natural);
   assert(a.size()==b.size());
   for(unsigned i=0;i<a.size();++i)assert(a[i].x==b[i].x&&a[i].y==b[i].y&&a[i].a==b[i].a);
  }
  // A natural smile keeps more of the eye than the legacy crescent.
  const auto ink=[](const std::vector<Pixel>& v){float sum=0;for(const auto& p:v)sum+=p.a;return sum;};
  const auto smiling=eye_acting::p(0,0,1,1,0,0,.65f);
  assert(ink(raster(smiling,false,Style::natural))>ink(raster(smiling,false,Style::legacy))*1.1f);
  // Happy crease: thicker at the centre than the corners, centre above the
  // corners (still a happy upward curve), and seated lower than the legacy arch.
  const auto closed=eye_acting::p(0,0,1,1,0,0,.6f,1,1);
  const auto stats=[&](Style style,float& centreY,float& cornerY,float& centreInk,float& cornerInk) {
   float cy=0,cw=0,oy=0,ow=0;
   for(const auto& p: raster(closed,false,style)) {
    if(p.x>=120)continue;
    const float dx=fabsf(p.x+.5f-85.5f);
    if(dx<3){cy+=(p.y+.5f)*p.a;cw+=p.a;}
    if(dx>17&&dx<21){oy+=(p.y+.5f)*p.a;ow+=p.a;}
   }
   assert(cw>0&&ow>0);centreY=cy/cw;cornerY=oy/ow;centreInk=cw/6;cornerInk=ow/8;
  };
  float nC,nO,nCi,nOi,lC,lO,lCi,lOi;
  stats(Style::natural,nC,nO,nCi,nOi);stats(Style::legacy,lC,lO,lCi,lOi);
  assert(nO-nC>2&&nCi>nOi*1.5f);           // curved and tapered
  assert(fabsf(lCi-lOi)<lCi*.25f);         // legacy: uniform stroke
  assert(nC>lC+3);                          // seated lower in the eye
 }
 const auto base=raster(eye_acting::p(),false);
 const auto larger=raster(eye_acting::withEyeScale(eye_acting::p(),1.04f,1),false);
 float baseL=0,baseR=0,largeL=0,largeR=0;
 for(const auto& p:base)(p.x<120?baseL:baseR)+=p.a;
 for(const auto& p:larger)(p.x<120?largeL:largeR)+=p.a;
 assert(largeL>baseL*1.07f&&largeR==baseR);
 assert(raster(eye_acting::withLowerLids(eye_acting::p(),.3f,.3f),false).size()<base.size());
 // New transition shapes must stay in the same modest pixel workload envelope.
 using namespace avatar_behavior;
 const Mode modes[]={Mode::offline,Mode::idle,Mode::sleeping,Mode::working,Mode::waiting,Mode::finished,Mode::listening};
 uint64_t shaped=0,neutral=0;unsigned maxShaped=0,maxNeutral=0;
 for(auto from:modes)for(auto to:modes)if(from!=to) {
  Director d;eye_acting::Player player;Targets t;
  d.step(from,player,0,1,false,t);d.capture(100,rest(from),rest(from));
  d.step(to,player,100,2,false,t);
  for(uint32_t ms=0;ms<d.length+90u;ms+=40) {
   auto pose=d.sample(ms,false),old=pose;
   old.scaleL=old.scaleR=old.lowerL=old.lowerR=old.slantL=old.slantR=0;
   const unsigned a=raster(pose,false).size(),b=raster(old,false).size();
   shaped+=a;neutral+=b;if(a>maxShaped)maxShaped=a;if(b>maxNeutral)maxNeutral=b;
  }
 }
 assert(shaped<=neutral*1.03&&maxShaped<=maxNeutral*1.03);
 // Bound and cancel event-only accents; no delayed replay after a request.
 Director d;eye_acting::Player player;Targets t;d.step(Mode::working,player,0,1,false,t);
 d.notifyCompletion();d.step(Mode::finished,player,100,2,false,t);
 unsigned pixels=0;
 d.accents(280,24,30,[&](int x,int y,float a){assert(x>=0&&x<240&&y<28&&y>0&&a>0&&a<=1);++pixels;});
 assert(pixels==18);
 d.step(Mode::waiting,player,300,3,false,t);assert(!d.accentActive);
 d.step(Mode::finished,player,400,4,false,t);assert(!d.accentActive);
}
