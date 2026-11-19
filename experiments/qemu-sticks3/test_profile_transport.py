#!/usr/bin/env python3
"""Synthetic timing-packet transport tests, not physical CPU/FPS measurements.

Run from the repository with PYTHONPATH=. and the existing STICKS3_QEMU variables.
Uses only the local QEMU CDC socket/MMIO; never opens a physical serial device.
"""
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_peripherals import QEMU, USB, Harness

from bridge.frame_profile import decode_profile
from bridge.voice import FrameParser

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(QEMU, "set STICKS3_QEMU to the patched executable")
class ProfileTransport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="stick-profile-codec-")
        cls.addClassCleanup(cls.temp.cleanup)
        binary = Path(cls.temp.name) / "codec"
        subprocess.run(["g++", "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "tests/test_frame_profile.cpp"), "-o", str(binary)], check=True)
        # Values inside this packet are preset fixtures, not measured emulator timings.
        cls.wire = subprocess.check_output([str(binary), "--fixture"])
        cls.listening_wire = subprocess.check_output([str(binary), "--listening-fixture"])
        # Explicit replay fixtures, not new physical/QEMU CPU measurements.
        cls.occupancy_wire = subprocess.check_output([str(binary), "--occupancy-fixtures"])
        cls.cache_wire = subprocess.check_output([str(binary), "--cache-fixtures"])
        cls.eye_wire = subprocess.check_output([str(binary), "--eye-pass-fixture"])

    def setUp(self):
        self.h = Harness()
        self.addCleanup(self.h.close)

    def send(self, wire):
        received = bytearray()
        for offset in range(0, len(wire), 64):
            chunk = wire[offset:offset + 64]
            for byte in chunk:
                self.h.write(USB, byte)
            if len(chunk) < 64:
                self.h.write(USB + 4, 1)
            self.h.step()
            target = len(received) + len(chunk)
            while len(received) < target:
                data = self.h.cdc.recv(target - len(received))
                if not data:
                    raise AssertionError("QEMU CDC closed")
                received.extend(data)
        return bytes(received)

    def test_native_timing_packet_crosses_fifo_boundary_and_decodes(self):
        received = self.send(self.wire)
        self.assertEqual(received, self.wire)
        parser = FrameParser()
        self.assertEqual(parser.feed(received[:64]), [])
        frames = parser.feed(received[64:])
        self.assertEqual(len(frames), 1)
        decoded = decode_profile(*frames[0])
        if decoded is None:
            raise AssertionError("Synthetic timing packet did not decode")
        self.assertEqual(decoded["build_id"], "12345678")
        self.assertEqual(decoded["us"]["total"], 24000)

    def test_round2_occupancy_fixtures_preserve_mode_and_recipe_ids(self):
        received = self.send(self.occupancy_wire)
        self.assertEqual(received, self.occupancy_wire)
        frames = FrameParser().feed(received)
        self.assertEqual(len(frames), 2)
        for frame, build, total in zip(frames, ("a4978b47", "9d40fa19"),
                                      (55874, 48684), strict=True):
            decoded = decode_profile(*frame)
            if decoded is None:
                raise AssertionError("Occupancy replay fixture did not decode")
            self.assertEqual(decoded["build_id"], build)
            self.assertEqual(decoded["state"], "working")
            self.assertEqual(decoded["layout_flags"], 1)
            self.assertEqual(decoded["us"]["total"], total)
            self.assertGreater(decoded["us"]["total"], decoded["period_us"])

    def test_cache_pass_occupancy_replay_preserves_new_source_ids(self):
        received = self.send(self.cache_wire)
        self.assertEqual(received, self.cache_wire)
        frames = FrameParser().feed(received)
        self.assertEqual(len(frames), 2)
        for frame, build, total in zip(frames, ("7dfcc87d", "2bf53c52"),
                                      (41248, 37166), strict=True):
            decoded = decode_profile(*frame)
            if decoded is None:
                raise AssertionError("Cache replay fixture did not decode")
            self.assertEqual(decoded["build_id"], build)
            self.assertEqual(decoded["state"], "working")
            self.assertEqual(decoded["layout_flags"], 1)
            self.assertEqual(decoded["us"]["total"], total)  # Preset quantile fixture only.
            self.assertGreater(total, decoded["period_us"])

    def test_authored_listening_mode_preserved_without_occupancy_calibration(self):
        received = self.send(self.listening_wire)
        self.assertEqual(received, self.listening_wire)
        frames = FrameParser().feed(received)
        self.assertEqual(len(frames), 1)
        decoded = decode_profile(*frames[0])
        if decoded is None:
            self.fail("Listening fixture did not decode")
        self.assertEqual(decoded["state"], "listening")
        self.assertEqual(decoded["build_id"], "12345679")
        self.assertEqual(decoded["us"]["total"], 24000)  # Synthetic only.

    def test_unmeasured_eye_revision_identity_without_cost_calibration(self):
        received = self.send(self.eye_wire)
        self.assertEqual(received, self.eye_wire)
        frames = FrameParser().feed(received)
        self.assertEqual(len(frames), 1)
        decoded = decode_profile(*frames[0])
        if decoded is None:
            self.fail("Unmeasured eye fixture did not decode")
        self.assertEqual(decoded["build_id"], "b088706a")
        self.assertEqual(decoded["state"], "working")
        self.assertEqual(decoded["layout_flags"], 1)
        self.assertEqual(decoded["us"]["total"], 24000)  # Unchanged placeholder, NOT measured.

    def test_crc_corruption_rejected_and_next_packet_survives(self):
        damaged = bytearray(self.wire)
        damaged[-1] ^= 1
        received = self.send(bytes(damaged)) + self.send(self.wire)
        frames = FrameParser().feed(received)
        self.assertEqual(len(frames), 2)
        self.assertIsNone(decode_profile(*frames[0]))
        self.assertIsNotNone(decode_profile(*frames[1]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
