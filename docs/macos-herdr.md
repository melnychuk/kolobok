# Mac / Herdr integration

Use `bridge/macos_herdr_bridge.py` for Herdr sessions (including Pi).
No firmware flash is needed if the Stick already runs the current `S3V1`
binary-framed button firmware. This host supports status and **short clicks only**;
held-button audio is discarded in memory, with no ASR, recording, upload, or input.

## Quick install

```sh
curl -fsSL https://melnychuk.github.io/stick-s3/install.sh | bash
```

The installer runs the read-only check below, then installs a LaunchAgent
(`io.github.melnychuk.stick-s3`, logs in `~/Library/Logs/stick-s3/`). Pass
`--session NAME` for a session other than `default`.

## Check before connecting

1. Connect the Stick with a USB data cable.
2. Identify it with `ioreg -p IOUSB -l -w 0` and inspect `/dev/cu.usbmodem*`.
   Port names can change on replug; do not copy a port from another machine.
3. Check `lsof /dev/cu.usbmodem<port>`: only one bridge may own serial.
4. Run `herdr session list --json` to identify the running session already
   attached in Ghostty's quick terminal. The Mac bridge addresses one explicit session, not a
   global scan of private transcripts or all running servers.
5. Read-only status preflight (does not open USB or change focus):

```sh
python3 bridge/macos_herdr_bridge.py --session default --once
```

`C|active|attention|done[|1]` includes working and blocked agents as active.
Idle tabs are not active. First startup/recovery snapshots establish a completion
baseline rather than replaying old done badges. Snapshot failures pause
heartbeats so the Stick becomes offline instead of showing false idle.
The optional `|1` means Ghostty is frontmost with just one terminal surface,
whose foreground process is Herdr; only Herdr's focused task is considered
viewed. With multiple Ghostty surfaces, visibility is conservatively unknown and
does not acknowledge completions. Ghostty's native JXA scripting API is used,
not System Events keystrokes or Accessibility UI scripting.

## Run the bridge

Stop anything else that owns the Stick's serial port, then:

```sh
python3 bridge/macos_herdr_bridge.py --session default --port /dev/cu.usbmodem<port>
```

To start it at login, wrap the same command in a LaunchAgent with absolute
Python, checkout and Herdr paths. An explicit port waits for that exact path on
reconnect; omit `--port` only when the sole USB serial device is known to be the
Stick. Multiple automatic matches fail closed.

A short A click opens the newest unread completion in the configured session;
subsequent clicks advance through unseen completion versions. If none exist, it
focuses a blocked agent first, then a focused working agent, then another working
agent. The bridge activates Ghostty and focuses the exact surface running Herdr.
Ghostty 1.4's native `focus terminal` action reveals a hidden quick terminal and
keeps an already visible one open: no quick-terminal toggle, new window, or
Herdr.app launch. With no eligible task it reveals that same surface only.
Targets are revalidated against a fresh snapshot; stale targets do not consume
navigation state. It never sends terminal keys, approvals, or prompts. The
Ghostty surface must already be attached to the configured session; this bridge
does not attach/create sessions or switch clients between named sessions.

The sole Ghostty surface with a foreground process named `herdr` is selected
automatically. With multiple Herdr surfaces, supply `--ghostty-terminal <UUID>`
to the bridge; selection otherwise fails closed. Surface UUIDs can
be read without terminal contents:

```sh
osascript -l JavaScript -e 'Application("com.mitchellh.ghostty").terminals().map(t => ({id:t.id(),pid:t.pid()}))'
```

UUIDs last for the surface lifetime, so update an explicit UUID after replacing
that Ghostty terminal. Native scripting requires Ghostty 1.4+ and enabled
`macos-applescript`; macOS may request Automation permission. Failed scripting
never falls back to launching another app or sending keys.

The hardware screen shows aggregate counts/avatar cues, not session titles or a
mirror of the terminal. A hold can still make the existing firmware show its
listening animation, but this Mac host does not transcribe it.

## Herdr Desktop

With `--herdr-source desktop`, the bridge reads Herdr Desktop instead of a
terminal session in Ghostty:

```sh
python3 bridge/macos_herdr_bridge.py --herdr-source desktop --port /dev/cu.usbmodem<port>
```

It counts every agent Desktop runs, whatever the kind, from `herdr-app ls` and
`herdr-app status` (structured layout, never terminal output). Herdr owns seen
state: an agent shows as done exactly while Herdr marks its completion unread,
including badges from before the bridge started. It finds `herdr-app` on `PATH`,
then in Herdr.app under `~/Applications` or `/Applications`; pass an absolute
`--herdr-app` otherwise. A short A click opens the newest unread completion,
then blocked and working agents, as above: the bridge checks the target against
a fresh snapshot, runs Herdr's `agent.focus` and brings Herdr forward with
`open -b` only if it's already running. It never launches Herdr.

Add `--no-usb` to run without a Stick, for example only for
[Kolobok](kolobok.md). An ambiguous USB port no longer stops the bridge: it keeps
publishing status and reports the ambiguity once. Losing the Stick keeps the
bridge's task state, so nothing it shows elsewhere resets.

### Status socket

The bridge publishes the counts it sends the Stick, plus each agent's kind, name
and status (and, from Herdr Desktop, its space, tab and pane name, in Herdr's
order), on a Unix socket for local apps such as Kolobok:
`$(getconf DARWIN_USER_TEMP_DIR)stick-s3/bridge.sock`. Binding creates that one
socket file at startup; every update stays in memory, so there are no per-poll
status files. The directory is `0700` and the socket `0600`, so only your user
can connect, and names never reach the logs. A leftover socket is replaced only
if nobody is listening on it. Apps may ask the bridge to focus an agent; one
focus runs at a time, shared with the Stick's button. Use `--status-socket PATH`
to move it or `--no-status-socket` to turn it off.

### Moving an existing LaunchAgent to Desktop

The installer doesn't offer the Desktop source yet. Edit
`~/Library/LaunchAgents/io.github.melnychuk.stick-s3.plist`: in
`ProgramArguments`, replace `--session` and its value with `--herdr-source` and
`desktop` (drop `--herdr` and `--ghostty-terminal` if present; keep `--port`, or
use `--no-usb`). Check it first with the same arguments plus `--once`, then reload:

```sh
launchctl bootout gui/$(id -u)/io.github.melnychuk.stick-s3
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/io.github.melnychuk.stick-s3.plist
```

## Validation

```sh
python3 -m unittest discover -s tests -p test_macos_herdr_bridge.py
python3 -m unittest discover -s tests -p test_macos_herdr_desktop.py
python3 -m unittest discover -s tests -p test_status_socket.py
python3 -m unittest discover -s tests -p test_herdr_bridge.py
python3 -m unittest discover -s tests -p test_voice.py
```

Tests cover metadata mapping, configurable session selection, baseline
completion tracking, fresh-target validation, focus-only actions, binary/audio
separation, visibility failure and serial cleanup. `--once`
proves snapshot access, not USB delivery. Finally, have the human confirm
working counts and click A after a task finishes to check that the right pane
opens. No synthetic prompts or agent input are needed for this check.
