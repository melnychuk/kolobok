#!/usr/bin/env python3
"""Explicit short-button action; never sends input or submits a task."""
import json
import sys

try:
    from .herdr_bridge import TaskState, load_helper
except ImportError:
    from herdr_bridge import TaskState, load_helper


def open_task(helper, target=None):
    snapshot = helper.snapshot()
    if snapshot.get("errors"):
        raise RuntimeError("Herdr snapshot unavailable")
    if target:
        # Validate the requested kind explicitly: stale completion navigation
        # must never silently become an active-task jump (or vice versa).
        active = target.get("status") in {"working", "blocked"}
        live = next((t for t in snapshot.get("tasks", [])
                     if t["session"] == target["session"] and t["pane"] == target["pane"]
                     and ("key" not in target or t["key"] == target["key"])
                     and ((active and t["status"] in {"working", "blocked"})
                          or (not active and t["status"] in {"idle", "done"}
                              and t.get("unread", True) is True
                              and (t.get("completion") or 0) == target["completion"]))), None)
        if not live:
            raise RuntimeError("Selected task is no longer available")
        helper.focus(live["session"], live["pane"])
        return
    # The bridge's cursor may already have visited the remaining unread badges.
    # With no selected target, only consider newly active work before revealing.
    current = TaskState(focus_session=vars(helper).get("FOCUS_SESSION", "agent-dropdown"))
    current.update(snapshot)
    active_target = current.active_target()
    if active_target:
        helper.focus(active_target["session"], active_target["pane"])
        return
    reveal = vars(helper).get("reveal")
    if reveal is not None:
        reveal()
        return
    try:
        monitors = json.loads(helper.run(["hyprctl", "-j", "monitors"]))
    except (ValueError, OSError, RuntimeError) as error:
        raise RuntimeError("Cannot determine quake visibility") from error
    visible = any(m.get("specialWorkspace", {}).get("name") == "special:agent-pane"
                  for m in monitors)
    if not visible:
        helper.run(["omarchy-agent-pane"])


def parse_target(argv):
    """Keep old standalone arguments compatible; new hosts also pass stable keys."""
    if len(argv) in {5, 6, 7}:
        kind = argv[5] if len(argv) >= 6 else "completed"
        if kind not in {"completed", "active"}:
            raise ValueError("Invalid button target kind")
        try:
            completion = int(argv[4])
        except ValueError as error:
            raise ValueError("Invalid button completion version") from error
        if completion < 0:
            raise ValueError("Invalid button completion version")
        target = {"session": argv[2], "pane": argv[3], "completion": completion,
                  "status": "working" if kind == "active" else "idle"}
        if len(argv) == 7:
            target["key"] = argv[6]
        return target
    if len(argv) != 2:
        raise ValueError("Invalid button action arguments")
    return None


if __name__ == "__main__":
    try:
        target = parse_target(sys.argv)
        open_task(load_helper(sys.argv[1]), target)
    except Exception:
        # No task content or private helper errors in persistent logs.
        print("Stick button: could not open selected task", file=sys.stderr)
        raise SystemExit(1) from None
