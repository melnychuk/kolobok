# Side-reach shoulder junction correction

> Superseded by the [soft-body outline](avatar-soft-body.md): the capsule hand,
> skin fillet and convex envelopes described here are gone, and
> `tests/test_shoulder_join.cpp` became `tests/test_check_swell.cpp`.

## User feedback and reproduction

On-device smoothness after the continuity work (`ec7c99e`) was good, but a
small concave inconsistency appeared on the
completed-only edge as the hand retreats. This is qualitative physical feedback,
not a measured FPS/cadence or full DMA/button/microphone validation. The same
artifact showed on the right edge for active-only tasks; the
correction and regressions cover both mirrored sides.

A native live-header replay with constant synthetic completed-only aggregates
reproduces an inward crease at the circle/body union. The resting base edge is
smooth: the hard union switches between the capsule and body with different
slopes. During retreat this produces ~1.9px inward deviation from a 24px chord
in the sampled replay, before disappearing once the hand is fully gone.

## Correction

- Keep the base mask, hand capsule, contact targets, eyes, body physics, counts
  and all authored clocks unchanged.
- Prepare a side-only skin fillet using two O(135-row) convex envelopes. Subtract
  the base envelope from the hand/body envelope so existing organic inflections
  remain, rather than flattening the whole character. The correction only adds
  black coverage; it never clips the hand back to the old oval.
- Every added row attaches to opaque base coverage. Eyes/mouth still clip to the
  original base mask, never the fillet. Numerals/outlines remain foreground.
- Fade the fillet through existing reflow/alert/sleep weights; paired, request,
  sleeping and zero-strength endpoints remain unchanged. Mirrored active-only
  reaches receive the same junction correction.
- Row bounds, raster candidate bounds and bottom contact bounds include the
  added skin. Single-counter contact skips its mathematically unused vertical
  component at exactly `corners == 0`, avoiding a per-column fillet scan there.
  Every nonzero reflow still computes both components. No new motion clock,
  animation delay, pixel SDF or fixed-point approximation is introduced.

## Verification

`tests/test_shoulder_join.cpp` checks 58,752 frames across every applicable
thinking/done clip, both sides, normal/affectionate presses, two strip widths
and 20/30/60fps sampling. Its 3,200 retreat frames reduce maximum inward chord
error from 6.867px (raw hard union) to 0.044px, including natural base variation.
It checks expansion-only coverage, unchanged eye clipping/base arrays,
whole-mask attachment/AA spans, bottom contact bounds and exact return to the
original mask after the hand disappears. These are native geometry metrics,
not physical timing evidence.

All 23 native suites and 76 Python tests pass. Active LSP checks on six affected
C++ modules/tests are clean. The normal ESP32 O2 build compiles at 750,279
program bytes / 51,352 static bytes. The unchanged historical occupancy selector
still rejects this changed source; no new physical envelope/FPS is inferred.

`join-before-after.gif` shows the old outline on the left and the correction on
the right. Sequence frames were reviewed.

## Status

Committed as `92bd3a8`, covering BOTH completed-only left and active-only right
shoulder retreats. Physical appearance/smoothness of this correction is not yet
validated.
