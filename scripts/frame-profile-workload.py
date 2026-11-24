"""Scripted Herdr helper for A/B frame captures (bridge --helper). Never real tasks.

Every 24 seconds: one task arrives (0-5s), completes into an unread done
counter (the transfer cue, then the done-only rest and its checks, 5-12s),
two more tasks join it (paired counters, 12-18s), then everything clears
(idle, 18-24s). Both builds of a comparison then render the same states in the
same proportions; live captures depend on whatever agents happen to do.

  bridge/herdr_bridge.py --helper scripts/frame-profile-workload.py --port PORT \
      --profile-output frames.jsonl --profile-seconds 72 --profile-warmup-seconds 5
"""
import time

FOCUS_SESSION = None  # Navigate across all (synthetic) sessions.
_start = time.monotonic()


def _task(key, status, unread=False):
    return {"key": key, "session": "profile", "pane": key, "status": status, "completion": 1,
            "completion_order": 0, "focused": False, "viewed": False, "unread": unread}


def snapshot():
    t = (time.monotonic() - _start) % 24
    if t < 5:
        tasks = [_task("a", "working")]
    elif t < 12:
        tasks = [_task("a", "done", True)]
    elif t < 18:
        tasks = [_task("a", "done", True), _task("b", "working"), _task("c", "working")]
    else:
        tasks = []
    return {"tasks": tasks, "quake_visible": False}


def run(args, timeout=1):
    raise RuntimeError("The scripted workload runs no external commands")
