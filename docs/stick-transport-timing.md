# StickS3 LCD/USB timing model

## Numeral accommodation — unmeasured layout

[Numeral space](avatar-counter-space.md) now reserves avatar shift/shrink for
side labels. Bottom labels use full-size fonts until a real width collision;
the avatar above them stays full-size and centred. Sorting reserves old/new
fonts and centres the whole unequal group within the screen. Larger glyphs
change raster workload, but no timing calibration is assigned: actual new
source/app hashes remain rejection-tested. O2 compilation and all 25 native/76
Python/16 functional QEMU cases pass.
Frame/CPU/USB/SPI/DMA clocks, FIFO fixtures and measured costs are unchanged.
Full-production GDMA/board-init/PSRAM and physical appearance/timing are unverified.

## Shoulder-junction correction

Physical smoothness after the continuity work was good, with a small inward
crease during completed-only left and active-only right hand retreats. The
[shoulder correction](avatar-shoulder-join.md) adds a row-prepared skin
fillet and skips unused vertical contact only at exactly zero corner reflow. All
nonzero reflows retain both contact components. Base masks/eye clipping,
acting/frame/transport clocks and historical occupancy costs are unchanged. The
new source remains deliberately uncalibrated; native geometry metrics and
compilation must not become a physical FPS claim.

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

Research and validation: **2026-11-01**. No hardware access, USB capture or
serial writes were involved.

**Separate functional follow-up:** the
[QEMU peripheral prototype](../experiments/qemu-sticks3/README.md) now patches a
private copy of this exact QEMU tag and passes ten tests, including a stock-IDF
SPI3/ST7789/USB-driver guest boot. It currently supports non-DMA LCD transfers
and a local CDC socket, not complete M5Unified firmware or calibrated timing.
The source-derived timing model below is a separate native harness; do not
conflate the two test suites.

## What we can establish from the source

### LCD

Installed M5GFX **0.2.32**, `src/M5GFX.cpp`, StickS3 detection/configuration:

- SPI3_HOST, SPI mode 0, **40,000,000 Hz write**, 16,000,000 Hz read.
- MOSI GPIO39, SCLK GPIO40, D/C GPIO45, CS GPIO41, reset GPIO21.
- 135×240 ST7789-family panel; this application renders 240×135 landscape.
- `spi_3wire=true` selects shared-data-line read behavior, **not nine-bit
  command/data encoding**: GPIO D/C and `writeCommand(..., 8)` are used.
- No shared display bus is configured (`bus_shared=false`).

`Panel_LCD.inl::set_window_8` writes CASET plus four address bytes, RASET plus
four address bytes, then RAMWR. Address windows are cached, so a steady full-screen
flush normally needs only RAMWR. `end_transaction()` calls `_bus->wait()`.
Historical builds therefore had synchronous transfer occupancy even when DMA
moved the pixels. Since the smoothness pass (step 3) firmware keeps two frame
buffers: `presentFrame()` ends the previous transaction (waiting for its DMA),
opens a new one, queues `pushSprite()` and returns while DMA sends that frame,
and the next frame renders into the other buffer. `Profile::overlapped_flush`
models this (off by default, so historical envelopes keep the synchronous
model); `transport_scenarios.h` checks that an assumed 25ms render fits 33ms
only with overlap and that overlap never hides a >33ms render.

For 32,400 RGB565 pixels:

```text
pixel bytes                         240 × 135 × 2 = 64,800
pixel-only wire time                64,800 × 8 / 40 MHz = 12,960 us
first/changed-window command bytes   11
cached-window command bytes         1
integer-ceil modeled durations       12,963 us first; 12,961 us cached
```

That is a **wire-time lower bound**, not a measured flush duration. Driver setup,
DMA descriptor gaps, conversion, PSRAM contention, and interrupt overhead add
cost. At a 33ms frame interval, about 39% is already occupied by pixel transfer,
leaving roughly 20ms for render/conversion/other work before additional overhead.

