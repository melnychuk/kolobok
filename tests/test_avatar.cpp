#include <assert.h>
#include <initializer_list>
#include <math.h>
#include <string.h>
#include "../firmware/avatar_scene.h"
#include "../firmware/status_background.h"

int main() {
 using namespace eye_acting;
 const uint16_t green=((34&248)<<8)|((197&252)<<3)|(94>>3);
 const uint16_t blue=((59&248)<<8)|((130&252)<<3)|(246>>3);
 const uint16_t coral=((247&248)<<8)|((118&252)<<3)|(142>>3);
 for(int y=0;y<4;++y) {
  assert(status_background::color(0,y,green,blue,1,1)==green);
  assert(status_background::color(239,y,green,blue,1,1)==blue);
  const uint16_t middle=status_background::color(120,y,green,blue,1,1);
  assert(middle!=green&&middle!=blue);
  assert(status_background::color(24,y,green,blue,1,0)==green);
  assert(status_background::color(216,y,green,blue,0,1)==blue);
  assert(status_background::color(24,y,green,blue,0,1)==0);
  assert(status_background::color(24,y,green,coral,0,1,coral,1)==coral);
 }
 // No center seam, even with RGB565 quantization. Compare average dither
 // coverage at the two center columns rather than demanding identical pixels.
 for(int shift : {11,5,0}) {
  const int mask=shift==5?63:31;
  float left=0,right=0;
  for(int y=0;y<4;++y) {
   left+=status_background::channel(status_background::color(119,y,green,blue,1,1),shift,mask);
   right+=status_background::channel(status_background::color(120,y,green,blue,1,1),shift,mask);
  }
  assert(fabsf(left-right)/4<=1);
 }
 assert(clipCount(State::thinking)==9);
 assert(clipCount(State::done)==7&&clipCount(State::attention)==7&&clipCount(State::listening)==7);
 // Calm completion: lively clips are reserved for a fresh completion, every
 // calm clip is still visited, and calm rests are longer.
 {
  Player calm;calm.calm=true;uint16_t seen=0;uint32_t clock=0;
  for(int round=0;round<40;++round) {
   calm.update(State::done,clock,static_cast<uint32_t>(round*2654435761u));
   assert(!lively(clip(State::done,calm.index)));
   assert(calm.pause>=900+Player::kCalmExtra);
   seen|=1u<<calm.index;clock+=duration(clip(calm.state,calm.index))+calm.pause;
  }
  for(int i=0;i<clipCount(State::done);++i)
   assert(lively(clip(State::done,i))||(seen&(1u<<i)));
  int livelyCount=0;for(int i=0;i<clipCount(State::done);++i)livelyCount+=lively(clip(State::done,i));
  assert(livelyCount==2);
  Player eager;uint16_t all=0;clock=0;   // Celebration still reaches them.
  for(int round=0;round<14;++round) {
   eager.update(State::done,clock,static_cast<uint32_t>(round*7919));all|=1u<<eager.index;
   clock+=duration(clip(eager.state,eager.index))+Player::kMaxPause;
  }
  assert(all==(1u<<clipCount(State::done))-1);
  Player thinker;thinker.calm=true;thinker.update(State::thinking,0,5);assert(thinker.pause<=2300);
 }
 // Eased layout values ARRIVE exactly, so exact-zero fast paths/caches engage.
 {
  float down=1,up=0;
  for(int frame=0;frame<400;++frame) {
   down=avatar_scene::approach(down,0,.039f,.21f);up=avatar_scene::approach(up,1,.039f,.21f);
  }
  assert(down==0&&up==1);
 }
 // Clip boundaries continue from the pose on screen: the resting completion
 // smile and wide request eyes never collapse to a neutral key and regrow.
 for(int state=0;state<4;++state) {
  Player player;float minSmile=1,minHeight=9;
  for(uint32_t ms=0;ms<60000;ms+=33) {
   const Pose before=player.eyes(ms);
   const int index=player.index;
   player.update(static_cast<State>(state),ms,ms*2654435761u);
   const Pose pose=player.eyes(ms);
   if(index>=0&&player.index!=index) {
    // A new clip starts exactly where the previous pose was.
    assert(fabsf(pose.x-before.x)<1e-4f&&fabsf(pose.height-before.height)<1e-4f&&
           fabsf(pose.width-before.width)<1e-4f&&fabsf(pose.smile-before.smile)<1e-4f);
   }
   if(ms>4000) { minSmile=fminf(minSmile,pose.smile);minHeight=fminf(minHeight,pose.height); }
  }
  if(state==1)assert(minSmile>=.44f);     // Completion stays visibly happy.
  if(state==2)assert(minHeight>=.87f);    // Requests never shrink to plain eyes.
 }
 for(int state=0;state<4;++state) {
  Player player;
  const int count=clipCount(static_cast<State>(state));
  uint32_t clock=0;int previous=-1;
  for(int bag=0;bag<8;++bag) {
   uint16_t seen=0;
   for(int round=0;round<count;++round) {
    player.update(static_cast<State>(state),clock,static_cast<uint32_t>(round*7919+bag));
    assert(player.index>=0&&player.index<count&&player.index!=previous);
    assert(!(seen&(1u<<player.index)));seen|=1u<<player.index;
    previous=player.index;
    clock+=duration(clip(player.state,player.index))+Player::kMaxPause;
   }
   assert(seen==(1u<<count)-1);
  }
  for(int index=0;index<count;++index) {
   const Clip& c=clip(static_cast<State>(state),index);
   assert(strlen(c.name)>0&&c.count>=6&&duration(c)>=2000);
   for(int key=1;key<c.count;++key) {
    assert(c.keys[key].ms>c.keys[key-1].ms);
    if(state==2 && c.keys[key-1].pose.closeL==1 && c.keys[key-1].pose.closeR==1 &&
       c.keys[key].pose.closeL==1 && c.keys[key].pose.closeR==1) {
     assert(c.keys[key].ms-c.keys[key-1].ms<=120); // Requests must not look asleep.
    }
   }
   for(uint32_t ms=0;ms<=duration(c)+1000u;ms+=17) {
    const Pose pose=sample(c,ms);
    assert(isfinite(pose.x)&&isfinite(pose.y)&&isfinite(pose.width)&&isfinite(pose.height));
    assert(pose.width>.7f&&pose.width<1.3f&&pose.height>.7f&&pose.height<1.4f);
    assert(pose.closeL>=0&&pose.closeL<=1&&pose.closeR>=0&&pose.closeR<=1);
    assert(pose.smile>=0&&pose.smile<=1);
    if(state==1)assert(pose.upperL==0&&pose.upperR==0); // No angry/sad completion lids.
    avatar_scene::Scene scene;
    scene.eyes=pose;scene.body=sample(c,ms>90?ms-90:0);
    scene.eyeWidth=50*pose.width;scene.eyeHeight=(state==2?fmaxf(48,44*pose.height):56*pose.height);
    scene.awakeHeight=state==2?44:56;scene.alert=state==2?1:0;
    scene.eyeX=120+pose.x;scene.eyeY=avatar_scene::eyeBaseline(1,scene.alert,0)+pose.y;
    scene.flowClock=ms*.0006f;
    for(int y=0;y<135;++y) {
     float left,right;
     if(avatar_scene::bounds(scene,y,left,right)) {
      assert(isfinite(left)&&isfinite(right));assert(left>=0&&right<=240&&right>left);
      if(state==2)assert(y>=36);  // the request dome stays below the `!`
      // Text overlap is intentional now; foreground outlines protect counts
      // instead of freezing a whole horizontal band of the face.
     }
    }
    // Check shared raster and silhouette clipping at a few meaningful frames.
    if(ms%170==0)avatar_scene::eyes(scene,[&](int x,int y,float alpha) {
     assert(x>=0&&x<240&&y>=0&&y<135&&alpha>0&&alpha<=1);
     if(state==2)assert(y>=50);
    });
   }
  }
 }
 // New intentions have genuinely still holds, neutral/engaged lids (no smile),
 // distinct diagonal/tilted staging, and exactly the same 90ms body clock.
 const auto& study=clip(State::thinking,7);
 const auto& dots=clip(State::thinking,8);
 assert(strcmp(study.name,"Work through it")==0&&strcmp(dots.name,"Connect the dots")==0);
 const auto tilted=sample(study,800),tiltedEnd=sample(study,1400);
 assert(tilted.tilt==-6&&tilted.upperR>tilted.upperL&&tilted.smile==0);
 assert(tilted.x==tiltedEnd.x&&tilted.tilt==tiltedEnd.tilt&&tilted.flow==tiltedEnd.flow);
 assert(sample(dots,600).x<0&&sample(dots,600).y>0);
 assert(sample(dots,1400).x>0&&sample(dots,1400).y<0);
 for(int index=7;index<9;++index) {
  Player actor;actor.state=State::thinking;actor.index=index;actor.started=100;
  for(uint32_t t=300;t<duration(clip(actor.state,index));t+=17) {
   const auto body=actor.body(100+t),eyes=actor.eyes(100+t-90);
   assert(body.x==eyes.x&&body.y==eyes.y&&body.tilt==eyes.tilt);
   assert(eyes.smile==0&&eyes.upperL<=.24f&&eyes.upperR<=.24f);
  }
  actor.update(State::attention,900,0);
  assert(actor.state==State::attention&&actor.started==900);
 }
 // Bag bookkeeping also survives millis wrap and visits indices 7 and 8.
 Player wrapped;uint32_t wrapClock=UINT32_MAX-1000;
 uint16_t wrapSeen=0;
 for(int i=0;i<clipCount(State::thinking);++i) {
  wrapped.update(State::thinking,wrapClock,0);
  wrapSeen|=1u<<wrapped.index;
  wrapClock+=duration(clip(wrapped.state,wrapped.index))+Player::kMaxPause;
 }
 assert(wrapSeen==511);

 // Positional travel arrives 25% earlier; expression and blink clocks stay
 // authored. No discontinuity at the early arrival or the next key/hold.
 const Key travelKeys[]={
  {0,p()}, {400,p(-12,-8,1.1f,1.2f,0,0,.7f,1,1,1.1f,1.1f,-4,-3)},
  {900,p(-12,-8,1.1f,1.2f,0,0,.7f,1,1,1.1f,1.1f,-4,-3)}};
 const Clip movementClip={"travel regression",travelKeys,3};
 const auto halfway=sample(movementClip,200);
 assert(halfway.x<-9&&halfway.y<-6);
 assert(fabsf(halfway.height-1.1f)<.0001f&&fabsf(halfway.closeL-.5f)<.0001f);
 const auto arrived=sample(movementClip,300);
 assert(arrived.x==-12&&arrived.y==-8&&arrived.lean==-4&&arrived.lift==-3);
 assert(arrived.height<1.2f&&arrived.closeL<1); // Expression still finishes at 400ms.
 assert(fabsf(sample(movementClip,299).x-arrived.x)<.001f);
 assert(sample(movementClip,400).x==sample(movementClip,899).x);
 assert(sample(movementClip,400).smile==sample(movementClip,899).smile);
 assert(duration(movementClip)==900);
 for(float tau: {avatar_scene::kShiftSeconds,avatar_scene::kReflowSeconds}) {
  assert(tau>0&&tau<=.21f);
  // Exact exponential easing is independent of render-frame partitioning.
  float at30=0,at60=0;
  for(int i=0;i<10;++i)at30+=(1-at30)*(1-expf(-.033f/tau));
  for(int i=0;i<20;++i)at60+=(1-at60)*(1-expf(-.0165f/tau));
  assert(fabsf(at30-at60)<.00001f&&at30>.79f);
 }

 // Asking for input preempts a thinking or completion sequence, not its tail.
 Player player;player.update(State::done,100,1);
 player.update(State::attention,150,2);
 assert(player.state==State::attention&&player.started==150);
 const Pose delayed=player.body(180);
 assert(delayed.x==player.entryBody.x&&delayed.y==player.entryBody.y);
 const Pose interrupted=player.entryEyes;
 const Pose first=player.eyes(150);
 assert(first.x==interrupted.x&&first.y==interrupted.y);

 // Coherent fluid field: bounded, repeatable, and continuous in both space
 // and time. No random retarget or per-frame jump is hidden in the renderer.
 float motion=0;
 for(int y=-100;y<240;y+=7)for(int x=-80;x<320;x+=11) {
  const float a=fluid_face::field(x,y,2),b=fluid_face::field(x,y,2.033f);
  assert(a>=-1.0001f&&a<=1.0001f&&b>=-1.0001f&&b<=1.0001f);
  assert(fabsf(a-b)<.04f);
  assert(a==fluid_face::field(x,y,2));
  motion+=fabsf(a-fluid_face::field(x,y,7));
 }
 assert(motion>10);
 // A real wink, not a blink on both eyes.
 Pose winkPose=p();winkPose.closeL=1;
 float leftInk=0,rightInk=0;
 eye_raster::render(120,68,50,56,19,winkPose,[&](int x,int,float a){if(x<120)leftInk+=a;else rightInk+=a;});
 assert(leftInk<rightInk*.15f);
 // Closed happy eyes curve upward, whereas neutral blinks remain flat.
 Pose happy=p(0,0,1,1,0,0,.6f,1,1);
 float centerY=0,centerWeight=0,outerY=0,outerWeight=0;
 eye_raster::render(120,68,50,56,19,happy,[&](int x,int y,float a) {
  const float dx=fabsf(x+.5f-85.5f);
  if(x<120 && dx<5){centerY+=y*a;centerWeight+=a;}
  if(x<120 && dx>19){outerY+=y*a;outerWeight+=a;}
 });
 assert(centerWeight>0&&outerWeight>0);
 assert(outerY/outerWeight-centerY/centerWeight>3);
}
