# OM Lite rebuild — progress log (newest at the bottom)

## 2026-11-10 (Claude)
- Brief written (`docs/REBUILD.md`). Firmware put in git, pushed to github.com/Slofi/Seeed-Studio-L2-PRO-Meshcore-OM-lite. New boot sound "TK-TK" on the device.
- DS sandbox can build this tree (`pio run` 91 s, SUCCESS); toolchain + `.git` read-only for DS.

## 2026-11-10 (DS, EST-327, work order item 1)
- **Screenshot over USB, device side.** `src/ota.cpp` matches a second six-byte command, `OMSHOT`, in the
  same byte-at-a-time reader that handles `OMOTA1`. The OTA matcher is the old state machine unchanged, fed
  every byte as before; the shot matcher is a second independent copy of it, so a leftover partial magic
  still recovers and an upload is recognised exactly as it was. On `OMSHOT` the device sends
  `OMSHOT1 <width> <height> <bytes>` and then the off-screen frame as native RGB565 little-endian, row by
  row from the top left; with no frame buffer it answers `OMSHOT0 no frame`. The frame is streamed in
  256-byte writes (the size of the CDC TX ring, so a write blocks only while that ring drains into USB),
  with no per-chunk host ACK, so only the transfer itself holds up the loop.
- **Real colours.** `src/board.cpp` / `board.h` gained `omFrameSize()` and `omFramePixel()`. The pixel
  accessor undoes the panel's byte order with the same boot-probe flag (`s_frameNeedsSwap`) the push uses,
  so the host always receives the colour as drawn, whatever order this sprite happens to store.
- **Host side.** `tools/shot.py <out.png> [port]`: pyserial + the standard library only, the PNG is built
  with `zlib` + `struct` (no Pillow). It opens the port the way `tools/ota_send.py` does (DTR set before
  writing, then never touched again), never writes RTS and never asks for a reset, and the handshake skips
  log lines and retries with the OTA tool's pad-byte resync.
- **Build** (`pio run -e om_lite_l2`, the command from REBUILD.md): exit code 0, SUCCESS in 12.7 s (incremental).
  RAM 58,988 B of 327,680 (18.0%), flash 782,677 B of 6,553,600 (11.9%). The previous run's figures were not
  recorded, so this is an absolute size, not a delta.
- **Tests**: new `test/host/run.sh` (9 tests, all pass, exit 0). PNG writer: a known 4x2 RGB565 buffer
  (black, white, red, green, blue, OM gold `#e8b04f`, mid grey, the smallest non-zero channel values) is
  written and decoded back by a zlib decoder inside the test that also verifies every chunk CRC, plus size
  checks and a 1x1 image. Header reader: junk log lines and empty reads before a valid header, a garbled
  header (wrong byte count, non-numeric field) rejected instead of half-read, `OMSHOT0`, a timeout, and a
  short frame. Out of tree, a full 320x240 payload (153,600 bytes, gradient) makes a 114 KB PNG that decodes
  pixel-exact in 0.04 s. The C++ byte scanner is not covered by a host test; it is the same code as before
  plus the second matcher.
- **Could not check** (no device in the sandbox): everything on the real L2. Claude to flash over OTA, run
  `python3 tools/shot.py /tmp/l2.png /dev/ttyACM0`, and compare with the preview. Also worth one OTA upload
  afterwards to confirm that path still works after the reader change. Two things to watch on the device:
  the frame buffer holds the last frame drawn through the off-screen path, so a view that still draws
  straight to the panel will show the previous screen in the PNG; and if colours come out wrong, the boot log
  line `frame probe ... push swap ...` says which byte order was detected.
- **Next step**: work order item 2, theme, fonts, layout and widgets.
