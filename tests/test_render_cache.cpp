#include <assert.h>
#include <chrono>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../firmware/display_fonts.h"
#include "../firmware/roman_counts.h"
#include "../firmware/status_background.h"

struct Ink {
 int x,y;
 float alpha;
 bool outline;
 bool operator==(const Ink& other) const {
  return x==other.x&&y==other.y&&alpha==other.alpha&&outline==other.outline;
 }
};
unsigned long comparisons=0;
void checkCount(count_raster::Cache& cache,const count_raster::Font& font,
                const char* label,float cx=120.3f,float cy=116.7f) {
 std::vector<Ink> actual,expected;
 cache.render(font,label,cx,cy,[&](int x,int y,float alpha,bool outline) {
  actual.push_back({x,y,alpha,outline});
 });
 count_raster::render(font,label,cx,cy,[&](int x,int y,float alpha,bool outline) {
  expected.push_back({x,y,alpha,outline});
 });
 // Sequence equality matters: merging adjacent AA halos changes repeated
 // blends even if their max-alpha mask appears the same.
 assert(actual==expected);comparisons+=actual.size();
}
volatile float benchmarkSink=0;
int main(int argc,char** argv) {
 status_background::RowCache cache;
 uint16_t rows[4][240]={};
 const status_background::State initial={0x262b,0x3c1e,0,1,1,0,0xf7fc,0,0xfe62,0,0};
 const auto checkRows=[&](const status_background::State& state,bool miss) {
  int calls=0;
  const bool refreshed=cache.update(state,[&](int x,int y,uint16_t color) {
   rows[y][x]=color;++calls;
  });
  assert(refreshed==miss&&calls==(miss?960:0));
  for(int y=0;y<4;++y)for(int x=0;x<240;++x) {
   assert(rows[y][x]==status_background::color(x,y,state.done,state.active,
          state.donePresence,state.activePresence,state.portrait,state.portraitPresence,
          state.wash,state.washPresence,state.alert,state.alertPresence,true,state.base));
   ++comparisons;
  }
 };
 checkRows(initial,true);checkRows(initial,false);
 for(int member=0;member<11;++member) {
  auto changed=initial;
  switch(member) {
   case 0:changed.done^=1;break;
   case 1:changed.active^=1;break;
   case 2:changed.portrait^=1;break;
   case 3:changed.donePresence=nextafterf(1,0);break;
   case 4:changed.activePresence=nextafterf(1,0);break;
   case 5:changed.portraitPresence=nextafterf(0,1);break;
   case 6:changed.wash^=1;break;
   case 7:changed.washPresence=nextafterf(0,1);break;
   case 8:changed.alert^=1;break;
   case 9:changed.alertPresence=nextafterf(0,1);break;
   case 10:changed.base^=1;break;
  }
  checkRows(changed,true);checkRows(changed,false);checkRows(initial,true);
 }
 for(int frame=0;frame<400;++frame) {
  auto state=initial;
  state.donePresence=(frame%101)/100.0f;
  state.activePresence=1-state.donePresence;
  state.portrait=frame%2?0xf3b1:0x262b;
  state.portraitPresence=(frame%41)/40.0f;
  state.active=frame%3?0x3c1e:0xf3b1;
  state.washPresence=frame%7?0:(frame%57)/56.0f;
  checkRows(state,true);checkRows(state,false);
 }
 // A zero wash is bit-identical to the original gradient; a full wash floods it.
 for(int y=0;y<4;++y)for(int x=0;x<240;++x) {
  assert(status_background::color(x,y,0x262b,0x3c1e,1,.6f,0xf3b1,.3f,0xf7fc,0)==
         status_background::color(x,y,0x262b,0x3c1e,1,.6f,0xf3b1,.3f));
  assert(status_background::color(x,y,0x262b,0x3c1e,1,.6f,0xf3b1,.3f,0xf7fc,1)==0xf7fc);
  // A zero base (dark mode) is bit-identical; absent statuses show the base.
  assert(status_background::color(x,y,0x262b,0x3c1e,1,.6f,0xf3b1,.3f,0,0,0,0,true,0)==
         status_background::color(x,y,0x262b,0x3c1e,1,.6f,0xf3b1,.3f));
  assert(status_background::color(x,y,0x262b,0x3c1e,0,0,0,0,0,0,0,0,true,0xf7be)==0xf7be);
 }
 const count_raster::Font* fonts[]={&kBadgeFont,&kMediumBadgeFont,&kSmallBadgeFont,
                                   &kTinyBadgeFont,&kCompactBadgeFont};
 count_raster::Cache labels;
 for(const auto* font:fonts)for(int count=0;count<=101;++count) {
  char text[10];roman_counts::format(count,text);
  checkCount(labels,*font,text);
  checkCount(labels,*font,text,-.5f,130.5f); // Moving/cropped label uses the same coverage.
 }
 checkCount(labels,kSmallBadgeFont,"VVIII"); // Glyph width can exceed advance.
 checkCount(labels,kBadgeFont,"IZI"); // Unknown glyph skipped, never corrupts indexing.
 checkCount(labels,kBadgeFont,"IIIIIIIIIIII"); // Long label falls back, never truncates.
 uint8_t pixels[29]={};pixels[0]=255;pixels[28]=255;
 const count_raster::Glyph wide={'I',29,1,29,0,-1,pixels};
 const count_raster::Font unsupported={1,1,&wide};
 assert(!labels.prepare(unsupported,"I"));checkCount(labels,unsupported,"I");
 count_raster::Glyph seven[7];
 for(int i=0;i<7;++i)seven[i]=kBadgeFont.glyphs[i%6];
 const count_raster::Font many={23,7,seven};
 assert(!labels.prepare(many,"IV"));checkCount(labels,many,"IV");
 checkCount(labels,kBadgeFont,"IV"); // Fallback never poisons the previous cache.
 // Sub-pixel spring offsets: zero is the exact path; a whole-pixel shift is the
 // exact path one pixel over; fractions keep solid interiors and order.
 {
  struct Hit { int x,y;float a;bool outline; };
  const auto capture=[&](float cx,float sx,float sy) {
   count_raster::Cache cache;std::vector<Hit> out;
   cache.render(kBadgeFont,"XIV",cx,60,[&](int x,int y,float a,bool o){out.push_back({x,y,a,o});},sx,sy);
   return out;
  };
  const auto base=capture(100,0,0),moved=capture(101,0,0),whole=capture(100,1,0);
  std::vector<Hit> visible;for(const auto& h:whole)if(h.a>0)visible.push_back(h);
  assert(visible.size()==moved.size());
  for(size_t i=0;i<moved.size();++i)
   assert(visible[i].x==moved[i].x&&visible[i].y==moved[i].y&&visible[i].a==moved[i].a&&
          visible[i].outline==moved[i].outline);
  assert(base.size()==moved.size());
  const auto half=capture(100,.4f,-.7f);
  bool interiorSeen=false,outlineDone=false;float total=0,reference=0;
  for(const auto& h:half) {
   assert(h.a>0&&h.a<=1);
   if(!h.outline) { outlineDone=true;total+=h.a;if(h.a==1)interiorSeen=true; }
   else assert(!outlineDone); // All outlines precede all interiors.
  }
  for(const auto& h:base)if(!h.outline)reference+=h.a;
  assert(interiorSeen&&fabsf(total-reference)<1e-2f*reference); // Ink is conserved.
 }
 printf("Cache exact color/callback comparisons: %lu\n",comparisons);
 if(argc==2&&strcmp(argv[1],"--benchmark")==0) {
  for(int version=0;version<2;++version) {
   count_raster::Cache warmed;
   const auto start=std::chrono::steady_clock::now();
   for(int i=0;i<1000;++i) {
    float sum=0;const auto pixel=[&](int,int,float a,bool){sum+=a;};
    if(version)warmed.render(kSmallBadgeFont,"LXXXVIII",120,116,pixel);
    else count_raster::render(kSmallBadgeFont,"LXXXVIII",120,116,pixel);
    benchmarkSink+=sum;
   }
   const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
   printf("HOST ONLY numeral %s %.2fms / 1000 frames (not device FPS)\n",
          version?"cached":"scalar",ms);
  }
 }
}
