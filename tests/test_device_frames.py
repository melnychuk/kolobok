"""The device's own frames, run on the host: firmware/firmware.ino through a
scripted session (tests/device/device_frames.cpp), compared with a recorded hash.

A change that is meant to alter what the Stick draws updates EXPECTED, after
looking at the frames (`device_frames --ppm MS`). A refactor must not.
"""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Every frame of the session (tests/device/device_frames.cpp), hashed in order.
EXPECTED = "total 915bfbff1e28b871 frames 1577"


class DeviceFramesTests(unittest.TestCase):
    def test_firmware_frames_are_unchanged(self):
        with tempfile.TemporaryDirectory(prefix="stick-device-frames-") as directory:
            binary = Path(directory) / "device_frames"
            # No fused multiply-add, so every host computes the same pixels.
            subprocess.run(["g++", "-std=c++17", "-O2", "-ffp-contract=off",
                            f"-I{ROOT / 'tests/device'}", f"-I{ROOT / 'firmware'}",
                            str(ROOT / "tests/device/device_frames.cpp"), "-o", str(binary)],
                           check=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True,
                                    timeout=120)
        self.assertEqual(result.stdout.splitlines()[-1], EXPECTED)


if __name__ == "__main__":
    unittest.main()
