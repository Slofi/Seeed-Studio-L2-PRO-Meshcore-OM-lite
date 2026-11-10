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

## 2026-11-10 (DS, EST-328, work order item 2, the foundation)
- **Theme, `src/ui/theme.h`.** Every mock colour is a named native-RGB565 constant (warm surfaces
  `#15110f/#130e0b/#211a16/#2c231d/#1d1611/#40322a/#251d17`, ink/dim/dimmer, OM gold `#e8b04f`,
  client blue `#4aa3ff`, repeater green `#39d98a`, room amber `#ffb020`, badge red, plus the chat,
  map, RPTR and warning colours the later screens need). Every L2 size is there too: screen 320x240,
  status 26, tab bar 25, row 30, paddings/gaps, radii, dot/battery/signal-bar sizes. No screen holds
  a colour or a size of its own. The mock's L2 CSS still says `.status{height:18px}`; the ticket and
  the brief say 26, so 26 is what the new layer uses.
- **Smooth fonts.** `tools/mkvlw.py` renders a TrueType face into LovyanGFX's 8-bit-coverage VLW
  format as a PROGMEM array. VLW goes through `draw_alpha_bitmap_common()`, so glyph edges are
  blended, not thresholded: this is the anti-aliased path (the old `font_om12.cpp`/`font_om18.cpp`
  are 1-bit GFXfonts from `mkfont.py`, which thresholds at 128 and therefore cannot be smooth).
  Faces: DejaVu Sans (Bitstream Vera licence) and DejaVu Sans Mono (same licence); the generated
  files carry the licence note. Four sizes, ASCII 0x20-0x7E, compiled into flash:
  sans 11 px 7,565 B, sans 14 px 10,522 B, sans 9 px 6,244 B, mono 10 px 6,515 B, **total 30,846 B
  (30.1 KiB)**. `ui/fonts.cpp` loads the four blobs once from `setup()` and maps FontRole to a face.
  The status line uses 10 px mono, not the brief's 11 px: at 320 px, 11 px makes
  `MC . EU868 . 2B` collide with `7 nodes` (seen in a 1:1 render of the real blobs before flashing).
- **Layout, `src/ui/layout.h`.** Pure C++ (no Arduino/LovyanGFX), so `test/host` compiles it with
  g++. `Layout` splits the screen into status/content/tab bar; `tab()` hands out four adjacent
  rectangles with no overlap; `row()/rowVisible()` give the 30 px row bands under scroll;
  `tabAt()/rowAt()` use exactly those rectangles, half-open (top edge in, bottom edge next row).
  `l2()` is 320x240; `layoutFor(w,h)` is ready for the Tanmatsu arrangement.
- **Widgets, `src/ui/widgets.cpp`.** Status bar: network dot, `MC . EU868 . 2B`, real node count,
  GPS sats or `--`, battery icon + percentage (3.2-4.2 V estimate; unknown shows `--`), clock,
  vector gear. Tab bar: four vector icons + labels (Nodes, Chat, Map, RPTR), selected tab gold with
  the 2 px top line, unread badge. List row: type dot (client blue / repeater green / room amber),
  sans name, mono path note + age, optional signal bars (only when a real RSSI exists), row line.
  Empty state. All draw into the surface passed in; the Nodes screen wraps them in
  `omFrameBegin()`/`omFrameEnd()` so the USB screenshot sees them.
- **Nodes screen, `src/ui/screen_nodes.cpp`.** Real MeshCore contacts only: `mesh_link.h` gained a
  read-only copy API (`meshContactList`, newest first, `meshContactSignature`, `meshNow`) over
  `BaseChatMesh`'s contact table. No sample nodes: with no contacts it shows `No nodes yet /
  Waiting for adverts. Heard nodes appear here.` MeshCore's contact record has no RSSI, so the rows
  show the real path length and age in mono instead of inventing signal bars. Tapping a row selects
  it; one-second signature check refreshes the list; finger drag scrolls it.
- **Shell wiring (`main.cpp`).** `drawHome()` is now the new Nodes screen. The new status + tab bar
  are drawn on Nodes; the other three tabs still use the old screens and their old bar, so their
  top-bar buttons stay usable. Every old path back to tab 0 already calls `drawHome()`, so returning
  to Nodes always redraws the new chrome. The status gear opens the existing Settings screen. Old
  chat/map/RPTR behaviour, the off-screen frame and finger scrolling are untouched; `ota.cpp` is
  unchanged. Deliberate information change: the old home's SD/radio diagnostic block is not on the
  new Nodes screen (the mock has no place for it); those values belong in Settings in item 4.
- **Build** (`pio run -e om_lite_l2`, the command from REBUILD.md): exit 0, SUCCESS, 16.96 s
  (incremental after the final font-restore edit; the clean build before it was 98.95 s).
  RAM 66,556 B of 327,680 (20.3%), flash 826,569 B of 6,553,600 (12.6%). Previous run:
  58,988 B / 782,677 B, so this ticket added **+7,568 B RAM and +43,892 B flash** (30,846 B of the
  flash is the four font blobs; the rest is the UI layer and its strings).
- **Tests**: `test/host/run.sh` exit 0. The existing 9 Python tests still pass, and the new
  `test/host/test_layout.cpp` (g++, `-Wall -Wextra -Werror`) passes: exact status/content/tab split,
  four tabs that tile the bar with no overlap, tab centres and edges, row pitch and row rectangles
  under scroll, clipping at the content edges, top-edge-inside/bottom-edge-next hit tests, gear rect,
  and the same arithmetic on an 800x480 layout.
- **Could not check** (no device): whether the four VLW fonts load and blend as expected on the
  panel; a screenshot of the new Nodes screen; the return path Nodes -> Chat/Map/RPTR -> Nodes; the
  empty state when the contact table has no adverts yet; the unread badge against real traffic.
  The clock intentionally shows `--:--`: this board has no RTC and the firmware does not parse GPS
  time yet, so any real-looking time would have been invented. Battery percentage is an estimate
  from the ADC millivolts, not a fuel gauge.
- **Next step**: Claude flashes and takes a screenshot. Then work order item 3, Chat on the new
  layer.

## 2026-11-10 07:02 (Claude) — EST-328 checked on the device
- Flashed over USB OTA, screenshot `docs/shots/2026-11-10-0702-nodes-first.png`: smooth fonts, status bar, tab bar with icons, gold selected tab, honest empty state. Committed.
- Against the mock: status separators print "." on the baseline (mock: middle dot); battery icon touches "97%"; the gear reads as a crosshair; still the warm palette (switch to the grey tokens in REBUILD.md); contacts are gone after a reboot (0 nodes), so they are not persisted.
