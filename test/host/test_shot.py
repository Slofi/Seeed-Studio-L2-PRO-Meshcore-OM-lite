#!/usr/bin/env python3
"""Host tests for firmware/tools/shot.py: the PNG writer and the OMSHOT header reader.

The PNG is decoded here with zlib and struct only, independently of the writer, and every chunk CRC
is checked, so a writer that produces a file Pillow would reject still fails this test.
"""
import os
import struct
import sys
import unittest
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import shot  # noqa: E402


# ---------------------------------------------------------------------------- a small PNG reader

def decode_png(data):
    """Decode the writer's output. Returns (width, height, rgb bytes). Raises on any structural fault."""
    signature = b"\x89PNG\r\n\x1a\n"
    if not data.startswith(signature):
        raise ValueError("not a PNG signature")
    pos = len(signature)
    width = height = depth = color = None
    idat = b""
    saw_end = False
    while pos < len(data):
        if pos + 8 > len(data):
            raise ValueError("truncated chunk header")
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        crc_at = pos + 8 + length
        if len(body) != length or crc_at + 4 > len(data):
            raise ValueError(f"truncated chunk {tag!r}")
        stored_crc = struct.unpack(">I", data[crc_at:crc_at + 4])[0]
        if stored_crc != zlib.crc32(tag + body) & 0xFFFFFFFF:
            raise ValueError(f"bad CRC on chunk {tag!r}")
        pos = crc_at + 4
        if tag == b"IHDR":
            width, height, depth, color, comp, filt, interlace = struct.unpack(">IIBBBBB", body)
            if (depth, color, comp, filt, interlace) != (8, 2, 0, 0, 0):
                raise ValueError("unexpected IHDR fields")
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            saw_end = True
    if not saw_end:
        raise ValueError("no IEND")
    raw = zlib.decompress(idat)
    stride = width * 3
    if len(raw) != (stride + 1) * height:
        raise ValueError("unexpected decompressed size")

    out = bytearray(stride * height)
    prev = bytearray(stride)
    for y in range(height):
        base = y * (stride + 1)
        ftype = raw[base]
        line = bytearray(raw[base + 1:base + 1 + stride])
        for i in range(stride):
            a = line[i - 3] if i >= 3 else 0
            b = prev[i]
            c = prev[i - 3] if i >= 3 else 0
            if ftype == 0:
                pass
            elif ftype == 1:
                line[i] = (line[i] + a) & 0xFF
            elif ftype == 2:
                line[i] = (line[i] + b) & 0xFF
            elif ftype == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
            else:
                raise ValueError(f"bad filter type {ftype}")
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, bytes(out)


# ---------------------------------------------------------------------------- tests

class PngWriterTest(unittest.TestCase):
    def test_known_rgb565_buffer_round_trips(self):
        # 4x2: black, white, red, green / blue, OM gold, mid grey, near-black blue.
        # The expected RGB triples are written out by hand (5/6/5 bits widened by replication),
        # not computed from the writer's own formula.
        values = [0x0000, 0xFFFF, 0xF800, 0x07E0,
                  0x001F, 0xED89, 0x8410, 0x0801]
        expected = [
            (0, 0, 0),
            (255, 255, 255),
            (255, 0, 0),
            (0, 255, 0),
            (0, 0, 255),
            (239, 178, 74),     # #e8b04f, the OM gold, through RGB565
            (132, 130, 132),    # mid grey
            (8, 0, 8),          # r=1, g=0, b=1: the smallest non-zero channel values
        ]
        pixels = b"".join(struct.pack("<H", v) for v in values)

        png = shot.png_bytes(4, 2, shot.rgb565_to_rgb888(pixels))
        width, height, rgb = decode_png(png)

        self.assertEqual((width, height), (4, 2))
        for i, want in enumerate(expected):
            got = tuple(rgb[i * 3:i * 3 + 3])
            self.assertEqual(got, want, f"pixel {i} of the 4x2 frame")

    def test_sizes_are_checked(self):
        with self.assertRaises(ValueError):
            shot.png_bytes(3, 3, b"\x00" * 26)          # one byte short
        with self.assertRaises(ValueError):
            shot.rgb565_to_rgb888(b"\x00\x00\x00")     # odd byte count
        with self.assertRaises(ValueError):
            shot.png_bytes(0, 4, b"")

    def test_lone_pixel_image(self):
        png = shot.png_bytes(1, 1, shot.rgb565_to_rgb888(struct.pack("<H", 0xF800)))
        self.assertEqual(decode_png(png), (1, 1, b"\xff\x00\x00"))


class HeaderTest(unittest.TestCase):
    def test_header_after_junk_log_lines(self):
        lines = [
            b"[om] expander ready out=FF/7F cfg=FF/FF\r\n",
            b"",
            b"[hb] uptime=42s heap=180000 board=ok mesh=up tab=0 settings=0 vol=60 snd=1 dnd=0\r\n",
            b"OMSHOT1 320 240 153600\r\n",
        ]
        seen = []
        status = shot.wait_status(lambda: lines.pop(0) if lines else b"", 5.0, log=seen.append)
        self.assertEqual(status, ("frame", 320, 240, 153600))
        self.assertEqual(len(seen), 2)                  # both log lines echoed, the status not
        self.assertIn("uptime=42s", seen[1])
        self.assertEqual(lines, [])                     # stopped at the answer, nothing read past it

    def test_no_frame_answer(self):
        lines = [b"OMSHOT0 no frame\r\n"]
        self.assertEqual(shot.wait_status(lambda: lines.pop(0), 5.0, log=lambda _: None),
                         ("none", 0, 0, 0))

    def test_garbled_header_is_not_mistaken_for_an_answer(self):
        # Byte count disagrees with the size, then a non-numeric header, then the real one.
        lines = [b"OMSHOT1 320 240 999\r\n", b"OMSHOT1 320 x 153600\r\n", b"OMSHOT1 320 240 153600\n"]
        status = shot.wait_status(lambda: lines.pop(0) if lines else b"", 5.0, log=lambda _: None)
        self.assertEqual(status, ("frame", 320, 240, 153600))

    def test_silence_times_out(self):
        # A clock that jumps past the deadline instead of sleeping.
        class Clock:
            def __init__(self):
                self.now = 0.0

            def __call__(self):
                self.now += 2.0
                return self.now

        self.assertIsNone(shot.wait_status(lambda: b"", 5.0, clock=Clock(), log=lambda _: None))

    def test_parse_status_rejects_plain_text(self):
        for line in [b"OMSHOT\r\n", b"OMSHOT1\r\n", b"OMSHOT1 0 240 0\r\n", b"hello\r\n"]:
            self.assertIsNone(shot.parse_status(line), line)


class FrameReadTest(unittest.TestCase):
    def test_short_frame_is_reported(self):
        class Dev:
            def __init__(self, data):
                self.data = data

            def read(self, n):
                piece, self.data = self.data[:n], self.data[n:]
                return piece

        dev = Dev(b"\x01\x02\x03")
        self.assertEqual(shot.read_exactly(dev, 6), b"\x01\x02\x03")
        self.assertEqual(shot.read_exactly(Dev(b"\x01\x02\x03"), 3), b"\x01\x02\x03")


if __name__ == "__main__":
    unittest.main(verbosity=2)
