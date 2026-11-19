#pragma once
#include <stdint.h>
#include <stddef.h>
#include <math.h>

// Listening eyes become a soundwave. While dictation records, each eye closes
// into a line, breaks into vertical bars and the bars ripple: the newest sound
// enters between the eyes and travels outward to both sides. Release reverses
// it (bars sink to a line, the eyes open).
//
// On the Stick the wave follows the loudness of the dictation blocks it is
// already capturing, computed on the device (Envelope). Only a 0..1 level per
// 10ms ever exists: nothing new is sent, stored or logged, and no speech
// content is analyzed. Native previews, the header widget and the product site
// have no microphone, so they use a seeded synthetic "voice" (Synth).
namespace listen_wave {
constexpr int kBarsPerEye=5;
constexpr int kBars=2*kBarsPerEye;  // left eye 0..4, right eye 5..9
constexpr float kClose=.35f;      // morph: eyes close into a line, then bars grow
constexpr float kBarGap=.6f;      // gap between bars, in bar widths
constexpr float kMinHeight=3;     // the closed-eye line thickness (eye_raster)
constexpr float kMaxScale=1.3f;   // tallest bar, relative to the open eye height
inline float unit(float v) { return v<0?0:(v>1?1:v); }
inline float smooth(float v) { v=unit(v);return v*v*(3-2*v); }

// ------------------------------------------------------------ loudness
// PCM16 blocks -> 10ms loudness levels (0..1), played back at real pace so a
// whole 80ms block does not land in one frame. The noise floor adapts (falls
// at once, rises slowly) and a decaying peak normalizes quiet and loud voices.
struct Envelope {
 static constexpr int kStepSamples=160;  // 10ms at 16kHz
 static constexpr int kQueue=32;
 float queue[kQueue]={};
 int head=0,count=0;
 uint32_t nextAt=0;
 float floorDb=-60,peakDb=-30,current=0;
 bool primed=false;
 void reset() { head=count=0;current=0;primed=false;floorDb=-60;peakDb=-30; }
 void feed(const int16_t* pcm,size_t samples,uint32_t now) {
  for(size_t start=0;start+kStepSamples<=samples;start+=kStepSamples) {
   float sum=0;
   for(int i=0;i<kStepSamples;++i) { const float v=pcm[start+i];sum+=v*v; }
   const float rms=sqrtf(sum/kStepSamples)/32768.0f;
   const float db=20*log10f(rms+1e-6f);
   if(!primed) { floorDb=db;peakDb=db+18;primed=true; }
   floorDb=db<floorDb?db:floorDb+.03f;           // +3dB/s at 10ms steps
   peakDb=db>peakDb?db:peakDb-.04f;              // -4dB/s
   const float span=fmaxf(18,peakDb-floorDb-4);
   push(unit((db-floorDb-5)/span));
  }
  if(count&&nextAt<now)nextAt=now;
 }
 void push(float level) {
  if(count==kQueue) { head=(head+1)%kQueue;--count; }  // drop the oldest
  queue[(head+count)%kQueue]=level;++count;
 }
 // Current level, consuming one queued 10ms step per 10ms of wall time.
 float level(uint32_t now) {
  while(count&&static_cast<int32_t>(now-nextAt)>=0) {
   current=queue[head];head=(head+1)%kQueue;--count;nextAt+=10;
  }
  if(!count&&static_cast<int32_t>(now-nextAt)>120)current=0;  // stream stopped
  return current;
 }
};

// ------------------------------------------------------- synthetic voice
// A plausible speech envelope for previews/site: syllables grouped into words
// and phrases, with pauses. Deterministic for a given seed and clock.
struct Synth {
 uint32_t state=0x9e3779b9u;
 uint32_t start=0,end=0;
 float peak=0;
 int syllables=0,words=0;
 bool primed=false;
 float random() { state=state*1664525u+1013904223u;return (state>>8)*(1.0f/16777216.0f); }
 void reset(uint32_t now,uint32_t seed=0x9e3779b9u) { state=seed;primed=true;start=end=now+260;peak=0;syllables=words=0; }
 float level(uint32_t now) {
  if(!primed)reset(now);
  int guard=0;
  while(static_cast<int32_t>(now-end)>=0&&guard++<64) {
   uint32_t gap=0;
   if(syllables>0) { --syllables;gap=static_cast<uint32_t>(10+30*random()); }
   else if(words>0) { --words;syllables=1+static_cast<int>(random()*3);gap=static_cast<uint32_t>(70+150*random()); }
   else { words=2+static_cast<int>(random()*5);syllables=1+static_cast<int>(random()*2);
          gap=static_cast<uint32_t>(380+520*random()); }
   start=end+gap;end=start+static_cast<uint32_t>(110+150*random());
   peak=.45f+.55f*random();
  }
  if(static_cast<int32_t>(now-start)<0)return 0;
  const float u=static_cast<float>(now-start)/static_cast<float>(end-start);
  const float bump=sinf(3.14159265f*unit(u));
  return peak*bump*(.85f+.15f*sinf(static_cast<float>(now)*.09f));  // a little grain
 }
};

// ------------------------------------------------------------------ wave
// Morph amount plus per-bar heights (0..1). Sound enters at the inner bars and
// travels outward every kStep; displayed heights rise fast and fall slower.
struct Wave {
 static constexpr uint32_t kStep=55;
 static constexpr float kEnterSeconds=.42f,kLeaveSeconds=.36f;
 float amount=0;
 float trail[kBarsPerEye]={};
 float shown[kBarsPerEye]={};
 float stepPeak=0,phase=0;
 uint32_t stepAt=0;
 bool active() const { return amount>0; }
 void update(float seconds,uint32_t now,bool listening,float level) {
  amount=unit(amount+(listening?seconds/kEnterSeconds:-seconds/kLeaveSeconds));
  if(amount<=0) {
   for(int k=0;k<kBarsPerEye;++k)trail[k]=shown[k]=0;
   stepPeak=0;stepAt=now;phase=0;
   return;
  }
  phase+=seconds;
  // Bounded and seamless: integer angular rates repeat every 2pi seconds.
  if(phase>6.2831853f)phase-=6.2831853f;
  if(!listening)level=0;
  stepPeak=fmaxf(stepPeak,level);
  if(static_cast<int32_t>(now-stepAt)>=static_cast<int32_t>(kStep)) {
   for(int k=kBarsPerEye-1;k>0;--k)trail[k]=trail[k-1];
   trail[0]=stepPeak;stepPeak=level;
   stepAt=now-(now-stepAt)%kStep;
  }
  for(int k=0;k<kBarsPerEye;++k) {
   // The newest (inner) bar shows the live level; outer bars echo it smaller.
   static constexpr float taper[kBarsPerEye]={1,.88f,.74f,.6f,.46f};
   const float source=k==0?fmaxf(level,trail[0]):trail[k];
   const float shimmer=.07f*(.5f+.5f*sinf(phase*7+k*1.9f));
   const float target=unit(source*taper[k]*(.86f+.14f*sinf(phase*11+k*2.7f))+shimmer);
   const float tau=target>shown[k]?.03f:.09f;
   shown[k]+=(target-shown[k])*(1-expf(-seconds/tau));
  }
 }
 // Height of bar i (0..kBars-1, left to right): mirrored so each eye's inner
 // bar is nearest the centre.
 float bar(int i) const {
  const int k=i<kBarsPerEye?kBarsPerEye-1-i:i-kBarsPerEye;
  return shown[k];
 }
};

// Draw both eyes as bars. cx/cy: the pair's centre; width/height: one open
// eye; gap: between the eyes. heights(i) gives 0..1, amount the morph (> kClose).
// box(cx,cy,w,h,radius) draws one antialiased rounded bar.
template<class Box,class Heights>
void bars(float cx,float cy,float width,float height,float gap,float amount,
          float scaleL,float scaleR,float tilt,float closeL,float closeR,
          float blinkSink,float blinkWiden,Heights heights,Box box) {
 const float grow=smooth((amount-kClose)/(1-kClose));
 const float slope=tanf(tilt*.01745329252f);
 for(int side=0;side<2;++side) {
  const float scale=side?scaleR:scaleL;
  const float closure=unit(side?closeR:closeL);
  // At grow 0 this is exactly the closed blink line: widened and sunk.
  const float w=width*scale*(1+blinkWiden*(1-grow));
  const float sink=(height*scale-kMinHeight)*blinkSink*(1-grow);
  const float barWidth=w/(kBarsPerEye+(kBarsPerEye-1)*kBarGap*grow);
  const float pitch=barWidth*(1+kBarGap*grow);
  const float ex=cx+(side?1:-1)*(width+gap)/2;
  const float tallest=height*scale*kMaxScale;
  for(int b=0;b<kBarsPerEye;++b) {
   const int i=side*kBarsPerEye+b;
   const float bx=ex-w/2+barWidth/2+b*pitch;
   const float level=heights(i);
   float h=kMinHeight+grow*(4+level*(tallest-kMinHeight-4));
   h=kMinHeight+(h-kMinHeight)*(1-.6f*closure);  // a blink squashes, never erases
   box(bx,cy+sink+(bx-cx)*slope,barWidth,h,fminf(barWidth,h)/2);  // pill; a line when flat
  }
 }
}
} // namespace listen_wave
