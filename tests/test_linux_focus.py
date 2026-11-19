import json
import unittest
from unittest.mock import Mock

from bridge.linux_focus import find_window, show_window


def window(app_id="native", workspace="native", address="0x123"):
    return {"class": app_id, "address": address, "workspace": {"name": "special:" + workspace}}


class LinuxFocusTests(unittest.TestCase):
    def test_exact_native_or_cli_window_is_selected_not_the_other_app(self):
        clients = [window(), window("cli", "cli", "0x456")]
        for app_id, address in (("native", "0x123"), ("cli", "0x456")):
            with self.subTest(app_id=app_id):
                run = Mock(side_effect=[json.dumps(clients),
                                        json.dumps([{"specialWorkspace": {"name": "special:" + app_id}}]),
                                        "ok\n", json.dumps({"address": address})])
                show_window(run, app_id, app_id)
                dispatch = run.call_args_list[2].args[0]
                self.assertEqual(dispatch, ["hyprctl", "dispatch",
                                           f'hl.dsp.focus({{ window = "address:{address}" }})'])
                self.assertTrue(all(call.args[0][0] == "hyprctl" for call in run.call_args_list))

    def test_workspace_visible_on_other_monitor_is_not_toggled_closed(self):
        run = Mock(side_effect=[json.dumps([window()]),
                                json.dumps([{"specialWorkspace": {"name": ""}},
                                            {"specialWorkspace": {"name": "special:native"}}]),
                                "ok", '{"address":"0x123"}'])
        show_window(run, "native", "native")
        self.assertEqual(len([c for c in run.call_args_list if c.args[0][1] == "dispatch"]), 1)

    def test_hidden_workspace_is_shown_once_then_exact_address_focused(self):
        run = Mock(side_effect=[json.dumps([window()]),
                                json.dumps([{"specialWorkspace": {"name": ""}}]),
                                "ok", "ok", '{"address":"0x123"}'])
        show_window(run, "native", "native")
        commands = [c.args[0][2] for c in run.call_args_list if c.args[0][1] == "dispatch"]
        self.assertEqual(commands, ['hl.dsp.workspace.toggle_special("native")',
                                    'hl.dsp.focus({ window = "address:0x123" })'])

    def test_missing_duplicate_wrong_workspace_or_unmapped_windows_fail_closed(self):
        for clients in ([], [window(), window(address="0x456")],
                        [window(workspace="wrong")], [{**window(), "hidden": True}],
                        [{**window(), "workspace": None}], ["invalid"], None):
            with self.subTest(clients=clients):
                run = Mock(return_value=json.dumps(clients))
                with self.assertRaises(RuntimeError):
                    show_window(run, "native", "native")
                self.assertEqual(run.call_count, 1)

    def test_replaced_window_is_not_focused(self):
        run = Mock(return_value=json.dumps([window(address="0x456")]))
        with self.assertRaises(RuntimeError):
            show_window(run, "native", "native", expected_address="0x123")
        self.assertEqual(run.call_count, 1)

    def test_unknown_monitor_state_never_toggles_anything(self):
        for monitors in (None, {}, [], [None], [{}], [{"specialWorkspace": None}]):
            with self.subTest(monitors=monitors):
                run = Mock(side_effect=[json.dumps([window()]), json.dumps(monitors)])
                with self.assertRaises(RuntimeError):
                    show_window(run, "native", "native")
                self.assertEqual(run.call_count, 2)

    def test_dispatch_error_or_wrong_foreground_is_not_reported_as_success(self):
        monitors = json.dumps([{"specialWorkspace": {"name": "special:native"}}])
        for replies in ([json.dumps([window()]), monitors, "not ok"],
                        [json.dumps([window()]), monitors, "ok", '{"address":"other"}']):
            with self.subTest(replies=replies), self.assertRaises(RuntimeError):
                show_window(Mock(side_effect=replies), "native", "native")

    def test_malformed_json_is_a_safe_error(self):
        with self.assertRaises(RuntimeError):
            find_window(Mock(return_value="invalid"), "native", "native")
