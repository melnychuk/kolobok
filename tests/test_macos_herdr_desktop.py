import json
import subprocess
import unittest
from unittest.mock import Mock, patch

from bridge import macos_herdr_desktop as desktop
from bridge.herdr_bridge import TaskState


def layout(*terminals, tab="Workspace"):
    return {"spaces": [{"tabs": [{"name": tab, "terminals": list(terminals)}]}], "recent": []}


def pane(terminal, status: str | None = "working", app: str | None = "pi", name=None, pane_id=None):
    return {"terminal": terminal, "pane": pane_id or "p-" + terminal, "app": app,
            "status": status, "name": name, "sub_agents": []}


class DesktopSnapshotTests(unittest.TestCase):
    def helper(self, ls, status=None):
        helper = desktop.MacHerdrDesktop("/Applications/Herdr.app/Contents/MacOS/Herdr")
        responses = {"ls": ls, "status": status or {"focused_pane": None}}
        self.app = Mock(side_effect=lambda command, *rest: responses[command])
        helper.app = self.app
        return helper

    def test_counts_every_agent_kind_once_and_skips_shells(self):
        helper = self.helper(layout(
            pane("t1", "working", "pi"), pane("t2", "blocked", "claude"), pane("t3", "done", "codex", " Fix "),
            pane("t4", None, "copilot"), pane("t5", "idle", None), pane("t1", "working", "pi")),
            {"focused_pane": "p-t2"})
        tasks = helper.snapshot()["tasks"]
        self.assertEqual([t["key"] for t in tasks], ["desktop:t1", "desktop:t2", "desktop:t3", "desktop:t4"])
        self.assertEqual([t["agent"] for t in tasks], ["pi", "claude", "codex", "copilot"])
        self.assertEqual([t["status"] for t in tasks], ["working", "blocked", "done", "unknown"])
        self.assertEqual([t["unread"] for t in tasks], [False, False, True, False])
        self.assertEqual([t["focused"] for t in tasks], [False, True, False, False])
        # The terminal's own name, else its tab's.
        self.assertEqual([t["title"] for t in tasks], ["Workspace", "Workspace", "Fix", "Workspace"])
        self.assertEqual(TaskState("desktop").update({"tasks": tasks}), b"C|2|1|1\n")

    def test_unread_order_follows_observed_completions(self):
        helper = self.helper(layout(pane("a", "done"), pane("b", "working")))
        first = {t["key"]: t for t in helper.snapshot()["tasks"]}
        self.app.side_effect = lambda command, *rest: (
            layout(pane("a", "done"), pane("b", "done")) if command == "ls" else {"focused_pane": None})
        second = {t["key"]: t for t in helper.snapshot()["tasks"]}
        self.assertEqual(second["desktop:a"]["completion_order"], first["desktop:a"]["completion_order"])
        self.assertGreater(second["desktop:b"]["completion_order"], second["desktop:a"]["completion_order"])
        state = TaskState("desktop")
        state.update({"tasks": list(second.values())})
        target = state.focus_target()
        self.assertIsNotNone(target)
        self.assertEqual(target and target["key"], "desktop:b")  # Newest unread first.

    def test_names_agents_by_project_and_tab_only_when_they_share_one(self):
        ls = {"spaces": [
            {"name": "stick-s3", "tabs": [{"name": "1", "terminals": [pane("a")]},
                                          {"name": "2", "terminals": [pane("b"), pane("c", name="Review")]}]},
            {"name": "website", "tabs": [{"name": "1", "terminals": [pane("d")]}]},
            {"name": "website", "machine": "devbox", "tabs": [{"name": "1", "terminals": [pane("e")]}]}]}
        tasks = self.helper(ls).snapshot()["tasks"]
        self.assertEqual([t["title"] for t in tasks],
                         ["stick-s3 · 1", "stick-s3 · 2", "Review", "website", "website on devbox"])
        self.assertEqual([(t["space"], t["tab"], t["name"]) for t in tasks],
                         [("stick-s3", "1", ""), ("stick-s3", "2", ""), ("stick-s3", "2", "Review"),
                          ("website", "1", ""), ("website on devbox", "1", "")])

    def test_unexpected_layout_fails_closed(self):
        helper = self.helper({"spaces": [{"no_tabs": []}]})
        with self.assertRaises(RuntimeError):
            helper.snapshot()

    def test_commands_drop_inherited_herdr_and_core_routes(self):
        helper = desktop.MacHerdrDesktop("/bin/herdr-app")
        with patch.dict(desktop.os.environ, {"HERDR_APP_HOME": "/x", "HC_SOCKET": "/y", "HOME": "/h"}), \
                patch.object(desktop.subprocess, "run", return_value=Mock(returncode=0, stdout="{}")) as run:
            helper.app("status")
        env = run.call_args.kwargs["env"]
        self.assertNotIn("HERDR_APP_HOME", env)
        self.assertNotIn("HC_SOCKET", env)
        self.assertEqual(env["HOME"], "/h")


