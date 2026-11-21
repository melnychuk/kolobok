#!/usr/bin/env python3
"""Capture Herdr Desktop with the Stick widget, fake spaces and a Copilot CLI agent.

Like bench-herdr-avatar.py, this starts a separate Herdr (temporary home, core
session and config), so the person's spaces, machines and agents never appear.
The fake repos live in ~/demo while it runs (so paths read ~/demo/...), and
are removed afterwards. Copilot CLI really works on the fake repo's task. A
small second Herdr window opens while it runs and must stay visible. One shot
is taken in light appearance and one in dark.

    scripts/capture-herdr-shot.py [--out site/media] [--wait 40]
"""

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEMO = os.path.expanduser("~/demo")
# A scripted agent: reports each state for a few seconds, then holds the last.
REPORTS = r"""
import sys, time
for step in sys.argv[1:]:
    state, _, seconds = step.partition(":")
    sys.stdout.write(f"\033]7501;state={state}:app=copilot\033\\")
    sys.stdout.flush()
    time.sleep(float(seconds or 0))
while True: time.sleep(1)
"""
REPOS = {
    "acme-api": ("feature/rate-limits", {
        "package.json": '{\n  "name": "acme-api",\n  "type": "module",\n  "scripts": {"test": "node --test"}\n}\n',
        "src/server.js": 'import http from "node:http";\n\nexport function handle(req, res) {\n'
                         '  res.writeHead(200, {"content-type": "application/json"});\n'
                         '  res.end(JSON.stringify({ ok: true, path: req.url }));\n}\n\n'
                         'http.createServer(handle).listen(8080);\n',
        "test/server.test.js": 'import { test } from "node:test";\n\ntest("responds", () => {});\n',
        "README.md": "# Acme API\n\nA small JSON API. Start it with `node src/server.js`.\n",
    }),
    "docs-site": ("main", {"README.md": "# Docs\n\nGuides for the Acme API.\n"}),
    "mobile-app": ("feature/offline-sync", {"README.md": "# Mobile app\n"}),
}
TASK = ("Add a small token-bucket rate limiter to src/server.js (10 requests per second per client "
        "IP, 429 when empty) and tests for it in test/server.test.js, then run npm test. "
        "Finally document the limit in README.md. Keep it dependency-free.")
# Tools run without prompts in the throwaway repo; git is off so its commands
# don't fill the transcript.
COPILOT = ["copilot", "--allow-all-tools", "--deny-tool", "shell(git)", "--no-custom-instructions",
           "--disable-builtin-mcps", "--no-auto-update", "--no-ask-user"]


def mcp_servers():
    """Every MCP server Copilot would start (user, plugin, workspace, builtin).

    Each is disabled for the shot: their startup notices name the person's
    own integrations (and fail without their sign-in).
    """
    try:
        listing = subprocess.run(["copilot", "mcp", "list", "--json"], capture_output=True, text=True,
                                 check=False, timeout=30)
        return sorted(json.loads(listing.stdout or "{}").get("mcpServers", {}))
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        sys.exit(f"Could not list Copilot's MCP servers: {error}")


def write(path, text):
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as file:
            file.write(text)
    except OSError as error:
        sys.exit(f"Could not write {path}: {error}")