The driver leaves the FRCTRL2 setting commented out because variants differ.
We did **not** assume 60Hz, 119Hz, or add a fabricated panel scan/TE delay. The
renderer does not wait for scanline/TE before pushing frames. Pixel arrival in
controller RAM is not the same as photon/scanout timing.

Startup code also has explicit PMIC/bus 100ms waits and an ST7789 sleep-out
130ms wait. These are source facts, but full panel/power boot is not represented
by the model and was not approximated by a single boot delay.

### USB

The board's default USB mode is hardware USB Serial/JTAG CDC, not UART or an
external USB-UART bridge. ESP32-S3 TRM v1.8 chapter 33 establishes:

- **USB full speed, 12 Mbit/s**, not USB high speed 480 Mbit/s.
- Bulk IN/OUT endpoints with **up to 64-byte packets**.
- CDC line-coding/baud-setting requests are ignored. The nominal serial baud
  setting must **not** become an emulated 115200-baud bottleneck.
- A flushed TX packet cannot be overwritten until the host consumes it;
  `IN_EMPTY` then permits the next packet.
- PHY clock is derived from 48MHz; controller/APB clocking and sleep states matter.

Installed Arduino core **m5stack:esp32@3.3.9**, `cores/esp32/HWCDC.cpp`:

- Default TX BYTEBUF and RX queue: **256 bytes each**.
- TX/RX ISR packets/stash: 64 bytes.
- Default enqueue timeout: **100ms**. Its current loop caps consecutive failed
  attempts at **20**: roughly **2s without enqueue progress per chunk**, not
  necessarily a total-call limit under intermittent progress.
- A full last packet can require an extra zero-length packet.
- RX ISR copies what fits in the software queue and discards excess bytes.
- IN-only backpressure can leave SOF/OUT alive; that is different from disconnect.

ESP-IDF's BYTEBUF ring implementation has **no per-item header overhead**; its
maximum item size is the whole buffer. `ReceiveUpToFromISR` returns only the
contiguous bytes before a wrap boundary. This detail matters: after a 7-byte
S3V1 header, a full-buffer producer can cause extra short packets, not just
`ceil(total_bytes/64)` ideally packed packets.

The source application sends a 7-byte header and then 2,560 bytes for each
1,280-sample PCM16 block. Capture readiness is nominally 80ms at 16kHz, and the
source rearms recording after its synchronous send. Timing tests use **byte
counts only**, no recordings, actual microphone capture, or transcripts.

`Serial.write` does not promise physical-wire completion when it returns:
bytes may still be in the ring/FIFO. Model enqueue blocking and packet delivery
separately. The general IDF console document's one-time 50ms wait is not a
replacement for the Arduino HWCDC path's current timeout policy.

## Why this is a harness layer rather than a QEMU peripheral patch

In the **exact QEMU esp-develop-9.2.2-20260417 tag**:

- ESP32-S3 instantiates `TYPE_ESP32C3_JTAG` at USB Serial/JTAG registers.
- `hw/misc/esp32c3_jtag.c` is a **stub**: reads return zero and writes are ignored.
  It does not model a working CDC FIFO, interrupts, host transactions, or timings.
- The SoC instantiates the SPI1 flash/PSRAM controller, but not the LCD-driving
  SPI3/GDMA/GPIO path as an ST7789 peripheral.

A proper register-level port would need SPI3, DMA/GPIO command handling, ST7789
RAM/commands, USB FIFO/IRQ/control behavior, and a host backend—not merely a
sleep inserted in the existing USB stub. We did not modify that stub to pretend
it implements those features.

Instead, a shared transport model runs in native tests. This adds meaningful
transfer occupancy/queue/backpressure coverage without replacing the production
drivers with misleading stubs. It is **a standalone transport model**, not full
M5Unified firmware emulation or a virtual USB device exposed to the host OS.

