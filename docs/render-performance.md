# StickS3 renderer performance

## Numeral accommodation — unmeasured working-tree revision

[Counter space](avatar-counter-space.md) adds up to 10px shift and six-percent
geometric shrink for five-character SIDE labels only. Bottom numerals grow
inward at normal font size, reducing only when the actual pair collides; the
avatar stays full-size and centred. Width/font decisions are cached on count
changes. Larger bottom glyphs increase numeral raster workload, not raster
algorithms, frame pacing, SPI/USB/DMA clocks or FIFO fixture costs. Historical
FPS/envelopes do not apply.

## Shoulder-junction correction

Physical smoothness after the continuity work was good, with a small inward
crease during completed-only left and active-only right hand retreats. The
[shoulder correction](avatar-shoulder-join.md) adds a row-prepared skin
fillet and skips unused vertical contact only at exactly zero corner reflow. All
nonzero reflows retain both contact components. Base masks/eye clipping,
acting/frame/transport clocks and historical occupancy costs are unchanged. The
new source remains deliberately uncalibrated; native geometry metrics and
compilation must not become a physical FPS claim. The [soft-body
outline](avatar-soft-body.md) later removed this fillet and the hand (a check is
now a swell in the edges themselves) and doubled the lower-rim nodes to 64: host
update +0.4µs and mask +0.2µs per frame, ESP32 cost not yet measured.

## Continuity director — unmeasured revision (2026-11-04)

The shared mode/transition controller and doze/wake/recovery acting are
described in [avatar-continuity.md](avatar-continuity.md). Native transition
scenarios test 20/30/60fps sampling, interruption and wrap, not physical timing.
Existing 33ms pacing, SPI/DMA/USB clocks, packet schema and historical occupancy
costs are unchanged. Changed source/binary/layout combinations remain outside
every measured envelope; `test_avatar_director.cpp` explicitly checks rejection.
All 16 functional QEMU cases pass. New eye controls retain legacy ordered parity
and have a separate +3% transition pixel-work guard, not a CPU-cost calibration.
No new physical or full-production-QEMU/GDMA timing is claimed.

## Research and first measured optimization — 2026-11-01

Primary online sources inspected:

