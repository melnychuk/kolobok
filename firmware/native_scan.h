#pragma once
#include <stdint.h>

// The Stick's ST7789 refreshes its 135x240 glass along the portrait rows, one
// landscape COLUMN at a time. A landscape MADCTL (rotation 1/3, MV=1) makes
// the controller fill GRAM one landscape row at a time instead: the write runs
// across the refresh, so every frame that lands mid-refresh shows a diagonal
// shear (old frame on one side of a slanted line, new on the other). With no
// TE pin there is no way to time the write around it, and the shear of a
// fast-moving edge reads as tearing across a solid background.
//
// Presenting in the panel's own order (rotation 0, MV=0) makes the write run
// with the refresh: a frame can still be caught mid-write, but the boundary is
// one straight scan line instead of a slant through every moving edge.
//
// transpose() copies a landscape frame (w x h, row-major) into a native
// portrait buffer (h wide, w tall) exactly where M5GFX's rotation `rotation`
// (1 or 3) would put each pixel (LovyanGFX Panel_HasBuffer::_rotate_pos):
//   rotation 1: landscape (x,y) -> native (h-1-y, x)
//   rotation 3: landscape (x,y) -> native (y, w-1-x)
// Pixels are copied as opaque 16-bit values: no byte-order change.
namespace native_scan {
inline void transpose(const uint16_t* src,int w,int h,uint16_t* dst,int rotation) {
 if(rotation==1) {
  for(int x=0;x<w;++x) {  // Native row x holds landscape column x, bottom first.
   uint16_t* row=dst+x*h;
   for(int n=0,i=(h-1)*w+x;n<h;++n,i-=w)row[n]=src[i];
  }
 } else {
  for(int r=0;r<w;++r) {  // Native row r holds landscape column w-1-r, top first.
   uint16_t* row=dst+r*h;
   for(int n=0,i=w-1-r;n<h;++n,i+=w)row[n]=src[i];
  }
 }
}
} // namespace native_scan
