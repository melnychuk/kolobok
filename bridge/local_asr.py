#!/usr/bin/env python3
"""Local-only English ASR. Model loading uses local files and CPU only."""
import argparse
from pathlib import Path
import time
import wave

DEFAULT_MODEL = Path.home() / ".local/share/stick-s3/asr/nemotron-en-int8"


def transcribe(path, model_dir=DEFAULT_MODEL, threads=4):
    import numpy as np
    import sherpa_onnx
    files = {name: str(model_dir / name) for name in
             ["encoder.int8.onnx", "decoder.int8.onnx", "joiner.int8.onnx", "tokens.txt"]}
    for name, file in files.items():
        if not Path(file).is_file():
            raise RuntimeError(f"Missing local model file: {name}; no network fallback")
    with wave.open(str(path), "rb") as audio:
        if audio.getnchannels() != 1 or audio.getsampwidth() != 2 or audio.getframerate() != 16000:
            raise ValueError("Audio must be mono 16-bit PCM WAV at 16000 Hz")
        if audio.getnframes() > 16000 * 60:
            raise ValueError("Maximum recording is 60 seconds")
        samples = np.frombuffer(audio.readframes(audio.getnframes()), dtype="<i2").astype(np.float32) / 32768
    recognizer = sherpa_onnx.OnlineRecognizer.from_transducer(
        encoder=files["encoder.int8.onnx"], decoder=files["decoder.int8.onnx"],
        joiner=files["joiner.int8.onnx"], tokens=files["tokens.txt"],
        model_type="nemo_transducer", provider="cpu", num_threads=threads,
        sample_rate=16000, feature_dim=128)
    stream = recognizer.create_stream()
    stream.accept_waveform(16000, samples)
    stream.accept_waveform(16000, np.zeros(16000, dtype=np.float32))
    stream.input_finished()
    while recognizer.is_ready(stream):
        recognizer.decode_stream(stream)
    return recognizer.get_result(stream)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav", type=Path)
    parser.add_argument("--model-dir", type=Path, default=DEFAULT_MODEL)
    parser.add_argument("--threads", type=int, default=4)
    args = parser.parse_args()
    print(transcribe(args.wav, args.model_dir, args.threads))
