#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>

namespace count_raster {
struct Glyph {
  char code;
  uint8_t width, height, advance;
  int8_t left, top;
  const uint8_t* alpha;
};
struct Font {
  uint8_t capHeight;
  uint8_t glyphCount;
  const Glyph* glyphs;
};
inline const Glyph* glyph(const Font& font, char code) {
  for (int i = 0; i < font.glyphCount; ++i) {
    if (font.glyphs[i].code == code) return &font.glyphs[i];
  }
  return nullptr;
}
inline int width(const Font& font, const char* label) {
  int result = 0;
  for (; *label; ++label) {
    const Glyph* g = glyph(font, *label);
    if (g) result += g->advance;
  }
  return result;
}
inline uint8_t alphaAt(const Glyph& glyph,int x,int y) {
  if(x<0||x>=glyph.width||y<0||y>=glyph.height)return 0;
  return glyph.alpha[y*glyph.width+x];
}
// Draw all outlines first, then all solid interiors: neighboring glyph halos
// must never erase an earlier letter's black stroke. Callback owns colors.
template<class Pixel>
void render(const Font& font,const char* label,float centerX,float centerY,Pixel pixel) {
  const int start=static_cast<int>(lroundf(centerX-width(font,label)/2.0f));
  const int baseline=static_cast<int>(lroundf(centerY+font.capHeight/2.0f));
  for(int pass=0;pass<2;++pass) {
    int cursor=start;
    for(const char* c=label;*c;++c) {
      const Glyph* g=glyph(font,*c);if(!g)continue;
      const int padding=pass==0?2:0;
      for(int y=-padding;y<g->height+padding;++y)for(int x=-padding;x<g->width+padding;++x) {
        uint8_t alpha=alphaAt(*g,x,y);
        if(pass==0)for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
          if(dx*dx+dy*dy>4)continue;
          const uint8_t neighbor=alphaAt(*g,x+dx,y+dy);
          if(neighbor>alpha)alpha=neighbor;
        }
        if(alpha)pixel(cursor+g->left+x,baseline+g->top+y,alpha/255.0f,pass==0);
      }
      cursor+=g->advance;
    }
  }
}
// Transient card flip for completion transfers only: a horizontal scale of the
// same glyph coverage (negative mirrors the back face), box-filtered so a thin
// edge-on card stays antialiased. A fresh 2px outline is grown around the
// squeezed interior and all outlines still precede all fills. Resting labels
// never use this path. Oversized labels fall back to the unscaled renderer.
template<class Pixel>
void renderScaled(const Font& font,const char* label,float centerX,float centerY,float scaleX,
                  Pixel pixel) {
  constexpr int kColumns=96,kRows=32,kPad=2;
  if(scaleX>=.999f) { render(font,label,centerX,centerY,pixel);return; }
  const float magnitude=fmaxf(.04f,fabsf(scaleX));
  const float scale=scaleX<0?-magnitude:magnitude;
  int top=0,bottom=0,right=0,cursor=0;
  for(const char* c=label;*c;++c) {
    const Glyph* g=glyph(font,*c);if(!g)continue;
    if(g->top<top)top=g->top;
    if(g->top+g->height>bottom)bottom=g->top+g->height;
    if(cursor+g->left<0) { render(font,label,centerX,centerY,pixel);return; }
    if(cursor+g->left+g->width>right)right=cursor+g->left+g->width;
    cursor+=g->advance;
  }
  const int rows=bottom-top,total=width(font,label);
  if(rows<=0||rows>kRows||right>kColumns) { render(font,label,centerX,centerY,pixel);return; }
  // Static scratch: single render thread, bounded size, no ESP32 stack spike.
  static uint16_t sums[kRows][kColumns+1];
  static uint8_t fill[kRows+2*kPad][kColumns+2*kPad+2];
  for(int y=0;y<rows;++y) {
    uint8_t coverage[kColumns]={};
    int cursor=0;
    for(const char* c=label;*c;++c) {
      const Glyph* g=glyph(font,*c);if(!g)continue;
      for(int x=0;x<g->width;++x) {
        const uint8_t a=alphaAt(*g,x,y+top-g->top);
        uint8_t& cell=coverage[cursor+g->left+x];
        if(a>cell)cell=a;
      }
      cursor+=g->advance;
    }
    sums[y][0]=0;
    for(int x=0;x<right;++x)sums[y][x+1]=static_cast<uint16_t>(sums[y][x]+coverage[x]);
  }
  // Source column u maps to centerX+scale*(u-total/2); right may exceed total.
  const float lo=fminf(centerX+scale*(0-total/2.0f),centerX+scale*(right-total/2.0f));
  const int x0=static_cast<int>(floorf(lo));
  const int columns=static_cast<int>(ceilf(magnitude*right))+2;
  if(columns>kColumns+2) { render(font,label,centerX,centerY,pixel);return; }
  const int baseline=static_cast<int>(lroundf(centerY+font.capHeight/2.0f));
  const auto integral=[&](int y,float u) {
    if(u<=0)return 0.0f;
    if(u>=right)return static_cast<float>(sums[y][right]);
    const int whole=static_cast<int>(u);
    return sums[y][whole]+(u-whole)*(sums[y][whole+1]-sums[y][whole]);
  };
  for(int y=0;y<rows+2*kPad;++y)for(int x=0;x<columns+2*kPad;++x)fill[y][x]=0;
  for(int y=0;y<rows;++y)for(int x=0;x<columns;++x) {
    float a=(x0+x-centerX)/scale+total/2.0f,b=(x0+x+1-centerX)/scale+total/2.0f;
    if(a>b){const float t=a;a=b;b=t;}
    const float value=(integral(y,b)-integral(y,a))/(b-a);
    fill[y+kPad][x+kPad]=static_cast<uint8_t>(fminf(255,value+.5f));
  }
  for(int pass=0;pass<2;++pass)
    for(int y=0;y<rows+2*kPad;++y)for(int x=0;x<columns+2*kPad;++x) {
      uint8_t alpha=fill[y][x];
      if(pass==0)for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
        if(dx*dx+dy*dy>4)continue;
        const int sy=y+dy,sx=x+dx;
        if(sy<0||sx<0||sy>=rows+2*kPad||sx>=columns+2*kPad)continue;
        if(fill[sy][sx]>alpha)alpha=fill[sy][sx];
      }
      if(alpha)pixel(x0+x-kPad,baseline+top+y-kPad,alpha/255.0f,pass==0);
    }
}
// Immutable generated fonts only. Cache per-glyph outline coverage, never the
// final screen pixels: preserve repeated AA blends and all-outline/all-fill order.
// Unsupported fonts/labels fall back to the scalar renderer without truncation.
struct Cache {
 static constexpr int slots=6,side=32,labelCapacity=10;
 const Font* previous=nullptr;
 char text[labelCapacity]={};
 int8_t indices[labelCapacity]={};
 int length=0,labelWidth=0;
 bool ready[slots]={};
 uint8_t outline[slots][side*side]={};
 bool prepare(const Font& font,const char* label) {
  const size_t size=strlen(label);
  if(size>=labelCapacity || font.glyphCount>slots)return false;
  for(int i=0;i<font.glyphCount;++i)
   if(font.glyphs[i].width+4>side || font.glyphs[i].height+4>side)return false;
  if(previous==&font && strcmp(text,label)==0)return true;
  if(previous!=&font) {
   previous=&font;
   for(int i=0;i<slots;++i)ready[i]=false;
  }
  memcpy(text,label,size+1);length=static_cast<int>(size);labelWidth=0;
  for(int c=0;c<length;++c) {
   int index=-1;
   for(int i=0;i<font.glyphCount;++i)if(font.glyphs[i].code==label[c]){index=i;break;}
   indices[c]=static_cast<int8_t>(index);
   if(index<0)continue;
   const Glyph& g=font.glyphs[index];labelWidth+=g.advance;
   if(ready[index])continue;
   for(int y=-2;y<g.height+2;++y)for(int x=-2;x<g.width+2;++x) {
    uint8_t alpha=alphaAt(g,x,y);
    for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
     if(dx*dx+dy*dy>4)continue;
     const uint8_t neighbor=alphaAt(g,x+dx,y+dy);
     if(neighbor>alpha)alpha=neighbor;
    }
    outline[index][(y+2)*side+x+2]=alpha;
   }
   ready[index]=true;
  }
  return true;
 }
 template<class Pixel>
 void render(const Font& font,const char* label,float centerX,float centerY,Pixel pixel,
             float shiftX=0,float shiftY=0) {
  if(!prepare(font,label)) {
   count_raster::render(font,label,centerX+shiftX,centerY+shiftY,pixel);return;
  }
  const int start=static_cast<int>(lroundf(centerX-labelWidth/2.0f));
  const int baseline=static_cast<int>(lroundf(centerY+font.capHeight/2.0f));
  if(shiftX!=0||shiftY!=0) { shifted(font,start,baseline,shiftX,shiftY,pixel);return; }
  for(int pass=0;pass<2;++pass) {
   int cursor=start;
   for(int c=0;c<length;++c) {
    const int index=indices[c];if(index<0)continue;
    const Glyph& g=font.glyphs[index];const int padding=pass==0?2:0;
    for(int y=-padding;y<g.height+padding;++y)for(int x=-padding;x<g.width+padding;++x) {
     const uint8_t alpha=pass==0?outline[index][(y+2)*side+x+2]:alphaAt(g,x,y);
     if(alpha)pixel(cursor+g.left+x,baseline+g.top+y,alpha/255.0f,pass==0);
    }
    cursor+=g.advance;
   }
  }
 }
 // Sub-pixel offset for small, slow motion (contact springs): a bilinear
 // resample of the same cached coverage, keeping all outlines before all
 // interiors. Interiors stay solid black; only edges gain/lose AA while the
 // label moves. A whole-pixel shift reproduces the exact path one pixel over.
 uint8_t cached(int index,bool outlinePass,int x,int y) const {
  const Glyph& g=previous->glyphs[index];
  if(outlinePass) {
   if(x<-2||y<-2||x>=g.width+2||y>=g.height+2)return 0;
   return outline[index][(y+2)*side+x+2];
  }
  return alphaAt(g,x,y);
 }
 template<class Pixel>
 void shifted(const Font& font,int start,int baseline,float shiftX,float shiftY,Pixel pixel) const {
  const int ix=static_cast<int>(floorf(shiftX)),iy=static_cast<int>(floorf(shiftY));
  const float fx=shiftX-ix,fy=shiftY-iy;
  const float w00=(1-fx)*(1-fy),w10=fx*(1-fy),w01=(1-fx)*fy,w11=fx*fy;
  for(int pass=0;pass<2;++pass) {
   int cursor=start;
   for(int c=0;c<length;++c) {
    const int index=indices[c];if(index<0)continue;
    const Glyph& g=font.glyphs[index];const int padding=pass==0?2:0;
    for(int y=-padding;y<=g.height+padding;++y)for(int x=-padding;x<=g.width+padding;++x) {
     const float alpha=w00*cached(index,pass==0,x,y)+w10*cached(index,pass==0,x-1,y)+
                       w01*cached(index,pass==0,x,y-1)+w11*cached(index,pass==0,x-1,y-1);
     if(alpha>0)pixel(cursor+g.left+x+ix,baseline+g.top+y+iy,fminf(1,alpha/255.0f),pass==0);
    }
    cursor+=g.advance;
   }
  }
 }
};
}  // namespace count_raster
