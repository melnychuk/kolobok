#!/usr/bin/env python3
"""Render shared firmware animation kernels to local GIFs and contact sheets.

No device access. Uses only native C++ + Pillow; output defaults to a private
unique directory outside Git. Install scripts/requirements-preview.txt first.
Run: python3 scripts/preview-avatar.py [--output /tmp/stick-s3-avatar-review]
"""
import argparse
import io
import json
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FRAME_BYTES = 240 * 135 * 3


def save_animation(data: bytes, destination: Path) -> None:
    if not data or len(data) % FRAME_BYTES:
        raise SystemExit("Native renderer returned an incomplete frame")
    frames = [Image.frombytes("RGB", (240, 135), data[p:p + FRAME_BYTES])
              for p in range(0, len(data), FRAME_BYTES)]
    # GIF clocks have 10ms resolution: 30/30/40 preserves ~30fps instead of
    # truncating every 33ms frame to 30ms and speeding up the physics.
    durations = [(30, 30, 40)[i % 3] for i in range(len(frames))]
    frames[0].save(destination, save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, disposal=2, optimize=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output is None:
        args.output = Path(tempfile.mkdtemp(prefix="stick-s3-avatar-review-"))
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="stick-avatar-build-") as temp:
        binary = Path(temp) / "preview"
        subprocess.run(["g++", "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "tests/preview_avatar.cpp"), "-o", str(binary)], check=True)
        try:
            clips = json.loads(subprocess.check_output([str(binary), "--list"]))
        except json.JSONDecodeError as error:
            raise SystemExit("Native preview returned invalid clip metadata") from error
        if not isinstance(clips, list) or not clips:
            raise SystemExit("Expected authored clip metadata")
        counts = [sum(c["state"] == state for c in clips) for state in range(4)]
        if not all(counts) or any([c["index"] for c in clips if c["state"] == state]
                                  != list(range(count)) for state, count in enumerate(counts)):
            raise SystemExit("Expected contiguous clips in each of four states")
        (args.output / "clips.json").write_text(json.dumps(clips, indent=2))
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 14)
        groups = ["thinking", "done", "attention", "listening"]
        sheets = [Image.new("RGB", (1440, count * 170), "#20242a") for count in counts]
        html = ["<!doctype html><meta charset='utf-8'><title>Stick S3 animation review</title>",
                "<style>body{background:#20242a;color:white;font:16px sans-serif}"
                "section{display:grid;grid-template-columns:repeat(3,1fr);gap:16px}"
                "img{width:100%;image-rendering:auto}article{background:#30363e;padding:12px}</style>",
                f"<h1>Shared firmware-kernel visual review</h1><p>{len(clips)} authored sequences at 30 fps. "
                "Same acting, soft body, eye/mouth shapes, masks, and RGB565 colors as firmware. "
                "Host preview holds each layout stable; USB/state transitions need device confirmation.</p>"]
        for group in groups:
            html.append(f"<h2>{group.title()}</h2><section>")
            for clip in [c for c in clips if groups[c["state"]] == group]:
                s, i = clip["state"], clip["index"]
                data = subprocess.check_output([str(binary), str(s), str(i)])
                name = f"{group}-{i + 1}"
                save_animation(data, args.output / f"{name}.gif")
                sheet = sheets[s]
                draw = ImageDraw.Draw(sheet)
                draw.text((8, i * 170 + 3), f"{i + 1}. {clip['name']} — eye/body beats", font=font, fill="white")
                keys = clip["keys"]
                samples = [keys[min(k, len(keys) - 1)] for k in (1, 2, 3, 4)] + keys[-2:]
                for col, ms in enumerate(samples):
                    ppm = subprocess.check_output([str(binary), str(s), str(i), str(ms)])
                    frame = Image.open(io.BytesIO(ppm)).convert("RGB")
                    sheet.paste(frame, (col * 240, i * 170 + 24))
                    draw.text((col * 240 + 6, i * 170 + 150), f"{ms} ms", font=font, fill="#c0ccd9")
                html.append(f"<article><h3>{i + 1}. {clip['name']}</h3><img src='{name}.gif'></article>")
            html.append("</section>")
        for name, sheet in zip(groups, sheets, strict=True):
            sheet.save(args.output / f"{name}-contact.png")
        html.append("<h2>Status events — synthetic fixtures, not live task data</h2><section>")
        event_sheet = Image.new("RGB", (1440, 27 * 170), "#20242a")
        draw = ImageDraw.Draw(event_sheet)
        for row, (mode, title) in enumerate(((4, "New active count / reflow"),
                                              (5, "Task completed"),
                                              (6, "New task / vertical happy reaction"),
                                              (7, "Active curiosity / completed happiness + contact"),
                                              (8, "Short press / open completed wink"),
                                              (9, "Dictation release / acknowledgement"),
                                              (10, "Completed only / happy side touch"),
                                              (11, "Active only / focused side touch"),
                                              (12, "Completed I / narrow numeral touch"),
                                              (13, "Active I / narrow numeral touch"),
                                              (14, "Completed only / affectionate kiss"),
                                              (15, "Paired counters / kiss-shaped rim press"),
                                              (16, "Completion sorting / II + III becomes III + II"),
                                              (17, "Long Romans / swap while gathered"),
                                              (18, "Last active / I + I becomes II + hidden zero"),
                                              (20, "New task / I drops into the opened blue strip"),
                                              (21, "New task / II + falling I = III"),
                                              (22, "New task / IV falls and knocks III out"),
                                              (23, "New task / I drops beside completed II"),
                                              (24, "Completed / charge, flash, I flips left, face returns"),
                                              (25, "Completed / III charges, I flips, II remains active"),
                                              (26, "New task / paired II + falling I = III"),
                                              (27, "Loud noise / paired startle (synthetic trigger)"),
                                              (28, "Loud noise / vertical startle (synthetic trigger)"),
                                              (29, "Last completion opened / step over the green"),
                                              (30, "Split completion / active falls, done 2s later: held, then flip"),
                                              (31, "Split completion / done rises, active 2s later: held, then flip"))):
            name = f"event-{mode}"
            state = "1" if mode in (10, 12, 14, 15, 29) else "0"
            data = subprocess.check_output([str(binary), state, "2", "-1", str(mode)])
            save_animation(data, args.output / f"{name}.gif")
            draw.text((8, row * 170 + 3), title, font=font, fill="white")
            samples = ((1400, 3300, 3900, 4400, 5800, 7000) if mode >= 30 else
                       (800, 980, 1080, 1200, 1400, 1800) if mode == 29 else
                       (560, 680, 760, 1000, 1350, 1650) if mode >= 27 else
                       (1300, 1700, 1800, 2300, 3200, 4400) if mode in (24, 25) else
                       (1000, 1300, 1600, 1850, 2100, 2800) if mode in (23, 26) else
                       (900, 1300, 1600, 1850, 2100, 2800) if mode >= 20 else
                       (1000, 1500, 2100, 2500, 3100, 3700) if mode >= 16 else
                       (1700, 2100, 2700, 4900, 5600, 6100) if mode == 7 else
                       (4400, 4800, 5140, 5600, 6100, 7300) if mode in (10, 12, 14, 15) else
                       (1400, 1800, 2140, 2600, 3100, 3800) if mode in (11, 13) else
                       (660, 820, 960, 1130, 1400, 2000))
            for col, ms in enumerate(samples):
                ppm = subprocess.check_output([str(binary), state, "2", str(ms), str(mode)])
                event_sheet.paste(Image.open(io.BytesIO(ppm)), (col * 240, row * 170 + 24))
                draw.text((col * 240 + 6, row * 170 + 150), f"{ms} ms", font=font, fill="#c0ccd9")
            html.append(f"<article><h3>{title}</h3><img src='{name}.gif'></article>")
        event_sheet.save(args.output / "events-contact.png")
        html.append("</section>")
        html.append("<h2>Vertical layouts — mouth feature disabled by default</h2><section>")
        mouth_sheet = Image.new("RGB", (1440, 1020), "#20242a")
        draw = ImageDraw.Draw(mouth_sheet)
        fixtures = ((0, 6, 2, "Quiet focus", (420, 1000, 1890, 1980, 2150, 2850)),
                    (0, 4, 2, "Aha insight", (260, 990, 1110, 1300, 1900, 2540)),
                    (1, 0, 1, "Relief / smile", (180, 390, 640, 900, 1920, 2820)),
                    (1, 4, 1, "Playful wink", (220, 380, 720, 900, 1810, 2600)),
                    (0, 2, 2, "Pursed scan / side-to-side", (180, 760, 990, 1190, 1450, 1640)),
                    (0, 1, 2, "Pursed compare / left-right", (140, 350, 850, 1150, 1560, 1750)))
        for row, (state, index, mode, title, samples) in enumerate(fixtures):
            name = f"mouth-{row + 1}"
            data = subprocess.check_output([str(binary), str(state), str(index), "-1", str(mode)])
            save_animation(data, args.output / f"{name}.gif")
            draw.text((8, row * 170 + 3), title, font=font, fill="white")
            for col, ms in enumerate(samples):
                ppm = subprocess.check_output([str(binary), str(state), str(index), str(ms), str(mode)])
                mouth_sheet.paste(Image.open(io.BytesIO(ppm)), (col * 240, row * 170 + 24))
                draw.text((col * 240 + 6, row * 170 + 150), f"{ms} ms", font=font, fill="#c0ccd9")
            html.append(f"<article><h3>{title}</h3><img src='{name}.gif'></article>")
        mouth_sheet.save(args.output / "mouth-contact.png")
        html.append("</section>")
        # Numeral stress / one-counter layouts use the identical renderer.
        stress = Image.new("RGB", (720, 3 * 160), "#20242a")
        draw = ImageDraw.Draw(stress)
        for s in range(3):
            for col, mode in enumerate((1, 2, 3)):
                ppm = subprocess.check_output([str(binary), str(s), "0", "1200", str(mode)])
                stress.paste(Image.open(io.BytesIO(ppm)), (col * 240, s * 160 + 24))
                draw.text((col * 240 + 5, s * 160 + 3),
                          f"{groups[s]} / {['done only', 'active only', 'long Romans'][col]}",
                          font=font, fill="white")
        stress.save(args.output / "layout-stress.png")
        html.append("<h2>Layout / Roman numeral stress test</h2><img src='layout-stress.png'>")
        (args.output / "index.html").write_text("\n".join(html))
    print(f"{len(clips)} acting + 27 event + 6 vertical-mouth GIFs, contact sheets, layout stress: {args.output / 'index.html'}")


if __name__ == "__main__":
    main()
