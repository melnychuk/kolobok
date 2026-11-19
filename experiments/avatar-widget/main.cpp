// Local command-driven desktop avatar. No serial, network, audio or task actions.
// Reuse the native preview compositor and all shared production rendering kernels.
#define main native_preview_main
#include "../../tests/preview_avatar.cpp"
#undef main
#include "../../firmware/avatar_behavior.h"

namespace {
struct HeaderAvatar {
 Preview preview{0,0,19};
 bool initialized=false,wasHealthy=false,wasListening=false;
 uint32_t now=0,idleSince=0,celebrateUntil=0;
 float awakeEyeWidth=50,awakeEyeHeight=56;
 int previousActive=0,previousDone=0;
 // Herdr can split one completion over two polls; callers whose snapshots are
 // always atomic (the product site) turn this off so plain decrements show at once.
 bool pairSplitCompletions=true;
 void frame(uint32_t ms,int active,int blocked,int done,bool listening,bool healthy,uint32_t background=0) {
  const float dt=initialized?fminf((ms-now)/1000.0f,.1f):0;
  now=ms;
  if(!healthy || listening)active=done=blocked=0;
  const bool baseline=initialized&&wasHealthy&&healthy&&!wasListening&&!listening;
  // Same pairing as the firmware: one completion split over two polls is
  // presented together so the transfer plays instead of a plain reflow.
  if(baseline&&pairSplitCompletions)preview.pairing.filter(ms,previousDone,previousActive,done,active,
      blocked==0&&!preview.button.active&&preview.shownActive>0);
  else preview.pairing.reset();
  if(!healthy||listening||blocked)celebrateUntil=ms;
  if(baseline&&!blocked&&done>previousDone) {
   celebrateUntil=ms+3600;preview.director.notifyCompletion();
  }
  const bool sorting=preview.flight.observe(ms,done,active,healthy&&!listening,
      initialized&&blocked==0&&preview.scene.corners>.95f&&
      !preview.reaction.active&&!preview.donePop.running&&!preview.activePop.running);
  // Staged task cues share the same priority edges as sorting. Recovery and a
  // listening return establish baselines; requests never start a cue.
  const bool cueReady=initialized&&blocked==0&&!sorting&&!preview.button.active;
  const bool transferred=preview.transfer.observe(ms,done,active,healthy&&!listening,cueReady);
  const bool arrived=preview.arrival.observe(ms,active,healthy&&!listening,
      cueReady&&!transferred&&!preview.transfer.busy(ms));
  // Opening the last unread completion: step over the green, never fade it.
  preview.stepOver.observe(ms,done,active,healthy&&!listening,
      cueReady&&!transferred&&!arrived&&!preview.transfer.busy(ms)&&!preview.arrival.busy(ms)&&
      preview.scene.corners<.05f&&preview.shownDone>0);
  // Celebrate after the face is home again, not while it is offscreen.
  if(transferred)celebrateUntil=ms+task_cues::Transfer::kEnd+3600;
  preview.healthy=healthy;
  if(!initialized) {
   preview.donePop.observed=done;preview.activePop.observed=active;
   previousActive=active;previousDone=done;idleSince=ms;
  } else if(baseline&&!sorting&&!transferred&&!arrived&&blocked==0) {
   if(active>previousActive || done>previousDone) {
    const bool completed=done>previousDone;
    preview.reaction.notify(completed?-1:1,completed,ms);
    preview.surface.kick(completed?-1:1,.9f);
   }
  }
  previousActive=active;previousDone=done;wasHealthy=healthy;wasListening=listening;
  if(active||done||blocked||listening||!healthy)idleSince=ms;
  const auto mode=avatar_behavior::select({active,blocked,done,healthy,listening},
      avatar_behavior::pending(ms,celebrateUntil),!initialized||ms-idleSince<4000);
  preview.liveMode=mode;
  preview.done=done;preview.active=active;preview.state=avatar_behavior::state(mode);
  preview.player.calm=!avatar_behavior::pending(ms,celebrateUntil);
  // Like the firmware layout, a charging transfer keeps its old counts until
  // the flash covers the swap; otherwise the band reflows in mid-charge.
  const int heldDone=preview.stepOver.displayCount(ms,preview.transfer.displayCount(ms,true,done));
  const int heldActive=preview.transfer.displayCount(ms,false,active);
  const int shownDone=preview.flight.displayCount(ms,true,heldDone);
  const int shownActive=preview.flight.displayCount(ms,false,heldActive);
  const float strip=fmaxf(shownDone?stripWidth(shownDone):0,shownActive?stripWidth(shownActive):0);
  const bool paired=(shownDone&&shownActive)||blocked||preview.flight.busy(ms);
  auto& scene=preview.scene;
  const float region=heldActive||heldDone?240-2*strip:280;
  const float offset=paired||listening?0:(shownDone?strip/2:shownActive?-strip/2:0);
  scene.region=initialized?approach(scene.region,region,dt,.15f):region;
  scene.corners=initialized?approach(scene.corners,paired?1:0,dt,avatar_scene::kReflowSeconds):paired?1:0;
  scene.offset=initialized?approach(scene.offset,offset,dt,avatar_scene::kShiftSeconds):offset;
  scene.side=initialized?approach(scene.side,heldActive||heldDone?1:0,dt,.16f):heldActive||heldDone?1:0;
  scene.alert=initialized?approach(scene.alert,blocked?1:0,dt,.18f):blocked?1:0;
  scene.awakeHeight=avatar_scene::mix(56,44,scene.alert);
  scene.gap=avatar_scene::mix(19,15.2f,scene.alert);
  scene.eyeWidth=awakeEyeWidth;scene.eyeHeight=awakeEyeHeight;
  preview.step(ms);
  awakeEyeWidth=scene.eyeWidth;awakeEyeHeight=scene.eyeHeight;
  const float sleepTarget=mode==avatar_behavior::Mode::sleeping?1:0;
  // Like firmware, a startle snaps the sleeping layout open (35ms) instead
  // of the ordinary slow wake.
  scene.sleep=approach(scene.sleep,sleepTarget,dt,
                       sleepTarget?.7f:preview.startle.active?.035f:.18f);
  scene.eyeWidth*=avatar_scene::mix(1,.65f,scene.sleep);
  scene.eyeHeight=avatar_scene::mix(scene.eyeHeight,4,scene.sleep);
  scene.eyeY+=8*scene.sleep;
  initialized=true;
  preview.render(false,background);
 }
 // Synthetic loud-noise startle (no microphone here). As on the device, it
 // wakes a dozing face: hold ordinary idle for 4s, then it dozes off again.
 void startle(float side) {
  preview.startle.notify(now,1,side<0?-1:1);
  preview.surface.kick(0,1);
  idleSince=now;
 }
};
} // namespace

#ifndef STICK_AVATAR_WIDGET_NO_MAIN
int main() {
 HeaderAvatar avatar;
 char line[128];
 while(fgets(line,sizeof(line),stdin)) {
  if(!strchr(line,'\n'))return 2;
  unsigned ms,background=0;int active,blocked,done,listening,healthy;char extra;
  const int fields=sscanf(line,"%u %d %d %d %d %d %u %c",&ms,&active,&blocked,&done,&listening,&healthy,&background,&extra);
  if((fields!=6 && fields!=7) || background>0xffffffu ||
     (fields==6 && sscanf(line,"%u %d %d %d %d %d %c",&ms,&active,&blocked,&done,&listening,&healthy,&extra)!=6) ||
     active<0||active>10000||blocked<0||blocked>active||done<0||done>10000||
     listening<0||listening>1||healthy<0||healthy>1 || (avatar.initialized&&ms<avatar.now))return 2;
  avatar.frame(ms,active,blocked,done,listening,healthy,background);
  if(fflush(stdout)!=0)return 1;
 }
 return ferror(stdin)?1:0;
}
#endif
