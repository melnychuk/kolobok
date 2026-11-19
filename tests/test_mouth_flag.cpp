#include <assert.h>
#include <initializer_list>
#include "../firmware/avatar_scene.h"

int main() {
 // Exercise the default-off build and the explicitly enabled build from the
 // same fixture. The flag must not alter eyes, face geometry or count acting.
 for(int state=0;state<4;++state)for(int clip=0;clip<eye_acting::clipCount(static_cast<eye_acting::State>(state));++clip)
 for(float corners: {0.0f,.1f,.35f,1.0f}) {
  eye_acting::Player player;player.state=static_cast<eye_acting::State>(state);player.index=clip;
  for(uint32_t ms=0;ms<4000;ms+=137) {
   avatar_scene::Scene scene;scene.corners=corners;scene.eyeY=67.5f;
   scene.body=player.body(ms);scene.eyes=player.eyes(ms);scene.thinking=state==0;
   avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);
   int pixels=0;
   avatar_scene::mouth(scene,mask,[&](int,int,float){++pixels;});
#if STICK_S3_ENABLE_MOUTH
   if(corners<.35f)assert(pixels>0&&avatar_scene::mouthVisibility(scene)>0);
   else assert(pixels==0&&avatar_scene::mouthVisibility(scene)==0);
#else
   assert(pixels==0&&avatar_scene::mouthVisibility(scene)==0);
#endif
  }
 }
}