class Capture:
    def __init__(self, args):
        self.args = args
        self.root = tempfile.mkdtemp(prefix="stick-shot-", dir="/var/tmp")
        self.session = "ss" + os.path.basename(self.root)[-6:]

    def env(self):
        env = dict(os.environ)
        env.update(HERDR_APP_HOME=f"{self.root}/home", HC_SESSION=self.session,
                   XDG_STATE_HOME=f"{self.root}/state", XDG_CONFIG_HOME=f"{self.root}/config",
                   HERDR_APP_NO_SOUND="1")
        for key in ("HC_SOCKET", "HC_TERMINAL_ID", "HC_TERMINAL_SOCKET", "HC_SCOPE"):
            env.pop(key, None)
        return env

    def run(self, *args, **kwargs):
        return subprocess.run(list(args), env=self.env(), capture_output=True, text=True, check=False, **kwargs)

    def do(self, command, payload=None):
        # A fresh window takes a moment to read its layout and refuses until then.
        argv = [self.args.app, "do", command, *([json.dumps(payload)] if payload else [])]
        result = self.run(*argv)
        for _ in range(50):
            if result.returncode == 0 and "error" not in result.stdout[:40]:
                return result.stdout
            time.sleep(0.2)
            result = self.run(*argv)
        sys.exit(f"do {command} failed: {result.stdout}{result.stderr}")

    def make_repos(self):
        if os.path.exists(DEMO):
            sys.exit(f"{DEMO} already exists; move it aside first")
        git = ["git", "-c", "user.name=Dev", "-c", "user.email=dev@example.com", "-c", "init.defaultBranch=main"]
        for name, (branch, files) in REPOS.items():
            folder = f"{DEMO}/{name}"
            for path, text in files.items():
                write(f"{folder}/{path}", text)
            for argv in (["init", "-q"], ["add", "-A"], ["commit", "-qm", "Initial commit"],
                         ["checkout", "-qb", branch] if branch != "main" else ["status", "-s"]):
                try:
                    subprocess.run(git + argv, cwd=folder, check=True, capture_output=True)
                except (OSError, subprocess.CalledProcessError) as error:
                    sys.exit(f"Could not prepare the fake repo {name}: {error}")

    def appearance(self, mode):
        write(f"{self.root}/home/settings.json", json.dumps({"appearance": mode}))

    def start_app(self, log):
        home = f"{self.root}/home"
        write(f"{home}/shared.json", json.dumps({"frame.welcomed": True, "layout": "main"}))
        x, y, width, height = self.args.bounds
        write(f"{home}/window.json", json.dumps({"state": "windowed", "bounds": [x, y, width, height]}))
        # Skip the "Updated to …" notice: this version's notes count as seen.
        try:
            shutil.copy(os.path.expanduser("~/.config/herdr-app/update-notes.json"), f"{home}/update-notes.json")
        except OSError as error:
            print(f"No update notes to copy ({error}); an update notice may show.")
        self.appearance("light")
        write(f"{self.root}/reports.py", REPORTS)
        self.make_repos()
        self.run("hc", "server", "start")
        app = subprocess.Popen([self.args.app], env=self.env(), stdin=subprocess.DEVNULL, stdout=log, stderr=log)
        subprocess.Popen(["caffeinate", "-d", "-w", str(app.pid)])  # Hidden windows aren't drawn.
        for _ in range(100):
            if self.run(self.args.app, "status").returncode == 0:
                break
            time.sleep(0.2)
        installer = [sys.executable, f"{REPO}/scripts/install-herdr-avatar.py", "--app-home", home]
        for _ in range(25):
            if subprocess.run(installer, capture_output=True, check=False).returncode == 0:
                return app
            time.sleep(0.4)
        sys.exit("Could not install the widget; see " + self.root)

    def trust(self, name):
        # Copilot asks once per folder whether to trust it; this run only.
        for _ in range(40):
            screen = self.run("hc", "read", "--lines", "0", name).stdout
            if "trust" in screen.lower():
                self.run("hc", "keys", name, "enter")
                return
            if "rate limiter" in screen:
                return
            time.sleep(0.5)

    def stage(self):
        reports = [sys.executable, "-u", f"{self.root}/reports.py"]
        self.do("layout.switch", {"name": "stick-s3"})
        # Background spaces first: one finished (an unread done), one still working.
        self.do("space.new", {"name": "docs-site", "folder": f"{DEMO}/docs-site", "tab": "changelog",
                              "argv": reports + ["working:4", "idle"]})
        self.do("space.new", {"name": "mobile-app", "folder": f"{DEMO}/mobile-app", "tab": "sync",
                              "argv": reports + ["working"]})
        # Copilot last, so its space is the one shown. `-i` starts it
        # interactively with the task; exec means a failed start closes the
        # pane instead of leaving a shell prompt (user@host) in the shot.
        quiet = [arg for name in mcp_servers() for arg in ("--disable-mcp-server", name)]
        command = " ".join(shlex.quote(part) for part in [*COPILOT, *quiet, "-i", TASK])
        self.do("space.new", {"name": "acme-api", "folder": f"{DEMO}/acme-api", "tab": "rate limits",
                              "argv": ["/bin/sh", "-c", f"exec {command}"]})
        self.trust("rate limits")

    def copilot(self):
        """Copilot's terminal (address, state), found by its folder."""
        try:
            terminals = json.loads(self.run("hc", "ls", "--json").stdout or "{}").get("terminals", [])
        except ValueError:
            return None, ""
        for terminal in terminals:
            if str(terminal.get("cwd", "")).endswith("/acme-api"):
                address = terminal.get("address")
                try:
                    details = json.loads(self.run("hc", "get", "--json", address).stdout or "{}")
                except ValueError:
                    return address, ""
                # `state` is liveness; the agent's own report is status.state.
                return address, str((details.get("status") or {}).get("state", ""))
        return None, ""

    def busy(self, limit, settle):
        """Wait until Copilot has edited a file and is still working, then let
        the transcript scroll on for `settle` seconds (at most `limit` s overall)."""
        deadline = time.monotonic() + limit
        while time.monotonic() < deadline:
            address, state = self.copilot()
            if address and state == "working":
                screen = self.run("hc", "read", "--lines", "0", address).stdout
                if "Edited" in screen:
                    time.sleep(settle)
                    return
            time.sleep(0.5)
        print("Copilot wasn't mid-task by the deadline; shooting anyway.")

    def shot(self, path):
        result = self.run(self.args.app, "shot", path)
        print(result.stdout.strip() or result.stderr.strip())

    def stop(self, app):
        if app and app.poll() is None:
            app.terminate()
            try:
                app.wait(timeout=10)
            except subprocess.TimeoutExpired:
                app.kill()
        try:
            terminals = json.loads(self.run("hc", "ls", "--json").stdout or "{}").get("terminals", [])
        except ValueError:
            terminals = []
        for terminal in terminals:
            self.run("hc", "kill", terminal["address"])
        self.run("hc", "server", "stop")
        try:
            shutil.rmtree(DEMO)
        except OSError as error:
            print(f"Could not remove {DEMO}: {error}")


