#pragma once
#include "avatar_scene.h"
#include "roman_counts.h"

// Layout only: no task events, bitmap scaling, allocations or per-pixel work.
namespace counter_space {
constexpr float kMaxShift=10,kLongScale=.94f;
inline int length(int count) {
 char text[10];roman_counts::format(count,text);
 int n=0;while(text[n])++n;return n;
}
inline float pressure(int count,float width) {
 return count>0?face_physics::clamp((width-32)/24,0,1):0;
}
struct Layout { float x=0,scale=1; };
inline Layout target(int done,int active,float doneWidth,float activeWidth,float corners,bool enabled) {
 Layout result;
 if(!enabled)return result;
 // Only side labels need accommodation. Ease it away throughout reflow;
 // bottom labels leave the avatar full-size and centred, even when unequal.
 const float side=1-corners;
 result.x=kMaxShift*(pressure(done,doneWidth)-pressure(active,activeWidth))*side;
 if(length(done)>=5||length(active)>=5)result.scale=avatar_scene::mix(1,kLongScale,side);
 return result;
}
struct State {
 Layout value;
 void update(avatar_scene::Scene& scene,int done,int active,float doneWidth,float activeWidth,
             float seconds,bool enabled) {
  const Layout goal=target(done,active,doneWidth,activeWidth,scene.corners,enabled);
  value.x=avatar_scene::approach(value.x,goal.x,seconds,avatar_scene::kShiftSeconds);
  // Give the shift a head start, then ease in at most six percent shrink.
  // No delay when no lateral move is needed. Restore full size
  // immediately as an easing target when labels shorten or priority changes.
  const float progress=1-face_physics::clamp(fabsf(goal.x-value.x)/fmaxf(1,fabsf(goal.x)),0,1);
  const float ready=eye_acting::smooth(face_physics::clamp((progress-.65f)/.35f,0,1));
  const float scale=avatar_scene::mix(1,goal.scale,ready);
  value.scale=avatar_scene::approach(value.scale,scale,seconds,avatar_scene::kReflowSeconds);
  scene.counterShift=value.x;scene.counterScale=value.scale;
 }
};
} // namespace counter_space
