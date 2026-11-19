import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

from bridge import combined_herdr as combined
from bridge.button_action import open_task
from bridge.herdr_bridge import TaskState, load_helper


def task(status="working", version=1, session="agent-dropdown", pane="p1", viewed=False):
    return {"key": session + ":" + pane, "session": session, "pane": pane,
            "status": status, "completion": version, "viewed": viewed}


def helper(tasks=None, errors=None, visible=False):
    return SimpleNamespace(snapshot=Mock(return_value={"tasks": tasks or [], "errors": errors or [],
                                                       "quake_visible": visible}),
                           focus=Mock(), reveal=Mock(), FOCUS_SESSION="quake-native")


def entry(name="quake"):
    return {"id": name, "home": "~/.config/herdr-quake", "session": "quake-native",
            "app": "~/.local/opt/herdr-quake/app/herdr-app",
            "core": "~/.local/opt/herdr-quake/core/herdr-core",
            "workspace": "herdr-native", "app_id": "org.omarchy.herdr-quake",
            "reveal": "~/.local/bin/omarchy-herdr-native-pane"}


class CombinedHerdrTests(unittest.TestCase):
    def setUp(self):
        self.cli = helper([task()])
        self.native = helper([task(session="quake-native", pane="t1")])
        self.sources = [combined.Source("cli", module=self.cli),
                        combined.Source("native/quake", module=self.native, socket="/run/core.sock")]
        self.patcher = patch.object(combined, "sources", return_value=self.sources)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)

    def set_tasks(self, module, tasks, errors=None):
        module.snapshot.return_value = {"tasks": tasks, "errors": errors or [], "quake_visible": False}

    def selected(self, state):
        result = state.focus_target()
        assert result is not None
        return result

    def test_counts_both_backends_and_preserves_wire_protocol(self):
        self.set_tasks(self.cli, [task("blocked"), task("idle", pane="p2")])
        snapshot = combined.snapshot()
        self.assertEqual(TaskState(None).update(snapshot), b"C|2|1|0\n")
        self.assertEqual({t["session"] for t in snapshot["tasks"]}, {"cli/agent-dropdown", "native/quake"})

    def test_legacy_sessions_outside_dropdown_are_navigable(self):
        self.set_tasks(self.native, [])
        self.set_tasks(self.cli, [task(session="project")])
        state = TaskState(None)
        state.update(combined.snapshot())
        target = self.selected(state)
        self.assertEqual(target["session"], "cli/project")
        open_task(combined, target)
        self.cli.focus.assert_called_once_with("project", "p1")
        self.native.focus.assert_not_called()

    def test_native_exports_from_cli_helper_are_not_counted_or_navigated(self):
        self.cli.NATIVE_SESSION = "native:quake-native"
        self.set_tasks(self.cli, [task("working", session=self.cli.NATIVE_SESSION, pane="t1"),
                                 task("idle", session="project")],
                       errors=[f"{self.cli.NATIVE_SESSION}: native query failed"])
        snapshot = combined.snapshot()
        self.assertEqual(TaskState(None).update(snapshot), b"C|1|0|0\n")
        self.assertEqual({t["session"] for t in snapshot["tasks"]}, {"cli/project", "native/quake"})
        self.assertFalse(snapshot["sources"][0]["partial"])
        self.native.snapshot.side_effect = RuntimeError()
        self.assertEqual(TaskState(None).update(combined.snapshot()), b"C|0|0|0\n")

    def test_same_ids_across_backends_do_not_collide(self):
        self.set_tasks(self.native, [task()])
        snapshot = combined.snapshot()
        self.assertEqual(len({t["key"] for t in snapshot["tasks"]}), 2)

    def test_duplicate_native_views_count_once_and_use_viewed_route(self):
        duplicate = helper([task(session="quake-native", pane="t1", viewed=True)])
        self.sources.append(combined.Source("native/other", module=duplicate, socket="/run/core.sock"))
        snapshot = combined.snapshot()
        natives = [t for t in snapshot["tasks"] if t["session"].startswith("native/")]
        self.assertEqual(len(natives), 1)
        self.assertEqual(natives[0]["session"], "native/other")
        self.assertTrue(natives[0]["viewed"])

    def test_distinct_cores_with_same_terminal_id_are_not_deduplicated(self):
        self.sources.append(combined.Source("native/other", module=self.native, socket="/run/other.sock"))
        self.assertEqual(len(combined.snapshot()["tasks"]), 3)

    def test_latest_observed_completion_wins_not_largest_source_version(self):
        state = TaskState(None)
        state.update(combined.snapshot())
        self.set_tasks(self.cli, [task("done", 900)])
        state.update(combined.snapshot())
        self.assertEqual(self.selected(state)["session"], "cli/agent-dropdown")
        self.set_tasks(self.native, [task("done", 2, session="quake-native", pane="t1")])
        state.update(combined.snapshot())
        newest = self.selected(state)
        self.assertEqual(newest["session"], "native/quake")
        state.note_opened(newest)
        self.assertEqual(self.selected(state)["session"], "cli/agent-dropdown")
        self.assertEqual(len(state.unread), 2)

    def test_partial_outage_drops_only_failed_source_and_recovery_is_a_baseline(self):
        state = TaskState(None)
        state.update(combined.snapshot())
        self.set_tasks(self.cli, [task("done", 2)])
        state.update(combined.snapshot())
        self.native.snapshot.side_effect = OSError("private failure")
        snapshot = combined.snapshot()
        self.assertFalse(snapshot["errors"])
        self.assertEqual(state.update(snapshot), b"C|0|0|1\n")
        self.assertFalse(snapshot["sources"][1]["available"])
        self.native.snapshot.side_effect = None
        self.set_tasks(self.native, [task("done", 100, session="quake-native", pane="t1")])
        self.assertEqual(state.update(combined.snapshot()), b"C|0|0|1\n")
        self.assertEqual(self.selected(state)["session"], "cli/agent-dropdown")

    def test_total_outage_stops_heartbeats(self):
        for module in (self.cli, self.native):
            module.snapshot.side_effect = RuntimeError("private failure")
        snapshot = combined.snapshot()
        self.assertIsNone(TaskState(None).update(snapshot))
        self.assertNotIn("private", json.dumps(snapshot))

    def test_empty_success_is_idle_not_a_failure(self):
        self.set_tasks(self.cli, [])
        self.native.snapshot.side_effect = RuntimeError()
        self.assertEqual(TaskState(None).update(combined.snapshot()), b"C|0|0|0\n")

    def test_cli_partial_session_failure_retains_healthy_tasks(self):
        self.set_tasks(self.cli, [task("blocked")], errors=["a different session failed"])
        snapshot = combined.snapshot()
        self.assertEqual(TaskState(None).update(snapshot), b"C|2|1|0\n")
        self.assertTrue(snapshot["sources"][0]["partial"])
        self.assertFalse(snapshot["errors"])

    def test_visibility_comes_from_either_backend(self):
        self.cli.snapshot.return_value["quake_visible"] = True
        self.assertTrue(combined.snapshot()["quake_visible"])
        self.cli.snapshot.return_value["quake_visible"] = False
        self.native.snapshot.return_value["quake_visible"] = True
        self.assertTrue(combined.snapshot()["quake_visible"])

    def test_fresh_native_focus_routes_to_correct_instance(self):
        state = TaskState(None)
        self.set_tasks(self.cli, [])
        state.update(combined.snapshot())
        open_task(combined, self.selected(state))
        self.native.focus.assert_called_once_with("quake-native", "t1")
        self.cli.focus.assert_not_called()

    def test_cli_dropdown_focus_bypasses_toggle_and_addresses_only_cli_window(self):
        self.cli.run = Mock(return_value='{}')
        with patch.object(combined, "find_window", return_value={"address": "0xCLI"}), \
                patch.object(combined, "show_window") as show:
            selected = next(t for t in combined.snapshot()["tasks"] if t["session"] == "cli/agent-dropdown")
            open_task(combined, selected)
        self.cli.run.assert_called_once_with(["herdr", "--session", "agent-dropdown", "agent", "focus", "p1"])
        show.assert_called_once_with(self.cli.run, "org.omarchy.agent-pane", "agent-pane", expected_address="0xCLI")
        self.cli.focus.assert_not_called()
        self.native.focus.assert_not_called()

    def test_missing_cli_window_never_falls_through_to_native(self):
        self.cli.run = Mock()
        with patch.object(combined, "find_window", side_effect=RuntimeError("missing")):
            selected = next(t for t in combined.snapshot()["tasks"] if t["session"] == "cli/agent-dropdown")
            with self.assertRaises(RuntimeError):
                open_task(combined, selected)
        self.cli.run.assert_not_called()
        self.native.focus.assert_not_called()

    def test_cli_visible_but_not_foreground_does_not_acknowledge_completion(self):
        self.set_tasks(self.cli, [{**task("done"), "focused": True, "viewed": True}])
        self.cli.snapshot.return_value["quake_visible"] = True
        with patch.object(combined, "find_window", return_value={"address": "0xCLI", "focusHistoryID": 1}):
            selected = next(t for t in combined.snapshot()["tasks"] if t["session"] == "cli/agent-dropdown")
        self.assertFalse(selected["viewed"])
        self.assertFalse(selected["focused"])

    def test_stale_native_completion_cannot_focus_a_resumed_task(self):
        state = TaskState(None)
        state.update(combined.snapshot())
        self.set_tasks(self.native, [task("done", 2, session="quake-native", pane="t1")])
        state.update(combined.snapshot())
        selected = self.selected(state)
        self.set_tasks(self.native, [task("working", 3, session="quake-native", pane="t1")])
        with self.assertRaises(RuntimeError):
            open_task(combined, selected)
        self.native.focus.assert_not_called()

    def test_empty_button_reveals_running_native_only(self):
        self.set_tasks(self.cli, [])
        self.set_tasks(self.native, [])
        open_task(combined)
        self.native.reveal.assert_called_once_with()

    def test_empty_button_does_not_launch_an_absent_instance(self):
        self.set_tasks(self.cli, [])
        self.cli.run = Mock(return_value="[]")
        self.native.snapshot.side_effect = RuntimeError()
        open_task(combined)
        self.native.reveal.assert_not_called()
        self.cli.run.assert_called_once_with(["hyprctl", "-j", "clients"], timeout=1)

    def test_unknown_route_never_focuses_any_source(self):
        with self.assertRaises(RuntimeError):
            combined.focus("native/deleted", "t1")
        self.native.focus.assert_not_called()
        self.cli.focus.assert_not_called()

    def test_malformed_source_does_not_break_other_instances(self):
        for malformed in (None, {}, ["not a task"], [{**task(), "completion": "invalid"}]):
            with self.subTest(malformed=malformed):
                self.cli.snapshot.return_value = {"tasks": malformed}
                snapshot = combined.snapshot()
                self.assertFalse(snapshot["errors"])
                self.assertEqual(len(snapshot["tasks"]), 1)
                self.assertFalse(snapshot["sources"][0]["available"])

    def test_cli_sessions_are_refreshed_each_poll(self):
        self.set_tasks(self.cli, [])
        combined.snapshot()
        self.set_tasks(self.cli, [task(session="new-session")])
        snapshot = combined.snapshot()
        self.assertIn("cli/new-session", {t["session"] for t in snapshot["tasks"]})
        self.assertEqual(self.cli.snapshot.call_count, 2)

    def test_missing_cli_helper_does_not_break_native_source(self):
        self.sources[0] = combined.Source("cli", helper=Path("/missing/helper.py"))
        snapshot = combined.snapshot()
        self.assertFalse(snapshot["errors"])
        self.assertEqual(len(snapshot["tasks"]), 1)
        self.assertFalse(snapshot["sources"][0]["available"])


