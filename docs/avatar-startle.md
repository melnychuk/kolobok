# Loud-noise startle (opt-in microphone level)

The avatar can be startled by a sudden loud noise (clap, bang, dropped object):
an instant wide-eyed jolt and hop with a short shiver, a double take, then a wary
glance to both sides. It is compiled in only with `STICK_S3_AMBIENT_STARTLE=1`;
the default build keeps the microphone closed except for push-to-talk dictation.

## Privacy

- Audio stays in two 512-sample RAM blocks on the Stick and is overwritten
  continuously. The detector keeps only a block level, a noise floor and the
  previous level. No samples, levels or startle events are sent over USB,
  stored, or logged; the host bridge and protocol are unchanged.
- Dictation is unchanged: holding A closes the ambient capture first, then
  streams PCM exactly as before.
- Ambient capture stops while unplugged/napping and during the attention chime.

## Firmware path (`firmware/firmware.ino`)

- The ES8311 codec and I2S clock pins are shared by the microphone and speaker:
  only one can run. `updateAmbient()` owns the hand-over: it closes the
  microphone and restarts the speaker before the attention chime, during
  dictation and while unplugged; it resumes 350ms after the chime (600ms after
  dictation release) so the Stick never startles at its own sounds.
- The M5Unified capture task (priority 2) is pinned to core 0, so the 30fps
  render loop (`loopTask`, priority 1, core 1) is never preempted by it.
- Two 32ms blocks at 16kHz alternate, so capture is continuous. Per block the
  render core does one integer pass (sum, squares, peak) and one log: a few
  microseconds per 32ms. No frame-time work is added except the cue's pose mix.
- Button activity and IMU rotation above 45°/s (picking it up, tapping it)
  suppress triggers for 700ms; suppressed blocks still adapt the floor.

## Detector (`firmware/sound_startle.h`)

A block triggers only when it is all of: ≥23dB above the adapted noise floor
(plus habituation), ≥12dB above the louder of the two previous blocks (sudden
onset), ≥-27dBFS RMS, and peaks ≥10000/32767. The relative and absolute
thresholds were raised 3dB on 2026-11-05 to slightly reduce sensitivity. The floor falls with a 1s and rises
with a 6s time constant, so gradually rising music/speech is absorbed. After a
trigger: 5s refractory period, +6dB habituation (max +18dB) fading at 0.3dB/s.
The first 700ms after the microphone opens are ignored. Thresholds are in
`sound_startle::Config` and are **not yet calibrated on the physical microphone**.

## Acting (`avatar_interaction::Startle`)

1.5s, 50ms entry, eyes-first with the usual 90ms body lag, mirrored glance side
chosen at random (the mono microphone cannot localize). It adds a centered
`surface.kick(0, .55..1)` for the soft rim. Listening, dictation, button cues and staged count cues (sorting, arrival,
transfer) win: they suppress or cancel it, and it never mutates task state.
USB-connected visual sleep does **not** suppress it: `updateStartle()` consumes
the trigger before face selection, wakes to idle (or offline during connection
grace), and uses a fast 35ms sleep-layout release so closed eyes do not hide the
jolt. The ordinary four-second idle-awake hold then lets it return to sleep.
On unplug/battery sleep the microphone remains off and any running startle is
cancelled. Task counts, heartbeat health and completion state are unchanged. While active it pauses counter
checks and count reactions like a button cue. Native gallery modes 27 (paired)
and 28 (vertical active-only) use a synthetic trigger at 600ms.

## Build and validation

```sh
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 \
  --build-property compiler.optimization_flags=-O2 \
  --build-property "compiler.cpp.extra_flags=-DSTICK_S3_AMBIENT_STARTLE=1" \
  --build-path /tmp/stick-s3-build firmware
```

`tests/test_sound_startle.cpp` covers level measurement/DC removal, quiet room,
clap, warm-up, gradual 54dB rise, soft taps, loud rooms, handling suppression,
refractory/habituation/recovery and the cue's timing, mirroring, bounds and
cancellation. All 27 native suites and 76 Python tests pass.

Physical trigger sensitivity, false positives (speech, typing), codec hand-over
pops and frame timing are unverified. A build without the flag removes the
feature.

Regressions also cover formerly
accepted marginal levels, connected-sleep wake/return, connection-grace offline
semantics and battery/listening/button/task suppression. This verifies logic,
not acoustic triggering or screen behavior. Physical check: with USB
connected and the face asleep, wait at least 1s after handling, clap sharply,
verify open-eye startle and return to sleep; wait at least 5s between claps
(longer after repeated triggers due to habituation). No physical frame timing
was captured and no historical occupancy envelope applies.
