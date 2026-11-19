// Run: g++ -std=c++11 -Wall -Wextra -Werror tests/test_face_contour.cpp -o /tmp/stick-s3-contour-test && /tmp/stick-s3-contour-test
#include <assert.h>
#include <math.h>
#include <initializer_list>
#include <limits.h>
#include <string.h>

#include "../firmware/face_contour.h"
#include "../firmware/roman_counts.h"
#include "../firmware/count_raster.h"
#include "../firmware/display_fonts.h"

int main() {
  using namespace face_contour;
  struct Example { int count; const char* label; };
  const Example examples[] = {
      {-1, ""}, {0, ""}, {1, "I"}, {2, "II"}, {3, "III"}, {4, "IV"},
      {8, "VIII"}, {9, "IX"}, {10, "X"}, {40, "XL"}, {49, "XLIX"},
      {50, "L"}, {88, "LXXXVIII"}, {90, "XC"}, {99, "XCIX"},
      {100, "XCIX+"}, {INT_MAX, "XCIX+"}};
  for (const auto& example : examples) {
    char label[10];
    roman_counts::format(example.count, label);
    assert(strcmp(label, example.label) == 0);
  }
  for (int count = 1; count <= 99; ++count) {
    char label[10];
    roman_counts::format(count, label);
    assert(strlen(label) >= 1 && strlen(label) <= 8);
    assert(strspn(label, "IVXLC") == strlen(label));
  }
  const count_raster::Font* fonts[] = {
      &kBadgeFont, &kMediumBadgeFont, &kSmallBadgeFont, &kTinyBadgeFont, &kCompactBadgeFont};
  for (const auto* font : fonts) {
    for (int i = 0; i < font->glyphCount; ++i) {
      const auto& glyph = font->glyphs[i];
      bool solid = false;
      for (int p = 0; p < glyph.width * glyph.height; ++p) solid |= glyph.alpha[p] == 255;
      assert(solid);  // Every bold stroke has filled, not outlined, interiors.
    }
    assert(count_raster::glyph(*font, 'Z') == nullptr);
    const auto* numeral = count_raster::glyph(*font, 'I');
    assert(numeral);
    assert(numeral->alpha[(numeral->height / 2) * numeral->width + numeral->width / 2] == 255);
  }
  for (int count = 1; count <= 100; ++count) {
    char label[10]; roman_counts::format(count, label);
    bool fits = false;
    for (const auto* font : fonts) fits |= count_raster::width(*font, label) <= 56;
    assert(fits);
  }

  float position = 0, velocity = 0;
  spring(position, velocity, 3, 0);
  assert(position == 0 && velocity == 0);
  spring(position, velocity, 3, 0.033f);
  assert(position > 0 && position < 0.05f);
  for (int step = 0; step < 1800; ++step) {
    spring(position, velocity, step / 60 % 2 ? -3 : 3, 0.033f);
    assert(position >= -3 && position <= 3);
    assert(isfinite(position) && isfinite(velocity));
  }
  const auto awake = bodySize(152, 180, 50, 56, 50, 56);
  const auto asleep = bodySize(152, 180, 32.5f, 4, 50, 56);
  const auto surprised = bodySize(152, 180, 53, 64.4f, 50, 56);
  assert(awake.x == 152 && awake.y == 180);
  assert(asleep.x < awake.x && asleep.y < awake.y);
  assert(surprised.x > awake.x && surprised.y > awake.y);

  Shape shape;
  float left, right;
  assert(bounds(shape, 120, 67.5f, 72, 96, 67.5f, 1, left, right));
  assert(left == 48 && right == 192);
  assert(!bounds(shape, 120, 67.5f, 72, 0, 67.5f, 1, left, right));
  assert(!bounds(shape, 120, 67.5f, 72, 96, 163.5f, 1, left, right));
  assert(coverage(2.25f, 5.75f, 2) == 0.75f);
  assert(coverage(2.25f, 5.75f, 3) == 1);
  assert(coverage(2.25f, 5.75f, 5) == 0.75f);
  assert(coverage(2.25f, 5.75f, 6) == 0);
  assert(fabsf(coverage(2.25f, 2.6f, 2) - 0.35f) < 0.00001f);

  assert(edgeStrength(0) == 0.15f);
  assert(edgeStrength(20) == 0.15f);
  assert(edgeStrength(55) == 1);
  assert(edgeStrength(-55) == 1);

  // All extreme coefficient combinations, screen rows, compact/wide faces,
  // and both horizontal shifts: boundaries remain ordered and drift bounded.
  for (int mask = 0; mask < 16; ++mask) {
    for (int i = 0; i < 4; ++i) shape.weights[i] = mask & (1 << i) ? 4.5f : -4.5f;
    for (float radiusX : {50.0f, 72.0f, 96.0f, 140.0f}) {
      const float radiusY = fmaxf(96, radiusX * 1.32f);
      for (float centerX : {85.0f, 120.0f, 155.0f}) {
        for (int y = 0; y < 135; ++y) {
          const float row = y + 0.5f;
          assert(bounds(shape, centerX, 67.5f, radiusX, radiusY, row, 1, left, right));
          const float halfWidth = radiusX * sqrtf(1 - powf((row - 67.5f) / radiusY, 2));
          assert(fabsf(left - (centerX - halfWidth)) <= 18.0001f);
          assert(fabsf(right - (centerX + halfWidth)) <= 18.0001f);
          assert(bounds(shape, centerX, 67.5f, radiusX, radiusY, row,
                        edgeStrength(row - 67.5f), left, right));
          if (fabsf(row - 67.5f) <= 20) {
            assert(fabsf(left - (centerX - halfWidth)) <= 2.7001f);
            assert(fabsf(right - (centerX + halfWidth)) <= 2.7001f);
          }
          for (int x = 0; x < 240; ++x) {
            const float alpha = coverage(left, right, x);
            assert(alpha >= 0 && alpha <= 1);
          }
          // Disabling deformation restores the exact oval, regardless of shape.
          assert(bounds(shape, centerX, 67.5f, radiusX, radiusY, row, 0, left, right));
          assert(fabsf(left - (centerX - halfWidth)) < 0.0001f);
          assert(fabsf(right - (centerX + halfWidth)) < 0.0001f);
        }
      }
    }
    // Bottom-corner layout: protect even wide count labels (up to 56px),
    // centered in 72px strips, throughout their glyph-height band.
    for (int y = 104; y <= 128; ++y) {
      if (bounds(shape, 120, -65, 152, 180, y + 0.5f,
                 edgeStrength(y + 0.5f - 116), left, right)) {
        assert(left > 64 && right < 176);
      }
    }
  }
}
