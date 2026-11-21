#pragma once
#include <math.h>
#include <stdint.h>
#include "eye_acting.h"
#include "eye_raster.h"

// Input-request ("needs you") staging shared by firmware and native previews:
// a slowly pulsing orange-yellow field, a large black `!` above the head, and
// small hops now that the body sits low with only the top of its blob showing.
// Hops drive avatar_scene::Scene::hop/hopStretch; the dome renders them.
// Counts are hidden visually in this state; no task data changes.
namespace request_scene {
// RGB565 endpoints: warm yellow and amber-orange.
constexpr uint16_t kYellow=((250&248)<<8)|((204&252)<<3)|(21>>3);
constexpr uint16_t kOrange=((251&248)<<8)|((146&252)<<3)|(22>>3);
// Exact wrap (twice the `!` bob period): no unbounded float clock.
constexpr uint32_t kPulsePeriod=3600;

inline uint16_t blend(uint16_t a,uint16_t b,float t) {
 const int r=((a>>11)&31)+(((b>>11)&31)-((a>>11)&31))*t+.5f;
 const int g=((a>>5)&63)+(((b>>5)&63)-((a>>5)&63))*t+.5f;
 const int bl=(a&31)+((b&31)-(a&31))*t+.5f;
 return static_cast<uint16_t>((r<<11)|(g<<5)|bl);
}
// 0 (yellow) .. 1 (orange): a slow cosine breath, never a flash.
inline float pulse(uint32_t ms) {
 return .5f-.5f*cosf((ms%kPulsePeriod)*(6.2831853f/kPulsePeriod));
}
// Light mode passes paler endpoints; the pulse itself is unchanged.
inline uint16_t field(uint32_t ms,uint16_t from=kYellow,uint16_t to=kOrange) {
 return blend(from,to,pulse(ms));
}

// Large black `!` above the head, gently bobbing (1.8s exact period).
constexpr float kSignX=120,kSignY=21;
template<class Pixel>
void sign(uint32_t ms,Pixel pixel) {
 const float bob=sinf((ms%1800)*(6.2831853f/1800))*1.4f;
 eye_raster::box(kSignX,kSignY-5+bob,9,24,3.5f,pixel);
 eye_raster::box(kSignX,kSignY+14+bob,9,9,4.5f,pixel);
}

// Small cartoon hops: crouch, jump, land with a squash. A double hop greets
// the request; later single/double hops come every few seconds. They are
// additive over the authored request acting and change only body/eye height.
struct Hops {
 static constexpr uint32_t kCrouch=90,kAir=300,kSettle=180,kCycle=kCrouch+kAir+kSettle;
 static constexpr uint32_t kEntryDelay=260,kMinGap=4200,kGapRange=3800;
 static constexpr float kHeight=9;
 bool requested=false,running=false;
 uint8_t count=0;
 uint32_t start=0,next=0;
 // `allowed` pauses scheduling (button/startle cues) without cancelling.
 void update(uint32_t now,bool request,bool allowed,uint32_t seed) {
  if(!request) { requested=running=false;return; }
  if(!requested) { requested=true;running=false;count=2;next=now+kEntryDelay; }
  if(running&&now-start>=count*kCycle) {
   running=false;
   count=static_cast<uint8_t>(1+(seed>>9)%2);
   next=now+kMinGap+seed%kGapRange;
  }
  if(!running&&allowed&&static_cast<int32_t>(now-next)>=0) { running=true;start=now; }
 }
 // Phase inside the current hop; false while resting.
 bool phase(uint32_t now,uint32_t& t,float& scale) const {
  if(!running||now-start>=count*kCycle)return false;
  const uint32_t local=now-start;
  t=local%kCycle;
  scale=local/kCycle?.7f:1;  // a smaller second hop
  return true;
 }
 // Pixels above the resting pose.
 float height(uint32_t now) const {
  uint32_t t;float scale;
  if(!phase(now,t,scale)||t<kCrouch||t>=kCrouch+kAir)return 0;
  const float u=(t-kCrouch)/static_cast<float>(kAir);
  return kHeight*scale*4*u*(1-u);
 }
 // Signed body stretch: negative squashes (crouch/landing), positive
 // stretches while travelling fast; round at the apex.
 float stretch(uint32_t now) const {
  uint32_t t;float scale;
  if(!phase(now,t,scale))return 0;
  struct K { uint32_t ms;float value; };
  static constexpr K keys[]={{0,0},{kCrouch,-.07f},{kCrouch+60,.06f},{kCrouch+kAir/2,0},
      {kCrouch+kAir-30,.04f},{kCrouch+kAir,-.08f},{kCycle,0}};
  for(int i=1;i<7;++i)if(t<keys[i].ms) {
   const float u=eye_acting::smooth((t-keys[i-1].ms)/static_cast<float>(keys[i].ms-keys[i-1].ms));
   return scale*(keys[i-1].value+(keys[i].value-keys[i-1].value)*u);
  }
  return 0;
 }
};
} // namespace request_scene
