#pragma once
#include <stdint.h>
#include "request_scene.h"

// Dark and light color themes, shared by the firmware, the native previews and
// the Herdr widget's WebAssembly. Body is the face silhouette and any screen
// without a status; ink fills numeral interiors and the request `!`.
namespace avatar_palette {
constexpr uint16_t rgb(int r,int g,int b) {
 return static_cast<uint16_t>(((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3));
}
struct Palette {
 uint16_t body,eye,ink;
 uint16_t working,blocked,done;
 uint16_t request,requestPeak;  // Pulsing input-request field, from..to.
 // Thin stroke around the face, drawn only in light mode, where the face is
 // the same off-white as an idle screen (or the widget's sidebar).
 uint16_t outline;
};
// Black face, white eyes, distinct saturated status colors. Needs-input
// blends toward the task list's red.
constexpr Palette kDark={
 0x0000,rgb(236,244,255),0x0000,
 rgb(0x3B,0x82,0xF6),rgb(0xF7,0x76,0x8E),rgb(0x22,0xC5,0x5E),
 request_scene::kYellow,request_scene::kOrange,
 0x0000};
// Off-white face, dark gray eyes and numerals, paler status colors.
constexpr Palette kLight={
 rgb(0xF4,0xF6,0xF9),rgb(0x3C,0x41,0x48),rgb(0x3C,0x41,0x48),
 rgb(0x93,0xC5,0xFD),rgb(0xFD,0xA4,0xAF),rgb(0x86,0xEF,0xAC),
 rgb(0xFE,0xF0,0x8A),rgb(0xFC,0xD3,0x4D),
 rgb(0xB8,0xBF,0xC9)};
inline const Palette& get(bool light) { return light?kLight:kDark; }
} // namespace avatar_palette
