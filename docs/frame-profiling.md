# Physical frame profiling and emulator calibration

## Scope and current evidence

This is an **opt-in production Arduino/M5Unified timing build**, not the isolated
QEMU/IDF probe. It preserves the existing avatar, status semantics, voice controls,
and 33ms frame target. Ordinary builds have profiling disabled.

The emulator milestone in commit `c7ff33b` is useful but not yet a physical FPS
oracle. Read [the prototype README](../experiments/qemu-sticks3/README.md): it has
functional SPI3 PIO/ST7789/USB interfaces, but lacks SPI3 GDMA, complete board init,
PSRAM, TE/scanout, and calibrated CPU/cache timing. Its exact SPI deadline proves
its programmed-divider formula, not real render performance.

This work adds a portable diagnostic codec and **two actual QEMU CDC/MMIO tests**.
A native-encoded 95-byte packet crosses the 64-byte FIFO boundary, reaches the
unchanged bridge frame parser, and decodes correctly. Corrupted CRC is rejected
without losing the next frame. Their timing values are **preset synthetic fixtures**,
not measured QEMU or physical CPU costs. The original ten peripheral/stock-driver
tests remain unchanged and also pass. A physical trace has now been collected;
see the measured results below. Compilation and transport tests alone must not
be reported as hardware FPS.

## What is measured

All durations are device `micros()` wall time, not instruction/cycle counts:

| Metric | Boundary |
|---|---|
| interval | Consecutive render starts, including prior rendering, transfer, observer, main-loop work and waiting |
| update | State selection, layout/acting/physics, brightness and profiling setup |
| background | Gradient generation and canvas background rows |
| mask | Complete face mask construction |
| face | Black face rasterization |
| eyes | Eye rasterization |
| mouth | Mouth rasterization, including the vertical-layout gate |
| attention | Yellow band/marker, or its inactive branch |
| badges | Both foreground count labels and their background-colored outlines |
| flush | Blocking presentation: wait for the previous frame's DMA, then queue this frame (see below) |
| total | Render start through that presentation call |

`total - flush` is the rendering-side wall-time budget, including timer overhead
and possible interrupts/preemption. It is **not** isolated CPU instruction time.

**Semantics changed with double buffering (smoothness pass, step 3).** Earlier
builds measured `flush` as `pushSprite()` through `waitDMA()` completion, a
fully serialized ~13ms transfer. Double-buffered builds queue the frame by DMA
and render the next one into the other buffer, so `flush` now records only
the time spent waiting for the PREVIOUS transfer plus queueing. A near-zero
`flush` means the transfer fully overlapped rendering; it is not a faster SPI
link. Captures from the two semantics must not be compared as one series, and
historical envelopes stay fingerprint-gated to their own builds. If the second
internal DMA buffer cannot be allocated, firmware falls back to the old
synchronous single-buffer path (the transfer is then inside `flush` again).
A completed display transfer is **not** photon/scanout completion or panel refresh.
The 64,800 pixel bytes have a source-derived 12,960us wire-only lower bound at the
configured 40MHz; driver/conversion/DMA gaps may add cost.

The packet also contains source-build fingerprint, frame sequence, device uptime,
coarse UI mode/layout flags, CPU MHz, target period, dropped telemetry count,
voice-excluded count, and **the previous frame's observer cost**. No counts,
task IDs/titles, prompts, transcripts, PCM samples, or screen pixels are included.
Flag bits: 0 mouth visible; 1 wide/reflow (`corners >= .35`); 2 sleep fade;
3 PSRAM fallback *requested*. The last flag is not an allocation/heap audit.

## Collection is bounded and opt-in

- `STICK_S3_PROFILE=1` compiles the sampler. The default is zero.
- Diagnostics reuse the existing `S3V1` **T** kind, with an `S3PF` v1 binary payload,
  explicit little-endian words, and CRC32. The existing B/A/E/X audio and button
  protocols are unchanged. Payload is 88 bytes; complete packet is 95 bytes.
- Firmware checks capacity for the **entire packet** and writes once. Insufficient
  space drops the sample; short writes are counted. It never changes the global
  HWCDC TX timeout, which would also affect voice. HWCDC's capacity/write APIs can
  take locks, so this is **drop-on-full admission, not a hard nonblocking guarantee**.
  Observer duration exposes that perturbation; reports reject calibration when it
  exceeds the configured 1ms quality budget.
- Diagnostic emission is skipped while recording. Do not press A/record audio
  during a baseline capture. The benchmark does not start microphones or manipulate
  tasks to manufacture states. Other existing voice behavior stays local-only.
- The Linux bridge saves data only with `--profile-output`. It accepts only the
  fixed timing signature/schema/CRC and decoded numeric fields. Audio, other text,
  button payloads and raw serial are never written to the profiling file.
