#!/usr/bin/env python3
"""Exercise the patched QEMU MMIO and stock-IDF probe; never open hardware.

STICKS3_QEMU=/path/qemu-system-xtensa
STICKS3_QEMU_ROM_DIR=/path/qemu/pc-bios  (optional)
STICKS3_PROBE_FLASH=/path/regular-file.bin (optional guest-driver test)
STICKS3_QEMU_ARTIFACTS=/path/screenshots (optional PPM copies)
"""
import json
import os
import re
import select
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

SPI = 0x60025000
GPIO = 0x60004000
USB = 0x60038000
USR = 1 << 24
DONE = 1 << 12
QEMU = os.environ.get("STICKS3_QEMU")


class Harness:
    def __init__(self, flash=None):
        if not QEMU:
            raise RuntimeError("set STICKS3_QEMU to the patched executable")
        flash_path = None
        if flash:
            flash_path = Path(flash).resolve(strict=True)
            if not flash_path.is_file() or flash_path.stat().st_size != 8 * 1024 * 1024:
                raise ValueError("probe flash must be a regular 8MiB file, not a device")
        self.tmp = tempfile.TemporaryDirectory(prefix="stick-qemu-")
        self.root = Path(self.tmp.name)
        self.out = (self.root / "stdout.log").open("wb")
        self.err = (self.root / "stderr.log").open("wb")
        args = [QEMU, "-machine", "esp32s3,stick-lcd=on", "-display", "none",
                "-monitor", "none", "-serial", "chardev:uart", "-S", "-nic", "none",
                "-d", "guest_errors,unimp",
                "-chardev", f"file,id=uart,path={self.root}/uart.log",
                "-qmp", f"unix:{self.root}/qmp,server=on,wait=off",
                "-chardev", f"socket,id=cdc,path={self.root}/cdc,server=on,wait=off",
                "-global", "driver=misc.esp32c3.usb_serial_jtag,property=chardev,value=cdc"]
        if not flash:
            args.remove("-S")  # qtest parks CPUs itself; keep chardev I/O running
            args += ["-accel", "qtest", "-qtest", f"unix:{self.root}/qtest,server=on,wait=off"]
        if os.environ.get("STICKS3_QEMU_ROM_DIR"):
            args += ["-L", os.environ["STICKS3_QEMU_ROM_DIR"]]
        if flash_path:
            escaped_path = str(flash_path).replace(",", ",,")
            args += ["-drive", f"file={escaped_path},if=mtd,format=raw,snapshot=on"]
        try:
            self.process = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=self.out, stderr=self.err)
        except Exception:
            self.out.close()
            self.err.close()
            self.tmp.cleanup()
            raise
        self.sockets = []
        try:
            if not flash:
                self.qt = self.connect("qtest").makefile("rwb")
            self.qmp = self.connect("qmp").makefile("rwb")
            json.loads(self.qmp.readline())
            self.command("qmp_capabilities")
            self.cdc = self.connect("cdc")
            self.cdc.settimeout(3)
        except Exception:
            self.close()
            raise

    def connect(self, name):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            sock = socket.socket(socket.AF_UNIX)
            sock.settimeout(5)
            try:
                sock.connect(str(self.root / name))
                self.sockets.append(sock)
                return sock
            except (FileNotFoundError, ConnectionRefusedError):
                sock.close()
                if self.process.poll() is not None:
                    raise RuntimeError((self.root / "stderr.log").read_text()) from None
                time.sleep(0.01)
        raise TimeoutError(f"QEMU socket {name} did not appear")

    def command(self, execute, arguments=None):
        request = {"execute": execute}
        if arguments is not None:
            request["arguments"] = arguments
        self.qmp.write(json.dumps(request).encode() + b"\n")
        self.qmp.flush()
        while True:
            reply = json.loads(self.qmp.readline())
            if "error" in reply:
                raise RuntimeError(reply)
            if "return" in reply:
                return reply["return"]

    def query(self, command):
        self.qt.write(command.encode() + b"\n")
        self.qt.flush()
        while True:
            reply = self.qt.readline().decode().strip()
            if not reply:
                raise RuntimeError("QEMU qtest closed")
            if reply.startswith("IRQ "):
                continue
            if not reply.startswith("OK"):
                raise RuntimeError(reply)
            return reply[2:].strip()

    def write(self, address, value):
        self.query(f"writel {address:#x} {value:#x}")

    def read(self, address):
        return int(self.query(f"readl {address:#x}"), 0)

    def step(self, ns=1_000_000):
        self.query(f"clock_step {ns}")

    def pin(self, number, high):
        offset = (0x08 if high else 0x0c) if number < 32 else (0x14 if high else 0x18)
        self.write(GPIO + offset, 1 << (number % 32))

    def transfer(self, data, dc=True):
        if not 1 <= len(data) <= 64:
            raise ValueError("PIO transfer must be 1..64 bytes")
        self.pin(45, dc)
        self.write(SPI + 0x10, 1 << 27)
        self.write(SPI + 0x1c, len(data) * 8 - 1)
        for offset in range(0, len(data), 4):
            self.write(SPI + 0x98 + offset, int.from_bytes(data[offset:offset + 4], "little"))
        self.write(SPI, USR)
        self.step()
        if self.read(SPI) & USR or not self.read(SPI + 0x3c) & DONE:
            raise AssertionError("SPI3 transfer did not complete")

    def panel(self, command, data=b""):
        self.transfer(bytes([command]), dc=False)
        if data:
            self.transfer(data)

    def init_panel(self, landscape=False):
        self.pin(21, True)
        self.pin(41, True)
        self.write(SPI + 0x0c, (1 << 12) | 1)  # 80MHz / 2 = assumed 40MHz
        self.panel(0x11)
        self.panel(0x3a, b"\x55")
        self.panel(0x36, b"\x20" if landscape else b"\x00")
        self.panel(0x29)
        self.window(40 if landscape else 52, 52 if landscape else 40,
                    240 if landscape else 135, 135 if landscape else 240)

    def window(self, x, y, w, h):
        self.panel(0x2a, x.to_bytes(2, "big") + (x + w - 1).to_bytes(2, "big"))
        self.panel(0x2b, y.to_bytes(2, "big") + (y + h - 1).to_bytes(2, "big"))
        self.panel(0x2c)

    def image(self, label="screen"):
        path = self.root / "screen.ppm"
        self.command("screendump", {"filename": str(path), "device": "stick-lcd"})
        contents = path.read_bytes()
        magic, dimensions, maximum, pixels = contents.split(b"\n", 3)
        assert magic == b"P6" and maximum == b"255"
        w, h = map(int, dimensions.split())
        # This pinned QEMU ppm_save writes Pixman's padded RGB24 stride.
        # Strip only the documented per-row padding, never framebuffer pixels.
        stride = (w * 3 + 3) & ~3
        if len(pixels) == stride * h and stride != w * 3:
            pixels = b"".join(pixels[y * stride:y * stride + w * 3] for y in range(h))
        assert len(pixels) == w * h * 3
        if os.environ.get("STICKS3_QEMU_ARTIFACTS"):
            dest = Path(os.environ["STICKS3_QEMU_ARTIFACTS"])
            dest.mkdir(parents=True, exist_ok=True)
            (dest / f"{label}.ppm").write_bytes(f"P6\n{w} {h}\n255\n".encode() + pixels)
        return w, h, pixels

    def wait_uart(self, marker, timeout=30):
        text = ""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = (self.root / "uart.log").read_text(errors="replace")
            if marker in text:
                return text
            if self.process.poll() is not None:
                break
            time.sleep(0.01)
        raise AssertionError(f"missing UART marker {marker}\n{text}\n" +
                             (self.root / "stderr.log").read_text(errors="replace")[-8000:])

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        for name in ("qt", "qmp"):
            stream = getattr(self, name, None)
            if stream:
                stream.close()
        for sock in self.sockets:
            sock.close()
        self.out.close()
        self.err.close()
        self.tmp.cleanup()