- [Espressif ESP32-S3 speed optimization, IDF 5.5.1](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/performance/speed.html): target hot paths, avoid software double precision, consider single-precision versus integer work, targeted IRAM, cache tradeoffs and performance optimization flags. These recommendations are not guarantees of speedup.
- [M5GFX 0.2.32 sprite API](https://github.com/m5stack/M5GFX/blob/0.2.32/src/lgfx/v1/LGFX_Sprite.hpp) and [implementation](https://github.com/m5stack/M5GFX/blob/0.2.32/src/lgfx/v1/LGFX_Sprite.inl): `getBuffer()`, packed 16-bit stride, rotation handling and raw pixel storage.
- [M5GFX 0.2.32 pixel API](https://github.com/m5stack/M5GFX/blob/0.2.32/src/lgfx/v1/LGFXBase.hpp): `readPixel()` constructs a pixel-copy conversion and calls `readRect()` even for one RGB565 pixel. Avoid that generic path in the hot loop; it is not a hardware framebuffer read requirement.

Installed library versions, SPI rate, CPU frequency, animation clocks and voice
behavior were not changed by these optimizations.

## Applied tricks

1. **Classify before calculating.** Face-mask pixels fully inside/outside return exact 1/0. Only the antialias fringe uses the old min/max area-intersection calculation. Do not use a pixel-center approximation or freeze a counter-safe border.
2. **Hoist invariants.** Happy-closure weight and lid asymmetry are computed once per eye instead of once per candidate pixel.
3. **Avoid repeated libm calls.** Finite eye geometry uses comparison clamps/maxima. The interior rounded-box distance bypasses `sqrt(0)` only; nonzero square roots remain exact. No approximate reciprocal, coarse lookup or global `-ffast-math` was introduced.
4. **Use checked direct RGB565 blending.** `framebuffer_raster::View` clips coordinates, has explicit swapped storage, blends only partial coverage, and writes opaque pixels without reading their old values. Firmware enables it only for the known unrotated 240×135 packed 16-bit sprite; other layouts retain M5GFX API fallback. Rendering is still serialized with synchronous display completion, not concurrent writes into DMA-owned memory.
5. **Reuse the actual backdrop.** Numeral outlines sample the same four RGB565 dither rows used for the current frame background instead of recalculating the gradient per outline pixel. Cache refresh happens every frame, including presence/coral transitions. Solid black interiors and final foreground draw order remain unchanged.
6. **Exploit a proven numeric range.** Gradient channels are clamped nonnegative before truncation, making an integer cast exactly equivalent to `floorf()`. Comparison clamps replace repeated min/max calls without changing quantized output.

The cached backdrop moves 1,920 bytes from a temporary stack array to persistent storage. Total globals are 34,096 bytes in the normal build (10%); no extra framebuffer or asset was allocated.

## Device measurements

See [profiling boundaries and baseline](frame-profiling.md). Same identified StickS3, 240MHz CPU, 40MHz SPI, 33ms target. No synthetic live task states or audio collection. These are wall-time occupancy, not isolated CPU cycles.

| Metric | Baseline median | Optimized median |
|---|---:|---:|
| Frame-start interval | 209.10ms | 73.90ms |
| Background | 9.61ms | 5.19ms |
| Mask construction | 13.32ms | 13.35ms |
| Face drawing | 68.55ms | 12.59ms |
| Eyes | 82.97ms | 20.40ms |
| Numerals | 16.76ms | 5.21ms |
| Full rendering + display call | 204.76ms | 69.58ms |
| Display call | 13.061ms | 13.067ms |

Mean cadence improves **4.79 → 13.58fps (~2.83×)**. Optimized full-frame p95 is 71.95ms; start p95 is 76.01ms. This is still renderer-bound and still misses the 33ms target; do not claim smooth 30fps or optical LCD behavior.

Baseline build `963823bf` sampled 313 frames; final optimized build `4c60aaf3`
sampled 885 with a completed capture footer. An intermediate build `a4ee5f3e`
measured 12.71fps before the gradient arithmetic optimization.

Both baseline and final captures contain startup telemetry drops (10 and 30); no host drops/invalid packets or voice exclusions. Original report gates therefore keep `calibration_ready=false`. They establish a major diagnostic improvement but are not approved loss-free calibration or a controlled identical-performance-clip A/B experiment. Live snapshots and no-repeat clip selection may differ. Concurrent task-cue/acting changes in the working tree remain outside this performance commit; manifests pin the actual firmware sources tested.

## Verification and remaining tricks

- Seven native suites passed, plus 37 Python tests.
- Frozen pre-optimization eye geometry plus original mask formula: **20,610,924 exact coverage comparisons** passed, covering all 21 authored clips.
- Whole native timelines for 21 clips × seven layouts/events: **17,508 RGB frames pixel-identical**, including the gradient change. Preview parity does not itself verify M5GFX pixel transfer; the direct path also relies on the inspected installed byte-order/stride contract and portable view tests.

Next measured experiments, in priority order:

1. Row/span classification to avoid testing every opaque face pixel; preserve multiple free-flap runs and exact edge coverage. Mask construction also still spends ~13ms on noise, powers and spring sampling.
2. Tighter conservative eye candidate bounds and skip unused normalized-x/lid/cheek work. Closed happy arches need their full-height bound; a simple closed-pill rectangle would be wrong.
3. Inspect Xtensa disassembly/costs before replacing division or sqrt with reciprocals/tables. Such approximations need explicit AA error limits and pixel tests; they are not automatically appearance-preserving.
4. Benchmark targeted inlining, sketch `-O2`, and hot-function IRAM separately. Include optimization flags in build provenance; check code size, internal RAM and real timing tails. Do not change installed core/library flags globally or assume host optimization results apply to Xtensa.
5. Consider fixed-point or cached coverage only where geometry permits it. Dynamic lids, tilt, contour physics and overlapping labels invalidate naive whole-sprite caching.
6. DMA/double buffering could overlap work and the ~13ms transfer after memory/race/voice validation, but even perfect transfer overlap cannot make today's ~56ms render-side occupancy fit a 33ms target. Dirty rectangles are also limited by continuously moving edges and changing background presence.

## Round 2: exact spans/bounds and compiler recipe

The next structural pass adds:

- `Mask::raster()` recomputes 16-column lower-rim minima, skips provably opaque blocks, and outputs multiple horizontal runs plus exact AA fringe pixels. No whole counter band is frozen; local holes/flaps still split runs. A scalar oracle tests all emitted pixels, including manually edited masks.
- Eye candidate bounds rotate the union of the rounded box and full happy-closed arch. They intersect the legacy search square, preserving its clipping behavior. Unused normalized-x work is skipped only when no lid/cheek/arch needs it; division remains unchanged otherwise.
- Tests cover extreme aspect ratios, unilateral closures, rotations from -90 to +90 degrees, and all authored clips: **30,330,924 exact comparisons**. The whole native gallery again yields **17,508 pixel-identical frames** against round 1.

Two physical runs used the **same firmware source** and different explicit recipe fingerprints:

| Measurement (median unless noted) | `-Os`, `a4978b47` | `-O2`, `9d40fa19` |
|---|---:|---:|
| Average cadence | 17.51fps | 19.82fps |
| Render-side occupancy | 39.66ms | 32.89ms |
| Mask construction | 10.55ms | 8.28ms |
| Face spans | 2.00ms | 1.80ms |
| Eyes | 8.80ms | 7.33ms |
| Background | 5.20ms | 4.34ms |
| Numerals | 5.20ms | 3.51ms |
| Total render + display call | 52.73ms | 45.95ms |
| Total p95 | 55.87ms | 48.68ms |
| Display call | 13.064ms | 13.065ms |

**Workload changed:** both round-2 windows are working/single-counter vertical layout (`flags-1`), including the mouth. Earlier round-1 measurements were predominantly wide (`flags-2`). Do not advertise 13.58→19.82 as a controlled same-layout speedup. The same-source compiler comparison here improves cadence by ~13.2%, though live no-repeat clips are not identical scripted A/B inputs.

Each collector ran 65 seconds with an explicit **5-second timing-only warmup**. The 1,050/1,190 saved frames have completed footers, contiguous sequences, unchanged clocks/period, no in-window firmware loss, host loss/invalid packets or voice exclusions, and observer maxima 119/126us. Cumulative startup counters are retained, not reset or edited; the report subtracts the first received counter to evaluate loss inside the observed window. Both reports pass `calibration_ready=true` for per-mode wall-occupancy candidates only. This does not calibrate Xtensa cycles, LCD photons or USB grants.

It uses 673,951 program bytes versus 624,143 at Os (~49.8KB more, still 20% of
flash); globals differ by eight bytes. No installed platform/library files were
edited. The Arduino recipe puts `compiler.cpp.extra_flags` **before**
optimization flags, so adding `-O2` there would be overridden by `-Os`. Use the
actual recipe property:

```sh
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 \
  --build-property compiler.optimization_flags=-O2 \
  --build-path /tmp/stick-s3-o2-normal firmware
.venv/bin/python scripts/build-frame-profile.py --optimization O2
```

The profiler manifest/ID now includes the optimization recipe, and old Os manifests remain readable. Still below 30fps: O2 render-side p95 is 35.615ms plus ~13ms display occupancy. Next experiments should target remaining mask/noise/power work and vertical mouth rasterization, with targeted inlining/IRAM measured separately. No reciprocal approximation, global fast-math, asynchronous DMA, SPI overclock or acting-clock change was added.

## Follow-up review after round 2

The clean O2 report (`9d40fa19`, working/vertical) shows mouth median 7,211us,
p95 7,910us and max 9,436us. The wide round-1 report's near-zero mouth cost is a
layout visibility gate, not a mouth speedup. Remaining O2 median costs are mask
8,275us, eyes 7,331us, background 4,337us and numerals 3,511us. Face span drawing
is now only 1,801us: prioritize the remaining work rather than revisiting that
already-successful optimization.

Next experiments, with one change per source-matched capture:

1. **Mouth segment invariants:** the 16-segment distance loop recomputes endpoint
   deltas and squared lengths for every candidate pixel. Precompute those per
   contour while retaining division order/zero-length handling; hoist opacity.
   Replace finite-domain min/max calls only with exact comparisons. Segment AA
   bounds can reject provably distant segments, but must preserve the pinched
   crease, thick stroke, oval and delayed smile. Keep a frozen scalar mouth
   oracle and all-clip/tilt/opacity tests before claiming pixel identity.
2. **Mask zero-contribution paths:** settled vertical layout has `corners == 0`,
   yet lower-rim drift still evaluates noise before multiplying by zero. Skip
   that field only at exact zero, with finite-input and pixel-parity tests; do
   not threshold the continuous reflow. Similarly investigate unused side-flow
   paths. Hoist geometry invariants before considering approximate powers/noise.
3. **Background and count reuse:** cache the four dither rows only when every
   background dependency is unchanged (presence, coral, portrait and endpoints).
   Still refresh during transitions and sample the current rows for outlines.
   Cache numeral masks/layout by label and font, not final screen pixels: face
   overlap and the count hop keep final compositing dynamic.
4. **Then assess transfer overlap:** serialized render-side median is 32,886us,
   plus 13,065us display time and ~4,135us outside-render work. The 33ms busy
   target leaves ~20ms render budget, or nearer 16ms after that loop overhead.
   DMA overlap alone still has insufficient p95 headroom (render p95 35,615us),
   and needs independent ownership/race/voice and emulator GDMA validation.

Do not change authored holds or shorten saccades to hide rendering costs. At the
latest start p95 of ~53ms, 90ms preparation gets ~1.7 intervals and 180ms upward
travel ~3.4; test short beats with measured-cadence previews instead of assuming
30fps. Re-measure wide, request, completion and sleep when real workloads permit;
vertical-only occupancy must not become a universal emulator preset.

The emulator regression integration retains historical Os/O2 envelopes in
`experiments/qemu-sticks3/measured_frame_envelopes.h`, shared transport budget/
late-start checks, and synthetic packet replays through actual QEMU CDC/FIFO.
These do not calibrate QEMU CPU cycles or optical scanout. Every subsequent
performance change must update those evidence/regression boundaries. No additional renderer optimization or
physical speed measurement was performed by this follow-up review.

## Round 3: prepared mouth/mask optimization (not physically measured)

`firmware/mouth_raster.h` now precomputes each of the 16 contour segments' endpoint
deltas and squared lengths once per contour. It retains the original projection
division order and degenerate-segment guard, hoists opacity, and replaces repeated
finite-domain libm clamps/minima with exact comparisons. No reciprocal, reduced
segment count, approximate sqrt, stroke change or acting-clock change was added.
The extra segment workspace is 320 stack bytes, with no new framebuffer/globals.

`firmware/avatar_scene.h` skips lower-rim noise at **exactly zero** corner reflow
and side-edge noise at exactly zero side flow. Tiny nonzero transitions still
execute the original formula; no epsilon cutoff or silhouette threshold exists.
Noise/powers/physics otherwise remain unchanged.

Frozen scalar oracles in `tests/reference_mouth_raster.h` and
`tests/reference_avatar_scene.h` validate **49,012,500 exact pixel/coordinate
comparisons**, across all authored clips, arbitrary contours/opacity/tilt,
degenerate segments, offscreen clipping, physics, zero flow and tiny nonzero
reflow. Full native RGB timelines remain **17,508 frames pixel-identical** to
round 2 (21 clips x seven layouts/events). Keep the oracles frozen.

A single host O2 microbenchmark of 2,000 iterations measured mouth 44.10ms
reference versus 14.67ms optimized, and vertical mask 58.76ms versus 41.44ms.
These are host workload observations, **not ESP32 stage costs, speedups or FPS**.
Reproduce with the strict native build of `tests/test_round3_raster.cpp` and
`--benchmark`; real timing needs a new source-matched device capture.

Normal and diagnostic O2 ESP32 builds compile. Do not claim the historical
19.82fps as this changed renderer's measured speed.

The emulator's `findEnvelope` now rejects mismatching source/binary hashes,
recipe or layout (including this unmeasured revision); codec/QEMU replay tests
exercise the gate. Older Os/O2 envelopes remain historical inputs. Default CPU,
SPI and USB clocks are unchanged; full-production GDMA/board-init/PSRAM emulation
is still incomplete. Refresh the envelope only after an authorized clean capture,
not by multiplying old measurements by host benchmark ratios.

## Fresh round-3 baseline and cache pass — 2026-11-01

Two authorized 65-second captures (5-second warmup, ~60-second observed window)
now measure the mouth/mask fixes and the subsequent caching pass.
Both are working/vertical (`flags-1`), 240MHz CPU, 40MHz SPI and the unchanged
33ms target, with complete footers, contiguous sequences, no in-window losses,
invalid packets or voice exclusions. Cumulative startup drops remain visible
(65/69); both reports pass per-mode occupancy calibration gates.

| Measurement | Fresh baseline `7dfcc87d` | Cached `2bf53c52` |
|---|---:|---:|
| Observed frames | 1,410 | 1,535 |
| Mean cadence | 23.50fps | 25.58fps |
| Start interval median / p95 | 43.00 / 45.00ms | 39.00 / 41.00ms |
| Render-side median / p95 | 25.58 / 28.17ms | 22.02 / 24.08ms |
| Background median | 4.35ms | 2.43ms |
| Mask + contact median | 5.68ms | 5.73ms |
| Eyes median | 7.38ms | 8.35ms |
| Mouth median | 2.78ms | 2.78ms |
| Numerals median / p95 | 2.37 / 3.52ms | 0.47 / 0.47ms |
| Full render/display median / p95 | 38.65 / 41.25ms | 35.09 / 37.16ms |
| Display median | 13.075ms | 13.074ms |

These are live sequential captures, not a controlled same-performance-clip A/B.
Labels/clip selection differed and a concurrent single-counter contact update
also landed in the optimized snapshot; it changes work charged to mask/contact.
The caching pass does not itself change eye/acting geometry. Do not attribute
all cadence variation to caching or apply these vertical results to wide,
request, sleep or listening. Still below 30fps; most frames miss the 33ms target.

`status_background::RowCache` regenerates the existing four dither rows only when
an exact color/presence/portrait key changes. The remaining ~2.43ms background
stage still repopulates the full sprite every frame; screen pixels are not cached
across face movement. Coral, endpoint and presence transitions remain exact.

`count_raster::Cache` lazily stores per-glyph two-pixel outline coverage for one
immutable generated font per badge and caches label glyph indices/width. It keeps
all outline callbacks before all interior callbacks, retaining every overlapping
AA blend in original glyph order. It does **not** merge outlines into a max-alpha
label mask, cache final colors, or freeze pop/contact positions. Fonts with over
six glyphs, padded glyphs over 32x32, or labels exceeding the 10-byte buffer fall
back to the scalar renderer without truncation. Font/label changes are cold misses;
steady-state capture timings do not benchmark every cold arrival/font change.

The added globals are 12,392 bytes, including 12,288 outline bytes; no extra full
framebuffer or heap allocation. Normal globals total 46,568 bytes (14% of static
RAM budget), normal program size 699,415 bytes (20%). The codec/profiling ABI,
voice controls, task meanings, physics and acting clocks are unchanged.

`tests/test_render_cache.cpp` passes **2,106,384 exact color/callback comparisons**,
including all five fonts, counts through XCIX+, moved/cropped labels, repeated
AA overlaps, unknown glyphs, fallback limits, exact/tiny-transition invalidation
and cache hits. Os/O2 and ASan/UBSan builds pass. Same frozen interaction snapshot
with scalar versus cached rendering yields **69,768 pixel-identical RGB frames**
(28 clips x 14 layouts/events). An initial comparison against an older interaction
snapshot differed; it was not a cache regression. Host-only 1,000-frame numeral
microbenchmark: scalar 26.08ms versus cached 1.25ms, not device FPS.

Emulator history retains both new per-mode manifests and p95 envelopes, alongside
older round-2 entries, with source/binary/mode/recipe gating. Effective pre-wire
p95 inputs are 28,288us and 24,199us. Shared transport and synthetic QEMU FIFO
replay tests remain occupancy/framing checks—not Xtensa CPU-cycle, USB-grant or
panel-scanout calibration. No default QEMU clocks were changed.

## Smaller eyes and exact eye-kernel pass — prepared, not physically timed

The requested visual adjustment applies `avatar_scene::kEyeHeightScale=.90f`
only at eye rendering: typical 56px height becomes 50.4px, with width, centers,
gap, authored pose/hold/blink clocks, body dimensions and mouth anchors unchanged.
The scale eases back to 1 during sleep; requests retain their 48px open-eye floor.
Happy arches retain their four-pixel stroke and real unilateral winks remain.
This visual change is intentional, not a claim of pixel parity with old-height eyes.

The subsequent **appearance-preserving** optimization hoists lid/cheek products
in their original arithmetic order and rejects fully lid/cheek-occluded pixels
before rounded-box distance/sqrt work. That rejection is enabled only when the
happy-arch blend is exactly zero: even a tiny nonzero blend can recover pixels.
At exactly zero rotation (`cos==1 && sin==0`), a bounded 128-column stack array
reuses the original normalized-x division across rows. Rotated/oversized eyes
retain scalar divisions; no reciprocal approximation, coordinate recurrence,
persistent eye cache, extra framebuffer, heap allocation or global cache was added.
The scratch array is 512 bytes; firmware stack/voice headroom still needs device
validation before extending it.

`tests/reference_round4_eye_raster.h` freezes the prior optimized eye kernel.
`tests/test_eye_pass.cpp` passes **14,644,546 exact ordered coverage callbacks**
under O2, Os and ASan/UBSan, across all 28 clips, old/new heights, request dimensions,
rotations, offscreen/overlapping eyes, unilateral closures, tiny happy blends and
column-cache capacity/fallback boundaries. The existing scalar eye oracle remains
unchanged. A frozen same-scene comparison yields **69,768 pixel-identical RGB
frames** at the new height (28 clips x 14 layouts/events). Concurrent poke/kiss
changes are held identical in both binaries; do not compare different snapshots.
An intermediate interaction test failed while that concurrent implementation and
its fixtures were being edited; the final interaction/poke suites now pass.
All 12 native suites, 55 Python tests and 16 actual QEMU FIFO/peripheral/stock-
driver tests pass. Normal/profiling Arduino and shared ESP-IDF probe cross-builds
pass. Final active LSP probes timed out and are inconclusive, not proof of clean;
the actual compilers/tests are the stronger evidence. Full renderer/GDMA guest
execution and physical button/microphone/stack behavior were not validated here.

One host-only mixed 2,800-frame run measured old-height/reference 83.54ms,
new-height/reference 75.94ms and new-height/optimized 68.59ms. This separates the
visual trim from the kernel pass, but is not ESP32 speedup or FPS evidence.
The last physical 25.58fps/8.35ms eye-stage result remains historical cache-pass
measurement and must not be applied to this source.

Thinking/done/request/ listening, mouth and numeral-stress sheets were
inspected. Both include concurrent poke work. No physical timing was performed
by this eye pass.

Emulator evidence explicitly records that prepared revision **outside** the
measured-envelope set. Codec tests reject both its actual source/binary pair and
old measured binaries combined with the changed source. A synthetic QEMU FIFO
fixture preserves build ID `b088706a` with unchanged placeholder costs; it is not
a timing calibration. Historical occupancy entries and QEMU CPU/SPI/USB clocks
remain unchanged; full production GDMA/board-init/PSRAM remains a separate gate.

### Further requested height trim: 81% of original

A second 10% reduction sets `kEyeHeightScale=.81f`: 56px → 50.4px → 45.36px,
or 19% less than the original. Width, centers/gap, body/mouth anchors and clocks
remain unchanged; request floor and full-sleep size are retained. The kernel
optimization itself is unchanged. O2/Os tests now pass **19,991,892 exact ordered
coverage callbacks**, covering 100%, 90% and 81% heights. The earlier full-frame
parity count and host benchmark above refer to 90%, not this new source.

No new hardware capture was performed here. `b088706a` remains the
archived first-trim profiling artifact; it is not the latest source identity.
Historical emulator occupancy and placeholder FIFO fixtures remain unchanged and
cannot calibrate this revision. Ongoing unrelated contact changes must be tested
separately from eye-height tuning; do not weaken contact assertions to make this
visual pass pass. After those concurrent fixtures settled, all 12 native suites,
55 Python tests and 16 actual QEMU tests passed again. The active LSP probe
remains partly inconclusive; native tests and the Arduino compiler are the
validation evidence.

### Combined build with broad counter touches

The later combined O2 firmware includes the 81%-height eye/kernel pass plus
black-only body-attached counter touches with broad, shallow shoulder geometry.
Normal build uses 704,247 program bytes and 46,656 global bytes. All 12 native
suites, 55 host tests, sanitizer checks and 16 QEMU functional tests pass;
changed-path LSP probes are clean for the scene/interaction/poke test files. No
fresh physical timing was collected, so neither the cached 25.58fps nor prepared
`b088706a` identity applies to this changed source. Full production
GDMA/board-init and physical button/mic tests remain separate gates.

### Paired flight animation — no matching physical occupancy

The paired-numeral duet intentionally changes label/gaze trajectories, not frame
pacing, renderer kernels or transport clocks. It uses absolute elapsed time, a
360ms lead stagger, 2.5-second label excursions and 90ms body overlap. Its wide
layout and changed source cannot reuse historical working/vertical FPS or
occupancy. `test_counter_flight.cpp` checks bounded fitted-label separation,
cadence/wrap/priority and the historical-envelope rejection boundary. The normal
O2 Arduino build with strict active-side lids/no smile compiles (706,539 program
bytes; 46,664 global bytes). No fresh physical
timing was collected. Native GIFs are synthetic visual evidence only; no new
occupancy envelope, USB-grant or CPU-cost value is assigned. Historical
`findEnvelope` gates and FIFO replay inputs remain unchanged.

### Completion sorting — unmeasured event-driven replacement

The completion-sort timeline removes idle recurrence/stagger. It gathers
both old labels simultaneously, holds at the apex from 1050–1600ms, swaps to exact
new snapshot counts at 1400ms, then returns. The 2.95s beat and 90ms eye/body overlap
remain; frame pacing, raster kernels and transport clocks are unchanged. Native
regressions cover the apex swap before return, count-edge gating, supersession,
zero/long Romans, cadence/wrap and full header composition. No physical occupancy
was collected or envelope added. Historical `f8cb1d72...`/`b7bf24bd...` results
are not this revision. Native GIFs are synthetic visual evidence only.

The beat is 380ms longer (3.33s). No physical occupancy was measured.

The normal O2 build uses 704,511 program bytes and 46,688 global bytes. Mouth
scene composition returns before raster work, but this is not a measured
performance improvement. No physical occupancy was collected and no historical
envelope is enabled for the changed source. Default-off and explicit opt-in
mouth builds are covered by native tests.

### Expanded thinking library — unmeasured

Two authored thinking clips (Work through it, 3.02s; Connect the dots, 3.14s)
expand the thinking bag to nine; the other states retain seven. All consumers
use state-derived clip counts and the player uses 16-bit masks, including no
immediate repeat at bag rollover. Existing clip timing, 90ms body overlap,
rendering kernels, frame pacing and transport clocks are unchanged. Native
all-clip raster/contact regressions now include the additions (160 real-contact
cases). The normal O2 Arduino build passes (706,339 program bytes; 46,688 global
bytes). No physical timing capture was performed. Historical occupancy envelopes
remain fingerprint-gated and unchanged; host GIFs are synthetic visual evidence,
not measured physical or QEMU FPS.

Keep authored gaze, holds, blink shapes, physics, status semantics and local-only voice. Optimize the workload before raising FPS, SPI, or hiding stalls by slowing gestures.

### Snappy saccades, life layer and blink shape — prepared, not physically timed

The 2026-11-04 expressiveness pass (see [avatar design](avatar-design.md)) adds
pose-level work only: derived saccade/closing easing, `eye_life::Life`
micro-saccades/blinks and a 240Hz-substepped eye-size spring. That is a few
dozen float operations per frame. The blink shape adds one per-eye offset and
width term; the per-pixel loop and column cache are unchanged (the arch path
subtracts the sink). Default `eye_raster::render` arguments remain the frozen
kernel, so all eye/mask/mouth parity oracles still pass unchanged.

Workload proxy: `tests/test_eye_life.cpp` counts eye-raster coverage callbacks
for every clip at 20/25fps through acting, life and spring. It fails if the max
or mean grows by more than 3% over the pre-pass baseline. Current: vertical
max +2.1% (spring overshoot), mean −0.7%; request max +0.3%, mean −0.7%. Tilt
time and the request 48px floor are unchanged. Expected device effect is within
noise of the eye stage, but this is a host proxy, **not** ESP32 timing or FPS.

The normal O2 build compiles (707,499 program bytes; 46,776 global bytes).
No physical timing capture. No profiling capture or
occupancy envelope was produced; the changed source is outside all historical
`findEnvelope` entries. Confirming the ≥20fps goal needs an authorized
working/vertical capture plus wide/request windows.

## Smoothness pass: overlapped presentation — prepared, not physically timed

The cached working/vertical capture measured ~22ms render-side median (24ms p95)
plus a serialized ~13ms display call, i.e. ~39ms start intervals. The round-2
objection to DMA overlap (render alone ~35ms) no longer holds, so firmware now:

- Allocates two 240×135 RGB565 sprites in internal DMA-capable RAM (~130KB);
  falls back to the old single synchronous buffer if the second fails or the
  first needed PSRAM. `presentFrame()` closes the previous transaction (waits
  for its DMA), opens one, queues `pushSprite()` and swaps buffers. Every frame
  repaints the whole buffer, so no state carries between buffers. Rotation and
  nap entry close the transfer first; the display bus is not shared.
- Copies the four cached background rows directly in sprite (swapped) byte
  order instead of 135 `pushImage` calls, and clears opaque face runs with
  `memset` (black is 0 in either byte order). API fallbacks remain.
- Schedules frames on fixed 33ms deadlines (resynchronising after a long stall)
  instead of "33ms since the last start", which drifted to every render+loop
  time; idle waits use 1ms delays.
- Polls the IMU every 40ms instead of every loop pass and formats labels into a
  stack buffer instead of allocating an Arduino `String` per badge per frame.

Expected but **unmeasured**: render ~22–25ms with the transfer hidden should hold
the 33ms cadence in working/vertical; wide/request/sleep remain unknown. The
`flush` metric semantics changed (see `docs/frame-profiling.md`); do not compare
new captures with historical ones as a series. Normal O2 build: 723,931 bytes,
globals 49,560 bytes (+2.3KB for the stored rows); sprite memory is heap.

## Smoothness pass: eye spans and cheaper mask — prepared, not physically timed

- **Eye interior spans.** For unrotated, non-happy-closed eyes each row's
  interior is solved analytically (rounded box via one `sqrtf`, convex lid via
  its quadratic roots, cheek row gate) with a 0.02px distance margin, so every
  claimed pixel evaluates to alpha exactly 1. Spans are reported in their row
  position; `avatar_scene::eyes(..., fill)` turns runs fully inside the face mask
  into direct fills and keeps edges, face-crossing runs and all AA per-pixel.
  Expanded in place the callback sequence equals the frozen round-4 oracle bit
  for bit (68,975,381 ordered comparisons, including the stress poses; spans
  cover 64.6% of oracle pixels there, more for real unrotated clips).
- **Superellipse roots.** Settled layouts use exactly p=2/p=4 now that
  `approach()` arrives, so `profile()` uses `n*n` (bit-identical to `powf`) and
  correctly rounded `sqrtf` roots instead of ~750 `powf` calls per frame; reflow
  keeps `powf`. The round-3 oracle now allows 2e-4px on mask edges; measured
  worst drift 3.1e-5px. Raster/edge-fill/poke tests are unchanged.
- **Per-frame fluid slices.** `fluid_face::Frame` hoists each noise call's time
  floor/fade and shifts once per mask (~510 field evaluations per paired frame);
  bit-identical to the plain formula (asserted in `test_face_physics.cpp`).

Host-only per-frame wall time over every clip (x86 -O2, outputs consumed; not an
ESP32 estimate): vertical mask 24.9→19.0us and eyes 29.8→16.8us; paired mask
39.6→31.9us and eyes 30.0→16.3us, versus the original `f7c8927` sources. Xtensa
`powf`/division costs differ; only a source-matched physical capture can say
what the device gains. No noise decimation or other approximation was added.

## Fluid lattice table — measured 2026-11-22

The refactor that moved the frame into `firmware/avatar_frame.h` was timed
first: two alternating pairs of 65-second O2 captures against the previous
firmware, all in the same finished/vertical workload (`flags-0`), at the 30fps
cap. Mean render-side time was 13.51/14.05ms before and 14.22/14.03ms after,
so the refactor is neutral within run-to-run noise (clip timing is seeded from
`esp_random()`).

At that point the mask stage was 45% of rendering (6.3ms), almost all of it
the two `fluid_face::field()` calls per row: 1,080 `noise()` calls and 8,640
lattice hashes per frame. Each hash ends in a float division, and the ESP32-S3
FPU has none: every division is a `__divsf3` call. `floorf` is a library call
too.

- **Hash table.** The lattice repeats every 16 in x, y and z, so
  `fluid_face::HashTable` holds all 4,096 `hash()` values (16 KB of internal
  DRAM, built once). Same floats, so `noise()` is bit-for-bit unchanged.
- **One table lookup per frame.** The first attempt fetched the table through
  a function-local static inside `noise()`. That measured **2ms slower**: this
  toolchain calls `__cxa_guard_acquire` (a lock) on every pass, not only on
  first use. `Frame` now holds the table pointer, so there is one guard per
  mask.
- **`floorInt`.** `static_cast<int>(floorf(x))` becomes a truncation plus a
  compare, identical for every finite value in `int` range (asserted in
  `test_face_physics.cpp`, including -0 and both neighbours of each integer).
- `sideBounds` takes `softSides`/`unusedSideStretch` from `buildMask` instead
  of recomputing them for every row.

| Same workload, back to back | Before | After |
|---|---:|---:|
| Mask median / mean | 6.15 / 6.25ms | 2.99 / 3.12ms |
| Render-side mean | 13.82ms | 10.73ms |
| Render-side p95 | 18.0ms | 15.0ms |

Static RAM grows by 16 KB (the table). The device's frames are unchanged
(`tests/test_device_frames.py`), and so are the site's: old and new
`avatar.wasm` give identical bytes over 3,000-frame traces for the site, the
dark widget and light mode.
