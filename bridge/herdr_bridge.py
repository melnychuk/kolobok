#!/usr/bin/env python3
"""Linux USB adapter for the Omarchy Herdr task helper. No transcript scanning."""
import argparse
import importlib.util
import json
import multiprocessing
import os
import queue
import select
import subprocess
import sys
import termios
import time
from concurrent.futures import ThreadPoolExecutor
from contextlib import suppress
from pathlib import Path

try:
    from .frame_profile import ProfileSink
    from .voice import FrameParser, asr_worker
except ImportError:
    from frame_profile import ProfileSink
    from voice import FrameParser, asr_worker

# The Herdr task helper (herdr_tasks.py) shipped with the desktop Agents panel
# plugin. Point --helper or STICK_S3_HERDR_HELPER at the installed copy.
DEFAULT_HELPER = Path(os.environ.get("STICK_S3_HERDR_HELPER", "~/.config/stick-s3/herdr_tasks.py")).expanduser()


def load_helper(path):
    name = f"{__package__}.herdr_tasks" if __package__ else "herdr_tasks"
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ImportError("Cannot load the configured Herdr helper")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TaskState:
    def __init__(self, focus_session: str | None = "agent-dropdown"):
        self.focus_session = focus_session
        self.previous = {}
        self.unread = set()
        self.tasks = []
        self.opened = {}  # Navigation cursor by completion version, not task acknowledgement.
        self.observed_order = {}
        self.generation = 0
        self.active_opened = {}  # Separate round-robin cursor; never acknowledges unread work.
        self.active_sequence = 0

    def note_opened(self, target):
        if target and target["status"] in {"idle", "done"}:
            self.opened[target["key"]] = target.get("completion") or 0
        elif target and target["status"] in {"working", "blocked"}:
            self.active_sequence += 1
            self.active_opened[target["key"]] = self.active_sequence

    def update(self, snapshot):
        if snapshot.get("errors"):
            self.previous = {}
            self.unread.clear()
            self.opened.clear()
            self.observed_order.clear()
            self.active_opened.clear()
            self.tasks = []
            return None  # Stop heartbeats: device must show offline, not false idle.
        tasks = snapshot.get("tasks", [])
        self.generation += 1
        present = {t["key"] for t in tasks}
        self.unread.intersection_update(present)
        for task in tasks:
            old = self.previous.get(task["key"])
            if old and task["status"] == "blocked" and old["status"] != "blocked":
                self.active_opened.pop(task["key"], None)  # A new input request gets a fresh turn.
            key = task["key"]
            version = task.get("completion") or 0
            old_version = (old.get("completion") or 0) if old else 0
            ready = task["status"] in {"idle", "done"}
            if "unread" in task:
                # Native Herdr owns acknowledgement, including marks made
                # by other clients and badges that predate this bridge.
                if ready and task["unread"] is True:
                    if key not in self.unread or version > old_version:
                        self.observed_order[key] = self.generation
                    self.unread.add(key)
                else:
                    self.unread.discard(key)
                    self.opened.pop(key, None)  # Can be marked unread again at the same turn.
                continue
            # Legacy sequences record history, not unreadness. Only observed
            # transitions after the baseline can create a legacy badge.
            if old and ready and (old["status"] == "working" or version > old_version):
                self.unread.add(key)
                self.observed_order[key] = self.generation
            if not ready or task.get("viewed"):
                self.unread.discard(key)
        self.opened = {key: version for key, version in self.opened.items() if key in present}
        self.observed_order = {key: order for key, order in self.observed_order.items() if key in present}
        active_keys = {t["key"] for t in tasks if t["status"] in {"working", "blocked"}}
        self.active_opened = {key: order for key, order in self.active_opened.items() if key in active_keys}
        self.previous = {t["key"]: t for t in tasks}
        self.tasks = tasks
        working = sum(t["status"] == "working" for t in tasks)
        blocked = sum(t["status"] == "blocked" for t in tasks)
        # Optional presence field: old firmware ignores it; old hosts omit it.
        presence = "|1" if snapshot.get("quake_visible") is True else ""
        return f"C|{working + blocked}|{blocked}|{len(self.unread)}{presence}\n".encode()

    def focus_target(self):
        # Sequence is monotonic within a Herdr session, not across sessions.
        # Navigate unread completions newest-first. Remember successful opens
        # immediately so a second click need not wait for the visibility poll.
        candidates = [t for t in self.tasks
                      if (self.focus_session is None or t["session"] == self.focus_session)
                      and t["status"] in {"idle", "done"} and t["key"] in self.unread
                      and (t.get("unread") is True or (
                          not t.get("viewed") and (t.get("completion") or 0) > 0))
                      and self.opened.get(t["key"]) != (t.get("completion") or 0)]
        # Across instances, versions are incomparable. Prefer the latest observed
        # poll; simultaneous edges have only a deterministic, best-effort order.
        completed = max(candidates, key=lambda t: (
            self.observed_order.get(t["key"], 0) if self.focus_session is None else 0,
            t["session"] if self.focus_session is None else "",
            t.get("completion_order", t.get("completion") or 0), t["key"]), default=None)
        return completed or self.active_target()

    def active_target(self):
        # Rotate through active tasks, least recently opened first.
        # Unvisited/input-request/foreground tasks lead each round.
        # Only a successful action advances this cursor, not visibility polling.
        active = [t for t in self.tasks
                  if (self.focus_session is None or t["session"] == self.focus_session)
                  and t["status"] in {"working", "blocked"}]
        return min(active, key=lambda t: (self.active_opened.get(t["key"], 0),
                                         t["status"] != "blocked",
                                         not t.get("viewed", False),
                                         not t.get("focused", False), t["key"]), default=None)


