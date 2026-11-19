#pragma once
#include "display_fonts.h"
#include "roman_counts.h"

// Font fitting is layout-specific. Side strips retain their 56px limit;
// bottom labels grow inward from the corners and share the whole screen.
namespace counter_labels {
constexpr int kSideLimit=56,kMargin=8,kGap=10,kBottomBudget=240-2*kMargin-kGap;
inline const count_raster::Font& font(int index) {
 const count_raster::Font* const fonts[]={&kBadgeFont,&kMediumBadgeFont,&kSmallBadgeFont,
                                       &kTinyBadgeFont,&kCompactBadgeFont};
 return *fonts[index];
}
struct Label { int index=0,width=0; };
struct State {
 int counts[2]={-1,-1},old[2]={-1,-1};
 int widths[2][5]={},reserved[2][5]={},side[2]={},bottom[2]={};
 Label labels[2];
 float lastCorners=-1;
 void update(int done,int active,float corners,int oldDone=-1,int oldActive=-1) {
  const int current[]={done,active},previous[]={oldDone,oldActive};
  bool changed=false;
  for(int s=0;s<2;++s)if(counts[s]!=current[s]||old[s]!=previous[s])changed=true;
  if(changed) {
   for(int s=0;s<2;++s) {
    counts[s]=current[s];old[s]=previous[s];
    char text[10],before[10];roman_counts::format(counts[s],text);
    roman_counts::format(old[s]<0?counts[s]:old[s],before);
    for(int f=0;f<5;++f) {
     widths[s][f]=count_raster::width(font(f),text);
     const int prior=count_raster::width(font(f),before);
     reserved[s][f]=prior>widths[s][f]?prior:widths[s][f];
    }
    side[s]=0;while(side[s]<4&&widths[s][side[s]]>kSideLimit)++side[s];
    bottom[s]=0;
   }
   // Only reduce fonts if their actual combined widths would collide.
   // Reduce the wider label first; equal widths stay visually balanced.
   while(reserved[0][bottom[0]]+reserved[1][bottom[1]]>kBottomBudget) {
    const int left=reserved[0][bottom[0]],right=reserved[1][bottom[1]];
    if(left>=right&&bottom[0]<4)++bottom[0];
    if(right>=left&&bottom[1]<4)++bottom[1];
   }
  }
  if(!changed&&corners==lastCorners)return;
  lastCorners=corners;
  for(int s=0;s<2;++s) {
   // Promote through the existing discrete fonts as the label moves down;
   // never stretch glyphs, or show a full-width label in a narrow side strip.
   const float full=reserved[s][bottom[s]]>kSideLimit?reserved[s][bottom[s]]:kSideLimit;
   const float limit=kSideLimit+(full+8-kSideLimit)*corners;
   int f=bottom[s];
   while(f<4&&widths[s][f]>limit)++f;
   labels[s].index=f;labels[s].width=widths[s][f];
  }
 }
 int sideWidth(int s) const { return widths[s][side[s]]; }
 int flightWidth(int s) const { return reserved[s][bottom[s]]; }
 float inset(int s,float exposed,float corners) const {
  const float corner=kMargin+labels[s].width/2.0f;
  const float eased=exposed/2+((corner>24?corner:24)-exposed/2)*corners;
  // Font promotions grow inward, never out through the screen edge while
  // the label is still travelling down from a narrow side strip.
  return eased>corner?eased:corner;
 }
};
} // namespace counter_labels
