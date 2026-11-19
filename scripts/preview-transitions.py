#!/usr/bin/env python3
"""Private synthetic transition gallery using the production header controller.

No live task/serial/audio access. Still frames are not device FPS evidence.
"""
import argparse
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SIZE = 240 * 135 * 3
# active, blocked, done, listening, healthy (aggregate fixtures only)
WORK = (2, 0, 0, 0, 1)
ASK = (2, 1, 0, 0, 1)
DONE = (0, 0, 2, 0, 1)
IDLE = (0, 0, 0, 0, 1)
LISTEN = (0, 0, 0, 1, 1)
OFFLINE = (0, 0, 0, 0, 0)
SCENARIOS = {
    "request-and-resume": [(0, WORK), (2400, ASK), (4800, WORK)],
    "completion-and-return": [(0, WORK), (2400, (1, 0, 1, 0, 1))],
    "listen-and-release": [(0, WORK), (2400, LISTEN), (4800, WORK)],
    "interrupted-request": [(0, WORK), (2400, ASK), (2600, LISTEN), (4800, ASK)],
    "offline-and-recovery": [(0, WORK), (2400, OFFLINE), (4800, DONE)],
    "doze-and-wake": [(0, IDLE), (6000, WORK)],
    "sorting-ownership": [(0, (2, 0, 1, 0, 1)), (2400, (1, 0, 2, 0, 1))],
    "resume-from-request": [(0, ASK), (2400, WORK)],
    "release-to-work": [(0, LISTEN), (2400, WORK)],
    "recovery-not-celebration": [(0, OFFLINE), (2400, DONE)],
    "wake-to-work": [(0, IDLE), (6000, WORK)],
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output or Path(tempfile.mkdtemp(prefix="stick-s3-transitions-"))
    output.mkdir(parents=True, exist_ok=True)
    sheet = Image.new("RGB", (6 * 240, len(SCENARIOS) * 165), "#20242a")
    draw = ImageDraw.Draw(sheet)
    html = ["<!doctype html><meta charset='utf-8'><title>Avatar transitions</title>",
            "<h1>Synthetic transition review — native, not physical timing</h1>"]
    with tempfile.TemporaryDirectory(prefix="stick-s3-transition-build-") as temp:
        binary = Path(temp) / "renderer"
        subprocess.run(["g++", "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "experiments/avatar-widget/main.cpp"), "-o", str(binary)], check=True)
        for row, (name, events) in enumerate(SCENARIOS.items()):
            times = sorted(set(range(0, 8400, 33)) | {time for time, _ in events})
            commands = []
            index = 0
            for time in times:
                while index + 1 < len(events) and events[index + 1][0] <= time:
                    index += 1
                commands.append(" ".join(map(str, (time, *events[index][1]))) + "\n")
            data = subprocess.run([str(binary)], input="".join(commands).encode(),
                                  capture_output=True, check=True).stdout
            if len(data) != len(times) * SIZE:
                raise SystemExit("Incomplete native frame stream")
            frames = [Image.frombytes("RGB", (240, 135), data[i:i + SIZE])
                      for i in range(0, len(data), SIZE)]
            # Quantize cumulative time, not each duration, to preserve elapsed
            # time in GIF's 10ms clock (including exact status-edge samples).
            ticks = [round(time / 10) for time in times]
            durations = [(ticks[i + 1] - ticks[i]) * 10 for i in range(len(ticks) - 1)] + [30]
            frames[0].save(output / f"{name}.gif", save_all=True, append_images=frames[1:],
                           duration=durations, loop=0, disposal=2, optimize=False)
            edge = 4000 if name == "doze-and-wake" else events[1][0]
            samples = [edge - 150, edge, edge + 140, edge + 340, edge + 650, edge + 1200]
            draw.text((8, row * 165 + 2), name, fill="white")
            for col, time in enumerate(samples):
                index = min(range(len(times)), key=lambda i: abs(times[i] - time))
                sheet.paste(frames[index], (col * 240, row * 165 + 18))
                draw.text((col * 240 + 6, row * 165 + 151), str(times[index]), fill="white")
            html.append(f"<h2>{name}</h2><img src='{name}.gif'>")
    sheet.save(output / "transitions-contact.png")
    (output / "index.html").write_text("\n".join(html))
    print(output)


if __name__ == "__main__":
    main()
