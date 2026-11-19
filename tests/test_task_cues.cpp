// Staged task-count cues: synthetic aggregates only, no device or task access.
#include <assert.h>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>
#include <initializer_list>
#define STICK_AVATAR_WIDGET_NO_MAIN
#include "../experiments/avatar-widget/main.cpp"

using task_cues::Arrival;
using task_cues::Transfer;

struct Drawn { std::string text; float x,y; bool flying; };
static std::vector<Drawn> drawArrival(const Arrival& a,uint32_t now,float restX=216,float restY=67.5f) {
 std::vector<Drawn> out;
 a.draw(now,fontFor(a.from),fontFor(a.to),restX,restY,
        [&](const count_raster::Font&,const char* text,float x,float y,bool flying) {
  out.push_back({text,x,y,flying});
 },[&](int x,int y,float alpha){assert(alpha>=0&&alpha<=1.001f);(void)x;(void)y;});
 return out;
}
static int coveredPixels(const avatar_scene::Scene& scene) {
 avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);
 int covered=0;
 for(int y=0;y<135;++y)for(int x=0;x<240;++x)if(mask.coverage(x,y)>0)++covered;
 return covered;
}

static void arrivalEdges() {
 Arrival a;
 assert(!a.observe(0,2,true,true));            // startup is a baseline
 assert(!a.observe(2000,2,true,true));         // heartbeats never retrigger
 assert(a.observe(4000,3,true,true)&&a.busy(4000)&&a.from==2&&a.to==3);
 assert(!a.observe(4100,3,true,true)&&a.busy(4100));
 // A further rise while the numeral is still in the air reveals current data.
 assert(!a.observe(4200,4,true,true)&&!a.busy(4200));
 // After landing, a new rise starts a fresh drop from the landed count.
 assert(a.observe(9000,5,true,true)&&a.from==4);
 assert(!a.observe(9000+Arrival::kImpact+10,6,true,true)||a.from==5);
 // Decreases, unready snapshots and recovery never start a drop.
 Arrival b;b.observe(0,3,true,true);
 assert(!b.observe(100,2,true,true)&&!b.busy(100));
 assert(!b.observe(200,4,true,false)&&!b.busy(200));
 assert(!b.observe(300,0,false,true)&&!b.busy(300));
 assert(!b.observe(400,5,true,true)); // first healthy snapshot is a baseline
 // Exactly one impact thud; dodge bounded and exactly zero afterwards.
 Arrival c;c.observe(0,1,true,true);assert(c.observe(1000,2,true,true));
 int impacts=0;float minDodge=0,previous=0;
 for(uint32_t t=1000;t<1000+Arrival::kEnd+500;t+=33) {
  c.update(t,true);
  if(c.consumeImpact(t))++impacts;
  const float dodge=c.dodge(t);
  assert(dodge<=0&&dodge>=-20.001f&&fabsf(dodge-previous)<14);
  minDodge=fminf(minDodge,dodge);previous=dodge;
 }
 assert(impacts==1&&minDodge<-19&&c.dodge(1000+Arrival::kEnd+500)==0);
 // No pose change outside the cue; eye and body overlap is 90ms.
 const auto base=eye_acting::p(3,2);
 assert(eye_acting::sameEyes(c.apply(base,1000+Arrival::kEnd+1),base));
 Arrival d;d.observe(0,1,true,true);assert(d.observe(1000,2,true,true));
 const auto eyes=d.apply(base,1000+500),body=d.apply(base,1000+590,true);
 assert(fabsf(eyes.x-body.x)<.01f&&eyes.y<base.y-10); // looking up, scared
}

