// Listening soundwave: on-device loudness envelope, synthetic preview voice,
// eye->line->bars morph continuity, exact eyes outside listening.
#include <assert.h>
#include <stdio.h>
#include "../firmware/avatar_scene.h"

using listen_wave::Envelope;
using listen_wave::Synth;
using listen_wave::Wave;

namespace {
struct Ink {
 float sum=0;int x0=1000,x1=-1000,y0=1000,y1=-1000;
 void operator()(int x,int y,float a) {
  sum+=a;
  if(a>.5f) { if(x<x0)x0=x;if(x>x1)x1=x;if(y<y0)y0=y;if(y>y1)y1=y; }
 }
};
avatar_scene::Scene centred() {
 avatar_scene::Scene s;
 s.region=280;s.side=0;s.corners=0;s.offset=0;s.eyeX=120;s.eyeY=60;
 return s;
}
Ink draw(const avatar_scene::Scene& s) {
 avatar_scene::Mask mask;avatar_scene::buildMask(s,mask);
 Ink ink;avatar_scene::eyes(s,mask,[&](int x,int y,float a){ink(x,y,a);});
 return ink;
}
void block(int16_t* pcm,float amplitude,int phase=0) {
 for(int i=0;i<1280;++i)pcm[i]=static_cast<int16_t>(amplitude*32767*sinf((i+phase)*.2f));
}

void envelope() {
 Envelope e;int16_t pcm[1280];
 uint32_t now=1000;
 for(int b=0;b<10;++b) { block(pcm,.002f,b);e.feed(pcm,1280,now);now+=80;e.level(now); }
 assert(e.level(now)<.15f);  // a steady room hum settles near the floor
 block(pcm,.3f);e.feed(pcm,1280,now);
 // Played back at real pace: eight 10ms levels across the next 80ms.
 const float first=e.level(now);
 assert(first>.6f);
 assert(e.count==7);
 e.level(now+35);assert(e.count==4);  // +10,+20,+30 consumed
 e.level(now+75);assert(e.count==0);
 // Stream stopped: decays to silence instead of freezing high.
 assert(e.level(now+300)==0);
 // Never more than the bounded queue, oldest dropped.
 for(int b=0;b<10;++b)e.feed(pcm,1280,now+400);
 assert(e.count==Envelope::kQueue);
 e.reset();assert(e.count==0&&e.level(now+500)==0);
}

void synth() {
 Synth a,b;
 a.reset(0,7);b.reset(0,7);
 int quiet=0,loud=0;
 for(uint32_t ms=0;ms<20000;ms+=10) {
  const float la=a.level(ms),lb=b.level(ms);
  assert(la==lb);              // deterministic for a seed
  assert(la>=0&&la<=1&&la==la);
  if(la<.02f)++quiet;
  if(la>.4f)++loud;
 }
 assert(quiet>200&&loud>200);   // pauses and syllables both occur
 Synth c;c.reset(0,8);
 bool differs=false;
 for(uint32_t ms=0;ms<5000&&!differs;ms+=10)differs=c.level(ms)!=b.level(20000+ms);
 assert(differs);
}

void morph() {
 // Outside listening the eye raster is exactly the original one.
 auto plain=centred();
 const Ink original=draw(plain);
 Wave w;Synth voice;voice.reset(0);
 uint32_t ms=0;
 for(;ms<3000;ms+=33)w.update(.033f,ms,true,voice.level(ms));
 assert(w.amount==1);
 bool tall=false;
 for(int i=0;i<listen_wave::kBars;++i) {
  assert(w.bar(i)==w.bar(listen_wave::kBars-1-i));  // mirrored about the centre
  assert(w.bar(i)>=0&&w.bar(i)<=1);
  tall=tall||w.bar(i)>.3f;
 }
 for(;ms<6000&&!tall;ms+=33) {
  w.update(.033f,ms,true,voice.level(ms));
  for(int i=0;i<listen_wave::kBars;++i)tall=tall||w.bar(i)>.3f;
 }
 assert(tall);
 auto listening=centred();avatar_scene::setWave(listening,w);
 const Ink bars=draw(listening);
 // Tallest bar stays within the authored reach.
 const float tallest=avatar_scene::renderedEyeHeight(listening)*listen_wave::kMaxScale;
 assert(bars.y1-bars.y0<=tallest+2);
 // Release: back to exact eyes within the leave time, bars cleared.
 const uint32_t release=ms;
 for(;ms<release+500;ms+=33)w.update(.033f,ms,false,1);
 assert(w.amount==0);
 for(int i=0;i<listen_wave::kBars;++i)assert(w.bar(i)==0);
 auto after=centred();avatar_scene::setWave(after,w);
 const Ink back=draw(after);
 assert(back.sum==original.sum&&back.x0==original.x0&&back.y1==original.y1);
}

void continuity() {
 // At the switch from the closing eyes to the bars, both draw the same line.
 auto closing=centred();closing.wave=listen_wave::kClose-1e-4f;
 auto opening=centred();opening.wave=listen_wave::kClose;
 const Ink line=draw(closing),dashes=draw(opening);
 assert(abs(line.x0-dashes.x0)<=1&&abs(line.x1-dashes.x1)<=1);
 assert(abs(line.y0-dashes.y0)<=1&&abs(line.y1-dashes.y1)<=1);
 assert(fabsf(line.sum-dashes.sum)<.2f*line.sum);
 // Gaps open smoothly: coverage changes little per small morph step.
 float previous=dashes.sum;
 for(float a=listen_wave::kClose+.02f;a<=1;a+=.02f) {
  auto s=centred();s.wave=a;
  const float sum=draw(s).sum;
  assert(fabsf(sum-previous)<.12f*previous+20);
  previous=sum;
 }
}

void cadence() {
 // Morph timing does not depend on the frame rate.
 Wave a,b;
 for(uint32_t ms=0;ms<=300;ms+=33)a.update(.033f,ms,true,0);
 for(uint32_t ms=0;ms<=300;ms+=11)b.update(.011f,ms,true,0);
 assert(fabsf(a.amount-b.amount)<.06f);
 assert(a.amount>.6f&&a.amount<.85f);
}
} // namespace

int main() {
 envelope();
 synth();
 morph();
 continuity();
 cadence();
 puts("Listening wave: paced on-device envelope, adaptive floor, bounded queue, deterministic synthetic voice, mirrored bounded bars, exact eyes outside listening, line-to-bars continuity and cadence passed");
 return 0;
}
