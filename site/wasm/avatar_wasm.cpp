// WebAssembly entry for the product site. It compiles the SAME header-widget
// compositor (experiments/avatar-widget) and every shared firmware kernel, then
// exposes a tiny frame/event API to JavaScript. No firmware sources change:
// stdout writes are redirected into a static RGB buffer by macro.
#include <stdio.h>
#include <stdint.h>
#include <string.h>

namespace site_io {
constexpr int kFrameBytes=240*135*3;
unsigned char frame[kFrameBytes];
size_t write(const void* data,size_t size,size_t count,FILE*) {
 const size_t bytes=size*count;
 if(bytes==sizeof(frame))memcpy(frame,data,bytes);
 return count;
}
int print(const char*,...) { return 0; }
} // namespace site_io

#define fwrite site_io::write
#define printf site_io::print
#define STICK_AVATAR_WIDGET_NO_MAIN
#include "../../experiments/avatar-widget/main.cpp"
#undef fwrite
#undef printf

namespace {
HeaderAvatar* avatar=nullptr;
int forcedState=-1,forcedIndex=-1;
uint32_t forcedAt=0;
char nameBuffer[64];

void applyForcedClip() {
 if(!avatar||forcedState<0)return;
 auto& p=avatar->preview;
 // Wait for the director to finish its handoff into the requested mode.
 if(p.director.running||p.player.state!=static_cast<eye_acting::State>(forcedState)||p.player.index<0) {
  if(avatar->now-forcedAt>4000)forcedState=-1;  // Gave up: state never arrived.
  return;
 }
 p.player.entryEyes=p.player.eyes(avatar->now);
 p.player.entryBody=p.player.body(avatar->now);
 p.player.index=forcedIndex;
 p.player.started=avatar->now;
 p.player.used[forcedState]|=static_cast<uint16_t>(1u<<forcedIndex);
 p.director.quietHold=false;p.director.pairReturn=false;
 forcedState=-1;
}
} // namespace

extern "C" {
__attribute__((export_name("stick_init"))) void stick_init() {
 delete avatar;
 avatar=new HeaderAvatar();
 // The site sets counts atomically, so never hold half of a "split" completion:
 // that made playground decrements (with no done count) stick for up to 4.6s.
 avatar->pairSplitCompletions=false;
 forcedState=-1;
}
__attribute__((export_name("stick_frame_ptr"))) unsigned char* stick_frame_ptr() { return site_io::frame; }
// Optional display hint: the four repeating RGB565 status rows under the face.
// This is read-only and does not change the site's normal RGB frame output.
__attribute__((export_name("stick_backdrop_ptr"))) const uint16_t* stick_backdrop_ptr() {
 return avatar?avatar->preview.backdrop[0]:nullptr;
}
// ms must be monotonic. blocked<=active ("needs you" is a subset of active work).
__attribute__((export_name("stick_frame")))
void stick_frame(uint32_t ms,int active,int blocked,int done,int listening,int healthy,uint32_t background) {
 if(!avatar)stick_init();
 if(blocked>active)blocked=active;
 avatar->frame(ms,active,blocked,done,listening!=0,healthy!=0,background);
 applyForcedClip();
}
// Front button A: a short tap winks; release after dictation nods.
__attribute__((export_name("stick_button"))) void stick_button(int voiceEnd) {
 if(!avatar)return;
 avatar->preview.button.notify(avatar->now,voiceEnd!=0);
}
// Loud-noise startle (the device uses its microphone level detector). It
// also wakes a dozing face for a few seconds, like the device.
__attribute__((export_name("stick_startle"))) void stick_startle(float side) {
 if(!avatar)return;
 avatar->startle(side);
}
__attribute__((export_name("stick_play"))) void stick_play(int state,int index) {
 if(state<0||state>3)return;
 const int count=eye_acting::clipCount(static_cast<eye_acting::State>(state));
 if(index<0||index>=count)return;
 forcedState=state;forcedIndex=index;forcedAt=avatar?avatar->now:0;
}
__attribute__((export_name("stick_clip_count"))) int stick_clip_count(int state) {
 return state<0||state>3?0:eye_acting::clipCount(static_cast<eye_acting::State>(state));
}
__attribute__((export_name("stick_clip_name"))) const char* stick_clip_name(int state,int index) {
 nameBuffer[0]=0;
 if(state<0||state>3||index<0||index>=stick_clip_count(state))return nameBuffer;
 strncpy(nameBuffer,eye_acting::clip(static_cast<eye_acting::State>(state),index).name,sizeof(nameBuffer)-1);
 nameBuffer[sizeof(nameBuffer)-1]=0;
 return nameBuffer;
}
__attribute__((export_name("stick_clip_duration"))) int stick_clip_duration(int state,int index) {
 if(index<0||index>=stick_clip_count(state))return 0;
 return eye_acting::duration(eye_acting::clip(static_cast<eye_acting::State>(state),index));
}
// Currently playing clip, for the UI caption: state*100+index, or -1.
__attribute__((export_name("stick_current_clip"))) int stick_current_clip() {
 if(!avatar||avatar->preview.player.index<0||avatar->preview.director.running)return -1;
 const auto& p=avatar->preview.player;
 if(!avatar_behavior::scripted(avatar->preview.liveMode))return -1;
 return static_cast<int>(p.state)*100+p.index;
}
__attribute__((export_name("stick_mode"))) int stick_mode() {
 return avatar?static_cast<int>(avatar->preview.liveMode):0;
}
// Site-only: someone is looking. Holds idle-awake (no tasks) instead of the
// 4s doze, like a person nearby; the sleep chapter simply stops calling it.
__attribute__((export_name("stick_keep_awake"))) void stick_keep_awake() {
 if(avatar)avatar->idleSince=avatar->now;
}
}
