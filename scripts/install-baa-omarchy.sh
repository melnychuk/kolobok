#!/usr/bin/env bash
# Install BAA's bar icon for Omarchy's Agents widget: copies omarchy/baa
# (BaaIcon.qml and its JavaScript) to $XDG_DATA_HOME/stick-s3/baa, where the
# widget loads it. It only copies files: it doesn't touch the widget, restart
# the shell or talk to Herdr. The widget picks the icon up the next time the
# shell loads it; until then (or without this) it keeps its robot glyph.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source_dir="$root/omarchy/baa"
target="${XDG_DATA_HOME:-$HOME/.local/share}/stick-s3/baa"

# The art is exported from the menu bar app's Swift; refuse a stale copy.
python3 "$root/scripts/export-baa-art.py" --check

mkdir -p "$target"
for file in BaaIcon.qml baa-avatar.mjs baa-art.mjs; do
  install -m 0644 "$source_dir/$file" "$target/$file.new"
  mv -f "$target/$file.new" "$target/$file"
done
echo "Installed BAA's bar icon in $target"
