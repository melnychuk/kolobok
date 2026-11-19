import struct
import subprocess
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from bridge import macos_herdr_bridge as mac
from bridge.herdr_bridge import TaskState
from bridge.voice import FrameParser


def task(status="working", completion=0, key="default:term", session="default"):
    return {"key": key, "session": session, "pane": "w1:p4", "status": status,
            "completion": completion, "focused": False, "viewed": False}


class MacHerdrTests(unittest.TestCase):
    def test_snapshot_uses_agent_metadata_not_transcripts(self):
        helper = mac.MacHerdr("/bin/herdr")
        agent = {"terminal_id": "term", "pane_id": "w1:p4", "agent_status": "working",
                 "focused": False}
        with patch.object(helper, "api", return_value={"agents": [agent]}) as api, \
                patch.object(helper, "visible", return_value=True):
            snapshot = helper.snapshot()
        api.assert_called_once_with("agent", "list")
        self.assertEqual(snapshot["tasks"], [task()])
        self.assertEqual(TaskState("default").update(snapshot), b"C|1|0|0|1\n")

    def test_focus_session_and_completion_baseline(self):
        state = TaskState("default")
        historical = task("idle", 20)
        state.update({"tasks": [historical]})
        self.assertIsNone(state.focus_target())
        state.update({"tasks": [task()]})
        state.update({"tasks": [task("idle", 21)]})
        self.assertEqual(state.focus_target(), task("idle", 21))
        state.note_opened(state.focus_target())
        self.assertIsNone(state.focus_target())
        state.update({"tasks": [task("blocked")]})
        self.assertEqual(state.focus_target(), task("blocked"))
        self.assertIsNone(TaskState().focus_target())

    def test_only_configured_session_is_selected(self):
        state = TaskState("default")
        state.update({"tasks": [task(), task("blocked", key="other", session="other")]})
        self.assertEqual(state.focus_target(), task())

    def test_only_visible_focused_task_is_viewed(self):
        helper = mac.MacHerdr("herdr")
        agent = {"terminal_id": "term", "pane_id": "w1:p4", "focused": True}
        for visible in (False, True):
            with patch.object(helper, "api", return_value={"agents": [agent]}), \
                    patch.object(helper, "visible", return_value=visible):
                self.assertEqual(helper.snapshot()["tasks"][0]["viewed"], visible)

    def test_visibility_failure_is_not_viewed(self):
        helper = mac.MacHerdr("herdr")
        with patch.object(helper, "run", side_effect=subprocess.TimeoutExpired("osascript", 1)):
            self.assertFalse(helper.visible())

    def test_stale_click_does_not_activate_or_focus(self):
        helper = mac.MacHerdr("herdr")
        for changed in (task(), task("idle", 22)):
            with patch.object(helper, "snapshot", return_value={"tasks": [changed]}), \
                    patch.object(helper, "run") as run, patch.object(helper, "api") as api:
                with self.assertRaises(RuntimeError):
                    helper.open_task(task("idle", 21))
                run.assert_not_called()
                api.assert_not_called()

    def test_valid_click_only_activates_and_focuses(self):
        helper = mac.MacHerdr("herdr")
        for target in (None, task(), task("idle", 21)):
            with patch.object(helper, "snapshot", return_value={"tasks": [target] if target else []}), \
                    patch.object(helper, "reveal") as reveal, patch.object(helper, "api") as api:
                helper.open_task(target)
                reveal.assert_called_once_with()
                if target:
                    api.assert_called_once_with("agent", "focus", "w1:p4")
                else:
                    api.assert_not_called()

    def test_activation_failure_does_not_mark_agent_seen(self):
        helper = mac.MacHerdr("herdr")
        with patch.object(helper, "snapshot", return_value={"tasks": [task()]}), \
                patch.object(helper, "reveal", side_effect=RuntimeError()), \
                patch.object(helper, "api") as api:
            with self.assertRaises(RuntimeError):
                helper.open_task(task())
            api.assert_not_called()

    def test_reveal_focuses_exact_ghostty_surface_without_open_or_toggle(self):
        helper = mac.MacHerdr("herdr")
        with patch.object(helper, "ghostty_surface", return_value=("surface-uuid", False)), \
                patch.object(helper, "run") as run:
            helper.reveal()
        command = run.call_args.args[0]
        self.assertEqual(command[:4], ["/usr/bin/osascript", "-l", "JavaScript", "-e"])
        self.assertIn('g.terminals.byId("surface-uuid")', command[4])
        self.assertIn("g.activate(); g.focus(t)", command[4])
        self.assertNotIn("toggle", command[4])
        self.assertNotIn("dev.herdr.app", command[4])

    def test_ghostty_surface_requires_live_herdr_and_conservative_visibility(self):
        helper = mac.MacHerdr("herdr")
        for frontmost in (False, True):
            info = '{"frontmost": ' + str(frontmost).lower() + ', "terminals": [{"id":"s", "pid":123}]}'
            with patch.object(helper, "run", side_effect=[info, "/bin/herdr\n"]):
                self.assertEqual(helper.ghostty_surface(), ("s", frontmost))
        info = '{"frontmost":true,"terminals":[{"id":"s","pid":123},{"id":"other","pid":456}]}'
        with patch.object(helper, "run", side_effect=[info, "/bin/herdr", "/bin/zsh"]):
            self.assertEqual(helper.ghostty_surface(), ("s", False))
        with patch.object(helper, "run", side_effect=[info, "/bin/herdr", "/bin/herdr"]), \
                self.assertRaises(RuntimeError):
            helper.reveal()
        helper.ghostty_terminal = "s"
        with patch.object(helper, "run", side_effect=[info, "/bin/herdr"]):
            self.assertEqual(helper.ghostty_surface(), ("s", False))
        with patch.object(helper, "run", side_effect=[info, "/bin/zsh"]), self.assertRaises(RuntimeError):
            helper.reveal()

    def test_binary_pcm_cannot_trigger_click_and_fragmented_text_can(self):
        def frame(kind, data):
            return b"S3V1" + kind + struct.pack("<H", len(data)) + data
        parser = FrameParser()
        self.assertFalse(mac.button_frames(parser, frame(b"A", b"BUTTON|focus\n")))
        click = frame(b"T", b"BUTTON|focus")
        self.assertFalse(mac.button_frames(parser, click[:8]))
        self.assertTrue(mac.button_frames(parser, click[8:]))
        self.assertFalse(mac.button_frames(parser, frame(b"B", b"") + frame(b"E", b"")))

    def test_api_invalid_json_fails_closed(self):
        helper = mac.MacHerdr("herdr")
        with patch.object(helper, "run", return_value="invalid"), self.assertRaises(RuntimeError):
            helper.snapshot()

    def test_command_clears_inherited_session_context(self):
        helper = mac.MacHerdr("/bin/herdr", "named")
        with patch.dict(mac.os.environ, {"HERDR_SOCKET_PATH": "wrong", "HERDR_PANE_ID": "wrong"}), \
                patch.object(mac.subprocess, "run", return_value=Mock(returncode=0, stdout="{}")) as run:
            helper.run(["command"])
            self.assertNotIn("HERDR_SOCKET_PATH", run.call_args.kwargs["env"])
            self.assertNotIn("HERDR_PANE_ID", run.call_args.kwargs["env"])

    def test_multiple_ports_fail_closed_and_explicit_missing_does_not_fallback(self):
        with patch.object(Path, "glob", return_value=[Path("/dev/a"), Path("/dev/b")]):
            with self.assertRaises(RuntimeError):
                mac.find_port()
            with patch.object(Path, "exists", return_value=False):
                self.assertIsNone(mac.find_port("/dev/missing"))

    def test_loop_closes_serial_on_interrupt(self):
        helper = Mock(session="default")
        with patch.object(mac, "ThreadPoolExecutor"), \
                patch.object(mac, "find_port", return_value="/dev/fixture"), \
                patch.object(mac, "open_serial", return_value=42), \
                patch.object(mac.select, "select", return_value=([], [], [])), \
                patch.object(mac.time, "sleep", side_effect=KeyboardInterrupt), \
                patch.object(mac.os, "close") as close:
            with self.assertRaises(KeyboardInterrupt):
                mac.run(helper)
            close.assert_called_once_with(42)


if __name__ == "__main__":
    unittest.main()
