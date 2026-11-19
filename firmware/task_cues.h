#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "avatar_scene.h"
#include "count_raster.h"
#include "roman_counts.h"

// Two staged, noticeable task-count cues shared by firmware, native previews
// and the header widget. Aggregate counts only: real task state updates at
// once; these only stage how numerals, colors and acting reveal a snapshot.
// No task mutation, acknowledgement, sound or synthetic live events.
//
// Arrival (active count rises): the character looks up at a numeral teetering
// at the top edge, gets scared, jerks aside and the numeral drops into place.
// Roman numerals are not positional digits, so the drop depends on the strings:
// when the new label EXTENDS the old one in the same font (I->II, V->VI, X->XI),
// the old letters slide over and only the new letters fall into the gap. When
// it rewrites the label (III->IV, VIII->IX, a font change), the whole new
// numeral falls and knocks the old one down off the screen.
//
// Transfer (active->done with no old done label, e.g. active I -> done I): the
// blue backdrop charges with green light, a cartoon starburst flashes the screen
// green, the new done numeral flips across to the green side, then the face
// creeps back in from the right and hops home to look at it.
namespace task_cues {
using eye_acting::Pose;
using eye_acting::p;
constexpr uint16_t rgb(int r,int g,int b) {
 return static_cast<uint16_t>(((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3));
}
constexpr uint16_t kEnergy=rgb(0xA6,0xFF,0x5C);  // charged lime light
constexpr uint16_t kFlash=rgb(0xF4,0xFF,0xE4);   // cartoon flash / motes
inline float unit(float v) { return v<0?0:(v>1?1:v); }
inline float ramp(float t,float a,float b) { return eye_acting::smooth((t-a)/(b-a)); }
inline float hop(float u) { u=unit(u);return 4*u*(1-u); }
inline uint16_t blend(uint16_t a,uint16_t b,float t) {
 if(t<=0)return a;
 if(t>=1)return b;
 const int r=static_cast<int>(((a>>11)&31)+((((b>>11)&31))-((a>>11)&31))*t+.5f);
 const int g=static_cast<int>(((a>>5)&63)+((((b>>5)&63))-((a>>5)&63))*t+.5f);
 const int l=static_cast<int>((a&31)+((b&31)-(a&31))*t+.5f);
 return static_cast<uint16_t>((r<<11)|(g<<5)|l);
}
inline uint32_t mix32(uint32_t v) {
 v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;return v;
}
// A small light mote: a plus with soft arms (no particle system/allocation).
template<class Pixel> void mote(float x,float y,float alpha,int arms,Pixel pixel) {
 if(alpha<=0)return;
 const int cx=static_cast<int>(lroundf(x)),cy=static_cast<int>(lroundf(y));
 pixel(cx,cy,alpha);
 for(int arm=1;arm<=arms;++arm) {
  const float a=alpha*(arm==1?.75f:.35f);
  pixel(cx-arm,cy,a);pixel(cx+arm,cy,a);pixel(cx,cy-arm,a);pixel(cx,cy+arm,a);
 }
}
// Extra lids/slant without non-constexpr helpers in constexpr key tables.
constexpr Pose lids(Pose b,float lower,float slant) {
 return {b.x,b.y,b.width,b.height,b.upperL,b.upperR,b.smile,b.closeL,b.closeR,
         b.bodyWidth,b.bodyHeight,b.lean,b.lift,b.flow,b.tilt,
         b.scaleL,b.scaleR,lower,lower,slant,slant};
}

// ---------------------------------------------------------------- arrival
// Eye keys look up-right (the active side), shiver, jerk aside before the
// fall, wince on impact, then peek back at the newcomer with relief.
constexpr eye_acting::Key arrivalKeys[]={
 {0,p()},
 {170,p(12,-17,.96f,1.16f,0,0,0,0,0,.98f,1.03f,2,-3,.8f)},
 {330,lids(p(15,-20,.88f,1.30f,0,0,0,0,0,.95f,1.05f,-1,-1,.7f),.16f,-.18f)},
 {800,lids(p(15,-21,.86f,1.32f,0,0,0,0,0,.93f,1.06f,-3,1,.7f),.18f,-.20f)},
 {900,lids(p(-13,-8,1.04f,1.04f,0,0,0,.40f,.40f,1.07f,.90f,-10,3,1.3f),.10f,-.10f)},
 {1150,lids(p(-10,-9,1.02f,1.08f,0,0,0,.28f,.28f,1.05f,.92f,-9,3,1.0f),.10f,-.12f)},
 {1230,p(-9,-4,1.06f,.96f,0,0,0,.85f,.85f,1.08f,.88f,-8,4,1.2f)},
 {1440,p(9,3,1.04f,1.08f,.04f,0,0,0,0,1.02f,1,-3,0,.8f)},
 {1900,p(12,4,1.02f,1.04f,.10f,.06f,.15f,0,0,1,1,-1,0,.8f)},
 {2400,p()}};
constexpr eye_acting::Clip arrivalClip=eye_acting::makeClip("New task drops in",arrivalKeys);
struct Arrival {
 static constexpr uint32_t kHang=280,kFall=860,kImpact=1180,kEnd=2400,kBodyLag=90;
 uint32_t started=0;
 bool enabled=false,initialized=false,impactPending=false;
 int previous=0,from=0,to=0;
 uint32_t phase(uint32_t now) const { return now-started; }
 bool busy(uint32_t now) const { return enabled&&phase(now)<kEnd; }
 bool landed(uint32_t now) const { return !busy(now)||phase(now)>=kImpact; }
 // Recovery/startup is a baseline. A rise starts a drop; any other change, or
 // a rise while a numeral is still in the air, reveals the current count.
 bool observe(uint32_t now,int active,bool healthy,bool ready) {
  if(!healthy) { initialized=false;enabled=false;previous=0;return false; }
  bool start=false;
  if(active!=previous) {
   const bool midair=busy(now)&&phase(now)<kImpact;
   enabled=false;
   if(initialized&&active>previous&&ready&&!midair) {
    from=previous;to=active;started=now;enabled=true;impactPending=true;start=true;
   }
  }
  previous=active;initialized=true;
  return start;
 }
 void update(uint32_t now,bool allowed) { if(!allowed||phase(now)>=kEnd)enabled=false; }
 // Exactly one landing thud per drop for the soft rim.
 bool consumeImpact(uint32_t now) {
  if(!busy(now)||!impactPending||phase(now)<kImpact)return false;
  impactPending=false;return true;
 }
 // Jerk aside (away from the active side) just as the numeral lets go.
 // distance comes from room(): the authored 20px, or more when the face
 // would otherwise stand in the falling numeral's way.
 float dodge(uint32_t now,float distance=kDodge) const {
  if(!busy(now))return 0;
  const float t=static_cast<float>(phase(now));
  const float u=unit((t-(kFall-60))/130.0f);
  const float jerk=1-(1-u)*(1-u)*(1-u);
  // Room beyond the authored jerk: back away from the corner while it
  // teeters (so it reads on its own backdrop, not over the face), then the
  // authored jerk follows as it lets go. With no extra room this is exactly
  // the original 20px jerk.
  const float extra=fmaxf(0,distance-kDodge);
  const float recoil=extra*fmaxf(ramp(t,40,kHang+120),jerk);
  return -(kDodge*jerk+recoil)*(1-ramp(t,kImpact+260,kEnd-260));
 }
 static constexpr float kDodge=20,kClear=16,kMaxDodge=88;
 // Left edge of what falls: the appended letters or the whole rewritten label.
 float fallLeft(const count_raster::Font& fromFont,const count_raster::Font& toFont,float restX) const {
  char before[10],after[10];
  roman_counts::format(from,before);roman_counts::format(to,after);
  const size_t kept=strlen(before);
  const bool append=strncmp(after,before,kept)==0&&after[kept]&&(kept==0||&fromFont==&toFont);
  const float start=restX-count_raster::width(toFont,after)/2;
  return append?start+count_raster::width(fromFont,before):start;
 }
 // How far the resting body reaches right of `left` in the rows the numeral
 // falls through (above its resting top). The wide paired face covers the
 // whole column, so a fixed 20px dodge left it falling through the face.
 // Ignores the dodge itself, flow and membranes (kClear covers them).
 static float intrusion(const avatar_scene::Scene& scene,float left,float restTop) {
  // Layout only: the neutral pose keeps the room steady while the acting
  // squashes and stretches (kClear covers those few pixels).
  avatar_scene::Scene s=scene;s.away=0;s.surface=nullptr;s.body=avatar_scene::neutralPose;
  const auto g=avatar_scene::geometry(s);
  const float stretch=avatar_scene::unusedSideStretch(s);
  float worst=-1000;
  for(int y=0;y<restTop&&y<135;y+=3) {
   if(y+.5f<avatar_scene::kAlertBandHeight*s.alert)continue;
   float ny=(y+.5f-g.cy)/g.ry;
   if(ny>.85f)ny=avatar_scene::mix(ny,.85f,s.corners);
   if(fabsf(ny)>=1)continue;
   float right=g.cx+g.rx*avatar_scene::profile(fabsf(ny),g.power);
   if(stretch>0)right+=(right-g.cx)*stretch;
   worst=fmaxf(worst,right-left);
  }
  return worst;
 }
 // Dodge distance that clears the fall column, never less than authored.
 static float room(const avatar_scene::Scene& scene,float left,float restTop) {
  return fminf(kMaxDodge,fmaxf(kDodge,intrusion(scene,left,restTop)+kClear));
 }
 // The body can clear the column further than the eyes may follow: give back
 // up to the extra (beyond authored) dodge so the outer eye stays on screen.
 // eyeLeft is the outer eye's left edge before the dodge. Continuous.
 static constexpr float kEyeMargin=3;
 static float eyeReturn(float dodge,float eyeLeft) {
  const float extra=fmaxf(0,-dodge-kDodge);
  return fminf(extra,fmaxf(0,kEyeMargin-(eyeLeft+dodge)));
 }
 Pose apply(const Pose& base,uint32_t now,bool body=false) const {
  const uint32_t elapsed=now-started;
  const uint32_t t=body?(elapsed>kBodyLag?elapsed-kBodyLag:0):elapsed;
  if(!enabled||t>=kEnd)return base;
  const float weight=eye_acting::smooth(t/110.0f)*(1-ramp(static_cast<float>(t),kEnd-450,kEnd));
  auto look=eye_acting::sample(arrivalClip,t);
  // A short frightened shiver while the numeral teeters; event-only, not idle.
  if(t>=330&&t<860) {
   const float shiver=sinf(t*.115f)*(t<760?1:1-(t-760)/100.0f);
   look.x+=.9f*shiver;look.lean+=.5f*shiver;
  }
  auto result=eye_acting::mix(base,look,weight);
  if(body)result.closeL=result.closeR=0;
  return result;
 }
 // Draw callbacks: text(font,label,cx,cy,flying) renders an outlined numeral
 // (flying selects a second cache); light(x,y,alpha) draws pale motes/streaks.
 template<class Text,class Light>
 void draw(uint32_t now,const count_raster::Font& fromFont,const count_raster::Font& toFont,
           float restX,float restY,Text text,Light light) const {
  char before[10],after[10];
  roman_counts::format(from,before);roman_counts::format(to,after);
  const float t=static_cast<float>(phase(now));
  const size_t kept=strlen(before);
  const bool append=strncmp(after,before,kept)==0&&after[kept]&&(kept==0||&fromFont==&toFont);
  const char* piece=append?after+kept:after;
  const float total=count_raster::width(toFont,after);
  const float keptWidth=count_raster::width(fromFont,before);
  const float pieceWidth=count_raster::width(toFont,piece);
  const float start=restX-total/2;
  // Impact jolts the letters it lands next to.
  const float jolt=t>=kImpact?2.5f*hop((t-kImpact)/150.0f):0;
  float pieceX=restX;
  if(append) {
   if(kept) {
    // Old letters make room for the incoming ones while the piece teeters.
    const float slide=ramp(t,180,640);
    text(fromFont,before,avatar_scene::mix(restX,start+keptWidth/2,slide),restY+jolt,false);
   }
   pieceX=start+keptWidth+pieceWidth/2;
  } else if(kept) {
   if(t<kImpact) text(fromFont,before,restX,restY,false);
   else {
    // Knocked down and out toward the screen edge, with gravity.
    const float s=(t-kImpact)/1000.0f;
    const float y=restY+60*s+1100*s*s;
    if(y-fromFont.capHeight/2.0f<136)text(fromFont,before,restX+38*s,y,false);
   }
  }
  const float cap=toFont.capHeight;
  // Teeter just inside the top edge so the whole incoming numeral reads.
  const float hang=cap*.5f+2,above=-cap*.6f;
  float y;
  if(t<kHang) {
   // Slide into view from above with a small catch, like a snagged weight.
   const float u=unit(t/kHang);
   y=avatar_scene::mix(above,hang,1-(1-u)*(1-u))+2.5f*hop((u-.55f)/.45f);
  } else if(t<kFall) {
   const float wobble=ramp(t,kHang,kHang+160);
   pieceX+=1.7f*wobble*sinf((t-kHang)*.037f);
   y=hang+.7f*wobble*sinf((t-kHang)*.061f);
  } else if(t<kImpact) {
   const float s=(t-kFall)/(kImpact-kFall);
   y=hang+(restY-hang)*s*s;
   // Cartoon speed lines trail the falling letters.
   const float length=6+16*s;
   const float tail=y-cap/2-3;
   for(int line=-1;line<=1;++line) {
    const float lx=pieceX+line*fmaxf(4,pieceWidth*.32f);
    const float offset=line==0?0:4;
    for(int k=0;k<static_cast<int>(length);++k)
     light(static_cast<int>(lx),static_cast<int>(tail-offset-k),.85f*(1-k/length));
   }
  } else {
   y=restY+jolt*.4f-7*hop((t-kImpact)/230.0f)-2*hop((t-kImpact-230)/140.0f);
   // Dust puffs kick out sideways from the landing.
   const float u=(t-kImpact)/320.0f;
   if(u<1)for(int i=0;i<6;++i) {
    const float side=i<3?-1:1,spread=.6f+.25f*(i%3);
    const float px=pieceX+side*(pieceWidth/2+2+20*spread*u);
    const float py=restY+cap/2-2-12*spread*u+16*u*u;
    mote(px,py,.9f*(1-u),1,light);
   }
  }
  text(toFont,piece,pieceX,y,true);
 }
};

// ------------------------------------------------------ completion pairing
// Herdr can report one completion over two polls: the active count falls in
// one snapshot and the done count rises in the next (or the reverse). With
// no done label yet, hold the first half for up to kWindow so both edges
// reach the cues together and the transfer plays instead of a plain reflow.
// A presentation delay only: never invents, drops or reorders counts. Any
// other change, an unready moment or the window expiring releases it.
struct CompletionPairing {
 static constexpr uint32_t kWindow=4600;  // two 2s polls plus write jitter
 bool pending=false;
 uint32_t since=0;
 int done=0,active=0;  // the held snapshot
 static bool half(int shownDone,int shownActive,int done,int active) {
  return shownDone==0&&shownActive>0&&((done==0&&active<shownActive)||(done>0&&active==shownActive));
 }
 // shown*: currently presented counts. done/active: incoming snapshot; while
 // holding they are replaced by the presented counts and true is returned.
 bool filter(uint32_t now,int shownDone,int shownActive,int& nextDone,int& nextActive,bool ready) {
  if(ready&&half(shownDone,shownActive,nextDone,nextActive)) {
   if(!pending) { pending=true;since=now; }
   if(now-since<kWindow) {
    done=nextDone;active=nextActive;nextDone=shownDone;nextActive=shownActive;
    return true;
   }
  }
  pending=false;
  return false;
 }
 // Release the held snapshot (apply done/active) when it expires or a
 // request, recording or button press makes staging inappropriate.
 bool due(uint32_t now,bool ready) const { return pending&&(!ready||now-since>=kWindow); }
 void reset() { pending=false; }
};

// --------------------------------------------------------------- transfer
constexpr eye_acting::Key chargeKeys[]={
 {0,p()},
 {200,p(12,2,1.04f,1.10f,0,0,0,0,0,1,1,2,-1,.8f)},
 {700,lids(p(13,3,.98f,1.16f,0,0,0,0,0,.97f,1.03f,-2,0,.9f),.10f,-.10f)},
 {960,lids(p(13,2,.95f,1.24f,0,0,0,0,0,.95f,1.05f,-5,1,1.1f),.16f,-.18f)},
 {1080,p(8,0,1.06f,.90f,.3f,.3f,0,.95f,.95f,1.08f,.90f,-6,3,1.3f)},
 {1300,p(8,0,1.06f,.90f,.3f,.3f,0,.95f,.95f,1.08f,.90f,-6,3,1.3f)}};
constexpr eye_acting::Clip chargeClip=eye_acting::makeClip("Charged completion",chargeKeys);
struct Transfer {
 // Charge, starburst, full-cover swap, flip, careful entry, happy hop home.
 static constexpr uint32_t kBurst=1050,kCover=1190,kReveal=1300,kFlip=1330,kLand=2180;
 static constexpr uint32_t kEnter=2300,kPeek=2950,kHop=3300,kHome=3700,kEnd=4150,kBodyLag=90;
 // Hidden far enough that even a 3x unused-side stretch stays offscreen;
 // re-entry starts just outside the settled final layout.
 static constexpr float kHidden=700,kOffscreen=330;
 uint32_t started=0;
 bool enabled=false,initialized=false,homePending=false;
 int previousDone=0,previousActive=0,oldActive=0,newDone=0,newActive=0;
 float sourceX=216,sourceY=67.5f;
 // Smoothed visuals so cancellation eases back instead of teleporting.
 float awayValue=0,washValue=0;
 uint16_t washTint=kFlash;
 uint32_t phase(uint32_t now) const { return now-started; }
 bool busy(uint32_t now) const { return enabled&&phase(now)<kEnd; }
 // Only completions without an existing done label: paired sorting keeps the
 // case where both old counters are readable. Requires both directional edges.
 bool observe(uint32_t now,int done,int active,bool healthy,bool ready) {
  if(!healthy) { initialized=false;enabled=false;previousDone=previousActive=0;return false; }
  const bool changed=done!=previousDone||active!=previousActive;
  const bool completion=initialized&&changed&&done>previousDone&&active<previousActive&&
                        previousDone==0&&previousActive>0;
  bool start=false;
  if(changed)enabled=false; // supersede: reveal current data, never replay
  if(completion&&ready) {
   oldActive=previousActive;newDone=done;newActive=active;started=now;
   enabled=true;homePending=true;start=true;
  }
  previousDone=done;previousActive=active;initialized=true;
  return start;
 }
 void update(uint32_t now,bool allowed) { if(!allowed||phase(now)>=kEnd)enabled=false; }
 // Old counts until the flash covers the screen; the swap is never visible.
 int displayCount(uint32_t now,bool done,int actual) const {
  return busy(now)&&phase(now)<kCover?(done?0:oldActive):actual;
 }
 // Where the charged numeral rests, sampled until the flash.
 void source(uint32_t now,float x,float y) {
  if(busy(now)&&phase(now)<kCover) { sourceX=x;sourceY=y; }
 }
 bool covering(uint32_t now) const { return busy(now)&&phase(now)>=kBurst; }
 // Accelerating green pulses through the active backdrop, saturating at the burst.
 float glow(uint32_t now) const {
  if(!busy(now))return 0;
  const float t=static_cast<float>(phase(now));
  if(t>=kCover)return 0;
  if(t>=kBurst)return 1;
  const float s=t/kBurst;
  const float pulse=.5f-.5f*cosf(6.2831853f*(1.6f*s+4.4f*s*s));
  const float charge=s*sqrtf(s);
  return avatar_scene::mix(charge*(.35f+.65f*pulse),1,ramp(t,kBurst-170,kBurst));
 }
 // Active-numeral shake while charging (sub-pixel; draw with spring shift).
 float shakeX(uint32_t now) const {
  if(!busy(now)||phase(now)>=kCover)return 0;
  const float t=static_cast<float>(phase(now));
  return 1.8f*ramp(t,250,kBurst)*sinf(t*.19f);
 }
 float shakeY(uint32_t now) const {
  if(!busy(now)||phase(now)>=kCover)return 0;
  const float t=static_cast<float>(phase(now));
  return .9f*ramp(t,400,kBurst)*sinf(t*.27f+1);
 }
 // Resting-label alpha: hidden through the flash; done waits for the flip.
 float labelAlpha(uint32_t now,bool done) const {
  if(!busy(now))return 1;
  const float t=static_cast<float>(phase(now));
  if(t<kCover)return 1;
  if(done)return t<kLand+240?0:1;
  return ramp(t,kReveal+150,kReveal+420);
 }
 float awayTarget(uint32_t now,float restCenter) const {
  if(!busy(now))return 0;
  const float t=static_cast<float>(phase(now));
  if(t<kCover)return 0;
  // Peek so the near eye shows at the right edge, whatever the layout.
  const float peek=fmaxf(40,226-restCenter);
  if(t<kEnter)return kHidden;
  if(t<kPeek) {
   const float u=unit((t-kEnter)/(kPeek-kEnter));
   return avatar_scene::mix(kOffscreen,peek,1-(1-u)*(1-u)*(1-u));
  }
  if(t<kHop+60)return peek-4*ramp(t,kPeek,kHop+60);
  return (peek-4)*(1-ramp(t,kHop+60,kHome));
 }
 // Full-screen green wash after the flash. With no remaining active numeral
 // it stays until the face is home (its body then covers the unused side).
 float washTarget(uint32_t now) const {
  if(!busy(now))return 0;
  const float t=static_cast<float>(phase(now));
  if(t<kCover)return 0;
  const float fade=newActive>0?kLand+80:kHome+120;
  return 1-ramp(t,fade,fade+420);
 }
 uint16_t washColor(uint32_t now,uint16_t green) const {
  const float t=static_cast<float>(phase(now));
  return blend(kFlash,green,ramp(t,kReveal,kReveal+380));
 }
 void animate(uint32_t now,float seconds,float restCenter,uint16_t green) {
  if(busy(now)) {
   awayValue=awayTarget(now,restCenter);washValue=washTarget(now);washTint=washColor(now,green);
  } else {
   awayValue=avatar_scene::approach(awayValue,0,seconds,.14f);
   washValue=avatar_scene::approach(washValue,0,seconds,.12f);
  }
 }
 bool consumeHome(uint32_t now) {
  if(!busy(now)||!homePending||phase(now)<kHome)return false;
  homePending=false;return true;
 }
 Pose apply(const Pose& base,uint32_t now,bool body=false,float lookX=-14,float lookY=4) const {
  const uint32_t elapsed=now-started;
  const uint32_t t=body?(elapsed>kBodyLag?elapsed-kBodyLag:0):elapsed;
  if(!enabled||t>=kEnd)return base;
  const float weight=eye_acting::smooth(t/180.0f)*(1-ramp(static_cast<float>(t),kEnd-380,kEnd));
  Pose look;
  if(t<kCover) {
   look=eye_acting::sample(chargeClip,t);
   if(t>=500&&t<kBurst)look.x+=.7f*ramp(t,500,kBurst)*sinf(t*.13f); // nervous shiver
  } else {
   const float lx=fminf(-8,lookX),ly=lookY;
   // Careful: narrowed, then spotting the numeral, crouch, jump, land, beam.
   const Pose wary=p(lx-3,ly-2,.94f,.92f,.22f,.18f,0,0,0,.98f,1.02f,-3,0,.6f);
   const Pose spot=p(lx-1,ly,1.06f,1.16f,0,0,.35f,0,0,1,1.04f,-2,-1,.9f);
   // The tall body mostly extends offscreen, so the eyes carry the jump.
   const Pose crouch=p(lx,ly+9,1.12f,.84f,0,0,.55f,0,0,1.08f,.90f,-2,5,1.0f);
   const Pose air=p(lx-2,ly-17,.92f,1.22f,0,0,.75f,0,0,.94f,1.10f,-6,-14,1.3f);
   const Pose land=p(lx,ly+7,1.14f,.86f,0,0,.8f,0,0,1.10f,.88f,-4,5,1.2f);
   const Pose beam=p(lx,ly+2,1.06f,1.06f,0,0,.85f,.9f,.9f,1.02f,1.0f,-3,0,1.0f);
   const Pose glad=p(lx+1,ly+2,1.05f,1.08f,0,0,.75f,0,0,1,1.02f,-3,-1,.9f);
   const eye_acting::Key keys[]={
    {kCover,wary},{kPeek-120,eye_acting::withGaze(wary,lx-4,ly-2)},{kPeek+80,spot},
    {kHop,crouch},{kHop+210,air},{kHome,land},{kHome+150,beam},{kHome+320,glad},
    {kEnd,glad}};
   look=eye_acting::sample(eye_acting::makeClip("Happy return",keys),t);
  }
  auto result=eye_acting::mix(base,look,weight);
  if(body)result.closeL=result.closeR=0;
  return result;
 }
 // Cartoon starburst growing from the charged numeral. span(x,y,n) receives
 // fully covered runs; pixel(x,y,alpha) the spiky antialiased fringe.
 template<class Span,class Pixel>
 void burst(uint32_t now,Span span,Pixel pixel) const {
  if(!busy(now)||phase(now)<kBurst||phase(now)>=kCover)return;
  const float u=(phase(now)-kBurst+33.0f)/(kCover-kBurst);
  const float outer=430*u*u+10,inner=outer*.62f;
  const float spin=phase(now)*.0025f,c=cosf(spin),s=sinf(spin);
  for(int y=0;y<135;++y) {
   const float dy=y+.5f-sourceY;
   if(fabsf(dy)>=outer)continue;
   const float reach=sqrtf(outer*outer-dy*dy);
   const int x0=static_cast<int>(fmaxf(0,floorf(sourceX-reach)));
   const int x1=static_cast<int>(fminf(239,ceilf(sourceX+reach)));
   int solid0=x1+1,solid1=x1;
   if(fabsf(dy)<inner-1) {
    const float half=sqrtf((inner-1)*(inner-1)-dy*dy);
    solid0=static_cast<int>(fmaxf(x0,ceilf(sourceX-half)));
    solid1=static_cast<int>(fminf(x1,floorf(sourceX+half)-1));
    if(solid1>=solid0)span(solid0,y,solid1-solid0+1);
    else { solid0=x1+1;solid1=x1; }
   }
   for(int x=x0;x<=x1;++x) {
    if(x>=solid0&&x<=solid1) { x=solid1;continue; }
    const float dx=x+.5f-sourceX,d=sqrtf(dx*dx+dy*dy);
    if(d<=0) { pixel(x,y,1);continue; }
    // Rotated direction, then cos(5θ) by Chebyshev: ten sharp rays, no atan2.
    const float cx=(dx*c-dy*s)/d,c2=cx*cx;
    const float ray=fabsf(cx*(16*c2*c2-20*c2+5));
    const float radius=inner+(outer-inner)*ray*ray*ray;
    const float alpha=unit(radius-d+.5f);
    if(alpha>0)pixel(x,y,alpha);
   }
  }
 }
 // Crackling energy motes around the charging numeral.
 template<class Pixel> void crackles(uint32_t now,Pixel pixel) const {
  if(!busy(now)||phase(now)<240||phase(now)>=kBurst)return;
  const float t=static_cast<float>(phase(now));
  const float power=ramp(t,240,kBurst);
  const uint32_t slot=phase(now)/66;
  const int count=2+static_cast<int>(6*power);
  for(int i=0;i<count;++i) {
   const uint32_t h=mix32(slot*977u+i*7919u+started);
   const float angle=(h&1023)*(6.2831853f/1024);
   const float radius=13+((h>>10)&31)*.7f*(1-.3f*power);
   mote(sourceX+cosf(angle)*radius*1.2f,sourceY+sinf(angle)*radius*.8f,
        .55f+.4f*power,(h>>16)&1?2:1,pixel);
  }
 }
 // The new done numeral flips across: text(font,label,cx,cy,scaleX,alpha).
 // light(x,y,alpha) adds a short trail and a landing ring of motes.
 template<class Text,class Light>
 void flip(uint32_t now,const count_raster::Font& font,float targetX,float targetY,
           Text text,Light light) const {
  if(!busy(now)||phase(now)<kReveal||phase(now)>=kLand+240)return;
  char label[10];roman_counts::format(newDone,label);
  const float t=static_cast<float>(phase(now));
  const float cap=font.capHeight;
  const auto at=[&](float u,float& x,float& y) {
   const float e=eye_acting::smooth(u);
   x=avatar_scene::mix(sourceX,targetX,e);
   const float mid=avatar_scene::mix(sourceY,targetY,.5f);
   const float arc=fminf(46,mid-cap/2-6);
   y=avatar_scene::mix(sourceY,targetY,e)-arc*sinf(3.14159265f*e);
  };
  float x,y,scale=1;
  if(t<kLand) {
   const float u=unit((t-kFlip)/(kLand-kFlip));
   at(u,x,y);
   // One full card turn: edge-on twice, mirrored back face in between.
   scale=cosf(6.2831853f*eye_acting::smooth(u));
   for(int k=1;k<=4;++k) {
    float tx,ty;at(unit(u-.045f*k),tx,ty);
    if(u-.045f*k>0)mote(tx,ty,.8f-.17f*k,k<3?2:1,light);
   }
  } else {
   x=targetX;y=targetY-6*hop((t-kLand)/240.0f);
   const float r=(t-kLand)/240.0f;
   for(int i=0;i<8;++i) {
    const float a=i*.785398f+.39f;
    mote(x+cosf(a)*(10+22*r),y+sinf(a)*(8+14*r),.9f*(1-r),1,light);
   }
  }
  text(font,label,x,y,scale,ramp(t,kReveal,kReveal+120));
 }
};

// -------------------------------------------------------------- step over
// Done-only -> nothing (the last unread completion was opened): instead of
// fading the green strip to a muddy dark, the character eyes the numeral,
// winds up, and steps over it. A rounded black foot sweeps out of the body,
// covering the numeral and the green, and plants past the screen edge; only
// then does the (now hidden) backdrop leave and the body follow it home.
constexpr eye_acting::Key stepKeys[]={
 {0,p()},
 {150,p(-20,5,1.02f,1.08f,0,0,.25f,0,0,.97f,1.03f,4,1,.9f)},
 {240,p(-22,7,1.06f,.94f,0,0,.30f,0,0,1.03f,.95f,6,3,.9f)},
 {470,p(-20,-6,1.0f,1.08f,0,0,.40f,0,0,1.0f,1.03f,-7,-3,1.2f)},
 {700,p(-12,6,1.12f,.86f,0,0,.60f,0,0,1.08f,.90f,-6,4,1.3f)},
 {870,p(-4,-5,.96f,1.12f,0,0,.70f,0,0,.96f,1.06f,-3,-4,1.1f)},
 {1020,p(-2,4,1.08f,.92f,0,0,.85f,.85f,.85f,1.05f,.94f,-1,3,1.1f)},
 {1200,p(0,1,1.04f,1.05f,0,0,.65f,0,0,1,1.01f,0,0,.9f)},
 {1500,p()}};
constexpr eye_acting::Clip stepClip=eye_acting::makeClip("Step over",stepKeys);
struct StepOver {
 static constexpr uint32_t kWind=220,kPlant=700,kSettle=1020,kEnd=1500,kBodyLag=90;
 uint32_t started=0;
 bool enabled=false,initialized=false,plantPending=false,settlePending=false;
 int previousDone=0,previousActive=0,oldDone=0;
 float side=-1;     // done strip is on the left
 float reachValue=0;  // eased foot progress; cancellation retracts it
 uint32_t phase(uint32_t now) const { return now-started; }
 bool busy(uint32_t now) const { return enabled&&phase(now)<kEnd; }
 // Only done-only -> zero. Recovery/startup is a baseline; any later change
 // supersedes the step and reveals the current data at once.
 bool observe(uint32_t now,int done,int active,bool healthy,bool ready) {
  if(!healthy) { initialized=false;enabled=false;previousDone=previousActive=0;return false; }
  const bool changed=done!=previousDone||active!=previousActive;
  bool start=false;
  if(changed)enabled=false;
  if(initialized&&changed&&previousDone>0&&previousActive==0&&done==0&&active==0&&ready) {
   oldDone=previousDone;started=now;enabled=true;plantPending=settlePending=true;start=true;
  }
  previousDone=done;previousActive=active;initialized=true;
  return start;
 }
 void update(uint32_t now,bool allowed) { if(!allowed||phase(now)>=kEnd)enabled=false; }
 // The old numeral (and so its strip/layout) stays until the foot has planted.
 int displayCount(uint32_t now,int actual) const {
  return busy(now)&&phase(now)<kPlant?oldDone:actual;
 }
 float reachTarget(uint32_t now) const {
  if(!busy(now))return 0;
  const float t=static_cast<float>(phase(now));
  if(t<kWind)return 0;
  const float u=unit((t-kWind)/(kPlant-kWind));
  // Lift-off eases in; the plant lands with speed rather than settling in.
  return u*u*(2-u);
 }
 void animate(uint32_t now,float seconds) {
  reachValue=busy(now)?reachTarget(now):avatar_scene::approach(reachValue,0,seconds,.12f);
 }
 // Two soft thuds: the foot plants, then the body settles over it.
 // Returns kick strength (0 when none); direction is the step side.
 float consumeImpact(uint32_t now) {
  if(!busy(now))return 0;
  if(plantPending&&phase(now)>=kPlant) { plantPending=false;return .85f; }
  if(settlePending&&phase(now)>=kSettle) { settlePending=false;return .45f; }
  return 0;
 }
 Pose apply(const Pose& base,uint32_t now,bool body=false) const {
  const uint32_t elapsed=now-started;
  const uint32_t t=body?(elapsed>kBodyLag?elapsed-kBodyLag:0):elapsed;
  if(!enabled||t>=kEnd)return base;
  const float weight=eye_acting::smooth(t/120.0f)*(1-ramp(static_cast<float>(t),kEnd-400,kEnd));
  auto result=eye_acting::mix(base,eye_acting::sample(stepClip,t),weight);
  if(body)result.closeL=result.closeR=0;
  return result;
 }
};
} // namespace task_cues
