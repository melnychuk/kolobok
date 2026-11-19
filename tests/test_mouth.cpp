#include <assert.h>
#include <math.h>
// Preserve the authored mouth implementation's opt-in regression coverage.
#define STICK_S3_ENABLE_MOUTH 1
#include "../firmware/avatar_scene.h"

struct Image {
 float alpha[240*135]={};
 void ink(int x,int y,float value) {
  assert(x>=0&&x<240&&y>=0&&y<135&&isfinite(value)&&value>0&&value<=1);
  alpha[y*240+x]=value;
 }
 float sum() const { float result=0;for(const auto value:alpha)result+=value;return result; }
 float centroid(bool vertical) const {
  float moment=0;
  for(int y=0;y<135;++y)for(int x=0;x<240;++x)moment+=alpha[y*240+x]*((vertical?y:x)+.5f);
  return moment/sum();
 }
};
Image raster(const mouth_raster::Shape& shape) {
 Image image;
 mouth_raster::render(120.5f,108.5f,shape,1,[&](int x,int y,float a){image.ink(x,y,a);});
 return image;
}
Image sceneMouth(const avatar_scene::Scene& scene,const avatar_scene::Mask& mask) {
 Image image;
 avatar_scene::mouth(scene,mask,[&](int x,int y,float a){image.ink(x,y,a);});
 return image;
}
int main() {
 const auto neutral=mouth_raster::expression(eye_acting::p());
 assert(neutral.halfWidth==14&&neutral.curve==0&&neutral.opening==0);
 const auto blink=mouth_raster::expression(eye_acting::p(0,0,1,1,0,0,0,1,1));
 assert(blink.halfWidth==neutral.halfWidth&&blink.curve==0&&blink.opening==0);
 const auto happy=mouth_raster::expression(eye_acting::p(0,0,1,1,0,0,.75f));
 assert(happy.curve==6&&happy.halfWidth>neutral.halfWidth&&happy.opening==0);
 const auto aha=mouth_raster::expression(eye_acting::p(0,-8,1,1.24f));
 assert(aha.opening>5.9f&&aha.halfWidth<neutral.halfWidth);
 const auto focus=mouth_raster::expression(eye_acting::p(0,0,1,.9f,.2f,.2f));
 assert(focus.halfWidth<neutral.halfWidth&&focus.opening==0);
 const auto line=raster(neutral),smile=raster(happy),oval=raster(aha);
 assert(line.alpha[108*240+120]>.99f);
 assert(line.alpha[107*240+120]>.99f&&line.alpha[109*240+120]>.99f); // Three opaque rows, not a hairline.
 assert(smile.alpha[108*240+120]==0&&smile.alpha[114*240+120]>.99f);
 assert(oval.alpha[108*240+120]==0&&oval.sum()>line.sum()); // Genuine open center.

 const auto pinched=mouth_raster::expression(eye_acting::p(0,0,1,.9f,.2f,.2f),true);
 assert(pinched.purse==1&&pinched.halfWidth<=8&&pinched.opening==0);
 const auto lips=raster(pinched);
 assert(lips.alpha[108*240+120]>0&&lips.alpha[108*240+120]<.7f); // Closed lip crease, not an open O.
 assert(lips.alpha[110*240+120]>.99f); // Visible upper/lower lip volume.
 const auto lookLeft=mouth_raster::expression(eye_acting::p(-12),true);
 const auto lookRight=mouth_raster::expression(eye_acting::p(12),true);
 assert(lookLeft.skew<0&&lookRight.skew>0&&lookLeft.tilt<lookRight.tilt);

 avatar_scene::Scene scene;scene.corners=0;scene.eyeY=67.5f;
 avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);
 const auto resting=sceneMouth(scene,mask);
 assert(resting.sum()>20&&fabsf(resting.centroid(true)-112.5f)<.02f); // 5px below the initial mouth.
 scene.thinking=true;scene.eyeX=108;scene.body=eye_acting::p(-12);
 const auto left=sceneMouth(scene,mask);
 scene.eyeX=132;scene.body=eye_acting::p(12);
 const auto right=sceneMouth(scene,mask);
 assert(right.centroid(false)-left.centroid(false)>13); // Lips travel with the eye look.
 scene.thinking=false;scene.eyeX=120;scene.body=eye_acting::p();
 float previous=1;
 for(int step=0;step<=1000;++step) {
  scene.corners=step/1000.0f;
  const float visible=avatar_scene::mouthVisibility(scene);
  assert(visible<=previous&&visible>=0&&visible<=1);previous=visible;
  if(scene.corners>=.35f)assert(visible==0);
 }
 avatar_scene::buildMask(scene,mask);
 assert(sceneMouth(scene,mask).sum()==0); // Both counters: unchanged eyes-only face.
 scene.corners=0;scene.alert=1;
 assert(avatar_scene::mouthVisibility(scene)==0); // Wide request layout too.
 scene.alert=0;scene.sleep=1;
 assert(avatar_scene::mouthVisibility(scene)==0);
 scene.sleep=0;
 avatar_scene::Mask empty;
 assert(sceneMouth(scene,empty).sum()==0);
 avatar_scene::Mask cut;
 for(int y=0;y<135;++y){cut.left[y]=0;cut.right[y]=240;}
 for(int x=0;x<240;++x)cut.bottom[x]=112.2f;
 const auto clipped=sceneMouth(scene,cut);
 assert(clipped.sum()>0&&clipped.sum()<line.sum());
 for(int y=113;y<135;++y)for(int x=0;x<240;++x)assert(clipped.alpha[y*240+x]==0);

 // Every thinking/completion script in both single-side vertical layouts.
 // Mouth is a delayed secondary action, never an additional autonomous loop.
 for(int s=0;s<2;++s)for(int index=0;index<eye_acting::clipCount(static_cast<eye_acting::State>(s));++index)for(int side=-1;side<=1;side+=2) {
  eye_acting::Player player;player.state=static_cast<eye_acting::State>(s);player.index=index;
  const auto& clip=eye_acting::clip(player.state,index);
  for(uint32_t ms=0;ms<=eye_acting::duration(clip)+700u;ms+=33) {
   scene=avatar_scene::Scene();scene.corners=0;scene.offset=side*24;
   scene.eyes=player.eyes(ms);scene.body=player.body(ms);scene.thinking=s==0;
   scene.eyeWidth=50*scene.eyes.width;scene.eyeHeight=56*scene.eyes.height;
   scene.eyeX=120+scene.offset+scene.eyes.x;scene.eyeY=67.5f+scene.eyes.y;
   avatar_scene::buildMask(scene,mask);
   int pixels=0;
   avatar_scene::mouth(scene,mask,[&](int x,int y,float a) {
    ++pixels;assert(a>0&&a<=1&&isfinite(a));
    assert(x>=0&&x<240&&y>=0&&y<135&&mask.coverage(x,y)>0);
    assert(y>scene.eyeY+scene.eyeHeight/2+3); // Mouth cannot touch the eyes.
   });
   assert(pixels>0);
   const auto shape=mouth_raster::expression(scene.body,scene.thinking);
   assert(shape.curve>=0); // Completion has no frown shape.
  }
 }
 eye_acting::Player player;player.state=eye_acting::State::thinking;player.index=4;
 // Aha insight now pops from a downward ponder at 1080-1180ms.
 const auto leading=mouth_raster::expression(player.eyes(1160),true);
 const auto following=mouth_raster::expression(player.body(1160),true);
 assert(leading.opening>following.opening); // Eyes register the insight first.
 const auto heldA=mouth_raster::expression(player.body(1600),true);
 const auto heldB=mouth_raster::expression(player.body(1700),true);
 assert(heldA.opening==heldB.opening&&heldA.curve==heldB.curve&&heldA.purse==heldB.purse); // Readable still hold.
}
