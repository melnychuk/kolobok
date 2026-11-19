"""Button-path regressions using real framing/selection, mocked OS and app actions."""
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import ExitStack
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

from bridge import herdr_bridge
from bridge.button_action import open_task, parse_target
from bridge.herdr_bridge import TaskState


def frame(kind, payload=b""):
    return b"S3V1" + kind.encode() + len(payload).to_bytes(2, "little") + payload


def task(session, pane, version, status="done", key=None):
    return {"session": session, "pane": pane, "completion": version, "status": status,
            "key": key or session + ":" + pane}


def completed_state():
    state = TaskState(None)
    tasks = [task("cli/agent-dropdown", "w1:p1", 900),
             task("native/quake", "t1", 2, key='["native","/run/quake.sock","t1"]')]
    state.update({"tasks": [{**t, "status": "working", "completion": 0} for t in tasks]})
    state.update({"tasks": [tasks[0], {**tasks[1], "status": "working"}]})
    state.update({"tasks": tasks})
    return state


def simulate(chunks, state, actions, times=None):
    times = times or [index * .1 for index in range(len(chunks))]
    end = max(times) + 1
    with ExitStack() as stack:
        stack.enter_context(patch.object(herdr_bridge, "TaskState", return_value=state))
        context = stack.enter_context(patch.object(herdr_bridge.multiprocessing, "get_context"))
        pool = stack.enter_context(patch.object(herdr_bridge, "ThreadPoolExecutor"))
        pool.return_value.submit.return_value.done.return_value = False
        stack.enter_context(patch.object(herdr_bridge.time, "monotonic", side_effect=[0, *times, end]))
        stack.enter_context(patch.object(herdr_bridge.time, "sleep"))
        stack.enter_context(patch.object(herdr_bridge, "find_port", return_value="fixture"))
        stack.enter_context(patch.object(herdr_bridge, "open_serial", return_value=42))
        stack.enter_context(patch.object(herdr_bridge.select, "select", return_value=([42], [], [])))
        stack.enter_context(patch.object(herdr_bridge.os, "read", side_effect=chunks))
        stack.enter_context(patch.object(herdr_bridge.os, "write", side_effect=lambda fd, payload: len(payload)))
        stack.enter_context(patch.object(herdr_bridge.os, "close"))
        launch = stack.enter_context(patch.object(herdr_bridge.subprocess, "Popen", side_effect=actions))
        herdr_bridge.run(SimpleNamespace(FOCUS_SESSION=None), None,
                         helper_path=Path("/fixture/combined.py"), profile_seconds=end)
    return [call.args[0] for call in launch.call_args_list], context.return_value.Queue.return_value