def snapshot_with_presence(helper):
    """Read quake visibility even with no agents; never acknowledge tasks here."""
    snapshot = dict(helper.snapshot())
    if "quake_visible" in snapshot:
        return snapshot  # Native helpers resolve their own window/workspace.
    try:
        monitors = json.loads(helper.run(["hyprctl", "-j", "monitors"], timeout=1))
        snapshot["quake_visible"] = any(
            m.get("specialWorkspace", {}).get("name") == "special:agent-pane"
            for m in monitors)
    except (ValueError, TypeError, AttributeError, OSError, RuntimeError,
            subprocess.TimeoutExpired):
        snapshot["quake_visible"] = False
    return snapshot


def find_port(explicit=None):
    if explicit:
        return str(explicit) if Path(explicit).exists() else None
    matches = sorted(Path("/dev/serial/by-id").glob("usb-Espressif_USB_JTAG_serial_debug_unit_*-if00"))
    if len(matches) > 1:
        raise RuntimeError("Multiple Espressif devices; configure --port explicitly")
    return str(matches[0]) if matches else None


def open_serial(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attrs = termios.tcgetattr(fd)
        attrs[0] = attrs[1] = attrs[3] = 0
        attrs[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
        attrs[4] = attrs[5] = termios.B115200
        attrs[6][termios.VMIN] = attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        return fd
    except Exception:
        os.close(fd)
        raise


def run(helper, port, helper_path=DEFAULT_HELPER, profile_sink: ProfileSink | None = None,
        profile_seconds: float | None = None, profile_warmup_seconds: float = 0):
    state = TaskState(focus_session=getattr(helper, "FOCUS_SESSION", "agent-dropdown"))
    fd = None
    parser = FrameParser()
    context = multiprocessing.get_context("spawn")
    voice_events = context.Queue(maxsize=128)
    worker = context.Process(target=asr_worker, args=(voice_events,
        Path.home() / ".local/share/stick-s3/asr/nemotron-en-int8"), daemon=True)
    worker.start()
    pool = ThreadPoolExecutor(max_workers=1)
    poll_future = None
    voice_active = False
    voice_overflow = False
    next_poll = 0
    last_error = ""
    action = None
    action_target = None
    action_started = 0
    action_cancelled = False
    payload = None
    last_write = 0
    started = time.monotonic()

    def cancel_action():
        nonlocal action_cancelled, action_target
        if action is not None and not action_cancelled:
            # Only our short-lived focus helper, never an app/core/agent.
            with suppress(OSError):
                action.kill()
            action_cancelled = True
            action_target = None

    while True:
        now = time.monotonic()
        if profile_seconds is not None and now - started >= profile_seconds and not voice_active:
            cancel_action()
            if fd is not None:
                os.close(fd)
            pool.shutdown(wait=False, cancel_futures=True)
            return
        if action:
            result = action.poll()
            if result is not None:
                if result == 0 and not action_cancelled:
                    state.note_opened(action_target)
                action = None
                action_target = None
            elif not action_cancelled and now - action_started >= 12:
                cancel_action()
                print("Stick button: focus action timed out", file=sys.stderr, flush=True)
        if poll_future is not None and poll_future.done():
            try:
                payload = state.update(poll_future.result())
            except Exception as error:
                payload = None
                if str(error) != last_error:
                    print(f"Herdr unavailable: {error}", file=sys.stderr, flush=True)
                    last_error = str(error)
            poll_future = None
        if now >= next_poll and poll_future is None:
            poll_future = pool.submit(snapshot_with_presence, helper)
            next_poll = now + 2
        if voice_overflow:
            try:
                voice_events.put_nowait(("X", b""))
                voice_overflow = False
            except queue.Full:
                pass
        if fd is None:
            try:
                path = find_port(port)
                if path:
                    fd = open_serial(path)
                    parser = FrameParser()
                    print("Stick connected", flush=True)
                    last_error = ""
            except (OSError, RuntimeError) as error:
                if str(error) != last_error:
                    print(f"Serial unavailable: {error}", file=sys.stderr, flush=True)
                    last_error = str(error)
        if fd is not None:
            try:
                if payload is not None and now - last_write >= 2:
                    if os.write(fd, payload) != len(payload):
                        raise OSError("Partial serial write")
                    last_write = now
                if select.select([fd], [], [], 0)[0]:
                    chunk = os.read(fd, 4096)
                    if not chunk:
                        raise OSError("Device disconnected")
                    for kind, data in parser.feed(chunk):
                        if (profile_sink is not None and now - started >= profile_warmup_seconds
                                and profile_sink.accept(kind, data)):
                            continue
                        if kind in "BAEX":
                            if kind == "B":
                                cancel_action()
                                voice_active = True
                            elif kind == "A" and not voice_active:
                                continue
                            if kind in "EX":
                                voice_active = False
                            if not voice_overflow:
                                try:
                                    voice_events.put_nowait((kind, data))
                                except queue.Full:
                                    voice_active = False
                                    voice_overflow = True
                            continue
                        if (kind == "T" and data == b"BUTTON|focus"
                                and not voice_active and action is None):
                            # Validate/focus outside the serial/audio loop. Keep the
                            # unread cursor in this process across repeated clicks.
                            action_target = state.focus_target()
                            command = [sys.executable,
                                       str(Path(__file__).with_name("button_action.py")),
                                       str(helper_path)]
                            if action_target:
                                command += [action_target["session"], action_target["pane"],
                                            str(action_target.get("completion") or 0),
                                            "active" if action_target["status"] in {"working", "blocked"}
                                            else "completed", action_target["key"]]
                            action = subprocess.Popen(command, stdout=subprocess.DEVNULL)
                            action_started = now
                            action_cancelled = False
            except BlockingIOError:
                pass
            except OSError:
                cancel_action()
                os.close(fd)
                fd = None
                parser = FrameParser()
                if voice_active:
                    voice_active = False
                    voice_overflow = True
        time.sleep(0.01)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", type=Path, default=DEFAULT_HELPER)
    parser.add_argument("--port")
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--profile-output", type=Path, help="New timing-only JSONL file; opt-in, no raw serial/audio")
    parser.add_argument("--profile-limit", type=int, default=1800, help="Maximum timing records, 1..10000")
    parser.add_argument("--profile-seconds", type=float, help="Gracefully end a timing capture after 1..600 seconds")
    parser.add_argument("--profile-warmup-seconds", type=float, default=0,
                        help="Exclude 0..60 startup seconds from timing capture only")
    args = parser.parse_args()
    if not 1 <= args.profile_limit <= 10000:
        parser.error("--profile-limit must be in 1..10000")
    if args.once and args.profile_output:
        parser.error("--profile-output requires the running bridge")
    if args.profile_seconds is not None and (not args.profile_output or not 1 <= args.profile_seconds <= 600):
        parser.error("--profile-seconds requires --profile-output and a duration in 1..600")
    if (not 0 <= args.profile_warmup_seconds <= 60 or
            (args.profile_warmup_seconds and (not args.profile_output or args.once)) or
            (args.profile_seconds is not None and args.profile_warmup_seconds >= args.profile_seconds)):
        parser.error("Timing warmup requires capture and must end before its deadline")
    helper = load_helper(args.helper)
    if args.once:
        snapshot = snapshot_with_presence(helper)
        wire = TaskState().update(snapshot)
        if wire is None:
            print(json.dumps({"errors": snapshot.get("errors")}), file=sys.stderr)
            raise SystemExit(1)
        print(wire.decode(), end="")
    else:
        sink = ProfileSink(args.profile_output, args.profile_limit) if args.profile_output else None
        try:
            run(helper, args.port, args.helper, sink, args.profile_seconds, args.profile_warmup_seconds)
        except KeyboardInterrupt:
            pass
        finally:
            if sink is not None:
                sink.close()
                if sink.error:
                    print(f"Timing capture failed: {sink.error}", file=sys.stderr)
