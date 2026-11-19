"""Address-targeted focus of existing Hyprland dropdowns. Never launches apps."""
import json
import subprocess


def query(run, kind):
    try:
        return json.loads(run(["hyprctl", "-j", kind], timeout=1))
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise RuntimeError("Cannot read Herdr window state") from error


def find_window(run, app_id, workspace):
    clients = query(run, "clients")
    if not isinstance(clients, list) or any(not isinstance(client, dict) for client in clients):
        raise RuntimeError("Invalid Herdr window list")
    matches = [client for client in clients if client.get("class") == app_id]
    if len(matches) != 1:
        raise RuntimeError("Expected exactly one existing Herdr window")
    window = matches[0]
    if (not isinstance(window.get("address"), str) or not window["address"]
            or window.get("hidden", False)
            or not isinstance(window.get("workspace"), dict)
            or window["workspace"].get("name") != f"special:{workspace}"):
        raise RuntimeError("Herdr window does not match the configured workspace")
    return window


def show_window(run, app_id, workspace, expected_address=None):
    window = find_window(run, app_id, workspace)
    address = window["address"]
    if expected_address is not None and address != expected_address:
        raise RuntimeError("Herdr window changed during focus")
    monitors = query(run, "monitors")
    if (not isinstance(monitors, list) or not monitors
            or any(not isinstance(m, dict) or not isinstance(m.get("specialWorkspace"), dict)
                   or not isinstance(m["specialWorkspace"].get("name"), str) for m in monitors)):
        raise RuntimeError("Cannot determine Herdr workspace visibility")
    visible = any(m["specialWorkspace"]["name"] == f"special:{workspace}" for m in monitors)
    commands = [] if visible else [f"hl.dsp.workspace.toggle_special({json.dumps(workspace)})"]
    target = json.dumps("address:" + address)
    commands.append(f"hl.dsp.focus({{ window = {target} }})")
    for command in commands:
        if run(["hyprctl", "dispatch", command], timeout=1).strip() != "ok":
            raise RuntimeError("Herdr window focus was not confirmed")
    active = query(run, "activewindow")
    if not isinstance(active, dict) or active.get("address") != address:
        raise RuntimeError("Herdr window did not receive focus")
