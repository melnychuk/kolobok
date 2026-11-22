// Scripted fixtures for the clip and event gallery (scripts/preview-avatar.py)
// and for tests. Each fixture is a script of inputs: counts over time, a
// button, a startle, dictation. They are played through the desktop avatar,
// which runs the Stick's own frame code (firmware/avatar_frame.h), so every
// fixture shows what the device would. A fixture can also hold one authored
// clip, as the site's clip picker does.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STICK_AVATAR_WIDGET_NO_MAIN
#include "../experiments/avatar-widget/main.cpp"
#undef STICK_AVATAR_WIDGET_NO_MAIN

// Count-label geometry for tests and the gallery: the side font a count fits
// in, its width, and the status strip it needs beside the face.
inline const count_raster::Font* const* fonts() {
 static const count_raster::Font* const all[]={&kBadgeFont,&kMediumBadgeFont,&kSmallBadgeFont,&kTinyBadgeFont,&kCompactBadgeFont};
 return all;
}
inline const count_raster::Font& fontFor(int count) {
 char label[10];roman_counts::format(count,label);
 for(int i=0;i<4;++i)if(count_raster::width(*fonts()[i],label)<=56)return *fonts()[i];
 return *fonts()[4];
}
inline float fittedLabelWidth(int count) {
 char label[10];roman_counts::format(count,label);
 return count_raster::width(fontFor(count),label);
}
inline int stripWidth(int count) {
 return static_cast<int>(fmaxf(48,fittedLabelWidth(count)+16));
}

namespace {
// Fixture time 0 comes after at least this long on the fixture's starting
// counts, so a fixture opens on its settled layout, with the face already in
// the requested state.
constexpr uint32_t kSettleMs=6000;
constexpr uint32_t kFrameMs=33;

// One gallery fixture: an authored clip (state, index) and a layout/event mode.
//  0 paired II+III            1 done II only            2 active III only
//  3 long Romans XCIX+LXXXVIII
//  4 new active count          5 a task completes        6 a new task (vertical)
//  7 counter checks (fixed timeline)                     8 short press (wink)
//  9 dictation release (nod)  10 done only + checks     11 active only + checks
// 12 done I + checks          13 active I + checks      14/15 checks with a kiss
// 16-18 completion sorting: 16 II+III, 17 long Romans, 18 I+I
// 19 the live header (no fixture events)
// 20-26 task cues at 600ms: 20 idle->I, 21 II->III, 22 III->IV, 23 done II +
//       active I, 24 active I->done I, 25 active III->done I+II, 26 II+II->II+III
// 27/28 a loud noise (paired II+III / active II)
// 29 the last completion opened: step over the green
// 30/31 one completion split over two polls 2s apart (active first / done first)
struct Preview {
 HeaderAvatar avatar;
 avatar_frame::Avatar& stick=avatar.stick;
 eye_acting::State state;
 int index,mode;
 int done=2,active=3,blocked=0;
 bool listening=false,eventFired=false;
 uint32_t origin=0;  // Avatar time of fixture time 0.
 Preview(int s,int i,int layout):state(static_cast<eye_acting::State>(s)),index(i),mode(layout) {
  avatar.emit=false;
  if(mode==1||mode==10||mode==14)active=0;
  if(mode==2||mode==11)done=0;
  if(mode==12){done=1;active=0;}
  if(mode==13){done=0;active=1;}
  if(mode==3||mode==17){done=99;active=88;}
  if(mode==18){done=active=1;}
  if(mode==4)active=0;
  if(mode==5||mode==6)done=0;
  if(mode==20){done=active=0;}
  if(mode==21){done=0;active=2;}
  if(mode==22){done=0;active=3;}
  if(mode==23){done=2;active=0;}
  if(mode==24){done=0;active=1;}
  if(mode==25){done=0;active=3;}
  if(mode==26){done=2;active=2;}
  if(mode==28){done=0;active=2;}
  if(mode==29){done=2;active=0;}
  if(mode==30||mode==31){done=0;active=2;}
  if(state==eye_acting::State::listening){done=active=0;listening=true;}
  if(state==eye_acting::State::attention){blocked=1;if(active<1)active=1;}
  // Counter checks on their deterministic 8.4s timeline (the device schedules
  // them at random); the kiss fixtures start one cycle in.
  const bool checks=mode==7||(mode>=10&&mode<=15);
  stick.counterChecks.fixed=checks;
  if(mode==14||mode==15)stick.counterChecks.actionCycle=1;
  // Settle, then hand the requested clip to the actor once the director has
  // brought the face into that state (as the site's stick_play does).
  uint32_t t=0;
  for(;t<kSettleMs||(!ready()&&t<kSettleMs*3);t+=kFrameMs)feed(t);
  origin=t;
  if(mode!=19&&ready()) {
   auto& actor=stick.actor;
   actor.entryEyes=actor.eyes(origin);actor.entryBody=actor.body(origin);
   actor.index=index;actor.started=origin;
   actor.used[static_cast<int>(state)]|=static_cast<uint16_t>(1u<<index);
   stick.director.quietHold=false;stick.director.pairReturn=false;
  }
  if(checks) { stick.counterChecks.started=origin;stick.counterChecks.previousPhase=0; }
 }
 bool ready() const {
  return !stick.director.running&&stick.actor.state==state&&stick.actor.index>=0;
 }
 uint32_t now() const { return avatar.now; }
 void feed(uint32_t t) {
  // Done clips play in the finished face, which otherwise lasts only as long
  // as a celebration after a completion.
  if(state==eye_acting::State::done&&!eventFired)stick.celebrateUntil=t+3600;
  avatar.frame(t,active,blocked,done,listening,true);
 }
 // Advance to fixture time `ms` (from 0, monotonic) and draw that frame.
 void step(uint32_t ms) {
  if(!eventFired&&ms>=600) {
   if(mode==8||mode==9) { eventFired=true;avatar.button(mode==9); }
   if(mode==4||mode==6) { eventFired=true;active=mode==6?4:3; }
   if(mode==5) { eventFired=true;done=2;active=2; }
   if(mode>=16&&mode<=18) { eventFired=true;++done;--active; }
   if(mode==27||mode==28) { eventFired=true;avatar.startle(mode==27?1:-1); }
   if(mode==29) { eventFired=true;done=0; }
   if(mode>=20&&mode<=26) {
    eventFired=true;
    if(mode==24){active=0;done=1;}
    else if(mode==25){active=2;done=1;}
    else active=mode==20||mode==23?1:mode==21?3:mode==22?4:3;
   }
  }
  if(mode==30||mode==31) {
   done=0;active=2;
   if(ms>=600) { if(mode==30)active=1; else done=1; }
   if(ms>=2600) { done=1;active=1; }
   eventFired=ms>=2600;
  }
  feed(origin+ms);
 }
 // The frame `step` drew: raw RGB, or a PPM image.
 void render(bool ppm) const {
  if(ppm)printf("P6\n240 135\n255\n");
  avatar.canvas.output(0,avatar.colors.body);
 }
};
}  // namespace

