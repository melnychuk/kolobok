#!/usr/bin/env python3
"""Combine auto-discovered legacy CLI sessions with registered native windows."""
import json
import os
import re
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

try:
    from .herdr_bridge import DEFAULT_HELPER, load_helper, snapshot_with_presence
    from .linux_focus import find_window, show_window
except ImportError:
    from herdr_bridge import DEFAULT_HELPER, load_helper, snapshot_with_presence
    from linux_focus import find_window, show_window

# None opts the bridge into navigation across all routed sessions.
FOCUS_SESSION = None
CONFIG = Path(os.environ.get("STICK_S3_INSTANCES", str(Path.home() / ".config/stick-s3/herdr.json")))
_sources = None


def expanded_path(value):
    if not isinstance(value, str) or not value:
        raise ValueError("Expected a nonempty absolute path")
    path = Path(value).expanduser()
    if not path.is_absolute():
        raise ValueError("Expected an absolute path")
    return path


def load_sources(path):
    """Read explicit native registrations. Importing/probing never launches apps."""
    try:
        config = json.loads(path.read_text())
        if not isinstance(config, dict) or set(config) - {"cli", "cli_helper", "native"}:
            raise ValueError("Invalid instance configuration")
        if not isinstance(config.get("cli", True), bool):
            raise ValueError("cli must be boolean")
        entries = config.get("native", [])
        if not isinstance(entries, list) or len(entries) > 16:
            raise ValueError("Expected up to 16 native instances")
        sources = []
        if config.get("cli", True):
            helper = expanded_path(config.get("cli_helper", str(DEFAULT_HELPER)))
            # Missing dependencies belong to this source, not to other instances.
            sources.append(Source("cli", helper))
        ids = set()
        for entry in entries:
            required = {"id", "home", "session", "app", "core", "workspace", "app_id"}
            if not isinstance(entry, dict) or not required <= entry.keys() or set(entry) - required - {"socket", "reveal"}:
                raise ValueError("Invalid native registration")
            if any(not isinstance(value, str) or not value for value in entry.values()):
                raise ValueError("Native registration values must be nonempty strings")
            name = entry["id"]
            if not re.fullmatch(r"[A-Za-z0-9_.-]+", name) or name in ids:
                raise ValueError("Native ids must be unique and filesystem-safe")
            if not re.fullmatch(r"[A-Za-z0-9_.-]+", entry["session"]):
                raise ValueError("Invalid native session name")
            ids.add(name)
            module = load_helper(Path(__file__).with_name("native_herdr.py"))
            runtime = Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"))
            socket = expanded_path(entry["socket"]) if "socket" in entry else runtime / "herdr-core" / f"{entry['session']}.sock"
            vars(module).update(APP_HOME=expanded_path(entry["home"]),
                                APP=str(expanded_path(entry["app"])),
                                CORE=str(expanded_path(entry["core"])),
                                FOCUS_SESSION=entry["session"], WORKSPACE=entry["workspace"],
                                APP_ID=entry["app_id"], CORE_SOCKET=str(socket), ADDRESS_FOCUS=True)
            if "reveal" in entry:
                vars(module)["REVEAL"] = str(expanded_path(entry["reveal"]))
            sources.append(Source("native/" + name, module=module, socket=str(socket.resolve())))
        if not sources:
            raise ValueError("At least one source is required")
        return sources
    except (OSError, ValueError, TypeError, ImportError) as error:
        # Do not expose configuration contents or private helper stderr in logs.
        raise RuntimeError("Cannot load Stick Herdr instance configuration") from error


