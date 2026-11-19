import copy
import json
import os
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
import zlib
from pathlib import Path

from bridge.frame_profile import (
    BODY,
    CAPTURE_SCHEMA,
    PAYLOAD_SIZE,
    ProfileSink,
    build_fingerprint,
    decode_profile,
)
from bridge.frame_profile_report import distribution, summarize
from bridge.voice import FrameParser


def packet(sequence=9, dropped=0, skipped=0, observer=15):
    # Synthetic timing fixture, never a physical measurement or audio recording.
    values = (33000, 1000, 500, 4000, 1000, 2500, 500, 0, 1500, 13000, 24000)
    body = BODY.pack(b"S3PF", 1, 3, 1, 0, 0x12345678, sequence, sequence * 33,
                     dropped, skipped, observer, 240, 33000, *values)
    payload = body + struct.pack("<I", zlib.crc32(body))
    return b"S3V1T" + struct.pack("<H", len(payload)) + payload


def record(sequence=9):
    result = decode_profile("T", packet(sequence)[7:])
    if result is None:
        raise AssertionError("Bad synthetic fixture")
    return {**result, "host_dropped": 0, "host_invalid": 0}


def manifest():
    return {"schema": "stick-s3-profile-build/v1", "build_id": "12345678",
            "source_sha256": "12345678" + "0" * 56, "firmware_bin_sha256": "0" * 64,
            "profiling_enabled": True, "source_assumed_spi_hz": 40000000, "pixel_bytes": 64800}


def footer(count, dropped=0):
    return {"schema": CAPTURE_SCHEMA, "accepted": count, "limit": count,
            "host_dropped": dropped, "host_invalid": 0, "complete": True, "limit_reached": True}


