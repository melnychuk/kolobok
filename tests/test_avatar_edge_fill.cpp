#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../firmware/avatar_scene.h"

int main() {
 unsigned frames=0;
 float worstBend=0,worstCut=0;
 unsigned long cuts=0;
 for(int side=-1;side<=1;side+=2)for(int clip=0;clip<7;++clip)
 for(int strip=48;strip<=72;strip+=24) {
  eye_acting::Player actor;
  actor.state=side>0?eye_acting::State::done:eye_acting::State::thinking;
  actor.index=clip;
  face_physics::Surface surface;
  for(uint32_t ms=0;ms<8000;ms+=33) {
   avatar_scene::Scene scene;scene.corners=0;scene.region=240-2*strip;
   scene.offset=side*strip/2.0f;scene.eyeY=67.5f;
   scene.body=actor.body(ms);scene.eyes=actor.eyes(ms);
   scene.eyeX=120+scene.offset+scene.eyes.x;
   surface.step(.033f,scene.body,false);scene.surface=&surface;
   scene.flowClock=ms*.0006f;
   avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);
   // Even if blue/green is deliberately retained underneath, no colored rim
   // can leak through the unused edge, including the first/last screen rows.
   for(int y=0;y<135;++y)for(int dx=0;dx<16;++dx)
    assert(mask.baseCoverage(side>0?239-dx:dx,y)==1);
   // The counter-facing soft side alone shapes the silhouette: the lower
   // envelope must not cut it into a corner, and the edge stays smooth.
   const float* facing=side>0?mask.left:mask.right;
   for(int y=0;y<135;++y) {
    const int first=static_cast<int>(fmaxf(0,ceilf(mask.left[y])));
    const int last=static_cast<int>(fminf(240,floorf(mask.right[y])));
    for(int x=first;x<last;++x)if(mask.bottom[x]<y+1) {
     ++cuts;worstCut=fmaxf(worstCut,y+1-mask.bottom[x]);
    }
    if(y>0&&y<134) {
     const float bend=fabsf(facing[y+1]-2*facing[y]+facing[y-1]);
     worstBend=fmaxf(worstBend,bend);assert(bend<.2f);
    }
   }
   // Suppressed layouts and tiny entry progress retain continuous geometry.
   scene.corners=1;assert(avatar_scene::unusedSideStretch(scene)==0);
   scene.corners=0;scene.alert=1;assert(avatar_scene::unusedSideStretch(scene)==0);
   scene.alert=0;scene.sleep=1;assert(avatar_scene::unusedSideStretch(scene)==0);
   scene.sleep=0;scene.offset=0;assert(avatar_scene::unusedSideStretch(scene)==0);
   scene.offset=side*1e-8f;assert(fabsf(avatar_scene::unusedSideStretch(scene))<1e-12f);
   ++frames;
  }
 }
 assert(cuts==0&&worstCut==0); // no lower-envelope corner on any side row
 printf("Offscreen single-side fill: %u authored frames, both edges opaque; "
        "soft facing edge max bend %.3f px/row^2\n",frames,worstBend);
}
