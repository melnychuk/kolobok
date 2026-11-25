#!/usr/bin/env python3
"""Mac Herdr status and short-click USB bridge. No audio capture or agent input."""
import argparse
import json
import os
import select
import shutil
import signal
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

try:
    from .herdr_bridge import TaskState, open_serial
    from .macos_herdr_desktop import MacHerdrDesktop, StaleTarget
    from .macos_herdr_desktop import locate as locate_desktop
    from .status_socket import StatusServer
    from .status_socket import Unavailable as StatusUnavailable
    from .voice import FrameParser
except ImportError:
    from herdr_bridge import TaskState, open_serial
    from macos_herdr_desktop import MacHerdrDesktop, StaleTarget
    from macos_herdr_desktop import locate as locate_desktop
    from status_socket import StatusServer
    from status_socket import Unavailable as StatusUnavailable
    from voice import FrameParser

APP_ID = "com.mitchellh.ghostty"
# Without a good snapshot for this long, status expires, as it does on the Stick.
STALE_AFTER = 8
GHOSTTY_SCRIPT = '''var g = Application("com.mitchellh.ghostty");
JSON.stringify({frontmost: g.frontmost(), terminals: g.terminals().map(function(t) {
    return {id: t.id(), pid: t.pid()};
})});'''


class MacHerdr:
    source = "terminal"

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
                  "viewed": visible and a.get("focused", False),
                  **({"agent": a["agent"]} if isinstance(a.get("agent"), str) and a["agent"] else {})}
                 for a in agents]
        return {"tasks": tasks, "quake_visible": visible}

    def focus_key(self, key):
        """Show one agent, chosen by key, after checking it still exists."""
        live = next((t for t in self.snapshot()["tasks"] if t["key"] == key), None)
        if live is None:
            raise StaleTarget("Agent is gone")
        self.reveal()
        self.api("agent", "focus", live["pane"])

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
                raise StaleTarget("Selected task changed")
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


def status_message(state, online, source):
    """What the Stick is told, plus one row per agent, for apps on the status socket.
    Herdr Desktop also says where each agent is (space, tab, pane name) so apps
    can group them; agents come in Herdr's own order."""
    tasks = state.tasks if online else []
    return {"v": 1, "type": "status", "online": online, "source": source,
            "active": sum(t["status"] in {"working", "blocked"} for t in tasks),
            "blocked": sum(t["status"] == "blocked" for t in tasks),
            "done": sum(t["key"] in state.unread for t in tasks),
            "agents": [{"key": t["key"], "agent": t.get("agent") or "agent",
                        "title": t.get("title") or t.get("agent") or "Agent",
                        "status": t["status"], "unread": t["key"] in state.unread,
                        **{field: t[field] for field in ("space", "tab", "name")
                           if isinstance(t.get(field), str)}} for t in tasks]}


