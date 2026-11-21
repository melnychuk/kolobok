#!/usr/bin/env python3
"""Install the Stick avatar and make a personal Herdr Desktop layout."""

import argparse
import json
import os
import re
import shutil
import tempfile
from pathlib import Path

MOD = "stick-s3-avatar"
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "herdr" / MOD
SLOT = 76
# Herdr's check command generates its own version-specific type/tooling files.
IGNORE_GENERATED = shutil.ignore_patterns("*.d.ts", "jsconfig.json", ".DS_Store")


def name(value):
    if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_-]*", value):
        raise argparse.ArgumentTypeError("use letters, digits, hyphens or underscores")
    return value


def walk(node):
    yield node
    for child in node.get("children", []):
        yield from walk(child)


def add_widget(layout):
    if any(n.get("id") == MOD or (n.get("content") or {}).get("mod") == MOD for n in walk(layout)):
        raise ValueError("The source layout already contains the Stick avatar")
    sidebar = next((n for n in walk(layout) if n.get("id") == "sidebar"), None)
    positions = [
        (parent["children"], index)
        for parent in walk(sidebar or {}) if parent.get("type") == "column"
        for index, child in enumerate(parent.get("children", []))
        if child.get("type") == "block" and (child.get("content") or {}).get("mod") == "agents"
    ]
    if len(positions) != 1:
        raise ValueError("Expected one Agents block in the sidebar column; use the manual layout instructions")
    children, index = positions[0]
    children.insert(index, {
        "id": MOD, "type": "block", "size": {"px": SLOT}, "min": 0, "max": SLOT,
        "content": {"mod": MOD},
    })


def read_layout(path):
    if not path.is_file():
        raise ValueError(f"Layout not found: {path}. Open Herdr Desktop once, or choose another layout.")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (ValueError, OSError) as error:
        raise ValueError(f"Could not read layout {path}: {error}") from error


def install(home, source_name, target_name, node, upgrade=False, usb_serial=None):
    layouts = home / "layouts"
    target = layouts / f"{target_name}.json"
    destination = home / "mods" / MOD
    if target.is_symlink() or destination.is_symlink():
        raise ValueError("Refusing to replace a symlink")
    if upgrade:
        layout = read_layout(target)
        blocks = [n for n in walk(layout) if (n.get("content") or {}).get("mod") == MOD]
        if len(blocks) != 1:
            raise ValueError(f"Expected exactly one Stick avatar block in {target}")
        blocks[0].update(size={"px": SLOT}, min=0, max=SLOT)
    else:
        if target.exists():
            raise ValueError(f"Refusing to overwrite {target}; use --upgrade or choose a different --name")
        layout = read_layout(layouts / f"{source_name}.json")
        add_widget(layout)
        layout.pop("cloned_from", None)

    if not node:
        raise ValueError("Node.js is required. Install Node.js 20+ or specify --node /path/to/node.")
    try:
        executable = Path(node).is_file() and os.access(node, os.X_OK)
    except (OSError, ValueError) as error:
        raise ValueError(f"Cannot check the Node.js executable: {error}") from error
    if not executable:
        raise ValueError(f"Not an executable Node.js path: {node}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".stick-avatar-", dir=destination.parent) as temp:
        staged = Path(temp) / MOD
        shutil.copytree(SOURCE, staged, ignore=IGNORE_GENERATED)
        for runtime_file in (ROOT / "herdr/runtime").glob("*.mjs"):
            shutil.copy2(runtime_file, staged / runtime_file.name)
        shutil.copy2(ROOT / "site/avatar.wasm", staged / "avatar.wasm")
        if usb_serial is None and (destination / "runtime.json").is_file():
            usb_serial = read_layout(destination / "runtime.json").get("usbSerial")
        (staged / "runtime.json").write_text(
            json.dumps({"node": node, "usbSerial": usb_serial or ""}) + "\n", encoding="utf-8"
        )
        if upgrade:
            backup = Path(tempfile.mkdtemp(prefix="stick-s3-avatar-backup-", dir=home))
            shutil.copy2(target, backup / target.name)
            if destination.exists():
                shutil.copytree(destination, backup / MOD)
            print(f"Backup: {backup}")
        elif destination.exists() and any(
            not (destination / path.name).is_file() or (destination / path.name).read_bytes() != path.read_bytes()
            for path in staged.iterdir() if path.is_file()
        ):
            raise ValueError(f"Existing mod at {destination} differs; use --upgrade on its layout")
        if upgrade and destination.exists():
            destination.rename(Path(temp) / "previous")
        if not destination.exists():
            staged.rename(destination)
        contents = json.dumps(layout, indent=2, ensure_ascii=False) + "\n"
        if upgrade:
            replacement = Path(temp) / target.name
            replacement.write_text(contents, encoding="utf-8")
            replacement.replace(target)
        else:
            with target.open("x", encoding="utf-8") as file:
                file.write(contents)
    return target


def main():
    default_home = os.environ.get("HERDR_APP_HOME") or (
        Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config") / "herdr-app"
    )
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-home", type=Path, default=default_home)
    parser.add_argument("--layout", type=name, default="main", help="layout to copy (default: main)")
    parser.add_argument("--name", type=name, default="stick-s3", help="target layout name (default: stick-s3)")
    parser.add_argument("--node", default=shutil.which("node"), help="absolute Node.js executable")
    parser.add_argument("--usb-serial", help="match only this Stick USB serial; empty clears the filter")
    parser.add_argument("--upgrade", action="store_true", help="back up and replace an existing widget in place")
    args = parser.parse_args()
    try:
        node = str(Path(args.node).expanduser().absolute()) if args.node else ""
        target = install(args.app_home.expanduser(), args.layout, args.name, node, args.upgrade, args.usb_serial)
    except (OSError, ValueError, TypeError, AttributeError) as error:
        parser.exit(1, f"{error}\n")
    print(f"Installed {target}. Select {args.name!r} in Herdr Desktop's layout picker.")


if __name__ == "__main__":
    main()
