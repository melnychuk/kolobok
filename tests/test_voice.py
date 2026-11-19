import struct
import unittest
from bridge.voice import FrameParser, MAGIC


def frame(kind, payload=b""):
    return MAGIC + kind.encode() + struct.pack("<H", len(payload)) + payload


class VoiceProtocolTests(unittest.TestCase):
    def test_binary_pcm_with_newlines_and_magic_is_not_split(self):
        payload = b"\n\x00S3V1\n\x00"
        parser = FrameParser()
        wire = frame("B") + frame("A", payload) + frame("E")
        result = []
        for byte in wire:
            result.extend(parser.feed(bytes([byte])))
        self.assertEqual(result, [("B", b""), ("A", payload), ("E", b"")])

    def test_garbage_and_invalid_lengths_resynchronize(self):
        parser = FrameParser()
        invalid = MAGIC + b"A" + struct.pack("<H", 65000)
        self.assertEqual(parser.feed(b"boot log\n" + invalid + frame("B")), [("B", b"")])

    def test_odd_pcm_and_invalid_control_frames_rejected(self):
        parser = FrameParser()
        self.assertEqual(parser.feed(frame("A", b"x") + frame("B", b"x") + frame("E")), [("E", b"")])

    def test_parser_has_bounded_garbage_buffer(self):
        parser = FrameParser()
        parser.feed(b"x" * 100000)
        self.assertLessEqual(len(parser.buffer), 3)


if __name__ == "__main__":
    unittest.main()
