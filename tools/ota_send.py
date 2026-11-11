#!/usr/bin/env python3
"""Send an OM Lite firmware image over USB CDC (no BOOT/RESET needed).

Usage: tools/ota_send.py [firmware.bin] [port]
Protocol: "OMOTA1" + size u32 LE + md5(16) + image bytes.
"""
import hashlib
import sys
import time

import serial

bin_path = sys.argv[1] if len(sys.argv) > 1 else ".pio/build/om_lite_l2/firmware.bin"
port = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyACM0"

with open(bin_path, "rb") as f:
    image = f.read()

print(f"{bin_path}: {len(image)} bytes, md5 {hashlib.md5(image).hexdigest()}")

dev = serial.Serial(port, 115200, timeout=0.5)
dev.dtr = True                       # the CDC only delivers host->device bytes with DTR (and RTS) asserted;
dev.rts = True                       # without them the device answers nothing and the upload stalls
time.sleep(1.5)                      # the app waits for the host to open the port

# The device reads in six-byte steps, so a partial write can leave 1-5 bytes behind and misalign the
# stream. Sending single pad bytes lets its reader pass through an empty FIFO, which is the only way to
# resynchronise from the host side. Then the header goes out and the handshake is retried, because the
# device may also be mid-log (a tap, a heartbeat) and swallow the first attempt.
header = b"OMOTA1" + len(image).to_bytes(4, "little") + hashlib.md5(image).digest()
ready = False
for attempt in range(1, 4):
    dev.reset_input_buffer()
    for pad in range(attempt):           # 1, 2, 3 bytes: the residue cycles and hits zero
        dev.write(b"\x00")
        dev.flush()
        time.sleep(0.15)
    dev.write(header)
    dev.flush()
    started = time.time() + 3
    while time.time() < started:
        line = dev.readline()
        if not line:
            continue
        text = line.decode(errors="replace").rstrip()
        print(text)
        if "upload started" in text:
            break
    else:
        print(f"handshake attempt {attempt} was not picked up, retrying")
        continue
    break
else:
    print("device never acknowledged the header after 3 attempts")
    dev.close()
    sys.exit(1)

# Wait for the device to finish erasing the OTA slot before streaming.
dev.timeout = 0.2
deadline = time.time() + 60
while time.time() < deadline:
    line = dev.readline()
    if not line:
        continue
    text = line.decode(errors="replace").rstrip()
    print(text)
    if "ready" in text:
        ready = True
        break
if not ready:
    print("device never said ready")
    dev.close()
    sys.exit(1)

dev.timeout = 5.0                 # flash writes can take a moment
sent = 0
for i in range(0, len(image), 1024):
    chunk = image[i:i + 1024]
    dev.write(chunk)
    # One ACK byte per chunk, but the device may interleave log lines (a tap, a heartbeat, a received
    # message). Skip anything that is not the ACK instead of aborting on the first log byte.
    ack = b""
    deadline = time.time() + 8
    while time.time() < deadline:
        b = dev.read(1)
        if not b:
            continue
        if b == b"\x06":
            ack = b
            break
    if ack != b"\x06":
        print(f"no ack at {sent}")
        dev.close()
        sys.exit(1)
    sent += len(chunk)
    if sent % (64 * 1024) == 0:
        print(f"  sent {sent}/{len(image)}", flush=True)

print("waiting for the device to finish...")
deadline = time.time() + 45
while time.time() < deadline:
    line = dev.readline()
    if not line:
        continue
    text = line.decode(errors="replace").rstrip()
    print(text)
    if "rebooting" in text or "FAILED" in text or "failed" in text:
        break
dev.close()
print("done")
