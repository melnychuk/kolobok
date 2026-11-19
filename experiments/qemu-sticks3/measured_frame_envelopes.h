#pragma once
#include <stdint.h>
#include <string.h>

// Historical, operator-declared physical Arduino/native-renderer occupancy
// candidates. See docs/render-performance.md, round 2 (2026-11-01).
// NOT Xtensa cycle calibration, USB grants or LCD scanout timing.
// These are opt-in regression inputs; never change default QEMU CPU/bus clocks.
namespace stick_measured {
struct Envelope {
 const char* buildId;
 const char* optimization;
 const char* mode;
 const char* sourceSha256;
 const char* binarySha256;
 uint32_t frames,renderP95Us,flushP95Us,preWireP95Us,startP95Us;
};
constexpr Envelope round2[]={
 {"a4978b47","Os","working/flags-1",
  "a4978b474251e17a9efdd3810439b50294e34bd8d6f1183fd9227f6c2649aaab",
  "97a769719bf917d1730f0e155f568ad6f85d356265f7f5553e530c5138aa27a4",
  1050,42807,13067,42910,60000},
 {"9d40fa19","O2","working/flags-1",
  "a4978b474251e17a9efdd3810439b50294e34bd8d6f1183fd9227f6c2649aaab",
  "8eaee58f1592b124c5f4279d7c371d2c99864fa3b6275efd59b147e7292a091e",
  1190,35615,13069,35723,52996}
};
// Clean 2026-11-01 working/vertical captures before/after caching. Contact
// behavior and live clip/label selection also differed; not a controlled A/B.
constexpr Envelope cachePass[]={
 {"7dfcc87d","O2","working/flags-1",
  "1db7acaaaaa1a8c83e116cc5293347b60e6155dcbaee6afc891d0d4b75bf31ae",
  "9cafdfef294f5f1512239cb6baec4c946321243dd074864002f22aef37804441",
  1410,28168,13080,28288,45000},
 {"2bf53c52","O2","working/flags-1",
  "6ebf35f2b0a3d556ef2989cd402bf180cbeebf238f8c3cc05b371e3dbc4b5826",
  "ec6fff2a459a826e4589c351e8128bdf79ce8a8e347fe12e4481136103a6f12f",
  1535,24083,13083,24199,41003}
};
constexpr const Envelope* all[]={&round2[0],&round2[1],&cachePass[0],&cachePass[1]};
// Prepared smaller-eye/optimized-kernel O2 snapshot b088706a, NOT measured.
// Keep separate from all[]: no host speedup may manufacture an occupancy entry.
constexpr uint32_t unmeasuredEyeBuildId=0xb088706au;
constexpr char unmeasuredEyeSource[]="faec76e48dc8aa95431a0b140add9e70feaf927a8cdd758a0284506092167c82";
constexpr char unmeasuredEyeBinary[]="5896c73f41fc5ee3727047cda7ba20442ecfe7ad25452dcc369e26502c4da622";
// Fail closed for changed kernels/recipes/layouts/binaries. Do not invent
// envelopes for other layouts or new revisions from host benchmark ratios.
inline const Envelope* findEnvelope(const char* source,const char* binary,
                                    const char* optimization,const char* mode) {
 if(!source||!binary||!optimization||!mode)return nullptr;
 for(const auto* envelope:all)
  if(strcmp(source,envelope->sourceSha256)==0 && strcmp(binary,envelope->binarySha256)==0 &&
     strcmp(optimization,envelope->optimization)==0 && strcmp(mode,envelope->mode)==0)
   return envelope;
 return nullptr;
}
// preWireP95Us is the report's effective envelope, not the sum of independent
// p95s or an exact physical frame-start interval. It includes non-wire overhead.
} // namespace stick_measured