@unittest.skipUnless(QEMU, "set STICKS3_QEMU to the patched executable")
class Peripherals(unittest.TestCase):
    def setUp(self):
        self.h = Harness()
        self.addCleanup(self.h.close)

    def test_spi_update_and_exact_divider_deadline(self):
        h = self.h
        h.write(SPI, 1 << 23)
        self.assertEqual(h.read(SPI), 0)
        h.write(SPI + 0x0c, (1 << 12) | 1)
        h.write(SPI + 0x10, 1 << 27)
        h.write(SPI + 0x1c, 7)
        h.write(SPI + 0x34, DONE)
        h.write(SPI, USR)
        self.assertEqual(h.read(SPI) & USR, USR)
        h.step(199)
        self.assertEqual(h.read(SPI) & USR, USR)
        h.step(1)
        self.assertEqual(h.read(SPI) & USR, 0)
        self.assertEqual(h.read(SPI + 0x40), DONE)
        h.write(SPI + 0x38, DONE)
        self.assertEqual(h.read(SPI + 0x40), 0)

    def test_irq_matrix_latched_reroute_and_shared_level(self):
        h = self.h
        matrix = 0x600c2000

        def pending():
            registers = h.command("human-monitor-command", {"command-line": "info registers"})
            match = re.search(r"INTERRUPT=([0-9a-fA-F]+)", registers)
            if match is None:
                raise AssertionError("CPU interrupt register missing from QMP response")
            return int(match[1], 16)

        # SPI3 source 22 is asserted BEFORE its CPU interrupt is routed.
        h.write(SPI + 0x34, DONE)
        h.write(SPI + 0x44, DONE)
        h.write(matrix + 22 * 4, 5)
        self.assertTrue(pending() & (1 << 5))
        # USB source 96 shares CPU interrupt 5; clearing SPI must not lower it.
        h.write(matrix + 96 * 4, 5)
        h.write(USB + 0x10, 1 << 3)
        h.write(SPI + 0x38, DONE)
        self.assertTrue(pending() & (1 << 5))
        h.write(USB + 0x14, 1 << 3)
        self.assertFalse(pending() & (1 << 5))
        h.write(SPI + 0x44, DONE)
        h.write(matrix + 22 * 4, 3)
        self.assertFalse(pending() & (1 << 5))
        self.assertTrue(pending() & (1 << 3))

    def test_full_frame_and_landscape_ramwr(self):
        h = self.h
        palette = [(b"\xf8\x00", b"\xff\x00\x00"),
                   (b"\x07\xe0", b"\x00\xff\x00"),
                   (b"\x00\x1f", b"\x00\x00\xff")]
        for landscape in (False, True):
            h.init_panel(landscape)
            w, height = (240, 135) if landscape else (135, 240)
            wire = b"".join(palette[(x // 15 + y // 15) % 3][0]
                            for y in range(height) for x in range(w))
            expected = b"".join(palette[(x // 15 + y // 15) % 3][1]
                                for y in range(height) for x in range(w))
            for i in range(0, len(wire), 64):
                h.transfer(wire[i:i + 64])
            self.assertEqual(h.image("landscape" if landscape else "portrait"), (w, height, expected))

    def test_manual_cs_and_split_pixel(self):
        h = self.h
        h.init_panel()
        h.window(52, 40, 1, 1)
        h.write(SPI + 0x20, 1)  # disable hardware CS0
        h.transfer(b"\xf8\x00")  # manual CS41 remains high: ignored
        self.assertEqual(h.image()[2][:3], b"\x00\x00\x00")
        h.pin(41, False)
        h.transfer(b"\xf8")
        h.pin(41, True)
        h.pin(41, False)
        h.transfer(b"\x00")
        self.assertEqual(h.image()[2][:3], b"\xff\x00\x00")

    def test_sleep_display_off_and_hardware_reset(self):
        h = self.h
        h.init_panel()
        h.transfer(b"\xf8\x00")
        self.assertEqual(h.image()[2][:3], b"\xff\x00\x00")
        for off, on in ((0x28, 0x29), (0x10, 0x11)):
            h.panel(off)
            self.assertEqual(h.image()[2][:3], b"\x00\x00\x00")
            h.panel(on)
            self.assertEqual(h.image()[2][:3], b"\xff\x00\x00")
        h.pin(21, False)
        h.pin(21, True)
        self.assertEqual(h.image()[2], bytes(135 * 240 * 3))

    def test_dma_fails_visibly_instead_of_false_success(self):
        h = self.h
        h.write(SPI + 0x10, 1 << 27)
        h.write(SPI + 0x1c, 7)
        h.write(SPI + 0x30, 1 << 28)
        h.write(SPI, USR)
        self.assertEqual(h.read(SPI) & USR, 0)
        self.assertEqual(h.read(SPI + 0x3c) & DONE, 0)
        self.assertEqual(h.read(SPI + 0x3c) & 2, 2)

    def test_usb_full_fifo_short_packet_irq_and_binary_integrity(self):
        h = self.h
        h.step()
        h.write(USB + 0x14, 0xffffffff)
        h.write(USB + 0x10, 1 << 3)
        payload = bytes(range(64))
        for b in payload:
            h.write(USB, b)
        self.assertEqual(h.read(USB + 4) & 2, 0)
        h.step()
        self.assertEqual(h.cdc.recv(64), payload)
        self.assertEqual(h.read(USB + 4) & 2, 2)
        self.assertEqual(h.read(USB + 0x0c), 1 << 3)
        h.write(USB + 0x14, 1 << 3)
        self.assertEqual(h.read(USB + 0x0c), 0)
        for b in b"S3V1A\x00\x0a":
            h.write(USB, b)
        h.write(USB + 4, 1)
        h.step()
        self.assertEqual(h.cdc.recv(64), b"S3V1A\x00\x0a")

    def test_usb_rx_backpressure_without_byte_loss(self):
        h = self.h
        payload = bytes(i % 256 for i in range(320))
        h.cdc.sendall(payload)
        received = bytearray()
        deadline = time.monotonic() + 5
        while len(received) < len(payload) and time.monotonic() < deadline:
            if h.read(USB + 4) & 4:
                received.append(h.read(USB))
            else:
                time.sleep(0.001)
        self.assertEqual(received, payload)

    def test_usb_disconnect_freezes_sof_and_reconnects(self):
        h = self.h
        h.step()
        first = h.read(USB + 0x24)
        self.assertNotEqual(first, 0)
        h.cdc.close()
        time.sleep(0.03)
        h.step()
        frozen = h.read(USB + 0x24)
        h.step(5_000_000)
        self.assertEqual(h.read(USB + 0x24), frozen)
        h.cdc = h.connect("cdc")
        h.step()
        self.assertNotEqual(h.read(USB + 0x24), frozen)


@unittest.skipUnless(QEMU and os.environ.get("STICKS3_PROBE_FLASH"), "set QEMU and stock-IDF probe flash")
class GuestDriver(unittest.TestCase):
    def test_stock_idf_spi_lcd_and_usb_drivers(self):
        h = Harness(os.environ["STICKS3_PROBE_FLASH"])
        self.addCleanup(h.close)
        h.command("cont")
        h.wait_uart("PROBE:USB_READY")
        # The S3 ROM emits boot text on CDC too, before the SDK console choice.
        # Drain startup text to a quiet packet interval before requesting a frame.
        boot_deadline = time.monotonic() + 5
        while select.select([h.cdc], [], [], 0.01)[0]:
            if time.monotonic() > boot_deadline or not h.cdc.recv(65536):
                self.fail("CDC did not settle after ROM startup")
        h.cdc.sendall(b"C|2|1|3\n")
        expected = b"S3V1A\x00\x0a" + bytes((i * 37 + 11) % 256 for i in range(2560))
        received = bytearray()
        deadline = time.monotonic() + 30
        while len(received) < len(expected) and time.monotonic() < deadline:
            if select.select([h.cdc], [], [], 0.1)[0]:
                chunk = h.cdc.recv(len(expected) - len(received))
                if not chunk:
                    break
                received.extend(chunk)
        self.assertEqual(received, expected)
        uart = h.wait_uart("PROBE:PASS")
        self.assertIn("PROBE:LCD_READY", uart)
        # Guest uses the stock panel driver in landscape with board offsets.
        w, height, image = h.image("stock-idf")
        self.assertEqual((w, height), (240, 135))
        palette = (b"\xff\x00\x00", b"\x00\xff\x00", b"\x00\x00\xff")
        expected_image = b"".join(palette[(x // 15 + y // 15) % 3]
                                  for y in range(135) for x in range(240))
        self.assertEqual(image, expected_image)


if __name__ == "__main__":
    unittest.main(verbosity=2)
