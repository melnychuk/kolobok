#pragma once
#include <stdint.h>

// A checked, unrotated RGB565 sprite view. Byte order is explicit; this is not
// a display-driver replacement and never writes while DMA is reading the sprite.
namespace framebuffer_raster {
inline uint16_t swap(uint16_t v) { return static_cast<uint16_t>((v<<8)|(v>>8)); }
inline uint16_t blend(uint16_t from,uint16_t to,float amount) {
 if(amount<=0)return from;
 if(amount>=1)return to;
 const int r=((from>>11)&31)+(((to>>11)&31)-((from>>11)&31))*amount+.5f;
 const int g=((from>>5)&63)+(((to>>5)&63)-((from>>5)&63))*amount+.5f;
 const int b=(from&31)+((to&31)-(from&31))*amount+.5f;
 return static_cast<uint16_t>((r<<11)|(g<<5)|b);
}
struct View {
 uint16_t* pixels=nullptr;
 int width=0,height=0,stride=0;
 bool swapped=true;
 void ink(int x,int y,uint16_t color,float alpha) const {
  if(!pixels || x<0 || y<0 || x>=width || y>=height || alpha<=0)return;
  uint16_t& dst=pixels[y*stride+x];
  if(alpha<1)color=blend(swapped?swap(dst):dst,color,alpha);
  if(swapped)dst=swap(color);
  else dst=color;
 }
};
} // namespace framebuffer_raster