class InstanceConfigurationTests(unittest.TestCase):
    def load(self, config):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "herdr.json"
            path.write_text(json.dumps(config))
            return combined.load_sources(path)

    def test_native_modules_are_isolated_and_paths_expanded(self):
        second = {**entry("second"), "session": "second", "socket": "/run/user/1000/custom.sock"}
        sources = self.load({"native": [entry(), second]})
        self.assertEqual(len(sources), 3)
        first, other = sources[1:]
        self.assertIsNot(first.module, other.module)
        self.assertEqual(first.module.FOCUS_SESSION, "quake-native")
        self.assertEqual(other.module.FOCUS_SESSION, "second")
        self.assertEqual(other.module.CORE_SOCKET, "/run/user/1000/custom.sock")
        self.assertTrue(first.module.APP.startswith(str(Path.home())))

    def test_two_native_registrations_focus_their_own_app_and_existing_window(self):
        second = {**entry("second"), "home": "/fixture/second", "session": "second",
                  "app": "/fixture/second-app", "app_id": "org.fixture.second", "workspace": "second"}
        configured = self.load({"cli": False, "native": [entry(), second]})
        first, other = configured
        with patch.object(combined, "sources", return_value=configured), \
                patch.object(first.module, "snapshot", return_value={"tasks": [], "errors": [], "quake_visible": False}), \
                patch.object(first.module, "run") as wrong_run, \
                patch.object(other.module, "snapshot", return_value={
                    "tasks": [task(session="second", pane="t1")], "errors": [], "quake_visible": False}), \
                patch.object(other.module, "find_window", return_value={"address": "0xSECOND"}), \
                patch.object(other.module, "show_window") as show, \
                patch.object(other.module, "run", return_value='{}') as run:
            selected = combined.snapshot()["tasks"][0]
            open_task(combined, selected)
        wrong_run.assert_not_called()
        run.assert_called_once_with(["/fixture/second-app", "do", "agent.focus", "--terminal", "t1"])
        show.assert_called_once_with(run, "org.fixture.second", "second", expected_address="0xSECOND")
        self.assertTrue(other.module.ADDRESS_FOCUS)

    def test_reveal_launcher_is_optional_in_combined_mode(self):
        native = entry()
        native.pop("reveal")
        self.assertTrue(self.load({"cli": False, "native": [native]})[0].module.ADDRESS_FOCUS)

    def test_bad_configurations_fail_before_probing_anything(self):
        for config in ({}, {"cli": False}, {"cli": "false"}, {"native": {}},
                       {"native": [entry(), entry()]}, {"native": [{**entry(), "session": "../other"}]},
                       {"native": [{**entry(), "home": "relative"}]},
                       {"native": [{**entry(), "id": "../bad"}]},
                       {"native": [{"id": "missing-fields"}]}, {"unknown": True}):
            if config == {}:
                continue  # Explicit CLI-only config is valid.
            with self.subTest(config=config), self.assertRaises(RuntimeError):
                self.load(config)

    def test_cli_only_is_supported(self):
        self.assertEqual([s.name for s in self.load({})], ["cli"])

    def test_missing_config_is_offline_not_silent_legacy_fallback(self):
        module = load_helper(Path(combined.__file__))
        with patch.object(module, "CONFIG", Path("/missing/config.json")):
            self.assertIsNone(TaskState(None).update(module.snapshot()))
