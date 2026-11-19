#!/usr/bin/env bash
# Copy an Arduino build's flash parts into DEST with a manifest for
# `install.sh --flash`: offsets and esptool options match `arduino-cli upload`.
set -euo pipefail
build=${1:?usage: package-firmware.sh BUILD_DIR DEST_DIR}
dest=${2:?usage: package-firmware.sh BUILD_DIR DEST_DIR}
mkdir -p "$dest"
cp "$build/firmware.ino.bootloader.bin" "$dest/bootloader.bin"
cp "$build/firmware.ino.partitions.bin" "$dest/partitions.bin"
cp "$build/boot_app0.bin" "$dest/boot_app0.bin"
cp "$build/firmware.ino.bin" "$dest/firmware.bin"
python3 - "$dest" "$(git rev-parse HEAD 2>/dev/null || echo unknown)" <<'PY'
import hashlib, json, pathlib, sys
dest, commit = pathlib.Path(sys.argv[1]), sys.argv[2]
parts = [("0x0", "bootloader.bin"), ("0x8000", "partitions.bin"),
         ("0xe000", "boot_app0.bin"), ("0x10000", "firmware.bin")]
manifest = {"chip": "esp32s3", "board": "m5stack:esp32:m5stack_sticks3", "commit": commit,
            "files": [{"offset": offset, "name": name,
                       "sha256": hashlib.sha256((dest / name).read_bytes()).hexdigest()}
                      for offset, name in parts]}
(dest / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"Packaged firmware {commit[:7]} into {dest}")
PY
