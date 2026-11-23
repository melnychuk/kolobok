#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "../firmware/avatar_interaction.h"

// A counter check grows a round cap toward its numeral with a small root
// swell carried by the side membrane (or the lower rim when paired); a kiss
// is a broad cheek swell instead. It replaced a broad bell swell that, with a
// body lean and rim kick, read as the whole body bumping the numeral; before
// that a capsule "hand" in a convex envelope read as an angular "<".
int main() {
 unsigned frames=0,checkFrames=0;
 float peakReach=0,peakResidual=0;
 for(int side: {-1,1})for(int kiss: {0,1})for(int strip: {48,72})
 for(uint32_t cadence: {17u,33u,50u}) {
  // `side` is the counter's screen side; its status follows status_sides.
  const bool isDone=side==status_sides::kDone;
  const auto state=isDone?eye_acting::State::done:eye_acting::State::thinking;
  for(int clip=0;clip<eye_acting::clipCount(state);++clip) {
   avatar_interaction::Checks checks;checks.fixed=true;checks.actionCycle=kiss;
   face_physics::Surface surface;
   eye_acting::Player actor;actor.state=state;actor.index=clip;
   float settledResidual=0;
   for(uint32_t ms=0;ms<9800;ms+=cadence) {
    checks.update(ms,true,&surface,isDone,!isDone);
    avatar_scene::Scene scene;scene.corners=0;scene.region=240-2*strip;
    scene.offset=-side*strip/2.0f;scene.eyeY=67.5f;
    scene.body=checks.apply(actor.body(ms),ms,true);scene.eyes=checks.apply(actor.eyes(ms),ms);
    surface.step(cadence*.001f,scene.body,false);scene.surface=&surface;
    scene.flowClock=ms*.0006f;
    checks.shape(scene,ms,status_sides::fromEdge(status_sides::kDone,strip/2.0f),
                 status_sides::fromEdge(status_sides::kActive,strip/2.0f),67.5f,14,14,23,23);
    // As in the frame: the membrane carries the root swell, lagging behind it.
    face_physics::Shape carried;avatar_scene::membraneShape(scene,carried);
    surface.follow(carried);
    avatar_scene::Mask mask,plain;avatar_scene::buildMask(scene,mask);
    auto rest=scene;rest.poke.strength=0;avatar_scene::buildMask(rest,plain);
    const auto swells=avatar_scene::pokeSwells(scene);
    if(swells.side==0)assert(memcmp(&mask,&plain,sizeof(mask))==0);
    else {
     // Only the facing side moves, only outward, and only near the numeral.
     assert(swells.side==side&&swells.rim.amount==0);
     for(int y=0;y<135;++y) {
      if(side<0)assert(mask.left[y]<=plain.left[y]&&mask.right[y]==plain.right[y]);
      else assert(mask.right[y]>=plain.right[y]&&mask.left[y]==plain.left[y]);
      if(fabsf(y+.5f-67.5f)>=swells.edge.width)
       assert(mask.left[y]==plain.left[y]&&mask.right[y]==plain.right[y]);
     }
     assert(memcmp(mask.bottom,plain.bottom,sizeof(mask.bottom))==0);
     // A blob, not a bump: the extrusion at the numeral's row dwarfs the
     // root swell, and it narrows toward its tip.
     const float reach=side<0?plain.left[67]-mask.left[67]:mask.right[67]-plain.right[67];
     peakReach=fmaxf(peakReach,reach);
     // (A kiss is a broad cheek nuzzle instead, with no blob.)
     if(scene.poke.kiss==0&&reach>avatar_scene::kPokeMinLength) {
      assert(swells.edge.amount<.2f*reach);
      int over=0;
      for(int y=0;y<135;++y) {
       const float r=side<0?plain.left[y]-mask.left[y]:mask.right[y]-plain.right[y];
       if(r>.75f*reach)++over;
      }
      assert(over<=2*fmaxf(avatar_scene::kPokeRadiusMax,reach));
     }
     ++checkFrames;
    }
    // The skin carries the root swell with follow-through, then settles.
    const int s=side<0?0:1;
    float residual=0;
    for(int i=0;i<face_physics::kEdgeNodes;++i)residual=fmaxf(residual,fabsf(surface.edge[s][i]));
    if(checks.moment(ms,true).side==side)peakResidual=fmaxf(peakResidual,residual);
    if(ms>=9500)settledResidual=fmaxf(settledResidual,residual);
    ++frames;
   }
   // The checks end by ~7.1s (done) or 3.6s (active) and the next one starts
   // at 9.8s: the follow-through wobble has died away in between.
   assert(settledResidual<.25f);
  }
 }
 assert(checkFrames>3000&&peakResidual>.25f&&peakReach>avatar_scene::kPokeMinLength);
 printf("Check pokes: %u frames / %u check frames; reach up to %.2f px, "
        "membrane follow-through up to %.2f px\n",frames,checkFrames,peakReach,peakResidual);

 // Reflow blends the sideways cap into the downward one continuously,
 // and paired, request and sleeping layouts carry no side swell at all.
 avatar_scene::Mask previous;bool first=true;
 for(float corners=0;corners<=1.0001f;corners+=.01f) {
  avatar_scene::Scene scene;scene.corners=fminf(1,corners);scene.offset=24*(1-scene.corners);
  scene.poke.side=-1;scene.poke.strength=1;scene.poke.cx=24;
  scene.poke.cy=avatar_scene::mix(67.5f,116,scene.corners);scene.poke.width=14;scene.poke.height=23;
  const auto swells=avatar_scene::pokeSwells(scene);
  if(scene.corners==1)assert(swells.edge.amount==0&&swells.rim.amount>0);
  if(scene.corners==0)assert(swells.rim.amount==0&&swells.edge.amount>0);
  avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);
  if(!first)for(int y=0;y<135;++y)assert(fabsf(mask.left[y]-previous.left[y])<2);
  previous=mask;first=false;
  scene.alert=1;assert(avatar_scene::pokeSwells(scene).side==0);
  scene.alert=0;scene.sleep=1;assert(avatar_scene::pokeSwells(scene).side==0);
 }
}
