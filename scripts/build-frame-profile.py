#!/usr/bin/env python3
"""Prepare an opt-in physical StickS3 timing build and provenance manifest. Never flash."""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FQBN = "m5stack:esp32:m5stack_sticks3"


def source_fingerprint(root=ROOT):
    digest = hashlib.sha256()
    entries = []
    for path in sorted((root / "firmware").rglob("*")):
        if path.suffix not in {".ino", ".h", ".hpp", ".cpp", ".c", ".inl"}:
            continue
        data = path.read_bytes()
        name = path.relative_to(root).as_posix()
        digest.update(name.encode() + b"\0" + data + b"\0")
        entries.append({"path": name, "sha256": hashlib.sha256(data).hexdigest()})
    if not entries:
        raise ValueError("No firmware sources found")
    return digest.hexdigest(), entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--optimization", choices=("Os", "O2"), default="Os")
    args = parser.parse_args()
    sys.path.insert(0, str(ROOT))
    from bridge.frame_profile import build_fingerprint
    build = (args.build_dir or Path(tempfile.mkdtemp(prefix="stick-s3-profile-build-"))).resolve()
    if build.is_relative_to(ROOT):
        parser.error("Build products must remain outside the repository")
    build.mkdir(parents=True, mode=0o700, exist_ok=True)
    info = build.stat()
    if info.st_uid != os.getuid() or info.st_mode & 0o022:
        parser.error("Build directory must be owned by this user and not writable by others")
    fingerprint, sources = source_fingerprint()
    recipe_fingerprint = build_fingerprint(fingerprint, args.optimization)
    build_id = recipe_fingerprint[:8]
    match = re.search(r"constexpr\s+uint32_t\s+kFrameMs\s*=\s*(\d+)\s*;", (ROOT / "firmware/firmware.ino").read_text())
    if match is None or not 1 <= int(match[1]) <= 1000:
        parser.error("Cannot establish the firmware's constant frame target")
    period_us = int(match[1]) * 1000
    flags = f"-DSTICK_S3_PROFILE=1 -DSTICK_S3_PROFILE_BUILD_ID=0x{build_id}u"
    subprocess.run(["arduino-cli", "compile", "--fqbn", FQBN, "--build-path", str(build),
                    "--build-property", f"compiler.cpp.extra_flags={flags}",
                    "--build-property", f"compiler.optimization_flags=-{args.optimization}",
                    "firmware"], cwd=ROOT, check=True)
    after, _ = source_fingerprint()
    if after != fingerprint:
        raise SystemExit("Firmware changed during compilation; rebuild before deployment")
    libraries = []
    for path in (
        Path.home() / "Arduino/libraries/M5GFX/src/M5GFX.cpp",
        Path.home() / "Arduino/libraries/M5GFX/src/lgfx/v1/panel/Panel_LCD.inl",
        Path.home() / ".arduino15/packages/m5stack/hardware/esp32/3.3.9/cores/esp32/HWCDC.cpp",
    ):
        if path.is_file():
            libraries.append({"name": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    manifest = {"schema": "stick-s3-profile-build/v1", "build_id": build_id,
                "source_sha256": fingerprint, "sources": sources, "libraries": libraries,
                "optimization": args.optimization, "build_sha256": recipe_fingerprint,
                "git_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "fqbn": FQBN, "profiling_enabled": True,
                "target": "physical_arduino_m5unified", "period_us": period_us,
                "pixel_bytes": 240 * 135 * 2, "source_assumed_spi_hz": 40000000,
                "firmware_bin_sha256": hashlib.sha256((build / "firmware.ino.bin").read_bytes()).hexdigest()}
    output = build / "frame-profile-manifest.json"
    fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "w") as stream:
        json.dump(manifest, stream, indent=2)
        stream.write("\n")
    print(f"Profile build {build_id}: {build}")
    print(f"Manifest: {output}")
    print("Prepared only: no upload, serial access, or service changes performed.")


if __name__ == "__main__":
    main()