static void arrivalRoman() {
 // Extension in the same font: old letters stay, only the new letter falls.
 for(int from: {1,2,5,6,10,11,20}) {
  Arrival a;a.observe(0,from,true,true);assert(a.observe(100,from+1,true,true));
  char before[10],after[10];roman_counts::format(from,before);roman_counts::format(from+1,after);
  const bool sameFont=&fontFor(from)==&fontFor(from+1);
  const auto frame=drawArrival(a,100+600);
  assert(frame.size()==2&&frame[0].text==before&&!frame[0].flying&&frame[1].flying);
  if(sameFont)assert(frame[1].text==std::string(after+strlen(before)));
  // After landing, the kept letters and the piece tile the new label exactly.
  const auto landed=drawArrival(a,100+Arrival::kImpact+600);
  const float total=count_raster::width(fontFor(from+1),after);
  const float kept=count_raster::width(fontFor(from),before);
  if(sameFont) {
   assert(fabsf(landed[0].x-(216-total/2+kept/2))<.01f&&fabsf(landed[0].y-67.5f)<.01f);
   assert(fabsf(landed[1].y-67.5f)<.01f);
  }
 }
 // Rewrites (III->IV, VIII->IX, IX->X) drop the whole numeral and knock the old one out.
 for(int from: {3,4,8,9,39}) {
  Arrival a;a.observe(0,from,true,true);assert(a.observe(100,from+1,true,true));
  char after[10];roman_counts::format(from+1,after);
  const auto hanging=drawArrival(a,100+500);
  assert(hanging.size()==2&&hanging[1].text==after&&hanging[1].y<20);
  assert(fabsf(hanging[0].x-216)<.01f&&fabsf(hanging[0].y-67.5f)<.01f);
  const auto knocked=drawArrival(a,100+Arrival::kImpact+180);
  assert(knocked.size()==2&&knocked[0].y>80);
  const auto gone=drawArrival(a,100+Arrival::kImpact+800);
  assert(gone.size()==1&&fabsf(gone[0].y-67.5f)<.01f);
 }
 // First arrival: the whole numeral falls into the newly opened strip.
 Arrival first;first.observe(0,0,true,true);assert(first.observe(100,1,true,true));
 float last=-100;
 for(uint32_t t=Arrival::kFall;t<Arrival::kImpact;t+=10) {
  const auto f=drawArrival(first,100+t);
  assert(f.size()==1&&f[0].text=="I"&&f[0].y>=last);last=f[0].y;
 }
 // The teetering numeral is entirely on screen, not hidden behind the top edge.
 const auto teeter=drawArrival(first,100+600);
 assert(teeter[0].y-fontFor(1).capHeight/2.0f>=-1);
}

static void transferEdges() {
 Transfer t;
 assert(!t.observe(0,0,1,true,true));
 // Requires both directional edges and NO old done label.
 assert(!t.observe(100,0,2,true,true)&&!t.observe(200,1,2,true,true));
 Transfer paired;paired.observe(0,2,3,true,true);
 assert(!paired.observe(100,3,2,true,true)); // paired sorting owns this case
 Transfer u;u.observe(0,0,1,true,true);
 assert(!u.observe(100,1,0,true,false)&&!u.busy(100)); // not ready: no replay
 Transfer v;v.observe(0,0,3,true,true);
 assert(v.observe(1000,1,2,true,true)&&v.oldActive==3&&v.newDone==1&&v.newActive==2);
 // Old counts until the flash covers the screen; the swap is never visible.
 assert(v.displayCount(1000+Transfer::kCover-1,true,1)==0&&v.displayCount(1000+Transfer::kCover-1,false,2)==3);
 assert(v.displayCount(1000+Transfer::kCover,true,1)==1&&v.displayCount(1000+Transfer::kCover,false,2)==2);
 float peakGlow=0;
 for(uint32_t ms=0;ms<Transfer::kEnd+600;ms+=11) {
  const uint32_t now=1000+ms;
  v.update(now,true);
  const float glow=v.glow(now),wash=v.washTarget(now);
  assert(glow>=0&&glow<=1&&wash>=0&&wash<=1);
  peakGlow=fmaxf(peakGlow,glow);
  if(ms<Transfer::kCover)assert(wash==0&&v.labelAlpha(now,false)==1);
  if(ms>=Transfer::kCover&&ms<Transfer::kReveal+150)assert(v.labelAlpha(now,false)==0&&v.labelAlpha(now,true)==0);
  if(ms>=Transfer::kCover&&ms<Transfer::kLand)assert(wash==1);
  if(ms>=Transfer::kCover&&ms<Transfer::kLand+240)assert(v.labelAlpha(now,true)==0);
  if(ms>=Transfer::kEnd)assert(!v.busy(now)&&glow==0&&wash==0&&v.labelAlpha(now,true)==1);
 }
 assert(peakGlow==1);
 // Supersede, priority and recovery: no replay.
 Transfer w;w.observe(0,0,1,true,true);assert(w.observe(100,1,0,true,true));
 assert(!w.observe(500,1,1,true,true)&&!w.busy(500));
 Transfer x;x.observe(0,0,1,true,true);assert(x.observe(100,1,0,true,true));
 x.update(300,false);assert(!x.busy(300));
 assert(!x.observe(400,0,0,false,true)&&!x.observe(500,1,0,true,true));
}

