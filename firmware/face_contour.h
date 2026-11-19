#pragma once

#include <math.h>

namespace face_contour {

// Analytic critically damped spring: a new random destination changes
// acceleration, not the current position or velocity. No per-frame jitter.
inline void spring(float& position, float& velocity, float target, float seconds) {
  const float omega = 2.6f;
  const float delta = position - target;
  const float step = (velocity + omega * delta) * seconds;
  const float decay = expf(-omega * seconds);
  position = target + (delta + step) * decay;
  velocity = (velocity - omega * step) * decay;
  if (position > 3 || position < -3) {
    position = fmaxf(-3, fminf(3, position));
    velocity = 0;
  }
}

struct BodySize { float x, y; };
inline BodySize bodySize(float baseX, float baseY, float eyeWidth, float eyeHeight,
                         float awakeWidth, float awakeHeight) {
  const float xScale = fmaxf(0.6f, fminf(1.12f, eyeWidth / awakeWidth));
  const float yScale = fmaxf(0.25f, fminf(1.20f, eyeHeight / awakeHeight));
  return {baseX * xScale, baseY * yScale};
}

// Low-frequency harmonics deform the two sides independently. Eased random
// drift plus continuous waves keeps each coefficient within +/-4.5px.
struct Shape {
  float weights[4] = {};
};

// Protect the count/eye band while making the upper and lower outline visibly
// fluid. Smoothstep avoids a kink where the stronger motion begins.
inline float edgeStrength(float distanceFromCenter) {
  const float t = fmaxf(0, fminf(1, (fabsf(distanceFromCenter) - 20) / 35));
  return 0.15f + 0.85f * t * t * (3 - 2 * t);
}

inline float displacement(const Shape& shape, float angle) {
  return shape.weights[0] * sinf(2 * angle) +
         shape.weights[1] * cosf(2 * angle) +
         shape.weights[2] * sinf(3 * angle) +
         shape.weights[3] * cosf(3 * angle);
}

inline bool bounds(const Shape& shape, float centerX, float centerY,
                   float radiusX, float radiusY, float y, float amount,
                   float& left, float& right) {
  if (radiusX <= 0 || radiusY <= 0) return false;
  const float normalizedY = (y - centerY) / radiusY;
  if (fabsf(normalizedY) >= 1) return false;
  const float angle = asinf(normalizedY);
  const float halfWidth = radiusX * sqrtf(1 - normalizedY * normalizedY);
  left = centerX - halfWidth + amount * displacement(shape, 3.14159265f - angle);
  right = centerX + halfWidth + amount * displacement(shape, angle);
  return right > left;
}

inline float coverage(float left, float right, int x) {
  const float start = fmaxf(left, static_cast<float>(x));
  const float end = fminf(right, static_cast<float>(x + 1));
  return fmaxf(0, fminf(1, end - start));
}

}  // namespace face_contour
