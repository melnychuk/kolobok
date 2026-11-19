#!/usr/bin/env python3
"""Mac Herdr status and short-click USB bridge. No audio capture or agent input."""
import argparse
import json
import os
import select
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

try:
    from .herdr_bridge import TaskState, open_serial
    from .voice import FrameParser
except ImportError:
    from herdr_bridge import TaskState, open_serial
    from voice import FrameParser

APP_ID = "com.mitchellh.ghostty"
GHOSTTY_SCRIPT = '''var g = Application("com.mitchellh.ghostty");
JSON.stringify({frontmost: g.frontmost(), terminals: g.terminals().map(function(t) {
    return {id: t.id(), pid: t.pid()};
})});'''


class MacHerdr:
    def __init__(self, binary, session="default", ghostty_terminal=None):
        self.binary = binary
        self.session = session
        self.ghostty_terminal = ghostty_terminal

    def run(self, argv, timeout=4):
        # launchd must address the configured session, never an inherited pane/socket.
        env = {k: v for k, v in os.environ.items() if not k.startswith("HERDR_")}
        result = subprocess.run(argv, env=env, capture_output=True, text=True, timeout=timeout)
        if result.returncode:
            raise RuntimeError("Host command failed")  # No task content in logs.
        return result.stdout

    def api(self, *args):
        try:
            response = json.loads(self.run([self.binary, "--session", self.session, *args]))
        except ValueError as error:
            raise RuntimeError("Invalid Herdr response") from error
        if "error" in response or "result" not in response:
            raise RuntimeError("Herdr API unavailable")
        return response["result"]

    def ghostty_surface(self):
        try:
            info = json.loads(self.run(["/usr/bin/osascript", "-l", "JavaScript", "-e",
                                        GHOSTTY_SCRIPT], timeout=2))
        except ValueError as error:
            raise RuntimeError("Invalid Ghostty response") from error
        terminals = info["terminals"]
        candidates = []
        for terminal in terminals:
            if self.ghostty_terminal and terminal["id"] != self.ghostty_terminal:
                continue
            pid = int(terminal["pid"])
            if pid <= 0:
                continue
            # Inspect process identity only, never command arguments or transcripts.
            command = self.run(["/bin/ps", "-p", str(pid), "-o", "comm="], timeout=1).strip()
            if Path(command).name == "herdr":
                candidates.append(terminal)
        if len(candidates) != 1:
            raise RuntimeError("Choose the Ghostty Herdr surface with --ghostty-terminal")
        # With multiple surfaces, foreground-app state alone cannot prove which
        # terminal is visible. Fail closed for acknowledgement (focus still works).
        visible = info.get("frontmost") is True and len(terminals) == 1
        return candidates[0]["id"], visible

    def visible(self):
        try:
            return self.ghostty_surface()[1]
        except (OSError, RuntimeError, ValueError, KeyError, subprocess.TimeoutExpired):
            return False  # Unknown visibility cannot acknowledge completions.

    def reveal(self):
        surface, _ = self.ghostty_surface()
        # Ghostty 1.4 focusSurface reveals a hidden quick terminal, without a
        # toggle. Explicit activation is needed by the installed macOS build.
        script = ('var g = Application("com.mitchellh.ghostty"); '
                  'var t = g.terminals.byId(' + json.dumps(surface) + '); '
                  't.id(); g.activate(); g.focus(t);')
        self.run(["/usr/bin/osascript", "-l", "JavaScript", "-e", script])

    def snapshot(self):
        agents = self.api("agent", "list")["agents"]
        visible = self.visible()
        tasks = [{"key": self.session + ":" + a["terminal_id"],
                  "session": self.session, "pane": a["pane_id"],
                  "status": a.get("agent_status", "unknown"),
                  "completion": a.get("completion_seq") or 0,
                  "focused": a.get("focused", False),
                  "viewed": visible and a.get("focused", False)} for a in agents]
        return {"tasks": tasks, "quake_visible": visible}

    def open_task(self, target):
        # A click is focus-only: validate against live state before doing anything.
        snapshot = self.snapshot()
        if target:
            active = target["status"] in {"working", "blocked"}
            live = next((t for t in snapshot["tasks"] if t["key"] == target["key"]
                         and t["pane"] == target["pane"]
                         and ((active and t["status"] in {"working", "blocked"})
                              or (not active and t["status"] in {"idle", "done"}
                                  and t["completion"] == target["completion"]))), None)
            if live is None:
                raise RuntimeError("Selected task changed")
        self.reveal()
        if target:
            self.api("agent", "focus", target["pane"])


