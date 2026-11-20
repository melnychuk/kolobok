#include <assert.h>
#include <math.h>
#include <initializer_list>
#include "../firmware/avatar_director.h"
#include "../experiments/qemu-sticks3/measured_frame_envelopes.h"
using namespace avatar_behavior;
static void same(const eye_acting::Pose& a,const eye_acting::Pose& b) {
#define EQ(f) assert(fabsf(a.f-b.f)<.00001f)
 EQ(x);EQ(y);EQ(width);EQ(height);EQ(upperL);EQ(upperR);EQ(smile);EQ(closeL);EQ(closeR);
 EQ(bodyWidth);EQ(bodyHeight);EQ(lean);EQ(lift);EQ(flow);EQ(tilt);
 EQ(scaleL);EQ(scaleR);EQ(lowerL);EQ(lowerR);EQ(slantL);EQ(slantR);
#undef EQ
}
static void finite(const eye_acting::Pose& p) {
 assert(isfinite(p.x)&&isfinite(p.y)&&isfinite(p.width)&&isfinite(p.flow));
 assert(fabsf(p.x)<40&&fabsf(p.y)<40&&p.width>0&&p.height>0);
 assert(p.closeL>=0&&p.closeL<=1&&p.closeR>=0&&p.closeR<=1);
}
int main() {
 const Mode modes[]={Mode::offline,Mode::idle,Mode::sleeping,Mode::working,Mode::waiting,Mode::finished,Mode::listening};
 // Gaze targets point at each counter's status_sides screen side.
 const float doneAt=120+96.0f*status_sides::kDone,activeAt=120+96.0f*status_sides::kActive;
 const auto t=targets(120,50,doneAt,activeAt,116,true,true);
 assert(t.doneX*status_sides::kDone>0&&t.activeX*status_sides::kActive>0&&t.doneY>0&&t.activeY>0);
 // Active only: the face fills the done side and looks across at the count.
 const auto vertical=targets(120+30.0f*status_sides::kDone,67,doneAt,activeAt,67,false,true);
 assert(vertical.doneX==0&&vertical.activeX*status_sides::kActive>0&&vertical.activeY==0);
 // A glance never crosses the center toward the other side.
 assert(targets(120,50,activeAt,doneAt,116,true,true).doneX==0);
 // Every directed state edge, at 20/30/60fps and across uint32 wrap.
 unsigned cases=0;
 for(auto from:modes)for(auto to:modes)if(from!=to)
 for(uint32_t cadence: {17u,33u,50u})for(uint32_t clock: {1000u,UINT32_MAX-400u}) {
  Director d;eye_acting::Player p;
  d.step(from,p,clock-200,1,false,t);
  auto entry=rest(from);entry.x=6;entry.y=-3;entry.lean=2;
  auto previous=entry;previous.x-=.33f;
  d.capture(clock-33,previous,previous);d.capture(clock,entry,entry);
  const auto first=d.step(to,p,clock,2,false,t);
  assert(first.transitioning);same(first.eyes,entry);same(first.body,entry);
  // Inherited x velocity is approximately 10px/s, not a neutral reset/snap.
  const auto tangent=d.sample(1,false);
  assert(fabsf((tangent.x-entry.x)*1000-10)<.5f);
  const auto frozen=d.sample(240,false);
  for(uint32_t elapsed=cadence;elapsed<d.length+90u;elapsed+=cadence) {
   const auto frame=d.step(to,p,clock+elapsed,2,false,t);
   finite(frame.eyes);finite(frame.body);
   same(frame.body,d.sample(elapsed>90?elapsed-90:0,true));
   d.capture(clock+elapsed,frame.eyes,frame.body);
   same(d.sample(240,false),frozen); // capture must not move the active curve
  }
  const auto last=d.step(to,p,clock+d.length+90u,2,false,t);
  assert(!last.transitioning);same(last.eyes,rest(to));same(last.body,rest(to));
  ++cases;
 }
 assert(cases==252);
 // Interrupt every 50ms of a completion with input/listening/offline. Redirect
 // from the composed pose; never finish or replay the interrupted celebration.
 for(Mode urgent: {Mode::waiting,Mode::listening,Mode::offline})
 for(uint32_t at=0;at<1000;at+=50) {
  Director d;eye_acting::Player p;d.step(Mode::working,p,0,1,false,t);
  d.capture(100,rest(Mode::working),rest(Mode::working));d.notifyCompletion();
  d.step(Mode::finished,p,100,2,false,t);
  const auto current=d.step(Mode::finished,p,100+at,2,false,t);
  d.capture(100+at,current.eyes,current.body);
  const auto redirected=d.step(urgent,p,100+at,3,false,t);
  same(redirected.eyes,current.eyes);same(redirected.body,current.body);
  assert(d.mode==urgent&&!d.completion);
 }
 // An external cue owns its full performance. No hidden transition tail later.
 Director d;eye_acting::Player p;d.step(Mode::working,p,0,1,false,t);
 d.notifyCompletion();d.step(Mode::finished,p,200,2,false,t,true);
 assert(!d.busy(200)&&!d.completion);
 auto external=eye_acting::p(-12,5,1.04f,1.08f,0,0,.7f,1,0);
 d.capture(500,external,external);
 auto released=d.step(Mode::finished,p,500,3,false,t);
 same(released.eyes,external);same(released.body,external);
 assert(!released.transitioning);
 // Host acting tests are not physical occupancy calibration. Changed source,
 // binary and transition layouts may never inherit historical vertical costs.
 for(const auto* envelope:stick_measured::all) {
  assert(!stick_measured::findEnvelope("continuity-source",envelope->binarySha256,"O2",envelope->mode));
  assert(!stick_measured::findEnvelope(envelope->sourceSha256,"continuity-binary","O2",envelope->mode));
  assert(!stick_measured::findEnvelope(envelope->sourceSha256,envelope->binarySha256,envelope->optimization,"transition"));
 }
 // Seeded phrasing/clip order is independent of the frame RNG and cadence.
 int referenceIndex[40]={};uint16_t referencePause[40]={};
 for(uint32_t cadence: {17u,33u,50u}) {
  Director phrase;eye_acting::Player actor;
  uint32_t previousStart=UINT32_MAX;int count=0;bool quiet=false,pair=false;
  for(uint32_t now=0;count<40;now+=cadence) {
   const auto frame=phrase.step(Mode::working,actor,now,now*1664525u,false,t);
   phrase.capture(now,frame.eyes,frame.body);
   if(actor.started==previousStart)continue;
   previousStart=actor.started;
   quiet=quiet||actor.pause>eye_acting::Player::kMaxPause;pair=pair||actor.pause==700;
   if(cadence==17) { referenceIndex[count]=actor.index;referencePause[count]=actor.pause; }
   else assert(referenceIndex[count]==actor.index&&referencePause[count]==actor.pause);
   ++count;
  }
  assert(quiet&&pair);
 }
 assert(Director::reason(Mode::offline,Mode::finished,false)==Beat::reconnect);
 assert(Director::reason(Mode::waiting,Mode::working,false)==Beat::resume);
 assert(Director::reason(Mode::working,Mode::finished,true)==Beat::complete);
}
