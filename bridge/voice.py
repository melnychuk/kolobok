"""Bounded USB PCM framing and CPU ASR worker. No network or audio files."""
import json
import os
from pathlib import Path
import struct
import queue
import subprocess
import time
import uuid

# Omarchy Agents panel plugin that hosts the dictation composer (shell IPC).
AGENTS_PLUGIN = os.environ.get("STICK_S3_AGENTS_PLUGIN", "")

MAGIC = b"S3V1"
MAX_FRAME = 4096
STATE_PATH = Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")) / "stick-s3-voice.json"


class FrameParser:
    """Frame: magic (4), kind (1), LE payload length (2), payload.

    B=begin, A=mono PCM16/16kHz, E=end, X=abort. B/E/X have no payload.
    T carries bounded legacy text lines (button and diagnostics).
    """
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while len(self.buffer) >= 7:
            index = self.buffer.find(MAGIC)
            if index < 0:
                del self.buffer[:-3]
                break
            if index:
                del self.buffer[:index]
            if len(self.buffer) < 7:
                break
            kind = chr(self.buffer[4])
            size = struct.unpack_from("<H", self.buffer, 5)[0]
            if size > MAX_FRAME or kind not in "BAEXT" or (kind in "BEX" and size):
                del self.buffer[:4]
                continue
            if len(self.buffer) < 7 + size:
                break
            payload = bytes(self.buffer[7:7 + size])
            del self.buffer[:7 + size]
            if kind == "A" and size % 2:
                continue
            frames.append((kind, payload))
        return frames


def publish(session, text, recording, error=""):
    state = {"session": session, "text": text, "recording": recording, "error": error}
    tmp = STATE_PATH.with_suffix(".tmp")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as file:
        json.dump(state, file)
    os.replace(tmp, STATE_PATH)


def make_recognizer(model_dir):
    import sherpa_onnx
    for name in ["encoder.int8.onnx", "decoder.int8.onnx", "joiner.int8.onnx", "tokens.txt"]:
        if not (model_dir / name).is_file():
            raise RuntimeError(f"Missing local ASR file {name}; no cloud fallback")
    return sherpa_onnx.OnlineRecognizer.from_transducer(
        encoder=str(model_dir / "encoder.int8.onnx"), decoder=str(model_dir / "decoder.int8.onnx"),
        joiner=str(model_dir / "joiner.int8.onnx"), tokens=str(model_dir / "tokens.txt"),
        provider="cpu", num_threads=4, model_type="nemo_transducer", feature_dim=128,
        sample_rate=16000)


def asr_worker(events, model_dir):
    import numpy as np
    recognizer = None
    stream = None
    session = ""
    transcript = ""
    sample_count = 0
    last_publish = 0
    while True:
        try:
            kind, data = events.get(timeout=5)
        except queue.Empty:
            if stream is not None:
                publish(session, transcript, False, "Microphone stream stopped; review partial text")
                stream = None
            continue
        if kind == "quit":
            return
        if kind == "B":
            session = uuid.uuid4().hex
            transcript = ""
            stream = None
            sample_count = 0
            publish(session, "", True)
            try:
                if not AGENTS_PLUGIN:
                    raise RuntimeError("Set STICK_S3_AGENTS_PLUGIN to the Agents panel plugin id; recording ignored")
                result = subprocess.run(["omarchy-shell", AGENTS_PLUGIN, "voiceStart", session],
                                        capture_output=True, text=True, timeout=3)
                if result.returncode or result.stdout.strip() != "ok":
                    raise RuntimeError("Agents composer has a draft or is unavailable; recording ignored")
                if recognizer is None:
                    recognizer = make_recognizer(model_dir)
                stream = recognizer.create_stream()
            except Exception as error:
                publish(session, "", False, str(error))
        elif kind == "A" and stream is not None and recognizer is not None:
            samples = np.frombuffer(data, dtype="<i2").astype(np.float32) / 32768
            sample_count += len(samples)
            if sample_count > 16000 * 60:
                publish(session, transcript, False, "Recording exceeds 60 seconds")
                stream = None
                continue
            stream.accept_waveform(16000, samples)
            while recognizer.is_ready(stream):
                recognizer.decode_stream(stream)
            transcript = recognizer.get_result(stream)
            now = time.monotonic()
            if now - last_publish >= 0.15:
                publish(session, transcript, True)
                last_publish = now
        elif kind == "E" and stream is not None and recognizer is not None:
            stream.accept_waveform(16000, np.zeros(16000, dtype=np.float32))
            stream.input_finished()
            while recognizer.is_ready(stream):
                recognizer.decode_stream(stream)
            transcript = recognizer.get_result(stream)
            publish(session, transcript, False, "" if transcript.strip() else "No speech detected")
            stream = None
        elif kind == "X":
            publish(session, transcript, False, "Recording interrupted; review partial text")
            stream = None