def find_port(explicit=None):
    if explicit:
        return explicit if Path(explicit).exists() else None
    ports = sorted(Path("/dev").glob("cu.usbmodem*"))
    if len(ports) > 1:
        raise RuntimeError("Multiple USB serial devices; specify --port")
    return str(ports[0]) if ports else None


def button_frames(parser, chunk):
    # PCM is never decoded as text. Unsupported voice frames are discarded in RAM.
    return any(kind == "T" and data == b"BUTTON|focus"
               for kind, data in parser.feed(chunk))


def run(helper, port=None):
    state = TaskState(focus_session=helper.session)
    parser = FrameParser()
    fd = None
    payload = None
    next_poll = last_write = 0
    poll = action = target = None
    error_reported = False
    pool = ThreadPoolExecutor(max_workers=2)
    try:
        while True:
            now = time.monotonic()
            if action is not None and action.done():
                try:
                    action.result()
                    state.note_opened(target)
                    print("Stick button: focused Herdr", flush=True)
                except Exception:
                    print("Stick button: focus unavailable", file=sys.stderr, flush=True)
                action = target = None
            if poll is not None and poll.done():
                try:
                    payload = state.update(poll.result())
                    error_reported = False
                except Exception:
                    payload = state.update({"errors": ["unavailable"]})
                    if not error_reported:
                        print("Herdr unavailable; heartbeats paused", file=sys.stderr, flush=True)
                        error_reported = True
                poll = None
            if now >= next_poll and poll is None:
                poll = pool.submit(helper.snapshot)
                next_poll = now + 2
            if fd is None:
                try:
                    path = find_port(port)
                    if path:
                        fd = open_serial(path)
                        parser = FrameParser()
                        last_write = 0
                        print(f"Stick connected: {path}", flush=True)
                except OSError:
                    pass
            if fd is not None:
                try:
                    if payload is not None and now - last_write >= 2:
                        if os.write(fd, payload) != len(payload):
                            raise OSError("Partial write")
                        last_write = now
                    if select.select([fd], [], [], 0)[0]:
                        chunk = os.read(fd, 4096)
                        if not chunk:
                            raise OSError("Device disconnected")
                        if button_frames(parser, chunk) and action is None:
                            target = state.focus_target()
                            action = pool.submit(helper.open_task, target)
                except BlockingIOError:
                    pass
                except OSError:
                    os.close(fd)
                    fd = None
                    parser = FrameParser()
                    # Reconnect is a baseline, not a replay of missed completions.
                    state = TaskState(focus_session=helper.session)
                    payload = None
            time.sleep(.01)
    finally:
        if fd is not None:
            os.close(fd)
        pool.shutdown(wait=True, cancel_futures=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--herdr", default=shutil.which("herdr"), help="Absolute Herdr CLI path")
    parser.add_argument("--session", default="default", help="Session already attached in Ghostty")
    parser.add_argument("--ghostty-terminal", help="Ghostty surface UUID when multiple Herdr terminals exist")
    parser.add_argument("--port", help="Confirmed Stick USB serial port")
    parser.add_argument("--once", action="store_true", help="Read status without opening USB or focusing")
    args = parser.parse_args()
    if not args.herdr:
        parser.error("Herdr not found; supply --herdr")
    helper = MacHerdr(args.herdr, args.session, args.ghostty_terminal)
    try:
        if args.once:
            wire = TaskState(focus_session=args.session).update(helper.snapshot())
            if wire is None:
                raise RuntimeError("Snapshot unavailable")
            print(wire.decode(), end="")
        else:
            run(helper, args.port)
    except KeyboardInterrupt:
        pass
    except Exception:
        print("Mac Herdr bridge unavailable; check Herdr and USB configuration",
              file=sys.stderr)
        raise SystemExit(1) from None


if __name__ == "__main__":
    main()