static void transferMotion() {
 // Hidden means fully offscreen in every layout, including a 3x unused stretch.
 for(float corners: {0.0f,.5f,1.0f})for(float offset: {-36.0f,0.0f,36.0f}) {
  avatar_scene::Scene scene;scene.corners=corners;scene.offset=offset*(1-corners);
  scene.region=corners?144:168;scene.side=1;scene.eyeX=120+scene.offset;
  scene.away=Transfer::kHidden;
  assert(coveredPixels(scene)==0);
  int eyes=0;avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);
  avatar_scene::eyes(scene,mask,[&](int,int,float){++eyes;});
  assert(eyes==0);
 }
 for(int cadence: {16,33,50}) {
  Transfer t;t.observe(0,0,1,true,true);assert(t.observe(1000,1,0,true,true));
  int homes=0;float previous=0;bool entered=false;
  for(uint32_t ms=0;ms<Transfer::kEnd+800;ms+=cadence) {
   const uint32_t now=1000+ms;
   t.update(now,true);t.animate(now,cadence/1000.0f,148,0x2e2b);
   if(t.consumeHome(now))++homes;
   const float away=t.awayValue;
   assert(isfinite(away)&&away>=0&&away<=Transfer::kHidden);
   if(ms<Transfer::kCover)assert(away==0);
   if(ms>=Transfer::kCover&&ms<Transfer::kEnter)assert(away==Transfer::kHidden);
   // Careful entry: once visible, it only ever moves inward, never jumps.
   if(ms>=Transfer::kEnter+cadence) { assert(away<=previous+.001f);entered=true; }
   if(ms>=Transfer::kHome)assert(away==0);
   if(ms>=Transfer::kEnter+cadence&&ms<Transfer::kPeek)assert(previous-away<cadence*1.6f);
   previous=away;
  }
  assert(homes==1&&entered);
 }
 // Cancelling while hidden eases back instead of teleporting into place.
 Transfer c;c.observe(0,0,1,true,true);c.observe(1000,1,0,true,true);
 for(uint32_t now=1000;now<1000+Transfer::kCover+200;now+=33)c.animate(now,.033f,148,0);
 c.update(1000+Transfer::kCover+233,false);
 float away=c.awayValue;
 for(uint32_t now=1000+Transfer::kCover+233;now<1000+Transfer::kCover+3200;now+=33) {
  c.animate(now,.033f,148,0);assert(c.awayValue<=away);away=c.awayValue;
 }
 assert(away==0&&c.washValue==0);
 // The flipping numeral turns over once (edge-on, mirrored) and lands exactly.
 Transfer f;f.observe(0,0,1,true,true);f.observe(1000,1,0,true,true);f.source(1000,216,67.5f);
 float minScale=1,maxScale=-1;Drawn landed{"",0,0,false};
 for(uint32_t ms=Transfer::kReveal;ms<Transfer::kLand+300;ms+=5) {
  f.flip(1000+ms,fontFor(1),24,67.5f,[&](const count_raster::Font&,const char* text,float x,float y,
                                          float scale,float alpha) {
   assert(!strcmp(text,"I")&&alpha>=0&&alpha<=1&&y>0&&x>0&&x<240);
   minScale=fminf(minScale,scale);maxScale=fmaxf(maxScale,scale);
   landed={text,x,y,false};
  },[&](int,int,float){});
 }
 assert(minScale<-.99f&&maxScale>.99f&&fabsf(landed.x-24)<.01f&&fabsf(landed.y-67.5f)<.6f);
 // The final starburst frame covers essentially the whole screen.
 Transfer b;b.observe(0,0,1,true,true);b.observe(1000,1,0,true,true);b.source(1000,210,67.5f);
 static float cover[135][240];
 memset(cover,0,sizeof(cover));
 b.burst(1000+Transfer::kCover-1,[&](int x,int y,int n) {
  assert(y>=0&&y<135&&x>=0&&x+n<=240);for(int i=0;i<n;++i)cover[y][x+i]=1;
 },[&](int x,int y,float a){assert(x>=0&&x<240&&y>=0&&y<135&&a>0&&a<=1);cover[y][x]=fmaxf(cover[y][x],a);});
 int full=0;for(int y=0;y<135;++y)for(int x=0;x<240;++x)if(cover[y][x]>=.999f)++full;
 assert(full>240*135*95/100);
 int count=0;b.burst(1000+Transfer::kBurst-1,[&](int,int,int){++count;},[&](int,int,float){++count;});
 assert(count==0);
}