class ProfileTests(unittest.TestCase):
    def test_native_codec_matches_python_and_existing_parser(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "codec"
            source = Path(__file__).resolve().with_suffix(".cpp")
            subprocess.run(["g++", "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True)
            wire = subprocess.check_output([str(binary), "--fixture"])
        parser = FrameParser()
        frames = []
        for byte in wire:
            frames.extend(parser.feed(bytes([byte])))
        self.assertEqual(len(wire), 95)
        self.assertEqual(len(frames), 1)
        result = decode_profile(*frames[0])
        if result is None:
            raise AssertionError("Native timing fixture did not decode")
        self.assertEqual(result["us"]["total"], 24000)
        self.assertEqual(result["build_id"], "12345678")

    def test_optimizer_recipe_changes_identity_and_validates_manifest(self):
        build = manifest()
        source = build["source_sha256"]
        self.assertEqual(build_fingerprint(source), source)
        recipe = build_fingerprint(source, "O2")
        self.assertNotEqual(recipe[:8], source[:8])
        build.update(optimization="O2", build_sha256=recipe, build_id=recipe[:8])
        rows = [record(i) for i in range(300)]
        for row in rows:
            row["build_id"] = recipe[:8]
        self.assertTrue(summarize(rows, build, "physical", footer(300))["calibration_ready"])
        build["build_sha256"] = "0" * 64
        with self.assertRaises(ValueError):
            summarize(rows, build, "physical", footer(300))
        with self.assertRaises(ValueError):
            build_fingerprint(source, "fast-math")

    def test_pre_window_startup_drop_counter_does_not_hide_in_window_losses(self):
        rows = [record(i) for i in range(300)]
        for row in rows:
            row["firmware_dropped"] = 30
        self.assertTrue(summarize(rows, manifest(), "physical", footer(300))["calibration_ready"])
        rows[-1]["firmware_dropped"] += 1
        self.assertFalse(summarize(rows, manifest(), "physical", footer(300))["calibration_ready"])

    def test_crc_version_and_bounds_reject_corruption(self):
        payload = packet()[7:]
        self.assertEqual(len(payload), PAYLOAD_SIZE)
        self.assertIsNone(decode_profile("A", payload))
        self.assertIsNone(decode_profile("T", payload[:-1]))
        damaged = bytearray(payload)
        damaged[45] ^= 1
        self.assertIsNone(decode_profile("T", damaged))
        damaged = bytearray(payload)
        damaged[4] = 2
        struct.pack_into("<I", damaged, len(damaged) - 4, zlib.crc32(damaged[:-4]))
        self.assertIsNone(decode_profile("T", damaged))

    def test_opt_in_sink_never_retains_audio_text_or_task_payloads(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "trace.jsonl"
            sink = ProfileSink(path, limit=1)
            self.assertFalse(sink.accept("A", packet()[7:]))
            self.assertFalse(sink.accept("T", b"BUTTON|focus"))
            self.assertFalse(sink.accept("T", b"private text fixture"))
            self.assertTrue(sink.accept("T", packet()[7:]))
            sink.close()
            self.assertIsNone(sink.error)
            self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)
            contents = path.read_text()
            self.assertNotIn("private", contents)
            self.assertNotIn("BUTTON", contents)
            rows = [json.loads(line) for line in contents.splitlines()]
            self.assertEqual(len(rows), 2)
            self.assertEqual(rows[-1]["schema"], CAPTURE_SCHEMA)
            self.assertTrue(rows[-1]["complete"])
            with self.assertRaises(FileExistsError):
                ProfileSink(path)

    def test_bounded_queue_records_tail_drops_in_footer(self):
        allow = threading.Event()

        class PausedSink(ProfileSink):
            def _write(self, fd):
                allow.wait(timeout=2)
                super()._write(fd)

        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "trace"
            sink = PausedSink(path, limit=3, queue_size=1)
            sink.accept("T", packet(1)[7:])
            sink.accept("T", packet(2)[7:])
            self.assertEqual(sink.dropped, 1)
            allow.set()
            sink.close()
            rows = [json.loads(line) for line in path.read_text().splitlines()]
            self.assertEqual(rows[-1]["host_dropped"], 1)
            self.assertTrue(rows[-1]["complete"])
            self.assertFalse(rows[-1]["limit_reached"])

    def test_percentiles_and_physical_gate_are_explicit(self):
        self.assertEqual(distribution([1, 2, 100])["p95"], 100)
        rows = [record(i) for i in range(300)]
        synthetic = summarize(rows, manifest(), "synthetic", footer(300))
        self.assertFalse(synthetic["calibration_ready"])
        physical = summarize(rows, manifest(), "physical", footer(300))
        self.assertTrue(physical["calibration_ready"])  # Tests a declaration gate, NOT real measurements.
        self.assertEqual(physical["cpu_render_us"]["p95"], 11000)
        self.assertEqual(physical["transport_candidates"]["pixel_wire_lower_bound_us"], 12960)
        self.assertEqual(physical["busy_deadline_misses"], 0)
        self.assertFalse(summarize(rows, manifest(), "qemu", footer(300))["calibration_ready"])
        self.assertFalse(summarize(rows, manifest(), "physical")["calibration_ready"])

    def test_cli_reads_completed_timing_file_without_promoting_synthetic_data(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            trace = root / "trace.jsonl"
            trace.write_text("\n".join(json.dumps(row) for row in [record(), footer(1)]) + "\n")
            build = root / "manifest.json"
            build.write_text(json.dumps(manifest()))
            report = root / "report.json"
            subprocess.run([sys.executable, "-m", "bridge.frame_profile_report", "--input", str(trace),
                            "--manifest", str(build), "--source", "synthetic", "--output", str(report)],
                           cwd=Path(__file__).resolve().parents[1], check=True, capture_output=True)
            result = json.loads(report.read_text())
            self.assertTrue(result["capture_complete"])
            self.assertFalse(result["calibration_ready"])
            self.assertEqual(report.stat().st_mode & 0o777, 0o600)

    def test_losses_observer_and_wrong_artifact_block_calibration(self):
        rows = [record(i) for i in range(300)]
        self.assertFalse(summarize(rows, manifest(), "physical", footer(300, 1))["calibration_ready"])
        rows[-1]["previous_observer_us"] = 2000
        rows[-1]["sequence"] += 2
        result = summarize(rows, manifest(), "physical", footer(300))
        self.assertFalse(result["calibration_ready"])
        self.assertEqual(result["sequence_gaps"], 2)
        bad = manifest()
        bad["build_id"] = "87654321"
        with self.assertRaises(ValueError):
            summarize(rows, bad, "physical", footer(300))
        malformed = copy.deepcopy(rows)
        malformed[0]["us"]["mask"] = "not a timing integer"
        with self.assertRaises(ValueError):
            summarize(malformed)


if __name__ == "__main__":
    unittest.main()
