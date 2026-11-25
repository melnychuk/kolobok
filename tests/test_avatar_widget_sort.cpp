// Full command-driven compositor regression, using synthetic aggregates only.
#include <assert.h>
#define main native_preview_main
#include "preview_avatar.cpp"
#undef main
int main() {
 assert(freopen("/dev/null","wb",stdout));
 HeaderAvatar avatar;
 for(uint32_t ms=0;ms<20000;ms+=33) {
  avatar.frame(ms,3,0,2,false,true);
  assert(!avatar.stick.counterFlight.busy(ms)); // no idle timer or synthetic fixture
 }
 const uint32_t start=20000;
 avatar.frame(start,2,0,3,false,true);
 assert(avatar.stick.counterFlight.busy(start));
 assert(avatar.stick.doneCount==3&&avatar.stick.activeCount==2); // real data updates immediately
 assert(avatar.stick.shownDone==2&&avatar.stick.shownActive==3);
 const uint32_t swap=avatar_interaction::CounterFlight::kSwap;
 for(uint32_t ms=start+33;ms<start+swap;ms+=33) {
  avatar.frame(ms,2,0,3,false,true);
  assert(avatar.stick.shownDone==2&&avatar.stick.shownActive==3);
  assert(!avatar.stick.ui.done.pop.running&&!avatar.stick.ui.active.pop.running);
 }
 avatar.frame(start+swap,2,0,3,false,true);
 assert(avatar.stick.shownDone==3&&avatar.stick.shownActive==2);
 auto gathered=avatar.stick.counterFlight.offset(start+swap,true,24,216,40,40);
 assert(gathered.y==-avatar_interaction::CounterFlight::kCarry);
 // Both blobs hold their numerals: the drawn body overlaps each label's top.
 assert(avatar.stick.scene.hands.reach[0]==1&&avatar.stick.scene.hands.reach[1]==1);
 {
  avatar_scene::Mask mask;avatar_scene::buildMask(avatar.stick.scene,mask);
  for(int i=0;i<2;++i) {
   const auto& hands=avatar.stick.scene.hands;
   const int x=static_cast<int>(hands.x[i]),y=static_cast<int>(hands.y[i]);
   assert(mask.coverage(x,y-2)==1);
   assert(mask.coverage(x,y+3)==0); // only slightly: the label stays readable
  }
 }
 const uint32_t end=start+avatar_interaction::CounterFlight::kEnd+150;
 for(uint32_t ms=start+swap+33;ms<end;ms+=33)avatar.frame(ms,2,0,3,false,true);
 assert(!avatar.stick.counterFlight.busy(end));
 assert(avatar.stick.scene.hands.reach[0]==0&&avatar.stick.scene.hands.reach[1]==0);
 assert(avatar.stick.shownDone==3&&avatar.stick.shownActive==2);
 assert(!avatar.stick.ui.done.pop.running&&!avatar.stick.ui.active.pop.running);
 // The last active numeral vanishes at the apex, not its blue backdrop.
 HeaderAvatar last;
 for(uint32_t ms=0;ms<5000;ms+=33)last.frame(ms,1,0,1,false,true);
 const uint32_t pickup=5000;
 last.frame(pickup,0,0,2,false,true);
 assert(last.stick.counterFlight.busy(pickup));
 bool covered=false,released=false;
 for(uint32_t ms=pickup+33;ms<pickup+6000;ms+=33) {
  last.frame(ms,0,0,2,false,true);
  auto& preview=last.stick;
  const float singleOffset=stripWidth(preview.shownDone)/2.0f;
  // Done only: the face slides over the (now empty) active side.
  covered=covered||(preview.scene.corners<.05f&&
                    preview.scene.offset*status_sides::kActive>.95f*singleOffset);
  if(ms>=pickup+avatar_interaction::CounterFlight::kSwap)assert(preview.shownActive==0);
  if(!covered)assert(preview.backgroundDeparture.presence[1]==1);
  else {
   avatar_scene::Mask mask;avatar_scene::buildMask(preview.scene,mask);
   const int activeEdge=status_sides::kActive>0?239:0;
   for(int y=0;y<135;++y)assert(mask.baseCoverage(activeEdge,y)==1);
  }
  if(preview.backgroundDeparture.presence[1]<.01f) {
   assert(covered);released=true;
  }
 }
 assert(covered&&released); // holding the background must not block reflow
 // Priority cancellation clears the retained color rather than leaking it into
 // listening/offline/request layouts; a returning active count also releases it.
 status_background::SortingDeparture departure;
 departure.update(1,0,true,true,false,false,false,true,.033f);
 assert(departure.held[1]&&departure.presence[1]==1);
 departure.update(0,0,false,false,false,false,false,false,.033f);
 assert(!departure.held[1]&&departure.presence[1]==0);
 departure.update(1,0,true,true,false,false,false,true,.033f);
 departure.update(1,1,false,true,true,false,false,true,.033f);
 assert(!departure.held[1]&&departure.presence[1]==1);
 // Requests suppress an edge and cannot replay it later.
 avatar.frame(24000,1,1,4,false,true);assert(!avatar.stick.counterFlight.busy(24000));
 avatar.frame(26000,1,0,4,false,true);assert(!avatar.stick.counterFlight.busy(26000));
 // Offline and recording cancel staging and reveal current data, never stale
 // counts. As on the device, recording keeps the real counts (hidden) rather
 // than zeroing them.
 avatar.frame(28000,0,0,5,false,true);assert(avatar.stick.counterFlight.busy(28000));
 avatar.frame(28100,0,0,5,true,true);assert(!avatar.stick.counterFlight.busy(28100));
 avatar.frame(28133,0,0,5,true,true);
 assert(avatar.stick.shownDone==5&&avatar.stick.shownActive==0);
 avatar.frame(29000,3,0,2,false,false);assert(!avatar.stick.counterFlight.busy(29000));
 avatar.frame(30000,2,0,3,false,true);assert(!avatar.stick.counterFlight.busy(30000));
 assert(!avatar.stick.countReaction.active);
 assert(avatar.stick.director.beat==avatar_behavior::Beat::reconnect);
 assert(!avatar_behavior::pending(30000,avatar.stick.celebrateUntil));
 // A listening interval consumes the aggregate baseline, not the unread work.
 avatar.frame(31000,2,0,3,true,true);
 avatar.frame(32000,2,0,3,false,true);
 assert(!avatar.stick.countReaction.active&&!avatar.stick.counterFlight.busy(32000));
 assert(!avatar_behavior::pending(32000,avatar.stick.celebrateUntil));
 // Fresh completion while work remains: celebrate once, then return to work.
 avatar.frame(33000,2,0,4,false,true);
 assert(avatar.stick.face==avatar_behavior::Mode::finished);
 avatar.frame(37000,2,0,4,false,true);
 assert(avatar.stick.face==avatar_behavior::Mode::working);
 // Intentional rhythm on the live compositor: a check leaves only after the
 // clip's return has been held kRestLead, and the next clip departs only
 // kHoldSettleMs after the check (with its body follow-through) has ended.
 {
  HeaderAvatar live;live.emit=false;
  uint32_t clipStart=0,checkEnd=0;int checks=0,clips=0;bool wasBusy=false;
  for(uint32_t ms=0;ms<300000;ms+=33) {
   live.frame(ms,3,0,2,false,true);
   auto& s=live.stick;
   const bool busy=s.counterChecks.busy(ms);
   if(busy&&!wasBusy&&ms>8000) {
    assert(s.actor.restAge(ms)>=avatar_interaction::Checks::kRestLead);++checks;
   }
   if(!busy&&wasBusy)checkEnd=ms;
   if(s.actor.started!=clipStart) {
    clipStart=s.actor.started;
    if(checkEnd&&ms>checkEnd)assert(ms-checkEnd>=avatar_behavior::Director::kHoldSettleMs);
    ++clips;
   }
   wasBusy=busy;
  }
  assert(checks>=15&&clips>=50);
 }
 return 0;
}