def capture(args):
    job = Capture(args)
    app = None
    out = os.path.abspath(args.out)
    try:
        with open(f"{job.root}/app.log", "w", encoding="utf-8") as log:
            try:
                app = job.start_app(log)
                job.stage()
                job.busy(args.wait, args.settle)
                job.shot(f"{out}/herdr-light.png")
                job.appearance("dark")
                time.sleep(3)  # The theme applies live; the widget restarts with the new sidebar color.
                job.shot(f"{out}/herdr-dark.png")
            except KeyboardInterrupt:
                print("Stopped.")
            finally:
                job.stop(app)
    except OSError as error:
        sys.exit(f"Capture failed: {error}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", default="/Applications/Herdr.app/Contents/MacOS/Herdr", help="Herdr executable")
    parser.add_argument("--out", default=f"{REPO}/site/media", help="folder for herdr-light.png and herdr-dark.png")
    parser.add_argument("--wait", type=float, default=90, help="most seconds to wait for Copilot to be mid-task")
    parser.add_argument("--settle", type=float, default=3, help="seconds to let the transcript scroll after the first edit")
    parser.add_argument("--bounds", type=float, nargs=4, default=[80, 80, 1040, 641],
                        metavar=("X", "Y", "W", "H"), help="window position and size in points")
    capture(parser.parse_args())


if __name__ == "__main__":
    main()
