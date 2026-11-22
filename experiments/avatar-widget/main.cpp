// The desktop avatar: the Stick's own frame (firmware/avatar_frame.h), drawn
// into an RGB frame for the product site and the Herdr Desktop widget. No
// serial, network, audio or task actions: the caller supplies the counts.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/avatar_frame.h"
#include "../../firmware/framebuffer_raster.h"

namespace {
// Desktop display options; the device and the site never set them. A widget
// that redraws frames as native shapes pays per color and per rectangle, so it
// can ask for fewer blended edge colors. Coverage is rounded to `alphaLevels`
// steps (1 = hard edges); 0 keeps full anti-aliasing.
namespace widget_display {
int alphaLevels=0;
}

// avatar_frame's drawing target: plain RGB565, blended exactly as the Stick
// blends its own frame buffer (framebuffer_raster::View, unswapped).
struct WidgetCanvas {
 static constexpr int kWidth=avatar_frame::kScreenWidth,kHeight=avatar_frame::kScreenHeight;
 uint16_t pixels[kWidth*kHeight]={};
 uint16_t rows[4][kWidth]={};
 framebuffer_raster::View view;
 WidgetCanvas() {
  view.pixels=pixels;view.width=kWidth;view.height=kHeight;view.stride=kWidth;view.swapped=false;
 }
 void ink(int x,int y,uint16_t color,float alpha) {
  const int levels=widget_display::alphaLevels;
  if(levels>0)alpha=floorf(alpha*levels+.5f)/levels;
  view.ink(x,y,color,alpha);
 }
 void run(int x,int y,int length,uint16_t color) {
  uint16_t* row=pixels+y*kWidth+x;
  for(int i=0;i<length;++i)row[i]=color;
 }
 void backdrop(int x,int y,uint16_t color) { rows[y][x]=color; }
 void fillBackdrop() {
  for(int y=0;y<kHeight;++y)memcpy(pixels+y*kWidth,rows[y&3],sizeof(rows[0]));
 }
 void mark(frame_profile::Metric) {}
 // RGB888 to stdout. A host face color comes out exactly, not as its RGB565
 // neighbor, so the face matches the sidebar around it.
 void output(uint32_t hostRgb,uint16_t host565) const {
  unsigned char bytes[kWidth*kHeight*3];
  for(int i=0;i<kWidth*kHeight;++i) {
   const uint16_t c=pixels[i];
   if(hostRgb && c==host565) {
    bytes[i*3]=(hostRgb>>16)&255;bytes[i*3+1]=(hostRgb>>8)&255;bytes[i*3+2]=hostRgb&255;
    continue;
   }
   bytes[i*3]=((c>>11)&31)*255/31;bytes[i*3+1]=((c>>5)&63)*255/63;bytes[i*3+2]=(c&31)*255/31;
  }
  fwrite(bytes,1,sizeof(bytes),stdout);
 }
};

inline uint16_t rgb565(uint32_t rgb) {
 return static_cast<uint16_t>((((rgb>>16)&0xf8)<<8)|(((rgb>>8)&0xfc)<<3)|((rgb&0xff)>>3));
}

struct HeaderAvatar {
 avatar_frame::Avatar stick;  // Everything a frame shows, as on the device.
 WidgetCanvas canvas;
 avatar_palette::Palette colors=avatar_palette::get(false);
 // A seeded synthetic voice drives the listening wave; there is no microphone.
 listen_wave::Synth voice;
 bool light=false;  // Light theme (avatar_palette); dark by default.
 // Herdr can split one completion over two polls; callers whose snapshots are
 // always atomic (the product site) turn this off so plain decrements show at once.
 bool pairSplitCompletions=true;
 bool initialized=false;
 uint32_t now=0;
 HeaderAvatar() { stick.palette=&colors; }
 HeaderAvatar(const HeaderAvatar&)=delete;  // `stick` points at `colors`.
 HeaderAvatar& operator=(const HeaderAvatar&)=delete;
 // ms must be monotonic. blocked<=active ("needs you" is a subset of active
 // work). `background`, when nonzero, is the host's color for the face.
 void frame(uint32_t ms,int active,int blocked,int done,bool listening,bool healthy,uint32_t background=0) {
  now=ms;
  colors=avatar_palette::get(light);
  if(background)colors.body=rgb565(background);
  stick.lightMode=light;stick.palette=&colors;
  // Dictation, as on the device: a fresh start hands the stage to the face.
  if(listening&&!stick.voiceRecording) { stick.buttonCue.active=false;voice.primed=false; }
  stick.voiceRecording=listening;
  stick.voiceLevelOverride=listening?voice.level(ms):0;
  if(!healthy) {
   if(stick.bridgeConnected)stick.disconnect();
  } else if(pairSplitCompletions) {
   stick.report(active,blocked,done,ms);
   stick.releasePairing(ms);
  } else {
   stick.applyCounts(active,blocked,done,ms);
  }
  const float seconds=stick.update(ms);
  stick.draw(canvas,ms,seconds);
  initialized=true;
  canvas.output(background,colors.body);
 }
 // Front button A: a short tap winks (and wakes); after dictation it nods.
 void button(bool voiceEnd) {
  stick.buttonCue.notify(now,voiceEnd);
  if(!voiceEnd)stick.lastTaskAt=now;
 }
 // A loud noise (the device's microphone detector), from the left (<0) or the
 // right. The next frame plays it when the face may startle; it also wakes a
 // dozing face for the ordinary idle hold.
 void startle(float side) {
  stick.pendingStartle=1;
  stick.startleSide=side<0?-1:1;
 }
 // Someone is looking: hold idle-awake instead of dozing after four seconds.
 void keepAwake() { stick.lastTaskAt=now; }
};
} // namespace

#ifndef STICK_AVATAR_WIDGET_NO_MAIN
int main() {
 static HeaderAvatar avatar;
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