- A bounded asynchronous writer drops instead of blocking the serial consumer on
  disk. Maximum 10,000 records; queue maximum 256. Files are mode 0600, created
  exclusively without following an output symlink. A footer records final host
  losses, including losses after the last saved sample. Intentional timed stops
  are complete captures once the writer drains; `limit_reached` is separate.

## Build only — no device access

```sh
cd stick-s3  # repository root
.venv/bin/python scripts/build-frame-profile.py
```

This prints a private build directory and `frame-profile-manifest.json`. The
manifest contains firmware-source SHA-256s, source-derived display parameters,
Git revision, installed driver fingerprints and actual application-bin SHA-256.
`--optimization Os|O2` records the actual recipe property and gives O2 a distinct
build fingerprint even with identical sources; older Os identities remain valid.
The compiled 32-bit build ID must match the selected manifest. It is an artifact
correlation key, **not cryptographic authentication of the serial peer**.
The script refuses build products inside the repository and detects firmware
changes during compilation. It never uploads, opens serial, or changes services.

## Flash and capture

Use the **just-printed build**, not an old general build or QEMU flash image.
Stop whatever owns the Stick's serial port (for example the bridge service)
and restore it afterwards.

For capture, temporarily run the same bridge in the foreground with the new
flag, keeping real status snapshots and controls rather than injecting fake C
messages. Do not run two owners of serial. No broad USB sniffer is needed.

```sh
# Replace BUILD with the exact printed directory and PORT with your Stick.
BUILD=/tmp/stick-s3-profile-build-REPLACE_ME
PORT=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<SERIAL>-if00
CAPTURE=$(mktemp -d /tmp/stick-s3-frame-capture.XXXXXX)
# Stop the serial owner first (and restart it on exit), e.g. your bridge service.
# The diagnostic upload uses --input-dir "$BUILD", with normal hash verification.
# After upload/re-enumeration, collect a ~60s window; do not press A.
timeout --foreground --signal=TERM --kill-after=10s 85s .venv/bin/python bridge/herdr_bridge.py \
  --port "$PORT" --profile-output "$CAPTURE/frames.jsonl" --profile-limit 10000 \
  --profile-seconds 65 --profile-warmup-seconds 5
# Warmup excludes only timing records; status/controls/voice still run normally.
# Internal deadline normally exits zero and drains the writer/footer.
# External timeout is a safety guard only, not the capture's shutdown mechanism.
```

A proper implementation must handle the intentional timeout status instead of
mistaking it for a measurement failure. Do not clear tasks/completions, submit
prompts, reset unidentified devices, or record ambient audio to exercise a state.
The normal bridge can remain attached after capture; it ignores the timing T
packets unless collection is explicitly enabled. Returning to an ordinary
non-profiling firmware build removes measurement overhead entirely.

## Physical baseline — 2026-11-01

A 65-second timing-only capture exited normally with a completed writer footer:
**313 frames**, primarily working/wide layout (309 frames). No synthetic live
statuses or audio capture.

| Measurement | Median | p95 |
|---|---:|---:|
| Render-start interval | 209.10ms | 217.00ms |
| Render-side wall time (`total - flush`) | 191.70ms | 199.98ms |
| Face rasterization | 68.55ms | 68.58ms |
| Eyes | 82.97ms | 91.33ms |
| Full render + completed transfer | 204.76ms | 213.04ms |
| Display transfer | 13.061ms | 13.065ms |
| Previous observer overhead | 0.118ms | 0.122ms |

Mean start interval is 208.92ms, approximately **4.79fps**, not 30fps.
Every sampled frame misses the 33ms render budget. This establishes rendering
as the dominant bottleneck for the sampled workload: face/eye per-pixel work is
much more expensive than display transfer. Do not raise FPS or overclock SPI
as the first fix. Review mask coverage, loop-invariant eye calculations, SDF
clamps and unnecessary framebuffer reads before changing authored animation.

The trace includes **10 startup telemetry drops** between sequence 1 and 12,
before the foreground bridge attached; subsequent sequence numbers are contiguous.
Host drops/invalid packets and voice exclusions are zero. Because the original
quality gates deliberately reject any loss, `calibration_ready=false`: these
percentiles are diagnostic evidence, not an approved emulator calibration.
Other layouts, completion, requests and voice load have not been benchmarked.

An earlier capture was forcibly killed without its footer; it is not a complete
measurement. `--profile-seconds` now supplies a controlled internal deadline,
deferring shutdown while a voice stream is active.

## First renderer optimization

The [performance research/results](render-performance.md) document exact mask
shortcuts, hoisted eye constants, guarded direct RGB565 blending and gradient
reuse. Final build `4c60aaf3` measures **13.58fps**, with 73.90ms median starts,
69.58ms full-frame occupancy, face 12.59ms, eyes 20.40ms and display 13.067ms.
17,508 complete native frames remain pixel-identical to the pre-optimization
working-tree renderer. Startup loss still prevents calibration approval; the
33ms target is not achieved.