static void scaledLabels() {
 for(const char* text: {"I","XL","LXXVIII","XCIX+"})for(float scale: {.6f,.2f,-.5f,-.999f}) {
  const auto& font=kBadgeFont;
  bool fills=false;int left=1000,right=-1000;float fill=0,reference=0;
  count_raster::renderScaled(font,text,120,67,scale,[&](int x,int,float a,bool outline) {
   assert(a>0&&a<=1.0001f);
   if(!outline) { fills=true;fill+=a;left=x<left?x:left;right=x>right?x:right; }
   else assert(!fills); // every outline precedes every interior
  });
  count_raster::render(font,text,120,67,[&](int,int,float a,bool outline){if(!outline)reference+=a;});
  const float width=count_raster::width(font,text);
  if(width<=90) {
   assert(right-left+1<=fabsf(scale)*width+4);
   assert(fabsf(fill-reference*fabsf(scale))<reference*.06f+12);
  }
 }
 // Mirror: L's stem moves to the right-hand side of its own box.
 float leftMass=0,rightMass=0;
 count_raster::renderScaled(kBadgeFont,"L",120,67,-.999f,[&](int x,int,float a,bool outline) {
  if(!outline)(x<120?leftMass:rightMass)+=a;
 });
 assert(rightMass>leftMass*1.3f);
}

static void completionPairing() {
 using task_cues::CompletionPairing;
 const uint32_t W=CompletionPairing::kWindow;
 // Active falls first: hold, then present both edges together.
 CompletionPairing a;int d=0,n=1;
 assert(a.filter(1000,0,2,d,n,true)&&d==0&&n==2&&a.pending);
 d=0;n=1;assert(a.filter(3000,0,2,d,n,true)&&d==0&&n==2);   // repeated heartbeat
 d=1;n=1;assert(!a.filter(5000,0,2,d,n,true)&&d==1&&n==1&&!a.pending);
 // Done rises first, including to zero active (I -> done I).
 CompletionPairing b;d=1;n=2;
 assert(b.filter(0,0,2,d,n,true)&&d==0&&n==2);
 d=1;n=1;assert(!b.filter(2000,0,2,d,n,true)&&d==1&&n==1);
 CompletionPairing z;d=0;n=0;
 assert(z.filter(0,0,1,d,n,true)&&d==0&&n==1);
 // Expiry presents the held snapshot; due() lets the firmware release it
 // between snapshots. Counts are delayed, never dropped or invented.
 CompletionPairing e;d=0;n=1;assert(e.filter(0,0,2,d,n,true));
 assert(!e.due(W-1,true)&&e.due(W,true)&&e.done==0&&e.active==1);
 d=0;n=1;assert(!e.filter(W,0,2,d,n,true)&&d==0&&n==1&&!e.pending);
 // Not ready (request, recording, button) or unready later: release at once.
 CompletionPairing r;d=0;n=1;assert(!r.filter(0,0,2,d,n,false)&&n==1&&!r.pending);
 d=0;n=1;assert(r.filter(0,0,2,d,n,true)&&r.due(10,false));
 // Never hold paired/done-label, arrival, unchanged or reverted snapshots.
 CompletionPairing q;
 d=1;n=1;assert(!q.filter(0,1,2,d,n,true));        // done label exists: sorting owns it
 d=0;n=3;assert(!q.filter(0,0,2,d,n,true));        // active rise
 d=1;n=3;assert(!q.filter(0,0,2,d,n,true));        // done + arrival together
 d=0;n=2;assert(!q.filter(0,0,2,d,n,true));        // heartbeat
 d=0;n=0;assert(!q.filter(0,0,0,d,n,true));        // idle
 d=0;n=1;assert(q.filter(0,0,2,d,n,true));
 d=0;n=2;assert(!q.filter(500,0,2,d,n,true)&&!q.pending&&n==2); // reverted
 d=0;n=1;assert(q.filter(600,0,2,d,n,true));
 d=0;n=3;assert(!q.filter(800,0,2,d,n,true)&&n==3&&!q.pending); // other change
 // A further fall while held keeps the original window and presents both.
 CompletionPairing f;d=0;n=2;assert(f.filter(0,0,3,d,n,true));
 d=0;n=1;assert(f.filter(2000,0,3,d,n,true)&&f.since==0);
 d=1;n=1;assert(!f.filter(4000,0,3,d,n,true)&&d==1&&n==1);
}

