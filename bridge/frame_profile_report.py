"""Summarize timing-only traces; prepare calibration candidates, never patch QEMU."""
import argparse
import json
import math
import os
import re
from pathlib import Path

from .frame_profile import CAPTURE_SCHEMA, METRICS, SCHEMA, STATES, build_fingerprint

NUMBERS = ("sequence", "uptime_ms", "firmware_dropped", "voice_skipped", "previous_observer_us",
           "cpu_mhz", "period_us", "layout_flags", "host_dropped", "host_invalid")


def validate_record(record):
    if not isinstance(record, dict) or record.get("schema") != SCHEMA:
        raise ValueError("Not a timing-only record")
    if record.get("state") not in STATES or not re.fullmatch(r"[0-9a-f]{8}", str(record.get("build_id", ""))):
        raise ValueError("Invalid timing identity")
    values = record.get("us")
    if not isinstance(values, dict) or set(values) != set(METRICS):
        raise ValueError("Invalid timing metrics")
    for value in [*(record.get(key) for key in NUMBERS), *values.values()]:
        if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= 0xffffffff:
            raise ValueError("Invalid timing integer")
    if not 0 < record["cpu_mhz"] <= 1000 or not 0 < record["period_us"] <= 1_000_000 or record["layout_flags"] & ~15:
        raise ValueError("Invalid timing configuration")
    if (sum(values[key] for key in METRICS[1:10]) & 0xffffffff) != values["total"] or values["total"] < values["flush"]:
        raise ValueError("Inconsistent timing sums")
    return record


def distribution(values):
    values = sorted(values)
    if not values:
        return {"count": 0}
    return {"count": len(values), "min": values[0], "mean": round(sum(values) / len(values), 2),
            "p50": values[math.ceil(len(values) * .5) - 1],
            "p95": values[math.ceil(len(values) * .95) - 1],
            "p99": values[math.ceil(len(values) * .99) - 1], "max": values[-1]}


