# Avatar continuity: staged integration

The character should notice, respond and settle, rather than reset to neutral
or queue stale reactions. Real status/recording indicators update immediately;
acting is decorative, interruptible and never acknowledges tasks.

## Stage 1 — shared policy and authoring foundation

- `firmware/avatar_behavior.h` is the hardware/header semantic policy: priority,
  state-to-performance mapping, moods, resting poses and wrap-safe deadlines.
  Adapters retain physical power policy and layout/transport responsibilities.
- Clip celebration eligibility is explicit metadata, not pointer identity.
  Player bag storage uses the authoritative state count. Named gaze/lid/body
  pose modifiers let new performances avoid fifteen positional arguments.
- `test_avatar_behavior.cpp` checks state priorities, sleep/offline separation,
  listening precedence, deadline wrap, clip key ordering and authoring helpers.
- Five targeted native suites and the renderer Python tests pass. Normal ESP32
  O2 compilation passes.

## Stage 2 — meaningful, interruptible transitions

`avatar_director.h` is now shared by firmware and the live header. It owns
notice/respond/settle acting, including request/resume, listening/release,
completion, doze/wake and disconnect/recovery. It captures the actual composed
pose after cues/life; immutable entry curves inherit bounded translation
tangents. Body follows by 90ms. There is no neutral reset or reaction queue.

Counter sorting and explicit button cues own their performances; transitions
yield and the next clip rebases on their final displayed pose. Checks and life
do not fight transition acting. Existing scalar clip/contact fixtures retain
their fixed timelines. Target gaze uses actual layout-relative counter centers.
Recovery/listening return establish count baselines rather than inventing
completion events. A fresh completion while work remains now celebrates briefly
in the header, then returns to work, matching hardware policy.

`test_avatar_director.cpp` covers all 42 directed mode edges at three cadences
and across uint32 wrap (252 scenarios), immutable curves, inherited tangents,
90ms body delay, every-50ms urgent interruptions, external-cue yield and
historical-envelope rejection. Full-header tests cover recovery/listening
baselines and return to working. Seven synthetic transition GIFs/contact rows
come from `scripts/preview-transitions.py`; no live statuses are injected.
Normal ESP32 O2 and native tests pass. No physical timing claim.

## Stage 3 — expressive shapes and behavioral phrasing

- Pose has additive, zero-default per-eye scale, lower-lid and explicit slant
  controls, plus named authoring modifiers. Neutral/legacy keys retain exact
  output. Transitions use brief balanced asymmetry for curiosity/wake, gentle
  lower lids for listening and a focused lower-lid/slant release when resuming.
  Shape controls interpolate/capture with every other pose field.
- The eye raster keeps row spans and cached columns. New lower-lid rejection is
  conservative; scaled bounds include happy closures. No per-pixel general SDF
  system, long tilt holds, global fast-math or changed baseline guard.
- A dedicated seeded boundary-only RNG drives clip selection and phrasing.
  Working/calm-completion sometimes pair two clips with a short pause and long
  release, or take an extra 2.4–4.8s quiet rest. Gentle eye asymmetry/lower lids
  ease into/out of quiet working holds. Existing no-repeat bags and authored
  timelines remain intact; urgent modes do not get longer waits.
- Fresh eligible completions get two tiny green glints above the left numeral,
  fading once over 520ms (18 pixel callbacks at most). Requests/listening/offline
  and sorting/button ownership cancel them, with no deferred replay.
- `test_eye_shapes.cpp` checks 486 scalar/span exact-order combinations, per-eye
  independence, lower-lid area, a +3% max/mean transition pixel-work guard and
  bounded/canceled accents. Director tests compare 40 seeded clip/pause choices
  across 20/30/60fps. Existing eye oracle passes 68,975,381 ordered comparisons.

All 22 native suites and 76 Python tests pass. All 16 actual patched-QEMU
functional tests pass, including the isolated stock-driver guest and timing
FIFO/CRC tests; these do not execute the full production renderer/GDMA path.
Normal ESP32 O2 compilation passes: 745,155 program bytes, 50,808 static bytes.
Active LSP checks on changed C++/Python files report no errors. Both the original
30-clip gallery and the expanded eleven-scenario transition gallery were generated
and their contact sequences reviewed. Artifacts stay outside Git.
Firmware/header layout and presentation adapters remain distinct; semantic policy,
transition direction, phrase selection and rendering kernels are shared.

Host playback/compilation is not physical timing or button/microphone evidence.
Historical emulator occupancy envelopes remain fingerprint-gated and unchanged.

## Status

Code stages: `eb5c101` (policy/metadata), `fa0b770` (continuity director),
`ec7c99e` (eye shapes/phrasing). Physical appearance, on-device cadence/DMA behavior and button/microphone smoke
tests remain unverified. No new occupancy envelope or physical FPS is claimed.
