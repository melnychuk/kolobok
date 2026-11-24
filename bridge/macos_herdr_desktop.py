#!/usr/bin/env python3
"""Herdr Desktop source for the Mac bridge.

Reads the app's structured layout and status (`herdr-app ls`, `status`), never
terminal output, and counts every agent it runs, whatever the kind. Herdr owns
seen state: an agent's status is "done" exactly while it has an unread
completion. The only actions are focusing an agent and raising Herdr.
"""
import json
import os
import shutil
import subprocess
import time
from pathlib import Path

SESSION = "desktop"
BUNDLE_ID = "dev.herdr.app"
APP_BUNDLES = ("~/Applications/Herdr.app", "/Applications/Herdr.app")


class StaleTarget(RuntimeError):
    """The requested agent is gone or no longer in the expected state."""


def names(pane, tab, space, agent):
    """How to show an agent: its space (the project, as Herdr's sidebar groups
    it, plus the machine for remote ones), its tab, a pane name you set, and a
    one-line title for clients that don't group."""
    def text(value):
        return value.strip() if isinstance(value, str) else ""
    name, space_name, tab_name = text(pane.get("name")), text(space.get("name")), text(tab.get("name"))
    machine = text(space.get("machine"))
    group = f"{space_name} on {machine}" if space_name and machine else space_name
    return {"title": name or group or tab_name or agent, "space": group, "tab": tab_name, "name": name}


def name_tabs(tasks):
    """Tell apart agents that share a title (one project, several tabs) by their tab."""
    counts = {}
    for task in tasks:
        counts[task["title"]] = counts.get(task["title"], 0) + 1
    for task in tasks:
        if counts[task["title"]] > 1 and not task["name"] and task["tab"] and task["tab"] != task["title"]:
            task["title"] = f"{task['title']} · {task['tab']}"


def locate(explicit=None):
    """An absolute herdr-app executable: explicit, on PATH, or inside Herdr.app."""
    if explicit:
        path = Path(explicit).expanduser()
        return str(path) if path.is_absolute() and os.access(path, os.X_OK) else None
    found = shutil.which("herdr-app")
    if found:
        return str(Path(found).resolve())
    for bundle in APP_BUNDLES:
        path = Path(bundle).expanduser() / "Contents/MacOS/Herdr"
        if os.access(path, os.X_OK):
            return str(path)
    return None


class MacHerdrDesktop:
    session = SESSION
    source = "desktop"

    def __init__(self, binary):
        self.binary = binary
        self._previous = {}  # key -> status, to see unread edges.
        self._orders = {}    # key -> when its current unread completion was observed.

    def run(self, argv, timeout=4):
        # Never inherit an agent's terminal identity or another core's route.
        env = {k: v for k, v in os.environ.items() if not k.startswith(("HC_", "HERDR_"))}
        result = subprocess.run(argv, env=env, capture_output=True, text=True, timeout=timeout)
        if result.returncode:
            raise RuntimeError("Herdr Desktop command failed")  # No task content in logs.
        return result.stdout

    def app(self, *args):
        try:
            return json.loads(self.run([self.binary, *args]))
        except ValueError as error:
            raise RuntimeError("Invalid Herdr Desktop response") from error

    def snapshot(self):
        try:
            layout = self.app("ls")
            status = self.app("status")
            focused_pane = status.get("focused_pane")
            tasks = []
            seen = set()
            for space in layout["spaces"]:
                for tab in space["tabs"]:
                    for pane in tab["terminals"]:
                        task = self._task(pane, tab, space, focused_pane)
                        # A terminal can appear in more than one place; count it once.
                        if task and task["key"] not in seen:
                            seen.add(task["key"])
                            tasks.append(task)
        except (KeyError, TypeError, AttributeError) as error:
            self._previous, self._orders = {}, {}
            raise RuntimeError("Unexpected Herdr Desktop layout") from error
        except Exception:
            self._previous, self._orders = {}, {}
            raise
        name_tabs(tasks)
        self._previous = {t["key"]: t["status"] for t in tasks}
        self._orders = {key: order for key, order in self._orders.items() if key in self._previous}
        return {"tasks": tasks}

    def _task(self, pane, tab, space, focused_pane):
        agent, terminal = pane.get("app"), pane.get("terminal")
        # Only agents: plain shells have no app.
        if not isinstance(agent, str) or not agent or not isinstance(terminal, str) or not terminal:
            return None
        key = f"{SESSION}:{terminal}"
        status = pane.get("status") or "unknown"
        if status == "done" and self._previous.get(key) != "done":
            self._orders[key] = time.monotonic_ns()  # Newest unread first for the Stick's button.
        return {"key": key, "session": SESSION, "pane": terminal, "status": status,
                "completion": 0, "completion_order": self._orders.get(key, 0),
                "unread": status == "done",
                "focused": focused_pane is not None and pane.get("pane") == focused_pane,
                "viewed": False, "agent": agent, **names(pane, tab, space, agent)}

    def focus_key(self, key):
        """Show one agent, chosen by key, after checking it still exists."""
        task = next((t for t in self.snapshot()["tasks"] if t["key"] == key), None)
        if task is None:
            raise StaleTarget("Agent is gone")
        self._focus(task)

    def open_task(self, target):
        """The Stick's button: the target TaskState chose, if it's still eligible."""
        if target:
            live = next((t for t in self.snapshot()["tasks"] if t["key"] == target["key"]), None)
            active = target["status"] in {"working", "blocked"}
            eligible = live is not None and (
                live["status"] in {"working", "blocked"} if active else live.get("unread") is True)
            if not eligible:
                raise StaleTarget("Selected task changed")
            self._focus(live)
        else:
            self.raise_window()

    def _focus(self, task):
        response = self.app("do", "agent.focus", json.dumps({"terminal": task["pane"]}))
        if not isinstance(response, dict) or "error" in response:
            raise RuntimeError("Herdr Desktop focus failed")
        self.raise_window()

    def raise_window(self):
        # Bring a running Herdr forward; never launch it if it has just quit.
        # lsappinfo and open need no Automation permission.
        if not self.run(["/usr/bin/lsappinfo", "find", f"bundleid={BUNDLE_ID}"], timeout=2).strip():
            raise RuntimeError("Herdr Desktop is not running")
        self.run(["/usr/bin/open", "-b", BUNDLE_ID], timeout=4)