def summarize(records, manifest=None, source="unknown", capture=None):
    if source not in {"unknown", "physical", "qemu", "synthetic"}:
        raise ValueError("Invalid source declaration")
    if not 1 <= len(records) <= 10000:
        raise ValueError("Expected 1..10000 timing records")
    records = [validate_record(record) for record in records]
    builds = sorted({record["build_id"] for record in records})
    verified_manifest = False
    if manifest is not None:
        if not isinstance(manifest, dict):
            raise ValueError("Invalid build manifest")
        if (manifest.get("schema") != "stick-s3-profile-build/v1" or
                manifest.get("profiling_enabled") is not True or builds != [manifest.get("build_id")] or
                not re.fullmatch(r"[0-9a-f]{64}", str(manifest.get("source_sha256", ""))) or
                build_fingerprint(manifest["source_sha256"], manifest.get("optimization", "Os"))[:8] != builds[0] or
                not re.fullmatch(r"[0-9a-f]{64}", str(manifest.get("firmware_bin_sha256", "")))):
            raise ValueError("Trace does not match the profile build manifest")
        expected_recipe = build_fingerprint(manifest["source_sha256"], manifest.get("optimization", "Os"))
        if manifest.get("build_sha256", expected_recipe) != expected_recipe:
            raise ValueError("Invalid build recipe fingerprint")
        if manifest.get("period_us") is not None and any(row["period_us"] != manifest["period_us"] for row in records):
            raise ValueError("Trace frame period does not match manifest")
        verified_manifest = True
    stages = {key: distribution([row["us"][key] for row in records if key != "interval" or row["us"][key]])
              for key in METRICS}
    cpu = distribution([row["us"]["total"] - row["us"]["flush"] for row in records])
    observer = distribution([row["previous_observer_us"] for row in records])
    gaps = resets = inconsistent = 0
    outside = []
    for before, after in zip(records[:-1], records[1:], strict=True):
        advance = (after["sequence"] - before["sequence"]) & 0xffffffff
        if before["build_id"] != after["build_id"] or not 1 <= advance <= 1_000_000:
            resets += 1
            continue
        gaps += advance - 1
        if advance == 1:
            gap = after["us"]["interval"] - before["us"]["total"]
            if gap < 0:
                inconsistent += 1
            else:
                outside.append(gap)
    counters = {key: max(row[key] for row in records) for key in
                ("firmware_dropped", "voice_skipped", "host_dropped", "host_invalid")}
    sealed = False
    if capture is not None:
        if not isinstance(capture, dict) or capture.get("schema") != CAPTURE_SCHEMA:
            raise ValueError("Invalid capture footer")
        for key in ("accepted", "limit", "host_dropped", "host_invalid"):
            value = capture.get(key)
            if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= 0xffffffff:
                raise ValueError("Invalid capture counter")
        if (capture["accepted"] != len(records) or not 1 <= capture["limit"] <= 10000 or
                capture["accepted"] > capture["limit"] or not isinstance(capture.get("complete"), bool) or
                not isinstance(capture.get("limit_reached"), bool) or
                capture["limit_reached"] != (capture["accepted"] == capture["limit"])):
            raise ValueError("Inconsistent capture footer")
        sealed = capture["complete"]
        counters["host_dropped"] = max(counters["host_dropped"], capture["host_dropped"])
        counters["host_invalid"] = max(counters["host_invalid"], capture["host_invalid"])
    # Startup firmware drops before the first received sample are not losses in this window.
    lost = max(0, counters["firmware_dropped"] - records[0]["firmware_dropped"])
    skipped = max(0, counters["voice_skipped"] - records[0]["voice_skipped"])
    warnings = []
    if source != "physical":
        warnings.append("Source is not operator-declared physical hardware; CPU timings are not hardware calibration.")
    if not verified_manifest:
        warnings.append("No matching build manifest: artifact/source provenance is incomplete.")
    if not sealed:
        warnings.append("Capture lacks a completed footer; tail losses or early termination cannot be excluded.")
    if len({row["cpu_mhz"] for row in records}) > 1 or len({row["period_us"] for row in records}) > 1:
        warnings.append("CPU clock or frame target changed; split captures before fitting calibration costs.")
    if len(records) < 300:
        warnings.append("Fewer than 300 frames; insufficient tail coverage for calibration.")
    if gaps or lost or skipped or counters["host_dropped"] or counters["host_invalid"] or resets or inconsistent:
        warnings.append("Trace has loss, voice exclusion, resets or inconsistent gaps; percentiles may be biased.")
    if observer["max"] > 1000:
        warnings.append("Observer exceeded the 1ms probe budget; timing collection may perturb frame cadence.")
    groups = {}
    for row in records:
        key = f'{row["state"]}/flags-{row["layout_flags"]}'
        groups.setdefault(key, []).append(row)
    grouped = {key: {"frames": len(rows),
                     "cpu_us": distribution([row["us"]["total"] - row["us"]["flush"] for row in rows]),
                     "flush_us": distribution([row["us"]["flush"] for row in rows]),
                     "total_us": distribution([row["us"]["total"] for row in rows])}
               for key, rows in groups.items()}
    result = {"schema": "stick-s3-profile-report/v1", "source_declaration": source,
              "provenance_note": "Source is operator-declared, not authenticated by CRC or the telemetry stream.",
              "build_ids": builds, "manifest_matched": verified_manifest, "capture_complete": sealed, "frames": len(records),
              "periods_us": sorted({row["period_us"] for row in records}),
              "cpu_mhz": sorted({row["cpu_mhz"] for row in records}),
              "stage_us": stages, "cpu_render_us": cpu, "observer_us": observer,
              "outside_render_us": distribution(outside), "sequence_gaps": gaps, "resets": resets,
              "counters": counters,
              "busy_deadline_misses": sum(row["us"]["total"] > row["period_us"] for row in records),
              "late_starts_over_2ms": sum(row["us"]["interval"] > row["period_us"] + 2000 for row in records),
              "groups": grouped, "warnings": warnings, "calibration_ready": not warnings}
    if verified_manifest and manifest is not None:
        hz = manifest.get("source_assumed_spi_hz", 40000000)
        byte_count = manifest.get("pixel_bytes", 64800)
        if not isinstance(hz, int) or not isinstance(byte_count, int) or hz <= 0 or byte_count <= 0:
            raise ValueError("Invalid source-derived display configuration")
        wire = byte_count * 8 * 1_000_000 / hz
        result["transport_candidates"] = {
            "source_assumed_spi_hz": hz, "pixel_wire_lower_bound_us": wire,
            "measured_render_cpu_p95_us": cpu["p95"],
            "display_call_non_wire_p95_us": max(0, stages["flush"]["p95"] - wire),
            "effective_pre_wire_p95_us": max(0, stages["total"]["p95"] - wire),
            "usable": result["calibration_ready"],
            "note": "Candidates for Transport.frame(cpu_us), not cycle costs. Flush overhead may include conversion, DMA gaps and waits. Do not change USB poll-us or claim panel scanout timing from this trace."}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--source", choices=("unknown", "physical", "qemu", "synthetic"), default="unknown")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    records = []
    capture = None
    seen = 0
    try:
        if not args.input.is_file():
            raise ValueError("Timing input must be a regular file")
        with args.input.open() as stream:
            while True:
                line = stream.readline(8193)
                if not line:
                    break
                seen += 1
                if len(line) > 8192 or seen > 10001:
                    raise ValueError("Timing input exceeds capture bounds")
                if capture is not None:
                    raise ValueError("Data after capture footer")
                value = json.loads(line)
                if isinstance(value, dict) and value.get("schema") == CAPTURE_SCHEMA:
                    capture = value
                else:
                    records.append(value)
        if args.manifest and (not args.manifest.is_file() or args.manifest.stat().st_size > 1_000_000):
            raise ValueError("Invalid manifest file")
        manifest = json.loads(args.manifest.read_text()) if args.manifest else None
        result = summarize(records, manifest, args.source, capture)
    except (OSError, UnicodeError, ValueError) as error:
        raise SystemExit("Invalid or unreadable timing metadata") from error
    fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "w") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print(f'Timing report: {args.output}; {result["frames"]} frames; calibration_ready={result["calibration_ready"]}')


if __name__ == "__main__":
    main()