## Implemented model

Files:

- `experiments/qemu-sticks3/transport_timing.h`: source-derived constraints and
  event-driven synthetic host schedule.
- `experiments/qemu-sticks3/transport_scenarios.h`: shared regression scenarios,
  including replay of the measured per-mode envelopes.
- `tests/test_transport_timing.cpp`: native test entry point.

The model includes:

1. Full RGB565 frame serialization, cached/changed command windows, blocking
   display occupancy, and configurable SPI rate.
2. TX ring plus 64-byte FIFO, contiguous wrap-aware drains, partial packets,
   zero-length termination, blocking chunk enqueue, and bounded no-progress waits.
3. A 256-byte RX queue whose ISR can progress while the main parser is blocked;
   poll latency and discarded bytes are reported.
4. Shared IN/OUT service slots, IN-only or full host pauses, 2s status-byte-count
   arrivals, and synthetic 80ms audio readiness/rearming.
5. Render+LCD busy budget, render-start gaps, and the application's ~2ms main-loop
   poll cadence. USB blocking can delay starts even when render+LCD itself fits.

USB full-speed frames are 1ms, but bulk transfers have **no guaranteed fixed
service rate**. The test profiles assign **1, 4, or 8 combined grants per
frame**. These are explicit host assumptions. Eight is the model's conservative
profile cap, **not USB's maximum packets/frame**. RX is given priority when both
directions are ready.

CPU time is never calibrated: deterministic cases supply explicit assumed CPU
costs (16ms or 25ms), and neither proves physical FPS. ISR execution cost,
critical-section delays, bus contention and endpoint disconnect/re-enumeration
are not fully reproduced.

## Results

Useful deterministic findings:

| Scenario | Model result |
|---|---|
| First / cached 40MHz LCD flush | 12,963 / 12,961us |
| Voice send, four grants/ms, empty initial buffers | 11,250us enqueue blocking; 51 data packets from wrap-aware drains |
| Assumed 16ms CPU + display, one grant/ms, recording | start gap up to 76,078us despite render+LCD fitting the busy budget |
| Same CPU, four grants/ms | start gap up to 42,328us |
| Same CPU, eight grants/ms | start gap up to 36,703us |
| Assumed 25ms CPU + 40MHz SPI | over 33ms busy budget, detected |
| Assumed 16ms CPU + 20MHz SPI | over 33ms busy budget, detected |
| IN-only service stopped for 3s | 2s enqueue timeout; header + one payload chunk accepted (263 bytes); short write detected; OUT still live |
| 320-byte RX burst while main waits for LCD | 256 queued, 64 discarded, detected |

Expected-fault tests pass by **detecting** the fault, not by claiming data was
preserved. One production follow-up is important: `sendFrame()` currently ignores
both `Serial.write` return values. A short payload write can truncate S3V1 framing.
That path must be hardened before promising safe behavior under host
backpressure.

## Reproduce

From the project root:

```sh
g++ -std=c++11 -O2 -Wall -Wextra -Werror tests/test_transport_timing.cpp \
  -o /tmp/stick-transport-test
/tmp/stick-transport-test
```

## Sources

- M5Stack board page: <https://docs.m5stack.com/en/core/StickS3>
- Board schematic:
  <https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150_Stick_S3_PRJ_V0.6_20251111_2025_11_17_16_10_24.pdf>
- ESP32-S3 TRM v1.8, chapter 33, especially 33.2 and 33.3.2–33.3.3:
  <https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf>
- IDF console buffering/sleep caveats:
  <https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/usb-serial-jtag-console.html>
- Exact QEMU USB stub:
  <https://github.com/espressif/qemu/blob/esp-develop-9.2.2-20260417/hw/misc/esp32c3_jtag.c>
- Exact QEMU SoC/SPI model:
  <https://github.com/espressif/qemu/blob/esp-develop-9.2.2-20260417/hw/xtensa/esp32s3.c>
