// Native RGB565 preview of the SAME acting, fluid field, layout mask and eye
// raster kernels as firmware. No hardware, task mutation, microphone or network.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/avatar_scene.h"
#include "../firmware/display_fonts.h"
#include "../firmware/roman_counts.h"
#include "../firmware/counter_space.h"
#include "../firmware/counter_labels.h"
#include "../firmware/status_background.h"
#include "../firmware/count_pop.h"
#include "../firmware/avatar_interaction.h"
#include "../firmware/eye_life.h"
#include "../firmware/avatar_director.h"
#include "../firmware/task_cues.h"
#include "../firmware/request_scene.h"
#include "../firmware/avatar_palette.h"

namespace {
constexpr int W=240,H=135;
uint16_t rgb(int r,int g,int b) { return ((r&248)<<8)|((g&252)<<3)|(b>>3); }
uint16_t blend(uint16_t a,uint16_t b,float t) {
 if(t<=0)return a;
 if(t>=1)return b;
 const int r=((a>>11)&31)+(((b>>11)&31)-((a>>11)&31))*t+.5f;
 const int g=((a>>5)&63)+(((b>>5)&63)-((a>>5)&63))*t+.5f;
 const int bl=(a&31)+((b&31)-(a&31))*t+.5f;
 return (r<<11)|(g<<5)|bl;
}
float approach(float v,float target,float dt,float tau) { return avatar_scene::approach(v,target,dt,tau); }
// Desktop display options; the device and the site never set them. A widget
// that redraws frames as native shapes pays per color and per rectangle, so it
// can ask for fewer blended edge colors. Coverage is rounded to `alphaLevels`
// steps (1 = hard edges); 0 keeps full anti-aliasing.
namespace display {
int alphaLevels=0;
}
struct Canvas {
 uint16_t pixels[W*H]={};
 void ink(int x,int y,uint16_t color,float alpha=1) {
  if(display::alphaLevels>0)alpha=floorf(alpha*display::alphaLevels+.5f)/display::alphaLevels;
  if(x>=0&&x<W&&y>=0&&y<H)pixels[y*W+x]=blend(pixels[y*W+x],color,alpha);
 }
 void output(bool ppm,uint32_t darkRgb=0) {
  if(ppm)printf("P6\n240 135\n255\n");
  unsigned char bytes[W*H*3];
  const uint16_t dark565=rgb((darkRgb>>16)&255,(darkRgb>>8)&255,darkRgb&255);
  for(int i=0;i<W*H;++i) {
   const uint16_t c=pixels[i];
   if(darkRgb && c==dark565) {
    bytes[i*3]=(darkRgb>>16)&255;bytes[i*3+1]=(darkRgb>>8)&255;bytes[i*3+2]=darkRgb&255;
    continue;
   }
   bytes[i*3]=((c>>11)&31)*255/31;bytes[i*3+1]=((c>>5)&63)*255/63;bytes[i*3+2]=(c&31)*255/31;
  }
  fwrite(bytes,1,sizeof(bytes),stdout);
 }
};
const count_raster::Font* fonts[]={&kBadgeFont,&kMediumBadgeFont,&kSmallBadgeFont,&kTinyBadgeFont,&kCompactBadgeFont};
const count_raster::Font& fontFor(int count) {
 char label[10];roman_counts::format(count,label);
 for(int i=0;i<4;++i)if(count_raster::width(*fonts[i],label)<=56)return *fonts[i];
 return *fonts[4];
}
float fittedLabelWidth(int count) {
 char label[10];roman_counts::format(count,label);
 return count_raster::width(fontFor(count),label);
}
int stripWidth(int count) {
 return static_cast<int>(fmaxf(48,fittedLabelWidth(count)+16));
}
template<typename Background>
void label(Canvas& canvas,int count,const count_raster::Font& font,float cx,float cy,Background background,
           const count_pop::Pop& pop,uint32_t now,count_raster::Cache& cache,
           const avatar_interaction::CounterSpring& spring,float alpha=1,float shakeX=0,float shakeY=0,
           const avatar_scene::Mask* stepping=nullptr,uint16_t ink=0) {
 if(count<=0||alpha<=0)return;
 char text[10];roman_counts::format(count,text);
 cy+=pop.lift(now);
 cache.render(font,text,cx,cy,[&](int x,int y,float a,bool outline) {
  // A stepping foot covers the numeral instead of the label floating above it.
  const float covered=stepping?stepping->stepCoverage(x,y):0;
  canvas.ink(x,y,outline?background(x,y):ink,a*alpha*(1-covered));
 },spring.x+shakeX,spring.y+shakeY);
}
struct Preview {
 eye_acting::State state;
 int index,mode,done=2,active=3,shownDone=2,shownActive=3;
 bool healthy=true;
 bool light=false;  // Light theme (avatar_palette); the site never sets it.
 avatar_scene::Scene scene;
 counter_space::State counterSpace;
 counter_labels::State counterLabels;
 eye_acting::Player player;
 avatar_behavior::Director director;
 avatar_behavior::Mode liveMode=avatar_behavior::Mode::working;
 face_physics::Surface surface;
 face_physics::Reaction reaction;
 count_pop::Pop donePop,activePop;
 count_raster::Cache doneRaster,activeRaster;
 status_background::RowCache backgroundCache;
 status_background::SortingDeparture backgroundDeparture;
 uint16_t backdrop[4][W]={};
 avatar_interaction::CounterSpring doneSpring,activeSpring;
 avatar_interaction::Checks checks;
 avatar_interaction::CounterFlight flight;
 avatar_interaction::ButtonCue button;
 avatar_interaction::Startle startle;  // modes 27/28: synthetic loud-noise trigger
 // Staged task cues: a new active numeral drops in; active->done flashes/flips.
 task_cues::Arrival arrival;
 task_cues::Transfer transfer;
 task_cues::StepOver stepOver;  // done-only -> zero: cover the strip, don't fade it
 task_cues::CompletionPairing pairing;  // one completion split over two polls
 request_scene::Hops hops;  // small request hops below the `!`
 count_raster::Cache flyRaster;
 eye_life::Life life;
 eye_life::SizeSpring eyeSize;
 // Listening soundwave. No microphone here: a seeded synthetic voice.
 listen_wave::Wave wave;
 listen_wave::Synth voice;
 float seconds=0;
 bool eventFired=false;
 uint32_t previous=0;
 // Resting numeral center for status slot 0 (done) / 1 (active) on its
 // status_sides screen side, and the shared label baseline.
 float counterX(int slot) const { return counterLabels.x(slot,(240-scene.region)/2,scene.corners); }
 float counterY() const { return avatar_scene::mix(67.5f,116,scene.corners); }
 Preview(int s,int i,int layout):state(static_cast<eye_acting::State>(s)),index(i),mode(layout) {
  player.state=state;player.index=index;player.started=0;
  if(mode==1||mode==10||mode==14)active=0;
  if(mode==14||mode==15)checks.actionCycle=1;
  // Event fixtures keep the deterministic 8.4s check timeline; the live
  // header widget (mode 19) uses the randomized clip-boundary scheduler.
  checks.fixed=mode!=19;
  if(mode==2||mode==11)done=0;
  if(mode==12){done=1;active=0;}
  if(mode==13){done=0;active=1;}
  if(mode==3||mode==17){done=99;active=88;}
  if(mode==18){done=active=1;}
  if(mode==4)active=0;
  if(mode==5||mode==6)done=0;
  // Task-cue fixtures (event at 600ms): 20 idle->I, 21 II->III, 22 III->IV,
  // 23 done II + active I, 24 active I->done I, 25 active III->done I+II,
  // 26 paired II+II -> II+III.
  if(mode==20){done=active=0;}
  if(mode==21){done=0;active=2;}
  if(mode==22){done=0;active=3;}
  if(mode==23){done=2;active=0;}
  if(mode==24){done=0;active=1;}
  if(mode==25){done=0;active=3;}
  if(mode==26){done=2;active=2;}
  // Startle fixtures (synthetic trigger at 600ms): 27 paired II+III, 28 active II.
  if(mode==28){done=0;active=2;}
  // Step-over fixture: done-only II opened at 600ms -> idle.
  if(mode==29){done=2;active=0;}
  // Split completion fixtures: active II -> done I + active I reported over
  // two polls 2s apart. 30: active falls first; 31: done rises first.
  if(mode==30||mode==31){done=0;active=2;}
  if(s==3){done=active=0;scene.side=0;}
  donePop.observed=done;activePop.observed=active;
  scene.surface=&surface;
  const float side=fmaxf(done?stripWidth(done):0,active?stripWidth(active):0);
  scene.region=s==3||(!done&&!active)?280:240-2*side;
  if(!done&&!active)scene.side=0;
  scene.corners=(done&&active)||s==2?1:0;
  // A single counter: the face fills the opposite side of the screen.
  scene.offset=scene.corners||s==3?0:(done?status_sides::kActive:status_sides::kDone)*side/2;
  scene.alert=s==2?1:0;
  scene.awakeHeight=s==2?44:56;
  scene.eyeHeight=scene.awakeHeight;
  scene.gap=s==2?15.2f:19;
 }
 void step(uint32_t ms) {
  const float dt=(ms-previous)/1000.0f;previous=ms;seconds=dt;
  if((mode==8||mode==9)&&!eventFired&&ms>=600) {
   eventFired=true;button.notify(ms,mode==9);
  }
  if(mode>=4 && mode<=6 && !eventFired && ms>=600) {
   eventFired=true;
   if(mode==4||mode==6) {
    active=mode==6?4:3;surface.kick(status_sides::kActive,.90f);reaction.notify(status_sides::kActive,false,ms);
   }
   else {
    done=2;active=2;
    player.entryEyes=scene.eyes;player.entryBody=scene.body;
    state=eye_acting::State::done;player.state=state;player.index=index;player.started=ms;
    surface.kick(status_sides::kDone,.95f);reaction.notify(status_sides::kDone,true,ms);
   }
  }
  if(mode>=16&&mode<=18&&!eventFired&&ms>=600) {
   eventFired=true;++done;--active;
  }
  if((mode==27||mode==28)&&!eventFired&&ms>=600) {
   eventFired=true;startle.notify(ms,1,mode==27?1:-1);surface.kick(0,1);
  }
  if(mode==29&&!eventFired&&ms>=600) { eventFired=true;done=0; }
  if(mode>=20&&mode<=26&&!eventFired&&ms>=600) {
   eventFired=true;
   if(mode==24){active=0;done=1;}
   else if(mode==25){active=2;done=1;}
   else active=mode==20||mode==23?1:mode==21?3:mode==22?4:3;
  }
  const bool cueAllowed=healthy&&!button.active&&state!=eye_acting::State::attention&&
                        state!=eye_acting::State::listening;
  if(mode==30||mode==31) {
   int nextDone=0,nextActive=2;
   if(ms>=600) { if(mode==30)nextActive=1; else nextDone=1; }
   if(ms>=2600) { nextDone=1;nextActive=1; }
   pairing.filter(ms,done,active,nextDone,nextActive,cueAllowed&&!flight.busy(ms));
   done=nextDone;active=nextActive;
   eventFired=ms>=2600;
  }
  if(mode!=19) { // the live header observes its own snapshots
   transfer.observe(ms,done,active,healthy,cueAllowed&&!flight.busy(ms)&&!reaction.active);
   arrival.observe(ms,active,healthy,cueAllowed&&!flight.busy(ms)&&!transfer.busy(ms));
   stepOver.observe(ms,done,active,healthy,cueAllowed&&!flight.busy(ms)&&!transfer.busy(ms)&&
                    !arrival.busy(ms)&&scene.corners<.05f);
  }
  transfer.update(ms,cueAllowed);arrival.update(ms,cueAllowed);stepOver.update(ms,cueAllowed);
  if((mode==24||mode==25||mode==30||mode==31)&&eventFired&&!transfer.busy(ms)&&
     state!=eye_acting::State::done) {
   player.entryEyes=scene.eyes;player.entryBody=scene.body;
   state=eye_acting::State::done;player.state=state;player.index=index;player.started=ms;
  }
  flight.observe(ms,done,active,healthy,scene.corners>.95f&&
                 !button.active&&!reaction.active&&!donePop.running&&!activePop.running&&
                 state!=eye_acting::State::attention&&state!=eye_acting::State::listening);
  flight.update(ms,healthy&&!button.active&&!reaction.active&&
                state!=eye_acting::State::attention&&state!=eye_acting::State::listening);
  shownDone=stepOver.displayCount(ms,transfer.displayCount(ms,true,flight.displayCount(ms,true,done)));
  shownActive=transfer.displayCount(ms,false,flight.displayCount(ms,false,active));
  const int done=shownDone,active=shownActive;
  if((mode>=4&&mode<=6)||(mode>=16&&mode<=18)||mode>=20) {
   const float side=fmaxf(done?stripWidth(done):0,active?stripWidth(active):0);
   const bool paired=(done&&active)||scene.alert>0||flight.busy(ms);
   scene.region=approach(scene.region,done||active?240-2*side:280,dt,.16f);
   scene.side=approach(scene.side,done||active?1:0,dt,.16f);
   scene.corners=approach(scene.corners,paired?1:0,dt,avatar_scene::kReflowSeconds);
   scene.offset=approach(scene.offset,paired?0:(done?status_sides::kActive:active?status_sides::kDone:0)*side/2,
                         dt,avatar_scene::kShiftSeconds);
  }
  const bool sorting=flight.busy(ms),beforeSwap=sorting&&!flight.swapped(ms);
  counterLabels.update(done,active,scene.corners,
      sorting?(beforeSwap?this->done:flight.oldDone):-1,
      sorting?(beforeSwap?this->active:flight.oldActive):-1);
  const bool staged=sorting||arrival.busy(ms)||transfer.busy(ms)||stepOver.busy(ms);
  if(sorting) {
   flight.fit(counterLabels.flightWidth(0),counterLabels.flightWidth(1));
   donePop.observed=done;activePop.observed=active;
   donePop.running=activePop.running=false;
   donePop.pending=activePop.pending=false;
  }
  donePop.observe(done);activePop.observe(active);
  donePop.update(ms,true);activePop.update(ms,true);
  button.update(ms,state==eye_acting::State::attention||state==eye_acting::State::listening);
  startle.update(ms,state==eye_acting::State::listening||button.active);
  avatar_behavior::Performance performance={player.eyes(ms),player.body(ms),false};
  if(mode==19) {
   const auto targets=avatar_behavior::targets(120+scene.offset+scene.counterShift,avatar_scene::eyeBaseline(scene.corners,scene.alert,0),
       counterX(0),counterX(1),counterY(),done>0,active>0);
   performance=director.step(liveMode,player,ms,ms*1664525u+1013904223u,checks.busy(ms),targets,
                             staged||button.active);
   if(performance.transitioning||staged||button.active)reaction.active=false;
  }
  checks.update(ms,(mode==7||(mode>=10&&mode<20))&&!performance.transitioning&&!staged&&!button.active&&!reaction.active&&
                   state!=eye_acting::State::attention&&state!=eye_acting::State::listening,
                   &surface,done>0,active>0,checks.fixed||player.resting(ms));
  const auto authoredEyes=performance.eyes;
  scene.body=checks.apply(performance.body,ms,true);
  scene.eyes=checks.apply(authoredEyes,ms);
  scene.body=flight.apply(scene.body,ms,true);
  scene.eyes=flight.apply(scene.eyes,ms);
  scene.body=arrival.apply(scene.body,ms,true);
  scene.eyes=arrival.apply(scene.eyes,ms);
  {
   const float lookY=avatar_scene::mix(2,9,scene.corners);
   const float lookX=14.0f*status_sides::kDone;
   scene.body=transfer.apply(scene.body,ms,true,lookX,lookY);
   scene.eyes=transfer.apply(scene.eyes,ms,false,lookX,lookY);
  }
  scene.body=stepOver.apply(scene.body,ms,true);
  scene.eyes=stepOver.apply(scene.eyes,ms);
  scene.body=button.apply(scene.body,ms,true);
  scene.eyes=button.apply(scene.eyes,ms);
  scene.body=startle.apply(scene.body,ms,true);
  scene.eyes=startle.apply(scene.eyes,ms);
  {
   const bool request=mode==19?liveMode==avatar_behavior::Mode::waiting:state==eye_acting::State::attention;
   hops.update(ms,request,!button.active&&!startle.active,ms*2654435761u);
  }
  scene.body=reaction.body(scene.body,ms,state==eye_acting::State::attention);
  scene.eyes=reaction.apply(scene.eyes,ms,state==eye_acting::State::attention);
  // Same life layer as firmware: only during authored eye holds, not cues.
  const bool scripted=mode!=19||avatar_behavior::scripted(liveMode);
  const auto segment=scripted?player.segment(ms):eye_acting::Segment{};
  const bool cued=!eye_acting::sameEyes(authoredEyes,scene.eyes);
  const eye_life::Mood& mood=mode==19?avatar_behavior::mood(liveMode):
                             state==eye_acting::State::attention?eye_life::kAttention:
                             state==eye_acting::State::done?eye_life::kDone:
                             state==eye_acting::State::listening?eye_life::kListening:eye_life::kThinking;
  if(performance.transitioning) { life.weight=0;life.blinking=false; }
  else scene.eyes=life.apply(scene.eyes,ms,dt,mood,(!scripted||segment.still)&&!cued&&scene.sleep<.5f,
                        scene.sleep<.3f,scripted?&segment:nullptr);
  if(mode==19)director.capture(ms,scene.eyes,scene.body);
  scene.thinking=state==eye_acting::State::thinking;
  // One thud when a dropped numeral lands; one when the face hops home.
  if(arrival.consumeImpact(ms))surface.kick(0,.75f);
  if(transfer.consumeHome(ms))surface.kick(0,.9f);
  if(const float thud=stepOver.consumeImpact(ms))surface.kick(stepOver.side,thud);
  surface.step(dt,scene.body,scene.corners>.5f);
  const float height=fmaxf(avatar_scene::kRequestEyeFloor*scene.alert,scene.awakeHeight*scene.eyes.height);
  if(!eyeSize.primed)eyeSize.reset(scene.eyeWidth,scene.eyeHeight);
  eyeSize.step(dt,50*scene.eyes.width,height);
  scene.eyeWidth=eyeSize.width;scene.eyeHeight=eyeSize.height;
  transfer.animate(ms,dt,120+scene.offset+scene.counterShift,avatar_palette::get(light).done);
  float dodgeRoom=task_cues::Arrival::kDodge;
  if(arrival.busy(ms)) {
   // Same fall column as the drawn drop: clear it, not just a fixed 20px.
   const auto& toFont=counter_labels::font(counterLabels.labels[1].index);
   const auto& fromFont=&fontFor(arrival.from)==&fontFor(arrival.to)?toFont:fontFor(arrival.from);
   dodgeRoom=task_cues::Arrival::room(scene,arrival.fallInner(fromFont,toFont,counterX(1)),
                                      counterY()-toFont.capHeight/2.0f);
  }
  const float arrivalDodge=arrival.dodge(ms,dodgeRoom);
  scene.away=arrivalDodge+transfer.awayValue;
  stepOver.animate(ms,dt);
  scene.stepSide=stepOver.side;scene.stepProgress=stepOver.reachValue;
  scene.eyeX=120+scene.offset+scene.eyes.x+surface.x*.45f;
  scene.eyeX+=task_cues::Arrival::eyeReturn(arrivalDodge,scene.eyeX+scene.counterShift,
      (scene.gap/2+scene.eyeWidth)*scene.counterScale);
  scene.eyeY=avatar_scene::eyeBaseline(scene.corners,scene.alert,0)+scene.eyes.y+surface.y*.55f;
  scene.hop=hops.height(ms)*scene.alert;scene.hopStretch=hops.stretch(ms)*scene.alert;
  {
   const bool listening=mode==19?liveMode==avatar_behavior::Mode::listening:
                        state==eye_acting::State::listening;
   if(!listening)voice.primed=false;  // each dictation starts a fresh phrase
   wave.update(dt,ms,listening,listening?voice.level(ms):0);
   avatar_scene::setWave(scene,wave);
  }
  scene.flowClock=fluid_face::advance(scene.flowClock,dt*(state==eye_acting::State::attention?.95f:state==eye_acting::State::done?.55f:.60f)*scene.body.flow);
  avatar_scene::Mask mask;
  const float doneX=counterX(0),activeX=counterX(1),cy=counterY();
  const float doneWidth=counterLabels.labels[0].width,activeWidth=counterLabels.labels[1].width;
  const auto& doneFont=counter_labels::font(counterLabels.labels[0].index);
  const auto& activeFont=counter_labels::font(counterLabels.labels[1].index);
  counterSpace.update(scene,done,active,counterLabels.sideWidth(0),counterLabels.sideWidth(1),dt,healthy&&
      state!=eye_acting::State::attention&&state!=eye_acting::State::listening&&
      (mode!=19||liveMode!=avatar_behavior::Mode::sleeping));
  transfer.source(ms,activeX,cy);
  flight.hands(ms,doneX,activeX,doneWidth,activeWidth,cy,doneFont.capHeight,
               activeFont.capHeight,scene.hands);
  checks.shape(scene,ms,doneX,activeX,cy,doneWidth,activeWidth,
               doneFont.capHeight,activeFont.capHeight);
  avatar_scene::buildMask(scene,mask);
  const float singleOffset=fmaxf(done?stripWidth(done):0,active?stripWidth(active):0)/2;
  const bool reclaimed=scene.corners<.05f && fabsf(scene.offset)>.95f*singleOffset;
  backgroundDeparture.update(done?1.0f:0.0f,active?1.0f:0.0f,
      flight.busy(ms),this->done>0,this->active>0,
      reclaimed&&scene.offset*status_sides::kDone>0,reclaimed&&scene.offset*status_sides::kActive>0,
      healthy&&state!=eye_acting::State::attention&&state!=eye_acting::State::listening,dt);
  doneSpring.update(seconds,avatar_interaction::contactDepth(mask,doneX,cy,
      doneWidth,doneFont.capHeight,scene.corners,status_sides::kDone),
      done>0&&!staged&&state!=eye_acting::State::attention&&state!=eye_acting::State::listening,
      status_sides::kDone);
  activeSpring.update(seconds,avatar_interaction::contactDepth(mask,activeX,cy,
      activeWidth,activeFont.capHeight,scene.corners,status_sides::kActive),
      active>0&&!staged&&state!=eye_acting::State::attention&&state!=eye_acting::State::listening,
      status_sides::kActive);
 }
 void render(bool ppm,uint32_t darkRgb=0) {
  const avatar_palette::Palette& colors=avatar_palette::get(light);
  // A host color (the Herdr sidebar) replaces the palette's body color.
  const uint16_t dark=darkRgb?rgb((darkRgb>>16)&255,(darkRgb>>8)&255,darkRgb&255):colors.body;
  const int done=shownDone,active=shownActive;
  Canvas canvas;
  const uint16_t green=colors.done,blue=colors.working,coral=colors.blocked,eye=colors.eye,ink=colors.ink;
  const uint16_t activeFill=blend(mode==19?blend(blue,coral,scene.alert):scene.alert?coral:blue,
                                  task_cues::kEnergy,transfer.glow(previous));
  const bool portrait=scene.alert && !(done&&active);
  const status_background::State backgroundState={green,activeFill,done?green:activeFill,
                                                 backgroundDeparture.presence[0],backgroundDeparture.presence[1],
                                                 portrait?(mode==19?scene.alert:1.0f):0.0f,
                                                 transfer.washTint,transfer.washValue,
                                                 request_scene::field(previous,colors.request,colors.requestPeak),
                                                 scene.alert,light?dark:static_cast<uint16_t>(0)};
  backgroundCache.update(backgroundState,[&](int x,int y,uint16_t color){backdrop[y][x]=color;});
  const auto background=[&](int x,int y) {
   const uint16_t color=x>=0&&x<W?backdrop[y&3][x]:static_cast<uint16_t>(0);
   return color==0?dark:color;
  };
  for(int y=0;y<H;++y)for(int x=0;x<W;++x)canvas.pixels[y*W+x]=background(x,y);
  // The `!` sits in the request field; the rising head may pass in front.
  if(scene.alert>0)request_scene::sign(previous,[&](int x,int y,float a){canvas.ink(x,y,ink,a*scene.alert);});
  const float doneX=counterX(0),activeX=counterX(1),cy=counterY();
  avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);
  mask.raster([&](int x,int y,int length) {
   for(int i=0;i<length;++i)canvas.ink(x+i,y,dark);
  },[&](int x,int y,float alpha){canvas.ink(x,y,dark,alpha);});
  avatar_scene::eyes(scene,mask,[&](int x,int y,float alpha){canvas.ink(x,y,eye,alpha);},
                     [&](int x,int y,int n){for(int i=0;i<n;++i)canvas.ink(x+i,y,eye);});
  avatar_scene::mouth(scene,mask,[&](int x,int y,float alpha){canvas.ink(x,y,eye,alpha);});
  const float doneWidth=counterLabels.labels[0].width,activeWidth=counterLabels.labels[1].width;
  const auto& doneFont=counter_labels::font(counterLabels.labels[0].index);
  const auto& activeFont=counter_labels::font(counterLabels.labels[1].index);
  const auto doneFlight=flight.offset(previous,true,doneX,activeX,doneWidth,activeWidth);
  const auto activeFlight=flight.offset(previous,false,activeX,doneX,activeWidth,doneWidth);
  // Requests hide the counts (visually only) behind the pulsing field.
  const float shown=1-scene.alert;
  if(mode==19&&done>0&&!flight.busy(previous)&&!transfer.busy(previous)&&!stepOver.busy(previous))
   director.accents(previous,doneX,cy+donePop.lift(previous)-doneFont.capHeight/2-2,
                    [&](int x,int y,float a){canvas.ink(x,y,green,a*shown);});
  const auto light=[&](int x,int y,float a){canvas.ink(x,y,task_cues::kFlash,a);};
  label(canvas,done,doneFont,doneX+doneFlight.x,cy+doneFlight.y,background,donePop,previous,doneRaster,doneSpring,
        transfer.labelAlpha(previous,true)*shown,0,0,mask.stepSide?&mask:nullptr,ink);
  if(arrival.busy(previous)) {
   // Resting layout font for the new label; keep extensions in one font.
   const auto& fromFont=&fontFor(arrival.from)==&fontFor(arrival.to)?activeFont:fontFor(arrival.from);
   arrival.draw(previous,fromFont,activeFont,activeX+activeFlight.x,cy+activeFlight.y,
       [&](const count_raster::Font& font,const char* text,float x,float y,bool flying) {
    (flying?flyRaster:activeRaster).render(font,text,x,y,[&](int px,int py,float a,bool outline) {
     canvas.ink(px,py,outline?background(px,py):ink,a);
    });
   },light);
  } else {
   label(canvas,active,activeFont,activeX+activeFlight.x,cy+activeFlight.y,background,activePop,previous,activeRaster,
         activeSpring,transfer.labelAlpha(previous,false)*shown,transfer.shakeX(previous),transfer.shakeY(previous),
         nullptr,ink);
  }
  transfer.crackles(previous,light);
  transfer.flip(previous,doneFont,doneX,cy,
      [&](const count_raster::Font& font,const char* text,float x,float y,float scale,float alpha) {
   count_raster::renderScaled(font,text,x,y,scale,[&](int px,int py,float a,bool outline) {
    canvas.ink(px,py,outline?background(px,py):ink,a*alpha);
   });
  },light);
  transfer.burst(previous,[&](int x,int y,int n){for(int i=0;i<n;++i)canvas.ink(x+i,y,task_cues::kFlash);},
                 [&](int x,int y,float a){canvas.ink(x,y,task_cues::kFlash,a);});
  canvas.output(ppm,darkRgb);
 }
};
}
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
 Preview preview(state,index,mode);
 const uint32_t end=at>=0?static_cast<uint32_t>(at):mode==29?2600:mode>=30?7400:mode>=20?5200:mode>=16?4600:(mode==7||mode>=10)?9000:mode>=4?4600:
                    eye_acting::duration(eye_acting::clip(preview.state,index))+700;
 for(uint32_t ms=0;ms<=end;ms+=33) {
  preview.step(ms);
  if(at<0)preview.render(false);
 }
 if(at>=0){preview.step(end);preview.render(true);}
 return ferror(stdout)?1:0;
}
