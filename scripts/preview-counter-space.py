#!/usr/bin/env python3
"""Synthetic native numeral-space gallery; never accesses hardware or live tasks."""
import argparse
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
FRAME_BYTES = 240 * 135 * 3


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output or Path(tempfile.mkdtemp(prefix="stick-s3-counter-space-"))
    output.mkdir(parents=True, exist_ok=True)
    fixtures = [(0, 1, "I"), (0, 8, "VIII"), (0, 18, "XVIII"),
                (0, 38, "XXXVIII"), (0, 88, "LXXXVIII"), (0, 100, "XCIX+")]
    fixtures += [(done, active, name) for active, done, name in fixtures.copy()]
    fixtures += [(3, 2, "II / III"), (8, 8, "VIII / VIII"),
                 (18, 18, "XVIII / XVIII"), (88, 1, "I / LXXXVIII"),
                 (88, 18, "XVIII / LXXXVIII"), (38, 38, "XXXVIII / XXXVIII"),
                 (88, 88, "LXXXVIII / LXXXVIII"), (100, 100, "XCIX+ / XCIX+")]
    with tempfile.TemporaryDirectory(prefix="stick-counter-build-") as temp:
        binary = Path(temp) / "avatar"
        subprocess.run(["g++", "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "experiments/avatar-widget/main.cpp"), "-o", str(binary)], check=True)
        sheet = Image.new("RGB", (6 * 480, ((len(fixtures) + 5) // 6) * 304), "#20242a")
        draw = ImageDraw.Draw(sheet)
        for index, (active, done, label) in enumerate(fixtures):
            commands = "".join(f"{ms} {active} 0 {done} 0 1\n" for ms in range(0, 6000, 33))
            raw = subprocess.check_output([str(binary)], input=commands.encode())
            assert len(raw) % FRAME_BYTES == 0
            frame = Image.frombytes("RGB", (240, 135), raw[-FRAME_BYTES:])
            x, y = index % 6 * 480, index // 6 * 304
            draw.text((x + 8, y + 6), f"{'green left' if done and not active else 'blue right' if active and not done else 'paired'}: {label}", fill="white")
            sheet.paste(frame.resize((480, 270)), (x, y + 25))
        sheet.save(output / "counter-space.png")
        # Side labels accommodate the face; bottom labels grow inward while
        # the avatar stays full-size. Fixtures never touch live task data.
        for side in ("done", "active", "paired"):
            commands = []
            for ms in range(0, 12000, 33):
                count = (1, 8, 18, 88, 8, 1)[min(ms // 2000, 5)]
                commands.append(f"{ms} {count if side != 'done' else 0} 0 {count if side != 'active' else 0} 0 1\n")
            raw = subprocess.check_output([str(binary)], input="".join(commands).encode())
            frames = [Image.frombytes("RGB", (240, 135), raw[i:i + FRAME_BYTES])
                      for i in range(0, len(raw), FRAME_BYTES)]
            frames[0].save(output / f"{side}-reflow.gif", save_all=True, append_images=frames[1:],
                           duration=[(30, 30, 40)[i % 3] for i in range(len(frames))],
                           loop=0, disposal=2, optimize=False)
    print(output)


if __name__ == "__main__":
    main()