static void splitCompletions() {
 // Gallery fixtures 30/31: II -> I + I reported over two polls 2s apart. The
 // transfer starts at the second poll from the ORIGINAL counts, and no green
 // or done numeral shows before the flash covers the swap.
 for(int mode: {30,31}) {
  Preview p(0,2,mode);bool started=false,covered=false;
  for(uint32_t ms=0;ms<=7400;ms+=33) {
   p.step(ms);
   if(ms<2600)assert(p.done==0&&p.active==2&&p.shownDone==0&&p.shownActive==2&&!p.transfer.busy(ms));
   if(p.transfer.busy(ms)) {
    started=true;
    assert(p.transfer.oldActive==2&&p.transfer.newDone==1&&p.transfer.newActive==1);
    if(p.transfer.phase(ms)<Transfer::kCover)assert(p.shownDone==0&&p.shownActive==2);
    else covered=true;
   }
  }
  assert(started&&covered&&!p.transfer.busy(7400)&&p.shownDone==1&&p.shownActive==1);
 }
 // Header widget: both orders play the transfer; a lone half is shown late.
 for(int order=0;order<3;++order) {
  HeaderAvatar avatar;auto& p=avatar.preview;
  for(uint32_t ms=0;ms<8000;ms+=33)avatar.frame(ms,2,0,0,false,true);
  bool transfer=false;
  for(uint32_t ms=8000;ms<16000;ms+=33) {
   const bool second=ms>=10000&&order<2;
   const int active=order==1&&!second?2:1,done=order==0&&!second?0:order==2?0:1;
   avatar.frame(ms,active,0,done,false,true);
   transfer=transfer||p.transfer.busy(ms);
   if(ms<10000&&order<2)assert(p.shownDone==0&&p.shownActive==2&&p.done==0&&p.active==2);
   if(order==2) {
    // Never completed: hold the lower count for the window, then show it.
    if(ms<8000+task_cues::CompletionPairing::kWindow)assert(p.active==2);
    if(ms>=8000+task_cues::CompletionPairing::kWindow+33)assert(p.active==1&&p.shownActive==1);
   }
  }
  assert(transfer==(order<2));
 }
 // A request in between releases the held half immediately, without a cue.
 HeaderAvatar r;
 for(uint32_t ms=0;ms<8000;ms+=33)r.frame(ms,2,0,0,false,true);
 r.frame(8000,1,0,0,false,true);assert(r.preview.active==2);
 r.frame(8033,2,1,0,false,true);assert(r.preview.active==2&&!r.preview.pairing.pending);
 r.frame(8066,1,1,1,false,true);assert(!r.preview.transfer.busy(8066));
}

static void pairedArrivalRoom() {
 // Single-counter layouts keep the authored 20px jerk exactly.
 {
  Preview p(0,2,21);float minAway=0;
  for(uint32_t ms=0;ms<=4000;ms+=33){p.step(ms);minAway=fminf(minAway,p.scene.away);}
  assert(minAway>=-20.001f&&minAway<-19);
 }
 // With both counters the wide face backs away from the corner while the
 // numeral teeters and stays clear of its column through the fall; the outer
 // eye stays on screen and everything returns exactly afterwards.
 for(int mode: {23,26})for(int cadence: {16,33,50}) {
  Preview p(0,2,mode);float previous=0,minAway=0,minEye=999;
  int clearFrames=0,checked=0;
  for(uint32_t ms=0;ms<=5200;ms+=cadence) {
   p.step(ms);
   const float away=p.scene.away;
   assert(isfinite(away)&&away<=0&&away>=-task_cues::Arrival::kMaxDodge-.001f);
   if(ms>static_cast<uint32_t>(cadence))assert(fabsf(away-previous)<14.0f*cadence/33+1);  // authored jerk only
   previous=away;minAway=fminf(minAway,away);
   if(p.arrival.busy(ms)) {
    minEye=fminf(minEye,p.scene.eyeX+p.scene.away-p.scene.gap/2-p.scene.eyeWidth);
    const uint32_t t=p.arrival.phase(ms);
    if(t>=task_cues::Arrival::kHang+150&&t<task_cues::Arrival::kImpact) {
     const auto& font=counter_labels::font(p.counterLabels.labels[1].index);
     const float restX=240-p.counterLabels.inset(1,(240-p.scene.region)/2,p.scene.corners);
     const auto& fromFont=&fontFor(p.arrival.from)==&fontFor(p.arrival.to)?font:fontFor(p.arrival.from);
     const float restY=avatar_scene::mix(67.5f,116,p.scene.corners);
     // The falling piece itself (with its 2px outline) must not land on the face.
     float worst=0;bool drawn=false;
     avatar_scene::Mask mask;avatar_scene::buildMask(p.scene,mask);
     p.arrival.draw(ms,fromFont,font,restX,restY,[&](const count_raster::Font& f,const char* text,
                                                    float x,float y,bool flying) {
      if(!flying)return;
      drawn=true;
      const float w=count_raster::width(f,text)/2+2,h=f.capHeight/2.0f+2;
      for(int py=static_cast<int>(y-h);py<=static_cast<int>(y+h);++py)
       for(int px=static_cast<int>(x-w);px<=static_cast<int>(x+w);++px)
        if(px>=0&&px<240&&py>=0&&py<135)worst=fmaxf(worst,mask.coverage(px,py));
     },[](int,int,float){});
     ++checked;if(drawn&&worst<.02f)++clearFrames;
     else fprintf(stderr,"mode %d cad %d t=%u worst %.3f corners %.2f\n",mode,cadence,t,worst,p.scene.corners);
    }
   }
  }
  assert(minAway<-40&&minEye>=0&&p.scene.away==0);
  assert(checked>0&&clearFrames==checked);
 }
}

