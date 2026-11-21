#pragma once
#include <math.h>
#include <stdint.h>
#include "status_sides.h"

// Shared by firmware and native previews. The two count colors blend spatially
// and fade into/out of that blend with the count-presence animations. Each
// status color sits on its status_sides screen side.
namespace status_background {
inline float clamp(float value) { return value<0?0:(value<1?value:1); }
inline float weight(int x) {
 const float t=clamp((x-32)/175.0f);
 return t*t*(3-2*t);
}
inline int quantize(float value,int maximum,float threshold) {
 value=value<0?0:(value<maximum?value:static_cast<float>(maximum));
 // Clamped nonnegative values make truncation exactly equivalent to floorf.
 const int whole=static_cast<int>(value);
 return whole+(value-whole>threshold && whole<maximum ? 1 : 0);
}
inline float channel(uint16_t color,int shift,int mask) {
 return static_cast<float>((color>>shift)&mask);
}
inline uint16_t color(int x,int y,uint16_t done,uint16_t active,
                      float donePresence,float activePresence,
                      uint16_t portrait=0,float portraitPresence=0,
                      uint16_t wash=0,float washPresence=0,
                      uint16_t alert=0,float alertPresence=0,bool dither=true,
                      uint16_t base=0) {
 donePresence=clamp(donePresence);activePresence=clamp(activePresence);
 portraitPresence=clamp(portraitPresence);washPresence=clamp(washPresence);
 alertPresence=clamp(alertPresence);
 const float paired=donePresence<activePresence?donePresence:activePresence;
 const float spatial=weight(x);
 // Static subpixel dithering softens RGB565 banding without animated grain.
 constexpr uint8_t bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
 // Desktop widgets can turn it off: they draw the gradient natively instead.
 const float threshold=dither?(bayer[static_cast<unsigned int>(y)&3][static_cast<unsigned int>(x)&3]+.5f)/16:.5f;
 const int shifts[]={11,5,0},masks[]={31,63,31};
 int result[3]={};
 for(int c=0;c<3;++c) {
  // Absent colors fade toward `base` (the face color: black, or light mode's
  // white). A zero base is bit-identical to fading toward black.
  const float empty=channel(base,shifts[c],masks[c]);
  const float background=empty+(channel(portrait,shifts[c],masks[c])-empty)*portraitPresence;
  const float completed=donePresence>=.01f?empty+(channel(done,shifts[c],masks[c])-empty)*donePresence:background;
  const float working=activePresence>=.01f?empty+(channel(active,shifts[c],masks[c])-empty)*activePresence:background;
  const float left=status_sides::kDone<0?completed:working;
  const float right=status_sides::kDone<0?working:completed;
  const float split=x<120?left:right;
  const float gradient=left+(right-left)*spatial;
  const float value=split+(gradient-split)*paired;
  // Exactly-zero wash leaves the established gradient bit-identical. A wash
  // floods the WHOLE backdrop (completion flash), dithered like the gradient.
  const float washed=washPresence>0?value+(channel(wash,shifts[c],masks[c])-value)*washPresence:value;
  // An input request floods the whole screen with its pulsing field, above
  // the counters' colors (which it hides). Exactly-zero keeps the old rows.
  result[c]=quantize(alertPresence>0?washed+(channel(alert,shifts[c],masks[c])-washed)*alertPresence:washed,
                     masks[c],threshold);
 }
 return static_cast<uint16_t>((result[0]<<11)|(result[1]<<5)|result[2]);
}
// Every dependency of color(), including transition/portrait inputs. Exact
// equality only: never freeze a nearly-settled fade or reuse an old coral row.
// Trailing wash/alert/base fields are value-initialized by shorter initializers.
struct State {
 uint16_t done,active,portrait;
 float donePresence,activePresence,portraitPresence;
 uint16_t wash;
 float washPresence;
 uint16_t alert;
 float alertPresence;
 uint16_t base;  // Field color where no status shows (0 = black).
 bool operator==(const State& other) const {
  return done==other.done && active==other.active && portrait==other.portrait &&
         donePresence==other.donePresence && activePresence==other.activePresence &&
         portraitPresence==other.portraitPresence && wash==other.wash &&
         washPresence==other.washPresence && alert==other.alert &&
         alertPresence==other.alertPresence && base==other.base;
 }
};
// A sorting numeral may become zero at the apex, before the wide face reflows.
// Slots are status slots (0 done, 1 active); `reclaimed*` says the face has
// shifted into that status's side.
// Retain its backdrop independently of digit visibility until the face has
// shifted into the vacated side, then ease it away behind the settled face.
// Do not wait for full-screen mask coverage: the organic rim can leave corners
// exposed even in the final single-counter layout.
struct SortingDeparture {
 bool held[2]={false,false};
 float presence[2]={0,0};
 void update(float done,float active,bool sorting,bool hasDone,bool hasActive,
             bool reclaimedDone,bool reclaimedActive,bool allowed,float seconds) {
  const float target[2]={done,active};
  const bool present[2]={hasDone,hasActive};
  const bool reclaimed[2]={reclaimedDone,reclaimedActive};
  for(int i=0;i<2;++i) {
   if(!allowed) { held[i]=false;presence[i]=target[i];continue; }
   if(sorting&&!present[i])held[i]=true;
   else if(present[i] || reclaimed[i])held[i]=false;
   presence[i]=held[i]?1:fmaxf(target[i],presence[i]*expf(-seconds/.12f));
   // Arrive exactly (as avatar_scene::approach does): the row cache is keyed
   // on exact presence and would otherwise miss for a long denormal tail.
   if(presence[i]-target[i]<1e-4f)presence[i]=target[i];
  }
 }
};
struct RowCache {
 State previous={0,0,0,0,0,0,0,0,0,0,0};
 bool valid=false;
 bool dither=true;  // The device always dithers; see color().
 template<class Pixel>
 bool update(const State& state,Pixel pixel) {
  if(valid && state==previous)return false;
  for(int y=0;y<4;++y)for(int x=0;x<240;++x)
   pixel(x,y,color(x,y,state.done,state.active,state.donePresence,state.activePresence,
                   state.portrait,state.portraitPresence,state.wash,state.washPresence,
                   state.alert,state.alertPresence,dither,state.base));
  previous=state;valid=true;return true;
 }
};
} // namespace status_background