- Local source files: M5GFX `M5GFX.cpp`, `Panel_LCD.inl`, `Panel_ST7789.hpp`,
  `Bus_SPI.inl`; Arduino `HWCDC.cpp`/`.h`; IDF `esp_ringbuf/ringbuf.c`.

An attempted Waveshare ST7789 PDF is **ST7789VW**, not the board page's ST7789P3.
It was not used to invent P3 rated SPI limits or scan timing. A board-specific
P3 datasheet was not obtained. The official USB 2.0 download link was discovered
but returned 403; two guessed mirrors returned 404. Exact legacy timing URLs for
an IDF driver page and a QEMU filename also returned 404; use the successful
TRM/console/exact-tag references above rather than repeating those guesses.

## Physical baseline added — 2026-11-01

The [production frame profiler](frame-profiling.md) measured build `963823bf` on
identified StickS3 hardware at 240MHz/40MHz SPI: median render-side wall occupancy
191.70ms, completed display call 13.061ms, total 204.76ms, and starts 209.10ms
apart (~4.79fps mean). Face and eyes cost ~68.55ms and ~82.97ms respectively.
This replaces the *assumption that the current native renderer fits the budget*.

Emulator timing requirements:

- Keep CPU/cache/FPU behavior uncalibrated until the same production fingerprint
  and metric boundaries run in both environments. Native renderer costs must not
  be converted into an instruction-count cycle claim.
- Retain 12,960us as pixel-wire lower bound; measured display p95 is 13,065us.
  Their difference is driver-call occupancy, not scanout or universal DMA cost.
- Full firmware regression must permit >33ms busy frames and delayed input polling;
  never force the renderer to 30fps with fictional sleeps or a 16ms CPU preset.
- Baseline p95 render-side occupancy is 199,978us. Treat this as a **diagnostic
  comparison envelope only**: 10 startup telemetry drops disqualify the original
  trace from calibration. No measured USB host-grant or audio timing is available.
- Preserve FIFO/CRC tests as functional gates. Implement SPI3 GDMA and board init/
  PSRAM independently before claiming full M5Unified compatibility.
- Re-measure after renderer optimizations with new source/bin fingerprints; do not
  silently keep this old occupancy as the optimized renderer's speed.

The complete trace/footer (313 frames) and limitations are documented in the
profiling guide; raw artifacts stay outside Git. A clean, loss-free
steady-state capture remains necessary before activating calibration candidates.

### Optimized native renderer comparison

The [first performance pass](render-performance.md), build `4c60aaf3`, measures
13.58fps with 73.90ms median starts and 69.58ms full render/display occupancy.
Face/eyes drop to 12.59/20.40ms; the display remains ~13.067ms. This is a new
source-specific diagnostic envelope, **not a change to the emulator's physical
SPI divider**. Startup telemetry loss still blocks
calibration. Keep baseline and optimized fingerprints separate; full production
emulation must expose the same stage boundaries before fitting timing behavior.

### First clean per-mode candidate windows

[Round-2 results](render-performance.md) provide loss-free, footer-complete,
operator-declared physical working/vertical captures. Same firmware source:
Os `a4978b47` (1,050 frames) and O2 `9d40fa19` (1,190), with distinct recipe IDs.
Their render-side p95 candidates are 42,807us and 35,615us; display-call p95 is
13,067us and 13,069us. After subtracting the source-derived wire-only lower bound,
the report's effective pre-wire p95 envelopes are 42,910us and 35,723us.
These pass the existing quality gates; use only the matching per-mode/native
fingerprint and recipe for an occupancy-model comparison. Do not apply them to
wide layouts, or alter virtual USB grants or instruction clocks to force a fit. Startup warmup is explicit and cumulative counters remain visible.
The full-production SPI3 GDMA/board-init/PSRAM gate remains separate and incomplete.

