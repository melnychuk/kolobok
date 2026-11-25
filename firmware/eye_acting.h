#pragma once
#include <stdint.h>
#include <math.h>
#include "status_sides.h"

// Eye-only cartoon acting: motivated saccades, holds, anticipation and overlap.
// Same keyframes are sampled by firmware, native tests, and visual previews.
namespace eye_acting {
enum class State : uint8_t { thinking, done, attention, listening };
constexpr unsigned kStateCount=4;
struct Pose {
  float x, y, width, height, upperL, upperR, smile, closeL, closeR;
  float bodyWidth, bodyHeight, lean, lift, flow, tilt;
  // Additive scale deltas keep zero-initialized/legacy poses neutral.
  float scaleL,scaleR,lowerL,lowerR,slantL,slantR;
};
constexpr Pose p(float x=0, float y=0, float w=1, float h=1,
                 float l=0, float r=0, float smile=0, float cl=0, float cr=0,
                 float bw=1, float bh=1, float lean=0, float lift=0,
                 float flow=1, float tilt=0) {
  return {x,y,w,h,l,r,smile,cl,cr,bw,bh,lean,lift,flow,tilt,0,0,0,0,0,0};
}
// Per-eye shaping for authored keys: scale multipliers (1 = neutral), lower
// lids (cheek/concentration) and lid slant (+ inner corners down/intent,
// - inner corners up/pleading). Asymmetry is what keeps a pose from reading
// as a symbol; keep it small and motivated.
constexpr Pose shape(Pose b,float sl,float sr,float ll=0,float lr=0,float tl=0,float tr=0) {
  return {b.x,b.y,b.width,b.height,b.upperL,b.upperR,b.smile,b.closeL,b.closeR,
          b.bodyWidth,b.bodyHeight,b.lean,b.lift,b.flow,b.tilt,sl-1,sr-1,ll,lr,tl,tr};
}
struct Key { uint16_t ms; Pose pose; };
// Named modifiers keep new acting readable without rewriting legacy key arrays.
inline Pose withGaze(Pose pose,float x,float y) { pose.x=x;pose.y=y;return pose; }
inline Pose withLids(Pose pose,float left,float right) { pose.upperL=left;pose.upperR=right;return pose; }
inline Pose withBody(Pose pose,float lean,float lift) { pose.lean=lean;pose.lift=lift;return pose; }
inline Pose withEyeScale(Pose pose,float left,float right) { pose.scaleL=left-1;pose.scaleR=right-1;return pose; }
inline Pose withLowerLids(Pose pose,float left,float right) { pose.lowerL=left;pose.lowerR=right;return pose; }
inline Pose withSlant(Pose pose,float left,float right) { pose.slantL=left;pose.slantR=right;return pose; }
inline Pose swapEyes(Pose pose) {
 const Pose s=pose;
 pose.upperL=s.upperR;pose.upperR=s.upperL;pose.closeL=s.closeR;pose.closeR=s.closeL;
 pose.scaleL=s.scaleR;pose.scaleR=s.scaleL;pose.lowerL=s.lowerR;pose.lowerR=s.lowerL;
 pose.slantL=s.slantR;pose.slantR=s.slantL;  // the raster mirrors slant per eye
 return pose;
}
// Left-right mirror image: gaze, lean and tilt flip; per-eye fields swap eyes.
inline Pose mirrored(Pose pose) {
 pose=swapEyes(pose);pose.x=-pose.x;pose.lean=-pose.lean;pose.tilt=-pose.tilt;
 return pose;
}
// Status-directed acting authored for the original done-left layout, shown in
// the configured one (status_sides): it keeps pointing at the same counter.
inline Pose authored(const Pose& pose) {
 return status_sides::kMirrorAuthored?mirrored(pose):pose;
}
// authoredDoneLeft: the clip glances, leans or winks toward a status counter
// as laid out originally (done left, active right); sampling orients it.
enum ClipFlag : uint8_t { ordinary=0, celebration=1, authoredDoneLeft=2 };
struct Clip {
 const char* name; const Key* keys; uint8_t count,flags;
 constexpr Clip(const char* n,const Key* k,uint8_t c,uint8_t f=ordinary):name(n),keys(k),count(c),flags(f) {}
};
template <unsigned N> constexpr Clip makeClip(const char* name,const Key (&keys)[N],uint8_t flags=ordinary) {
 return Clip(name,keys,static_cast<uint8_t>(N),flags);
}

// THINKING: nine different cognitive intentions; asymmetry never anger.
// Lower lids are concentration (reading, focus, pondering); a single bigger,
// unlidded eye is a raised brow. Sideways perspective is NOT authored here:
// avatar_scene derives it from gaze for every pose (kGazePerspective).
constexpr Key recall[] = {
 {0,p()}, {180,p(0,3,1.04f,.92f,0,0,0,0,0,1.03f,.94f,0,2,.6f)},
 {340,shape(p(-11,-8,1,1.06f,0,.22f,0,0,0,.98f,1.04f,-4,-2,.6f),1,1,0,.10f)},
 {1120,shape(p(-11,-8,1,1.06f,0,.22f,0,0,0,.98f,1.04f,-4,-2,.6f),1,1,0,.10f)},
 {1210,p(-11,-8,1,1,0,0,0,1,1)}, {1350,p(8,-6,1,1.04f)},
 {2070,p(8,-6,1,1.04f,0,0,0,0,0,.99f,1.03f,3,-1,.7f)}, {2700,p()}};
constexpr Key compare[] = {
 {0,p()}, {140,p(2,0)}, {250,p(-12,1,1,.98f,0,.12f)},
 {850,p(-12,1,1,.98f,0,.12f,0,0,0,.96f,1.02f,-4,0,.7f)},
 {990,p(12,1,1,.98f,.12f,0)}, {1560,p(12,1,1,.98f,.12f,0,0,0,0,.96f,1.02f,4,0,.7f)},
 // Weigh the two: a small undecided tilt and an appraising lower-lid squint.
 {1760,shape(p(0,-1,1.03f,1.02f,.06f,.06f,0,0,0,1.02f,1.01f,0,-1,.55f,5),1,1,.10f,.06f)},
 {2180,shape(p(0,-1,1.03f,1.02f,.06f,.06f,0,0,0,1.02f,1.01f,0,-1,.55f,5),1,1,.10f,.06f)}, {2620,p()}};
// Reading squint: lower lids up a little while the gaze steps down the lines.
constexpr Key read[] = {
 {0,p()}, {180,shape(p(-11,3,1,.97f,.10f,.10f),1,1,.12f,.12f)},
 {760,shape(p(-11,3,1,.97f,.10f,.10f),1,1,.12f,.12f)},
 {860,shape(p(0,4,1,.97f,.10f,.10f),1,1,.12f,.12f)}, {1190,shape(p(0,4,1,.97f,.10f,.10f),1,1,.12f,.12f)},
 {1290,shape(p(11,5,1,.97f,.10f,.10f),1,1,.12f,.12f)},
 {1640,shape(p(11,5,1,.97f,.10f,.10f,0,0,0,1.02f,.98f,3,1,.65f),1,1,.12f,.12f)},
 {1740,p(-9,7,1,1,0,0,0,1,1)}, {1880,p(-9,7)}, {2500,p()}};
// Double-check: one narrowed, one raised eye at the hunch; back; glance AGAIN
// more skeptically; then a sure nod with both eyes even.
constexpr Key verify[] = {
 {0,p()}, {180,shape(p(10,-2,1,.99f,.18f,.03f,0,0,0,.96f,1.03f,4,0,.55f),.97f,1.04f,.12f,0)},
 {880,shape(p(10,-2,1,.99f,.18f,.03f,0,0,0,.96f,1.03f,4,0,.55f),.97f,1.04f,.12f,0)},
 {990,p(0,0,1.02f,1)}, {1240,p(0,0,1.02f,1)},
 {1330,shape(p(11,-1,1,.98f,.20f,.03f,0,0,0,.96f,1.03f,4,0,.55f),.96f,1.05f,.16f,0)},
 {1760,shape(p(11,-1,1,.98f,.20f,.03f,0,0,0,.96f,1.03f,4,0,.55f),.96f,1.05f,.16f,0)},
 {1960,p(0,4,1.05f,.94f,0,0,0,0,0,1.03f,.96f,0,2,.5f)},
 {2200,p(0,-2,1,1.04f,0,0,0,0,0,.99f,1.02f,0,-1,.6f)}, {2800,p()}};
// Ponder downward under heavy lids, then the idea pops the eyes up and open.
constexpr Key insight[] = {
 {0,p()}, {320,shape(p(2,7,1.02f,.92f,.30f,.30f,0,0,0,1.02f,.97f,0,2,.4f),1,1,.10f,.10f)},
 {1080,shape(p(2,7,1.02f,.92f,.30f,.30f,0,0,0,1.02f,.97f,0,2,.4f),1,1,.10f,.10f)},
 {1180,p(0,-9,.94f,1.20f,0,0,0,0,0,.94f,1.12f,0,-6,1.2f)},
 {1380,p(0,-6,.97f,1.14f,0,0,0,0,0,.97f,1.08f,0,-4,1.0f)},
 {1820,p(0,-6,.97f,1.14f,0,0,0,0,0,.97f,1.08f,0,-4,1.0f)},
 {2160,p(0,1,1.02f,.98f)}, {2600,p()}};
// Count it out: small rhythmic steps across with a tick-tock head tilt and a
// working squint, then a decided nod.
constexpr Key calculate[] = {
 {0,p()}, {200,shape(p(-9,-6,1,1.03f,0,0,0,0,0,.99f,1.02f,-2,-1,.6f,-2),1,1,.08f,.08f)},
 {420,shape(p(-9,-6,1,1.03f,0,0,0,0,0,.99f,1.02f,-2,-1,.6f,-2),1,1,.08f,.08f)},
 {520,shape(p(-3,-6,1,1.03f,0,0,0,0,0,1,1,0,0,1,1),1,1,.08f,.08f)},
 {700,shape(p(-3,-6,1,1.03f,0,0,0,0,0,1,1,0,0,1,1),1,1,.08f,.08f)},
 {800,shape(p(3,-6,1,1.03f,0,0,0,0,0,1,1,0,0,1,-1),1,1,.08f,.08f)},
 {980,shape(p(3,-6,1,1.03f,0,0,0,0,0,1,1,0,0,1,-1),1,1,.08f,.08f)},
 {1080,shape(p(9,-6,1,1.03f,0,0,0,0,0,.99f,1.02f,2,-1,.6f,2),1,1,.08f,.08f)},
 {1420,shape(p(9,-6,1,1.03f,0,0,0,0,0,.99f,1.02f,2,-1,.6f,2),1,1,.08f,.08f)},
 {1600,p(2,3,1.05f,.94f,.08f,.08f,0,0,0,1.03f,.96f,0,2,.5f)},
 {1840,p(0,-1,1,1.03f,0,0,0,0,0,1,1.01f,0,-1,.6f)}, {2700,p()}};
// Narrowed from above AND below: intent concentration, not sleepiness.
constexpr Key focus[] = {
 {0,p()}, {420,shape(p(0,1,1.08f,.88f,.14f,.14f,0,0,0,1.04f,.97f,0,1,.35f),1,1,.18f,.18f)},
 {1890,shape(p(0,1,1.08f,.88f,.14f,.14f,0,0,0,1.04f,.97f,0,1,.35f),1,1,.18f,.18f)},
 {1980,p(0,1,1,1.05f,0,0,0,1,1)}, {2150,p(0,-2,1,1.05f)}, {2850,p()}};

// A tilted, one-lid study (the other eye opened a touch wider, quizzical):
// stop and consider, then straighten with a small nod.
constexpr Key untangle[] = {
 {0,p()}, {200,p(0,3,1.05f,.94f,0,0,0,0,0,1.03f,.97f,0,2,.5f)},
 {430,shape(p(-6,-3,1,1.04f,.04f,.24f,0,0,0,.98f,1.03f,-3,-1,.4f,-6),1.04f,.97f,0,.10f)},
 {1460,shape(p(-6,-3,1,1.04f,.04f,.24f,0,0,0,.98f,1.03f,-3,-1,.4f,-6),1.04f,.97f,0,.10f)},
 {1560,p(-6,-3,1,1,0,0,0,1,1,.98f,1.03f,-3,-1,.4f,-6)},
 {1750,p(0,-4,1.03f,1.10f,0,0,0,0,0,1,1.05f,0,-2,.65f)},
 {2170,p(0,-4,1.03f,1.10f,0,0,0,0,0,1,1.05f,0,-2,.65f)},
 {2350,p(0,2,1.04f,.96f,0,0,0,0,0,1.03f,.98f,0,1,.5f)}, {3020,p()}};
// Trace a diagonal between two ideas, hold each, then bring them into focus.
constexpr Key connect[] = {
 {0,p()}, {240,p(-10,5,1.04f,.96f,.12f,.12f,0,0,0,1.02f,.98f,-3,1,.45f,-2)},
 {910,p(-10,5,1.04f,.96f,.12f,.12f,0,0,0,1.02f,.98f,-3,1,.45f,-2)},
 {1070,p(10,-7,.98f,1.12f,0,0,0,0,0,.98f,1.05f,3,-2,.7f,2)},
 {1750,p(10,-7,.98f,1.12f,0,0,0,0,0,.98f,1.05f,3,-2,.7f,2)},
 {1940,shape(p(0,-2,1.08f,.94f,.16f,.16f,0,0,0,1.04f,.98f,0,0,.4f),1,1,.14f,.14f)},
 {2540,shape(p(0,-2,1.08f,.94f,.16f,.16f,0,0,0,1.04f,.98f,0,0,.4f),1,1,.14f,.14f)}, {3140,p()}};

// DONE: all seven are positive, never sad/annoyed "completed" poses: no
// upper lids or slant here. Happiness lives in the cheeks (smile + lower
// lids) and in the closures, which the scene draws as soft tapered creases.
// Exhale: a small inhale, the eyes sink shut as the body settles, then they
// reopen content with the cheeks still lifted.
constexpr Key relief[] = {
 {0,p()}, {220,p(0,-3,.98f,1.08f,0,0,.45f,0,0,.97f,1.05f,0,-3,.6f)},
 {560,p(0,3,1.06f,.94f,0,0,.55f,1,1,1.07f,.94f,0,4,.4f)},
 {900,p(0,4,1.06f,.94f,0,0,.55f,1,1,1.08f,.93f,0,5,.35f)},
 {1180,shape(p(0,2,1.06f,.96f,0,0,.60f,0,0,1.06f,.96f,0,2,.5f),1,1,.12f,.12f)},
 {1920,shape(p(0,2,1.06f,.96f,0,0,.60f,0,0,1.06f,.96f,0,2,.5f),1,1,.12f,.12f)}, {2820,p(0,0,1,1,0,0,.45f)}};
// Chin up with lifted cheeks, then a small self-assured nod.
constexpr Key proud[] = {
 {0,p()}, {180,p(0,3,1.06f,.93f,0,0,.45f,0,0,1.05f,.94f,0,2,.7f)},
 {460,shape(p(0,-7,1.02f,1.10f,0,0,.62f,0,0,.98f,1.08f,0,-5,.85f,3),1,1,.14f,.14f)},
 {1450,shape(p(0,-7,1.02f,1.10f,0,0,.62f,0,0,.98f,1.08f,0,-5,.85f,3),1,1,.14f,.14f)},
 {1750,shape(p(0,-4,1.04f,1.05f,0,0,.68f,0,0,1,1.05f,0,-3,.8f,2),1,1,.16f,.16f)},
 {2050,shape(p(0,1,1.05f,.98f,0,0,.68f,0,0,1.03f,.98f,0,1,.7f),1,1,.18f,.18f)},
 {2400,shape(p(0,-2,1.03f,1.03f,0,0,.62f,0,0,1.01f,1.02f,0,-1,.75f),1,1,.10f,.10f)}, {3000,p(0,0,1,1,0,0,.45f)}};
// Squashes push the cheeks up; stretches open the eyes.
constexpr Key bounce[] = {
 {0,p()}, {170,shape(p(0,5,1.14f,.80f,0,0,.45f,0,0,1.12f,.86f,0,4,.8f),1,1,.16f,.16f)},
 {330,p(0,-8,.94f,1.18f,0,0,.62f,0,0,.95f,1.14f,0,-6,1.2f)},
 {560,shape(p(0,3,1.07f,.92f,0,0,.68f,0,0,1.06f,.96f,0,2,1.0f),1,1,.12f,.12f)},
 {780,p(0,-4,.98f,1.09f,0,0,.66f,0,0,.98f,1.06f,0,-3,1.0f)},
 {1090,shape(p(0,1,1.03f,.98f,0,0,.66f),1,1,.08f,.08f)},
 {2080,shape(p(0,1,1.03f,.98f,0,0,.66f),1,1,.08f,.08f)}, {2700,p(0,0,1,1,0,0,.45f)}};
// Melt: a slow, tilted, softening smile; the eyes close one after the other
// into a long held crease.
constexpr Key warmth[] = {
 {0,p()}, {500,shape(p(0,1,1.03f,.97f,0,0,.6f,0,0,1.03f,.99f,0,1,.45f,4),1,1,.06f,.06f)},
 {1300,shape(p(1,0,1.05f,1,0,0,.75f,0,0,1.04f,1,1,0,.45f,6),1,1,.12f,.12f)},
 {1900,shape(p(1,0,1.05f,1,0,0,.75f,0,0,1.04f,1,1,0,.45f,6),1,1,.12f,.12f)},
 {2250,p(1,1,1.02f,1,0,0,.75f,1,.85f,1.04f,1,1,1,.45f,6)},
 {2420,p(1,1,1.02f,1,0,0,.75f,1,1,1.04f,1,1,1,.45f,6)},
 {2650,p(1,1,1.02f,1,0,0,.75f,1,1,1.04f,1,1,1,.45f,6)},
 {3000,p(0,0,1.02f,1,0,0,.6f,0,0,1.01f,1,0,0,.5f,2)}, {3400,p(0,0,1,1,0,0,.45f)}};
// A deliberate wink: lean and tilt into it; the winking eye scrunches smaller
// as its cheek lifts while the open eye stays bright, then a pleased settle.
constexpr Key wink[] = {
 {0,p()}, {220,shape(p(3,-2,1.04f,1.04f,0,0,.5f,0,0,1,1.04f,-2,-1,.8f,-2),1,1.02f,.08f,0)},
 {380,shape(p(3,-1,1.04f,1.04f,0,0,.55f,1,0,1,1.04f,-3,0,.8f,-5),.92f,1.05f,0,.12f)},
 {720,shape(p(3,-1,1.04f,1.04f,0,0,.55f,1,0,1,1.04f,-3,0,.8f,-5),.92f,1.05f,0,.12f)},
 {900,shape(p(2,-2,1.04f,1.04f,0,0,.6f,0,0,1,1.02f,-1,-1,.8f,-1),1,1,.12f,.12f)},
 {1810,shape(p(2,-2,1.04f,1.04f,0,0,.6f),1,1,.12f,.12f)}, {2600,p(0,0,1,1,0,0,.45f)}};
// Giggle: two happy squeezes, each with a little dip, the second deeper.
constexpr Key doubleHappy[] = {
 {0,p()}, {250,shape(p(0,-3,1.05f,1.04f,0,0,.6f,0,0,1.04f,1.03f,0,-2,.8f),1,1,.08f,.08f)},
 {720,shape(p(0,-3,1.05f,1.04f,0,0,.6f),1,1,.08f,.08f)},
 {820,p(0,-1,1.02f,1,0,0,.55f,1,1,1.05f,.97f,0,2,.8f)},
 {990,shape(p(0,-3,1.04f,1.03f,0,0,.62f,0,0,1.02f,1.03f,0,-2,.8f),1,1,.10f,.10f)},
 {1220,p(0,0,1.02f,1,0,0,.6f,1,1,1.06f,.96f,0,3,.8f)},
 {1420,shape(p(0,-1,1.04f,1.03f,0,0,.65f,0,0,1.01f,1.01f,0,-1,.8f),1,1,.14f,.14f)},
 {2250,shape(p(0,-1,1.04f,1.03f,0,0,.65f),1,1,.14f,.14f)}, {2850,p(0,0,1,1,0,0,.45f)}};
// Present it: anticipate away, then turn to the completed counter with a big
// tilted smile, come back to the viewer and close happily. Directed: authored
// with the completed counter on the left.
constexpr Key tada[] = {
 {0,p()}, {200,p(4,2,1.04f,.96f,0,0,.5f,0,0,1.03f,.97f,3,2,.7f)},
 {420,shape(p(-12,-5,1.08f,1.08f,0,0,.72f,0,0,1.02f,1.06f,-6,-3,.95f,-6),1,1,.10f,.10f)},
 {1300,shape(p(-12,-5,1.08f,1.08f,0,0,.72f,0,0,1.02f,1.06f,-6,-3,.95f,-6),1,1,.10f,.10f)},
 {1500,shape(p(2,-2,1.06f,1.04f,0,0,.7f,0,0,1.03f,1.02f,1,-1,.8f,3),1,1,.12f,.12f)},
 {2100,shape(p(2,-2,1.06f,1.04f,0,0,.7f,0,0,1.03f,1.02f,1,-1,.8f,3),1,1,.12f,.12f)},
 {2240,p(2,-1,1.02f,1,0,0,.7f,1,1,1.03f,1.02f,1,0,.8f,3)},
 {2420,shape(p(0,-1,1.04f,1.03f,0,0,.66f),1,1,.10f,.10f)}, {3000,p(0,0,1,1,0,0,.45f)}};

// ATTENTION: direct engagement, questioning, repeated requests, not anger.
constexpr Key engage[] = {
 {0,p()}, {140,p(0,4,1.16f,.88f,0,0,0,0,0,1.06f,.92f,0,2,1)},
 {320,p(0,-2,1.14f,1.24f,0,0,0,0,0,1.08f,1.10f,0,-3,1.5f)},
 {1370,p(0,-2,1.14f,1.24f,0,0,0,0,0,1.08f,1.10f,0,-3,1.5f)},
 {1480,p(0,-2,1,1,0,0,0,1,1)}, {1640,p(0,-2,1.14f,1.24f)}, {2470,p(0,0,1.10f,1.15f)}};
// "Huh?" squint on the two side looks makes the final wide pop land harder.
constexpr Key doubleTake[] = {
 {0,p()}, {150,shape(p(-14,0,1.06f,1.08f,0,0,0,0,0,.98f,1.04f,-4,0,1.2f),1,1,.14f,.14f)},
 {520,shape(p(-14,0,1.06f,1.08f),1,1,.14f,.14f)}, {630,shape(p(14,0,1.06f,1.08f),1,1,.10f,.10f)},
 {1000,shape(p(14,0,1.06f,1.08f,0,0,0,0,0,.98f,1.04f,4,0,1.2f),1,1,.10f,.10f)},
 {1110,p(0,0,1,1,0,0,0,1,1)}, {1310,p(0,-2,1.16f,1.25f,0,0,0,0,0,1.10f,1.10f,0,-2,1.5f)}, {2320,p(0,0,1.10f,1.15f)}};
constexpr Key knock[] = {
 {0,p()}, {220,p(0,5,1.13f,.88f,0,0,0,0,0,1.08f,.91f,0,4,1.2f)},
 {410,p(0,-5,1.02f,1.17f,0,0,0,0,0,.98f,1.08f,0,-4,1.4f)},
 {720,p(0,4,1.10f,.91f,0,0,0,0,0,1.06f,.94f,0,3,1.2f)},
 {920,p(0,-3,1.05f,1.15f,0,0,0,0,0,1.02f,1.06f,0,-2,1.4f)}, {1920,p(0,-3,1.05f,1.15f)}, {2460,p(0,0,1.10f,1.15f)}};
// Quizzical: the higher eye opens a little bigger (a raised brow), the lower
// one narrows from above; the head tilts one way, then the other.
constexpr Key question[] = {
 {0,p()}, {220,shape(p(-7,-2,1.10f,1.14f,.10f,0,0,0,0,.98f,1.05f,-4,-1,1.2f,-5),.95f,1.06f)},
 {1110,shape(p(-7,-2,1.10f,1.14f,.10f,0,0,0,0,.98f,1.05f,-4,-1,1.2f,-5),.95f,1.06f)},
 {1390,shape(p(7,-2,1.10f,1.14f,0,.10f,0,0,0,.98f,1.05f,4,-1,1.2f,5),1.06f,.95f)},
 {2070,shape(p(7,-2,1.10f,1.14f,0,.10f,0,0,0,.98f,1.05f,4,-1,1.2f,5),1.06f,.95f)}, {2530,p(0,0,1.10f,1.15f)}};
constexpr Key peek[] = {
 {0,p()}, {220,p(0,2,1.08f,1,0,0,0,1,1,1.04f,.97f,0,2,1.0f)},
 {280,p(0,2,1.08f,1,0,0,0,1,1,1.04f,.97f,0,2,1.0f)},
 {460,p(0,-3,1.15f,1.25f,0,0,0,0,0,1.08f,1.10f,0,-3,1.4f)},
 {760,p(0,-3,1.08f,1.15f,0,0,0,.85f,.85f)},
 {920,p(0,-3,1.15f,1.25f,0,0,0,0,0,1.08f,1.10f,0,-3,1.4f)},
 {2200,p(0,-3,1.15f,1.25f)}, {2700,p(0,0,1.10f,1.15f)}};
// Point out the alert: look up at the yellow band, then pop back down at you.
constexpr Key scanRequest[] = {
 {0,p()}, {200,p(0,-11,1.06f,1.08f,0,0,0,0,0,.98f,1.06f,0,-3,1.2f)},
 {800,p(0,-11,1.06f,1.08f,0,0,0,0,0,.98f,1.06f,0,-3,1.2f)},
 {920,p(0,2,1.12f,.92f,0,0,0,0,0,1.06f,.95f,0,2,1.3f)},
 {1120,p(0,-2,1.16f,1.24f,0,0,0,0,0,1.08f,1.08f,0,-2,1.5f)},
 {1600,p(0,-11,1.06f,1.08f,0,0,0,0,0,.98f,1.06f,0,-3,1.2f)},
 {1900,p(0,-11,1.06f,1.08f,0,0,0,0,0,.98f,1.06f,0,-3,1.2f)},
 {2020,p(0,-2,1.16f,1.24f,0,0,0,0,0,1.08f,1.08f,0,-2,1.5f)},
 {2400,p(0,-2,1.16f,1.24f)}, {2740,p(0,0,1.10f,1.15f)}};
// Eager side-to-side wiggle with wide pleading eyes (inner corners raised).
constexpr Key please[] = {
 {0,p()}, {240,shape(p(-6,-3,1.12f,1.20f,0,0,0,0,0,1.04f,1.06f,-3,-2,1.3f,-3),1,1,0,0,-.32f,-.32f)},
 {500,shape(p(6,-3,1.12f,1.20f,0,0,0,0,0,1.04f,1.06f,3,-2,1.3f,3),1,1,0,0,-.32f,-.32f)},
 {760,shape(p(-6,-3,1.12f,1.20f,0,0,0,0,0,1.04f,1.06f,-3,-2,1.3f,-3),1,1,0,0,-.32f,-.32f)},
 {1020,shape(p(6,-3,1.12f,1.20f,0,0,0,0,0,1.04f,1.06f,3,-2,1.3f,3),1,1,0,0,-.32f,-.32f)},
 {1280,shape(p(-6,-3,1.12f,1.20f,0,0,0,0,0,1.04f,1.06f,-3,-2,1.3f,-3),1,1,0,0,-.32f,-.32f)},
 {1560,shape(p(0,-4,1.14f,1.22f,0,0,0,0,0,1.06f,1.10f,0,-3,1.25f),1,1,0,0,-.38f,-.38f)},
 {2060,shape(p(0,-4,1.14f,1.22f,0,0,0,0,0,1.06f,1.10f,0,-3,1.25f),1,1,0,0,-.38f,-.38f)},
 {2160,p(0,-4,1,1,0,0,0,1,1)}, {2340,p(0,-2,1.12f,1.20f)}, {2860,p(0,0,1.10f,1.15f)}};

// LISTENING: quiet receptive holds, small ear-like turns and acknowledgements.
// Clock-authored poses only. While these play, the eyes themselves render as
// a soundwave (listen_wave.h) that follows on-device dictation loudness.
constexpr Key listenReady[] = {
 {0,p()}, {220,p(0,-3,1.02f,1.16f,0,0,.12f,0,0,1.02f,1.04f,0,-2,.45f)},
 {1380,p(0,-3,1.02f,1.16f,0,0,.12f,0,0,1.02f,1.04f,0,-2,.45f)},
 {1480,p(0,-3,1,1,0,0,.12f,1,1)}, {1650,p(0,-2,1.02f,1.12f)}, {2800,p(0,-2,1,1.08f)} };
constexpr Key listenLeft[] = {
 {0,p(0,-2,1,1.08f)}, {280,p(-7,-3,1,1.12f,0,0,.10f,0,0,.99f,1.03f,-3,-1,.45f,-3)},
 {1550,p(-7,-3,1,1.12f,0,0,.10f,0,0,.99f,1.03f,-3,-1,.45f,-3)},
 {1650,p(-7,-3,1,1,0,0,.1f,1,1)}, {1840,p(0,-2,1,1.10f)}, {3000,p(0,-2,1,1.08f)} };
constexpr Key listenRight[] = {
 {0,p(0,-2,1,1.08f)}, {300,p(7,-3,1,1.12f,0,0,.10f,0,0,.99f,1.03f,3,-1,.45f,3)},
 {1600,p(7,-3,1,1.12f,0,0,.10f,0,0,.99f,1.03f,3,-1,.45f,3)},
 {1710,p(7,-3,1,1,0,0,.1f,1,1)}, {1900,p(0,-2,1,1.10f)}, {3100,p(0,-2,1,1.08f)} };
// Two small acknowledging nods, no blink.
constexpr Key listenNod[] = {
 {0,p(0,-2,1,1.08f)}, {500,p(0,-2,1,1.08f)},
 {680,p(0,3,1.03f,1.02f,0,0,.18f,0,0,1.03f,.98f,0,2,.5f)},
 {900,p(0,-2,1,1.10f,0,0,.18f,0,0,1,1.02f,0,-1,.5f)},
 {1080,p(0,2,1.02f,1.02f,0,0,.18f,0,0,1.02f,.99f,0,1,.5f)},
 {1320,p(0,-3,1,1.13f,0,0,.18f,0,0,1,1.04f,0,-2,.5f)},
 {2000,p(0,-3,1,1.13f,0,0,.18f)}, {2900,p(0,-2,1,1.08f)} };
// Take it in: drift down and slowly across while processing, blink on the way
// back up.
constexpr Key listenConsider[] = {
 {0,p(0,-2,1,1.08f)}, {420,shape(p(-8,4,1.04f,1.02f,.05f,.05f,0,0,0,1.02f,1,-2,1,.35f),1,1,.10f,.10f)},
 {1350,shape(p(2,5,1.04f,1.02f,.05f,.05f,0,0,0,1.02f,1,1,1,.35f),1,1,.10f,.10f)},
 {1480,p(2,2,1,1.02f,0,0,0,1,1)},
 {1660,p(0,-3,1,1.13f,0,0,.15f)}, {2300,p(0,-3,1,1.13f,0,0,.15f)}, {3200,p(0,-2,1,1.08f)} };
// A slow, reassuring happy blink.
constexpr Key listenWarm[] = {
 {0,p(0,-2,1,1.08f)}, {480,shape(p(0,-2,1.03f,1.10f,0,0,.30f,0,0,1.03f,1.02f,0,-1,.4f),1,1,.08f,.08f)},
 {1500,shape(p(0,-2,1.03f,1.10f,0,0,.30f,0,0,1.03f,1.02f,0,-1,.4f),1,1,.08f,.08f)},
 {1720,p(0,-2,1,1,0,0,.50f,1,1)}, {1760,p(0,-2,1,1,0,0,.50f,1,1)},
 {2120,p(0,-2,1.03f,1.10f,0,0,.25f)}, {3300,p(0,-2,1,1.08f)} };
// Patient: settle a little lower and wider, then a slow drift; no blink.
constexpr Key listenSettle[] = {
 {0,p(0,-2,1,1.08f)}, {420,p(0,0,1.04f,1.06f,0,0,.10f,0,0,1.03f,.99f,0,1,.3f)},
 {1600,p(0,0,1.04f,1.06f,0,0,.10f,0,0,1.03f,.99f,0,1,.3f)},
 {2300,p(3,-1,1.03f,1.08f,0,0,.12f,0,0,1.02f,1,1,0,.3f,2)},
 {2700,p(3,-1,1.03f,1.08f,0,0,.12f,0,0,1.02f,1,1,0,.3f,2)}, {3500,p(0,-2,1,1.08f)} };

#define EYE_CLIP(name, keys) makeClip(name,keys)
constexpr Clip thinking[] = {
 EYE_CLIP("Recall",recall), EYE_CLIP("Compare options",compare), EYE_CLIP("Read and scan",read),
 EYE_CLIP("Verify a hunch",verify), EYE_CLIP("Aha insight",insight), EYE_CLIP("Calculate",calculate), EYE_CLIP("Deep focus",focus),
 EYE_CLIP("Work through it",untangle), EYE_CLIP("Connect the dots",connect)};
constexpr Clip completed[] = {
 EYE_CLIP("Relief",relief), EYE_CLIP("Proud of it",proud), makeClip("Joyful bounce",bounce,celebration),
 EYE_CLIP("Warm smile",warmth), EYE_CLIP("Playful wink",wink), EYE_CLIP("Happy double blink",doubleHappy), makeClip("Ta-da",tada,celebration|authoredDoneLeft)};
constexpr Clip requesting[] = {
 EYE_CLIP("Look at me",engage), EYE_CLIP("Double take",doubleTake), EYE_CLIP("Knock knock",knock),
 EYE_CLIP("Question tilt",question), EYE_CLIP("Peek and open",peek), EYE_CLIP("Check then ask",scanRequest), EYE_CLIP("Please",please)};
constexpr Clip listening[] = {
 EYE_CLIP("Ready to hear",listenReady), EYE_CLIP("Lean in left",listenLeft),
 EYE_CLIP("Lean in right",listenRight), EYE_CLIP("I follow",listenNod),
 EYE_CLIP("Taking it in",listenConsider), EYE_CLIP("Reassuring blink",listenWarm),
 EYE_CLIP("Patient listening",listenSettle)};
#undef EYE_CLIP
template <unsigned N> constexpr int countOf(const Clip (&)[N]) { return N; }
// Exuberant completion clips: reserved for an actual new completion, not for
// the long quiet stretches while unread completions merely remain.
inline bool lively(const Clip& c) { return (c.flags&celebration)!=0; }
constexpr int clipCount(State state) {
 return state==State::done ? countOf(completed) : state==State::attention ? countOf(requesting) :
        state==State::listening ? countOf(listening) : countOf(thinking);
}
static_assert(countOf(thinking)<=16 && countOf(completed)<=16 &&
              countOf(requesting)<=16 && countOf(listening)<=16,"Player bag capacity exceeded");
inline const Clip& clip(State state, int index) {
 return (state==State::done ? completed : state==State::attention ? requesting :
         state==State::listening ? listening : thinking)[index%clipCount(state)];
}
inline uint16_t duration(const Clip& c) { return c.keys[c.count-1].ms; }
// Key pose in screen orientation (directed clips follow the status sides).
inline Pose keyPose(const Clip& c,int index) {
 return (c.flags&authoredDoneLeft)?authored(c.keys[index].pose):c.keys[index].pose;
}
inline float smooth(float t) { t=fmaxf(0,fminf(1,t)); return t*t*t*(t*(t*6-15)+10); }
inline Pose mix(const Pose& a,const Pose& b,float t) {
 Pose result;
#define MIX(f) result.f=a.f+(b.f-a.f)*t
 MIX(x);MIX(y);MIX(width);MIX(height);MIX(upperL);MIX(upperR);MIX(smile);MIX(closeL);MIX(closeR);
 MIX(bodyWidth);MIX(bodyHeight);MIX(lean);MIX(lift);MIX(flow);MIX(tilt);
 MIX(scaleL);MIX(scaleR);MIX(lowerL);MIX(lowerR);MIX(slantL);MIX(slantR);
#undef MIX
 return result;
}
// Finish positional travel 25% earlier, without shortening expressions,
// closures, authored holds or the 90ms eye/body overlap.
inline float travel(float t) { return smooth(t/.75f); }
inline Pose mixTravel(const Pose& a,const Pose& b,float t) {
 Pose result=mix(a,b,smooth(t));
 const float movement=travel(t);
 result.x=a.x+(b.x-a.x)*movement;result.y=a.y+(b.y-a.y)*movement;
 result.lean=a.lean+(b.lean-a.lean)*movement;
 result.lift=a.lift+(b.lift-a.lift)*movement;
 return result;
}
// Cartoon saccade: fast start, ~9% overshoot, exact zero-velocity arrival.
// 1-(1-s)^2(1-2.6s) peaks at s~.59, then settles; no time beyond the key.
constexpr float kSnapShape=2.6f;
inline float snap(float s) {
 s=fmaxf(0,fminf(1,s));
 const float u=1-s;
 return 1-u*u*(1-kSnapShape*s);
}
constexpr uint16_t kSaccadeMs=320;   // Longer gaze moves remain smooth drifts.
constexpr float kSaccadeMinPx=4;
constexpr uint16_t kBlinkSegmentMs=160;
inline bool isSaccade(const Pose& a,const Pose& b,uint32_t length) {
 const float dx=b.x-a.x,dy=b.y-a.y;
 return length<=kSaccadeMs && dx*dx+dy*dy>=kSaccadeMinPx*kSaccadeMinPx;
}
// Per-key easing derived from the authored change itself: short gaze jumps
// snap with overshoot (eye x/y only; body lean/lift keep their smooth travel
// and spring follow-through), and short closing lids accelerate shut.
inline Pose mixSegment(const Pose& a,const Pose& b,float t,uint32_t length) {
 Pose result=mixTravel(a,b,t);
 if(isSaccade(a,b,length)) {
  const float m=snap(t/.75f);
  result.x=a.x+(b.x-a.x)*m;result.y=a.y+(b.y-a.y)*m;
 }
 if(length<=kBlinkSegmentMs) {
  const float s=fmaxf(0,fminf(1,t)),in=s*s;
  if(b.closeL>a.closeL)result.closeL=a.closeL+(b.closeL-a.closeL)*in;
  if(b.closeR>a.closeR)result.closeR=a.closeR+(b.closeR-a.closeR)*in;
 }
 // The quintic ease can exceed 1 by an ulp; partial closures (e.g. .875)
 // would then reopen to -1e-7. Closures are coverage fractions: keep [0,1].
 result.closeL=fmaxf(0,fminf(1,result.closeL));
 result.closeR=fmaxf(0,fminf(1,result.closeR));
 return result;
}
inline Pose sample(const Clip& c,uint32_t ms) {
 for(int i=1;i<c.count;++i) if(ms<c.keys[i].ms) {
  const Key& a=c.keys[i-1]; const Key& b=c.keys[i];
  return mixSegment(keyPose(c,i-1),keyPose(c,i),static_cast<float>(ms-a.ms)/(b.ms-a.ms),b.ms-a.ms);
 }
 return keyPose(c,c.count-1);
}
// Every clip begins from the pose actually on screen: its authored first key is
// replaced by `entry`, and the first segment keeps its full authored timing.
// A state's resting pose (smile, wide request eyes) therefore never drops to a
// neutral key and regrows at each clip boundary or interruption.
inline Pose sampleFrom(const Clip& c,const Pose& entry,uint32_t ms) {
 if(c.count>1&&ms<c.keys[1].ms)
  return mixSegment(entry,keyPose(c,1),static_cast<float>(ms)/c.keys[1].ms,c.keys[1].ms);
 return sample(c,ms);
}
// Eye-visible fields only (body/flow may still move during an eye hold).
inline bool sameEyes(const Pose& a,const Pose& b) {
 return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height&&
        a.upperL==b.upperL&&a.upperR==b.upperR&&a.smile==b.smile&&
        a.closeL==b.closeL&&a.closeR==b.closeR&&a.tilt==b.tilt&&
        a.scaleL==b.scaleL&&a.scaleR==b.scaleR&&a.lowerL==b.lowerL&&a.lowerR==b.lowerR&&
        a.slantL==b.slantL&&a.slantR==b.slantR;
}
// The authored key interval currently driving the eyes. Holds and the pause
// after a clip are "still"; the life layer only adds motion there.
struct Segment {
 uint32_t start=0;
 uint16_t length=0;
 float distance=0;
 bool valid=false,still=false,closure=false,saccade=false;
};
constexpr State thinkingState = State::thinking;
constexpr State attentionState = State::attention;
constexpr Pose neutralPose = p();
struct Player {
 State state=thinkingState;
 int index=-1;
 uint32_t started=0;
 uint16_t used[kStateCount]={};
 uint16_t pause=1500;  // Rest after the current clip; drawn per clip.
 Pose entryEyes=neutralPose;
 Pose entryBody=neutralPose;
 // Resting pauses vary per clip so the acting never settles into a metronome.
 // Requests stay brisk; listening is unhurried.
 static uint16_t pauseFor(State s,uint32_t randomValue) {
  const uint32_t r=(randomValue>>11)%1000;  // Independent of the clip choice bits.
  if(s==attentionState) return static_cast<uint16_t>(500+r*500/1000);
  if(s==State::listening) return static_cast<uint16_t>(1200+r*1400/1000);
  return static_cast<uint16_t>(900+r*1400/1000);
 }
 static constexpr uint16_t kMaxPause=2600,kCalmExtra=1600;
 // Content (not celebrating): completion skips the lively clips and rests
 // longer between performances. Other states ignore it.
 bool calm=false;
 // Between clips: the authored performance finished and the pause is running.
 bool resting(uint32_t now) const { return index>=0 && now-started>=duration(clip(state,index)); }
 // `hold` keeps the current clip's resting pose (e.g. during a counter check)
 // instead of starting the next clip; a state change still interrupts.
 void update(State next,uint32_t now,uint32_t randomValue,bool hold=false) {
  if(index>=0 && next==state && hold) return;
  if(index>=0 && next==state && now-started<duration(clip(state,index))+pause) return;
  entryEyes=eyes(now); entryBody=body(now);
  const int previous=next==state?index:-1;
  state=next; const int s=static_cast<int>(state),count=clipCount(state);
  const uint16_t full=static_cast<uint16_t>((1u<<count)-1);
  uint16_t excluded=0;
  if(calm&&state==State::done)
   for(int i=0;i<count;++i) if(lively(clip(state,i))) excluded|=static_cast<uint16_t>(1u<<i);
  if((used[s]|excluded)==full) used[s]=0;
  // At a bag boundary, avoid immediately repeating its last performance.
  const int skip=used[s]==0&&count>1?previous:-1;
  int remaining=count;
  for(int i=0;i<count;++i) if(((used[s]|excluded)&(1u<<i))||i==skip) --remaining;
  if(remaining<=0) { excluded=0;remaining=count;for(int i=0;i<count;++i) if((used[s]&(1u<<i))||i==skip) --remaining; }
  int choice=randomValue%remaining;
  for(int i=0;i<count;++i)
   if(!((used[s]|excluded)&(1u<<i)) && i!=skip && choice--==0) { index=i; break; }
  used[s]|=1<<index; started=now; pause=pauseFor(state,randomValue);
  if(calm&&state==State::done) pause=static_cast<uint16_t>(pause+kCalmExtra);
 }
 Segment segment(uint32_t now) const {
  Segment result;
  if(index<0) return result;
  const Clip& c=clip(state,index);
  const uint32_t elapsed=now-started;
  result.valid=true;result.start=started;
  if(elapsed>=duration(c)) { result.start=started+duration(c);result.still=true;return result; }
  for(int i=1;i<c.count;++i) if(elapsed<c.keys[i].ms) {
   // The first move leaves from wherever the eyes actually were.
   const Pose a=i==1?entryEyes:keyPose(c,i-1),b=keyPose(c,i);
   result.start=started+c.keys[i-1].ms;
   result.length=static_cast<uint16_t>(c.keys[i].ms-c.keys[i-1].ms);
   result.distance=sqrtf((b.x-a.x)*(b.x-a.x)+(b.y-a.y)*(b.y-a.y));
   result.still=sameEyes(a,b);
   result.saccade=isSaccade(a,b,result.length);
   // An authored blink just before/after already punctuates this move.
   for(int k=i-2;k<=i+1;++k)
    if(k>=1&&k<c.count&&(c.keys[k].pose.closeL>0||c.keys[k].pose.closeR>0))result.closure=true;
   break;
  }
  return result;
 }
 Pose eyes(uint32_t now) const {
  if(index<0) return entryEyes;
  return sampleFrom(clip(state,index),entryEyes,now-started);
 }
 // Eyes lead the body, then the body follows through. Delay belongs to the
 // sequence clock, so a fast request interrupts the previous state instantly.
 Pose body(uint32_t now) const {
  if(index<0) return entryBody;
  const uint32_t elapsed=now-started;
  const uint32_t delayed=elapsed>90?elapsed-90:0;
  return sampleFrom(clip(state,index),entryBody,delayed);
 }
};
} // namespace eye_acting