static void headerCompositor() {
 // Live header: arrival and transfer from real-looking aggregates only.
 HeaderAvatar avatar;
 for(uint32_t ms=0;ms<12000;ms+=33) {
  avatar.frame(ms,1,0,0,false,true);
  assert(!avatar.preview.arrival.busy(ms)&&!avatar.preview.transfer.busy(ms));
 }
 avatar.frame(12000,2,0,0,false,true);
 assert(avatar.preview.arrival.busy(12000)&&!avatar.preview.reaction.active);
 for(uint32_t ms=12033;ms<16000;ms+=33)avatar.frame(ms,2,0,0,false,true);
 assert(!avatar.preview.arrival.busy(16000)&&avatar.preview.scene.away==0);
 avatar.frame(16000,1,0,1,false,true);
 auto& p=avatar.preview;
 assert(p.transfer.busy(16000)&&!p.flight.busy(16000)&&!p.reaction.active);
 assert(p.done==1&&p.active==1&&p.shownDone==0&&p.shownActive==2);
 bool hidden=false;
 for(uint32_t ms=16033;ms<16000+Transfer::kEnd+2000;ms+=33) {
  avatar.frame(ms,1,0,1,false,true);
  const uint32_t t=ms-16000;
  if(t>=Transfer::kCover+40&&t<Transfer::kEnter) { assert(coveredPixels(p.scene)==0);hidden=true; }
  if(t>=Transfer::kHome) assert(p.scene.away==0);
  if(t>=Transfer::kCover)assert(p.shownDone==1&&p.shownActive==1);
 }
 assert(hidden&&!p.transfer.busy(16000+Transfer::kEnd+2000));
 assert(avatar_behavior::pending(16000+Transfer::kEnd+100,avatar.celebrateUntil));
 // Requests and listening returns never start cues.
 avatar.frame(30000,2,1,1,false,true);assert(!p.arrival.busy(30000));
 avatar.frame(31000,2,0,1,false,true);assert(!p.arrival.busy(31000));
 avatar.frame(32000,2,0,1,true,true);avatar.frame(33000,3,0,1,false,true);
 assert(!p.arrival.busy(33000));
 avatar.frame(34000,4,0,1,false,true);assert(p.arrival.busy(34000));
 // Offline cancels without replay; recovery is a baseline.
 avatar.frame(34100,4,0,1,false,false);assert(!p.arrival.busy(34100));
 avatar.frame(35000,5,0,1,false,true);assert(!p.arrival.busy(35000));
}

int main() {
 assert(freopen("/dev/null","wb",stdout));
 arrivalEdges();arrivalRoman();transferEdges();transferMotion();scaledLabels();headerCompositor();
 completionPairing();splitCompletions();pairedArrivalRoom();
 fprintf(stderr,"task cues: arrival, Roman drop modes, transfer, flash, flip, header, split pairing, paired room ok\n");
 return 0;
}
