// native_scan::transpose places every landscape pixel where M5GFX's display
// rotation 1/3 would (LovyanGFX Panel_HasBuffer::_rotate_pos), so presenting
// at rotation 0 in the panel's scan order shows the same picture.
#include <stdio.h>
#include <stdint.h>
#include <utility>
#include "../firmware/native_scan.h"

namespace {
int failures = 0;
void check(bool ok, const char* what, int r, int x, int y) {
  if (!ok && failures++ < 10) printf("FAIL %s rotation %d at %d,%d\n", what, r, x, y);
}
// LovyanGFX's own mapping, transcribed: logical (x,y) at internal rotation r
// to the panel_width x panel_height buffer.
void rotatePos(int r, int pw, int ph, int& x, int& y) {
  if (r % 2 != 0) std::swap(x, y);
  // Mirror sets, as LovyanGFX's bit masks: x for rotations 1,2,6,7; y for 2,3,4,7.
  if (r == 1 || r == 2 || r == 6 || r == 7) x = pw - 1 - x;
  if (r == 2 || r == 3 || r == 4 || r == 7) y = ph - 1 - y;
}
}  // namespace

int main() {
  constexpr int W = 240, H = 135;  // landscape; the panel is H wide, W tall
  static uint16_t src[W * H], dst[W * H];
  for (int i = 0; i < W * H; ++i) src[i] = static_cast<uint16_t>(i * 2654435761u >> 7);
  for (int r : {1, 3}) {
    for (int i = 0; i < W * H; ++i) dst[i] = 0xdead;
    native_scan::transpose(src, W, H, dst, r);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        int nx = x, ny = y;
        rotatePos(r, H, W, nx, ny);
        check(nx >= 0 && nx < H && ny >= 0 && ny < W, "in range", r, x, y);
        check(dst[ny * H + nx] == src[y * W + x], "pixel", r, x, y);
      }
  }
  if (failures) { printf("%d failures\n", failures); return 1; }
  printf("native_scan: rotations 1 and 3 match LovyanGFX\n");
  return 0;
}
