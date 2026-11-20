// Snappy keys, procedural life layer, eye-size spring and cartoon blink shape.
// Also guards the eye raster workload so expressiveness cannot silently cost FPS.
#include <assert.h>
#include <initializer_list>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../firmware/avatar_scene.h"
#include "../firmware/eye_life.h"

using namespace eye_acting;

namespace {
// Pre-change eye-raster workload (old smooth keys, 55ms size lag, no life),
// measured on this harness at 20/25fps over every clip. Allow 3% headroom.
constexpr unsigned long kBaselineMax[2]={6358,5452};
constexpr double kBaselineMean[2]={4252.0,4226.9};

struct Workload { unsigned long max=0; double total=0; unsigned long frames=0; };
// Firmware-equivalent composition without cues: acting -> life -> size spring.
Workload workload(int layout) {
 Workload result;
 for(int s=0;s<4;++s)for(int i=0;i<clipCount(static_cast<State>(s));++i)for(int step:{40,50}) {
  Player actor;actor.state=static_cast<State>(s);actor.index=i;actor.started=0;
  eye_life::Life life;eye_life::SizeSpring spring;
  avatar_scene::Scene scene;const bool request=layout==1;
  scene.alert=request;scene.awakeHeight=request?44:56;scene.gap=request?15.2f:19;scene.corners=request;
  const eye_life::Mood& mood=s==0?eye_life::kThinking:s==1?eye_life::kDone:
                             s==2?eye_life::kAttention:eye_life::kListening;
  for(unsigned ms=0;ms<=duration(clip(actor.state,i))+700u;ms+=step) {
   const float dt=ms?step/1000.0f:0;
   const auto segment=actor.segment(ms);
   const auto eyes=life.apply(actor.eyes(ms),ms,dt,mood,segment.still,true,&segment);
   spring.step(dt,50*eyes.width,fmaxf(avatar_scene::kRequestEyeFloor*scene.alert,scene.awakeHeight*eyes.height));
   scene.eyeWidth=spring.width;scene.eyeHeight=spring.height;
   unsigned long count=0;
   eye_raster::render(120+eyes.x,avatar_scene::eyeBaseline(scene.corners,scene.alert,0)+eyes.y,
                      scene.eyeWidth,avatar_scene::renderedEyeHeight(scene),scene.gap,eyes,
                      [&](int,int,float){++count;},avatar_scene::kBlinkSink,avatar_scene::kBlinkWiden);
   if(count>result.max)result.max=count;
   result.total+=count;++result.frames;
  }
 }
 return result;
}
struct Ink { int x,y;float a;bool operator==(const Ink& o) const {return x==o.x&&y==o.y&&a==o.a;} };
std::vector<Ink> ink(const Pose& pose,float height,float sink,float widen,float cx=120) {
 std::vector<Ink> out;
 eye_raster::render(cx,67.5f,50,height,19,pose,[&](int x,int y,float a){out.push_back({x,y,a});},sink,widen);
 return out;
}
void centroidAndSpan(const std::vector<Ink>& pixels,float& cy,int& left,int& right) {
 float sum=0,weight=0;left=1000;right=-1000;
 for(const auto& p:pixels)if(p.x<120){sum+=(p.y+.5f)*p.a;weight+=p.a;if(p.a>.5f){left=p.x<left?p.x:left;right=p.x>right?p.x:right;}}
 cy=weight>0?sum/weight:0;
}
}