class Source:
    def __init__(self, name, helper=None, module=None, socket=None):
        self.name = name
        self.helper = helper
        self.module = module
        self.socket = socket

    def read(self):
        try:
            if self.module is None:
                self.module = load_helper(self.helper)
            snapshot = snapshot_with_presence(self.module)
            tasks = snapshot.get("tasks", [])
            if not isinstance(tasks, list):
                raise ValueError("Invalid task snapshot")
            for task in tasks:
                if any(not isinstance(task[field], str) or not task[field]
                       for field in ("key", "session", "pane", "status")):
                    raise ValueError("Invalid task identity")
                version = task.get("completion")
                if version is not None and (not isinstance(version, int) or version < 0):
                    raise ValueError("Invalid completion version")
            errors = snapshot.get("errors") or []
            if self.name == "cli":
                # Recent Omarchy helpers also query a native core for the bar.
                # Combined mode owns native discovery and address-safe routing.
                native_session = getattr(self.module, "NATIVE_SESSION", None)
                if isinstance(native_session, str) and native_session:
                    tasks = [t for t in tasks if t["session"] != native_session]
                    errors = [e for e in errors if not (
                        isinstance(e, str) and e.startswith(f"{native_session}:"))]
            if self.name == "cli" and any(t["session"] == "agent-dropdown" for t in tasks):
                try:
                    window = find_window(self.module.run, "org.omarchy.agent-pane", "agent-pane")
                    focused = snapshot.get("quake_visible", False) and window.get("focusHistoryID") == 0
                except Exception:
                    focused = False  # Unknown foreground window cannot acknowledge tasks.
                tasks = [{**t, "viewed": bool(focused and t.get("focused")),
                          "focused": bool(focused and t.get("focused"))}
                         if t["session"] == "agent-dropdown" else t for t in tasks]
            errors = bool(errors)
            # The legacy helper reports individual session failures alongside
            # healthy sessions. Keep those healthy tasks, never stale failed ones.
            return {"tasks": tasks, "available": not errors or bool(tasks),
                    "partial": errors and bool(tasks),
                    "quake_visible": snapshot.get("quake_visible", False)}
        except Exception:
            return {"tasks": [], "available": False, "partial": False, "quake_visible": False}

    def routed_task(self, task):
        if self.name == "cli":
            key = ["cli", task["session"], task["key"]]
            session = "cli/" + task["session"]
        else:
            # Same core terminal shown in two registered windows counts once.
            key = ["native", self.socket, task["pane"]]
            session = self.name
        return {**task, "key": json.dumps(key, separators=(",", ":")), "session": session}


def sources():
    global _sources
    if _sources is None:
        _sources = load_sources(CONFIG)
    return _sources


def snapshot():
    try:
        configured = sources()
    except RuntimeError:
        return {"tasks": [], "errors": ["Herdr instance configuration unavailable"], "quake_visible": False}
    with ThreadPoolExecutor(max_workers=min(8, len(configured))) as pool:
        results = list(pool.map(lambda source: source.read(), configured))
    tasks = {}
    health = []
    for source, result in zip(configured, results, strict=True):
        health.append({"source": source.name, "available": result["available"],
                       "partial": result["partial"], "tasks": len(result["tasks"])})
        for raw in result["tasks"]:
            task = source.routed_task(raw)
            old = tasks.get(task["key"])
            # Prefer the actually viewed window when duplicate native views exist.
            if old is None or task.get("viewed") or (task.get("focused") and not old.get("viewed")):
                tasks[task["key"]] = task
    available = any(r["available"] for r in results)
    return {"tasks": list(tasks.values()), "sources": health,
            "errors": [] if available else ["No Herdr source available"],
            "quake_visible": any(r["available"] and r["quake_visible"] for r in results)}


def focus(session, pane):
    for source in sources():
        legacy = source.name == "cli" and session.startswith("cli/")
        if not legacy and source.name != session:
            continue
        if source.module is None:
            source.read()
        if source.module is None:
            raise RuntimeError("Herdr instance unavailable")
        original_session = session[4:] if legacy else source.module.FOCUS_SESSION
        if legacy and original_session == "agent-dropdown":
            # Never call the desktop toggle/launcher: it can reveal the wrong
            # window on another monitor, or start a missing dropdown.
            window = find_window(source.module.run, "org.omarchy.agent-pane", "agent-pane")
            try:
                result = json.loads(source.module.run(["herdr", "--session", original_session,
                                                       "agent", "focus", pane]))
                if not isinstance(result, dict) or "error" in result:
                    raise RuntimeError("CLI Herdr focus failed")
            except ValueError as error:
                raise RuntimeError("CLI Herdr focus unconfirmed") from error
            show_window(source.module.run, "org.omarchy.agent-pane", "agent-pane",
                        expected_address=window["address"])
        else:
            source.module.focus(original_session, pane)
        return
    raise RuntimeError("Herdr instance is no longer configured")


def reveal():
    # No eligible task: show an already-running registered native instance.
    # Never invoke a launcher for an unavailable source.
    configured = sources()
    for source in configured:
        if source.name != "cli" and source.read()["available"]:
            source.module.reveal()
            return
    for source in configured:
        if source.name == "cli" and source.read()["available"]:
            try:
                window = find_window(source.module.run, "org.omarchy.agent-pane", "agent-pane")
            except RuntimeError:
                return  # No unique existing dropdown: never launch/guess one.
            show_window(source.module.run, "org.omarchy.agent-pane", "agent-pane",
                        expected_address=window["address"])
            return
    raise RuntimeError("No running Herdr instance to reveal")


if __name__ == "__main__":
    report = snapshot()
    print(json.dumps({"sources": report.get("sources", []),
                      "errors": report["errors"],
                      "task_count": len(report["tasks"]),
                      "quake_visible": report["quake_visible"]}, indent=2))
    raise SystemExit(1 if report["errors"] else 0)