Executable regression inputs now live in
`experiments/qemu-sticks3/measured_frame_envelopes.h`, preserving both source and
binary hashes, recipes and working/vertical mode. The shared native/QEMU guest
transport scenarios replay the effective pre-wire candidates, assert budget
misses and late starts, and leave default bus/CPU schedules unchanged. The actual
QEMU FIFO/CRC suite separately replays synthetic packets keyed to both build IDs.
Those modeled results are not fresh physical timings or CPU cycle calibration.
No envelope is selected automatically for current firmware; future performance
changes must add source-matched evidence and update the emulator tests.

Round-3 mouth segment/zero-flow mask optimizations change the firmware fingerprint.
Prepared O2 build `31e870e0` is compiled/native-parity-tested, not physically
measured. `findEnvelope` fails closed unless source and binary SHA-256s, recipe
and mode all match a historical entry; codec tests reject unknown source/binary,
recipe and wide-layout requests. Keep existing round-2 replay inputs unchanged
until a new authorized clean capture exists. Host microbenchmark ratios must not
be translated into emulator CPU costs or a fabricated new FPS envelope.

### Button/listening/counter-contact interaction revision

New authored listening/check-in/button timelines and bounded contact-driven
numeral springs change the current source fingerprint. Contact sampling is charged
to the mask stage in the existing profiler schema. No fresh physical occupancy
window existed for the original interaction revision. Fresh captures below now
provide matching working/vertical envelopes; do not use them for other source
fingerprints/modes or infer 30fps from native GIFs. `tests/test_avatar_interaction.cpp` validates
bounded/settling contact, timestep consistency, listening priority/acting and
side-specific emotions without hardware. The synthetic listening-mode packet
in `tests/test_frame_profile.cpp` crosses the actual QEMU FIFO in
`test_profile_transport.py`; its costs are placeholders for framing coverage,
not calibrated listening/voice-load costs. Full M5Unified/GDMA/board-init and
physical button/microphone viewing remain separate gates.

### Clean baseline/cache-pass occupancy windows

Authorized O2 working/vertical captures `7dfcc87d` and `2bf53c52` add 1,410 and
1,535 complete, loss-free observed frames after explicit warmup. Measured cadence
is 23.50 and 25.58fps; live clip/label/contact differences preclude a controlled
A/B claim. Their render-side p95s are 28,168/24,083us, display p95s 13,080/13,083us,
and effective pre-wire p95s 28,288/24,199us. Latest start p95 is 41,003us; not 30fps.
`measured_frame_envelopes.h` retains both source/binary fingerprints in `cachePass`
and includes them in the opt-in `all` regression set. Shared transport cases
verify pipeline misses/late starts; synthetic QEMU FIFO replay preserves each
build ID without assigning measured costs to QEMU instruction clocks.

See [the performance guide](render-performance.md) for all stage costs, memory,
scalar/cached parity and limitations. No wide/request/listening/
voice-load occupancy or optical scanout measurement was added.

### Local poke/kiss revision — no matching occupancy window

The localized numeral-tip deformation, completed-side kiss/recoil and averaged
contact probe change the production source fingerprint again. Existing native
mask/AA/cache parity gates remain valid for inactive pokes; the intentional
interaction changes have separate coverage in `tests/test_avatar_poke.cpp`,
including 140 visible-contact performances. The solid-hand follow-up unions a
body-rooted rounded extrusion with the base mask, clips facial artwork to base
coverage, and asserts >1px bounded numeral travel plus flood-filled connectivity
through reflow/rim stress. Exact cross-sections, not the extrusion bbox, drive
contact. It also has no matching occupancy envelope. These are synthetic native tests,
not physical/QEMU FPS. Historical `findEnvelope` hash/recipe/mode gates must keep
rejecting this revision until a new authorized source-matched capture exists.
The existing native transport/codec and actual QEMU FIFO/CRC replays retain their
historical fixtures; no new occupancy values, CPU/bus clocks or USB grants are
inferred from the host preview. Full M5Unified/GDMA/board-init remains unrun.

