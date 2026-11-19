#include <assert.h>
#include <stdio.h>
#include <vector>
#include "../firmware/avatar_scene.h"
#include "../firmware/framebuffer_raster.h"
#include "reference_eye_raster.h"

void checkSpans(const avatar_scene::Mask& mask) {
 std::vector<float> actual(240*135,0);
 mask.raster([&](int x,int y,int n) {
  assert(x>=0 && x+n<=240 && y>=0 && y<135 && n>0);
  for(int i=0;i<n;++i){assert(actual[y*240+x+i]==0);actual[y*240+x+i]=1;}
 },[&](int x,int y,float a) {
  assert(x>=0 && x<240 && y>=0 && y<135 && a>0 && a<.999f);
  assert(actual[y*240+x]==0);actual[y*240+x]=a;
 });
 for(int y=0;y<135;++y)for(int x=0;x<240;++x) {
  float a=mask.coverage(x,y);
  if(a>=.999f)a=1;
  assert(actual[y*240+x]==a);
 }
}

int main() {
 unsigned comparisons=0;
 for(int mode=0;mode<6;++mode) {
  avatar_scene::Scene scene;
  scene.corners=mode/5.0f;scene.offset=(mode-3)*13.0f;
  scene.sleep=mode==5?.85f:0;scene.alert=mode==4?.8f:0;
  scene.flowClock=mode*.37f;
  avatar_scene::Mask mask;avatar_scene::buildMask(scene,mask);
  checkSpans(mask);
  for(int y=-1;y<=135;++y)for(int x=-1;x<=240;++x) {
   const float expected=x<0||x>=240||y<0||y>=135?0:
    fminf(face_contour::coverage(mask.left[y],mask.right[y],x),fmaxf(0,fminf(1,mask.bottom[x]-y)));
   assert(mask.coverage(x,y)==expected);++comparisons;
  }
 }
 avatar_scene::Mask holes;
 for(int y=0;y<135;++y){holes.left[y]=3.2f;holes.right[y]=232.9f;}
 for(int x=0;x<240;++x)holes.bottom[x]=100+24*sinf(x*.12f);
 holes.bottom[42]=-1000;holes.bottom[128]=87.9995f;
 checkSpans(holes);
 for(int state=0;state<3;++state)for(int clip=0;clip<eye_acting::clipCount(static_cast<eye_acting::State>(state));++clip) {
  eye_acting::Player player;player.state=static_cast<eye_acting::State>(state);player.index=clip;
  for(unsigned ms=0;ms<4000;ms+=137) {
   std::vector<float> actual(240*135,0),expected(240*135,0);
   const auto pose=player.eyes(ms);
   const float width=50*pose.width,height=56*pose.height;
   eye_raster::render(120,67.5f,width,height,19,pose,[&](int x,int y,float a) {
    if(x>=0&&x<240&&y>=0&&y<135)actual[y*240+x]=a;
   });
   reference_eye_raster::render(120,67.5f,width,height,19,pose,[&](int x,int y,float a) {
    if(x>=0&&x<240&&y>=0&&y<135)expected[y*240+x]=a;
   });
   assert(actual==expected);comparisons+=actual.size();
  }
 }
 for(int sample=0;sample<300;++sample) {
  auto pose=eye_acting::p();
  pose.tilt=(sample%181)-90;pose.closeL=(sample%11)/10.0f;pose.closeR=((sample*3)%11)/10.0f;
  pose.smile=(sample%7)/6.0f;pose.upperL=(sample%5)/5.0f;pose.upperR=(sample%3)/3.0f;
  const float width=2+(sample*37)%123,height=3+(sample*17)%118;
  std::vector<float> actual(240*135,0),expected(240*135,0);
  eye_raster::render(117.3f,64.9f,width,height,17,pose,[&](int x,int y,float a) {
   if(x>=0&&x<240&&y>=0&&y<135)actual[y*240+x]=a;
  });
  reference_eye_raster::render(117.3f,64.9f,width,height,17,pose,[&](int x,int y,float a) {
   if(x>=0&&x<240&&y>=0&&y<135)expected[y*240+x]=a;
  });
  assert(actual==expected);comparisons+=actual.size();
 }
 for(int swapped=0;swapped<2;++swapped) {
  uint16_t pixels[18]={};
  framebuffer_raster::View view;view.pixels=pixels+1;view.width=3;view.height=4;view.stride=4;view.swapped=swapped;
  for(int i=0;i<100;++i) {
   const uint16_t old=swapped?framebuffer_raster::swap(pixels[1]):pixels[1];
   const uint16_t color=static_cast<uint16_t>(i*643);
   const float alpha=(i%11)/10.0f;
   view.ink(0,0,color,alpha);
   assert((swapped?framebuffer_raster::swap(pixels[1]):pixels[1])==framebuffer_raster::blend(old,color,alpha));
   view.ink(-1,0,color,alpha);view.ink(3,0,color,alpha);view.ink(0,4,color,alpha);
   assert(pixels[0]==0 && pixels[17]==0 && pixels[4]==0);
  }
 }
 printf("Exact raster comparisons: %u\n",comparisons);
}