int main(int argc,char** argv) {
 if(argc==2 && strcmp(argv[1],"--list")==0) {
  printf("[");
  for(int s=0;s<4;++s)for(int i=0;i<eye_acting::clipCount(static_cast<eye_acting::State>(s));++i) {
   const auto& c=eye_acting::clip(static_cast<eye_acting::State>(s),i);
   if(s||i)printf(",");
   printf("{\"state\":%d,\"index\":%d,\"name\":\"%s\",\"duration\":%u,\"keys\":[",s,i,c.name,eye_acting::duration(c));
   for(int k=0;k<c.count;++k)printf("%s%u",k?",":"",c.keys[k].ms);
   printf("]}");
  }
  printf("]\n");return 0;
 }
 if(argc<3)return 2;
 const int state=static_cast<int>(strtol(argv[1],nullptr,10)),index=static_cast<int>(strtol(argv[2],nullptr,10));
 if(state<0||state>3||index<0||index>=eye_acting::clipCount(static_cast<eye_acting::State>(state)))return 2;
 const int at=argc>3?static_cast<int>(strtol(argv[3],nullptr,10)):-1;
 const int mode=argc>4?static_cast<int>(strtol(argv[4],nullptr,10)):0;
 static Preview preview(state,index,mode);
 const uint32_t end=at>=0?static_cast<uint32_t>(at):mode==29?2600:mode>=30?7400:mode>=20?5200:mode>=16?4600:(mode==7||mode>=10)?9000:mode>=4?4600:
                    eye_acting::duration(eye_acting::clip(preview.state,index))+700;
 for(uint32_t ms=0;ms<=end;ms+=kFrameMs) {
  preview.step(ms);
  if(at<0)preview.render(false);
 }
 if(at>=0){preview.step(end);preview.render(true);}
 return ferror(stdout)?1:0;
}
