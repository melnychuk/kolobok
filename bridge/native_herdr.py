#!/usr/bin/env python3
"""Native Herdr adapter. Reads structured app/core state, never terminal output."""
import json
import os
import subprocess
import time
from pathlib import Path

try:
    from .linux_focus import find_window, show_window
except ImportError:
    from linux_focus import find_window, show_window

FOCUS_SESSION = os.environ.get("STICK_S3_NATIVE_SESSION", "quake-native")
APP_HOME = Path(os.environ.get("STICK_S3_NATIVE_HOME", str(Path.home() / ".config/herdr-quake")))
PREFIX = Path.home() / ".local/opt/herdr-quake"
APP = os.environ.get("STICK_S3_NATIVE_APP", str(PREFIX / "app/herdr-app"))
CORE = os.environ.get("STICK_S3_NATIVE_CORE", str(PREFIX / "core/herdr-core"))
WORKSPACE = os.environ.get("STICK_S3_NATIVE_WORKSPACE", "herdr-native")
APP_ID = os.environ.get("STICK_S3_NATIVE_APP_ID", "org.omarchy.herdr-quake")
REVEAL = os.environ.get("STICK_S3_NATIVE_REVEAL", str(Path.home() / ".local/bin/omarchy-herdr-native-pane"))
CORE_SOCKET = os.environ.get("STICK_S3_NATIVE_SOCKET")
ADDRESS_FOCUS = False  # Combined mode never delegates focus to a launcher.
_previous = {}
_orders = {}


def run(argv, timeout=4):
    # Never inherit the calling agent's terminal identity or another core's route.
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("HC_", "HERDR_"))}
    runtime = Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"))
    env.update(HERDR_APP_HOME=str(APP_HOME), HC_SESSION=FOCUS_SESSION,
               HC_SOCKET=str(CORE_SOCKET or runtime / "herdr-core" / f"{FOCUS_SESSION}.sock"))
    if argv[0] == CORE and not Path(env["HC_SOCKET"]).is_socket():
        raise RuntimeError("Native core is not running")
    result = subprocess.run(argv, env=env, capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError("Native Herdr command failed")
    return result.stdout


def visibility():
    """Unknown compositor state must never acknowledge a completion."""
    try:
        monitors = json.loads(run(["hyprctl", "-j", "monitors"], timeout=1))
        clients = json.loads(run(["hyprctl", "-j", "clients"], timeout=1))
        matches = [c for c in clients if c["class"] == APP_ID]
        if len(matches) != 1:
            return False, False  # Never guess which same-class window was viewed.
        client = matches[0]
        workspace = client.get("workspace", {}).get("name")
        visible = (not client.get("hidden", False) and workspace == f"special:{WORKSPACE}"
                   and any(m.get("specialWorkspace", {}).get("name") == workspace for m in monitors))
        return visible, visible and client.get("focusHistoryID") == 0
    except (ValueError, KeyError, TypeError, AttributeError, OSError, RuntimeError,
            subprocess.TimeoutExpired):
        return False, False


def snapshot():
    global _previous, _orders
    try:
        layout = json.loads(run([APP, "ls"]))
        status = json.loads(run([APP, "status"]))
        terminals = json.loads(run([CORE, "ls", "--json"]))["terminals"]
        by_id = {t["address"]: t for t in terminals if t["state"] == "live"}
        visible, window_focused = visibility()
        tasks = []
        for space in layout["spaces"]:
            for tab in space["tabs"]:
                for pane in tab["terminals"]:
                    # Only agents shown by this window, not shells or hidden workers.
                    terminal = pane["terminal"]
                    core = by_id.get(terminal)
                    if not pane.get("app") or core is None:
                        continue
                    # The app applies the shared seen state; the core's raw
                    # "done" can linger after Herdr has cleared the badge.
                    authoritative = "status" in pane
                    state = pane.get("status", core.get("status")) or "unknown"
                    version = core.get("turn", 0)
                    key = f"{FOCUS_SESSION}:{terminal}"
                    old = _previous.get(key)
                    if authoritative:
                        completed = state == "done" and (
                            old is None or old[0] != "done" or version > old[1])
                    else:
                        completed = old and state in {"idle", "done"} and (
                            old[0] == "working" or version > old[1])
                    if completed:
                        _orders[key] = time.monotonic_ns()
                    focused = status.get("focused_pane") == pane["pane"]
                    task = {"key": key, "session": FOCUS_SESSION, "pane": terminal,
                            "status": state, "completion": version,
                            "completion_order": _orders.get(key, 0),
                            "focused": focused, "viewed": window_focused and focused}
                    if authoritative:
                        task["unread"] = state == "done"
                    tasks.append(task)
        _previous = {t["key"]: (t["status"], t["completion"]) for t in tasks}
        _orders = {t["key"]: t["completion_order"] for t in tasks}
        return {"tasks": tasks, "errors": [], "quake_visible": visible}
    except (ValueError, KeyError, TypeError, AttributeError, OSError, RuntimeError, subprocess.TimeoutExpired):
        _previous = {}
        _orders = {}
        # Do not put task content or command stderr in service logs.
        return {"tasks": [], "errors": ["Native Herdr snapshot unavailable"], "quake_visible": False}


def reveal(expected_address=None):
    if ADDRESS_FOCUS:
        show_window(run, APP_ID, WORKSPACE, expected_address=expected_address)
    else:
        # Preserve the standalone adapter's explicit custom-reveal contract.
        run([REVEAL, "show"], timeout=30)


def focus(session, terminal):
    if session != FOCUS_SESSION:
        raise RuntimeError("Wrong native Herdr session")
    window = find_window(run, APP_ID, WORKSPACE) if ADDRESS_FOCUS else None
    live = snapshot()
    if live["errors"] or not any(t["pane"] == terminal for t in live["tasks"]):
        raise RuntimeError("Native task is no longer available")
    try:
        response = json.loads(run([APP, "do", "agent.focus", "--terminal", terminal]))
        if not isinstance(response, dict) or "error" in response:
            raise RuntimeError("Native Herdr focus failed")
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        raise RuntimeError("Native Herdr focus unconfirmed") from error
    if window is not None:
        reveal(expected_address=window["address"])
    else:
        reveal()