## Round-2 clean windows

Span/bounds and compiler experiments are recorded in [render performance](render-performance.md).
Working/vertical layout measured 17.51fps at Os (`a4978b47`) and 19.82fps at O2
(`9d40fa19`), with the same source and distinct optimization recipes. Five seconds
of explicit timing-only startup warmup avoids including pre-attachment gaps;
no counters or saved records are reset, edited or retroactively discarded.
The 1,050/1,190-frame windows have completed footers and no observed loss/exclusion,
so both pass the existing calibration gates for **per-mode occupancy candidates**.
O2 render-side p95 is 35,615us, display p95 13,069us. This is not wide-layout or
proof of calibrated CPU cycles/USB grants/optical scanout. Retain the O2 recipe
flag on rebuild.

## Measured round-3 baseline and caching pass

Both use O2, 240MHz, 40MHz SPI, the same 33ms target, 65-second collectors and
explicit 5-second warmup. Footers are complete, clocks and sequence numbers are
stable, with no in-window loss/invalid packets or voice exclusions; both pass
per-mode occupancy gates. Startup totals remain visible. Live clip/label
selection and contact behavior changed between windows; do not advertise a
controlled same-workload A/B or extrapolate vertical to other modes.

Cached render-side median/p95 is 22,015/24,083us, full render/display p95
37,159us, start p95 41,003us and display p95 13,083us. Background and numeral
median costs are now 2,431/471us. This still misses 30fps. Artifacts and
comparison are in [render performance](render-performance.md).
Source/recipe/binary-gated emulator envelopes preserve both captures separately;
no CPU-cycle, USB-host-grant or optical-scanout calibration is implied.

## Report and calibration gates

```sh
.venv/bin/python -m bridge.frame_profile_report \
  --input "$CAPTURE/frames.jsonl" \
  --manifest "$BUILD/frame-profile-manifest.json" --source physical \
  --output "$CAPTURE/report.json"
```

Source is explicitly **operator-declared**: use `qemu`, `synthetic`, or `unknown`
when appropriate. CRC or a matching build ID never makes emulator/fixture data
physical evidence. The report checks stage sums and outputs min/mean/p50/p95/p99/
max, busy deadline misses, late starts, sequence gaps, losses, observer overhead,
CPU-clock/period consistency and coarse mode groups. It requires a completed
capture footer, matching manifest and at least 300 clean frames before marking
physical calibration candidates usable. Loss/excluded voice, resets, inconsistent
gaps, clock/period changes or observer over-budget make percentiles suspect.

Interpretation:

- Steady ~33–35ms starts with `total < 33ms`: our ~30fps cap/short gesture staging
  is a likely contributor; many 90–140ms gestures get only a few updates.
- `total > 33ms`: render/transfer workload is missing the budget. Inspect individual
  stages rather than raising the cap blindly.
- Start gaps large while `total` fits: inspect work outside rendering and observer
  time. The outside-render gap includes intentional idle/polling; it is not pure
  idle CPU time.
- A long `flush` identifies display-call occupancy, not LCD optical lag. Do not
  assume a panel refresh rate or overclock SPI based on this trace.

`transport_candidates` separates measured rendering wall time from display-call
non-wire overhead and supplies an effective pre-wire budget candidate for
`Transport.frame(cpu_us)` in the earlier transport harness. That scalar is an
**occupancy envelope**, not a cycle-calibrated CPU implementation: conversion,
DMA gaps and waits may overlap. Fit per-mode distributions/conservative bounds,
not one average. This frame-only trace cannot determine USB host grant rates,
CRC/retry physics, TE/scanout, I2S or voice enqueue timing. In particular, do not
"calibrate" QEMU's `poll-us` by pretending non-1ms USB SOF intervals are physical.

The sorting-background retention fix changes presentation only: the
zero numeral still swaps at the apex, while background removal waits for the
single-side shift and uses the existing 120ms fade. Flight/eye clocks, frame
pacing, transport fixtures and historical occupancy gates are unchanged. Native
header regression and O2 compilation do not calibrate this changed source; no
matching physical capture or full production QEMU/GDMA run has been made. The
subsequent unused-edge mask stretch fixes the reported blue sliver
without changing frame/flight/acting clocks. Native 6,804-frame edge-fill and
full-sorting regressions/O2 compilation are not a physical timing capture; this
changed source remains outside historical occupancy envelopes. Physical visual
feedback and fresh timing remain pending.

The next emulator integration gate remains SPI3 GDMA completion ordering, then
M5Unified board-init/PSRAM support. Once the full production renderer runs there,
compare the **same source fingerprint and metric boundaries** with physical
captures. Preserve the distinction between functional peripheral tests and
calibrated timing/visual behavior.
