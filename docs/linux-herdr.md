# Linux / Herdr bridge

`bridge/herdr_bridge.py` turns Herdr agent state into the Stick's USB status and
handles its button and push-to-talk frames. It reads task snapshots through a
**Herdr task helper** (`herdr_tasks.py`, shipped with the Omarchy Agents panel
plugin), the same snapshots, identities and focused-task visibility the desktop
bar uses. There is no independent transcript or Copilot event-log scanner.

## Requirements

- Linux with a systemd user manager (for running it as a service).
- Python 3.10+ with `venv`/pip.
- Herdr, plus the Agents panel plugin that provides `herdr_tasks.py` and the
  dictation composer (`voiceStart` IPC, see [Voice frames](#voice-frames)).
- A StickS3 running this repository's firmware.

## Configure

| Setting | Purpose |
|---|---|
| `STICK_S3_HERDR_HELPER` or `--helper` | Path to the installed `herdr_tasks.py` (default `~/.config/stick-s3/herdr_tasks.py`). |
| `STICK_S3_AGENTS_PLUGIN` | Plugin id that hosts the dictation composer (`omarchy-shell <id> voiceStart`). Without it, recordings are ignored. |
| `--port` | Serial port; by default the single `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*-if00`. |

## Set up

The quickest way is the installer, which does the steps below for you
(`--dictation` adds the speech model, `--help` lists options):

```sh
curl -fsSL https://melnychuk.github.io/stick-s3/install.sh | bash
```

By hand:

```sh
python3 -m venv .venv
.venv/bin/pip install sherpa-onnx==1.13.8 numpy==2.5.3
export STICK_S3_HERDR_HELPER=/path/to/herdr_tasks.py
.venv/bin/python bridge/herdr_bridge.py --once   # prints C|... without opening USB
.venv/bin/python bridge/herdr_bridge.py          # runs the bridge
```

The voice worker imports numpy even when no dictation is in progress. To start
the bridge at login, a minimal user unit
(`~/.config/systemd/user/stick-s3-herdr.service`) looks like this; use absolute
paths for your checkout and helper:

```ini
[Unit]
Description=Stick S3 Herdr USB status bridge

[Service]
Type=simple
Environment="STICK_S3_HERDR_HELPER=/path/to/herdr_tasks.py"
Environment="STICK_S3_AGENTS_PLUGIN=your.agents-plugin-id"
ExecStart="/path/to/stick-s3/.venv/bin/python" "/path/to/stick-s3/bridge/herdr_bridge.py"
Restart=on-failure
RestartSec=3
NoNewPrivileges=true

[Install]
WantedBy=default.target
```

Then `systemctl --user daemon-reload && systemctl --user enable --now
stick-s3-herdr.service`. Restarting establishes a new completion baseline; old
unread completion state is not persisted. Only one process may own the serial
port: do not run a flasher or another bridge at the same time.

## USB status

The firmware wire protocol is `C|active|attention|done[|quake-visible]\n`.
The optional presence field is `1` while Hyprland shows `special:agent-pane` on
any monitor; otherwise the bridge omits it. The bridge checks visibility on its
two-second background snapshot poll, with a one-second Hyprland timeout. Unknown
visibility clears presence without breaking task heartbeats. Presence is
display-only: it does not invent tasks or acknowledge unread completions.

Active includes working and blocked agents, not idle tabs. For legacy CLI tasks,
completion edges are tracked locally by the bridge; a viewed focused task clears
its completion. Native Herdr can instead provide authoritative unread badges.
The first snapshot after startup/recovery is a baseline: a historical
`completion_seq` on an idle/done tab does not imply unreadness and must not be
replayed as a new completion. Only observed working-to-ready or increasing
ready-completion sequence edges add unread badges; resuming work, blocked/unknown
status, disappearance, or visible focus clears the old badge. The tracker is
process-local: it cannot reconstruct completions missed while stopped. Snapshot
errors stop heartbeats so the device goes offline instead of displaying a false
idle state. The firmware may present one completion's two halves (active fall,
then done rise, or the reverse) up to 4.6s late so they reach the transfer cue
together; see [split completions](avatar-task-cues.md#split-completions-two-polls--firmware--header).

Unplugging the Stick from the host (USB start-of-frame activity lost for 1.2s)
drops the bridge at once and the avatar falls asleep, dimming the backlight.
After boot or a replug it stays asleep for up to 10s while the bridge reconnects,
then shows offline if no heartbeat arrives. A plugged Stick whose bridge stops
heartbeats shows offline after 8s, never sleep (`firmware/usb_presence.h`). On
battery, once the eyes have closed and the PMIC reports no USB power, the panel
sleeps and the ESP32-S3 light-sleeps; front A or side B wakes it for a short peek
(`firmware/power_nap.h`). While unplugged, buttons only wake the face.

## Buttons

The front A button's short click advances through unread completed tasks in
`agent-dropdown`, newest first, revealing/focusing the quake view through the
same exact-pane helper as the bar. Selection uses the bridge's unread set and
Herdr's session-wide `completion_seq`; viewed tasks and untouched idle tabs are
excluded. A successful open records a completion-version cursor immediately, so
the next click advances even before the two-second visibility poll; this cursor
does not acknowledge unread counts. `bridge/button_action.py` validates the
selected completion against a fresh snapshot outside the serial/audio loop; a
vanished/working/changed target fails closed and is not consumed. With no unread
completion left, it opens an active task in the quake session (blocked first,
then a focused working task) without consuming it. If neither exists, it reveals
quake only, without toggling it closed. No approvals or agent input are sent.

Holding A for 300ms is dictation, never also a short click.

## Combined CLI and native instances (recommended)

`bridge/combined_herdr.py` watches **both** the legacy CLI sessions discovered by
the installed Omarchy helper and an explicit list of native app/core instances.
It does not scan private logs or guess native app homes/sockets. Polling only
reads existing instances; it does not start/restart apps, cores, or tasks.

Copy [the example configuration](herdr-instances.example.json) to
`~/.config/stick-s3/herdr.json`, adjusting it to match your installation:

```sh
mkdir -p ~/.config/stick-s3
# Only copy if no existing configuration needs to be preserved:
cp -n docs/herdr-instances.example.json ~/.config/stick-s3/herdr.json
.venv/bin/python -m bridge.combined_herdr
.venv/bin/python bridge/herdr_bridge.py --helper bridge/combined_herdr.py --once
```

The first check prints only per-source health and task counts, not task content.
The second prints the USB status without opening the serial device. Registered
native instances may be closed; healthy sources keep working independently.

Configuration fields:

- `cli` (default `true`): auto-discover all running legacy CLI sessions on each
  poll. Set `false` for native-only operation.
- `cli_helper` (optional): absolute path (or `~/...`) to the legacy CLI helper;
  by default `STICK_S3_HERDR_HELPER` (see [Configure](#configure)).
- `native`: up to 16 registrations. Each requires a unique `id`, `home`,
  `session`, `app`, `core`, `workspace`, and `app_id`, as in the example.
  Paths must be absolute or `~/...`. Each app home/core route is independent.
- A native registration can additionally specify an absolute `socket`; otherwise
  it uses `$XDG_RUNTIME_DIR/herdr-core/<session>.sock`.
- Combined mode reveals existing windows directly by their configured `app_id`
  and `workspace`, verifies their exact address and foreground focus, and never
  calls a native launcher. An optional legacy `reveal` path is still accepted for
  configuration compatibility but is not invoked in combined mode. The standalone
  native adapter retains its custom `STICK_S3_NATIVE_REVEAL` contract.
- Native visibility/focus currently supports local Hyprland special-workspace
  dropdowns, not arbitrary desktop windows or remote cores. Use distinct window
  classes per instance; missing, ambiguous, or wrong-workspace windows fail closed.

To activate combined mode, create or update the **same** service override used
for single-native mode,
`~/.config/systemd/user/stick-s3-herdr.service.d/native.conf`:

```ini
[Service]
ExecStart=
ExecStart="/home/YOU/Dev/stick-s3/.venv/bin/python" "/home/YOU/Dev/stick-s3/bridge/herdr_bridge.py" --helper "/home/YOU/Dev/stick-s3/bridge/combined_herdr.py"
```

Use your actual absolute checkout paths. Run `systemctl --user daemon-reload`
and `systemctl --user restart stick-s3-herdr.service`. Restart only the Stick
bridge, never Herdr. The filename is retained from the earlier native-only
configuration so two competing ExecStart overrides are not installed. Native
registrations are loaded once per bridge process; restart the bridge after
editing them. `STICK_S3_INSTANCES` can select another absolute config path in the
service environment. The single-native `STICK_S3_NATIVE_*` defaults do not override
explicit registrations.

Behavior in combined mode:

- Counts include agents across all discovered CLI sessions and registered native
  windows. Native records emitted by the Omarchy helper are ignored in combined
  mode, so only the dedicated native adapter counts and routes them. Native shell
  tabs and hidden workers are excluded. The same native core terminal shown in
  two registrations counts once.
- Short A opens the latest **observed** unread completion across instances. With
  none left, successive successful presses cycle through active tasks across both
  apps, least recently opened first, wrapping after every active task has a turn.
  Initially unvisited blocked tasks lead, followed by foreground/focused working
  tasks; a new input request gets a fresh turn. Visibility polling does not reset
  the active cursor. Failed/cancelled focus does not advance it. The active cursor
  is separate from unread navigation: it never changes task counts or acknowledges
  unread completions, and old idle/history-only tabs are not added to the cycle.
  A fresh snapshot validates the exact routed session, pane and stable task key
  before focus: a reused pane ID or reassigned core must not become another target.
  If the background poll had no target, the action can select newly active work
  from its fresh snapshot, without replaying historical completions. Legacy
  non-dropdown sessions use the existing helper to open a terminal client attached
  to that session; this does not restart its agents.
- Versions are never treated as globally comparable. Completion edges first seen
  in the same poll have only a deterministic, best-effort order. Only legacy
  sources without authoritative badges need a startup/recovery baseline.
- An unavailable source is omitted, not retained with stale counts. Other healthy
  sources keep updating. A recovered legacy source establishes a new baseline;
  a recovered native app supplies its current badges. Partial CLI session failures
  retain healthy tasks.
  If every source fails, heartbeats stop and the device goes offline. A successful
  CLI scan with no tasks is still a healthy, empty source: the device shows idle
  if the only native instance is closed/unavailable. Check per-source health to
  distinguish partial coverage; the USB protocol has no partial-error indicator.
- If no eligible task exists, short A reveals the first reachable registered
  native window, or an already-running legacy dropdown. It does not intentionally
  launch a missing window. CLI-dropdown and native routes use separate exact
  window classes; visibility is checked on all monitors so an already-shown
  dropdown is not toggled closed. A visible CLI dropdown only acknowledges its
  focused task while that window actually has keyboard focus.
- Clicks received while a focus action is busy are deliberately debounced, not
  queued for delayed app switching. A stalled action has a 12-second deadline;
  only the bridge's own helper is killed, never a Herdr app/core/agent. Failed or
  cancelled actions do not consume the completion navigation cursor.
- Holding A (300ms in the existing firmware) is dictation, never also a short
  click. The host ignores focus frames during recording and cancels any pending
  focus helper on recording start or serial disconnect. A focus already dispatched
  to the compositor cannot be undone. Dictation stays in the Agents popup; Enter there still creates a legacy task, not a native-app task.

Regression tests (no live Herdr mutations or serial access):

```sh
.venv/bin/python -m unittest discover -s tests -p test_combined_herdr.py
.venv/bin/python -m unittest discover -s tests -p test_button_routing.py
.venv/bin/python -m unittest discover -s tests -p test_linux_focus.py
```

The routing tests cover real serial framing and child-process argument handling
with synthetic tasks and mocked app/desktop actions. They do not prove physical
button timing, microphone operation, or real compositor focus. Afterwards,
manually short-click A and check the intended app/pane; hold/release A to check
that only the reviewable voice popup opens.

## Native Linux Herdr app

For **single-native-only** operation, the native GPUI app uses `herdr-core` (`hc`), not the legacy `herdr` TUI's
session API. Select the repository adapter explicitly; the Agents panel helper is still
used by the bar and dictation:

```sh
.venv/bin/python bridge/herdr_bridge.py --helper bridge/native_herdr.py --once
```

Standalone defaults (override the workspace if your window lives elsewhere):

- App: `~/.local/opt/herdr-quake/app/herdr-app`
- Core: `~/.local/opt/herdr-quake/core/herdr-core`
- App home: `~/.config/herdr-quake`; core session: `quake-native`
- Socket: `$XDG_RUNTIME_DIR/herdr-core/quake-native.sock`
- Window class: `org.omarchy.herdr-quake`; workspace: `special:herdr-native`
- Reveal command: `~/.local/bin/omarchy-herdr-native-pane show`

Only live agents in the app's layout are counted. The app's pane status is the
source of truth for unread badges, including badges already present at startup
and acknowledgements made in Herdr before the bridge polls. Raw core status can
remain `done` after acknowledgement; core `turn` is used for navigation versioning,
not to infer unreadness when the app provides status. Older apps that omit the
pane status field fall back to observed completion edges and visible focused-pane
acknowledgement.

Short A uses unread/active priority, focuses the exact terminal through
`agent.focus`, and shows (never toggles closed) the native window. Completions are
ordered by observation, because core turn numbers are not globally comparable.
App/core snapshot failure stops standalone heartbeats, and a missing core socket
fails closed rather than auto-starting a core.

To switch an existing bridge service, create
`~/.config/systemd/user/stick-s3-herdr.service.d/native.conf` with absolute paths
(adjust the checkout location):

```ini
[Service]
ExecStart=
ExecStart="/home/YOU/Dev/stick-s3/.venv/bin/python" "/home/YOU/Dev/stick-s3/bridge/herdr_bridge.py" --helper "/home/YOU/Dev/stick-s3/bridge/native_herdr.py"
```

Then run `systemctl --user daemon-reload` and
`systemctl --user restart stick-s3-herdr.service`. Only restart the Stick bridge,
never the Herdr app or its core. The override survives reinstalling the bridge
unit; remove this specific override and reload/restart to return to legacy mode.
Restarting clears process-local unread history.

For other native installations, set `STICK_S3_NATIVE_APP`,
`STICK_S3_NATIVE_CORE`, `STICK_S3_NATIVE_HOME`, `STICK_S3_NATIVE_SESSION`,
`STICK_S3_NATIVE_WORKSPACE`, `STICK_S3_NATIVE_APP_ID`, and
`STICK_S3_NATIVE_REVEAL` as needed in the service's `Environment=` entries.
`STICK_S3_NATIVE_SOCKET` optionally supplies an explicit socket path.
The reveal setting is one executable path, invoked with `show`; it must reveal
the same configured native app/session. This adapter currently expects a
Hyprland special-workspace dropdown, not generic desktop-window visibility.

**Dictation is unchanged:** hold A still uses local CPU ASR and the Agents
draft popup. This is not native-app draft integration and does
not send text or keystrokes to a native terminal. Enter in that popup retains
its existing legacy task-creation behavior. No audio leaves the machine.

## Device permissions

A `Serial unavailable: ... Permission denied` log means the service is running
but cannot open USB. Identify **your** device, rather than copying another
machine's serial or assuming `/dev/ttyACM0`:

```sh
ls -l /dev/serial/by-id/
udevadm info --query=property --name=/dev/ttyACM0
getfacl /dev/ttyACM0
```

After confirming vendor `303a`, product `1001`, and the device serial, create
`/etc/udev/rules.d/70-stick-s3.rules` through normal administrator authorization.
Replace `YOUR_DEVICE_SERIAL` with the exact `ID_SERIAL_SHORT` value:

```udev
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", ATTRS{serial}=="YOUR_DEVICE_SERIAL", TAG+="uaccess"
```

The `70-` prefix matters: the tag must be set before the system's
`73-seat-late.rules` applies desktop-session ACLs. Then reload and replug, or
apply the rule to the confirmed tty only:

```sh
sudo udevadm control --reload-rules
sudo udevadm trigger --action=change /sys/class/tty/ttyACM0
sudo udevadm settle
getfacl /dev/ttyACM0
systemctl --user restart stick-s3-herdr.service
```

Adjust the tty path to match your device. Verify a read/write ACL for the active
local desktop user and check access survives a replug. `uaccess` relies on an
active local logind seat; an SSH-only/headless session needs a separately reviewed
access policy. Do not make the port world-writable or grant access to every
serial device. A one-off `setfacl` grant alone does not survive reconnection.

## Local English ASR (CPU only)

`bridge/local_asr.py` loads the NVIDIA Nemotron English 0.6B INT8 ONNX export via
Sherpa-ONNX on the CPU. Missing files fail closed: there is no download or cloud
fallback at runtime. Put the four model files in
`~/.local/share/stick-s3/asr/nemotron-en-int8/`, from Hugging Face repository
`csukuangfj/sherpa-onnx-nemotron-speech-streaming-en-0.6b-int8-2026-01-14` at
revision `f13b0c6a48186fdd9fdd8d203b9527b0b709b09f` (about 632 MiB):

| File | SHA-256 |
|---|---|
| `encoder.int8.onnx` | `2f6ae81fe4ccd69ef04cdf048ecd49628e2d3148a6195e152a91b4d2497952dc` |
| `decoder.int8.onnx` | `1fb1795cb46e7d0e99b2e096eae83f7e324294e895975a1a894b0384cbbe37f6` |
| `joiner.int8.onnx` | `a3f41dccc0f67f37e4210051d1c39a29d473c841cfc32fe574135bac890db91d` |
| `tokens.txt` | `dc0b4584ab2e4ddbf888425c076c61b736e7356a015250db7d307e6f1a8188ff` |

```sh
.venv/bin/python bridge/local_asr.py /path/to/recording.wav
```

Input must be 16 kHz, mono, 16-bit PCM WAV, at most 60 seconds. The underlying
model is NVIDIA's `nvidia/nemotron-speech-streaming-en-0.6b`; review its license
before redistribution.

## Voice frames

Microphone capture is mono PCM16 at 16kHz, 80ms blocks, maximum 60 seconds. The
firmware also turns each block into 10ms loudness levels for the soundwave eyes;
they never leave the device (`firmware/listen_wave.h`). Speaker output is stopped
during recording. USB frames use `S3V1`, one-byte kind, two-byte little-endian
length, then payload: B begin, A audio, E end, X abort, T text/button. Status
input stays `C|...`.

`bridge/voice.py` decodes PCM in a separate CPU-only worker. Bounded queues,
frame lengths, stream timeout and maximum duration limit memory use. Audio is
held only in memory, not written to disk or sent over the network. Transcript
updates are written mode 0600 into the user's runtime directory
(`$XDG_RUNTIME_DIR/stick-s3-voice.json`), not logged.

Holding A asks the Agents panel (`STICK_S3_AGENTS_PLUGIN`) to open its minimal
dictation popup. Transcription fills and grows the input; release leaves
editable text for review. Enter starts a task; no speech automatically submits.
An existing draft causes a new recording to be rejected rather than overwritten.

## Tests

```sh
# Bridge tests need no device, Pillow or C++ compiler.
.venv/bin/python -m unittest discover -s tests -p 'test_*herdr*.py'
.venv/bin/python -m unittest discover -s tests -p test_voice.py
.venv/bin/python -m unittest discover -s tests -p test_button_routing.py
```

Builds with `-DSTICK_S3_AMBIENT_STARTLE=1` keep the microphone open while idle so
sudden loud noises startle the avatar; only an on-device block level is analyzed.
See [Loud-noise startle](avatar-startle.md). Opt-in frame profiling is documented
in [Physical frame profiling](frame-profiling.md).