### Smaller-eye/kernel snapshot — framing only, not occupancy

Its identity is recorded **outside** `measured_frame_envelopes::all`; codec
tests reject its source/binary pair and mixed new-source/old-binary requests. A
sixth timing FIFO qtest preserves that build ID with unchanged placeholder stage
costs. No physical eye-stage costs/FPS, CPU-cycle calibration, optical scanout
or USB-grant values were measured. All historical occupancy values and
bus/default CPU schedules are unchanged. See [render
performance](render-performance.md) for exact callback/frame parity, native
previews and prepared build provenance.

### Paired counter flight — visual timeline only

`CounterFlight` adds a 2.95-second staggered numeral/gaze duet after 7.6 seconds
of eligibility, repeating every 16.8 seconds. Absolute-time sampling keeps
trajectories frame-cadence independent; body tracking trails by 90ms. Native
regression checks long-label separation, wrap, cancellation, settling and
rejects both unknown animation fingerprints and historical vertical envelopes
for this wide layout. No new physical occupancy, QEMU CPU/bus clocks, USB grants
or synthetic packet costs are introduced. Existing transport/codec/FIFO replays
remain functional gates; full production GDMA/board-init execution is still
unrun.

### Completion-sort replacement — no occupancy calibration

Completion sorting replaces idle flight with a real paired directional
snapshot edge: done increases and active decreases. Both old labels gather at
once; the 1050–1600ms apex hold swaps labels to actual new totals at 1400ms
before return begins. The 2.95s visual beat retains 90ms body follow-through.
Native regressions cover swap-before-return, zero, exact/batched counts,
priority, wrap, cadence and rejecting historical envelopes for this changed
source/wide layout. No frame pacing, transport clocks, USB grants, modeled
packet costs or historical occupancy inputs changed. No matching physical
capture or full-production GDMA execution exists; native compositor tests do not
calibrate QEMU CPU/scanout. Disabling mouth composition does not justify
assigning new CPU costs, FPS, USB grants or occupancy. Historical
hash/recipe/mode gates remain fail-closed.

### Expanded thinking library — unmeasured

Two new authored thinking performances expand its no-repeat bag from seven to
nine (other states remain seven). The player and native inventory now derive
counts per state; 16-bit masks retain all nine entries. Existing clip timing,
90ms body overlap, rendering kernels and all transport clocks/costs are
unchanged. No physical timing capture was performed. Historical occupancy
fingerprints remain unchanged/fail-closed for this new source; no new FPS or CPU
calibration follows from synthetic GIFs. Full-production GDMA/board-init remains
an unrun gate.

## Ambient startle follow-up (2026-11-05, unmeasured)

See `avatar-startle.md` for native regression evidence. No
physical frame timing was captured: historical envelope selection stays
fail-closed, and transport costs, FIFO/CRC fixtures and QEMU clocks remain
unchanged. Native tests prove policy, not acoustic triggering, physical sleep
appearance or measured FPS. Full guest/GDMA unrun.

## Next step toward physical fidelity

A source-derived model cannot remove unknown CPU/driver/host latency. With
explicit authorization for an instrumented device build, measure render,
conversion, SPI completion, serial enqueue/blocking and short writes, RX polling,
and minimum heap under synthetic traffic. Fit distributions or conservative
bounds, not one average FPS. Keep timing metadata separate from audio payloads;
never run a broad USB capture that could record unrelated keyboard/audio traffic.

Feed measured CPU/overhead costs into `Transport::frame(cpu_us)` and richer
host-pause schedules. The
[MMIO peripheral work](../experiments/qemu-sticks3/README.md) has started as a
separate project, with SPI3 GDMA and complete board initialization still pending.
Neither this model nor the QEMU tests are a substitute for timing the real
dashboard firmware.