class ButtonRoutingTests(unittest.TestCase):
    def test_active_clicks_cycle_across_both_apps_and_wrap_around(self):
        state = TaskState(None)
        state.update({"tasks": [task("cli/agent-dropdown", "p1", 0, "working"),
                                 {**task("native/quake", "t1", 4, "working"), "viewed": True},
                                 task("native/quake", "t2", 2, "working")]})
        click = frame("T", b"BUTTON|focus")
        commands, _ = simulate([click] * 4, state, [Mock(poll=Mock(return_value=0)) for _ in range(4)])
        targets = [parse_target(command[1:]) for command in commands]
        self.assertEqual([(t["session"], t["pane"]) for t in targets if t],
                         [("native/quake", "t1"), ("cli/agent-dropdown", "p1"),
                          ("native/quake", "t2"), ("native/quake", "t1")])
        self.assertEqual(state.unread, set())
        self.assertEqual(state.opened, {})

    def test_failed_active_focus_retries_same_target_without_advancing_cycle(self):
        state = TaskState(None)
        state.update({"tasks": [task("native/quake", "t1", 1, "working"),
                                 task("native/quake", "t2", 1, "working")]})
        click = frame("T", b"BUTTON|focus")
        commands, _ = simulate([click, click], state,
                               [Mock(poll=Mock(return_value=1)), Mock(poll=Mock(return_value=0))])
        self.assertEqual(commands[0], commands[1])
        self.assertEqual(state.active_opened, {})

    def test_active_cycle_survives_focus_polls_without_changing_counts(self):
        state = TaskState(None)
        tasks = [task("native/quake", "t1", 1, "blocked"),
                 task("native/quake", "t2", 1, "working"),
                 task("cli/project", "p1", 1, "working")]
        visited = []
        for _ in range(6):
            self.assertEqual(state.update({"tasks": tasks}), b"C|3|1|0\n")
            selected = state.focus_target()
            assert selected is not None
            visited.append(selected["key"])
            state.note_opened(selected)
            tasks = [{**t, "focused": t["key"] == selected["key"],
                      "viewed": t["key"] == selected["key"]} for t in tasks]
        self.assertEqual(len(set(visited[:3])), 3)
        self.assertEqual(visited[:3], visited[3:])
        self.assertEqual(visited[0], "native/quake:t1")  # Blocked leads, but does not starve others.

    def test_new_completion_preempts_but_does_not_reset_active_cycle(self):
        state = TaskState(None)
        first = task("native/quake", "t1", 1, "working")
        second = task("native/quake", "t2", 1, "working")
        completing = task("cli/project", "p1", 1, "working")
        state.update({"tasks": [first, second, completing]})
        state.note_opened(first)
        completed = {**completing, "status": "done", "completion": 2}
        state.update({"tasks": [first, second, completed]})
        self.assertEqual(state.focus_target(), completed)
        state.note_opened(completed)
        self.assertEqual(state.focus_target(), second)
        self.assertEqual(len(state.unread), 1)  # Neither cursor acknowledges completion counts.

    def test_active_cursor_prunes_finished_disappeared_and_failed_sources(self):
        state = TaskState(None)
        first = task("native/quake", "t1", 1, "working")
        second = task("cli/project", "p1", 1, "working")
        state.update({"tasks": [first, second]})
        state.note_opened(first)
        state.note_opened(second)
        state.update({"tasks": [{**first, "status": "idle"}]})
        self.assertEqual(state.active_opened, {})
        state.update({"errors": ["unavailable"]})
        self.assertEqual(state.active_opened, {})
        state.update({"tasks": [second]})
        self.assertEqual(state.focus_target(), second)

    def test_new_input_request_gets_a_fresh_turn_in_active_cycle(self):
        state = TaskState(None)
        first = task("native/quake", "t1", 1, "working")
        second = task("cli/project", "p1", 1, "working")
        state.update({"tasks": [first, second]})
        state.note_opened(first)
        blocked = {**first, "status": "blocked"}
        state.update({"tasks": [blocked, second]})
        self.assertEqual(state.focus_target(), blocked)
        state.note_opened(blocked)
        self.assertEqual(state.focus_target(), second)

    def test_fragmented_frames_and_repeated_clicks_route_across_apps(self):
        state = completed_state()
        click = frame("T", b"BUTTON|focus")
        actions = [Mock(poll=Mock(return_value=0)), Mock(poll=Mock(return_value=0))]
        commands, _ = simulate([click[:5], click[5:], click], state, actions)
        targets = [parse_target(command[1:]) for command in commands]
        self.assertEqual([t["session"] for t in targets if t], ["native/quake", "cli/agent-dropdown"])
        self.assertEqual([t["key"] for t in targets if t],
                         ['["native","/run/quake.sock","t1"]', "cli/agent-dropdown:w1:p1"])
        self.assertEqual(len(state.unread), 2)  # Navigation is not acknowledgement.

    def test_failed_action_does_not_consume_or_cross_route_to_another_task(self):
        state = completed_state()
        click = frame("T", b"BUTTON|focus")
        commands, _ = simulate([click, click], state,
                               [Mock(poll=Mock(return_value=1)), Mock(poll=Mock(return_value=0))])
        self.assertEqual(commands[0], commands[1])
        self.assertEqual(state.opened, {})

    def test_clicks_while_action_is_busy_are_debounced_not_queued(self):
        state = completed_state()
        click = frame("T", b"BUTTON|focus")
        commands, _ = simulate([click + click, click], state, [Mock(poll=Mock(return_value=None))])
        self.assertEqual(len(commands), 1)
        self.assertEqual(state.opened, {})

    def test_recording_ignores_focus_frames_and_only_a_later_click_routes(self):
        state = completed_state()
        click = frame("T", b"BUTTON|focus")
        commands, queue = simulate([frame("B") + click + frame("A", b"\x00\x00") + frame("E"), click],
                                   state, [Mock(poll=Mock(return_value=0))])
        self.assertEqual(len(commands), 1)
        self.assertEqual([call.args[0][0] for call in queue.put_nowait.call_args_list], ["B", "A", "E"])

    def test_hold_release_with_no_click_never_launches_focus(self):
        commands, _ = simulate([frame("B") + frame("E")], completed_state(), [])
        self.assertEqual(commands, [])

    def test_starting_a_hold_cancels_pending_focus_without_consuming_target(self):
        state = completed_state()
        pending = Mock(poll=Mock(return_value=None))
        commands, _ = simulate([frame("T", b"BUTTON|focus"), frame("B") + frame("E")], state, [pending])
        self.assertEqual(len(commands), 1)
        pending.kill.assert_called_once_with()
        self.assertEqual(state.opened, {})

    def test_disconnect_cancels_pending_focus_without_consuming_target(self):
        state = completed_state()
        pending = Mock(poll=Mock(return_value=None))
        simulate([frame("T", b"BUTTON|focus"), b""], state, [pending])
        pending.kill.assert_called_once_with()
        self.assertEqual(state.opened, {})

    def test_unknown_text_frames_do_not_focus(self):
        commands, _ = simulate([frame("T", b"BUTTON|other") + frame("T", b"BUTTON|focus\n")],
                               completed_state(), [])
        self.assertEqual(commands, [])

    def test_timed_out_focus_is_killed_without_consuming_target_then_can_retry(self):
        state = completed_state()
        stuck = Mock(poll=Mock(side_effect=[None, -9]))
        click = frame("T", b"BUTTON|focus")
        commands, _ = simulate([click, frame("T", b"ignored"), click], state,
                               [stuck, Mock(poll=Mock(return_value=0))], times=[0, 13, 13.1])
        stuck.kill.assert_called_once_with()
        self.assertEqual(commands[0], commands[1])
        self.assertEqual(state.opened, {})

    def test_reused_pane_or_reconfigured_native_socket_is_rejected(self):
        for status in ("done", "working"):
            with self.subTest(status=status):
                selected = task("native/quake", "t1", 2, status, key="original-core-terminal")
                changed = {**selected, "key": "different-core-terminal"}
                helper = SimpleNamespace(snapshot=Mock(return_value={"tasks": [changed]}), focus=Mock())
                with self.assertRaises(RuntimeError):
                    open_task(helper, selected)
                helper.focus.assert_not_called()

    def test_no_cached_target_uses_fresh_active_task_not_unrelated_native_fallback(self):
        active = task("cli/project", "p1", 3, "blocked")
        historical = task("native/quake", "t1", 900)
        helper = SimpleNamespace(FOCUS_SESSION=None, snapshot=lambda: {"tasks": [historical, active]},
                                 focus=Mock(), reveal=Mock())
        open_task(helper)
        helper.focus.assert_called_once_with("cli/project", "p1")
        helper.reveal.assert_not_called()

    def test_visible_foreground_active_task_beats_another_instances_internal_focus(self):
        state = TaskState(None)
        foreground = {**task("native/quake", "t1", 2, "working"), "viewed": True}
        hidden = {**task("cli/project", "p1", 3, "working"), "focused": True}
        state.update({"tasks": [hidden, foreground]})
        self.assertEqual(state.focus_target(), foreground)

    def test_real_button_subprocess_preserves_identity_without_live_app_actions(self):
        selected = task("native/quake", "t1", 2, key='["native","fixture core","t1"]')
        with tempfile.TemporaryDirectory() as directory:
            helper = Path(directory) / "helper.py"
            proof = Path(directory) / "focused.json"
            helper.write_text("import json\nfrom pathlib import Path\n"
                              f"def snapshot(): return {{'tasks': [{selected!r}]}}\n"
                              f"def focus(session,pane): Path({str(proof)!r}).write_text(json.dumps([session,pane]))\n")
            command = [sys.executable, str(Path(herdr_bridge.__file__).with_name("button_action.py")),
                       str(helper), selected["session"], selected["pane"], "2", "completed", selected["key"]]
            result = subprocess.run(command, capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(proof.read_text()), ["native/quake", "t1"])
            proof.unlink()
            result = subprocess.run([*command[:-1], "wrong identity"], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 1)
            self.assertFalse(proof.exists())

    def test_legacy_button_arguments_remain_supported(self):
        self.assertIsNone(parse_target(["button.py", "helper.py"]))
        target = parse_target(["button.py", "helper.py", "session", "pane", "42"])
        assert target is not None
        self.assertEqual(target["status"], "idle")
        self.assertNotIn("key", target)
        with self.assertRaises(ValueError):
            parse_target(["button.py", "helper.py", "session", "pane", "42", "invalid"])
