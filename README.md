# Copilot Stick S3

A tiny desk buddy for your coding agents: an M5Stack StickS3 that shows when
your GitHub Copilot CLI and Herdr agents are working, need you, or are done.
Live demo with the real firmware renderer compiled to WebAssembly:
<https://melnychuk.github.io/stick-s3/>.

- **Blue, left:** active tasks, as Roman numerals. A new one drops in from the top.
- **Green, right:** unread completions. A finished task charges, bursts and flips across.
- **Orange-yellow glow:** an agent needs you. The avatar peeks up from below a
  bobbing `!`, hops a little, and plays one polite C6→G6 chime.
- **Front key:** short press opens the next unread or waiting task; hold to dictate
  into your agent with local, CPU-only speech recognition. While you talk, the eyes
  become a soundwave that follows your voice.
- Idle, it naps. Turned over, the IMU flips the face upright.

## Layout

| Path | What |
|---|---|
| `firmware/` | Arduino sketch and the shared, portable rendering/acting kernels (`*.h`). |
| `bridge/` | Host bridges: Linux Herdr (`herdr_bridge.py`, combined CLI + native instances), macOS Herdr, voice/ASR, frame profiling. |
| `site/` | The product page; `scripts/build-site-wasm.sh` compiles the renderer for it. |
| `tests/` | Native C++ and Python tests, plus the native preview renderer. |
| `experiments/qemu-sticks3/` | QEMU peripheral prototype and the USB/LCD transport timing model. |
| `experiments/avatar-widget/` | Command-driven compositor shared by the site and desktop header. |
| `docs/` | Design notes, bridge guides and performance research. |

## Install

Easiest: paste this into your coding agent (Copilot CLI, Pi, …). The
[agent guide](site/install.md) tells it how to check your machine, ask before
flashing or using sudo, run the installer and verify the Stick with you:

```text
Set up my Copilot Stick S3 using https://melnychuk.github.io/stick-s3/install.md
```

Or, on Linux or macOS, one line installs the bridge as a login service (no sudo):

```sh
curl -fsSL https://melnychuk.github.io/stick-s3/install.sh | bash
```

Add options after `bash -s --`: `--flash` also flashes the latest prebuilt
firmware (it asks first), `--flash-only` skips the bridge, `--dictation` enables
Linux push-to-talk with local CPU speech recognition, `--uninstall` removes the
service, `--help` lists the rest. The script is [`site/install.sh`](site/install.sh).

## Build and flash the firmware yourself

Arduino CLI with the M5Stack ESP32 core:

```sh
arduino-cli core install m5stack:esp32 \
  --additional-urls https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
arduino-cli lib install M5Unified
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 \
  --build-property compiler.optimization_flags=-O2 firmware
arduino-cli upload --fqbn m5stack:esp32:m5stack_sticks3 --port <your-port> firmware
```

If the upload will not start, hold the side power key for about two seconds until
the green LED blinks, then retry. Optional: add
`--build-property "compiler.cpp.extra_flags=-DSTICK_S3_AMBIENT_STARTLE=1"` for the
[loud-noise startle](docs/avatar-startle.md).

## Run the bridge

- **Linux / Herdr:** [docs/linux-herdr.md](docs/linux-herdr.md): status, focus,
  combined CLI and native Herdr instances, and local push-to-talk dictation.
- **macOS / Herdr:** [docs/macos-herdr.md](docs/macos-herdr.md): status and
  short-press focus (no dictation).

The bridge sends only aggregate counts (`C|active|attention|done`) over USB serial.
Audio stays on your machine: no cloud ASR, and nothing is submitted without you
pressing Enter.

## Develop

```sh
# Native suites (example), Python tests, and a GIF/contact-sheet preview gallery
g++ -std=c++11 -O2 -Wall -Wextra -Werror tests/test_listen_wave.cpp -o /tmp/t && /tmp/t
python3 -m unittest discover -s tests
python3 -m pip install -r scripts/requirements-preview.txt
python3 scripts/preview-avatar.py
```

Design and behaviour are documented in [avatar design](docs/avatar-design.md),
[task cues](docs/avatar-task-cues.md) and
[render performance](docs/render-performance.md).

## Credits and license

The robot eyes started from [FluxGarage RoboEyes](https://github.com/FluxGarage/RoboEyes),
which drew the first prototype's eyes. They were then rebuilt from scratch and
extended here: authored acting, a soft-body face, fluid edges, Roman task
counters and the cues around them. No RoboEyes code is included.

MIT licensed; see [LICENSE](LICENSE). The generated display fonts in
`firmware/display_fonts.h` derive from DejaVu Sans and keep their own
[license](docs/dejavu-font-license.txt).