class DesktopFocusTests(unittest.TestCase):
    def helper(self, tasks, focus_response=None):
        helper = desktop.MacHerdrDesktop("/bin/herdr-app")
        helper.snapshot = Mock(return_value={"tasks": tasks})
        self.app = Mock(return_value=focus_response if focus_response is not None else {"ok": True})
        self.raise_window = Mock()
        helper.app = self.app
        helper.raise_window = self.raise_window
        return helper

    def task(self, key="desktop:t1", status="done", unread=True):
        return {"key": key, "pane": key.split(":")[1], "status": status, "unread": unread}

    def test_explicit_focus_revalidates_the_key_whatever_its_status(self):
        helper = self.helper([self.task(status="unknown", unread=False)])
        helper.focus_key("desktop:t1")
        self.app.assert_called_once_with("do", "agent.focus", json.dumps({"terminal": "t1"}))
        self.raise_window.assert_called_once()

    def test_unknown_key_is_stale_and_never_raises_the_window(self):
        helper = self.helper([self.task()])
        with self.assertRaises(desktop.StaleTarget):
            helper.focus_key("desktop:gone")
        self.app.assert_not_called()
        self.raise_window.assert_not_called()

    def test_focus_error_reply_fails_without_raising_the_window(self):
        helper = self.helper([self.task()], focus_response={"error": "no such terminal"})
        with self.assertRaises(RuntimeError):
            helper.focus_key("desktop:t1")
        self.raise_window.assert_not_called()

    def test_button_target_must_still_be_unread_or_active(self):
        helper = self.helper([self.task(status="idle", unread=False)])
        with self.assertRaises(desktop.StaleTarget):
            helper.open_task({"key": "desktop:t1", "status": "done"})
        helper = self.helper([self.task(status="blocked", unread=False)])
        helper.open_task({"key": "desktop:t1", "status": "working"})
        self.app.assert_called_once()

    def test_raise_window_never_launches_a_quit_herdr(self):
        helper = desktop.MacHerdrDesktop("/bin/herdr-app")
        with patch.object(helper, "run", return_value="") as run, self.assertRaises(RuntimeError):
            helper.raise_window()
        run.assert_called_once()
        self.assertEqual(run.call_args.args[0][0], "/usr/bin/lsappinfo")
        with patch.object(helper, "run", side_effect=['ASN:0x0-0x1-"Herdr":', ""]) as run:
            helper.raise_window()
        self.assertEqual(run.call_args.args[0], ["/usr/bin/open", "-b", desktop.BUNDLE_ID])

    def test_locate_requires_an_absolute_executable(self):
        self.assertIsNone(desktop.locate("relative/herdr-app"))
        self.assertIsNone(desktop.locate("/nonexistent/herdr-app"))
        with patch.object(desktop.shutil, "which", return_value=None), \
                patch.object(desktop.os, "access", return_value=False):
            self.assertIsNone(desktop.locate())

    def test_timeouts_surface_as_failures(self):
        helper = desktop.MacHerdrDesktop("/bin/herdr-app")
        with patch.object(desktop.subprocess, "run", side_effect=subprocess.TimeoutExpired("herdr-app", 4)), \
                self.assertRaises(subprocess.TimeoutExpired):
            helper.snapshot()


if __name__ == "__main__":
    unittest.main()
