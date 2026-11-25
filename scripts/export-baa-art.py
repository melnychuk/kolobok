#!/usr/bin/env python3
"""Export BAA's art and timings from the Swift sources for the Omarchy icon.

The menu bar app (menu-bar/Sources/BAA) is the source of truth. This writes
omarchy/baa/baa-art.mjs: the hand-drawn grids from AvatarPixels.swift, each
face's eye placement, the layout constants from Avatar.swift and the
celebration's timings from Celebration.swift, so omarchy/baa/baa-avatar.mjs
carries only the drawing logic. Run it after changing any of them:

    python3 scripts/export-baa-art.py

`--check` exits non-zero if the file is out of date, without writing it.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ROOT / "menu-bar" / "Sources" / "BAA"
OUTPUT = ROOT / "omarchy" / "baa" / "baa-art.mjs"


def need(match, what):
    if not match:
        sys.exit(f"export-baa-art: couldn't find {what} in the Swift sources")
    return match


def arts(text):
    found = {}
    for match in re.finditer(r"static let (\w+) = Art\(rows1: \[(.*?)\], rows2: \[(.*?)\]\)", text, re.S):
        found[match.group(1)] = [re.findall(r'"([^"]*)"', match.group(2)),
                                 re.findall(r'"([^"]*)"', match.group(3))]
    need(found.get("body"), "the body art")
    return found


def eyes(text, faces):
    body = need(re.search(r"static func eyes\(.*?switch face \{(.*?)\n        \}", text, re.S), "eyes()").group(1)
    table = {}
    for match in re.finditer(r"case \.(\w+): \(eyes, lidsMeet\) = \(pair\((\w+), (\d+), (\d+)\), (\d+)\)", body):
        face, art, left, top, meet = match.groups()
        table[face] = {"eyes": [[art, int(left), int(top)], [art, int(left) + 8, int(top)]], "lidsMeet": int(meet)}
    for match in re.finditer(r"case \.(\w+):\s*\(eyes, lidsMeet\) = \(\[(.*?)\], (\d+)\)", body, re.S):
        face, listed, meet = match.groups()
        placed = re.findall(r"Eye\(art: (\w+), left: (\d+), top: (\d+)\)", listed)
        table[face] = {"eyes": [[art, int(left), int(top)] for art, left, top in placed], "lidsMeet": int(meet)}
    blinks = need(re.search(r"case ([^:]+): return true", faces), "Face.blinks").group(1)
    for face in table:
        table[face]["blinks"] = f".{face}" in blinks
    return table


def number(text, name):
    return float(need(re.search(rf"static (?:let|var) {name}(?:: \w+)? = ([0-9.]+)", text), name).group(1))


def export():
    pixels = (SOURCES / "AvatarPixels.swift").read_text()
    avatar = (SOURCES / "Avatar.swift").read_text()
    celebration = (SOURCES / "Celebration.swift").read_text()
    sizes = {name: int(number(avatar, name)) for name in
             ("faceWidth", "faceHeight", "sideHeight", "itemHeight", "doneWidth", "tuck", "moat", "burstSteps")}
    sizes["starInner"] = number(avatar, "starInner")
    colors = {name: "#" + "".join(f"{int(v, 16):02X}" for v in need(re.search(
        rf"static let {name} = rgb\(0x(\w\w), 0x(\w\w), 0x(\w\w)\)", avatar), name).groups())
        for name in ("working", "done", "request", "requestPeak")}
    flickers = [float(v) for v in need(re.search(r"static let flickers: \[TimeInterval\] = \[(.*?)\]",
                                                 celebration), "flickers").group(1).split(",")]
    timing = {name: number(celebration, name) for name in
              ("chargeLength", "frightLength", "jumpLength", "burstLength", "goneLength")}
    timing.update({name: int(number(celebration, name)) for name in ("peekHidden", "dropPeek")})
    beats = need(re.search(r"var length: TimeInterval \{\s*switch self \{(.*?)\n            \}", celebration, re.S),
                 "Beat.length").group(1)
    order = need(re.search(r"enum Beat: CaseIterable, Equatable \{(.*?)\n        var length", celebration, re.S),
                 "Beat cases").group(1)
    names = re.findall(r"^\s*case ([\w, .]+)$", order, re.M)
    beat_order = [name.strip() for line in names for name in line.split(",")]
    lengths = {}
    for match in re.finditer(r"case ([.\w, ]+): return ([0-9.]+)", beats):
        for name in match.group(1).split(","):
            lengths[name.strip().lstrip(".")] = float(match.group(2))
    data = {
        "sizes": sizes, "colors": colors, "art": arts(pixels), "eyes": eyes(pixels, avatar),
        "celebration": {**timing, "flickers": flickers,
                        "beats": [[name, lengths[name]] for name in beat_order]},
    }
    return ("// BAA's art and timings, exported from menu-bar/Sources/BAA by\n"
            "// scripts/export-baa-art.py. Don't edit: change the Swift and re-export.\n"
            f"export default {json.dumps(data, indent=1)};\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if omarchy/baa/baa-art.mjs is out of date")
    args = parser.parse_args()
    text = export()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != text:
            sys.exit("omarchy/baa/baa-art.mjs is out of date: run scripts/export-baa-art.py")
        return
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(text)
    print(f"wrote {OUTPUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