def run(helper, port=None, *, usb=True, server=None):
    # Task state belongs to the Herdr source, not the USB link: losing the
    # Stick must not wipe what the status socket's apps are showing.
    state = TaskState(focus_session=helper.session)
    parser = FrameParser()
    fd = None
    payload = None
    last_good = None
    next_poll = last_write = 0
    poll = action = target = reply_to = None
    error_reported = port_reported = False
    requests = []
    pool = ThreadPoolExecutor(max_workers=2)
    if server is not None:
        server.on_focus = lambda client, request_id, key: requests.append((client, request_id, key))
    try:
        while True:
            now = time.monotonic()
            if action is not None and action.done():
                outcome = "ok"
                try:
                    action.result()
                except StaleTarget:
                    outcome = "stale"
                except Exception:
                    outcome = "failed"
                if reply_to is None:
                    if outcome == "ok":
                        state.note_opened(target)
                        print("Stick button: focused Herdr", flush=True)
                    else:
                        print("Stick button: focus unavailable", file=sys.stderr, flush=True)
                elif server is not None:
                    client, request_id = reply_to
                    server.reply(client, {"id": request_id, "ok": outcome == "ok",
                                          **({} if outcome == "ok" else {"error": outcome})})
                action = target = reply_to = None
            # One focus at a time, whether it comes from the Stick or an app.
            for client, request_id, key in requests:
                if action is None:
                    reply_to = (client, request_id)
                    action = pool.submit(helper.focus_key, key)
                elif server is not None:
                    server.reply(client, {"id": request_id, "ok": False, "error": "busy"})
            requests.clear()
            if poll is not None and poll.done():
                try:
                    payload = state.update(poll.result())
                    last_good = now if payload is not None else None
                    error_reported = False
                except Exception:
                    payload = state.update({"errors": ["unavailable"]})
                    last_good = None
                    if not error_reported:
                        print("Herdr unavailable; heartbeats paused", file=sys.stderr, flush=True)
                        error_reported = True
                poll = None
            if now >= next_poll and poll is None:
                poll = pool.submit(helper.snapshot)
                next_poll = now + 2
            fresh = payload is not None and last_good is not None and now - last_good <= STALE_AFTER
            if server is not None:
                server.publish(status_message(state, fresh, helper.source))
                server.poll()
            if usb and fd is None:
                try:
                    path = find_port(port)
                    if path:
                        fd = open_serial(path)
                        parser = FrameParser()
                        last_write = 0
                        print(f"Stick connected: {path}", flush=True)
                except RuntimeError:
                    # Ambiguous ports: keep serving the status socket, never guess.
                    if not port_reported:
                        print("Multiple USB serial devices; specify --port", file=sys.stderr, flush=True)
                        port_reported = True
                except OSError:
                    pass
            if fd is not None:
                try:
                    if fresh and payload is not None and now - last_write >= 2:
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
            time.sleep(.01)
    finally:
        if fd is not None:
            os.close(fd)
        pool.shutdown(wait=True, cancel_futures=True)


def make_helper(parser, args):
    if args.herdr_source == "desktop":
        binary = locate_desktop(args.herdr_app)
        if not binary:
            parser.error("Herdr Desktop not found; supply an absolute --herdr-app")
        return MacHerdrDesktop(binary)
    if not args.herdr:
        parser.error("Herdr not found; supply --herdr")
    return MacHerdr(args.herdr, args.session, args.ghostty_terminal)


def open_status_server(args):
    if args.no_status_socket:
        return None
    try:
        return StatusServer(args.status_socket)
    except (StatusUnavailable, OSError):
        # The Stick still works without it; apps such as Kolobok show the bridge as missing.
        print("Status socket unavailable; apps won't see this bridge", file=sys.stderr, flush=True)
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--herdr-source", choices=("terminal", "desktop"), default="terminal",
                        help="Read the terminal Herdr in Ghostty, or Herdr Desktop")
    parser.add_argument("--herdr", default=shutil.which("herdr"), help="Absolute Herdr CLI path (terminal)")
    parser.add_argument("--herdr-app", help="Absolute herdr-app path (desktop; default: PATH, then Herdr.app)")
    parser.add_argument("--session", default="default", help="Session already attached in Ghostty (terminal)")
    parser.add_argument("--ghostty-terminal", help="Ghostty surface UUID when multiple Herdr terminals exist")
    parser.add_argument("--port", help="Confirmed Stick USB serial port")
    parser.add_argument("--no-usb", action="store_true", help="Never open a serial port (status socket only)")
    parser.add_argument("--status-socket", help="Unix socket for apps such as Kolobok (default: per-user temp dir)")
    parser.add_argument("--no-status-socket", action="store_true", help="Don't publish status to apps")
    parser.add_argument("--once", action="store_true", help="Read status without opening USB or focusing")
    args = parser.parse_args()
    if args.no_usb and args.port:
        parser.error("--port and --no-usb are exclusive")
    helper = make_helper(parser, args)
    # launchd stops agents with SIGTERM: unwind normally so the socket is removed.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    server = None
    try:
        if args.once:
            wire = TaskState(focus_session=helper.session).update(helper.snapshot())
            if wire is None:
                raise RuntimeError("Snapshot unavailable")
            print(wire.decode(), end="")
        else:
            server = open_status_server(args)
            run(helper, args.port, usb=not args.no_usb, server=server)
    except KeyboardInterrupt:
        pass
    except Exception:
        print("Mac Herdr bridge unavailable; check Herdr and USB configuration",
              file=sys.stderr)
        raise SystemExit(1) from None
    finally:
        if server is not None:
            server.close()


if __name__ == "__main__":
    main()
