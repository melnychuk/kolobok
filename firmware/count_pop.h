#pragma once
#include <stdint.h>
#include "eye_acting.h"

// Counter-only anticipation, hop and small rebound. Use real font sizes, not
// stretched glyphs; retain the background-matched outline and black interiors.
// The former 90ms smaller-font swap lasted only two or three frames and read
// as a flicker plus a sideways jump, so the pop is now lift-only.
namespace count_pop {
struct Pop {
 int observed=0;
 uint32_t started=0;
 bool pending=false,running=false;
 void observe(int count) {
  if(count==observed)return;
  observed=count;pending=count>0;running=false;
 }
 void update(uint32_t now,bool readable) {
  if(pending&&readable){started=now;running=true;pending=false;}
  if(running&&now-started>=620)running=false;
 }
 float lift(uint32_t now) const {
  if(!running)return 0;
  const uint32_t t=now-started;
  if(t<70)return 2*eye_acting::smooth(t/70.0f);
  if(t<200)return 2-12*eye_acting::smooth((t-70)/130.0f);
  if(t<390)return -10+12*eye_acting::smooth((t-200)/190.0f);
  return 2*(1-eye_acting::smooth((t-390)/230.0f));
 }
};
} // namespace count_pop
