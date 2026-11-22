#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "../firmware/avatar_interaction.h"

// A counter check swells the body toward its numeral as a smooth bell carried
// by the side membrane (or the lower rim when paired). It replaced a capsule
// "hand" unioned with the body and wrapped in a convex envelope, whose
// straight tangent lines met at the numeral and read as an angular "<".

// Largest change of edge direction, in degrees, between the `span` rows above
// and below any row of a row-based edge. The base oval stays under ~8 degrees.
static float turn(const float* edge,int span) {
 float worst=0;
 for(int y=span;y<135-span;++y) {
  const float above=atan2f(edge[y]-edge[y-span],static_cast<float>(span));
  const float below=atan2f(edge[y+span]-edge[y],static_cast<float>(span));
  worst=fmaxf(worst,fabsf(below-above)*57.29578f);
 }
 return worst;
}
static float sideTurn(const avatar_scene::Mask& mask,int side) {
 float edge[135];
 for(int y=0;y<135;++y)edge[y]=side<0?mask.left[y]:mask.right[y];
 return turn(edge,6);
}
int main() {
 unsigned frames=0,checkFrames=0;
 float worstChecked=0,worstPlain=0,peakResidual=0;
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
    // As in the frame: the membrane carries the swell, lagging behind it.
    face_physics::Shape carried;avatar_scene::membraneShape(scene,carried);
    surface.follow(carried);
    avatar_scene::Mask mask,plain;avatar_scene::buildMask(scene,mask);
    auto rest=scene;rest.poke.strength=0;avatar_scene::buildMask(rest,plain);
    const auto swells=avatar_scene::pokeSwells(scene);
    if(swells.side==0)assert(memcmp(&mask,&plain,sizeof(mask))==0);
    else {
     // Only the facing side moves, and only outward.
     assert(swells.side==side&&swells.rim.amount==0);
     for(int y=0;y<135;++y) {
      if(side<0)assert(mask.left[y]<=plain.left[y]&&mask.right[y]==plain.right[y]);
      else assert(mask.right[y]>=plain.right[y]&&mask.left[y]==plain.left[y]);
     }
     assert(memcmp(mask.bottom,plain.bottom,sizeof(mask.bottom))==0);
     const float checked=sideTurn(mask,side),natural=sideTurn(plain,side);
     worstChecked=fmaxf(worstChecked,checked);worstPlain=fmaxf(worstPlain,natural);
     // No corner: at most a little more bend than the oval's own.
     if(checked>natural+3.5f)
      fprintf(stderr,"Angular check side=%d kiss=%d strip=%d cadence=%u clip=%d ms=%u: %.2f vs %.2f\n",
              side,kiss,strip,cadence,clip,ms,checked,natural);
     assert(checked<=natural+3.5f);
     ++checkFrames;
    }
    // The skin carries the swell with follow-through, then settles.
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
 assert(checkFrames>3000&&peakResidual>1);
 printf("Check swell: %u frames / %u check frames; worst 12-row turn %.2f deg (oval alone %.2f), "
        "membrane follow-through up to %.2f px\n",frames,checkFrames,worstChecked,worstPlain,peakResidual);

 // Reflow blends the side swell into the rim swell continuously, and paired,
 // request and sleeping layouts carry no side swell at all.
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
