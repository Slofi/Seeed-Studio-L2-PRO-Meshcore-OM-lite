#!/usr/bin/env python3
"""Grab the L2 Pro's screen over USB and write it as a PNG, so the screen can be checked without a photo.

Usage: tools/shot.py <out.png> [port]

Protocol (device side: src/ota.cpp, the same reader that handles OTA):
    host -> device  "OMSHOT" (6 bytes)
    device -> host  "OMSHOT1 <width> <height> <bytes>\\n" then <bytes> of RGB565 little-endian,
                    row by row from the top left;
                    or "OMSHOT0 no frame\\n" when the app has no off-screen frame buffer.

The port is opened the way tools/ota_send.py opens it, because that sequence is proven on this
board: DTR is asserted before anything is written (this CDC delivers host -> device bytes only with
DTR set), then nothing else is driven. RTS is never written and DTR is never dropped, and the tool
never asks for a reset: a wrong DTR/RTS sequence puts this board in the ROM loader instead of
running the app (pocket-mesh.md, "The USB flash cycle, exactly").

No Pillow: the PNG is written with zlib and struct only.
"""
import struct
import sys
import time
import zlib

FRAME = "OMSHOT1"
NO_FRAME = "OMSHOT0"
MAX_BYTES = 4 * 1024 * 1024     # sanity limit on a header from the wire


def rgb565_to_rgb888(pixels):
    """Little-endian RGB565 bytes -> 8-bit RGB bytes, 3 per pixel. 5/6/5 widened by bit replication."""
    if len(pixels) % 2:
        raise ValueError("odd number of bytes for RGB565")
    out = bytearray((len(pixels) // 2) * 3)
    j = 0
    for i in range(0, len(pixels), 2):
        value = pixels[i] | (pixels[i + 1] << 8)
        r = (value >> 11) & 0x1F
        g = (value >> 5) & 0x3F
        b = value & 0x1F
        out[j] = (r << 3) | (r >> 2)
        out[j + 1] = (g << 2) | (g >> 4)
        out[j + 2] = (b << 3) | (b >> 2)
        j += 3
    return bytes(out)


def png_bytes(width, height, rgb):
    """A truecolour 8-bit PNG (colour type 2) with no filtering, from width*height*3 RGB bytes."""
    if width <= 0 or height <= 0:
        raise ValueError("bad image size")
    stride = width * 3
    if len(rgb) != stride * height:
        raise ValueError("RGB buffer does not match the size")

    raw = bytearray()
    for y in range(height):
        raw.append(0)                      # filter type 0 (None) for every row
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
            chunk(b"IEND", b""))


def parse_status(line):
    """Classify one line from the device.

    Returns ("frame", width, height, nbytes), ("none", 0, 0, 0), or None when the line is not a
    status line at all (a log line, a heartbeat, an empty read). The byte count must agree with the
    size, so a garbled header is rejected rather than half-read.
    """
    text = line.decode("ascii", "replace").strip()
    if text.startswith(NO_FRAME):
        return ("none", 0, 0, 0)
    if text.startswith(FRAME + " "):
        parts = text.split()
        if len(parts) != 4:
            return None
        try:
            width, height, nbytes = (int(p) for p in parts[1:])
        except ValueError:
            return None
        if width > 0 and height > 0 and nbytes == width * height * 2 and nbytes <= MAX_BYTES:
            return ("frame", width, height, nbytes)
    return None


def wait_status(readline, seconds, clock=time.monotonic, log=print):
    """Read lines until a status line arrives; print and tolerate anything else.

    Log lines arrive before the answer (a tap, a heartbeat, a received message), exactly as with the
    OTA handshake, so they are skipped instead of treated as failure. Returns the parse_status()
    tuple, or None when the deadline passes.
    """
    end = clock() + seconds
    while clock() < end:
        line = readline()
        if not line:
            continue                    # read timeout: the deadline check ends the loop
        status = parse_status(line)
        if status:
            return status
        text = line.decode("ascii", "replace").rstrip()
        if text:
            log(text)
    return None


def read_exactly(dev, count):
    """Read count bytes, allowing the port timeout between pieces. b"" means the device stopped."""
    data = bytearray()
    while len(data) < count:
        piece = dev.read(count - len(data))
        if not piece:
            break
        data += piece
    return bytes(data)


def main(argv):
    if len(argv) not in (2, 3):
        print("usage: tools/shot.py <out.png> [port]", file=sys.stderr)
        return 2
    out_path = argv[1]
    port = argv[2] if len(argv) == 3 else "/dev/ttyACM0"

    import serial  # imported here so the pure functions above stay testable without a port

    dev = serial.Serial(port, 115200, timeout=0.5)
    dev.dtr = True                       # host -> device bytes need DTR; see the module note
    time.sleep(1.5)                      # the app waits for the host to open the port

    # Same resync as the OTA tool: the device reads one byte at a time, so a write can split the
    # magic. Pad bytes pass through an empty FIFO and cycle the residue until the header lands.
    status = None
    for attempt in range(1, 4):
        dev.reset_input_buffer()
        for _ in range(attempt):
            dev.write(b"\x00")
            dev.flush()
            time.sleep(0.15)
        dev.write(b"OMSHOT")
        dev.flush()
        status = wait_status(dev.readline, 3.0)
        if status:
            break
        print(f"handshake attempt {attempt} was not picked up, retrying")
    if not status:
        print("device never answered OMSHOT after 3 attempts", file=sys.stderr)
        dev.close()
        return 1

    if status[0] == "none":
        print("device has no frame buffer (OMSHOT0 no frame)", file=sys.stderr)
        dev.close()
        return 1

    _, width, height, nbytes = status
    print(f"frame {width}x{height}, {nbytes} bytes of RGB565")
    dev.timeout = 5.0
    payload = read_exactly(dev, nbytes)
    dev.close()                          # closed as opened: nothing is toggled, no reset requested
    if len(payload) != nbytes:
        print(f"short frame: got {len(payload)} of {nbytes} bytes", file=sys.stderr)
        return 1

    with open(out_path, "wb") as f:
        f.write(png_bytes(width, height, rgb565_to_rgb888(payload)))
    print(f"wrote {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
