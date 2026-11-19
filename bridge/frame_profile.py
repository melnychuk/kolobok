"""Opt-in timing-only diagnostics. Never retain raw serial/audio/task payloads."""
import hashlib
import json
import os
import queue
import struct
import threading
import zlib
from pathlib import Path

SIGNATURE = b"S3PF"
METRICS = ("interval", "update", "background", "mask", "face", "eyes", "mouth",
           "attention", "badges", "flush", "total")
STATES = ("offline", "idle", "sleeping", "working", "waiting", "finished", "listening")
BODY = struct.Struct("<4sBBBB8I11I")
PAYLOAD_SIZE = BODY.size + 4
SCHEMA = "stick-s3-frame-profile/v1"
CAPTURE_SCHEMA = "stick-s3-profile-capture/v1"


def build_fingerprint(source_sha256, optimization="Os"):
    """Keep legacy Os IDs; distinguish an O2 recipe with identical sources."""
    if optimization not in {"Os", "O2"}:
        raise ValueError("Unsupported optimization recipe")
    if optimization == "Os":
        return source_sha256
    return hashlib.sha256((source_sha256 + "\0compiler.optimization_flags=-O2").encode()).hexdigest()


def decode_profile(kind, payload):
    if kind != "T" or len(payload) != PAYLOAD_SIZE or payload[:4] != SIGNATURE:
        return None
    if zlib.crc32(payload[:-4]) != struct.unpack_from("<I", payload, BODY.size)[0]:
        return None
    signature, version, state, flags, reserved, *words = BODY.unpack(payload[:-4])
    if signature != SIGNATURE or version != 1 or state >= len(STATES) or flags & ~15 or reserved:
        return None
    build, sequence, uptime, dropped, skipped, observer, cpu, period = words[:8]
    values = words[8:]
    if not 0 < cpu <= 1000 or not 0 < period <= 1_000_000:
        return None
    if (sum(values[1:10]) & 0xffffffff) != values[10] or values[10] < values[9]:
        return None
    return {"schema": SCHEMA, "build_id": f"{build:08x}", "sequence": sequence,
            "uptime_ms": uptime, "firmware_dropped": dropped, "voice_skipped": skipped,
            "previous_observer_us": observer, "cpu_mhz": cpu, "period_us": period,
            "state": STATES[state], "layout_flags": flags,
            "us": dict(zip(METRICS, values, strict=True))}


class ProfileSink:
    """Bounded asynchronous opt-in sink; serial consumer never waits for disk."""
    def __init__(self, path: Path, limit=1800, queue_size=256):
        if not 1 <= limit <= 10000 or not 1 <= queue_size <= 256:
            raise ValueError("Invalid profile capture bounds")
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
        os.fchmod(fd, 0o600)
        self.limit = limit
        self.accepted = self.dropped = self.invalid = 0
        self.error = None
        self.pending = queue.Queue(maxsize=queue_size)
        self.done = threading.Event()
        self.thread = threading.Thread(target=self._write, args=(fd,), daemon=True)
        self.thread.start()

    def accept(self, kind, data):
        if kind != "T" or not data.startswith(SIGNATURE):
            return False
        if self.done.is_set() or self.error is not None:
            return True
        record = decode_profile(kind, data)
        if record is None:
            self.invalid += 1
            return True
        record["host_dropped"] = self.dropped
        record["host_invalid"] = self.invalid
        try:
            self.pending.put_nowait(record)
        except queue.Full:
            self.dropped += 1
            return True
        self.accepted += 1
        if self.accepted >= self.limit:
            self.done.set()
        return True

    def _write(self, fd):
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as stream:
                while not self.done.is_set() or not self.pending.empty():
                    try:
                        record = self.pending.get(timeout=.05)
                    except queue.Empty:
                        continue
                    stream.write(json.dumps(record, separators=(",", ":")) + "\n")
                    stream.flush()
                footer = {"schema": CAPTURE_SCHEMA, "accepted": self.accepted, "limit": self.limit,
                          "host_dropped": self.dropped, "host_invalid": self.invalid,
                          "complete": True, "limit_reached": self.accepted >= self.limit}
                stream.write(json.dumps(footer, separators=(",", ":")) + "\n")
                stream.flush()
        except OSError as error:
            self.error = error.strerror or "Profile write failed"
            self.done.set()

    def close(self):
        self.done.set()
        self.thread.join(timeout=2)
        if self.thread.is_alive():
            self.error = "Profile writer did not drain before close"
