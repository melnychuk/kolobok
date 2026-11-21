#pragma once
#include <math.h>
#include <stdint.h>
#include "face_contour.h"

// CPU analogue of a domain-warped fluid shader. Spatially coherent, advected
// value noise moves through the coordinate field; no random shape retargets.
namespace fluid_face {
inline float fade(float t) { return t*t*t*(t*(t*6-15)+10); }
inline float mix(float a,float b,float t) { return a+(b-a)*t; }
// Every time-dependent lattice coordinate below shifts by a multiple of 16 when
// the flow clock advances by kFlowPeriod, so a 16-periodic lattice makes the
// whole field exactly periodic in time. The clock can then wrap and stay
// precise for any uptime (an unbounded float clock quantises after ~2 days and
// stops advancing after ~1-2 weeks). Spatially the lattice repeats only every
// ~440-890px, far beyond the 240px display.
constexpr float kFlowPeriod=8000;
constexpr int kLatticePeriod=16;
inline float advance(float flowClock,float delta) {
 flowClock+=delta;
 if(flowClock>=kFlowPeriod)flowClock-=kFlowPeriod;
 return flowClock;
}
inline float hash(int x,int y,int z) {
 x&=kLatticePeriod-1;y&=kLatticePeriod-1;z&=kLatticePeriod-1;
 uint32_t h=static_cast<uint32_t>(x)*0x8da6b343u ^ static_cast<uint32_t>(y)*0xd8163841u ^ static_cast<uint32_t>(z)*0xcb1ab31fu;
 h^=h>>13;h*=0x85ebca6bu;h^=h>>16;
 return static_cast<float>(h&0xffffu)/32767.5f-1;
}
// The time axis of each lattice lookup is constant for a whole frame. `Slice`
// holds its floor and fade once; the arithmetic is otherwise identical, so the
// sliced evaluation is bit-for-bit equal to noise(x,y,z).
struct Slice { int iz;float w; };
// hash() of every lattice point. The lattice repeats every 16 in x, y and z, so
// the field only ever needs these 4,096 values: computed once (16 KB) instead
// of eight hashes, each ending in a float division, per noise() call. They are
// the same values, so noise() stays bit-for-bit equal.
struct HashTable {
 float value[kLatticePeriod*kLatticePeriod*kLatticePeriod];
 HashTable() {
  for(int z=0;z<kLatticePeriod;++z)for(int y=0;y<kLatticePeriod;++y)for(int x=0;x<kLatticePeriod;++x)
   value[(z*kLatticePeriod+y)*kLatticePeriod+x]=hash(x,y,z);
 }
 float operator()(int x,int y,int z) const {
  constexpr int m=kLatticePeriod-1;
  return value[((z&m)*kLatticePeriod+(y&m))*kLatticePeriod+(x&m)];
 }
};
// Fetch it once per Frame, not per noise(): the ESP32 toolchain locks
// (__cxa_guard_acquire) on every pass through a function-local static.
inline const HashTable& hashTable() { static const HashTable table;return table; }
// static_cast<int>(floorf(x)) for any finite x in int range, without the call
// to floorf (a library function on the ESP32).
inline int floorInt(float x) { const int i=static_cast<int>(x);return x<i?i-1:i; }
inline Slice slice(float z) { const int iz=static_cast<int>(floorf(z));return {iz,fade(z-iz)}; }
inline float noise(float x,float y,const Slice& z,const HashTable& lattice) {
 const int ix=floorInt(x),iy=floorInt(y),iz=z.iz;
 const float u=fade(x-ix),v=fade(y-iy),w=z.w;
 const float a=mix(mix(lattice(ix,iy,iz),lattice(ix+1,iy,iz),u),mix(lattice(ix,iy+1,iz),lattice(ix+1,iy+1,iz),u),v);
 const float b=mix(mix(lattice(ix,iy,iz+1),lattice(ix+1,iy,iz+1),u),mix(lattice(ix,iy+1,iz+1),lattice(ix+1,iy+1,iz+1),u),v);
 return mix(a,b,w);
}
inline float noise(float x,float y,const Slice& z) { return noise(x,y,z,hashTable()); }
inline float noise(float x,float y,float z) { return noise(x,y,slice(z)); }
// Per-frame constants of field(); build once per mask, reuse for every row and
// column (~510 evaluations per paired frame).
struct Frame {
 float shiftX=0,shiftY=0;
 Slice warpX{0,0},warpY{0,0},low{0,0},high{0,0};
 const HashTable* lattice=nullptr;
 explicit Frame(float flowClock) {
  lattice=&hashTable();
  shiftX=flowClock*.12f;shiftY=flowClock*.06f;
  warpX=slice(flowClock*.08f+17);warpY=slice(flowClock*.08f);
  low=slice(flowClock*.17f);high=slice(flowClock*.26f);
 }
};
inline float field(float x,float y,const Frame& f) {
 const float px=x*.018f+f.shiftX,py=y*.018f-f.shiftY;
 const HashTable& lattice=*f.lattice;
 const float qx=noise(px*.6f,py*.6f,f.warpX,lattice)*.80f;
 const float qy=noise(px*.6f+9,py*.6f-7,f.warpY,lattice)*.80f;
 const float a=px+qx,b=py+qy;
 // Rotate the higher octave to suppress grid/axis artifacts.
 return .67f*noise(a,b,f.low,lattice)+.33f*noise(1.6f*a-1.2f*b+13,1.2f*a+1.6f*b-5,f.high,lattice);
}
inline float field(float x,float y,float flowClock) { return field(x,y,Frame(flowClock)); }
inline bool bounds(float cx,float cy,float rx,float ry,float row,float flowClock,
                   float energy,float lean,float& left,float& right) {
 if(rx<=0||ry<=0) return false;
 const float ny=(row-cy)/ry;
 if(fabsf(ny)>=1) return false;
 const float half=rx*sqrtf(1-ny*ny);
 const float shear=lean*ny*.35f;
 left=cx-half+shear+12*energy*field(cx-half,row,flowClock);
 right=cx+half+shear+12*energy*field(cx+half,row,flowClock);
 return right>left;
}
} // namespace fluid_face
