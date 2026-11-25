#!/usr/bin/env bash
# Install Kolobok's bar icon for Omarchy's Agents widget. This only copies
# files; it doesn't touch the widget, restart the shell or talk to Herdr.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source_dir="$root/omarchy/kolobok"
data_dir="${XDG_DATA_HOME:-$HOME/.local/share}/stick-s3"

# The art is exported from the menu bar app's Swift; refuse a stale copy.
python3 "$root/scripts/export-kolobok-art.py" --check

install_icon() {
  local target=$1 entry=$2 file name
  mkdir -p "$target"
  for file in kolobok-avatar.mjs kolobok-art.mjs KolobokIcon.qml; do
    name=$file
    if [[ $file == KolobokIcon.qml ]]; then name=$entry; fi
    install -m 0644 "$source_dir/$file" "$target/$name.new"
    mv -f "$target/$name.new" "$target/$name"
  done
}

install_icon "$data_dir/kolobok" KolobokIcon.qml
# Keep loading in existing Agents widgets that look for the old entry point.
install_icon "$data_dir/baa" BaaIcon.qml
echo "Installed Kolobok's bar icon in $data_dir/kolobok (with a legacy widget entry point)"
