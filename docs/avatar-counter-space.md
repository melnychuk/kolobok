# Numeral space — prepared firmware, installed header backend

Side labels and bottom labels use different space policies. Task counts, Roman
conversion, foreground layering and status sides are unchanged.

## Avatar accommodation: side labels only

`firmware/counter_space.h` is shared by firmware and native/header rendering.
Extra horizontal shift follows side-fitted glyph width: zero through 32px,
eased toward at most 10px at 56px. A left label pushes right and vice versa.
Five-or-more-character side labels (`XVIII`, `LXXXVIII`, capped `XCIX+`) target
94% body radii, eye dimensions and eye gap; shorter labels retain full size.

Shift uses the existing 150ms exponential time constant. Shrink begins after
shift is 65% complete, ramps in smoothly, and eases with the existing 210ms
reflow constant. These are easing constants, not total animation durations.
Request, listening, offline and sleep softly restore neutral layout.

**Bottom labels leave the avatar full-size and centred**, even with unequal or
long labels. Shift/shrink strength fades with `1-corners` throughout reflow.
This supersedes the initial paired half-bias/94%-size behavior in `bcd14d0`.
The existing unused-side body stretch and lower-envelope expansion remain.
`Scene::counterShift` / `counterScale` default to 0/1, preserving kernel oracles.
No bitmap scaling, rotation, extra framebuffer, new per-pixel fields, physics
clocks or task events. Authored eye/body poses retain their 90ms overlap. The
optional default-off mouth follows horizontal shift; no mouth is enabled.

## Numeral fitting: use the bottom width

`firmware/counter_labels.h` caches glyph widths/font decisions when counts or
sorting snapshots change. Firmware and native/header composition use the same
font indices, contact bounds and numeral positions.

- Side-mounted labels keep their existing **56px** fitting limit.
- Bottom labels start at the normal largest font (23px cap height), grow inward
  from 8px outer margins and share 214px total width plus a 10px minimum gap.
  Narrow labels retain their familiar corner positions (at least 24px inset).
- Only reduce fonts when their actual combined normal-font widths exceed that
  budget: reduce the wider label first; equal widths stay balanced. No glyph
  stretching or bitmap resampling. `XVIII / XVIII` is **84px + 84px**, both
  normal-size. Even `I / LXXXVIII` is **12px + 152px**, both normal-size.
- Promote through existing fonts during reflow; anchor growth inward so outlines
  and bounded +/-3px contact travel stay on screen. Body region/unused-side
  reclamation still use SIDE widths, never the expanded bottom numerals.
- Completion sorting jointly reserves OLD/NEW widths/fonts before pickup. The
  gathered pair is centred by its full outer bounds, not by the mean of its
  centres: unequal wide labels remain on screen, separated, and stationary
  during the apex snapshot swap. Authored flight/gaze/body clocks are unchanged.
- Solid black interiors and exact backdrop-colored two-pixel outlines remain;
  numerals render above the face. Larger fonts intentionally increase numeral
  pixel work versus the old narrow-strip fitting; no device cost is claimed.

## Validation and synthetic review

- All **25 native C++ suites**, **76 Python tests**, and **16 actual QEMU
  peripheral/FIFO/CRC/stock-IDF guest cases** pass.
- `test_counter_labels.cpp`: all count pairs 0..120; mirrored fitting; no font
  reduction when the normal pair fits; full-width space sharing; reflow outline/
  contact bounds; joint sorting reservations, separation, on-screen bounds and
  invariant apex centres across old/new snapshots; full compositor integration.
- `test_counter_space.cpp`: count pairs 0..150; five-character boundary; monotone
  easing at 20/33/50ms; restoration; full-size centred pairs; unused-edge fill
  across every thinking/completion clip at 94% size. Existing sorting and frozen
  raster/contact/shoulder/eye suites still pass. New source/app hashes and old
  binaries paired with the new source are rejected by historical timing gates.
- ASan/UBSan and opt-in mouth builds of the new layout regression pass.
- Normal **O2 ESP32 build** passes: 753,223 program bytes, 51,496 global bytes.
- Focused long-label gallery, side/paired reflow GIFs and complete acting/event/
  mouth-disabled/layout-stress gallery were generated and sampled visually.
  These are synthetic native images, not physical smoothness or FPS evidence.

Reproduce the focused gallery (C++ compiler and project Pillow venv required):

```sh
.venv/bin/python scripts/preview-counter-space.py
```

## Performance limits

Physical appearance and source-matched timing remain pending. Historical occupancy envelopes, synthetic FIFO
costs and emulator CPU/USB/SPI/DMA clocks are unchanged. Full-production GDMA/
board-init/PSRAM guest execution remains a separate unrun gate.