int main() {
 // 1. Saccade curve: exact endpoints, bounded ~9% overshoot, zero end velocity.
 assert(snap(0)==0&&snap(1)==1&&snap(-1)==0&&snap(2)==1);
 float peak=0;
 for(int i=0;i<=1000;++i)peak=fmaxf(peak,snap(i/1000.0f));
 assert(peak>1.07f&&peak<1.10f);
 assert(fabsf(snap(.999f)-1)<.0001f);

 // 2. All authored segments: short gaze jumps overshoot <=10% / 3px and land
 //    by 75%; long moves never leave their endpoints. Holds remain exact.
 for(int s=0;s<4;++s)for(int i=0;i<clipCount(static_cast<State>(s));++i) {
  const Clip& c=clip(static_cast<State>(s),i);
  for(int k=1;k<c.count;++k) {
   const Pose a=keyPose(c,k-1),b=keyPose(c,k);  // as shown on screen
   const uint32_t length=c.keys[k].ms-c.keys[k-1].ms;
   const float distance=sqrtf((b.x-a.x)*(b.x-a.x)+(b.y-a.y)*(b.y-a.y));
   const float slack=isSaccade(a,b,length)?fminf(.10f*distance,3.0f)+.001f:.001f;
   for(uint32_t t=c.keys[k-1].ms;t<c.keys[k].ms;++t) {
    const Pose pose=sample(c,t);
    assert(pose.x>=fminf(a.x,b.x)-slack&&pose.x<=fmaxf(a.x,b.x)+slack);
    assert(pose.y>=fminf(a.y,b.y)-slack&&pose.y<=fmaxf(a.y,b.y)+slack);
    assert(pose.closeL>=0&&pose.closeL<=1&&pose.closeR>=0&&pose.closeR<=1);
    if(isSaccade(a,b,length)&&t>=c.keys[k-1].ms+length*3/4+1)
     assert(fabsf(pose.x-b.x)<.01f&&fabsf(pose.y-b.y)<.01f);
    if(sameEyes(a,b))assert(sameEyes(pose,a));
   }
  }
 }
 // A short closing key accelerates shut; it is not slower to arrive.
 const Key blinkKeys[]={{0,p()},{100,p(0,0,1,1,0,0,0,1,1)},{240,p()},{900,p()}};
 const Clip blinkClip={"blink",blinkKeys,4};
 assert(sample(blinkClip,50).closeL<.3f&&sample(blinkClip,90).closeL>.75f);
 assert(sample(blinkClip,100).closeL==1&&sample(blinkClip,100).closeR==1);
 assert(fabsf(sample(blinkClip,170).closeL-.5f)<.0001f); // Opening keeps smooth timing.

 // 3. Segment classification used by the life layer.
 {
  Player actor;actor.state=State::thinking;actor.index=0;actor.started=1000; // Recall.
  assert(!actor.segment(1100).still);                       // Entry blend.
  assert(actor.segment(1000+700).still);                    // 340..1120 hold.
  const auto blink=actor.segment(1000+1150);
  assert(!blink.still&&blink.closure);                      // Authored blink nearby.
  assert(actor.segment(1000+2700+200).still);               // Pause after the clip.
  Player compare;compare.state=State::thinking;compare.index=1;compare.started=0;
  const auto jump=compare.segment(900);                     // 850->990: -12 -> 12.
  assert(jump.saccade&&!jump.closure&&jump.distance>=eye_life::kSaccadeBlinkPx&&jump.start==850);
 }

 // 4. Life layer: bounded micro-saccades only while still, at any frame rate.
 {
  eye_life::Life a,b;
  for(uint32_t t=0;t<=20000;t+=16) {
   const auto pose=b.apply(p(),t,t?.016f:0,eye_life::kIdle,true,false);
   (void)pose;
   if(t%48==0) {
    const auto x=a.apply(p(),t,t?.048f:0,eye_life::kIdle,true,false);
    float bx,by,ax,ay;b.micro(t,bx,by);a.micro(t,ax,ay);
    assert(ax==bx&&ay==by);                                  // Same absolute schedule.
    assert(fabsf(x.x)<=eye_life::kMicroX*1.2f&&fabsf(x.y)<=eye_life::kMicroY*1.2f); // Snap overshoot.
   }
  }
  // Leaving a hold fades the offset out quickly; authored motion dominates.
  eye_life::Life fade;float moved=0;
  for(uint32_t t=0;t<6000;t+=33)moved=fmaxf(moved,fabsf(fade.apply(p(),t,.033f,eye_life::kIdle,true,false).x));
  assert(moved>.3f);
  float last=0;uint32_t at=0;
  for(uint32_t t=6000;t<6300;t+=33){last=fade.apply(p(),t,.033f,eye_life::kIdle,false,false).x;at=t;}
  float mx,my;fade.micro(at,mx,my);
  assert(fabsf(last)<=fabsf(mx)*.01f+.01f);
 }
 // Procedural blinks: fully close at 20/25/30/60fps, short, spaced, both eyes.
 for(int step:{50,40,33,16}) {
  eye_life::Life life;
  uint32_t lastStart=0;int blinks=0;bool closed=false;float maxL=0,maxR=0;
  uint32_t closedSince=0;
  for(uint32_t t=0;t<=60000;t+=step) {
   const auto pose=life.apply(p(),t,t?step/1000.0f:0,eye_life::kIdle,true,true);
   maxL=fmaxf(maxL,pose.closeL);maxR=fmaxf(maxR,pose.closeR);
   const bool now=pose.closeL>.05f||pose.closeR>.05f;
   if(now&&!closed) {
    if(blinks)assert(t-lastStart>=eye_life::kBlinkLength+eye_life::kDoubleGap-step);
    lastStart=t;++blinks;closedSince=t;
   }
   if(now)assert(t-closedSince<=eye_life::kBlinkLength+step);  // Never a sleepy hold.
   closed=now;
  }
  assert(maxL>=.97f&&maxR>=.97f);
  assert(blinks>=10&&blinks<=30);
 }
 // No blinks while disallowed (sleep) or while an authored closure is active.
 {
  eye_life::Life life;
  for(uint32_t t=0;t<20000;t+=33)assert(life.apply(p(),t,.033f,eye_life::kIdle,true,false).closeL==0);
  eye_life::Life authored;Pose wink=p();wink.closeL=1;
  for(uint32_t t=0;t<20000;t+=33) {
   const auto pose=authored.apply(wink,t,.033f,eye_life::kIdle,true,true);
   assert(pose.closeR==0);                                   // Real wink stays a wink.
  }
  // After the authored closure ends, wait at least blinkMin before blinking.
  uint32_t first=0;
  for(uint32_t t=20000;t<40000;t+=33) {
   if(authored.apply(p(),t,.033f,eye_life::kIdle,true,true).closeL>.05f){first=t;break;}
  }
  assert(first>=20000+eye_life::kIdle.blinkMin-33);
 }
 // Blink with a big authored saccade (anchored to the segment start), never
 // when the clip already punctuates the move with its own closure.
 {
  const eye_life::Mood always={1,60000,60000,0,1},never={1,60000,60000,0,0};
  for(int variant=0;variant<2;++variant) {
   Player actor;actor.state=State::thinking;actor.index=1;actor.started=0; // Compare.
   eye_life::Life life;bool blinked=false;
   for(uint32_t t=0;t<2500;t+=33) {
    const auto segment=actor.segment(t);
    const auto pose=life.apply(actor.eyes(t),t,.033f,variant?never:always,segment.still,true,&segment);
    if(life.blinking&&!blinked){blinked=true;assert(life.blinkAt==140);} // 2->-12 jump.
    (void)pose;
   }
   assert(blinked==(variant==0));
  }
  Player recall;recall.state=State::thinking;recall.index=0;recall.started=0;
  eye_life::Life life;
  for(uint32_t t=0;t<2700;t+=33) {
   const auto segment=recall.segment(t);
   life.apply(recall.eyes(t),t,.033f,always,segment.still,true,&segment);
   if(t>=1210&&t<1500)assert(!life.blinking||life.blinkAt<1100||life.blinkAt>1400);
  }
 }
 // millis() wrap: schedules keep running and outputs stay bounded.
 {
  eye_life::Life life;int blinks=0;bool closed=false;
  for(uint32_t i=0,t=UINT32_MAX-30000;i<2000;++i,t+=33) {
   const auto pose=life.apply(p(),t,.033f,eye_life::kIdle,true,true);
   assert(isfinite(pose.x)&&fabsf(pose.x)<=2.2f&&fabsf(pose.y)<=1.35f);
   const bool now=pose.closeL>.05f;if(now&&!closed)++blinks;closed=now;
  }
  assert(blinks>=8);
 }

 // 5. Eye-size spring: lively but bounded overshoot, settles, frame-rate stable.
 {
  eye_life::SizeSpring spring;spring.reset(50,45);
  float peakH=45;
  for(int i=0;i<60;++i){spring.step(.0167f,50,55);peakH=fmaxf(peakH,spring.height);}
  assert((peakH-55)/10>.05f&&(peakH-55)/10<.20f);
  assert(fabsf(spring.height-55)<.6f);
  // Smooth authored ramp: 20fps/30fps/60fps paths stay close.
  float results[3];int index=0;
  for(int step:{50,33,16}) {
   eye_life::SizeSpring s;s.reset(50,40);float worst=0;
   for(int t=step;t<=1000;t+=step) {
    const float target=40+16*smooth(t/220.0f);
    s.step(step/1000.0f,50,target);worst=fmaxf(worst,s.height);
    assert(isfinite(s.height)&&s.height>0);
   }
   results[index++]=worst;
  }
  assert(fabsf(results[0]-results[2])<1.5f&&fabsf(results[1]-results[2])<1);
  eye_life::SizeSpring zero;zero.step(0,50,56);zero.step(0,60,30);
  assert(zero.width==50&&zero.height==56);  // No time, no motion.
 }

 // 6. Blink shape: defaults are the exact old kernel; plain closures sink and
 //    widen; happy arches and open eyes are unchanged by the blink shape.
 {
  Pose half=p();half.closeL=half.closeR=.5f;
  assert(ink(half,45,0,0)==ink(half,45,0,0));
  std::vector<Ink> legacy;
  eye_raster::render(120,67.5f,50,45,19,half,[&](int x,int y,float a){legacy.push_back({x,y,a});});
  assert(legacy==ink(half,45,0,0));
  assert(ink(p(),45,avatar_scene::kBlinkSink,avatar_scene::kBlinkWiden)==ink(p(),45,0,0));
  Pose happy=p(0,0,1,1,0,0,.6f,1,1);
  assert(ink(happy,45,avatar_scene::kBlinkSink,avatar_scene::kBlinkWiden)==ink(happy,45,0,0));
  Pose shut=p();shut.closeL=shut.closeR=1;
  float openY,shutY,oldY;int openL,openR,shutL,shutR,oldL,oldR;
  centroidAndSpan(ink(p(),45,0,0),openY,openL,openR);
  centroidAndSpan(ink(shut,45,0,0),oldY,oldL,oldR);
  centroidAndSpan(ink(shut,45,avatar_scene::kBlinkSink,avatar_scene::kBlinkWiden),shutY,shutL,shutR);
  assert(fabsf(oldY-openY)<.5f);                 // Old blink collapsed to the centre.
  assert(shutY-openY>6&&shutY-openY<12);         // New lid comes down to the lower part.
  assert(shutR-shutL>oldR-oldL);                 // Closed line is a touch wider.
  // Rotated/winking eyes remain finite and inside a sane neighbourhood.
  for(float tilt:{-30.0f,-6.0f,0.0f,6.0f,30.0f}) {
   Pose wink=p();wink.closeL=.8f;wink.tilt=tilt;
   eye_raster::render(120,67.5f,50,45,19,wink,[&](int x,int y,float a) {
    assert(isfinite(a)&&a>0&&a<=1&&x>40&&x<200&&y>20&&y<115);
   },avatar_scene::kBlinkSink,avatar_scene::kBlinkWiden);
  }
 }

 // 7. Workload guard: expressiveness must not grow the eye raster budget.
 for(int layout=0;layout<2;++layout) {
  const Workload w=workload(layout);
  const double mean=w.total/w.frames;
  printf("Eye workload %s: max %lu (baseline %lu), mean %.1f (baseline %.1f)\n",
         layout?"request":"vertical",w.max,kBaselineMax[layout],mean,kBaselineMean[layout]);
  // Requests used to clamp every authored height to a 48px floor; their
  // stretch peaks (1.25 -> ~52px) are now visible on purpose. Allow only that
  // peak (+10%) while the MEAN workload keeps the original +3% guard.
  assert(w.max<=kBaselineMax[layout]*(layout?110:103)/100);
  assert(mean<=kBaselineMean[layout]*1.03);
 }
 printf("Eye life/easing/spring/blink-shape checks passed\n");
}
