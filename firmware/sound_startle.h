#pragma once
#include <stddef.h>
#include <stdint.h>
#include <math.h>

// Opt-in "startled by a sudden loud noise" detector (STICK_S3_AMBIENT_STARTLE).
//
// Privacy: the firmware feeds short PCM blocks from on-device RAM, and this
// detector keeps three numbers per block: its level, a slow noise floor, and
// the previous level. Samples are never stored, sent or logged; the only
// output is a 0..1 startle strength for the avatar. Dictation is unchanged.
//
// A startle needs a SUDDEN onset: the block is far above the adapted noise
// floor, above an absolute loudness, has a near-clipping peak, and jumped from
// the previous block. Music/speech that grows gradually raises the floor first.
// After a trigger there is a refractory period, and repeated triggers within
// a minute raise the threshold (habituation) so a noisy room is not spammed.
// Pure C++11 without Arduino calls, shared with native tests.
namespace sound_startle {

struct Config {
 float riseDb=23;        // above the adapted noise floor (+3 dB less sensitive)
 float jumpDb=12;        // above the louder of the two previous blocks
 float minDbfs=-27;      // absolute RMS loudness (+3 dB less sensitive)
 int peak=10000;         // |sample| at least this (about -10 dBFS)
 float floorRiseSeconds=6,floorFallSeconds=1;
 float minFloorDbfs=-72;
 uint32_t warmMs=700;    // after (re)starting the microphone
 uint32_t refractoryMs=5000;
 float habituationDb=6,maxHabituationDb=18;
 float habituationDecayDbPerSecond=.3f;  // 6 dB fades in 20s
};

// RMS level of one block in dBFS (DC removed) and its absolute peak.
struct Level { float dbfs; int peak; };
inline Level measure(const int16_t* samples,size_t count) {
 if(!samples||count==0)return {-120,0};
 int64_t sum=0,squares=0;int peak=0;
 for(size_t i=0;i<count;++i) {
  const int32_t v=samples[i];
  sum+=v;squares+=static_cast<int64_t>(v)*v;
  const int a=v<0?-v:v;
  if(a>peak)peak=a;
 }
 const double mean=static_cast<double>(sum)/count;
 double power=static_cast<double>(squares)/count-mean*mean;
 if(power<1)power=1;
 return {static_cast<float>(10*log10(power/(32768.0*32768.0))),peak};
}

struct Detector {
 Config config;
 float floor=0,previous=-120,beforePrevious=-120,habituation=0;
 uint32_t warmUntil=0,lastTrigger=0,lastBlock=0;
 bool primed=false,triggered=false;
 // (Re)start after the microphone (re)opens; habituation is kept.
 void reset(uint32_t now) {
  primed=false;warmUntil=now+config.warmMs;previous=beforePrevious=-120;lastBlock=now;
 }
 // Returns the startle strength (0 = none) for one block. `suppressed` blocks
 // (button clicks, handling, cues that own the face) still adapt the floor.
 float feed(const int16_t* samples,size_t count,uint32_t sampleRate,uint32_t now,bool suppressed=false) {
  const Level level=measure(samples,count);
  return feedLevel(level,count,sampleRate,now,suppressed);
 }
 float feedLevel(const Level& level,size_t count,uint32_t sampleRate,uint32_t now,bool suppressed=false) {
  const float seconds=sampleRate?static_cast<float>(count)/sampleRate:0;
  const float elapsed=static_cast<float>(now-lastBlock)/1000.0f;lastBlock=now;
  habituation=fmaxf(0,habituation-config.habituationDecayDbPerSecond*(elapsed<10?elapsed:10));
  if(!primed) {
   floor=fmaxf(level.dbfs,config.minFloorDbfs);previous=beforePrevious=level.dbfs;primed=true;
   return 0;
  }
  const float reference=fmaxf(previous,beforePrevious);
  float strength=0;
  const bool warm=static_cast<int32_t>(now-warmUntil)>=0;
  const bool rested=!triggered||now-lastTrigger>=config.refractoryMs;
  const float rise=level.dbfs-floor;
  if(warm&&rested&&!suppressed&&level.peak>=config.peak&&level.dbfs>=config.minDbfs&&
     rise>=config.riseDb+habituation&&level.dbfs-reference>=config.jumpDb) {
   const float over=(rise-config.riseDb-habituation)/20;
   strength=.35f+.65f*(over<0?0:over>1?1:over);
   triggered=true;lastTrigger=now;
   habituation=fminf(config.maxHabituationDb,habituation+config.habituationDb);
  }
  // Asymmetric floor: falls quickly to quiet, rises slowly under sustained sound.
  const float tau=level.dbfs<floor?config.floorFallSeconds:config.floorRiseSeconds;
  const float alpha=tau>0?fminf(1,seconds/tau):1;
  floor=fmaxf(config.minFloorDbfs,floor+(level.dbfs-floor)*alpha);
  beforePrevious=previous;previous=level.dbfs;
  return strength;
 }
};
}
