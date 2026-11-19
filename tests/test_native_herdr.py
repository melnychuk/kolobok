import json
import os
import unittest
from types import SimpleNamespace
from unittest.mock import patch

from bridge import native_herdr as native
from bridge.button_action import open_task
from bridge.herdr_bridge import TaskState, snapshot_with_presence


class NativeHerdrTests(unittest.TestCase):
    def setUp(self):
        native._previous = {}
        native._orders = {}
        self.core = {"terminals": [{"address": "t1", "state": "live", "status": "working", "turn": 1},
                                    {"address": "t2", "state": "live", "status": "idle", "turn": 9},
                                    {"address": "hidden", "state": "live", "status": "working", "turn": 1}]}
        self.layout = {"spaces": [{"tabs": [{"terminals": [
            {"terminal": "t1", "pane": "p1", "app": "pi"},
            {"terminal": "t2", "pane": "p2", "app": "pi"},
            {"terminal": "shell", "pane": "p3", "app": None}]}]}]}
        self.status = {"focused_pane": "p1"}
        self.visible = (True, True)

    def reply(self, argv, timeout=4):
        if argv == [native.APP, "ls"]:
            return json.dumps(self.layout)
        if argv == [native.APP, "status"]:
            return json.dumps(self.status)
        if argv == [native.CORE, "ls", "--json"]:
            return json.dumps(self.core)
        raise AssertionError(argv)

    def snapshot(self):
        with patch.object(native, "run", side_effect=self.reply), \
                patch.object(native, "visibility", return_value=self.visible):
            return native.snapshot()

    def test_only_window_agents_and_core_status_are_counted(self):
        snapshot = self.snapshot()
        self.assertEqual(len(snapshot["tasks"]), 2)
        self.assertEqual(TaskState().update(snapshot), b"C|1|0|0|1\n")
        self.assertTrue(snapshot["tasks"][0]["viewed"])
        self.assertFalse(snapshot["tasks"][1]["viewed"])

    def test_app_badge_is_authoritative_even_when_core_status_lags(self):
        p1, p2, _ = self.layout["spaces"][0]["tabs"][0]["terminals"]
        self.core["terminals"][0].update(status="done", turn=8)
        self.core["terminals"][1].update(status="done", turn=9)
        p1["status"], p2["status"] = "done", "idle"
        state = TaskState(native.FOCUS_SESSION)
        self.assertEqual(state.update(self.snapshot()), b"C|0|0|1|1\n")
        self.assertEqual(self.target_pane(state), "t1")  # The app still says unread.
        state.note_opened(state.focus_target())
        self.assertIsNone(state.focus_target())

        p1["status"] = "idle"  # Same turn; only the app's seen state changed.
        self.visible = (False, False)
        self.assertEqual(state.update(self.snapshot()), b"C|0|0|0\n")
        p1["status"] = "done"  # Mark unread again without another turn.
        self.assertEqual(state.update(self.snapshot()), b"C|0|0|1\n")
        self.assertEqual(self.target_pane(state), "t1")
        p1["status"] = None  # Explicitly unknown, not the old API's absent field.
        self.assertEqual(state.update(self.snapshot()), b"C|0|0|0\n")

    def test_button_rejects_badge_acknowledged_since_last_poll(self):
        p1 = self.layout["spaces"][0]["tabs"][0]["terminals"][0]
        p1["status"] = "done"
        state = TaskState(native.FOCUS_SESSION)
        state.update(self.snapshot())
        selected = state.focus_target()
        p1["status"] = "idle"
        with patch.object(native, "snapshot", return_value=self.snapshot()), \
                patch.object(native, "focus") as focus:
            with self.assertRaises(RuntimeError):
                open_task(native, selected)
            focus.assert_not_called()

    def test_native_presence_is_not_overwritten_by_legacy_workspace(self):
        helper = SimpleNamespace(snapshot=lambda: {"tasks": [], "quake_visible": True})
        self.assertTrue(snapshot_with_presence(helper)["quake_visible"])

    def test_unfocused_window_does_not_acknowledge(self):
        self.visible = (True, False)
        self.assertFalse(any(t["viewed"] for t in self.snapshot()["tasks"]))

    def target_pane(self, state):
        target = state.focus_target()
        assert target is not None
        return target["pane"]

    def test_completion_versions_are_per_terminal_but_navigation_is_observed_order(self):
        self.visible = (False, False)
        state = TaskState(focus_session=native.FOCUS_SESSION)
        self.core["terminals"][0]["turn"] = 100  # Larger than the later completion's turn.
        state.update(self.snapshot())
        self.core["terminals"][0]["status"] = "done"
        state.update(self.snapshot())
        self.assertEqual(self.target_pane(state), "t1")
        self.core["terminals"][1].update(status="working", turn=10)
        state.update(self.snapshot())
        self.core["terminals"][1]["status"] = "done"
        state.update(self.snapshot())
        self.assertEqual(self.target_pane(state), "t2")
        state.note_opened(state.focus_target())
        self.assertEqual(self.target_pane(state), "t1")
        state.update(self.snapshot())
        self.assertEqual(len(state.unread), 2)

    def test_status_errors_fail_closed_and_reset_history(self):
        self.snapshot()
        with patch.object(native, "run", return_value="invalid"):
            snapshot = native.snapshot()
        self.assertIsNone(TaskState().update(snapshot))
        self.assertEqual(native._previous, {})
        self.assertEqual(native._orders, {})

    def test_finished_and_disappeared_tasks_are_excluded(self):
        self.core["terminals"][0]["state"] = "finished"
        self.assertEqual([t["pane"] for t in self.snapshot()["tasks"]], ["t2"])

    def test_visibility_requires_matching_visible_client_and_keyboard_focus(self):
        monitors = [{"specialWorkspace": {"name": f"special:{native.WORKSPACE}"}}]
        client = {"class": native.APP_ID, "workspace": {"name": f"special:{native.WORKSPACE}"},
                  "focusHistoryID": 0}
        for focused in (0, 1):
            client["focusHistoryID"] = focused
            with patch.object(native, "run", side_effect=[json.dumps(monitors), json.dumps([client])]):
                self.assertEqual(native.visibility(), (True, focused == 0))
        for invalid in ("invalid", "null", "{}", "[]"):
            with patch.object(native, "run", return_value=invalid):
                self.assertEqual(native.visibility(), (False, False))

    def test_ambiguous_window_class_never_acknowledges(self):
        monitors = [{"specialWorkspace": {"name": f"special:{native.WORKSPACE}"}}]
        client = {"class": native.APP_ID, "workspace": {"name": f"special:{native.WORKSPACE}"},
                  "focusHistoryID": 0}
        with patch.object(native, "run", side_effect=[json.dumps(monitors), json.dumps([client, client])]):
            self.assertEqual(native.visibility(), (False, False))

    def test_explicit_socket_overrides_inherited_and_default_routing(self):
        with patch.object(native, "CORE_SOCKET", "/run/user/1000/explicit.sock"), \
                patch.object(native.subprocess, "run") as run:
            run.return_value = SimpleNamespace(returncode=0, stdout="{}")
            native.run([native.APP, "status"])
        self.assertEqual(run.call_args.kwargs["env"]["HC_SOCKET"], "/run/user/1000/explicit.sock")

    def test_reveal_is_explicit_show_not_toggle(self):
        with patch.object(native, "run") as run:
            native.reveal()
        run.assert_called_once_with([native.REVEAL, "show"], timeout=30)

    def test_empty_button_uses_native_reveal(self):
        with patch.object(native, "snapshot", return_value={"tasks": [], "errors": []}), \
                patch.object(native, "reveal") as reveal:
            open_task(native)
        reveal.assert_called_once_with()

    def test_focus_uses_terminal_id_and_never_sends_input(self):
        with patch.object(native, "snapshot", return_value={"tasks": [{"pane": "t1"}], "errors": []}), \
                patch.object(native, "run", return_value='{}') as run, \
                patch.object(native, "reveal") as reveal:
            native.focus(native.FOCUS_SESSION, "t1")
        run.assert_called_once_with([native.APP, "do", "agent.focus", "--terminal", "t1"])
        reveal.assert_called_once_with()

    def test_stale_focus_and_wrong_session_fail_closed(self):
        with patch.object(native, "snapshot", return_value={"tasks": [], "errors": []}), \
                patch.object(native, "run") as run:
            for session in (native.FOCUS_SESSION, "other"):
                with self.assertRaises(RuntimeError):
                    native.focus(session, "t1")
            run.assert_not_called()

    def test_failed_focus_does_not_reveal(self):
        with patch.object(native, "snapshot", return_value={"tasks": [{"pane": "t1"}], "errors": []}), \
                patch.object(native, "run", return_value='{"error":"missing"}'), \
                patch.object(native, "reveal") as reveal:
            with self.assertRaises(RuntimeError):
                native.focus(native.FOCUS_SESSION, "t1")
            reveal.assert_not_called()

    def test_route_is_explicit_and_agent_identity_is_removed(self):
        with patch.dict(os.environ, {"HC_SOCKET": "wrong", "HC_TERMINAL_ID": "t99",
                                     "HERDR_SESSION": "wrong"}), \
                patch.object(native.subprocess, "run") as run:
            run.return_value = SimpleNamespace(returncode=0, stdout="{}")
            native.run([native.APP, "status"])
        env = run.call_args.kwargs["env"]
        self.assertEqual(env["HC_SESSION"], native.FOCUS_SESSION)
        self.assertTrue(env["HC_SOCKET"].endswith(f"/{native.FOCUS_SESSION}.sock"))
        self.assertNotIn("HC_TERMINAL_ID", env)
        self.assertNotIn("HERDR_SESSION", env)

    def test_missing_socket_never_autostarts_core(self):
        with patch.object(native.Path, "is_socket", return_value=False), \
                patch.object(native.subprocess, "run") as run:
            with self.assertRaises(RuntimeError):
                native.run([native.CORE, "ls", "--json"])
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
