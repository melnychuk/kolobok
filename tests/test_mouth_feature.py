"""The default eyes-only feature flag and preserved opt-in mouth stay tested."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class MouthFeatureTests(unittest.TestCase):
    def test_default_off_and_explicit_opt_in(self):
        with tempfile.TemporaryDirectory(prefix="stick-mouth-flag-") as directory:
            for enabled in (False, True):
                binary = Path(directory) / ("enabled" if enabled else "default")
                flags = ["-DSTICK_S3_ENABLE_MOUTH=1"] if enabled else []
                subprocess.run(["g++", "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                                *flags, str(ROOT / "tests/test_mouth_flag.cpp"), "-o", str(binary)],
                               check=True)
                subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
