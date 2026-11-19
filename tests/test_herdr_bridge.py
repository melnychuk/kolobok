import unittest
from unittest.mock import Mock, patch

from bridge import herdr_bridge
from bridge.button_action import open_task
from bridge.herdr_bridge import TaskState


def task(status, completion: int | None = 0, viewed=False):
    return {"key": "session:term", "status": status, "completion": completion,
            "viewed": viewed, "session": "session", "pane": "w1:p1"}


def observe_completion_edges(state, tasks):
    # Synthetic history, not a live task mutation: navigation fixtures must
    # observe actual edges rather than pretend old startup records are unread.
    state.update({"tasks": [{**t, "status": "working", "completion": 0, "viewed": False}
                            if t["status"] in {"idle", "done"} else t for t in tasks]})
    return state.update({"tasks": tasks})


class HerdrBridgeTests(unittest.TestCase):
    def test_timed_capture_returns_without_accessing_serial_when_deadline_is_due(self):
        helper = Mock()
        with patch.object(herdr_bridge.multiprocessing, "get_context"), \
                patch.object(herdr_bridge, "ThreadPoolExecutor") as factory, \
                patch.object(herdr_bridge.time, "monotonic", side_effect=[0, 1]), \
                patch.object(herdr_bridge, "find_port") as find_port:
            herdr_bridge.run(helper, None, profile_seconds=1)
            find_port.assert_not_called()
            helper.snapshot.assert_not_called()
            factory.return_value.shutdown.assert_called_once_with(wait=False, cancel_futures=True)

    def test_timed_capture_closes_its_serial_descriptor(self):
        with patch.object(herdr_bridge.multiprocessing, "get_context"), \
                patch.object(herdr_bridge, "ThreadPoolExecutor"), \
                patch.object(herdr_bridge.time, "monotonic", side_effect=[0, 0, 1]), \
                patch.object(herdr_bridge.time, "sleep"), \
                patch.object(herdr_bridge, "find_port", return_value="fixture"), \
                patch.object(herdr_bridge, "open_serial", return_value=42), \
                patch.object(herdr_bridge.select, "select", return_value=([], [], [])), \
                patch.object(herdr_bridge.os, "close") as close:
            herdr_bridge.run(Mock(), None, profile_seconds=1)
            close.assert_called_once_with(42)

    def test_warmup_excludes_only_timing_and_keeps_voice_events(self):
        sink = Mock()
        sink.accept.return_value = True
        with patch.object(herdr_bridge.multiprocessing, "get_context") as context, \
                patch.object(herdr_bridge, "ThreadPoolExecutor"), \
                patch.object(herdr_bridge.time, "monotonic", side_effect=[0, 1, 6, 7]), \
                patch.object(herdr_bridge.time, "sleep"), \
                patch.object(herdr_bridge, "find_port", return_value="fixture"), \
                patch.object(herdr_bridge, "open_serial", return_value=42), \
                patch.object(herdr_bridge.select, "select", return_value=([42], [], [])), \
                patch.object(herdr_bridge.os, "read", return_value=b"fixture"), \
                patch.object(herdr_bridge.os, "close"), \
                patch.object(herdr_bridge, "FrameParser") as parser:
            parser.return_value.feed.return_value = [("T", b"S3PF-fixture"), ("B", b""), ("E", b"")]
            sink.accept.side_effect = lambda kind, data: kind == "T"
            herdr_bridge.run(Mock(), None, profile_sink=sink, profile_seconds=7, profile_warmup_seconds=5)
            self.assertEqual(sink.accept.call_count, 3)
            self.assertEqual(context.return_value.Queue.return_value.put_nowait.call_count, 4)

    def test_quake_presence_does_not_invent_tasks_or_acknowledge_completions(self):
        state = TaskState()
        self.assertEqual(state.update({"tasks": [], "quake_visible": True}), b"C|0|0|0|1\n")
        self.assertEqual(state.update({"tasks": [], "quake_visible": False}), b"C|0|0|0\n")
        state.update({"tasks": [task("working")]})
        snapshot = {"tasks": [task("idle", 1)], "quake_visible": True}
        self.assertEqual(state.update(snapshot), b"C|0|0|1|1\n")
        self.assertEqual(state.update(snapshot), b"C|0|0|1|1\n")
        self.assertIsNone(state.update({"errors": ["fixture"], "quake_visible": True}))

    def test_visibility_reads_all_monitors_even_without_tasks(self):
        helper = Mock()
        helper.snapshot.return_value = {"tasks": []}
        helper.run.return_value = '[{"specialWorkspace": {}}, {"specialWorkspace": {"name": "special:agent-pane"}}]'
        self.assertTrue(herdr_bridge.snapshot_with_presence(helper)["quake_visible"])
        helper.run.assert_called_once_with(["hyprctl", "-j", "monitors"], timeout=1)
        self.assertNotIn("quake_visible", helper.snapshot.return_value)
        helper.run.return_value = '[{"specialWorkspace": {"name": "special:other"}}]'
        self.assertFalse(herdr_bridge.snapshot_with_presence(helper)["quake_visible"])

    def test_visibility_failure_does_not_break_task_heartbeat_or_latch_awake(self):
        helper = Mock()
        helper.snapshot.return_value = {"tasks": [task("working")]}
        for invalid in ("invalid", "null", "{}", '[{"specialWorkspace": null}]'):
            helper.run.return_value = invalid
            snapshot = herdr_bridge.snapshot_with_presence(helper)
            self.assertFalse(snapshot["quake_visible"])
            self.assertEqual(TaskState().update(snapshot), b"C|1|0|0\n")
        for error in (OSError(), RuntimeError(), herdr_bridge.subprocess.TimeoutExpired("hyprctl", 1)):
            helper.run.side_effect = error
            self.assertFalse(herdr_bridge.snapshot_with_presence(helper)["quake_visible"])

    def test_counts_exclude_idle(self):
        state = TaskState()
        self.assertEqual(state.update({"tasks": [task("idle")]}), b"C|0|0|0\n")
        self.assertEqual(state.update({"tasks": [task("working")]}), b"C|1|0|0\n")
        self.assertEqual(state.update({"tasks": [task("blocked")]}), b"C|1|1|0\n")

    def test_completion_and_viewed(self):
        state = TaskState()
        state.update({"tasks": [task("working")]})
        self.assertEqual(state.update({"tasks": [task("idle", 1)]}), b"C|0|0|1\n")
        self.assertEqual(state.update({"tasks": [task("idle", 1, True)]}), b"C|0|0|0\n")

    def test_restart_baseline_does_not_replay_historical_completions(self):
        active = {**task("working", None), "session": "agent-dropdown", "key": "active"}
        historical = [{**task("idle", seq), "session": "agent-dropdown", "key": f"old-{seq}"}
                      for seq in (10, 20, 30, 40)]
        snapshot = {"tasks": [active, *historical]}
        state = TaskState()
        self.assertEqual(state.update(snapshot), b"C|1|0|0\n")
        self.assertEqual(state.update(snapshot), b"C|1|0|0\n")
        self.assertEqual(state.focus_target(), active)
        # Existing explicit done status is likewise history on first sight.
        self.assertEqual(TaskState().update({"tasks": [task("done", 50)]}), b"C|0|0|0\n")
        # The same idle tab finishing a new run is a genuine new completion.
        new = {**historical[0], "completion": 51}
        self.assertEqual(state.update({"tasks": [active, new, *historical[1:]]}), b"C|1|0|1\n")
        self.assertEqual(state.focus_target(), new)

    def test_recovery_baseline_does_not_resurrect_old_completions(self):
        state = TaskState()
        state.update({"tasks": [task("working")]})
        state.update({"tasks": [task("idle", 10)]})
        self.assertIsNone(state.update({"errors": ["fixture"]}))
        self.assertEqual(state.update({"tasks": [task("idle", 10)]}), b"C|0|0|0\n")

    def test_task_resuming_work_clears_old_unread_badge(self):
        for status, expected in (("working", b"C|1|0|0\n"), ("blocked", b"C|1|1|0\n"),
                                 ("unknown", b"C|0|0|0\n")):
            state = TaskState()
            state.update({"tasks": [task("working")]})
            self.assertEqual(state.update({"tasks": [task("idle", 10)]}), b"C|0|0|1\n")
            self.assertEqual(state.update({"tasks": [task(status, 10)]}), expected)
            self.assertEqual(state.unread, set())

    def test_unknown_not_completed(self):
        state = TaskState()
        state.update({"tasks": [task("working")]})
        self.assertEqual(state.update({"tasks": [task("unknown")]}), b"C|0|0|0\n")

    def test_errors_stop_heartbeat(self):
        self.assertIsNone(TaskState().update({"errors": ["Disconnected"]}))

    def test_focus_latest_unread_completed_in_quake(self):
        state = TaskState()
        observe_completion_edges(state, [task("done", 900),
                       {**task("blocked", 100), "session": "agent-dropdown"},
                       {**task("idle", 20), "session": "agent-dropdown", "key": "old"},
                       {**task("idle", 30, True), "session": "agent-dropdown", "key": "viewed"},
                       {**task("idle", 25), "session": "agent-dropdown", "key": "latest"},
                       {**task("idle", None), "session": "agent-dropdown", "key": "untouched"}])
        target = state.focus_target()
        if target is None:
            self.fail("Expected a completed task")
        self.assertEqual(target["key"], "latest")

    def test_missing_completion_is_not_a_focus_target(self):
        state = TaskState()
        state.tasks = [{**task("unknown"), "session": "agent-dropdown"},
                       {**task("idle", None), "session": "agent-dropdown"}]
        self.assertIsNone(state.focus_target())
        state.update({"tasks": [task("working", None)]})
        self.assertEqual(state.update({"tasks": [task("idle", 1)]}), b"C|0|0|1\n")

    def test_button_resolves_fresh_and_only_focuses_exact_completed_pane(self):
        helper = Mock()
        latest = {**task("idle", 42, True), "session": "agent-dropdown"}
        helper.snapshot.return_value = {"tasks": [latest]}
        open_task(helper, latest)
        helper.focus.assert_called_once_with("agent-dropdown", "w1:p1")
        helper.run.assert_not_called()

    def test_repeated_clicks_advance_before_visibility_poll(self):
        state = TaskState()
        older = {**task("idle", 20), "session": "agent-dropdown", "key": "older"}
        newer = {**task("idle", 30), "session": "agent-dropdown", "key": "newer"}
        snapshot = {"tasks": [older, newer]}
        observe_completion_edges(state, snapshot["tasks"])
        self.assertEqual(state.focus_target(), newer)
        state.note_opened(newer)
        state.update(snapshot)  # Identical heartbeat cannot reset navigation.
        self.assertEqual(state.focus_target(), older)
        state.note_opened(older)
        self.assertIsNone(state.focus_target())
        self.assertEqual(len(state.unread), 2)  # Cursor is not visibility acknowledgement.
        completed_again = {**newer, "completion": 40}
        state.update({"tasks": [older, completed_again]})
        self.assertEqual(state.focus_target(), completed_again)

    def test_serial_clicks_pass_next_target_after_successful_action(self):
        state = TaskState()
        older = {**task("idle", 20), "session": "agent-dropdown", "key": "older", "pane": "w1:p2"}
        newer = {**task("idle", 30), "session": "agent-dropdown", "key": "newer"}
        observe_completion_edges(state, [older, newer])
        with patch.object(herdr_bridge, "TaskState", return_value=state), \
                patch.object(herdr_bridge.multiprocessing, "get_context"), \
                patch.object(herdr_bridge, "ThreadPoolExecutor") as pool, \
                patch.object(herdr_bridge.time, "monotonic", side_effect=[0, 0, .1, .2]), \
                patch.object(herdr_bridge.time, "sleep"), \
                patch.object(herdr_bridge, "find_port", return_value="fixture"), \
                patch.object(herdr_bridge, "open_serial", return_value=42), \
                patch.object(herdr_bridge.select, "select", return_value=([42], [], [])), \
                patch.object(herdr_bridge.os, "read", return_value=b"fixture"), \
                patch.object(herdr_bridge.os, "close"), \
                patch.object(herdr_bridge, "FrameParser") as parser, \
                patch.object(herdr_bridge.subprocess, "Popen") as action:
            pool.return_value.submit.return_value.done.return_value = False
            parser.return_value.feed.return_value = [("T", b"BUTTON|focus")]
            action.return_value.poll.return_value = 0
            action.return_value.returncode = 0
            herdr_bridge.run(Mock(), None, profile_seconds=.2)
            commands = [call.args[0] for call in action.call_args_list]
            self.assertEqual([command[-5:] for command in commands],
                             [["agent-dropdown", "w1:p1", "30", "completed", "newer"],
                              ["agent-dropdown", "w1:p2", "20", "completed", "older"]])

    def test_failed_focus_does_not_consume_unread_target(self):
        state = TaskState()
        target = {**task("idle", 20), "session": "agent-dropdown"}
        observe_completion_edges(state, [target])
        helper = Mock()
        helper.snapshot.return_value = {"tasks": [target]}
        helper.focus.side_effect = RuntimeError("fixture")
        with self.assertRaises(RuntimeError):
            open_task(helper, target)
        self.assertEqual(state.focus_target(), target)

    def test_stale_target_cannot_focus_active_or_new_completion(self):
        target = {**task("idle", 20), "session": "agent-dropdown"}
        for changed in ({**target, "status": "working"}, {**target, "completion": 21}):
            helper = Mock()
            helper.snapshot.return_value = {"tasks": [changed]}
            with self.assertRaises(RuntimeError):
                open_task(helper, target)
            helper.focus.assert_not_called()

    def test_empty_button_reveals_but_never_toggles_visible_quake_closed(self):
        for visible in (False, True):
            helper = Mock()
            helper.snapshot.return_value = {"tasks": []}
            helper.run.return_value = ('[{"specialWorkspace":{"name":"special:agent-pane"}}]'
                                       if visible else '[{}]')
            open_task(helper)
            helper.focus.assert_not_called()
            calls = [call.args[0] for call in helper.run.call_args_list]
            self.assertEqual(calls, [["hyprctl", "-j", "monitors"]] +
                             ([] if visible else [["omarchy-agent-pane"]]))

    def test_active_fallback_and_input_priority_without_completions(self):
        state = TaskState()
        working = {**task("working", None), "session": "agent-dropdown", "key": "working"}
        blocked = {**task("blocked", None), "session": "agent-dropdown", "key": "blocked"}
        state.update({"tasks": [working]})
        self.assertEqual(state.focus_target(), working)
        state.note_opened(working)
        self.assertEqual(state.focus_target(), working)  # Active tasks aren't consumed.
        self.assertEqual(state.opened, {})
        state.update({"tasks": [working, blocked]})
        self.assertEqual(state.focus_target(), blocked)

    def test_completed_navigation_precedes_active_fallback(self):
        state = TaskState()
        working = {**task("working", None), "session": "agent-dropdown", "key": "working"}
        completed = {**task("idle", 20), "session": "agent-dropdown", "key": "completed"}
        observe_completion_edges(state, [working, completed])
        self.assertEqual(state.focus_target(), completed)
        state.note_opened(completed)
        self.assertEqual(state.focus_target(), working)

    def test_active_target_is_validated_against_fresh_state(self):
        target = {**task("working", None), "session": "agent-dropdown"}
        for status in ("working", "blocked", "idle", "unknown"):
            helper = Mock()
            helper.snapshot.return_value = {"tasks": [{**target, "status": status}]}
            if status in {"working", "blocked"}:
                open_task(helper, target)
                helper.focus.assert_called_once_with("agent-dropdown", "w1:p1")
            else:
                with self.assertRaises(RuntimeError):
                    open_task(helper, target)
                helper.focus.assert_not_called()

    def test_active_fallback_stays_in_quake_session(self):
        state = TaskState()
        state.update({"tasks": [task("working"), task("blocked")]})
        self.assertIsNone(state.focus_target())

    def test_button_snapshot_error_fails_closed(self):
        helper = Mock()
        helper.snapshot.return_value = {"errors": ["fixture"]}
        with self.assertRaises(RuntimeError):
            open_task(helper)
        helper.focus.assert_not_called()
        helper.run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
