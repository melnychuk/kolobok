#!/usr/bin/env python3
"""Measure what the Stick widget costs Herdr Desktop's main thread.

Starts a separate Herdr (its own temporary home and core session, so yours is
untouched), installs the widget from this checkout, and reports Herdr's own
per-second metrics (HERDR_APP_BENCH) with and without the widget: idle, with
working and blocked agents, with four streaming agent panes, and under CPU load.
A second Herdr window opens while it runs and must stay visible.

    scripts/bench-herdr-avatar.py [--app /Applications/Herdr.app/Contents/MacOS/Herdr]
                                  [--seconds 8] [--load 12] [--label run]
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# An agent that only reports its state (idle, working or blocked).
REPORTS = r"""
import sys, time
sys.stdout.write(f"\033]7501;state={sys.argv[1]}:app=pi\033\\")
sys.stdout.flush()
while True: time.sleep(1)
"""
# An agent streaming output: a new line every 10 ms under a spinner.
AGENT = r"""
import sys, time, itertools
sys.stdout.write("\033]7501;state=working:app=pi\033\\")
words = "the quick brown fox reads the diff and writes a careful patch for the failing test".split()
spin = itertools.cycle("|/-\\")
n = 0
while True:
    n += 1
    line = " ".join(words[(n + i) % len(words)] for i in range(12))
    sys.stdout.write(f"\r\033[K{line} {n}\n\033[2m{next(spin)} working {n // 20}s\033[0m")
    sys.stdout.flush()
    time.sleep(0.01)
"""


class Bench:
    def __init__(self, args):
        self.args = args
        self.root = tempfile.mkdtemp(prefix="stick-bench-", dir="/var/tmp")
        self.session = "sb" + os.path.basename(self.root)[-6:]
        self.frames = f"{self.root}/frames.jsonl"

    def env(self):
        env = dict(os.environ)
        env.update(HERDR_APP_HOME=f"{self.root}/home", HC_SESSION=self.session,
                   XDG_STATE_HOME=f"{self.root}/state", XDG_CONFIG_HOME=f"{self.root}/config",
                   HERDR_APP_NO_SOUND="1", HERDR_APP_BENCH=self.frames)
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
                return
            time.sleep(0.2)
            result = self.run(*argv)
        sys.exit(f"do {command} failed: {result.stdout}{result.stderr}")

    def lines(self):
        try:
            with open(self.frames, encoding="utf-8") as file:
                return file.readlines()
        except FileNotFoundError:
            return []

    def measure(self, name):
        time.sleep(3)  # Let the scenario settle.
        start = len(self.lines())
        time.sleep(self.args.seconds)
        rows = []
        for text in self.lines()[start:]:
            try:
                rows.append(json.loads(text))
            except ValueError:
                continue  # A line still being written.

        def average(key):
            return round(sum(row.get(key, 0) for row in rows) / max(len(rows), 1), 1)

        out = {
            "scenario": name, "window_fps": average("window"),
            "fps_min": min((row.get("window", 0) for row in rows), default=0),
            "script_ms_s": average("script_ms"), "scripts_s": average("scripts"),
            "native_ms_s": average("materialize_ms"), "render_to_paint_ms_s": average("frame_ms"),
        }
        each = round(out["script_ms_s"] / out["scripts_s"], 2) if out["scripts_s"] else 0
        out["script_ms_per_render"] = each
        print(f"{self.args.label:10} {name:22} fps {out['window_fps']:5.1f} (min {out['fps_min']:4})  "
              f"script {out['script_ms_s']:6.1f} ms/s  native {out['native_ms_s']:5.1f} ms/s  "
              f"render->paint {out['render_to_paint_ms_s']:6.1f} ms/s  "
              f"mod renders {out['scripts_s']:4.1f}/s ({each:4.2f} ms each)", flush=True)
        return out

    def start_app(self, log):
        try:
            os.makedirs(f"{self.root}/home")
            with open(f"{self.root}/home/shared.json", "w", encoding="utf-8") as file:
                json.dump({"frame.welcomed": True, "layout": "main"}, file)
            for name, source in (("reports.py", REPORTS), ("agent.py", AGENT)):
                with open(f"{self.root}/{name}", "w", encoding="utf-8") as file:
                    file.write(source)
        except OSError as error:
            sys.exit(f"Could not prepare {self.root}: {error}")
        self.run("hc", "server", "start")
        app = subprocess.Popen([self.args.app], env=self.env(), stdin=subprocess.DEVNULL, stdout=log, stderr=log)
        subprocess.Popen(["caffeinate", "-d", "-w", str(app.pid)])  # Hidden windows aren't drawn.
        for _ in range(100):
            if self.run(self.args.app, "status").returncode == 0:
                break
            time.sleep(0.2)
        # The installer needs the default layout, which the app writes on its first start.
        installer = [sys.executable, f"{REPO}/scripts/install-herdr-avatar.py", "--app-home", f"{self.root}/home"]
        for _ in range(25):
            if subprocess.run(installer, capture_output=True, check=False).returncode == 0:
                return app
            time.sleep(0.4)
        sys.exit("Could not install the widget; see " + self.root)

    def scenarios(self):
        reports = [sys.executable, "-u", f"{self.root}/reports.py"]
        agent = [sys.executable, "-u", f"{self.root}/agent.py"]
        results = []

        def both(label):
            for layout in ("main", "stick-s3"):
                self.do("layout.switch", {"name": layout})
                results.append(self.measure(f"{layout}-{label}"))

        self.do("space.new", {"name": "bench", "folder": self.root, "argv": reports + ["idle"]})
        both("idle")
        for name, state in (("w1", "working"), ("w2", "working"), ("b1", "blocked")):
            self.do("tab.new", {"name": name, "argv": reports + [state]})
        both("busy")
        self.do("tab.new", {"name": "four", "argv": agent})
        for direction in ("right", "down", "down"):
            self.do("pane.split", {"direction": direction, "argv": agent})
        both("4streams")
        spinners = [subprocess.Popen([sys.executable, "-c", "while True: pass"]) for _ in range(self.args.load)]
        try:
            both(f"busy+load{self.args.load}")
        finally:
            for spinner in spinners:
                spinner.kill()
        return results

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
            terminals = []  # The core is already gone.
        for terminal in terminals:
            self.run("hc", "kill", terminal["address"])
        self.run("hc", "server", "stop")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", default="/Applications/Herdr.app/Contents/MacOS/Herdr", help="Herdr executable")
    parser.add_argument("--seconds", type=int, default=8, help="measurement time per scenario")
    parser.add_argument("--load", type=int, default=12, help="busy processes for the loaded scenario")
    parser.add_argument("--label", default="run", help="label printed on each row")
    bench = Bench(parser.parse_args())
    app = None
    try:
        with open(f"{bench.root}/app.log", "w", encoding="utf-8") as log:
            try:
                app = bench.start_app(log)
                results = bench.scenarios()
                with open(f"{bench.root}/results.json", "w", encoding="utf-8") as file:
                    json.dump(results, file, indent=1)
                print("results:", f"{bench.root}/results.json")
            finally:
                bench.stop(app)
    except OSError as error:
        sys.exit(f"Benchmark failed: {error}")


if __name__ == "__main__":
    main()
