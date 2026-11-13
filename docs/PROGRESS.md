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

## 2026-11-10 (DS, EST-329, grey palette, status-bar fixes, contact persistence)
- **Grey palette.** Every surface in `src/ui/theme.h` is now a REBUILD.md token: bg `#131210`,
  screen `#100f0d` (the mock's `.screen`, also used for a full-screen detail), panel `#1c1b19`,
  panel2 `#242321`, bars `#171614`, line `#333230`, row line `#1e1d1b`. The colours the later screens
  already use follow the mock as well: code/draft field `#131210`, map canvas `#191816`
  (`.mapwrap`), map side rail `#11100e` (`.map-side`). Gold accent, ink/dim/dimmer and the node-type
  colours are the mock's values and did not change (the reply bar and tag stay blue: they mark a
  quote, not a selection). No warm value is left anywhere in `src/ui`. The old screens in `main.cpp`
  still hold their own two colours; they are not on the new layer and were not touched.
- **Middle dot.** `tools/mkvlw.py` gained `--extra` (comma-separated code points, decimal or 0x hex;
  records stay sorted because LovyanGFX looks glyphs up by binary search). mono10 was regenerated
  with U+00B7: 96 glyphs, 6,567 B, **+52 B**. The status line now formats `MC · EU868 · 2B` as UTF-8
  (`\xC2\xB7`); the dot is 6x4 px at x-height and has the same 6 px advance as the period, so the
  string is the same width and still fits beside the node count at 320 px. sans9 was regenerated
  without an extra glyph and came out byte-identical, so the tab labels cannot move.
- **Battery gap.** New constant `kBattGap = 8` (the mock's `.status .bits` gap) between the percent
  text and the icon; it was 2 px. The status layout arithmetic was re-checked on the real rects:
  battery right edge to "97%" left edge = 8 px, "97%" right edge to the clock left edge = 6 px
  (kStatusGap, unchanged), clock right edge to the gear left edge = 6 px.
- **Settings icon.** `kGearSize` 11 -> 9 (the mock draws its ⚙ at the status font size, 8.5 px) and
  `drawGear()` is now a filled cog: a 7 px body, eight 2x2 teeth and a 1 px hub punched back to the
  bar colour. I rendered both candidates at 1:1 and 26x before choosing: DejaVu's U+2699 at 9 px
  breaks up into a ring with faint spikes and reads like the old crosshair, the silhouette reads as
  a cog. Vector also keeps the sans9 font metrics untouched, so no tab label shifts by a pixel.
- **Contacts survive a reboot.** New `src/contact_store.h` (plain C++, no Arduino) plus the wiring in
  `mesh_link.cpp`. After `BaseChatMesh::begin()` and the identity store, `/contacts` in SPIFFS is
  read; if there is no file the node starts empty (`[mc] contacts: none stored`). Format: 12-byte
  header ("OMC1", u16 version, u16 count, CRC32 of the payload) then fixed 152-byte records with
  pub key, name, type, flags, out path + length, advert timestamp, lastmod, lat/lon, sync_since.
  Any mismatch (size, magic, version, count > 32, a field a save can never write, CRC) rejects the
  whole file and the node starts empty with a log line; a header-only 0-record file is valid.
  Restore uses MeshCore's public `addContact()` (no MeshCore code is copied; the field names follow
  `ContactInfo.h`, MIT), then `bootstrapRTCfromContacts()`, MeshCore's own hook for contacts brought
  back from storage, so lastmod values from the previous boot are not "in the future".
  Saving: `onDiscoveredContact`, `onContactPathUpdated` and `onContactOverwrite` mark the table dirty;
  `meshLinkLoop()` then writes at most once per 60 s (`ContactSaveScheduler`). The first change after
  boot is written immediately, every later one waits for the interval (successful or failed), and a
  failed write stays dirty and retries after the interval rather than in a tight loop. Writes go to
  `/contacts.tmp`; the old file is removed and the temp renamed over it. A power cut mid-write leaves
  the old file (before the swap) or no file (between remove and rename), and the next boot then starts
  empty with a log line, which is the documented fallback.
  RAM: two buffers (4,892 B file image + 4,880 B record array) are static and shared by load and
  save; they would not fit the loop task's stack, and the first build had one copy of each inside
  each function, which wasted 9.8 KB.
- **Build** (`pio run -e om_lite_l2`, the command from REBUILD.md): exit 0, SUCCESS in 20.04 s
  (incremental). RAM 76,300 B of 327,680 (23.3%), flash 829,549 B of 6,553,600 (12.7%). Previous
  run 66,556 B / 826,569 B, so this ticket added **+9,744 B RAM** (9,772 B of contact buffers minus
  alignment) and **+2,980 B flash** (52 B of that is the extra mono glyph; the rest is the contact
  store, the cog and the theme). A first build failed to link because `mkvlw.py` was called with a
  lowercase `--name`, producing `ksans9Data` while `ui/fonts.h` declares `kSans9Data`; regenerated
  with `--name Sans9/Mono10`, and the usage note now says the name becomes the identifier.
- **Tests**: `test/host/run.sh` exit 0. The 9 Python screenshot tests still pass and `test_layout.cpp`
  is unchanged. New `test/host/test_contact_store.cpp` (g++, `-Wall -Wextra -Werror`) proves:
  0/1/32-record round trips with both signed lat/lon and all 0xFF/0x00 byte values; the header bytes
  on disk are "OMC1", version 1, count 32; encode refuses >32 records, a negative count, a 1-byte-short
  buffer and a null pointer; decode refuses every truncation (17-byte steps through the whole file),
  one byte of trailing junk, bad magic, a future version, a count over the table, fewer output slots
  than records, one flipped payload bit (CRC), a non-zero reserved byte, type NONE, an empty name, a
  32-byte name with no terminator, and out_path_len 65 (0xFF is allowed as MeshCore's "unknown").
  Save policy: not dirty never saves; the first dirty change is due at once; after a successful save
  nothing is due until exactly 60,000 ms; a failed save stays dirty and retries after the interval,
  not immediately; repeated `markDirty` calls do not postpone a pending save; the interval works
  across the 32-bit `millis()` wrap; a zero interval saves on every change.
- **Could not check** (no device in the sandbox): the grey palette, the middle dot, the cog and the
  8 px battery gap as they actually look on the panel; that SPIFFS `rename`/`remove` behave as
  expected on this partition; that a real reboot brings the Nodes list back. To check the feature on
  the device: let it hear adverts, watch the log for `[mc] contacts: saved N (x bytes)`, reboot, and
  look for `[mc] contacts: loaded N of M from flash` before the Nodes list is drawn. A corrupted-file
  test can be done by writing junk to `/contacts` over the serial shell if one exists; otherwise it
  stays a host-tested path. Also worth confirming that a contact saved just after boot (the
  immediate first save) is there after the reboot, and that the 60 s timer does not delay the first
  one.
- **Next step**: Claude flashes, screenshots the Nodes screen after a reboot with contacts, and
  compares the status bar and palette with the mock. Then work order item 3, Chat on the new layer.


## 2026-11-10 07:18 (Claude) — EST-329 checked on the device
- Screenshot `docs/shots/2026-11-10-0718-nodes-grey.png`: middle dots, battery gap, a gear that reads as one, grey palette. Contact save/load reviewed (temp file + rename, CRC, rate-limited); not yet proven across a reboot with real contacts (0 heard since the last reboot).
## 2026-11-10 (DS, EST-330, Chat tab on the new layer)

- **What this ticket is**: work order item 3. The old chat screens (channel list -> messages ->
  composer, with bubbles, its own 34 px keyboard and a second copy of every coordinate) are gone from
  `main.cpp`; the Chat tab is now the mock's chat: a chip row, a flat message list, a compose line and
  a keyboard sheet that covers the tab bar. The channel list and its pages (add, #tag, private key,
  mute, rename, delete, the base64 key view) are unchanged and still reachable: the three-line button
  at the right end of the chip row opens them, and so does the status gear (the old SET did the same).
- **New files** (all inside `src/ui/`):
  - `text_wrap.h/.cpp` (118 lines): greedy word wrap. The width of a byte range comes from a callback
    (`MeasureFn`), so the device measures with the real VLW face and test/host measures with 6 px per
    character. Every line is a byte range of the source, so the draw renders exactly what the wrapper
    measured. A word wider than the whole line is split between characters, nothing is truncated.
  - `chat_layout.h/.cpp` (347 lines): every rectangle of the screen, from `theme.h` numbers only. Chip
    strip (scroll area + fixed list button), message band, compose line (field / keyboard button /
    Send), the sheet (reply bar, draft line, four key rows) and the two overlays. The keyboard rows
    are split from integer weights (10 = normal, 22 = wide) using cumulative boundaries, so rounding
    never accumulates: keys come out 29/30 px wide with clean 2 px gaps and each row ends exactly on
    the 4 px pad.
  - `screen_chat.h/.cpp` (598 lines): draws into the off-screen frame (`omFrameBegin/End`) and answers
    taps with the same rectangles (`chatScreenHit`). The shell only reacts to what was hit.
- **Screen, in numbers**: chips row 26..49 (23 px, chip 16 px high, pill radius 8, selected = dark gold
  fill + 1 px gold outline, unread count in red at the right inside the chip); message band 49..188
  (139 px); compose line 188..215 (field 230x20, keyboard button 26, Send 44); tab bar 215..240. The
  sheet is 123 px tall (4 pad + 18 draft + 3 + 4x22 keys + 3x2 gaps + 4 pad) and starts at y=117, so
  it covers the whole tab bar, which is not drawn while it is open (mock `.kb-open`). With a reply bar
  it is 144 px and starts at y=96. The compose field is hidden while the sheet is open; the draft line
  rides on top of the keys, with a solid 1x9 gold caret after the text (the mock blinks it; a blink
  needs a redraw timer, so it is steady for now).
- **Messages** follow the mock: no bubbles. Time in mono dim, sender in mono (gold for others, green
  for ours), text in sans 11 px, wrapped, 14 px line pitch, 4 px between messages, newest at the
  bottom, one clip rect for the band. The time is an elapsed time ("now"/"2m"), not a clock: there is
  no RTC on this board.
- **Scroll**: the old finger formula is kept (`scroll = scroll0 - dy`, px up from the newest). The
  rubber band is kept (10 px of give, snapped back on release). A new message moves the view only when
  the reader is at the bottom (scroll 0); otherwise the scroll grows by the content growth, so the
  text being read holds still. While the sheet is open the live update is skipped, so the draft cannot
  jump under a finger.
- **Behaviour kept**: send on the selected channel; a reply goes on the wire as `@Name text` (MeshCore
  has no reply field); tap a message -> REPLY / INFO / COPY; INFO shows sender, channel, age, hops and
  SNR (hops -1 = local echo, SNR 0 = unknown); unread cleared when a chip is opened and on live
  traffic; mute still silences the chime (a muted chip's label is drawn dimmer); the channel list and
  its settings as before. One deliberate change: Send with an empty draft does nothing now (it used
  to put an empty message on the air).
- **DM chips**: the model has them (`ChatChip::dm`, label "DM: name") and the chip strip draws whatever
  the view lists, but nothing produces DM traffic yet: `mesh_link.cpp`'s `onMessageRecv` is still an
  empty override, so there is no DM store and no DM unread count. Work order item 5 adds DMs; the
  screen will not need a change then. No DM chip is invented in the meantime.
- **Old code deleted**: `drawChannelsTab`, `openChannels`, `drawComposer`, `drawChatMessages`,
  `drawBubbleMenu`, `drawBubbleInfo`, `drawKbIcon`, `kbKey`, `wrapLines`, `fmtAge`, the composer branch
  of `handleTap`, the composer case inside the name editor (it was unreachable), the channel-list tap
  branch and 19 globals/constants. `main.cpp`: **2,068 -> 1,741 lines, 327 fewer** (605 lines removed,
  278 added for the new shell: chip/message view building, one draw path, the tap switch and the send).
- **Widgets**: the three text helpers (`setRole`, `drawTextLeft/Right/Ellipsis`) moved out of
  `widgets.cpp`'s anonymous namespace into `widgets.h`, and `measureText` was added, so the chat screen
  does not grow a second copy of them (item 4 will want the same).
- **Build** (`pio run -e om_lite_l2`, the command from REBUILD.md): exit 0, SUCCESS, 18.80 s
  (incremental after the final `chat_layout` edit; the first full build of this ticket was 78.08 s).
  RAM 77,340 B of 327,680 (23.6%), flash 834,813 B of 6,553,600 (12.7%). Previous run 76,300 B /
  829,549 B, so this ticket added **+1,040 B RAM and +5,264 B flash** (no new font bytes: the four VLW
  blobs are unchanged).
- **Tests**: `test/host/run.sh` exit 0. The 9 Python screenshot tests and the layout and contact-store
  tests still pass, and the new `test/host/test_chat.cpp` (g++, `-Wall -Wextra -Werror`) passes:
  wrapping exactly at the width, the first line narrower than the rest, a long word split by
  characters, leading/repeated spaces, a realistic 80-character message (every line within its width,
  all bytes consumed), the empty draft, a cut at maxLines, and the bad-argument paths; the chip strip
  (widths with and without an unread count, strip width, overflow, max scroll, chip edges in and out of
  the gaps, scrolling a chip into view, clamping at both ends); the closed screen (chips/band/compose
  tile the content, field/kbd/Send in order with exactly the gap between them); the sheet (bottom edge
  on the screen floor, covers the tab bar, compose gone, draft and rows exactly where the numbers say,
  every key row tiling with 2 px gaps and flush edges, 10/9/9/3 and 10/10/10/2 keys, reply bar above
  the draft); the key definitions (QWERTZ letters, shift/mode/space/del/send); and the message band
  (content height, max scroll, bottom anchoring, spans, edge taps in the gaps and the pads, a full
  20-message ring clamped to its top and bottom, and the smaller band while the sheet is open).
- **Could not check** (no device in the sandbox): how any of this looks on the panel; that the VLW
  text at 11 px reads well inside 139 px of band; that the chips row scrolls to the selected chip
  under a real finger; the sheet over the tab bar; the caret; a real send and reply round trip; the
  "new message while scrolled up" rule against live traffic. To check: send from a second node while
  the list is scrolled up (the view must not jump) and while it is at the bottom (it must show the new
  message), open the sheet (tab bar disappears, keys 22 px, tap outside closes, shift/123/space/del),
  and open the channel list from the three-line button.
- **Next step**: Claude flashes, screenshots the Chat tab with and without the keyboard sheet, and
  compares the chip row, the message lines and the sheet with the mock. Then work order item 4 (map
  chrome, RPTR, Settings on the new layer, `main.cpp` down to setup + loop + dispatch).

## 2026-11-10 07:36 (Claude): EST-330 checked
- Built in the sandbox (exit 0, RAM 77,340 B, flash 834,813 B), host tests exit 0, OTA ok (835,184 B), boots to Nodes (screenshot `docs/shots/2026-11-10-0736-boot.png`). The Chat screen itself is not yet seen on the panel (needs a finger on the tab).
- Review finding, to fix in the next ticket: `g_replyTo`, `g_bubMenu` and `g_bubInfo` are indexes into the per-channel view rebuilt from the shared 32-message ring. Once the ring is full, every new message drops the oldest and shifts the indexes, so a reply typed while a message arrives can go to the wrong sender, and an open menu/INFO can switch to another message. Hold the message's sequence number (and copy the reply name at the moment REPLY is tapped) instead of an index.

## 2026-11-10 (DS, EST-331, clock + gear + chat reply fix)

- **What this ticket is**: EST-331, three parts asked for in one run. (1) Replace MeshCore's
  `VolatileRTCClock` (15 May 2024 on every boot, so other nodes dropped our adverts as a replay) with
  the system clock fed by ranked sources. (2) Replace the hand-built cog with the mock's U+2699 glyph,
  rendered as an alpha mask by a host tool, and prepare Wi-Fi/Bluetooth icons. (3) Fix Claude's EST-330
  finding: reply/menu/INFO held indexes into the ring view, which shift when a 33rd message arrives.
- **Clock, source ranking and the floor** (new `src/timekeeper.h/.cpp`, plain POSIX C++, no Arduino):
  - One clock: `settimeofday()`/`time()`. MeshCore gets `OmRTCClock` in `mesh_link.cpp`; its
    `getCurrentTime()` reads the system clock and `setCurrentTime()` writes it (plumbing only, never
    marks a trusted source). `VolatileRTCClock` is no longer used.
  - Sources, best first: GPS > Wi-Fi > phone > manual. A lower source may set the clock only when no
    higher source has set it in the last 6 hours; an equal source may refresh it. Manual always
    applies when the user does it. The 6-hour history is kept per source, so a Wi-Fi set does not
    block a phone that was waiting for a GPS set from 7 hours ago.
  - GPS: `board.cpp` keeps reading NMEA and now also parses `$..RMC` and `$..ZDA`; the parser checks
    the XOR checksum, RMC status `A`, a real calendar date and the 2025..2099 range, so the module's
    default date before its first satellite never reaches the clock. `GpsState.timeEpoch` carries the
    latest sentence. A real source below the floor is refused, so time can never go backwards.
  - Floor: `tfloor` in NVS, saved on every applied sync and at least every 10 minutes (a failed NVS
    write is retried on the next 10-minute cadence, not every loop). Boot starts the system clock at
    `max(saved floor, build time)`; `__DATE__`/`__TIME__` are local Europe/Ljubljana and are converted
    to UTC with the same POSIX TZ rule. The floor is not a real time: `realSinceBoot()` stays false.
  - Removed `bootstrapRTCfromContacts()` from the contact loader: it would set the clock to
    `latest contact lastmod + 1` (a 2024-era timestamp) and move the new clock backwards.
  - Status bar: `buildStatus()` shows `HH:MM` (24 h, `CET-1CEST,M3.5.0,M10.5.0/3`) only after a real
    source has set the clock; otherwise `--:--`. A once-a-minute repaint keeps it live. Getters for
    Settings: `timeKeeper().source()`, `sourceAgeSec()`, `timeSourceName()`.
  - Ages: chat messages deliberately keep monotonic uptime stamps, so a forward clock sync cannot
    make an age negative or leap by months. The Nodes "last heard" keeps `meshNow()` but now shows
    `--` for a future timestamp and `30d+` instead of a huge day count (old contacts can carry a
    timestamp from a previous boot/clock).
  - Sent messages now use `meshNow()` for MeshCore's sender timestamp (they used uptime seconds).
- **Gear and icons** (new `tools/mkicons.py`, `src/ui/icons.cpp/.h`): the gear is DejaVu Sans U+2699,
  rendered at 9x9 (`4x` supersampled, LANCZOS down, coverage stretched to full 0..255) and blended
  over the bar colour by `drawIcon()`; `drawGear()` now calls the mask. Wi-Fi (three arcs + dot) and
  Bluetooth (rune) are 11x11 masks ready for the later tabs. The generator writes both the C++ tables
  and `docs/shots/icons-preview.png`, a contact sheet with each icon at 1:1 and 8x.
- **Chat fix**: `mesh_link.cpp` now stores a monotonic `seq` with every message and exposes
  `meshMsgSeq(idx)`. The shell holds `g_bubMenuSeq`, `g_bubInfoSeq`, `g_replySeq` (0 = none) instead
  of indexes; `buildChatView()` resolves each id to the current view slot. `g_replyName` is copied
  when REPLY is tapped, so the `@Name` reply still goes to the right sender even if that message is
  the one the ring drops. An open menu/INFO stays on its message across a new arrival and closes only
  when its target has really fallen out of the 32-message ring. The new `src/ui/chat_model.h` carries
  the plain view type and `chatMsgIndexBySeq()` so the rule is host-testable.
- **Build** (`pio run -e om_lite_l2`, the command from REBUILD.md): exit 0, SUCCESS.
  RAM 77,572 B of 327,680 (23.7%), flash 849,885 B of 6,553,600 (13.0%). Previous run 77,340 B /
  834,813 B, so this ticket added **+232 B RAM and +15,072 B flash** (the clock module, the icon
  tables/tool, and the MeshCore adapter). No new font blobs.
- **Tests**: `test/host/run.sh` exit 0. The 9 Python screenshot tests, layout, contact-store and chat
  tests still pass. New `test/host/test_timekeeper.cpp` (g++, `-Wall -Wextra -Werror`) proves: valid
  RMC/GP RMC/ZDA; void RMC, 2024 default date, bad checksum, split line, impossible date and missing
  date all rejected; `__DATE__`/`__TIME__` converted from local CET/CEST; winter/summer clock format;
  both 2026 DST change instants (last Sunday March and October); floor never backwards and
  max(saved, build); the real-source flag; GPS > Wi-Fi > phone > manual with the 6-hour hold, equal
  refresh, manual always applying and a later GPS taking over; same-second refresh without an NVS
  write; `setHardware()` raising the clock without making it real; and the 10-minute save cadence.
  `test_chat.cpp` adds the ring-shift test: a full 32-message view plus one new message keeps the
  held menu and reply ids on the same names/texts, and the copied reply name still matches.
- **Could not check** (no device in the sandbox): that the L76K's real RMC/ZDA lines pass the checksum
  and date gate on hardware, and how long the first fix takes; the status bar actually switching from
  `--:--` to `HH:MM`; the floor surviving a reboot (needs `tfloor` in NVS and a second boot); the 9x9
  U+2699 mask on the panel (see `docs/shots/icons-preview.png` first); the Chat menu staying on its
  message under live traffic. To check: boot with no fix (clock must stay `--:--`), wait for a fix,
  watch `[time] GPS -> ...` and the status bar, reboot and confirm the clock continues rising (not
  2024) before the next fix, and open a message menu/reply while a second node sends.
- **Next step**: Claude flashes and screenshots the status bar and the gear (1:1 mask preview is in
  `docs/shots/icons-preview.png`), then checks the Chat menu under a live message. Settings (work
  order item 4) can now show "GPS, 2 min ago" through `timeKeeper().source()`/`sourceAgeSec()` and
  call `setFromSource(..., TimeSource::Manual)` for a manual set; Wi-Fi and Bluetooth only need to
  call the same entry.
## 2026-11-10 08:40 (Claude): EST-331 checked
- Built in the sandbox (exit 0, RAM 77,572 B, flash 849,885 B), host tests exit 0. Not flashed yet: it goes to the device together with the chat polish (one interruption for Filip).
- Review: clock design as asked (ranking, floor, honest `--:--`); the reply/menu fix holds sequence numbers. **Icons not accepted:** the 9x9 U+2699 reads as a ring with a dot and the Bluetooth mask as a diamond (`docs/shots/icons-preview.png`). Claude drew candidates (`docs/shots/2026-11-10-icons-claude-try-8x.png`, left to right: DS 9 px glyph, glyph 13 px, vector gear 11, **vector gear 13 px 8 teeth (chosen)**, 6 teeth, Bluetooth rune 13 and 15 px). The chosen shapes and their exact code go in the next ticket.


## 2026-11-10 (DS, EST-332, chat polish + icons)

- **What this ticket is**: Filip's first look at the Chat screen on the device
  (`docs/shots/2026-11-10-0800-chat.png`). His word overrides the mock: the channel bar goes and a
  channel/DM selector takes its place, the compose line loses the Send button, the keyboard gains the
  comma and the dot, text the font cannot draw gets an honest fallback instead of a bar that looked
  like the caret, and the rejected icons are redrawn from Claude's chosen shapes.
- **Header + selector (the L2 arrangement)**:
  - The 23 px chip row is now one header: the open chat's name (`#test`, or `DM: Name`), a vector down
    chevron right after it (it points up while the dropdown is open), and at the right edge a 5 px OM
    gold dot with the count of unread messages in the OTHER chats (`9+` above nine). The count room
    (14 px) is always reserved, so a count appearing never moves the name. Nothing waiting elsewhere =
    no dot. `chatUnreadElsewhere()` in `chat_model.h`.
  - Tap the header: the selector drops down over the message band (header bottom to the compose line,
    139 px). A vertical list: "Channels" label, one 30 px two-line row per channel (name + the last
    message's age on line 1, `sender: start of the text` on line 2), then "Direct messages" and either
    its rows or one dim line "No direct messages yet". The open chat is marked with the gold dot and
    the `kSelBg` row. The unread count of each row sits at the right of line 2 in gold. Rows scroll
    with the finger (10 px give past the ends, snapped back on release) inside a clipped list; the
    last row, "Manage channels", is pinned to the panel's floor above a 1 px line and opens the
    existing channel settings (add, join, delete, key, mute). Tap a row = open that chat and close;
    tap the header again or anywhere else = close.
  - The three-line button is gone. The gear now opens Settings (it opened the channel list before);
    the channel list is reached only through "Manage channels".
  - `chatLayoutFor()` grew a `selector` flag and the rects `head`, `headTitle`, `headDot`, `selPanel`,
    `selList`, `selManage`; `selContentHeight()`/`selMaxScroll()`/`selRowTop()`/`selRowAt()` are the
    list arithmetic (labels, rows, the empty-DM line, the pads and the gaps answer "no row").
- **Direct messages**: prepared only, as asked. `mesh_link` gained `meshDmCount()`/`meshDmName()`
  (0 / ""), with the comment that the store arrives with upstream's companion code (work order item 5,
  `onMessageRecv` is still an empty override). The selector asks the facade; nothing is invented.
  The view model `ChatEntry` (channels first, then DMs) copies the last message's sender, preview and
  age into fixed arrays, so a new packet cannot move the list under the finger.
- **Chip code removed**: `ChatChip`, `chipWidth`, `chipStripWidth`, `chipMaxScroll`, `chipLeft`,
  `chipAt`, `chipScrollTo`, `chatChipScrollInto`, the `kChip*` constants and their host tests.
  `kMaxChips` became `kMaxChats`.
- **Compose line**: the Send button is gone ("the keyboard already contains the send button"). The
  field is now 278 px (was 234) and the keyboard button sits at the right edge; tapping the field or
  the button opens the keyboard, as before.
- **Keyboard**: the letters page's bottom row is 123, comma, space, dot, Send (5 keys, 22/10/40/10/22
  units: the space is the widest of the middle three); the 123 page's bottom row is space, comma, dot,
  Send (4 keys). New constant `kKeyUnitSpace = 40`. The 22 px key height is unchanged and re-checked:
  the sheet is still 123 px tall, covers the tab bar, and every row tiles with 2 px gaps and flush
  edges.
- **Text the font cannot draw** (the fallback until the emoji ticket): the bright bar was LovyanGFX's
  missing-glyph rectangle, space wide and a whole line tall, which read as the caret. Now:
  - `text_wrap` gained plain-C++ UTF-8: `utf8SeqLen()`, `utf8Decode()` (one malformed or truncated
    byte = one replacement, always advancing by one byte), `utf8Encode()` and `measureUtf8()`, which
    sums code points and charges a missing one a space plus `kMissingExtra` (2 px).
  - `wrapText()` splits long words on code point boundaries, never inside a sequence.
  - `measureText()`, `drawTextLeft()`, `drawTextRight()` and `drawTextEllipsis()` walk UTF-8; a code
    point the face lacks is never handed to LovyanGFX. It measures as a space plus 2 px and draws as
    one small dim 1 px box (`kMissingBoxH` = 6) of that width, so the wrap and the drawing agree. The
    tail of a long draft also starts on a code point boundary now.
  - Faces still carry ASCII 0x20-0x7E (and U+00B7 in mono), so the rule is `fontHasGlyph()`.
- **Icons**: `tools/mkicons.py` now draws Claude's chosen shapes with his exact geometry, 16x
  supersampled and LANCZOS down: the VECTOR gear 13x13 with 8 teeth (polygon of four points per tooth,
  disc, hub hole) and the Bluetooth rune as one polyline, 13 px tall and 9 px wide. Wi-Fi stays 11x11.
  `src/ui/icons.cpp/.h` and `docs/shots/icons-preview.png` regenerated; the mask sizes now come from
  the images, not from a square assumption. `kGearSize` 9 -> 13, so `Layout::gear()` hands the status
  bar a 13x13 slot (layout value, no literal in the screen).
- **Build** (`pio run -e om_lite_l2`, the command from REBUILD.md): exit 0, SUCCESS, 9.90 s.
  RAM 78,644 B of 327,680 (24.0%), flash 852,737 B of 6,553,600 (13.0%). Previous run 77,572 B /
  849,885 B, so this ticket added **+1,072 B RAM and +2,852 B flash** (the ChatEntry copies, the UTF-8
  walkers and the two bigger icon masks; no new font bytes).
- **Tests**: `test/host/run.sh` exit 0. The 9 Python screenshot tests, layout, contact-store and
  timekeeper tests still pass, and `test_chat.cpp` (g++, `-Wall -Wextra -Werror`) now proves, besides
  the existing wrap/message/ring checks: UTF-8 decode of 1/2/3/4-byte sequences, stray continuation
  and truncated bytes, the encode round trip, a missing code point measuring space+2, the 4-byte emoji
  measured and wrapped correctly (every line start and end on a boundary, every line within its
  width, no empty line after a split that ends the message), one emoji narrower than the line still
  getting exactly one line; the header rect, its gold dot, the reserved title room and the title/dot
  never overlapping; the compose field taking the freed width with exactly one gap to the keyboard
  button; the dropdown covering exactly the message band, its list and pinned Manage row not
  overlapping; `selContentHeight`/`selMaxScroll` with and without DMs; row tops at scroll 0 and
  scrolled, and the hit test at every edge (labels, pads, the 1 px between rows, the Manage row, the
  empty DM line); the 5-key and 4-key bottom rows, their order, their widths, 22 px height and exact
  tiling; and the chat tab's unread-elsewhere sum. `test_layout.cpp` asserts the 13 px gear slot.
- **Could not check** (no device in the sandbox): how the dropdown looks and how it scrolls under a
  real finger; whether 3 visible rows are enough before scrolling (the DM section sits below the fold
  with three or more channels: the panel is the 139 px message band); the gold dot and its count at
  11 px; the placeholder box on the panel (dim 6 px box) against a bot's emoji reply; the 5-key bottom
  row at 22 px on the real panel; the 13 px gear in the 26 px status bar. The 1:1/8x gear and
  Bluetooth previews are in `docs/shots/icons-preview.png`.
- **Next step**: Claude flashes and screenshots, in this order: the Chat tab with the new header
  (name + chevron, gold dot with a count from a second chat), the dropdown open at the top and
  scrolled to "Direct messages"/"Manage channels", a bot reply that contains an emoji (expect one
  small dim box, no bright bar), the compose line without Send, and the keyboard's bottom row with
  the comma and the dot (letters and 123). If the dropdown feels too short, letting the panel cover
  the compose line too is a one-line change in `chatLayoutFor()`.

## 2026-11-10 08:20 (Claude): EST-331 + EST-332 on the device
- Built in the sandbox (exit 0, RAM 78,644 B, flash 852,737 B), host tests exit 0, OTA ok, boot log read over USB (opened like shot.py, no reset).
- **Clock works:** `[time] GPS -> 1794295133` = 2026-11-10 07:18:53 UTC, matching TestBox; the status bar shows 08:20 (CET). Only two `[time]` lines at boot and none in the next 20 s, so the floor is not written to NVS every second.
- **Contacts survive a reboot (EST-329 proven):** `[mc] contacts: loaded 2 of 2 from flash`, both on the Nodes list after the flash.
- **GPS:** the UART works and the module has real time, but GGA says 0 satellites used and no fix (indoors now; 15 min on a balcony with half the sky also gave no fix, old firmware). The time came from ZDA (RMC was void), i.e. from the module's own clock.
- Gear 13 px reads as a gear (`docs/shots/2026-11-10-0820-nodes-clock.png`).
- Found: both stored contacts show `30d+`: their `lastmod` was written by the old 2024 clock. Filip found: the wake button turns the screen on black until a touch, and that touch can press something.

## 2026-11-10 (DS, EST-333, wake button, GPS diagnostics, ZDA gate, stale last heard)

- **What this ticket is**: the four small things Filip and Claude found on the device at 08:20: the
  wake button comes back to a black screen until a touch (and that touch presses something), the GPS
  reports 0 satellites with no way to tell "hears nothing" from "hears plenty, too weak", ZDA has no
  validity field and can carry a module default date, and the two flash-loaded contacts show `30d+`
  because their `lastmod` was stamped by the old 2024 clock.
- **1. Wake button.** `screenPower()` did `wakeup() + brightness` and nothing drew, so the panel
  stayed black until some other code pushed a frame, and the touch that caused it was handled as a
  tap. Now:
  - `board` grew `panelPower(bool)` (panel sleep/wake only, backlight untouched), a named
    `kScreenBrightness` (160, was a literal) and `kPanelWakeOutMs = 120`. The 120 ms is the panel's
    own sleep-out time: LovyanGFX's `Panel_NV3031B` sets `CMD_SLPOUT_DELAY = 120`, and `wakeup()`
    only sends SLPOUT, it does not wait, so the app has to wait before the first frame.
  - The loop's wake order is now: panel wake, wait 120 ms, redraw the screen, then backlight on. No
    stale frame is ever lit.
  - One redraw entry point, `redrawCurrentScreen()`, covers every screen the app can be showing:
    all four tabs (Nodes, Chat with its selector / keyboard sheet / bubble menu, Map, RPTR) and
    every settings page (sound list, chime and click pickers, channel list, channel page, add
    sheet, name editor, base64 key entry, map and repeater settings). The once-a-minute clock
    repaint uses it too, so a settings page or an open keyboard now survives the minute rollover
    instead of being skipped.
  - `src/touch_gate.{h,cpp}`: new, plain C++. `onWake(now, downAtWake)` arms a 300 ms window; a
    finger already down at the wake, or a press that BEGINS inside the window, is ignored until it
    lifts (so a held finger cannot become a tap the moment the window ends); the first new press
    after the window passes counts and ends the gate. The loop feeds every sample through it and
    reads the panel only while the screen is on.
  - Sleeping now drops any gesture in flight (`g_down`/`g_drag`/`g_acted` cleared), so the finger
    that was on the glass when the screen went dark cannot tap on release. The gesture statics moved
    to the top of `loop()` for that. A comment in `touch_gate.h` says any future auto-sleep must come
    through the same sleep/wake path.
- **2. GPS diagnostics.**
  - (a) `gpsPoll()` now parses `$..GSV` as well as GGA: satellites in view per constellation
    (`GpsState::inView[]` for GPS / GLONASS / BeiDou / Galileo / other, GN and GQ land in "other"),
    the best C/N0 of the current GSV cycle (`s_gsvBestSnr[]`, reset when a constellation's message 1
    arrives) and a total (`inViewTotal`: the sum of the named constellations, or GN's own count when
    only GNGSV talks, never both). GGA's count stays the "used" one. Every line now passes an XOR
    checksum check first; a bad line is counted in `checksumBad` and never parsed (before, GGA/GSV
    were parsed unverified).
  - (b) The 2 s heartbeat gains a second line: `[hb] gps bytes=... nmea=... bad=... view=N(g l b a)
    used=N snr=N fix=0/1`, and the `[gps]` change line now says `used=/view=/snr=`.
  - (c) Status bar: with a fix the cell stays `GPS 7`; while searching it is `GPS 0/5`
    (used/in view); `GPS --` only when the module has not sent a single sentence. The row is
    measured before it is drawn (`ui/status_cells.h`): if the full cell would push the node count
    into the left group, the short form (`0/5`) is drawn instead. Widths: the 10 px mono face
    advances 6 px per glyph (checked against the VLW blob: all 96 glyphs = 6), so `GPS 0/5` is
    42 px; the normal row (2 nodes, 93 %, 08:20) leaves 8 px of slack, the tight row (12 nodes,
    100 %, `GPS 0/24`) does not fit and falls back to `0/24`.
  - (d) Boot: `gpsBegin()` sends `$PCAS04,7` (GPS + BeiDou + GLONASS) after the board's power/reset
    sequence and with the UART up, exactly in MeshCore's `configureL76K()` order (delay 250 ms,
    the sentence with its XOR checksum, delay 250 ms), and also on the swapped-pin retry. For 300 ms
    afterwards every byte the module sends is logged as `[gps] reply: ...`, so the boot log will say
    whether the L76K acknowledged (a `$PCAS...` line) or at least whether its GSV talkers changed.
    **Not verifiable in the sandbox**: the device log has to answer this one.
  - (e) GNSS power at a soft reboot: `board.cpp` sets expander bit 13 (GNSS power) high in
    `expanderBegin()` and **never brings it low anywhere**, so a soft reboot leaves the module
    powered (the PCA9554 keeps its latched outputs while the SoC restarts). What a reboot does do is
    pulse the GNSS reset line low for 40 ms at every boot, which restarts the module; its almanac
    lives in its own flash and survives that. Left as it is, with a comment saying why not to "fix"
    it by cycling the rail.
- **3. ZDA gate.** `parseNmeaTime()` now refuses a ZDA whose year is outside
  `buildYear() .. buildYear() + 1` (`kZdaYearsAhead`), on top of the checksum, the impossible-date
  and the sane-epoch checks. `buildYear()` reads `__DATE__` in `timekeeper.cpp`. RMC is unchanged
  (it carries a validity field). Host test: ZDA 2080 and 1980 refused, the build year and the year
  after it accepted, two years ahead refused.
- **4. Stale "last heard".** `ageText` moved to `ui/age.{h,cpp}` (plain C++) and takes the build
  epoch: a stored `lastmod` older than the build cannot be real (it was written by the previous
  session's 2024 clock), so the row says `--` instead of `30d+`. `main.cpp` keeps the build epoch
  from `parseBuildTimestamp(__DATE__, __TIME__)` in `g_buildEpoch` and passes it in. Nothing else
  changed: the next advert from that node overwrites `lastmod` with the now-real clock, and the row
  then shows a real age. Host test covers the 2023/2024 stamp, one second before the build, a
  future stamp, `--` with no clock, the whole `now`/`5m`/`3h`/`12d`/`30d+` ladder and the case
  where the build epoch is unknown.
- **Build** (`pio run -e om_lite_l2`, the command from REBUILD.md): exit 0, SUCCESS, 11.8 s.
  RAM 78,676 B of 327,680 (24.0%), flash 854,993 B of 6,553,600 (13.0%). Previous run 78,644 B /
  852,737 B, so this ticket added **+32 B RAM and +2,256 B flash** (the GSV parser, the PCAS writer,
  the touch gate and the two text helpers; no new fonts).
- **Tests**: `test/host/run.sh` exit 0. New `test_touch_gate.cpp` (press held through the wake
  ignored until it lifts, press begun inside the window never becomes a tap, a new press at exactly
  300 ms accepted, second wake re-arms, reset) and `test_age.cpp` (the age rules above, the GPS cell
  strings for fix / searching / silent / two digits / clamped at 99, and the status row widths with
  the real mono advance). `test_timekeeper.cpp` gained the ZDA gate cases and now builds its valid
  ZDA from `buildYear()` instead of a fixed 2026. The other host tests still pass.
- **Could not check** (no device in the sandbox): that the wake frame is actually fully drawn 120 ms
  after SLPOUT on this panel (the driver's number, not a measurement); the gate's feel at 300 ms
  under a real finger; whether the L76K answers `$PCAS04,7` and whether GSV talkers change (the boot
  log now prints `[gps] sent ...` and `[gps] reply: ...`); whether the real GSV lines parse (unit
  tested only through the protocol, the parser itself is device code); the real in-view counts and
  SNRs at the balcony spot; the `GPS 0/5` cell on the panel and the short-form fallback with a full
  battery and 10+ nodes; a ZDA carrying a real date near the build year; `--` on the two stored
  contacts.
- **Next step**: Claude flashes and reads the boot log first: `[gps] sent $PCAS04,7*..` and whatever
  `[gps] reply:` lines follow, then `[hb] gps ... view=N snr=N` after a minute outdoors. Screenshot
  the status bar while searching (`GPS 0/N`) and the Nodes list (both contacts should now show `--`),
  then press Wake twice with a finger resting on the glass and confirm nothing is pressed.

## 2026-11-10 09:00 (Claude): EST-333 checked
- Built in the sandbox (exit 0, RAM 78,676 B, flash 854,993 B), host tests exit 0, wake path read (panel wake, 120 ms, redraw, backlight, touch gate). Not flashed alone: goes to the device with EST-334 (one interruption for Filip). Unverified claim in the entry above: that the L76K keeps its almanac "in its own flash" across the boot reset pulse; the boot log after the flash and the time to a fix will tell.

## 2026-11-10 08:50 (DS, EST-334, chat: selector drag, 160-byte messages, composer limit, no keyboard button, sharp badge)

- **What this ticket is**: the five things Filip found in the new chat on the device at ~08:40: the
  channel/DM selector scrolls the wrong way, a long bot reply is cut after 94 bytes, the composer has
  no idea how many bytes it may send, the keyboard button in the compose line is redundant, and the
  unread badge on the Chat tab is fuzzy.

- **1. The selector follows the finger.** The list's scroll subtracts from the row's y
  (`selRowTop = list.y + off - scroll`), so a finger that moves UP has to make the scroll BIGGER; the
  old drag did `g_selScroll0 - dy` and ran the list away from the fingertip (the message band below
  has the opposite sign convention, which is why it was right and this was not). The arithmetic now
  lives in `om::ui::selDragScroll(base, dyUp, maxScroll)` (`ui/chat_layout.cpp`), the one function the
  loop's drag branch calls, with the same 10 px of stretch past either end (`kSelGive`, now in
  theme.h) the message band has, snapped back on release. A second sample of one gesture is still
  measured from the press, not from the last sample.

- **2. Long messages are no longer cut.**
  - `src/mesh_text.h` (new, plain C++) holds MeshCore's own limits in one place: `kMeshTextMax` =
    `MAX_TEXT_LEN` (160 bytes, helpers/BaseChatMesh.h, 10 * CIPHER_BLOCK_SIZE), `kMeshTextCap` = +1,
    `kMeshNameMax` = `sizeof(ContactInfo::name)` (32, terminator already inside), and our send name
    `kMeshOwnName` / `kMeshOwnPrefix` ("OM Lite" + ": " = 9 bytes). `mesh_link.cpp` static_asserts
    both numbers against the real upstream constants, so a MeshCore upgrade that changes either stops
    the build instead of silently cutting text again.
  - `mesh_link.cpp`: `Msg.name[20] -> 32`, `Msg.text[96] -> 161`, `s_lastText[96] -> 161`, the
    duplicate-dispatch guard's `lastText[64] -> 161` (strncmp over MAX_TEXT_LEN, not 63). The
    "<name>: <text>" split now accepts a name up to the full `ContactInfo::name` and keeps the whole
    text after it.
  - `ui/chat_model.h`: the selector's per-chat copies are sized the same way, `name[28] -> 36`
    (name + "DM: "), `lastFrom[20] -> 32`, `lastText[60] -> 161`, so the preview line gets the whole
    message and is only shortened by `drawTextEllipsis`, which cuts on a code point boundary and ends
    with "...". `main.cpp`: `g_kbText[96] -> 161`, `g_replyName[20] -> 32`, the send wire buffer
    161 + 32 + 4.
  - `ui/screen_chat.cpp`: the selector preview line `line[96] -> 195` ("from: text" at both limits),
    one wrapped line `buf[128] -> 161`, the reply bar `bar[48] -> 35` ("RE " + a full name).
  - **The message Filip quoted** ("[OM Bot Ljubljana] Last Seen: ... Heard Recently: 4") is 134
    bytes on the air with its 20-byte "`[OM Bot Ljubljana]: `" prefix, 114 bytes of text: it fits
    MAX_TEXT_LEN whole and now lands whole in the 161-byte ring. The old 96-byte `text[]` cut it
    after 76 text bytes, which is the "M" of "MC".
  - **PSRAM: no need and not better.** The ring grows from 32 x 116 B to 32 x 194 B, 6.2 KB in all;
    the whole ticket costs +4.2 KB of static RAM, and the node is at 25.3 % of 320 KB. Static keeps
    the receive path off the heap (no fragmentation on the loop task, no malloc under a packet), and
    PSRAM reads are slower on every wrap, draw and lookup for no gain at this size. If a later ticket
    wants a longer history, the ring is the first candidate for PSRAM.
  - **Audit of the radio -> screen path for other fixed-size buffers** (ticket asks for the list):
    - `lib/meshcore` `onGroupDataRecv`: `data[len] = 0`, upstream's packet buffer, untouched; the
      text is at most MAX_TEXT_LEN by construction.
    - `mesh_link.cpp`: `s_lastText` (161), duplicate `lastText` (161), `Msg.name` (32),
      `Msg.text` (161), all sized above. `meshContactList` -> `MeshContact.name[32]` is the upstream
      contact-name cap, no cut.
    - `main.cpp`: `g_kbText` (161), `g_replyName` (32), the send wire buffer (197). `refreshChats`
      copies into the ChatEntry fields (36 / 32 / 161). Channel names are not message text:
      `channels.h McChannel.name[24]` and the name editor's `g_editBuf[20]` still cut a channel name
      at 23 / 19 bytes (MeshCore allows 32); left alone here because it is the channel store, not the
      chat, and named for a later ticket.
    - `ui/screen_chat.cpp`: selector preview line (195), one wrapped line (161), reply bar (35),
      INFO's `line[64]` holds "12m ago" only (the FROM and CHANNEL values are drawn straight from the
      view through `drawTextEllipsis`, no text copy exists there).
    - `ui/text_wrap.cpp`: `kMaxWrapLines` = 24. A 160-byte message at the L2 width (306 px, ~51 mono
      chars) wraps to at most 5 lines, so the cap cannot cut; every line starts and ends on a code
      point boundary.
    - `ui/widgets.cpp`: `drawTextEllipsis`'s `buf[80]` stops at 76 bytes, which at 6 px per mono
      glyph is 456 px, wider than the 320 px screen, so it cannot bind today (and its cut is code
      point safe and ends with "..."). `drawRun`'s `chunk[112]` chunks a draw call, it does not cut.
    - `board.cpp` `s_nmea[128]` is a GPS sentence, not chat.

- **3. The composer counts bytes.** `src/ui/compose_text.h` (new, plain C++) is the budget shared by
  the device and the host test: `composeAllowance(replying, name)` = MAX_TEXT_LEN - our 9-byte
  "<name>: " prefix - "@Name " for a reply (151 without a reply, 151 - name - 2 with one),
  `composeRemaining`, `composeAppend` (refuses a whole key or code point that would not fit),
  `composeBackspace` (removes a whole code point, so an emoji does not leave broken bytes) and
  `composeCopy` (COPY: whole text, or cut on a code point boundary plus "..."). `chatKey` uses them,
  `chatSend` refuses and logs an over-long draft instead of letting MeshCore's `sendGroupMessage`
  cut it, and `meshLinkSend` itself refuses a text whose prefix would pass MAX_TEXT_LEN. Near the
  limit the draft line shows the remaining bytes in mono dim at its right, from 20 left
  (`kComposeWarn`), and that room is taken out of the text width before the tail is measured, so the
  tail never runs under the counter. The counter is drawn on both the compose field and the sheet's
  draft line.

- **4. The keyboard button is gone** (Filip: "remove the keyboard icon, tapping the message line
  already brings it up"). The `kbdBtn` rectangle, its drawing, its hit test (`ChatHit::KbdBtn`), the
  `drawKbIcon` vector, `kKbdBtnW` and `kComposeGap` are all removed; the draft field is the whole
  compose row (`compose.w - 2 * kComposePadX`) and a tap anywhere on it opens the sheet.

- **5. A sharp badge.** One widget, `ui::drawCountBadge` (`ui/widgets.cpp`), for every count that sits
  on red: a 13 px circle drawn with LovyanGFX's anti-aliased `fillSmoothCircle` (radius 6) for one
  digit, a 19x13 pill with `fillSmoothRoundRect` for "9+", a 1 px ring in the bar colour drawn first
  so the shape separates from the tab icon under it, and the digits from a new pixel-exact 5x7 face
  with no anti-aliasing at all (`ui/badge_font.h`: white on the solid red, one bit per pixel, which
  is what reads sharp at this size; the smooth SansTab face's coverage is what bled white into the
  red and read pink). "9+" is now drawn for anything above nine (the old code drew a bare "9"), and
  the same `badgeText()` rule feeds the gold counts in the header and the selector, so they cannot
  disagree. Geometry is in `theme.h` (`kBadgeD`, `kBadgePillW`, `kBadgeRing`, `kBadgeTop` =
  kTabTopLine + 1, `kBadgeCxOfs` = kTabIcon / 2) and handed out by `countBadgeRect()` in layout.h:
  the badge starts 1 px under the selected tab's 2 px gold line (it used to sit ON it) and its last
  row is 1 px above the SansTab label's line box. The 1:1 + 8x contact sheet is
  `docs/shots/badge-preview.png`, rendered by the new `tools/mkbadge.py`, which parses the digit table
  and the theme numbers from the sources so the preview cannot drift from the device (only the AA rim
  is approximated: 16x supersampled with the same radii).

- **Build** (`pio run -e om_lite_l2`, the command from REBUILD.md): exit 0, SUCCESS, 12.1 s.
  RAM 82,924 B of 327,680 (25.3 %), flash 856,825 B of 6,553,600 (13.1 %). Previous run 78,676 B /
  854,993 B, so this ticket added **+4,248 B RAM and +1,832 B flash** (the wider ring and copies,
  the compose budget, the badge widget and its pixel face, the preview tool).
- **Tests**: `test/host/run.sh` exit 0. New in `test_chat.cpp`: the selector drag through the same
  `selDragScroll` the device calls (a fingertip up by 12/37 px moves the row up by exactly that,
  measured on `selRowTop`; the sign that was wrong; a second sample of one gesture; the stretch and
  its clamp at both ends and on release; a list that cannot scroll); the composer budget (160 =
  MAX_TEXT_LEN, the 9-byte own prefix, 151 without a reply, -name-2 with one, a full draft then a
  refused key, the counter's 20-byte threshold, a 4-byte emoji that fits exactly / refused as a unit
  when 1 byte is left, backspace over a whole code point, COPY cut on a code point boundary with
  "..."); the badge (13 px circle / 19x13 pill, same centre, clear of the gold line and of the label,
  inside the tab, right of the tab's centre; 1..9 and "9+" for 10 and 99, 0 draws nothing; the face's
  5x7 rows stay inside five bits and both "9+" glyphs fit the pill); and the compose row is now the
  field alone (`field.w == compose.w - 2 * kComposePadX`, all the way to both pads). The rest of the
  suite (layout, contacts, time, touch gate, age/status, 9 screenshot tests) still passes.
- **Could not check** (no device in the sandbox): the drag under a real finger (the sign is proven,
  the feel is not); the 160-byte message on the panel: the wrap, the bubble heights and whether a
  5-line message is comfortable in the 139 px band; the emoji reply from the real bot; the counter at
  20/3/0 bytes on the mono 10 px face and at the right of the sheet's draft line; the compose line
  with no button (and that a tap on the empty right end of the field opens the sheet); the badge's
  antialiased rim and the pixel digits at 13 px on the real panel, and whether 1 px of clearance to
  the gold line and to the label reads as clear; the 8x preview's AA rim is an approximation of
  LovyanGFX's analytic blend.
- **Next step**: Claude flashes and screenshots: the Chat tab with a count (expect the sharp circle,
  "9+" when the count passes nine) and the badge clear of the gold line; the selector, dragged up
  with the finger (the list must move up with it) and to the end (snap back); the long bot reply
  whole, including the emoji placeholders; the compose line with no keyboard button and the
  remaining-bytes counter from 20 left (type 140 characters, then keep typing past 151 to see it
  stop at 0).

## 2026-11-10 08:53 (Claude): EST-333 + EST-334 on the device
- Built in the sandbox (exit 0, RAM 82,924 B, flash 856,825 B), host tests exit 0, OTA ok (857,184 B). Badge preview checked first (`docs/shots/badge-preview.png`): crisp.
- Boot log, GPS, indoors: `$PCAS04,7*1E` sent, no `$PCAS` acknowledgement seen (the module kept talking, RMC void, ZDA 2026 from its own clock). `view=13(g10 l0 b3 a0) used=0 snr=0`. The GSV lines carry elevation and azimuth but an EMPTY C/N0 field for every satellite (`04,05,091,,`): these 13 are satellites the module EXPECTS from its almanac, not satellites it HEARS. So the status bar's `GPS 0/13` (screenshot `docs/shots/2026-11-10-0853-gps-predicted.png`) reads as "13 heard" while nothing is received. Must count only satellites with a C/N0 value.
- Also seen: the status bar's left group runs into the right one (`2B 2 nodes` with no gap). The node name in sent messages is the constant "OM Lite" (`kMeshOwnName`); it follows the real node name once the core moves to upstream's companion prefs.

## 2026-11-10 09:05 (DeepSeek): EST-335 heard vs expected, 60 s best C/N0, status row fit
- **The bug.** GSV's "satellites in view" is what the module's ALMANAC expects, not what it hears: indoors every C/N0 field is empty (`$GPGSV,3,1,10,04,05,091,,05,35,311,,...`) and the status bar read that as "13 heard". A satellite is now HEARD only when its C/N0 field has a value above zero (a literal "0" is not a measurement either); the GSV number stays beside it as `expected`.
- **New `src/gps_sky.h`** (plain C++, no Arduino/LovyanGFX, so the host test compiles it): `GpsState` moved there, with `expected[]`/`heard[]` per constellation (GP / GL / BD / GA / other), the totals (a GNGSV-only receiver's GN count is used only while no NAMED constellation has reported, so it is never counted twice), `maxHeard` (most satellites heard at once since boot; a new GSV cycle starts the live count over) and `bestSnr` (the best C/N0 of the last 60 s). The GSV parser, the NMEA checksum and the totals left `board.cpp`; the UART, GGA and RMC/ZDA stay there. `gpsPoll()` calls the refresh every poll, so the window decays even when the module has gone quiet. `gpsBegin()` no longer clears the sky: the counts live in the caller's `GpsState`, and the only re-begin (the 10 s pin retry) runs while `bytes == 0`, i.e. before anything was parsed.
- **The 60 s window** is 64 one-second buckets (`GpsSnrWindow`): each second keeps its strongest value, a bucket older than 60 s is ignored, so the reported value is the exact maximum of the window and, when the strongest sample ages out, the best of what is left takes over (a single value+timestamp would have reported 0). The stored second is 16-bit, so the 18 h `millis()` wrap is handled by unsigned subtraction.
- **Status bar.** While searching the cell now reads "GPS used/heard" as the ticket asked: `GPS 0/0` indoors, `GPS 0/4` when four are received but none is used yet; with a fix it stays `GPS 7`; before the module ever talks, `GPS --`.
- **Status row.** One `kStatusGap` (6 px) between every pair of cells, the left group -> right group boundary included; the battery pair keeps the mock's 8 px (`.bits` gap). Measured from Claude's 08:53 shot: left group ends at x 106, the node cell starts at 115 (8 px), the right cells are 6 px apart, so the groups did not overlap there; what the code lacked was the fitting ORDER. The old code only ever shortened the GPS cell and, if that still did not fit, drew an overlapping row. New `chooseStatusRow()` tries: full row -> node count's word dropped ("12 nodes" -> "12") -> GPS label dropped ("GPS 0/13" -> "0/13") -> node cell dropped. The host test sweeps node counts 0..999 x heard 0..99 x battery 0..100 % and asserts the chosen form always fits at 320 px and that no earlier form fit when a later one was chosen. The node cell is clamped to three digits.
- **Heartbeat.** `[hb] gps bytes=.. nmea=.. bad=.. expected=13(g10 l0 b3 a0) heard=0(g0 l0 b0 a0) used=0 best=0 maxheard=0 fix=0`, and the `[gps]` change line prints `used=/expected=/heard=/best=/maxheard=`. The screen redraws when `heardTotal` changes too (it watched only `sats` and the fix before), so the cell moves 0/0 -> 0/4 as satellites come in.
- **Build** (`pio run -e om_lite_l2`, the REBUILD.md command): exit 0, SUCCESS. RAM 83,124 B of 327,680 (25.4 %), flash 857,297 B of 6,553,600 (13.1 %), `firmware.bin` 857,664 B. Previous run 82,924 B / 856,825 B, so this ticket added **+200 B RAM and +472 B flash** (the 64 x 3-byte window, the extra GpsState counters, the status-row measurement).
- **Tests:** `test/host/run.sh` exit 0 (7 g++ binaries plus the 9 python checks). New `test/host/test_gps.cpp` with real NMEA sentences and checksums: the indoor 3-message cycle = 10 expected / 0 heard / best 0; a mixed cycle counts only the filled C/N0 fields (one blank and one literal "0" are not heard) = 4 heard, best 45; per-constellation counts and totals; a new cycle resets `heard` but not `maxHeard`; the 60 s window (31 at t=0 is best at 59,999 ms and gone at 60,000; 45 at 0 s + 31 at 30 s gives 31 at 61 s; 16-bit second wrap); the GN-only fallback; `gpsResetSky`; a one-character checksum change fails. `test_age.cpp` now covers `GPS 0/0`, `GPS 0/4`, `GPS 7`, the node cell, and the tight row: `12 nodes` + `GPS 0/24` + `100%` = 217 px (left group ends 107, so 320 - 217 = 103 < 113) does not fit, dropping the word gives 181 px and fits, and the GPS label is dropped only when even that is too wide.
- **Could not check** (no device in the sandbox): the real L76K in the open sky (how many satellites it hears, whether it ever sends a literal "0", whether a satellite repeats inside one cycle); the new cell and the dropped-word row on the panel at 1:1; whether an 8 px boundary gap reads as a gap to Filip on the glass (the mock's spacer has no minimum of its own, so the leftover slack sits there); the heartbeat text as it arrives on the USB CDC. Settings does not show `bestSnr`/`maxHeard` yet (later ticket).
- **Next step:** Claude flashes and takes the device outside: indoors the cell must read `GPS 0/0` (not `0/13`) and the heartbeat `expected=13 heard=0 best=0`; in the open sky `heard` must rise before `used`, `best` should reach the 30-45 dB-Hz range, and after coming back in `maxheard` must keep the outdoor peak while `best` falls to 0 after a minute. If Filip still sees the two groups too close at 2 nodes, the next change is a wider boundary minimum (one constant in `ui/theme.h`); the cell gap itself is now uniform.

## 2026-11-10 09:08 (Claude): EST-335 on the device
- Built in the sandbox (exit 0, RAM 83,124 B, flash 857,297 B), tests exit 0, OTA ok (857,664 B). Indoors: `expected=13(g10 l0 b3 a0) heard=0 used=0 best=0 maxheard=0`, as it should be. Note: GLONASS expected stays 0 after `$PCAS04,7`; whether the mode was taken is still open (no `$PCAS` reply, no GL talker yet). Outdoor test next (Filip, balcony, upright).

## 2026-11-10 09:25 (DeepSeek): EST-336 path hash really 2B, one GPS number, clean wake, no "Say something", new message sheet

- **What this ticket is**: the five things Filip found at ~09:05 on the EST-333/334 build. Where he quoted
  the mock, his words win.

- **1. The path hash is 2B on the air, not just in the status bar.** Two separate bugs, as Claude said:
  `ui/widgets.h` carried `hash = "2B"` as a literal that nothing ever wrote to (so the bar always said 2B),
  and every flood went out with MeshCore's `sendFlood(pkt, delay)` default of `path_hash_size = 1` because
  `BaseChatMesh::sendFloodScoped()` calls it without a size (both variants, BaseChatMesh.cpp lines 14-18).
  - `OmChat` now overrides BOTH virtual `sendFloodScoped` variants (`ContactInfo` and `GroupChannel`) and
    passes the configured size, exactly the way upstream's companion does (`_prefs.path_hash_mode + 1`,
    reference MyMesh.cpp ~490-517). That covers channel messages, ACKs, path replies and DM floods.
  - The self advert no longer calls `sendPacket(pkt, 0, 800)` (which left the route type and size as
    `createAdvert()` made them, i.e. 1-byte hashes): it is a real `sendFlood(pkt, 0, configured)`, like
    upstream's `CMD_SEND_SELF_ADVERT`.
  - One setting: `s_pathHashBytes`, 2 by default, allowed 1..3, saved in NVS as `phs` in the `mcnet`
    namespace, read in `meshLinkBegin()` before the first advert (`[mc] path hash 2B (NVS phs=2)` on the
    boot log). Facade: `meshPathHashBytes()` / `meshPathHashSet()` / `meshPathHashLabel()`; the Settings
    ticket wires the page, this ticket only stores it. `meshPathHashSet()` refuses a value outside
    1..3 (returns false) rather than clamping it silently.
  - The status bar no longer has a string literal: `StatusModel.pathHashBytes` (number) is filled from
    `meshPathHashBytes()` by `buildStatus()` and drawn as `N` + "B", so the bar and the air cannot disagree.
  - **The received side needs nothing**, checked: `Packet::getPathHashSize()` is `(path_len >> 6) + 1`,
    carried per packet, and MeshCore reads it on RX, forwarding and dispatch (`Mesh.cpp` uses it for the
    path checks and the append). Our receive path does not assume 1.
  - **How to verify on the air** (Claude, on gandalf): send a channel message from the L2 with one repeater
    between, then look at that message on OM: its path must be **2 bytes for that one hop** (4 for two
    repeaters), where the old build showed 1 byte per hop. The L2's own advert is a flood too now, so OM's
    recently-heard/advert path for our node must also show 2-byte entries. On the device, the boot log's
    `[mc] path hash 2B` and the status bar's `... · 2B` come from the same variable.

- **2. One GPS number.** The cell now shows only the satellites HEARD right now (`C/N0 > 0`, EST-335's
  counting): `GPS 4` while searching in `kDim`, the same `GPS 4` in **`kGreen`** (theme.h, the
  "repeater/good" green `#39d98a`) once there is a fix, so the fix is the colour and not a second number;
  `GPS --` while the module is silent. `GpsCell.used` is gone from the cell (the struct no longer has it);
  the used count and the expected count stay in the heartbeat (`[hb] gps ... expected=... used=...`). The
  fitting row is unchanged in order, only the cell text is shorter, so the "drop the word, then the GPS
  label, then the node cell" rules still hold.

- **3. Clean wake, no diagonal.** What LovyanGFX 1.2.27 actually sends for the NV3031B (read from the panel
  driver in the libdeps tree): `sleep()` = **SLPIN only**; `wakeup()` = **SWRESET, 120 ms, the whole init
  table (which ends with SLPOUT + 100 ms), then DISPON with NV303x's dummy byte**. So the panel turns
  DISPLAY ON inside `wakeup()`; DISPOFF cannot be held through the sleep-out with the public API alone.
  `boardSetBrightness()` does **not** ramp: it writes 0x18..0x1B then latches 0x0F, so the light steps.
  - New `board.cpp panelDisplay(bool)`: DISPOFF / DISPON through LovyanGFX's `powerSaveOn()/Off()`
    (0x28 / 0x29 + the dummy byte). `screenPower(false)` is now light 0 -> DISPOFF -> SLPIN;
    `screenPower(true)` is wake -> DISPON -> 20 ms -> light.
  - The wake path in `loop()` is now: `panelPower(true)` (SWRESET + init, ends with DISPON), **DISPOFF at
    once**, the 120 ms sleep-out, `redrawCurrentScreen()` (the whole frame, pushed into the panel's RAM
    while the display is off), **DISPON**, one frame period `kPanelFramePeriodMs` = 20 ms, then the
    backlight. So the diagonal (the panel scanning a half-written frame, or scanning garbage after the
    reset) can no longer be seen: the display is dark through both, and the light comes up on a complete
    frame.
  - The ticket's fallback ("at least light the backlight after one full frame period past the push") is
    therefore also in place; the DISPOFF part additionally hides the post-reset RAM.

- **4. Empty chat**: "Say something." is gone; only "No messages yet". `drawEmptyState()` now takes a null
  or empty sub as one line, centred on the band instead of 9 px high.

- **5. A message sheet, not the three-button bubble menu.** `src/ui/message_sheet.h/.cpp` (new, plain C++,
  host-tested) builds the rows and the geometry; `screen_chat.cpp` draws it and answers the taps with the
  same rectangles. It is a bottom sheet in the mock's style (rounded top corners, full width, over the tab
  bar, tap outside closes), as tall as its contents (7 rows max: 2*6 + 34 + 7*22 = 200 px, so it never
  reaches the status bar). Header (what INFO used to show, so INFO is no longer a step): sender in sans
  with the age at the right, then one mono line "channel · N hops · SNR x.x dB" ("local echo" / "SNR --"
  where unknown).
  - On someone else's message: **Reply** (the "@Name text" draft, as before), **Direct message** (only
    when a DM can actually be opened: `meshDmAvailable()` is false while the DM store is empty, so the row
    is left OUT, not greyed), **Copy text**, **Mention** ("@Name " into the draft without the reply bar),
    **Node details** (jumps to the sender in Nodes, only when the name matches a known contact),
    **Show on map** (only when that contact has an advertised position: recentres the map and drops the
    marker), **Path** (only when the packet carried a repeater; the value is the named path, below).
  - On our own message: **Copy text**, **Resend** (same text, same channel, a new packet: a fresh
    timestamp makes a new payload, MeshCore's own `sendGroupMessage` does the rest), **Heard repeats**
    with the count at the right.
  - **Heard repeats works, with no lib/meshcore edit.** MeshCore's `MeshTables` is a public virtual
    interface, and `SimpleMeshTables` was ours to subclass: `OmMeshTables` remembers the packet hash that
    MeshCore `markSeen()`s on the outgoing packet (while our send is in flight), and when a copy of that
    hash comes back from a repeater, `wasSeen()` sees it before MeshCore drops the duplicate and counts it
    on the local echo with the same message id. So the row is shown; the count is "times we heard our own
    packet repeated", not a hop count.
  - **Path naming**: the hash bytes are stored per message as they arrived (up to 64 B in the ring, about
    +2.1 KB RAM for 32 messages). MeshCore's path hash is the first 1..3 bytes of the repeater's public key
    (`Identity.h copyHashTo`), so `meshPathHopName()` matches each entry against a **known repeater's**
    pub_key prefix and returns its name; an unmatched entry stays lower-case hex at the packet's own hash
    width (1 byte = 2 digits, 3 = 6). Entries are joined " > " ("OM-RPTR > 3fa2").
  - The old bubble menu and the INFO panel are removed where nothing calls them (draw functions, hit
    cases, `ChatHit::Menu*/InfoClose`, `ChatLayout`'s menu/info rects, `chatMenuHeight/chatInfoHeight`).
  - New facade bits: `meshMsgPath()`, `meshMsgHeardRepeats()`, `meshContactByName()`, `meshPathHopName()`,
    `meshDmAvailable()`.

- **Build** (`pio run -e om_lite_l2`, the REBUILD.md command): exit 0, SUCCESS, 21.4 s. RAM 85,724 B of
  327,680 (26.2 %), flash 860,777 B of 6,553,600 (13.1 %), `firmware.bin` 861,136 B. Previous run 83,124 B /
  857,297 B, so this ticket added **+2,600 B RAM and +3,480 B flash** (the 32 x 64-byte message paths and
  the repeat counters, the path-hash plumbing, the sheet and its rows, the empty-state change).

- **Tests**: `test/host/run.sh` exit 0 (7 g++ binaries + the 9 python checks). New `testMessageSheet()` in
  `test_chat.cpp`, compiled against the same `message_sheet.cpp` the device draws with: the 7 rows of a
  channel message with every fact available and their order; the rows that are left out (no DM store, no
  known sender, no position, no path) and that each appears exactly when its fact does; the 3 rows of our
  own message and that no Reply/Mention is offered there; the cap and the null/0 cases; the geometry (full
  width, bottom edge, 200 px for 7 rows, above the status bar, header then rows with no gap, each row 22 px,
  the last row ends kSheetPadY above the floor) and the hit rects (each row's own band, the header and the
  edges answer -1); the path naming (named + hex, 1/2/3-byte widths, unknown, no resolver, empty, a cap
  smaller than the first name, 63 bytes accepted and 66 refused). `test_age.cpp` was rewritten for the one
  number (`GPS 4` searching and with a fix, `GPS 0` indoors, `GPS --` silent, two digits, the 99 clamp) and
  its status-row widths recomputed (a full row is 193 px now); the tight case uses `999 nodes` + `GPS 24`
  because the shorter cell made the old `12 nodes` row fit.

- **Could not check** (no device): the on-air path size (Claude + OM on gandalf, above is the test); the
  sheet on the panel at 1:1 (row height 22 px, the header's two lines, a 7-row sheet over the tab bar, the
  Path row's long value); the wake with a real eye on the glass (the artefact is timing, we only reason
  about it); whether OM shows our advert's path as 2-byte entries; whether a repeater actually repeats us
  within the test window (the Heard repeats count may stay 0 on a quiet band, which is honest); the "2B"
  with a value saved to NVS (no page writes it yet, so only the default 2 path is exercised).

- **Next step**: Claude flashes and checks, in this order: boot log `[mc] path hash 2B (NVS phs=2)`; the
  status bar `MC · EU868 · 2B`; indoors `GPS 0` (dim) with the heartbeat `expected=13 heard=0`; the chat's
  empty state with only "No messages yet"; a tap on someone else's message (sheet header + Reply, Copy
  text, Mention; Node details / Show on map / Path only where they apply); a tap on our own message (Copy
  text, Resend, Heard repeats); tap outside to close. Then on gandalf, the one real proof: OM must show
  our next message/advert with **2 bytes per path hop**.

## 2026-11-10 09:28 (Claude): EST-336 on the device
- Built in the sandbox (exit 0, RAM 85,724 B, flash 860,777 B), tests exit 0, OTA ok (861,136 B). Boot: `[mc] path hash 2B (NVS phs=2)`, advert sent as a 2B flood. Status bar: `GPS 0` indoors (heard only), 2B from the setting. On-air check through OM on gandalf still to do (needs a message or advert from the L2 heard there). Wake, message sheet: Filip to check.

## 2026-11-10 09:43 (DeepSeek): EST-337 — GLONASS mode 7 for real, and the new top bar
- **1. GLONASS.** The old `configureL76K()` sent `$PCAS04,7` 250 ms after `gpsBegin()`, i.e. while the
  L76K was still starting (the boot reply window had binary bytes before the first NMEA line), so the
  module ignored it and stayed in its GPS+BeiDou factory mode (`expected=12(g9 l0 b3 a0)`).
  - New plain-C++ `src/gps_mode.h` (`GpsModeSetter`): waits for the first sentence with a valid
    checksum, sends once, watches 15 s for `$GLGSV` (or a GL PRN in `$GNGSV`, or the NMEA 4.1 system
    id 2 in `$GNGSA`), resends at 15 s and 30 s, then gives up. Logs exactly
    `[gps] mode 7: sent (try N)`, `[gps] mode 7: GLONASS seen`, `[gps] mode 7: not taken after 3 tries`.
  - A module reboot resets it: an explicit `onModuleReboot()` from `gpsBegin()`, or a >5 s gap between
    valid sentences, sends again on the next sentence.
  - `board.cpp` no longer blocks or dumps a reply window: `gpsBegin()` only opens the UART, and
    `gpsPoll()` drives the machine on every complete, checksum-valid line and on its own clock.
    Nothing writes the module's flash ($PCAS00 is never sent), so a factory-reset module still works.
- **2. Top bar (EST-337).** `ui/status_cells.h` is rebuilt around `statusBarRects()`: LEFT is the clock
  only (mono ink, `--:--` until a real source), then the radio-fault mark; RIGHT from the screen edge is
  the gear, the battery cell (percent + icon, unchanged 8 px inner gap), the GPS cell from EST-336, then
  DND, Wi-Fi and Bluetooth. The last three are drawn only when on; the DND setting is live, Wi-Fi and
  Bluetooth are reserved slots (their widths are theme.h constants) for later tickets. Every adjacent
  pair is exactly `kStatusGap`. The green dot, `MC`, `EU868`, `2B` and the node count are gone, and
  `chooseStatusRow()`/the node cell/short GPS forms are deleted rather than left dead. The measured-fit
  test remains and covers the worst case (all icons on, `100%`, `GPS 14`).
- **Radio fault mark.** New plain-C++ `src/radio_fault.h`: reasons Radio (chip failed to start / stopped
  answering), Mesh (MeshCore did not start) and Tx (last transmission failed); the more severe reason
  wins; a good TX or a radio re-init clears it; there is deliberately no silence input. Logs
  `[radio] fault: <reason>` and `[radio] fault cleared`. `radio.cpp` sets Radio on a failed `begin()`;
  `mesh_link.cpp` sets Radio on a failed `std_init()`, main sets Mesh when `meshLinkBegin()` fails, and a
  new `OmSX1262Wrapper` reads the real RadioLib result (`startSendRaw` false or a >15 s incomplete TX
  sets Tx, `onSendFinished` clears it). A tap on the 11 px red mark opens a one-line toast naming the
  reason (2.5 s, then the screen is redrawn).
- **Nodes header.** `ui/nodes_header.h` draws one mono dim line above the list:
  `<N> nodes heard`, plus `· <M> in the last hour` only when `timeKeeper().realSinceBoot()` (the same
  real-timestamp rule as the ages). `Layout::listHeader()/listBody()` feed both drawing and hit testing;
  rows and scroll now start under the 18 px header.
- **Icons.** `tools/mkicons.py` gains the 11x11 DND moon and the 11x11 fault triangle; the preview was
  generated and checked first (`docs/shots/icons-preview.png`).
- **Build** (`pio run -e om_lite_l2`, REBUILD.md command): exit 0, SUCCESS. RAM 85,804 B of 327,680
  (26.2 %, previous 85,724, +80 B), flash 863,189 B of 6,553,600 (13.2 %, previous 860,777, +2,412 B),
  `firmware.bin` 863,552 B (previous 861,136, +2,416 B).
- **Tests**: `test/host/run.sh` exit 0 (10 C++ binaries + the 9 python checks). New:
  `test_gps_mode.cpp` (line detection; send only after the module talks; seen; 15 s retry x3; give up;
  explicit and gap reboots; GLONASS already on sends nothing), `test_radio_fault.cpp` (each reason,
  severity, clears by TX and re-init, silence never sets it, tokens/toast text), `test_status_bar.cpp`
  (left time, right chain with one gap, every optional icon on/off, fault rect, worst-case fit at 320
  and 800 px, a 200 px screen correctly reported as too narrow, Nodes header text). `test_layout.cpp`
  was updated for the 18 px header/list body and the fault rect; `test_age.cpp` now covers the GPS cell
  only (the bar arithmetic moved).
- **Could not check** (no device): whether the L76K takes mode 7 and the heartbeat changes to
  `expected=..(g.. l.. b..)`; the bar at 1:1 (time, fault mark, DND moon, the toast, the reserved empty
  slots); the fault mark in a real failure (we cannot make the SX1262 fail on demand, so only the state
  machine is proven); the Nodes header with real "in the last hour" stamps; the list with 6 rows and
  the 18 px header.
- **Next step**: Claude flashes and checks, in this order: boot log `[gps] mode 7: sent (try 1)` after
  the first NMEA line, then `[gps] mode 7: GLONASS seen` within 15 s and the heartbeat's `l` count
  above 0; the top bar (time left; gear, battery, `GPS n`; DND moon when DND is on); the Nodes header
  (`4 nodes heard`, and the last-hour clause once GPS has set the clock); then Settings > Radio for the
  settings that left the bar (region, frequency, path hash) in the next Settings ticket.

## 2026-11-10 09:49 (Claude): EST-337 on the device
- Built in the sandbox (exit 0, RAM 85,804 B, flash 863,189 B), tests exit 0, OTA ok (863,552 B). Top bar as specified (time left; GPS, battery, gear right), Nodes header shows (`docs/shots/2026-11-10-0949-topbar.png`).
- **GLONASS still 0** (`expected=15(g12 l0 b3 a0)` after the retries): not taken; whether commands reach the module at all is unproven. Next ticket diagnoses it.
- **Bug (EST-333 rule):** every node shows `--` and the header says `0 in the last hour`: the "older than the build" cut-off moves with every flash. Fixed cut-off in the next ticket. Emoji in node names draw as the placeholder box (emoji ticket).

## 2026-11-10 10:10 (DeepSeek): EST-338 - fade, times instead of ages, keyboard bottom edge, fixed stamp cut-off, GLONASS TX-path diagnosis
- **1. Backlight fade.** `boardSetBrightness()` writes one of four 8-bit PWM registers (0x18..0x1B) and latches 0x0F <- 0x55. The tree names no part; the scheme (0x2C, per-channel current 0x14..0x17 written 200, four PWM registers, the 0x55 update latch) is the TI LP581x / LP5810-class 4-channel I2C LED driver layout. Its PWM is 8-bit, so the real range is 0..255: **256 duty levels**, and the old 0..255 brightness already was one level. The panel's 160 is 63 % duty; the current registers are not the fade.
  - New plain-C++ `src/fade.h/.cpp` (`FadeRamp`): a time-based, non-blocking ramp. `tick()` writes at most one level per call (one I2C step, no wait) and returns -1 when nothing changed. Fade-in 0 -> 160 over 200 ms with an ease-out cubic; fade-out 160 -> 0 over 150 ms linear. A new `start()` takes the CURRENT level as its start, so a wake during the fade-out reverses without a jump.
  - `loop()` ticks it every pass and drops its delay to 1 ms while the ramp runs (20 ms otherwise), so the 8-bit PWM gets many real levels in the 200 ms: the host test emits **114 distinct levels in, 149 out**; on the device the loop's own work may reduce that, but it is not a fixed 4/8/16-step table.
  - Sleep now fades the light to 0, then DISPOFF/SLPIN (`[wake] screen off (fade 150 ms from N)` ... `[wake] fade done at 0 (N steps)` ... `[wake] panel asleep`). Wake keeps EST-336's panel order (wake, DISPOFF, 120 ms, redraw, DISPON, one frame period), sets brightness 0 and starts the 200 ms fade up; a second wake press during the fade-out reverses it. Touch stays under the existing 300 ms wake gate.
- **2. Timestamps instead of ages.** One formatter, `ui/age.cpp` `stampText()`, now used by the chat lines, the selector's last-message column, the message sheet header and the Nodes list:
  - today `HH:MM`; within the last 7 days `Mon 14:02`; older `10.11. 14:02` (Europe/Ljubljana, CET/CEST). The narrow `Clock` form (chat line and selector) is exactly five mono glyphs: `HH:MM` today, the date alone (`09.11`, `10.10`) otherwise.
  - Every message stores the system-clock seconds and `timeKeeper().realSinceBoot()` next to its monotonic uptime (`Msg.wall`, `Msg.wallReal`, accessors `meshMsgWallTs()`/`meshMsgWallReal()`); `ChatMsgView` and `ChatEntry` carry the same fields. The chat line reserves `kMsgStampW` = 5 x 6 px, so the name and the text start at one fixed column whatever the stamp is.
  - Contacts get a small side table in `mesh_link.cpp` (pub_key -> wall, uptime, real bit) filled in `onDiscoveredContact` (MeshCore sets `lastmod` just before that callback); `MeshContact` carries `lastmodUp`/`lastmodReal`. A contact restored from flash has no side entry and is judged by the fixed cut-off.
  - When the clock was not real at that moment (a message before the first GPS, a live contact from before the clock), the formatter falls back to the age from the monotonic stamps (`now`, `5m`, `3h`, `12d`, `30d+`), never to an invented time. A stamp in the future falls back to `now`.
- **3. Keyboard bottom edge.** `ChatLayout` now has separate `keys` (drawn, unchanged: 4 px side pad and the 4 px pad under the last row) and `keyHit` (touch). In `keyHit` the outer key of every row reaches x = 0 / x = 320 and every bottom-row key reaches y = 240; the 2 px gaps between keys stay gaps. `keyAt()` is the one hit test and uses `keyHit`. `omTouch()` clamps a raw GT911 y of 240+ into the screen before any hit test and logs the first clamp (`[touch] raw ... -> ... (clamped to 320x240)`). In `chatScreenHit`, a point inside the sheet that is not a key/draft/message does nothing; only `chatSheetClosesAt()` (a point above the sheet) closes it.
- **4. Fixed stamp cut-off (the EST-333 bug).** The moving build-time check is gone. `kMinSaneEpoch` = **2025-01-01 00:00 UTC** is the one constant: a stamp before it is the old 15 May 2024 VolatileRTCClock and prints `--`; a stamp after it is real and prints its time, even if it is before the current firmware's build. The Nodes `in the last hour` count uses the same rule. A contact heard after the real 08:18 clock now shows its time instead of `--`.
- **5. GLONASS diagnosis.** `gps_mode.h` is now a one-boot diagnostic after the first checksum-valid NMEA line:
  1. `$PCAS03,1,1,1,0,1,1,1,1` turns GSV off for 5 s (the 500 ms tail of the burst the command landed in is ignored), then `$PCAS03,1,1,1,1,1,1,1,1` turns it back on. No GSV during OFF and GSV back after ON logs `[gps] tx path: ok`; GSV during OFF logs `[gps] tx path: no effect`. This is the direct test of our GPIO17 TX path.
  2. Mode 7 (`$PCAS04,7`) gets a 15 s GLONASS watch.
  3. If not taken, mode 5 (`$PCAS04,5`, GPS + GLONASS) gets 15 s.
  4. Mode 3 (`$PCAS04,3`, GPS + BeiDou, factory) is restored at the end unless mode 7 worked.
  - Logs: `[gps] tx probe: GSV output off/on`, `[gps] tx path: ok|no effect`, `[gps] mode 7: sent`, `[gps] mode 7: GLONASS seen (accepted)|not taken after 15 s`, `[gps] mode 5: ...`, `[gps] mode 3: restored`, then `[gps] diagnostic done: tx path=..., mode 7=..., mode 5=...; accepted: ...`. The module's flash is never written ($PCAS00 is never sent).
  - Which mode this module actually takes is **not known yet**: it is what the device's next boot log will say. The machine leaves mode 7 on only if GLONASS was seen; otherwise it restores mode 3.
- **Build** (`pio run -e om_lite_l2`, the REBUILD.md command): exit 0, SUCCESS. RAM **89,012 B** of 327,680 (27.2 %, previous 85,804, **+3,208 B**), flash **867,773 B** of 6,553,600 (13.2 %, previous 863,189, **+4,584 B**), `firmware.bin` **868,144 B** (previous 863,552, **+4,592 B**). Most of the RAM is the 40-entry contact stamp table (~1.9 KB), the message/entry wall fields and the extra chat/Nodes text buffers.
- **Tests**: `test/host/run.sh` exit 0 (11 C++ binaries + the 9 python checks). New `test_fade.cpp` (curve start/end/monotonic, 114 levels in, 149 out, exact target, zero duration, clamping, a wake reversing a fade-out from the current level). `test_age.cpp` rewritten for `stampText()`: today, yesterday, five days, older than a week, the narrow five-glyph form, clock not real at the event, clock not real now, the old 2024 stamp, a stamp before the build but after 2025-01-01 showing its time, a future stamp, `30d+`, the cut-off boundary and a tiny cap. `test_gps_mode.cpp` rewritten: no command before the module talks, the GSV off/on probe with its settle window, TX path ok, TX path no effect, mode 7 accepted, mode 7 refused then mode 5 accepted, both refused with mode 3 restored, explicit and gap reboots. `test_chat.cpp` gains the keyboard-edge case: y = 239 and 240+ clamped hit Send/space/123, drawn rects unchanged, outer keys reach the edges, a gap inside the sheet does nothing, a touch above the sheet closes it.
- **Could not check** (no device in the sandbox): the fade on the glass (200 ms in, 150 ms out, banding at the panel's real response); the raw GT911 y at the very bottom (the clamp handles 239/240+, the log will name the value); the time forms at 1:1 (the long `Mon 14:02` / `10.11. 14:02` in the Nodes row and the sheet header, the fixed chat column); the Nodes list with a real 11-character stamp; the actual GLONASS outcome and whether `$PCAS03` changed GSV on this module; whether the mode commands reach the module at all.
- **Next step**: Claude flashes and reads, in this order: `[gps] tx probe: GSV output off for 5 s`, `[gps] tx path: ok` or `no effect`, then `[gps] mode 7: ...`, `[gps] mode 5: ...`, `[gps] diagnostic done: ...; accepted: ...`; the heartbeat's `expected/heard` `l` count after the sequence. On the glass: press Wake (the light should come up over ~200 ms), press Wake again (down over ~150 ms, then the panel sleeps, no diagonal), tap Send at the very bottom edge, open a message (sheet header time), open the selector and the Nodes list (times, not `--`). The Nodes header should now count the nodes heard after 08:18.

## 2026-11-10 10:14 (Claude): EST-338 on the device
- Built in the sandbox (exit 0, RAM 89,012 B, flash 867,773 B), tests exit 0, OTA ok (868,144 B). Nodes list shows real times (09:52, 09:51, ...) and "9 nodes heard, 5 in the last hour" (`docs/shots/2026-11-10-1014-nodes-times.png`): the cut-off bug is fixed.
- **GPS TX path proven:** `[gps] tx path: ok (GSV stopped for 5 s and came back)`. Commands reach the module.
- **GLONASS result not trustworthy:** `mode 7: GLONASS seen (accepted)`, but the heartbeat still says `expected=13(g11 l0 b2)`, no GLONASS satellite listed. The detector counts a `$GNGSA ...,2` line (NMEA 4.1 system id 2 = GLONASS) as "seen", and those GSA lines were in the boot log at 08:52, BEFORE any command was sent. So either GLONASS was on from the factory (a GLONASS GSA slot from the first boot suggests it) and simply has no satellite listed yet, or the detector is fooled. Only a GLONASS satellite in GSV (GLGSV, or GN PRN 65..96) proves it; the outdoor per-constellation `heard` count will settle it.
- **Log flood (bug):** `[gps] diagnostic done: ...` is printed on every loop after the diagnosis: 4,062 lines in 80 s (417 KB of log). Must print once.

## 2026-11-10 10:45 (DeepSeek): EST-339 - GPS diagnostic out, log flood, gamma fade, tap-away is only close
- **1. One command, no diagnostic.** `gps_mode.h` is now `GpsModeCommand`: a single bool. The first
  checksum-valid NMEA sentence sends `$PCAS04,7` once, logged exactly `[gps] mode 7 sent`; every later
  sentence sends nothing; `gpsBegin()` (the 10 s pin retry) arms it again. Gone: the `$PCAS03` GSV
  on/off probe, the 15 s GLONASS watches, the mode 5 fallback, the mode 3 restore, the 5 s silence
  reboot detector, `GpsDiagAction`/`GpsDiagEvent`/`GpsDiagStep`/`GpsTxPath`/`GpsModeResult`,
  `gpsLineHasGsv()`, `gpsLineHasGlonass()` (the fooled detector), the `tick()` call in `gpsPoll()`, and
  the whole `runGpsDiagStep()` in `board.cpp`. Nothing claims the mode was accepted: the heartbeat's
  per-constellation `expected/heard` counts are unchanged and a heard GLONASS satellite (`l` above 0)
  is the only proof.
- **2. Log flood.** Root cause: `runGpsDiagStep()` ended with `if (s_gpsMode.done())` and was called on
  every sentence AND on every `gpsPoll()`, so the "done" state printed on every loop pass (4,062 lines
  in 80 s). It went with the diagnostic; in the new code the only GPS log is the one send.
  **Audit of the other 151 `Serial.*` statements** (all of `src/`, looked for a state printed while it
  holds instead of when it changes, i.e. a log on a per-loop or per-sentence path):
  - fixed: the GPS diagnostic line (above);
  - clear: the heartbeat (`[hb]`, every 2 s by design), `[gps] ... used=...` (1 s gate, prints only
    when sats/valid/heardTotal changed), `[batt]` (on change), `[wake] fade done/panel asleep` (once per
    fade), `[touch] raw ... clamped` (first clamp only), `[time] GPS -> ...` (on change), every boot
    line (setup), every tap/mesh/send/OTA line (one per action), `audio`/`radio` init and fault lines
    (on change);
  - noted, not changed: `map.cpp` prints `[map] decode z...` per decoded tile and `[map] z... -> N tiles`
    per rendered frame, so a pan at the 60 ms redraw rate logs ~16 lines/s. Each line is one real render
    (not a held state), and it stops when the finger stops; left as is.
- **3. Fade on a perceptual curve.** `FadeRamp` lost the `easeOut` flag: `levelAt()` is gamma 2.2 both
  ways, fade up `from + (to-from) * p^2.2`, fade down `to + (from-to) * (1-p)^2.2` (the fade-in curve
  mirrored in time), so the two directions look alike. Fade-in 300 ms (`om::kFadeInMs`), fade-out
  stays 150 ms (`om::kFadeOutMs`); `main.cpp` uses the constants and the sleep log now names the real
  start level. Host test: monotonic both ways, exact at both ends, a perfect time mirror between the
  directions, and after the first 20 % of the fade-in (60 of 300 ms) the duty is 5 of 160, i.e. under
  5 % of the target (a linear ramp would be at 32).
- **4. Tap away closes and is consumed, for every overlay.** New pure `chatOverlayPress()` in
  `chat_layout.h/.cpp`: the overlay on top (message sheet > selector > keyboard sheet, the draw order)
  and whether a point is inside it; the message sheet's panel comes from the new
  `chatSheetPanel()` (screen_chat). New `OverlayDismiss` in `touch_gate.h`: `onPress(outside)` spends
  the whole gesture, `spent()` is true until the finger lifts, `onRelease()` re-arms. `loop()` now
  decides on the PRESS: outside the top overlay it closes that overlay, logs
  `[tap] x,y away from <overlay>: closed, gesture spent`, redraws, and skips the drag scroll, the hold
  and the release tap; only the next new press acts. `chatScreenHit()` no longer lets a tap on a
  message above the keyboard open the message sheet (that was Filip's bug); anything above the sheet is
  `Outside`. The bug also fixed the message sheet: a drag started above it used to scroll the chat
  behind it.
  - Per overlay (host test): keyboard sheet (message band above it = outside, key/draft inside, a 2 px
    key gap inside but inert), selector (panel inside, header/compose outside), message sheet (rows and
    header inside, band above outside, sheet wins over the keyboard), and the guard sequence for each:
    tap away closes, its drag and release do nothing, the next tap acts.
  - **Not covered, because they do not exist in this tree**: a toast with a close (the fault toast has
    no button and auto-expires) and any confirm dialog (delete channel is a two-tap "tap again to
    delete" row, no overlay). The map's info panel is a map interaction (a tap elsewhere moves the
    pin), not one of the listed overlays, so it is unchanged. When a toast-with-close or a confirm
    arrives it must go through `chatOverlayPress`/`OverlayDismiss`.
  - Note for later (Filip, not this ticket): Send (bottom row right) sits under Backspace (row 3
    right), so a missed press sends instead of deleting.
- **Build** (`pio run -e om_lite_l2`, the REBUILD.md command): exit 0, SUCCESS, 21.4 s. RAM **88,988 B**
  of 327,680 (27.2 %, previous 89,012, **-24 B**), flash **866,385 B** of 6,553,600 (13.2 %, previous
  867,773, **-1,388 B**), `firmware.bin` **866,752 B** (previous 868,144, **-1,392 B**). The removal of
  the diagnostic pays for the overlay plumbing.
- **Tests**: `test/host/run.sh` exit 0 (11 C++ binaries + the 9 python checks). `test_gps_mode.cpp`
  rewritten: nothing before the module talks (null/empty/garbage/not-a-sentence), exactly one send and
  1,800 more sentences sending nothing, re-arm after a module reboot (twice in a row = one command).
  `test_fade.cpp` rewritten for the gamma curve (see 3.). `test_chat.cpp` gains `testOverlayTapAway()`
  (per overlay, including the exact message-band point that used to answer). `test_touch_gate.cpp`
  gains the `OverlayDismiss` sequence.
- **Could not check** (no device): the fade on the glass (300 ms in, 150 ms out, gamma banding at the
  panel's real response); tap-away on the real panel at 1:1 (every overlay, and that the release of the
  dismissing press never taps); whether the L76K takes mode 7 (still open, and no longer claimed); the
  one-line boot log in a real boot.
- **Next step**: Claude flashes and checks, in this order: the boot log has exactly one
  `[gps] mode 7 sent` and no `diagnostic done` line, and 80 s of log is small again; with the keyboard
  open, a tap on the chat area only hides it (no message sheet, no chat menu), and the NEXT tap on a
  message opens the sheet; tap above the open message sheet: it closes and nothing behind it fires;
  the same for the selector (open it, press the message band: closes, no row opens); press Wake: the
  light comes up over ~300 ms with no jump at the start, and goes down over ~150 ms.

## 2026-11-10 10:45 (Claude): EST-339 checked
- Built in the sandbox (exit 0, RAM 88,988 B, flash 866,385 B), tests exit 0. Committed before flashing because the L2 is outside for a GPS run; flash + device check when it is back.

## 2026-11-10 10:41 (Claude): GPS outdoor read-back + EST-339 on the device
- After the balcony (Filip saw up to 17 on the screen): `expected=21(g10 l8 b3) maxheard=19 best=46` dB-Hz. **GLONASS is on**: 8 GLONASS satellites now listed. Whether the factory mode had it or `$PCAS04,7` switched it on is not provable from this log, and no longer matters.
- EST-339 flashed (OTA ok, 866,752 B; size matches the 10:45 sandbox build). Boot log quiet: 91 lines in 30 s (was 4,000+), one `[gps] mode 7 sent`.
- **Process hazard found:** DS builds into the same build dir (`~/.cache/pio-om-lite/build`), so while a DS ticket runs, the `firmware.bin` there may be DS's in-progress build, not the reviewed one. From now on Claude copies the reviewed binary out right after the fenced build and flashes the copy.

## 2026-11-10 10:58 (DeepSeek): EST-340 — the mesh now runs on MeshCore's own companion code

- **Vendored, not adapted.** `lib/meshcore_companion/` now holds upstream `examples/companion_radio`
  at tag `companion-v1.18.0` (commit `81dbae5`): `MyMesh.cpp/.h`, `DataStore.cpp/.h`, `NodePrefs.h`,
  `AbstractUITask.h`. All six are byte-identical to `reference/meshcore-companion-v1.18.0/` (cmp, each
  one). `lib/meshcore` was not touched (git status shows no change; the manifest test hashes all 163
  files). The only local files in that folder are `README.md`, `LICENSE-MeshCore-MIT.txt` and
  `library.json` (PlatformIO metadata).
- **Board glue in `src/target.h` + `src/target.cpp`**, following MeshCore's own MIT
  `variants/xiao_s3_wio`: `OmBoard` (our ADS1115 battery, reboot, MCU temperature, no-op dynamic prefs),
  `OmSX1262Wrapper` (the EST-337 radio-fault mark: a failed `startSendRaw` or a >15 s TX sets Tx, a
  good `onSendFinished` clears it), `OmRTCClock` (the timekeeper; `bootstrapRTCfromContacts` during
  `begin()` never marks a real source, `CMD_SET_DEVICE_TIME` after it goes through
  `setFromSource(..., Phone, ...)`), `OmSensors` (the latest GPS fix for adverts/telemetry and the
  `gps`/`gps_interval` settings the phone can change), `radio_init`/`radio_new_identity`/the radio
  helpers, and a null serial interface because Bluetooth is OFF in this ticket.
- **`src/mesh_link.cpp` is now a facade over `the_mesh`.** Kept: the 32-message ring with the
  monotonic seq, wall time + `real` flag, hops/SNR/path, heard-repeat count, per-channel unread and
  mute, contact list/signature/path-hop names, and `mesh_link.h` exactly as the screens use it.
  Two new fields: `meshNodeName()` (NodePrefs) and `meshDmUnread()`; `meshDmCount/Name/Available` are
  real now (one thread per contact, same seq/time rules, unread counts). The composer budget takes
  the runtime name (`meshOwnTextMaxFor`); the static_asserts against `MAX_TEXT_LEN` and
  `ContactInfo::name` stay.
- **One-time migration at first boot** (`mig` in NVS namespace `mcnet`):
  - identity: old and new both use MeshCore's own `IdentityStore` at `/identity/_main.id`, so it is
    read, never rewritten. The boot log prints exactly `[mc] identity <16 hex>` (first 8 bytes) and a
    second line `same as before` / `CHANGED` when an old file was found.
  - channels: the 16-byte NVS keys go into MeshCore's 32-byte group secret (`[0..15]` copied,
    `[16..31]` zero, so `setChannel()` derives the same sha256(secret,16) hash); names and all slots
    are written to `/channels2`.
  - node name seeded `"OM Lite"`, `path_hash_mode = NVS "phs" - 1` (so phs=2 stays 2B; the out-of-range
    fallback is also 2B), both saved to `/prefs.json`.
  - `/contacts` (EST-329 format, header + CRC) is decoded and rewritten as `/contacts3` before
    `the_mesh.begin()` loads it; the old file is left in place for one release.
  - mute flags stay ours but are now keyed by channel hash (`m<hh>`); the old `m<i>` key is still
    read as a fallback and still written, so a rollback keeps them. Old NVS keys (`n/k/e/m<i>`, `phs`,
    `crashed`, `fails`) are never deleted.
- **Storage** (DataStore, SPIFFS): `/identity/_main.id` 96 B (32 pub + 64 private, unchanged),
  `/prefs.json` JSON text (a few hundred bytes; the boot log prints the exact size), `/channels2`
  68 B per slot x 8 slots = 544 B, `/contacts3` 152 B per contact, `/contacts` old 12 + 152 x n B kept,
  `/bl/<hex>` only if the advert blob cache is used. The boot log prints every file's real size.
- **Removed**: `src/contact_store.h` (the EST-329 store), `src/mc_board.h/.cpp` (replaced by
  `target.*`), `test/host/test_contact_store.cpp` (its decode moved to `src/mesh_migration.h` and
  `test/host/test_mesh_migration.cpp`).
- **Build shims needed because the sandbox has no network** (no upstream file edited; Claude decides
  if he wants real libraries):
  - `src/meshcore_common_radio_prefs.cpp` includes `lib/meshcore/helpers/CommonRadioPrefs.cpp`
    unchanged, because `lib/meshcore/library.json` (a local file we must not edit) excludes it and
    MyMesh's `NodePrefs::RadioPrefs` vtable needs `setByKey/getByKey`.
  - `src/CayenneLPP.h` is a minimal encoder for the four methods MyMesh uses (`reset`, `addVoltage`,
    `addTemperature`, `addGPS`, `getSize`, `getBuffer`); the full ElectronicCats library would need
    ArduinoJson, which is not cached.
  - `src/RTClib.h` is an empty include shim: `MyMesh.h` includes it but uses nothing from it.
- **`tools/update_meshcore.py <tag>`** clones/fetches the tag (or uses `--repo` as a local clone),
  replaces `lib/meshcore` from `src/` (keeping `library.json`), the six companion files, and
  `reference/meshcore-companion-<tag>/` from `examples/companion_radio`, writes
  `lib/MESHCORE_VERSION` (tag, commit, sha256 manifest), prints the companion_radio diff between the
  old and new tag, and never writes `src/`. `--dry-run` works offline on a local clone. Tested here:
  dry run against the local 1.15.0 clone; a real run on a throw-away copy replaced lib/ (149 files),
  left src/ byte-identical and the manifest test passed.
- **Host tests**: `test/host/run.sh` exit 0 (11 C++ binaries + `test_shot.py`'s 9 tests + the
  manifest check). New `test_mesh_migration.cpp` (identity bytes, 16->32 channel mapping, path-hash
  mode, legacy contacts decode/CRC/reserved/type/name) and `test_meshcore_manifest.py` (all 181 files
  in the three vendored folders match `lib/MESHCORE_VERSION`; proven to fail on a one-line hand edit).
- **Build** (REBUILD.md command): exit 0, SUCCESS. RAM **116,716 B** of 327,680 (35.6 %, previous
  88,988, **+27,728 B**), flash **912,361 B** of 6,553,600 (13.9 %, previous 866,385, **+45,976 B**),
  `firmware.bin` **912,720 B** (previous 866,752, **+45,968 B**). Most of the RAM is MeshCore itself:
  100 contacts instead of 32 (+~11 KB), the 32-message DM ring + 16 threads (~9 KB), the one-time
  migration buffers (~9.7 KB) and the 40 contact stamps (~1.9 KB).
- **Could not check** (no device in the sandbox): the same key on the glass (the boot line is the
  proof; compare before/after), the migration on the real NVS/SPIFFS state, the channel list arriving
  from `/channels2` on the second boot, chat both ways on air and the 2B path hash on gandalf, DM
  reception (needs another node to send one), GPS in adverts/telemetry (`advert_loc_policy` is still
  0 by default), the exact SPIFFS sizes (logged at boot, expected as above), and boot stability with
  the new global MeshCore objects (upstream requires `extern MyMesh the_mesh`; the crash guard still
  covers `meshLinkBegin`, but the global constructors now run before USB exists — a new risk, noted).
- **Next step**: Claude flashes and checks, in this order: `[mc] identity <hex>` equals the key from
  the old firmware; `[mc] migration: name "OM Lite", path hash 2B (NVS phs=2)`; the `[mc] store:`
  size lines; the Nodes list still shows the migrated contacts; one channel chat each way; the
  advert/message on gandalf is 2 bytes per hop. Then the next ticket: the DM chat screen + the
  sheet's "Direct message" row, and Bluetooth through upstream's own interface with our toggle
  (default off), including channel/radio changes from the phone.


## 2026-11-10 11:10 (Claude): EST-340 crashed on the device; rolled back
- Reviewed first: identity stays (old and upstream both use `/identity` + `_main` on ESP32 SPIFFS, DataStore.cpp), channel hashes stay (BaseChatMesh::setChannel hashes 16 bytes when the upper half is zero). Built in the sandbox (exit 0, RAM 116,716 B, flash 912,361 B), tests exit 0; binary copied out before flashing; rollback binary of aee3c5b built in the sandbox first.
- Flashed 11:02: boot stops after `[mc] 3 store + mesh begin` (USB drops); `migrate /contacts -> /contacts3: 10 contacts, 0 bytes`. Guard: fail 2/3, 3/3, mesh FAILED, parked.
- Rolled back to aee3c5b: **mesh still off**, the parked guard in NVS `mcnet` applies to every image and nothing clears it. Recovered with `docs/unpark-crash-guard.patch` (a new image resets the guard; NVS `gbuild`), built in the sandbox from a temporary worktree, flashed 11:08: mesh up, 10 contacts, Nodes list as before. The device runs aee3c5b + that patch (not committed on main; the next ticket carries the rule).
- EST-340's tree stays uncommitted; ELF saved by Claude for decoding the stored core dump (core dump to flash is on, partition at 0xFF0000).

## 2026-11-10 11:32 (DeepSeek): EST-341 — the boot crash found (one class, two layouts), crash reporter, idempotent migration, guard unpark

- **1. The crash, by reading (and proved against the saved EST-340 ELF).** `MAX_CONTACTS` was 32 in
  `mesh_link.cpp` and in `lib/meshcore/helpers/BaseChatMesh.cpp` (BaseChatMesh.h's default), but 100 in
  `MyMesh.cpp` (MyMesh.h defines it before including BaseChatMesh.h). So `BaseChatMesh` had **two
  layouts**: `sizeof(BaseChatMesh)` was 9,376 B in mesh_link.cpp and 22,160 B in MyMesh.cpp, and
  `sizeof(MyMesh)` was 15,312 B where the global `the_mesh` was created but 28,096 B where MyMesh.cpp
  writes it. In the saved EST-340 ELF the `the_mesh` symbol is 0x3bd0 = **15,312 B** (it sits 12,784 B
  short), while the surviving `MyMesh` DWARF entry says 28,096. Concretely:
  - `MyMesh::savePrefs()` is inline in MyMesh.h, so the copy used in mesh_link.cpp reads `_store` at
    offset 9,380 (`addmi a3,a2,0x2400; l32i a10,a3,164` in the old ELF). The MyMesh constructor
    (MyMesh.cpp, 100-contact layout) had written `_store` at 24,164 instead, so offset 9,380 stayed
    zero → `_store->savePrefs()` called `DataStore::savePrefs` with a **NULL `_store`** → panic
    (InstFetch/LoadProhibited). `store.saveChannels(&the_mesh)` had the same problem one step later:
    the `DataStoreHost` sub-object at the 32-contact offset has a zero vptr, so the first
    `getChannelForSave()` jumps through 0.
  - That is exactly the flash window: `[mc] 3 store + mesh begin` printed, `identity`/`migration`
    lines were lost in the USB CDC buffer when the port dropped.
  - **Fix:** `-D MAX_CONTACTS=100` in `build_flags` (build_flags reaches library TUs, build_src_flags
    would not), `MyMesh.h` included before `<helpers/BaseChatMesh.h>` in mesh_link.cpp, and a
    `static_assert(MAX_CONTACTS == 100)` there. Verified in the new ELF: `BaseChatMesh` has exactly
    one size (22,160), `MyMesh` 28,096, `the_mesh` symbol 0x6dc0 = 28,096. `test/host/test_build_flags.py`
    guards the define (and that it is not in build_src_flags).
- **2. Crash reporter** (`src/crash_report.*`, pure logic in `crash_report_logic.*`, host-tested). At
  the very start of `setup()`, after `Serial.begin` and **before `boardBegin()`**: prints
  `[crash] previous boot ended in <reason>; last checkpoint "<step>" (seq N)` when the reset reason is
  panic or a watchdog (software reset/power-on does not count), one `[crash] reset reason: N (token)`
  line (esp_reset_reason), and one `[crash] stored core dump: task <name>, PC 0x..., exc cause N (name),
  vaddr 0x..., backtrace 0x... ...` line from `esp_core_dump_image_check()`/`get_summary()`, plus the
  dump's app sha256 (so the ELF can be matched), then **erases** the dump so it prints once. No dump:
  `[crash] stored core dump: none`. A dump that checks but cannot be summarised is reported and **kept**
  (evidence, not erased). The checkpoint is an `RTC_NOINIT_ATTR` block (magic/seq/32-byte step) written
  at every `[mc] N ...` step (0 guard, 1 spiffs, 1b identity, 1c contacts, 2 radio, 3 store + mesh
  begin, 3a store.begin, 3b the_mesh.begin, 3c interface, 4 prefs, 5 channels, 5b mig gate,
  6 advert, up) and at `[om] board.begin / prefs + clock / audio / radio / mesh begin`; it survives a
  panic, a watchdog and `ESP.restart()`, and the magic rejects random RTC RAM after a power-on.
- **3. The unpark rule is in the new `mesh_link.cpp`** (the exact rule from
  `docs/unpark-crash-guard.patch`): NVS `gbuild` holds `__DATE__ " " __TIME__`; a different image
  clears `crashed`/`fails` on its first boot, so a parked guard can never outlive the image that
  crashed. A reboot of the same image keeps the guard.
- **4. Migration is now idempotent and verified.** `mig` is set **only** when all four outputs pass a
  write-and-read-back check: `/prefs.json` exists, is non-empty and parses with MeshCore's own
  `loadSerial` (into a scratch `NodePrefs`); `/channels2` is exactly `kMaxChannels` 68-byte entries
  whose name/secret match the channel table just written; `/contacts3` is a whole number of 152-byte
  records and, when this boot wrote it, byte-identical to the image that was just written (tmp file,
  `flush`, close, reopen, `memcmp` every byte, rename, reopen again). A failure logs why, leaves `mig`
  unset and retries on the next boot; a *valid* `/contacts3` is never rewritten (MeshCore owns it
  after the first migration and appends new adverts). The `mig` condition no longer marks the move
  complete before the channels are written.
  - **Why `/contacts3` reported 0 bytes:** the old code asked `File::size()` **before `flush()`/
    `close()`**; ESP-IDF's SPIFFS `stat()` does not see the stdio buffer, so it reported 0 (the writes
    themselves had returned their full sizes, which is why the "short write" branch did not fire). The
    new code never trusts `size()` before close and compares the bytes after reopening. (The file was
    openable and SPIFFS writable before `store.begin()`: `store.begin()` only creates `/bl`, so path
    and mount were never the problem.)
  - Added pure helpers in `mesh_migration.h`: `encodeContacts3()` (the whole image in one place),
    `contactsFileComplete()`, `channelsFileComplete()`; the host test round-trips the image through a
    decoder that mirrors `DataStore::loadContacts()`'s field order. That test caught a real off-by-one
    in my first draft (sync_since at 67, not 68), now fixed and pinned by hard-coded wire offsets.
- **5. The other things the ticket asked to check** (all clean, against upstream
  `reference/meshcore-companion-v1.18.0/main.cpp`): board.begin (main.cpp) → SPIFFS.begin →
  radio_init → `fast_rng.begin(radio_get_rng_seed())` → `store.begin()` → `the_mesh.begin()` →
  interface start; `sensors.begin()` is not called by upstream's mesh path and our `OmSensors::begin()`
  is a no-op. Global constructors touch no hardware and no Serial: `MyMesh` only stores references and
  does `new ArduinoMillis()` / `new StaticPoolPacketManager(16)`; `DataStore`/`OmSensors`/`OmBoard`/
  `OmRTCClock` are trivial. `loadPrefs` on a missing `/prefs.json` is guarded by `exists()` first, so
  the constructor defaults (869.618 / BW 62.5 / SF8 / CR8 / 22 dBm) stay. `MyMesh::begin()` runs in
  `setup()` on the Arduino `loopTask` (8,192 B stack): measured frames are setup 96 B, meshLinkBegin
  240 B, MyMesh::begin 144 B, DataStore::loadContacts 510 B — no stack risk. `target.cpp` returns no
  unexpected nullptr (only `OmSensors::getSettingName/Value` for out-of-range `i`, as documented).
- **Build** (`pio run -e om_lite_l2`, the REBUILD.md command): exit 0, SUCCESS. RAM **135,208 B** of
  327,680 (41.3 %, previous 116,716, **+18,492 B**: `the_mesh` +12,784 is the real fix, the
  contacts read-back image +4,864, the prefs scratch +384, reporter ~240, `.data` +220). Flash
  **919,801 B** of 6,553,600 (14.0 %, previous 912,361, **+7,440 B**), `firmware.bin` **920,160 B**
  (previous 912,720, **+7,440 B**). Only the two pre-existing RadioLib `#warning`s.
- **Tests**: `test/host/run.sh` exit 0 (12 C++ binaries + 10 python checks). New
  `test_crash_report.cpp` (reset reasons that count, checkpoint magic/truncation/seq, the two formats,
  bounded buffers), new `test_build_flags.py`, `test_mesh_migration.cpp` gained the `/contacts3` image
  round trip and the file-completeness rules.
- **Could not check** (no device in the sandbox): the caught panic itself (the stored dump should now
  print on the first boot of this image and be erased); the real `/contacts3`/`/channels2` sizes on the
  device; whether `RTC_NOINIT` keeps the checkpoint across this board's reset; the on-glass behaviour
  (not touched by this ticket).
- **Next step**: Claude flashes this image. Expect, in order: (a) a `[crash]` block whose core-dump
  line names the EST-340 fault (that is the post-mortem, decode the addresses with the saved ELF);
  (b) `[mc] new firmware image (...): crash guard reset` (the unpark rule, so the old parked guard
  cannot block it); (c) `[mc] migrate /contacts -> /contacts3: 10 contacts, 1520 bytes written and read
  back`, `[mc] migration: complete (prefs, channels and contacts read back)`; (d) `[mc] up` and the
  nodes list with the 10 contacts. On the second boot `/contacts3` must be reported `already valid`
  and `mig` must stay set.

## 2026-11-10 11:36 (Claude): EST-340 + EST-341 on the device: the new core boots
- Built in the sandbox (exit 0, RAM 135,208 B, flash 919,801 B), tests exit 0, the reviewed binary copied out and flashed (920,160 B). Boot: the stored dump printed and was erased, `new firmware image ...: crash guard reset`, `[mc] identity 564f7689850aa060` + **`identity same as before`**, `migration: name "OM Lite", path hash 2B`, `channels: 5 from NVS (saved to /channels2)`, `migration: complete (... read back)`, store sizes (/prefs.json 433 B, /channels2 544 B, /contacts3 1520 B), `[mc] up: 869.6 MHz SF8, path hash 2B`, mesh up, heap 207,784 B. Nodes list intact (`docs/shots/2026-11-10-1136-core-up.png`).
- **Post-mortem of the EST-340 crash, decoded by Claude** (dump sha `c0ce7515c7b2a618` = sha256 of the saved EST-340 ELF): `LoadProhibited` at vaddr 0 in `fs::FS::mkdir` <- `DataStore::begin()` (DataStore.cpp:62, `_fs->mkdir("/bl")`) <- `meshLinkBegin` (mesh_link.cpp:569, `store.begin()`). So the panic was in `store.begin()`, with `store._fs` == NULL, not in `savePrefs` as DS reasoned. Same root cause, different victim: the_mesh was allocated with the 32-contact size (15,312 B) while MyMesh's constructor writes the 100-contact layout (28,096 B), so it wrote ~12.8 KB past the object into the neighbouring globals, `store` among them. DS's fix (one MAX_CONTACTS for every TU + static_assert) removes the cause; the boot proves it.
- Seen: `/contacts3 already valid (1520 B, 12 in /contacts), kept`: EST-340's run had written /contacts3 with 10 contacts; the 2 contacts heard later on the rollback image are not in it. They come back with their next advert. Acceptable, recorded.
- To do on the air (Filip): chat both ways on Don't Panic, and a DM to the L2.

## 2026-11-10 12:11 (DeepSeek): EST-342 — direct messages end to end, node menu, new-node notice, Nodes scroll

Built on `d65b0b2`. Read `docs/REBUILD.md` and the end of this log first (the core is MeshCore's own
companion code). Nothing in `lib/` was touched, `.git` untouched, nothing committed.

- **1. Direct messages, display and send (the two missing halves).** Reception already worked: the
  thread existed because `storeDm()` ran. The chat screen returned early for a non-channel entry in
  `refreshChatMessages()`, and nothing called a send at all. Now:
  - A DM thread exists as soon as a DM arrives OR the user opens one from the Nodes menu; the
    selector row keeps its `thread` index in `ChatEntry`, so the open chat and the send path never
    guess from the list position. Opening a DM shows the same chat screen with the header
    `DM: <name>`, the thread's messages, the compose field and the keyboard sheet.
  - Sending goes through **upstream's own path**: `BaseChatMesh::sendMessage(contact, wireTs,
    attempt, text, expected_ack, est_timeout)`, the same calls the companion app's
    `CMD_SEND_TXT_MSG` makes. `MyMesh` computes the frame hash for the ACK; we keep it, and
    `Listener::onACKRecv()` proves delivery. The ACK also shows the companion's own `est_timeout`:
    one resend per expired attempt up to attempt 3 (the 2-bit attempt field `composeMsgPacket()`
    writes), then **not confirmed**. A MeshCore compose failure shows **not sent**; **no** of these
    sets the radio-fault mark, because an unanswered DM is not a broken radio.
  - Delivery state per sent DM, in the thread under the text: `sending`, `delivered <time>` (the
    ACK's wall-clock time, `ui/age.h`), `not confirmed`, `not sent`. The delivery machine is pure
    C++ (`ui/dm_delivery.h`) and host-tested from ACK events and timeouts.
  - Receiving through the Listener (`onMessageRecv`) stores the thread, counts unread (selector row
    and the Chat tab badge via `unreadTotal()`), and plays the existing per-event DM sound (`dm=`).
    A new receive-only serial (`meshDmRecvSerial()`) is what the chime watches, so our own send
    never rings it.
  - **Composer budget for a DM: 155 bytes.** `BaseChatMesh::composeMsgPacket()` puts a 4-byte
    timestamp and one attempt byte in front of the text, and there is no `"name: "` prefix, so the
    budget is `MAX_TEXT_LEN` (160) minus those 5 bytes (`mesh_text.h`, `static_assert`ed against
    upstream). The draft counter, the key guard and the send check all use that one number.
  - Times, not ages: DM message stamps and the ACK time use the same `ui/age` formatter as the
    rest of the chat (the age is only the fallback when the clock was not real, rule 8).
  - The own-message sheet follows the thread: `Copy text` + `Resend` (Resend sends the DM again
    through the companion's path), and `Heard repeats` is left out in a DM because a direct packet
    has no group repeats to count. Copy/Mention take the DM's 155-byte budget in a DM.
- **2. Sender-name tap and the sheet's DM row.** A tap on a sender's *name* in a channel message is
  the same as the message sheet's `Direct message` row: `chatScreenHit()` measures the drawn name
  rectangle (`chatSenderNameRect`), and only a non-mine, non-DM message can hit it. The sheet row is
  built only when the sender is a known contact **with a public key**, and it is left out inside that
  same DM thread (already open) and for our own messages. **Why a name that is not a contact gets no
  row:** the key is what `createDatagram` encrypts to; without it there is nothing to send to, so the
  row is absent rather than greyed (the same rule as the other rows).
- **3. Node menu (long press).** A 450 ms hold on a Nodes row opens a bottom sheet in the message
  sheet's style; the row behind it turns gold as the visual cue, the hold fires once, and its release
  is not a tap. A drag of more than 12 px cancels it. The rule is one tested struct
  (`ui/long_press.h`), used by the shell instead of the old inline `millis() - downMs >= 450` for this
  list. Plain tap still just selects the row. The sheet header is the name plus
  `type · last heard (time) · path`; the rows are:
  `Direct message` (clients with a key), `Info`, `Show on map` (with a position), `Ping`, `Trace`
  (when a path is known; upstream's trace packet takes an explicit path), `Reset path`, `Favourite`,
  `Remove contact` (two taps: the first renames the row), and for a repeater `Log in (RPTR)`.
  - **Info** is a full page (`ui/screen_node_info.*`): key short (8 bytes hex) and the whole key split
    over two mono lines, type, last advert as a time, position when there is one, path.
    **The SNR of the last advert is not shown: the companion's `Listener::onDiscoveredContact()`
    gets the contact and its path but not the packet, so there is no SNR to report.** The field is in
    the model for the day upstream carries it; the row is left out today.
  - **Ping** is `sendRequest(contact, REQ_TYPE_GET_STATUS)`. The reply reaches
    `Listener::onUnhandledResponse()` (we call `sendRequest` directly, so MyMesh's own
    `pending_status` stays 0) and the toast gives the measured round trip, or `no reply` after the
    companion's estimate. **Trace** is `Mesh::createTrace()` + `sendDirect()` along the contact's
    path, the same packet the companion's `CMD_SEND_TRACE_PATH` builds; `Listener::onTraceRecv()`
    gives one SNR per hop and the final SNR, drawn as the small list.
  - **Reset path** goes through `lookupContactByPubKey()` so `resetPathTo()` edits the table entry,
    not a copy (a copy would be forgotten immediately).
  - **Favourite** is kept in our own NVS namespace `omfav` (public keys, 32 max) because MeshCore's
    `ContactInfo` has a favourite *flag* but no public setter; `addContact()` allocates a new slot
    instead of replacing. Favourites sort first in `meshContactList()` (`nodeComesBefore`) and draw a
    gold star at the row's right. `Remove contact` uses `removeContact()` + `saveContacts()`.
  - **Log in (RPTR)** opens the RPTR tab with the node remembered. The tab itself is still the old
    placeholder, so it now says `node: <name>` and `Login exchange: not built yet` instead of
    pretending. **This is a gap on purpose: the repeater login/status flow is a later ticket**;
    Claude decides whether the row should stay until that tab works.
- **4. New-node notice.** One event (name + advert type) is queued when an advert really ADDS a
  contact; the shell drains it into a 3 s toast `New node: <name> (<type>)`, a small gold dot on the Nodes tab
  (drawn on the new tab bar and on the old Map/RPTR bar too) until the Nodes screen is next drawn, and
  the sound. The rules are pure (`ui/new_node.h`, host-tested): known contacts never queue, DND
  silences only the sound, toast and dot remain. The toast is drawn only, never hit-tested, so it
  cannot steal a touch outside its own rectangle. **Detection had to be our own:** MeshCore
  v1.18.0's `Listener::onDiscoveredContact(is_new)` initialises `is_new = false` and never sets it
  true on the normal add path (checked in `lib/meshcore/helpers/BaseChatMesh.cpp`; it is true only on
  the early-exit paths where the contact is NOT added). So `ui/new_node.h` carries a small 32-bit
  key-hash set (`KnownKeySet`, 128 slots, ~0.5 KB): seeded from the contact table right after
  `the_mesh.begin()`, added on a new advert and removed on Remove contact. A notice fires only when
  the key is absent from the baseline AND the contact is in the table now, so a filtered or
  table-full advert that did not add anything cannot toast. Settings > Sound gained a `New node >` row in the
  same picker as message/DM/mention; **the picker now ends with `Off` for every event** (stored as
  `kChimeCount`, so old NVS values still mean the same chime). The default new-node sound is a new
  soft two-note `Soft`, appended so no existing picker index moved. The ten settings rows now sit at
  a 19 px pitch and all fit above the tab bar.
- **5. Nodes scroll.** The drag did `g_nodeScroll0 - dy` while the rows are drawn at `y - scroll`:
  the list ran away from the finger, the EST-334 sign bug again. It now uses the shared tested
  `selDragScroll(base, dyUp, maxScroll)` (finger up moves the rows up), with the same 10 px give and
  a snap-back on release. `test_dm.cpp` drives the same helper over the Nodes layout and checks the
  row really moves with the fingertip.
- **6. One linking fix found by the build.** `the_mesh` is defined inside `mesh_link.cpp`'s anonymous
  namespace, below the DM code. A use compiled *before* that definition bound to `MyMesh.h`'s global
  `extern MyMesh the_mesh` and failed to link; the object is now declared at the top of the anonymous
  namespace with a comment, so every use is the file-local object.
- **Build** (`pio run -e om_lite_l2`, the REBUILD.md command): exit 0, SUCCESS. RAM **150,848 B** of
  327,680 (46.0 %, previous 135,208, **+15,640 B**), flash **940,321 B** of 6,553,600 (14.3 %,
  previous 919,801, **+20,520 B**), `firmware.bin` **940,688 B** (previous 920,160, **+20,528 B**).
  The first cut cost +28.5 KB of RAM: two 32-message `MeshDm` copy buffers in `main.cpp` and a
  64-byte path copied into every `MeshContact`. They are now one shared copy buffer (the two users
  never nest) and the path is formatted on demand (`meshContactPathText`), which brought it to
  +15.1 KB. Only the two pre-existing RadioLib `#warning`s.
- **Tests**: `test/host/run.sh` exit 0 (13 C++ binaries + 3 python checks). New `test_dm.cpp`:
  the DM budget (155, one byte over refused), the delivery table (matching ACK, a different ACK,
  a late duplicate ACK, one resend per timeout, attempt 0..3, not confirmed, a retry's new ACK), the
  long press (fires exactly at 450 ms once, release before is a tap, release after is not, 12 px
  cancels, 8/6 px does not), the node sheet rows for a client/repeater/room/no-key contact and the
  two-step remove label, the sheet geometry and hit rects (8 rows, top edge 34, clear of the status
  bar), the new-node rules (new/known, DND, master off, per-event Off) and toast text, the message
  sheet's DM row (key, unknown name, already in the DM, own message), the Info page's lines (short
  and whole key, no SNR row, position/path only when known), favourite ordering, the known-key
  baseline (new vs known, a removed contact counting as new again), and the Nodes drag sign. `test_chat.cpp` moved to the new `SheetContext` (key rule instead of `dmAvailable`).
- **Could not check** (no device in the sandbox): the glass. Everything below needs Filip/Claude on
  the device: a DM to and from OverMesh on gandalf (send, ACK time, the `delivered` line, an
  unanswered DM reaching `not confirmed` after the retries), the DM sound and unread dot/badge, a tap
  on a sender's name, the long press and each menu row, the Info page layout at 320 px, a ping round
  trip, a trace with two or three hops, the new-node toast/dot/sound on an advert from a third node,
  and the Nodes scroll with a full list. The Status/response path was proved by reading upstream
  (`onUnhandledResponse` is the fallthrough for an unmatched RESPONSE), not on the air.
- **Known limits, said plainly:** the DM ring is RAM-only (32 messages, like the channel ring); the
  favourite list holds 32 keys; advert SNR is not available through the companion Listener; a trace
  needs a known path, so its row is absent until one exists; the RPTR login exchange is not built;
  two contacts heard after the EST-341 migration are not in `/contacts3` and will produce a
  new-node notice on the first boot of this image, which is honest (they really are new to the
  baseline, which is seeded from `/contacts3`).
- **Next step**: Claude flashes this image and takes screenshots; Filip tests DM both ways with
  OverMesh, the node menu, a new-node advert and the Nodes scroll. Then the RPTR tab (login/status)
  and the remaining MeshCore features from REBUILD.md's work order.

## 2026-11-10 12:15 (Claude): EST-342 on the device
- Built in the sandbox (exit 0, RAM 150,848 B, flash 940,321 B), tests exit 0, reviewed binary flashed (940,688 B). Boot: identity same, mesh up, heap 191,996 B. Cosmetic: `[crash] stored core dump: unreadable (err 0x104)` on an erased partition (fix in the next ticket). DM both ways, node menu, new-node notice, Nodes scroll: Filip to test.

## 2026-11-10 12:40 (DeepSeek): EST-343 USB test commands
- **What this gives Claude:** one more framed command on the CDC port, so the device can be driven without a hand on the glass: `OMCMD1 <len>\n` + `<len>` UTF-8 bytes, answered with `[cmd] ok ...` or `[cmd] error <reason>`. The protocol is USB-only (physical access) and no command can read or print a key, a channel secret or a password; everything else is refused as `unknown`.
- **One parser, three magics.** `src/usb_frames.h` is the six-byte prefix matcher `ota.cpp` has always used, now shared by OTA, screenshot and command. The command machine is a byte-at-a-time state machine with the OTA header's 1.5 s deadlines: it never enters a blocking read while idle (the S492 lesson), and a frame that arrives in pieces keeps its place across loop passes. `OMOTA1` and `OMSHOT` behave exactly as before; the host test feeds partial and alternating magics.
- **Command language** (`src/usb_cmd_logic.cpp`, pure): double quotes group a name with spaces (`send "Don't Panic" hello`), the text is the whole remainder or one quoted word, argument errors and unknown verbs are refused before any handler runs. 300-byte frame cap.
- **The commands** (in `main.cpp`, through the real screens' functions): `send <channel> <text>` calls the same `meshLinkSend` the composer's Send key reaches, with the same `meshOwnTextMaxFor` budget; `dm <contact or pubkey prefix> <text>` opens the same thread the message sheet's Direct-message row opens and calls the same `meshDmSend`, and its answer carries the message `seq` (`[cmd] ok dm to="..." seq=N state=sending`), so the ACK can be followed with `msgs`; `status` prints mesh state, name, path hash, channel/contact/DM counts, unread total and clock source; `nodes` prints one `[node]` line per contact (name, type, first 8 hex of the public key, last heard time, hops); `msgs [n]` prints the last n channel and DM messages merged by uptime with seq, where, sender, time, delivery state and text; `tap x y` and `drag x0 y0 x1 y1 ms` inject raw samples into the loop's own touch path (the wake gate, press/drag/release, overlay dismissal and the 12 px rule all run); `key wake` injects a press/release pair through the same wake handler.
- **Answer shape:** `nodes` and `msgs` print their data lines first and then the one terminating `[cmd] ok ...`; `tools/cmd.py` prints every line until that line, then exits 0 for ok and 1 for error/timeout. Each executed command is logged once (`[cmd] > <text>`; the `>` keeps a command whose text starts with "ok" from being mistaken for the answer).
- **Sending is never retried.** `tools/cmd.py` opens the port exactly like `tools/shot.py` (DTR asserted, RTS untouched, no reset) and pads two NUL bytes to clear a stale magic, then sends the frame once: a retry could send the same message twice.
- **Crash-dump fix (from Claude's EST-342 check):** ESP-IDF 4.4's `esp_core_dump_image_check()` reads the partition's first four bytes as `data_size`; an erased partition reads 0xFFFFFFFF and the call returns `ESP_ERR_INVALID_SIZE` (0x104), not damage. `crash_report.cpp` now reads those four bytes (`coreDumpPartitionErased()`) and `classifyCoreDumpCheck()` maps `ESP_ERR_NOT_FOUND` and `INVALID_SIZE`+erased to `[crash] stored core dump: none` (printed only when a crash boot expected a dump, as before); `INVALID_CRC` (0x109) and every other failure stay `unreadable`. The pure mapping is in the host test.
- **Build** (`pio run -e om_lite_l2`, the REBUILD.md command): exit 0, SUCCESS. RAM **151,224 B** of 327,680 (46.1 %, previous 150,848, **+376 B**), flash **946,053 B** of 6,553,600 (14.4 %, previous 940,321, **+5,732 B**), `firmware.bin` **946,416 B** (previous 940,688, **+5,728 B**). The small RAM cost is the 301-byte frame buffer plus the injector state; the `msgs` merge reuses the existing DM copy buffer and stores only scalars.
- **Tests**: `test/host/run.sh` exit 0 (14 C++ binaries + 4 python checks). New `test_usb_cmd.cpp`: magic in one byte at a time, split magic, two frames back to back, junk/old magics before the magic, wrong length, zero length, header and body timeouts, NUL/newline in the body, the quoted tokenizer (including apostrophes inside `"Don't Panic"`, escapes, unbalanced quote), every command's argument forms, and the refusals (including anything with a key/secret word). New `test_cmd.py`: argv quoting, UTF-8 byte length, answer detection, a fake device proving data lines are printed and the frame is sent once, error exit, timeout exit. `test_crash_report.cpp` gained the erased/none vs CRC/unreadable mapping.
- **Could not check** (no device in the sandbox): the glass and the air. Claude needs to flash and run `tools/cmd.py status`, `nodes`, `send "Don't Panic" ...` on the air, `dm "ERA-3 (SI)" ...` and then `msgs` to watch `sending -> delivered` (or `not confirmed`), `tap`/`drag` followed by `tools/shot.py`, `key wake` (off, then on), and a normal reboot to confirm the erased-dump line is gone. Also unproven on the device: a host that stalls mid-frame (the 1.5 s timeout answer) and a command sent while the screen is off (tap/drag do nothing until `key wake`).
- **Next step**: Claude's on-air test script (status/nodes/msgs first, then send to Don't Panic and DM to ERA-3), then the RPTR tab and the remaining MeshCore features.

## 2026-11-10 12:40 (Claude): EST-343 on the device, first on-air test by USB command
- tools/cmd.py reviewed (opens like shot.py, sends once), firmware built in the sandbox (exit 0, RAM 151,224 B, flash 946,053 B), tests exit 0, reviewed binary flashed (946,416 B).
- `status`: mesh up, "OM Lite", 2B, 5 channels, 13 contacts, clock GPS. `nodes`: 13 lines; `ERA-3 (SI)` shows `hops=32` (raw encoded path length, not decoded: fix next).
- `send "Don't Panic" ...` 12:37: ERA-3 on gandalf logged it (OverMesh DB, read only): channel 2, path `c2aa`, path_hash_size 2, one hop, SNR 7.75. **2B confirmed by Claude as well.**
- `dm "ERA-3 (SI)" ...` 12:38: state `sending -> delivered` (ACK), ERA-3 logged a DM from `564f7689850a` (our identity).

## 2026-11-10 12:55 (DeepSeek): EST-344 — DM ring overwrite, toast overlay, decoded hops

Built on `5e8cdd1`. Read the end of this log first. Nothing in `lib/` was touched, `.git` untouched, nothing
committed.

- **1. The DM ring had two slot rules.** `storeDm()` picked `s_dms[s_dmSeq % 32]` and THEN set
  `m.seq = ++s_dmSeq`, so a received DM with seq N landed in slot (N-1) % 32; `meshDmSend()` and
  `meshDmList()` used slot N % 32. The reply overwrote the message before it and the list never found it —
  exactly Filip's "the DM disappears when the reply comes back". The store now lives in
  `src/ui/dm_ring.h` (plain C++, no Arduino): threads, the 32-message ring, **one** `alloc()` that commits
  `seq = newest + 1, slot = seq % ring`, `list()` (walks firstSeq..newest, reads slot seq % ring), the ACK
  update (`markDelivered`/`setState`) and the serials. `storeDm()` and `meshDmSend()` both call that one
  alloc; `MeshDm` is the ring's `DmMsg` itself, so the layout cannot drift. `mesh_link.cpp` keeps only the
  MeshCore adapter (packet fields, sendMessage, Listener).
- **2. The channel ring had the same class of bug.** `slotFor()` used a second rule (`newest-1` based) and
  the heard-repeat counter used a third (`seq % ring`), so repeats were looked up in the wrong slot and never
  counted. Both rings now use `src/seq_ring.h` (`SeqRing<N>`: `alloc()`, `slotOf()`, `firstSeq()`); channel
  stores allocate through it, `slotFor()` reads `firstSeq()+idx` through the same `slotOf()`, and the
  own-send marker uses `nextSeq()` (the sequence `alloc()` will commit) and `slotOf()`.
- **3. Toast is part of the frame.** `ui/toast.h` is the pure state (text, deadline, `visible`, one-shot
  `expire`, a newer toast replaces the older one). `omFrameEnd()` (`board.cpp`) now calls a registered
  overlay (`omFrameSetOverlay`) on the finished frame, right before the push, so every framed redraw
  (`redrawCurrentScreen`, Nodes/Info/Chat and the framed settings pages) carries it and no later repaint can
  erase it. The old screens that draw straight to the panel (`drawChannelSettings`, `drawNameEditor`,
  `drawMapTab`, `drawRptrTab`, map/RPTR settings) end with `drawDirectToast()`, the same overlay last.
  `showToast()`/the radio-fault toast now set the state and trigger one redraw; expiry (`expire()`) triggers
  the one redraw without it. New-node, ping, trace and radio-fault toasts all use this path.
- **4. One hop decoder.** `src/mesh_path.h`: `decodeOutPathLen(raw)` = hash size `(raw >> 6) + 1` (4 is
  reserved by `Packet::isValidPathLen`), hop count `raw & 63`, `0xFF` = `OUT_PATH_UNKNOWN`, and the
  count*size <= 64 check; `pathHopCount()` returns -1 for unknown, `pathHopsText()` gives "direct"/"N hops"/
  "--". The Nodes row note is "direct"/"path N hops"/"path --"; the node sheet and Info page use
  `meshContactPathText()`, now decoding the hash size **per contact** instead of dividing by our current
  preference; the `nodes` command prints `hops=1` / `hops=--` (never the raw byte); the DM sheet header
  shows "--" (not "local echo") when a DM's hops are unknown. The same decode fixed **Trace**: `sendDirect()`
  for a TRACE appends `path_len` *bytes*, so the old code sent the encoded byte (65 bytes for a 2B one-hop
  path); it now sends `count * hashSize` bytes with `hashSize-1` in the flags.
- **Build** (`pio run -e om_lite_l2`, the REBUILD.md command): exit 0, SUCCESS. RAM **151,368 B** of
  327,680 (46.2 %, previous 151,224, **+144 B**), flash **946,117 B** of 6,553,600 (14.4 %, previous
  946,053, **+64 B**), `firmware.bin` **946,480 B** (previous 946,416, **+64 B**). The ring refactor is
  size-neutral; the small cost is the overlay registration and the toast state.
- **Tests**: `test/host/run.sh` exit 0 (15 C++ binaries + 4 python checks). New `test_est344.cpp`:
  - DM ring: our send (seq 1), the reply (seq 2) must not take seq 1's slot, two more arrive, the list is
    every message in order with the right sender/mine, the other thread is untouched, ACK update by seq,
    then 60 more to **wrap** the ring: the newest 32 come back in order, seq 1 is gone, seq 64 is there.
  - `SeqRing<32>`: seq/slot rule, 32 live seqs -> 32 distinct slots, firstSeq after the wrap, reset.
  - Toast: fresh/active/expired (deadline inclusive of the "over" side), exactly one expiry event, newer
    replaces older with its own deadline, empty/zero-duration refused, clear(), and millis() wrap-safety.
  - Path: 1B (1/3 hops), 2B (1/2), 3B (2), direct 0, 0xFF unknown, reserved hash size 4, 40x2 > 64 refused,
    63x1 allowed, and the display texts.
- **Could not check** (no device in the sandbox): the glass and the air. Claude/Filip need to flash and
  check: a DM both ways and its reply staying on screen (this was the bug), the new-node pop-up staying up
  across the Nodes screen's 1 s repaints and vanishing after 3 s, the fault/ping toasts, `tools/cmd.py nodes`
  showing `hops=1` for ERA-3 (was 32), and a trace. Also unproven on hardware: the overlay on the map tab
  (direct draw) and on the keyboard sheet (framed).
- **One wording note:** the ticket's "DM header" is the message-sheet header (the open DM's `DM · hops ·
  SNR` line); the contact's own path is shown on the Nodes row, the node sheet and the Info page. If Claude
  meant a second header somewhere, it is not in this tree — say so and it is a small follow-up.
- **Next step**: Claude flashes this image and re-runs the EST-343 on-air script (`nodes`, `dm`, `msgs`), then
  the RPTR tab and the remaining MeshCore features.

## 2026-11-10 12:59 (Claude): EST-344 on the device, DM round trip proven by USB commands
- Built in the sandbox (exit 0, RAM 151,368 B, flash 946,117 B), tests exit 0, reviewed binary flashed (946,480 B). `nodes`: hops now decoded (`--` unknown, ERA-3 `0`).
- `dm "ERA-3 (SI)" ping` 12:57 -> `delivered`; the OM bot's reply arrived 12:58; `msgs` lists BOTH (seq 1 ours delivered, seq 2 ERA-3's) and `status` shows dm=1 unread=1. Driven by `tap`/`drag`: Chat tab, header dropdown, scroll, DM row: the thread shows "ping / delivered 12:57" and the PONG under it (`docs/shots/2026-11-10-1259-dm-thread.png`); the unread badge cleared on opening.
- Open: the selector's last row (the DM row) cannot be scrolled fully into view: its second line stays under the pinned "Manage channels" row (`docs/shots/2026-11-10-1259-selector-dm.png`); the max scroll does not count the pinned row. ERA-3 `hops=0` while OverMesh logged our messages over one repeater (c2aa): the stored out path may be direct after the ACK path return; not investigated. New-node pop-up: needs a real new node (Filip). The emoji placeholder box is visible in the bot's reply.

## 2026-11-10 13:06 (Claude): correction
- The 12:59 "last selector row hidden under Manage channels" was **wrong**: Filip saw no such thing, and a full drag (`drag 160 150 160 20`) shows the DM row completely (`docs/shots/2026-11-10-1305-selector-full.png`). My first drag (90 px) had simply not reached the end of the list. EST-345 must NOT change the selector max scroll for it.

## 2026-11-10 13:20 (DeepSeek): EST-345 colour emoji in messages and the emoji keyboard page

Built on `0dfb374` plus Claude's 13:06 correction. Nothing in `lib/` was touched, `.git` untouched, nothing committed.

- **1. The table.** `tools/mkemoji.py` renders Noto Color Emoji at its 109 px bitmap strike, crops to the bbox, resizes with premultiplied LANCZOS to 13x13 (the chat line is 14 px, so one px of air), and quantises to little-endian RGB565 plus 4-bit alpha (7-byte row stride, high nibble = left pixel). The filter is every single-code-point cmap entry >= U+00A9 except ZWJ, U+FE0E/U+FE0F, U+20E3, the five skin tones and the 26 regional indicators (all sequence-only; flags are two boxes by the ticket's rule). **1,416 emoji**. Each cell is **432 B** (429 used + 3 pad, 4-byte aligned), the index is 8 B per entry: **`src/ui/emoji_data.bin` = 623,056 B = 608.5 KiB**. The ticket's 1.5 MB estimate assumed about 3,700 emoji; this installed font's cmap has 1,501 code points, not 3,700. The index is sorted by code point and binary-searched. Embedded with `board_build.embed_files` and read through the flash mapping, so no RAM copy; `.gitignore` un-ignores this one `.bin`. The licence is next to it: `src/ui/emoji_data.LICENSE.txt` (Debian copyright, SIL OFL 1.1 for the font, Apache-2.0 for the rest).
  - **Why a flash blob, not SPIFFS/a partition:** the app slot is 6.5 MB and was 12.7% used; +609 KB leaves it at 24.0%. A SPIFFS file would need a partition-table change, a mount and a runtime read/CRC before the first draw. The memory-mapped blob has none, no RAM copy, and one 16-byte header to validate at most.
- **2. Drawing.** `ui/text_wrap` now walks drawable units (`textUnitAt`): FE0E/FE0F, skin tones and U+20E3 are consumed with their base; a ZWJ belongs to the part before it, so a joined sequence draws its parts in order; regional indicators stay separate, so a flag pair is two placeholder boxes. `measureUtf8`, `drawTextLeft`, `drawTextRight`, `drawTextEllipsis` and the word wrap all use the same walk. An emoji draws from the flash cell with its 4-bit coverage blended over the known surface colour (`drawEmoji`, the same bargain as `drawIcon`, no frame read) and advances **w + 1 = 14 px**; the wrap measures exactly the same width. Anything the table lacks keeps the dim box (space + 2 px). This reaches channel text, DM text, selector previews, drafts, reply bars and the message-sheet header, because they all draw through `drawText*`.
- **3. Typing.** A vector smiley key sits next to `123` on both key pages (`KeyKind::Emoji`). It replaces the four key rows (94 px, sheet height unchanged) with `emojiPadLayout()`: a rail of five group buttons (faces, hands, hearts, objects, symbols) plus `abc`; the **Recent** strip (16 cells) with **Send** at its right; and a scrolling grid of **22 px** cells, 12 columns by 3 visible rows. The grid is a curated 120-emoji "most used" set (40/24/20/18/18), every entry checked against the table by the host test; it scrolls with the same tested sign and give as the selector. A pick appends its UTF-8 bytes through `composeAppend` (the whole sequence or nothing, so the byte budget can never split it), moves it to the front of Recent, and writes Recent to NVS `omlite/erec` (16 little-endian u32 + count, 65 B). It survives a reboot; the boot log prints `emoji_rec=N` and `[emoji] table ok, 1416 cells`.
- **4. Selector last row.** Claude's 13:06 correction already withdrew the report (the first drag stopped 15 px short; `docs/shots/2026-11-10-1305-selector-full.png`). **No production change was made to the selector max scroll.** I added `testSelectorLastRowVisible()`: for every channel/DM count the app can show, at max scroll the content's last pixel lands on `selList.bottom()`, the last row's whole 30 px is above `selManage.y`, and its second line still hit-tests to that row. That locks the invariant without "fixing" a correct path.
- **5. Build** (`pio run -e om_lite_l2`, the REBUILD.md command): exit 0, SUCCESS. RAM **151,504 B** of 327,680 (46.2 %, previous 151,368, **+136 B**), flash **1,575,041 B** of 6,553,600 (24.0 %, previous 946,117, **+628,924 B**), `firmware.bin` **1,575,408 B** (previous 946,480, **+628,928 B**). Only the two pre-existing RadioLib `#warning`s.
- **Tests**: `test/host/run.sh` exit 0 (16 C++ binaries + 4 python checks). New `test_emoji.cpp` reads the shipped blob and checks the header, 1,416 strictly sorted entries, every offset in range and 4-aligned, every cell decoding with at least one visible nibble, binary search finding every entry, and ASCII/ZWJ/FE0F/skin tone/regional indicators being absent; that all 120 curated code points exist and are unique; UTF-8 2/3/4-byte and broken sequences; the sequence walker (heart + VS, thumb + skin, a three-part ZWJ family, the SI flag, a stray VS, a broken byte); width measuring (emoji 14, VS/skin free, ZWJ parts, flag two boxes, broken byte one box, text around emoji); and the Recent list (dedupe, move-to-front, 16-cap, restore drops zeros and duplicates). `test_chat.cpp` gained the selector invariant and emoji-pad geometry (12x3 grid, 16 Recent cells plus Send, rail and `abc`, per-cell hit test, scroll clamp and last cell at max), and its key rows moved to the 6-key bottom row.
- **Could not check** (no device in the sandbox): the glass. Claude/Filip need to check a message with colour emoji, the emoji page, the Recent row after a pick and after a reboot, the group rail and the grid's finger scroll, Send from the emoji page, and a received bot reply that used to show the placeholder box.
- **Exact USB test commands for Claude** (run from the firmware folder; `tools/shot.py <out.png>`):
  1. `tools/cmd.py tap 120 227` (Chat tab), `tools/cmd.py tap 160 200` (open the keyboard sheet), `tools/cmd.py tap 84 225` (the smiley key beside 123).
  2. `tools/shot.py docs/shots/2026-11-10-emoji-page.png` (rail, empty Recent strip, Send, faces grid).
  3. `tools/cmd.py tap 57 175` (first face, U+1F600), `tools/cmd.py tap 24 209` (symbols group), `tools/cmd.py tap 57 175` (first symbol, U+2705), `tools/cmd.py tap 301 152` (Send in the Recent strip).
  4. `tools/shot.py docs/shots/2026-11-10-emoji-sent.png` (the sent message on the Chat screen, in colour), then `tools/cmd.py msgs 5` (the text still has the same UTF-8 bytes).
  5. `tools/cmd.py tap 160 200`, `tools/cmd.py tap 84 225`, `tools/shot.py docs/shots/2026-11-10-emoji-recent.png` (Recent shows U+2705 then U+1F600). Reboot and the boot log must print `emoji_rec=2`.
  6. Incoming: `tools/cmd.py dm "ERA-3 (SI)" "ping 😀"`, wait for the reply, `tools/cmd.py msgs 5`. To see the thread: `tools/cmd.py tap 120 227`, `tools/cmd.py tap 100 38`, `tools/cmd.py drag 160 150 160 20 600`, `tools/cmd.py tap 100 137` (last selector row, the DM), then `tools/shot.py docs/shots/2026-11-10-emoji-dm.png`.
  7. Selector regression (already proven by Claude): `tools/cmd.py tap 120 227`, `tools/cmd.py tap 100 38`, `tools/cmd.py drag 160 150 160 20 600`, `tools/shot.py docs/shots/2026-11-10-1305-selector-full.png`.
  - The coordinates come from `emojiPadLayout()` and the host test: the rail is at x 4..44, Send is (301,152), the first grid cell is (57,175), and `abc` is (24,227). A tap one row off is a coordinate bug in the test script, not a missing hit rectangle.
- **Next step**: Claude flashes this image and runs the script above; then the remaining MeshCore features and sounds from REBUILD.md.

## 2026-11-10 13:25 (Claude): EST-345 on the device
- Built in the sandbox (exit 0, RAM 151,504 B, flash 1,575,041 B = 24 % of the slot), tests exit 0, reviewed binary flashed (1,575,408 B, OTA 31 s). `.gitignore` exception and `board_build.embed_files` read: fine.
- **Contact removal survives a reboot:** `status` contacts=1 after the flash (13 removed through the node menu at 13:07).
- `dm "ERA-3 (SI)" "ping 😀"`: delivered, the bot's "🏓PONG" arrived; the thread draws both emoji in colour (`docs/shots/2026-11-10-1325-emoji-dm.png`). Keyboard: smiley key next to 123; the emoji page shows the group rail, Send and the grid (`docs/shots/2026-11-10-1325-emoji-page.png`). Picking, Recent and the grid scroll: Filip to try.

## 2026-11-10 13:36 (DeepSeek): EST-346 toast timing/dismissal, š č ž, Latin fonts, keyboard reset

Built on `69c6881`. Nothing in `lib/` touched, `.git` untouched, nothing committed.

- **1. One toast duration, 5 s.** `theme.h` has `kToastMs = 5000` and it is the only number left: the new-node notice, the ping result, the trace toasts, the radio fault and the node-menu feedback ("Path reset", "Favourite added", "Contact removed") all go through it. `showToast(text)` no longer takes a millisecond argument, so a second duration cannot creep back in. The menu feedback used to be 2 s and the rest 2.5/3 s; they are all 5 s now, which is my reading of "one constant" - say if you want the short confirmations shorter.
- **2. Any touch dismisses a toast and is CONSUMED.** `toast.h` gained `toastDismissOnPress()`: on the loop's PRESS edge, if a toast is visible it is cleared and the whole gesture is spent exactly like the EST-339 press away from an overlay - `g_spent`, `g_drag` and `g_acted` are set, so that press's release and any drag of it do nothing, and the screen is redrawn once without the toast. Only the next new press acts. A toast that has run out by time is not visible, so its touch is free (the loop's `expire()` has already cleared it). The rule sits before the Nodes long-press arming, the chat overlay rule, the node-sheet close and the accent arming, and each of those was guarded with `!g_spent` so none of them can run on the dismissing press.
- **3. Contact removal:** nothing to do (Claude proved it after the EST-345 flash).
- **4. Slovenian letters on s, c, z.** `chat_layout.cpp` has `accentCodePoint()`: s -> š U+0161, c -> č U+010D, z -> ž U+017E, and with shift Š U+0160, Č U+010C, Ž U+017D; every other key returns 0 and is untouched. A press on one of the three keys on the LETTERS page arms the same `ui/long_press.h` helper the Nodes menu uses (`kHoldMs = 450`, the ticket said "about 400 ms"; using the shared helper was explicit, so the number is its 450). Past the threshold the key shows a bubble ABOVE it (`kAccentBubbleW/H = 26x24`, sans14 letter, gold edge) drawn by `drawAccentBubble()` from the same key rectangles the layout handed out; the plain letter is NOT typed at the threshold. The release enters the accented letter through `composeAppend` (2 UTF-8 bytes as one unit), and the release is not a tap, so the plain letter never follows. A short tap still enters the plain letter; a finger that moves more than the helper's 12 px cancels the accent and behaves as before. Because the letter is fixed at the press, a shift change under the finger cannot mix cases.
- **5. The fonts now draw them.** All four faces were regenerated with `tools/mkvlw.py --extra` for the whole of U+00A0..U+017F (Latin-1 Supplement + Latin Extended-A, 224 code points): **319 glyphs each**, sorted, records and bitmaps checked. Blob sizes: sans9 **22,760 B** (was 6,244), sans11 **28,164 B** (was 7,565), sans14 **40,044 B** (was 10,522), mono10 **23,886 B** (was 6,567): **+83,956 B of font data**, which is almost all of the build's flash increase. `fontHasGlyph()` now answers from `fontCoversCodePoint()` (fonts.h: ASCII + U+00A0..U+017F), which is the other half of the fix: before, even a face with the glyph would have drawn the dim placeholder box, because the text path only knew ASCII and U+00B7. The 1:1 proof is `docs/shots/2026-11-10-1340-fonts-latin1-1x.png`: all four faces at device pixels, "č š ž ć đ Č Š Ž ä ö ü ß" and "SI-Ivančna Gorica" all render. New `test/host/test_fonts.py` parses the four generated arrays, fails on a missing/extra/out-of-order code point, a zero bitmap for a named letter, or a blob whose length disagrees with its records.
- **6. The keyboard always opens on letters, shift off.** The four globals (`g_compose`, `g_kbNums`, `g_kbShift`, `g_kbEmoji`) became one `ui::KeyboardState` (`chat_layout.h`) with `openSheet()`, `close()`, `toggleNumbers()`, `toggleEmoji()`, `showLetters()`. Every door into the sheet - the compose field, REPLY, COPY and MENTION - calls `openSheet()`, which sets the letters page and shift off; closing keeps the page only until the next open. The draft (`g_kbText`) is untouched by any of it.
- **Build** (`pio run -e om_lite_l2`, the REBUILD.md command): exit 0, SUCCESS. RAM **151,528 B** of 327,680 (46.2 %, previous 151,504, **+24 B**), flash **1,660,249 B** of 6,553,600 (25.3 %, previous 1,575,041, **+85,208 B**), `firmware.bin` **1,660,608 B** (previous 1,575,408, **+85,200 B**). Only the two pre-existing RadioLib `#warning`s.
- **Tests**: `test/host/run.sh` exit 0 (17 C++ binaries + 5 python checks). New `test_est346.cpp`: the press model (a touch during a toast clears it and its release fires nothing, the next press acts, an expired toast leaves the touch free, `kToastMs == 5000`); `KeyboardState` open -> 123 -> close -> open gives letters and shift off, the same from the emoji page and with shift on, draft-free; the accent mapping and its exact UTF-8 bytes (C5 A1, C4 8D, C5 BE ... capitals); the shared `LongPress` (short tap = tap, hold fires once and is not a tap, 12 px cancels); and the byte budget with 2-byte letters - exactly five č in 10 bytes, the sixth refused whole, odd budgets, ščž at an exact boundary, backspace removing one whole letter, plus the real channel (151) and DM (155) budgets filled to the last whole letter.
- **Could not check** (no device in the sandbox): the glass. Claude/Filip need to flash and check: the bubble while holding a key, the letter that comes out on release (short tap too), shift capitals, the toast staying up 5 s and one touch closing it without pressing anything behind, and the keyboard reset from the 123 and emoji pages.
- **Exact USB commands for Claude** (`tools/cmd.py`, `tools/shot.py`; keyboard tap coordinates from `chatLayoutFor`: z on row 0 at (176,153), s on row 1 at (55,177), c on row 2 at (133,201), shift at (32,201), 123 at (30,225)):
  1. `tap 120 227` (Chat tab), `tap 160 200` (open the keyboard).
  2. `drag 176 153 176 153 2000` (hold z) then immediately `shot.py docs/shots/2026-11-10-est346-accent-bubble.png`: the bubble with ž above the z key. Release happens at 2 s and enters ž; `shot.py docs/shots/2026-11-10-est346-accent-typed.png` should show ž in the draft.
  3. `tap 55 177` (short tap s -> s), `tap 32 201` (shift), `drag 55 177 55 177 900` (hold s -> Š), `tap 133 201` (short tap c -> c), `drag 133 201 133 201 900` (č).
  4. Keyboard reset: `tap 30 225` (123), `tap 160 80` (above the sheet: closes), `tap 160 200` (reopen), shot: letters page, shift off. Repeat closing on the emoji page (smiley at 84,225) and reopening.
  5. Toast: `tap 120 100` (a node row), hold it with `drag 120 100 120 100 700` to open the sheet, `tap` the Ping row. While the 5 s toast shows, `tap 240 227` (Chat tab): the toast must vanish and the screen must stay on Nodes; `tap 240 227` again: Chat opens. To watch a new-node toast, a real new node is still needed (Filip).
- **Next step**: Claude flashes this image and runs the script above; then the remaining MeshCore features and sounds from REBUILD.md.

## 2026-11-10 13:40 (Claude): EST-346 on the device
- Font preview checked first (`docs/shots/2026-11-10-1340-fonts-latin1-1x.png`): č š ž ć đ Č Š Ž ä ö ü ß and "SI-Ivančna Gorica" in all four faces. Built in the sandbox (exit 0, RAM 151,528 B, flash 1,660,249 B), tests exit 0, reviewed binary flashed (1,660,608 B).
- By USB: keyboard opened, 123, closed, reopened: letters page. Long press on s, c, z then a short tap on s: the draft reads "ščžs" (`docs/shots/2026-11-10-1339-szc-keyboard.png`); the test text was deleted again. Toast 5 s + touch dismiss: Filip to see with the next real toast.

## 2026-11-10 14:05 (DeepSeek): EST-347 Settings rebuilt with tabs in the mock's style

Built on `69c6881`. Nothing in `lib/` was touched, `.git` untouched, nothing committed. Read
REBUILD.md, the end of this log, the mock's `renderSettings()` and its `.setwrap/.sect/.setrow/.lbl/.val/.chev/.check/.warn` CSS first.

**The old settings screens 0..8, and where each one went** (the first question in the ticket):

- **0 the sound list** -> Settings > General > SOUND > Sound. Same master / volume / per-event chimes /
  touch clicks / click tone / DND / welcome rows, same NVS keys.
- **1 the chime and click picker** -> Sound > Message / Direct message / Mention / New node / Click tone.
  Tap previews (the row gets the filled dot), **Use this sound** applies and saves. Use is now ABOVE
  the list: 18 rows would otherwise put it a long scroll away.
- **2 the channel list** -> Settings > General > Channels, and the Chat tab's "This chat" section.
  `Manage channels` in the chat selector opens the same page.
- **3 the name editor** -> Settings > Node > Name, and Channels > a channel > Name. The same 10x4
  letter grid and the same 19-character buffer, restyled as key boxes in the frame.
- **4 map options** -> Settings > Map tab. Recentre and Clear marker kept; the tile source is shown
  read-only (SD card / no card). "Follow my position" and "show node names" are not built, so they
  are not shown (a note says so).
- **5 repeater options** -> Settings > RPTR tab. Shows the remembered repeater and a Forget row; the
  login exchange and auto-login are still not built and the page says exactly that.
- **6 one channel's page** -> Settings > Channels > <channel>, with Name / Enabled / Sound / Unread /
  Clear / Key / Delete. Delete still needs the second tap. The Chat tab shows the same rows for the
  open channel.
- **7 the add-channel sheet** -> Settings > Channels > + Add channel: Create private, Join private,
  Join Public, Join #tag (same model calls as before).
- **8 the base64 key entry** -> Channels > Add > Join private > Channel key. Same 13x5 base64 grid,
  same `mcChannelAddFromB64` check.

**What is new**

- **Tabs, per the final shape.** General | Nodes | Chat | Map | RPTR, five equal 64 px taps under the
  26 px status bar, mono labels, the selected tab's 2 px gold underline and `kSelBg`. The gear opens
  Settings on the tab of the screen it was pressed on (`settingsTabForMainTab`: Nodes -> Nodes, Chat ->
  Chat, Map -> Map, RPTR -> RPTR); General is one tap away. Back at a root returns to that same screen.
  On the Tanmatsu the same five entries become the sidebar: `settingsLayoutFor(w, h, subPage)` takes
  the size as a parameter and the tab row is just the L2 arrangement.
- **One page = one flat list of `SettingRow`.** Section, Value, Chevron, Check, Toggle, Stepper,
  Button, Warn and Note rows at the mock's 30 px minimum (Section 20, Warn 44, Note 22). The row
  rectangle draws the row AND answers the tap; the stepper's +/- keys are rectangles inside that same
  row. Everything draws into `omFrameBegin/omFrameEnd`; the shared `settingsDragScroll` gives the
  constant 10 px of give and the release snaps back, exactly the chat list's rule.
- **Radio & mesh through NodePrefs, not a second store.** `meshRadioPrefsGet/Set`, `meshTxPowerSet`,
  `meshNodeNameSet`, `meshAdvertPosPolicyGet/Set` and `meshPubKeyShort` in `mesh_link.h/.cpp` write
  the same NodePrefs fields, call `MyMesh::savePrefs()` and then `radio_driver.setParams/setTxPower`,
  which is what the phone's CMD_SET_RADIO_PARAMS / _TX_POWER / _ADVERT_NAME / _OTHER_PARAMS handlers
  do. `ui/settings_prefs.h` is only the pure arithmetic (presets, ranges, a store with an injected
  backend); the firmware's backend is those calls. `/prefs.json` remains the only store. A missing or
  invalid radio record is seeded at boot from `LORA_FREQ/BW/SF/CR/TX` (the values the radio already
  runs), so nothing changes on the air; the boot log says `radio prefs seeded from build` only then.
  A preset never resets TX power, which has its own stepper.
- **Confirmations.** A preset (including Custom) and a path hash open a confirm page with the mock's
  warning ("A node with the wrong preset is deaf..."), Apply and Cancel. TX power is a +/- stepper and
  applies live without a confirm; it cannot make the node deaf. Say if you want a confirm there too.
- **General tab** holds Radio & mesh (preset, TX power, path hash, Node, Channels), Connections
  (Wi-Fi, Bluetooth -> pages that say "Not built yet", never a fake switch), Sound, Display
  (brightness, saved as NVS `bright`; there is no screen-timeout setting today, so no row), Time
  (now, "GPS, 2 min ago" or "not set", Set manually with +/- steppers that call
  `setFromSource(..., Manual)`), GPS (satellites, fix, position; there is no GPS on/off setting, so no
  row; Share position is `advert_loc_policy` through NodePrefs) and About (firmware date, node id,
  battery, free heap, SD card).
- **Tabs with nothing behind them say so:** Nodes ("No node list settings yet", plus the real node
  count). Chat shows "This chat: <name>" with Enabled / Sound / Rename / Key / Delete for a channel,
  then the three sound rows; for a DM it says DM settings are not built yet. Map and RPTR as above.
  The "show ages" and "text size" Chat rows were left out: neither exists in the app.
- **Old code deleted.** `main.cpp`: **905 lines removed** (the old drawing for screens 0..8, the old
  picker, the old tap branches and their state). The new row builders and action dispatch add 1,300
  lines, so the file is 3,435 -> 3,830 (+395); no coordinate is left in it. All geometry and pixels
  are in `ui/settings_layout.h` and `ui/screen_settings.cpp`.
- **New files:** `src/ui/settings_layout.h` (pure: tabs, layout, rows, scroll, steppers, civil time),
  `src/ui/settings_prefs.h` (pure: presets, ranges, store), `src/ui/screen_settings.h/.cpp` (the
  frame drawing and the hit tests), `test/host/test_settings.cpp`.

**Build** (`pio run -e om_lite_l2`, the REBUILD.md command): exit 0, SUCCESS. RAM **157,464 B** of
327,680 (48.1 %, previous 151,528, **+5,936 B**), flash **1,676,981 B** of 6,553,600 (25.6 %, previous
1,660,249, **+16,732 B**), `firmware.bin` **1,677,344 B** (previous 1,660,608, **+16,736 B**). No
warnings except the two pre-existing RadioLib ones. The RAM is almost all the 28 `SettingRow`s
(160-byte labels so the mock's warning texts are not cut); the flash is the new drawing, the row
actions and the MeshCore pref calls.

**Tests**: `test/host/run.sh` exit 0 (18 C++ binaries + 5 Python checks). New `test_settings.cpp`:
the five tabs (labels and 6 px mono advance, every label + padding inside 64 px at 320 and 160 px at
800, five taps that tile exactly, tab row under the status bar, no tab row on a sub-page, the
gear-to-tab mapping for all four screens); a 6-row page (content height, contiguous row rectangles,
the hit test at both edges, a row scrolled above the body not tappable, maxScroll, the 10 px give and
the snap); the stepper keys (inside the row, not overlapping, in minus/value/plus order, each hit);
the manual-time stepper (31/30/28/29 days, 2000 and 2024 leap, 2100 not, every minute/hour/day/month/
year wrap, 31 Jan + 1 month = 28/29 Feb, 29 Feb + 1 year = 28 Feb and + 4 years = 29 Feb); the radio
prefs (the seed IS the first SLO preset and the build plan, every range boundary accepted/refused,
frequency and bandwidth text, the store seeding once from the Build values, a refused edit changing
nothing, a preset keeping TX power, a corrupt record reseeded, a failed save reported but not losing
the RAM value, a persistence round trip through the fake backend). `test_fonts.py` now also proves the
mono face's ASCII advance is 6 and that the five tab labels fit 64 px, so the constant is checked
against the real blob, not assumed.

**Could not check** (no device and no radio in the sandbox): the glass. Claude/Filip need to see the
five tabs at 320 px, the mono labels and gold underline, the settings pages at the mock's spacing, a
3-line warning box, the letter and base64 grids, the stepper keys under a real finger, scrolling with
one finger and the snap, the back key returning to the right screen, a preset change actually moving
the SX1262 (and the phone still agreeing through `/prefs.json`), the boot seed line absent on a normal
boot, and the brightness change. The host tests prove the arithmetic and the drawing never sees a
second set of coordinates, not the panel.

**Exact USB checks for Claude** (`tools/cmd.py`, then `tools/shot.py docs/shots/<name>.png`; x = tab
centres are 32/96/160/224/288 at y 38; root back is 30,64, a sub-page back 30,39; every drag starts
inside the body):
1. **Open and the tab it came from:** on Nodes `tap 307 12` (the status gear) -> shot
   `2026-11-10-settings-nodes-tab`. `tap 30 64` -> back on Nodes. `tap 120 227` (Chat tab), `tap 307 12`
   -> shot `...-settings-chat-tab`, `tap 30 64` -> back on Chat. On Map: `tap 224 38` after the gear ->
   shot `...-settings-map-tab`; RPTR likewise with `tap 288 38`.
2. **General, radio:** from the gear `tap 32 38` -> shot `...-settings-general`. `tap 120 112`
   (Region / preset) -> shot `...-settings-presets`; `tap 120 87` (EU 868 (SLO)) -> confirm shot
   `...-settings-preset-confirm`; `tap 120 171` (Cancel), `tap 30 39` (back). `tap 120 172` (Path hash)
   -> shot `...-settings-hash`; `tap 120 117` (2B) -> confirm; Cancel; back. `tap 120 202` (Node) ->
   shot `...-settings-node`; `tap 120 87` (Name) -> name editor shot; back twice.
3. **Channels:** from General `tap 120 232` (Channels, partly visible at scroll 0) -> shot
   `...-settings-channels`; `tap 120 87` (first channel) -> shot `...-settings-channel`; back twice.
4. **Connections:** `drag 160 200 160 10 700` (190 px) then `tap 120 92` (Wi-Fi) -> shot
   `...-settings-wifi` (must read "Not built yet."); back; `tap 120 122` (Bluetooth) -> shot
   `...-settings-bt`; back.
5. **Sound:** reopen General to reset the scroll (`tap 30 64`, `tap 307 12`, `tap 32 38`), then
   `drag 160 200 160 80 700` twice (240 px), `tap 120 122` (Sound) -> shot `...-settings-sound`;
   `tap 120 147` (Message) -> shot `...-settings-sound-picker`; `tap 120 117` (first chime, previews),
   `tap 120 87` (Use this sound) -> back on Sound; shot `...-settings-sound-saved`; back.
6. **Display, Time, GPS, About:** reopen General, `drag 160 200 160 100 700` three times (300 px),
   `tap 303 112` (brightness +) -> shot `...-settings-brightness`. Reopen General, drags to 350 px
   (100 x3 then `drag 160 200 160 150 700`), `tap 120 112` (Time) -> shot `...-settings-time`;
   `tap 120 147` (Set manually) -> shot `...-settings-time-edit`; `drag 160 200 160 180 700` (20 px),
   `tap 120 217` (Set the clock) -> shot `...-settings-time-set`. Reopen General, drags to 400 px,
   `tap 120 202` (Share position) -> shot `...-settings-gps`; reopen and drag to 560 px -> shot
   `...-settings-about`.
7. **Chat tab rows:** Chat screen, gear, Chat tab: `tap 120 102` (Open channel) -> the same channel
   page; back. The sound rows are at `tap 120 252` (Message sound) after a small scroll
   (`drag 160 200 160 160 700`, 40 px) because five "This chat" rows come first.
Coords come from `settingsLayoutFor` and the row heights; a tap one row off is a test-script
coordinate, not a missing rectangle.

**Known limits, said plainly:** the Nodes list sort/hide filters, the Chat show-ages and text-size
rows, map follow-me and node names, RPTR login/auto-login, Wi-Fi/Bluetooth and the BLE PIN are not
implemented, so those rows are absent (or the page says "Not built yet"); there is no GPS on/off and
no screen-timeout setting today; the name editor keeps its old 19-character limit; TX power steps
write `/prefs.json` on each tap (one NVS write per tap, not per finger sample).

**Next step**: Claude flashes this image and runs the script above, then decides whether the empty
Nodes/Map/RPTR tabs are the right shape until their features arrive (the "rows with nothing behind
them" rule), and moves on to the remaining MeshCore features and the sounds from REBUILD.md.

## 2026-11-10 14:01 (Claude): EST-347 on the device
- Reviewed the radio path (NodePrefs + savePrefs + radio setParams/setTxPower, upstream's limits). Built in the sandbox (exit 0, RAM 157,464 B, flash 1,676,981 B), tests exit 0, reviewed binary flashed (1,677,344 B). `status`: contacts=2 (one node rediscovered since the clean-up).
- By USB: the gear from Chat opens Settings on the Chat tab ("This chat: Public": open, enabled, sound, rename, key); the General tab shows Radio & mesh (preset EU 868 (SLO), TX 22 dBm stepper, 2B, Node, 5 channels) (`docs/shots/2026-11-10-1401-settings-*.png`).
- Note for the restyle ticket: main.cpp is 3,830 lines (905 removed, the new shell added); the target remains setup + loop + dispatch.

## 2026-11-10 14:22 (DeepSeek): EST-348 chat history that survives a reboot (the card)

Built on `69c6881`. Nothing in `lib/` touched, `.git` untouched, nothing committed. Read REBUILD.md,
the end of this log, MeshCore's channel/DM paths and the board's SD mount first.

**Why the history was gone.** Both rings are RAM only: 32 channel messages (`s_msgs`, `mesh_link.cpp`)
and 32 DMs (`s_dmRing`, `ui/dm_ring.h`). A reboot and a flash empty them. They are now a *view*: every
stored message is also appended to a per-chat log on the card, and at boot the newest messages come
back into the same rings.

**1. The log and its record format** (`src/history/history_log.h`, plain C++, no Arduino)
- One append-only file per chat. SD: `/omlite/history/<chat id>.log`, rotation `<chat id>.old`.
  SPIFFS: `/h/<chat id>.log` (SPIFFS names are at most 31 bytes).
- Chat id exactly as the ticket says: `ch-<hash 2 hex>-<sanitised name>` for a channel (name bytes
  outside `[A-Za-z0-9_-]` become `_`, 14 chars max, so the whole id is 20), `dm-<pubkey[0..3] 8 hex>`
  for a DM. The DM file also carries the full 32-byte public key and the contact name in a `Chat`
  record, because the file name only has the prefix.
- Record: 10-byte little-endian header `[magic 0x4F][version 1][type][flags][payload len u16][CRC-32 u32]`
  then the payload. The CRC is over `[version..len]` + payload (CRC-32/ISO-HDLC, the polynomial the
  EST-329 contacts file already used). Max payload 288 B, max record 298 B.
- Four record types: `Chat` (identity: kind, name, key), `Msg` (logId, seq, uptime, wall, wallReal,
  ackWall, ackReal, mine, dm, delivery state, hops, SNR, path hash size/count + 64 B path, name,
  text), `Update` (logId + kind + delivery/ackWall/ackReal/repeats, 24 B) and `Meta` (seenLogId u32 +
  unread u16, 16 B).
- `logId` is the history's own monotonic id per chat. The RAM rings keep it next to their per-boot
  `seq`; update records name it. `seq` is also stored for the `msgs` dump.
- **Torn write:** the length prefix and the CRC make a half-written last record identifiable. The
  reader stops at the first record that does not validate and keeps everything before it. Before the
  next write the store repairs the tail (copies the valid prefix to `.tmp`, removes the `.log`,
  renames). One log line: `[hist] repaired a torn last record: the rest of the log is intact`.
  Proven by `test_history.cpp` (a half record after three messages: the three survive, the repair
  makes the file validate whole, the next message is id 4).

**2. Boot load, updates, paging**
- `meshHistoryBegin(sdOk)` runs in `setup()` **after** the SD mount (meshLinkBegin runs before the SD
  pins are powered; no packets are pumped in between, so nothing is missed). For every channel and
  every `dm-*.log` it reads the newest 32 messages and fills the RAM rings; a DM log recreates its
  thread (key + current contact name) and its unread count. Boot log:
  `[hist] restored N channel message(s), M DM message(s) from the card`.
- The reader walks the file **backwards in 4 KB windows** (a window start is found by validating a
  candidate magic to the known boundary), so a boot reads ~4-16 KB per chat, not the whole log. The
  store owns ONE 32-message scan ring: boot load and paging use it, no second copy.
- Update records are applied in file order. Because the walk is backwards, updates are collected as
  they are met and applied when their message comes into the window. Delivery state, ACK wall time and
  the heard-repeat counter therefore survive: `onACKRecv`, `setDmState` and `OmMeshTables::wasSeen`
  append them.
- **Seen/unread:** a received message appends `Msg` + `Meta{seen, unread+1}`; reading a chat appends
  `Meta{seen = newest logId, unread = 0}` (`meshClearUnread` / `meshDmClearUnread`). Last Meta wins on
  load, so badges and the seen point survive. Only the current `.log`'s Meta is trusted (the `.old`
  may be from before the last read).
- **Paging:** pulling past the oldest message in RAM asks for one page of 16 (a fixed 32-message
  window in `main.cpp`, so up to **32 older than the 32 in RAM = 64 per chat**, then it stops). The
  scroll is measured from the newest, so the page's newest messages appear where the finger already
  is; no jump. A paged message gets a synthetic seq (`0x80000000 | logId`) so REPLY/the message sheet
  still resolve to it. Back at scroll 0 the window is dropped.

**3. Limits and timing**
- SD: **2 MB per chat**, rotate to `.old`; **64 MB total**; at most 32 chat slots (8 channels + 16 DM
  threads + room for orphaned logs). Rotation happens BEFORE the total-cap check, so a full store can
  make room. Over the caps a record is dropped and `full` is set (`status hist_dropped=`), never a
  half write.
- SPIFFS fallback (missing/failed card): **48 KB per chat, 512 KB total**, logged once
  (`[hist] SD card absent or not mountable: history on SPIFFS ...`); a card that mounts but fails a
  write switches the whole store to SPIFFS with one line (`[hist] SD write failed: switched to the
  SPIFFS fallback`).
- Writes are **buffered**: `appendMsg` encodes into a RAM queue of 8 records (2.4 KB); the mesh loop
  flushes **one record per pass** (`history::loop()` -> `flushStep(1)`), one open + one write + flush +
  close. Every flush is timed with `micros()` and the worst is kept: `status hist_worst_us=` (and
  `hist_records=`). The SD number has to come from the device; the sandbox cannot write a card.

**4. USB**
- `history <chat> [n]` (new `Kind::History`): channel name, or DM contact name / 8-hex public-key
  prefix; n defaults to 20, clamped to 32. Prints `[hist] id= seq= up= wall= real= from= hops= snr=
  state= repeats= text=` per line, then `[cmd] ok history <count>`. Unknown chat ->
  `[cmd] error no such chat`.
- `status` now ends with `history=sd|spiffs|off hist_bytes=<all logs> hist_chats=<slots>
  hist_records=<since boot> hist_worst_us=<worst flush> hist_dropped=<refused>`.
- The parser is tested in `test_usb_cmd.cpp`: plain and quoted chat names, the optional n, and
  `history` / `history Public x` / `history Public 5 extra` refused.

**Build** (`pio run -e om_lite_l2`, the REBUILD.md command): exit 0, SUCCESS. RAM **189,488 B** of
327,680 (57.8 %, previous 157,464, **+32,024 B**), flash **1,724,589 B** of 6,553,600 (26.3 %,
previous 1,676,981, **+47,608 B**), `firmware.bin` **1,724,960 B** (previous 1,677,344, **+47,616 B**).
Only the two pre-existing RadioLib warnings. The RAM is the store (`sizeof(HistoryStore)` = 20,232 B:
the 32-message scan ring, the 4 KB window, the 8-record queue, 32 slots) plus the page window
(32 x 288 B) and 32 more `ChatMsgView`s.

**Tests**: `test/host/run.sh` exit 0 (19 C++ binaries + 5 Python checks). New `test_history.cpp`
drives the SAME `HistoryStore` over an in-memory volume:
record encode/decode round trip (all fields, UTF-8 text, path), a corrupted record refused, CRC over
header+payload; a torn last record: 3 messages survive, the file validates after the repair, the next
append is id 4; update records (delivery + ACK wall + repeats) applied on load; a `dm-*.log` restores
its key, name and unread; paging 100 messages in 16s with no gaps or duplicates; rotation to `.old`
with every id still reachable across both files, and unread/seen after rotation; unread/seen across
two reloads; the SPIFFS fallback (status flags, per-chat cap) and the total cap refusing records with
`full` set; 60 messages with five records each (the 256-offset window); chat ids. `test_usb_cmd.cpp`
gained the `history` command.

**Could not check** (no device, no card, no radio in the sandbox): the glass and the card. Claude/Filip
need to check: the boot restore line and `status history=sd`, a message still on screen after a
reboot, `history` after the reboot, the DM delivery state after the ACK and after a reboot, unread
surviving a reboot, paging with a finger (the log line `[hist] page: 16 older...`), an absent card
(`history=spiffs`), and the real worst flush time from `status hist_worst_us`. The host test proves
the format and the rules, not SD timing.

**Exact USB steps for Claude** (from the firmware folder; `tools/cmd.py`, `tools/shot.py`):
1. `tools/cmd.py status` -> `history=sd hist_bytes=...` (the card must be in).
2. `tools/cmd.py send Public "est348 before reboot"`, `tools/cmd.py msgs 3` -> the message (note its
   `seq`; `seq` is per boot, `history`'s `id` is the stable one).
3. Reboot by OTA of the SAME image (Claude's usual flash). Boot log must show `[hist] store sd, ...`
   and `[hist] restored N channel message(s), M DM message(s) from the card`.
4. `tools/cmd.py msgs 3` -> the same text is still in the ring; `tools/cmd.py history Public 5` ->
   the same message with `wall`/`real`/`id`, and the `[cmd] ok history N` line.
5. DM: `tools/cmd.py dm "ERA-3 (SI)" "est348 dm"`, wait for the PONG and the ACK, then
   `tools/cmd.py history "ERA-3 (SI)" 5` -> our message `state=delivered` with its ACK wall, then the
   reply. OTA the same image again and repeat `history "ERA-3 (SI)" 5` and `msgs 5`.
6. Unread: leave the Chat on Nodes, send a channel message from another node (or a DM), confirm
   `status unread=1`, OTA the same image, `status` must still say `unread=1`; open the chat and the
   badge clears.
7. Paging: Chat tab, drag up until the log prints `[hist] page: 16 older message(s), 16 in the
   window`, then `tools/shot.py docs/shots/2026-11-10-est348-paging.png`. Drag up twice more to reach
   32 older (the window stops there).
8. Card out: power off, remove the card, boot. The log prints
   `[hist] SD card absent or not mountable: history on SPIFFS ...`; `status` says `history=spiffs`.
   Reboot once more without the card and `history Public 5` still shows what was written there.

**Known limits, said plainly:** a renamed channel gets a NEW chat id, so its old log is orphaned (it
still counts toward 64 MB and one of the 32 slots, never loaded); a DM log is keyed by the first 4
bytes of the public key (a collision is possible but 1 in 4 billion); the paging window stops 32
messages beyond the RAM ring; a DM received with no thread (thread table full) is stored without a
chat id; the SPIFFS fallback is 512 KB total, not the card's room.

**Next step**: Claude flashes this image, runs the steps above and commits. If the on-device
`hist_worst_us` is above a few ms, the flush chunk is the knob (one record is at most 298 B).

## 2026-11-10 14:35 (Claude): EST-348 panics on the first send; rolled back to EST-347
- Built in the sandbox (exit 0, RAM 189,488 B = 57.8 %, flash 1,724,589 B), tests exit 0, flashed 14:16 (1,724,960 B). `status`: history=sd, 5 chats, 0 records.
- `send "Don't Panic" ...` crashed the device twice (USB dropped). Captured with ONE reader (`docs/est348-crash-boot.log`; a first attempt with two readers on the port shredded the log, the known two-readers trap): vfs open() errors on /sdcard/omlite/history/... ("does not exist, no permits for creation"), then a panic; next boot: last checkpoint "[om] history", core dump PC 0x4037e844, exc cause 65, app sha c6184c0ddff1dc19 = the EST-348 ELF. Decoded by Claude: a spinlock timeout in xQueueReceive inside the SD driver (sdmmc_host_wait_for_event <- sdmmc_get_status <- ff_sdmmc_card_available <- ff_sdmmc_status). The backtrace line was cut at 10 frames by the device's line limit.
- Rolled back to the EST-347 binary 14:34: send to Don't Panic OK. EST-348's tree stays uncommitted; fix ticket with the evidence.

## 2026-11-10 14:46 (DeepSeek): EST-349 the first send no longer touches the card (history is enqueue-only)

Built on `9e43002` + the uncommitted EST-348 tree. Nothing in `lib/` touched, `.git` untouched, nothing
committed. Read the end of this log, `docs/est348-crash-boot.log` and REBUILD.md first.

**1. The core dump in full.** The next boot stored 16 frames (Claude's first read cut at 10). Decoded
against the EST-348 ELF (`firmware.bin` 1,724,960 B, build 14:26:02, saved in
`paperclip-pilot/sweep/est349/est348-crash/`), frames top to bottom:

```
 1 0x4037e844 compare_and_set_native / spinlock_acquire / xPortEnterCriticalTimeout (port.c:301)
 2 0x4037d81d vPortEnterCritical -> xTaskCheckForTimeOut (tasks.c:3927)
 3 0x4037c7b1 xQueueReceive (queue.c:1472)
 4 0x4204ccf9 sdmmc_host_wait_for_event (sdmmc_host.c:537)
 5 0x4204d68d handle_event -> sdmmc_host_do_transaction (sdmmc_transaction.c:255/157)
 6 0x42099f13 sdmmc_send_cmd (sdmmc_cmd.c:23)
 7 0x42069919 sdmmc_send_cmd_send_status (sdmmc_cmd.c:374)
 8 0x42069b6d sdmmc_get_status (sdmmc_cmd.c:536)
 9 0x4205db1b ff_sdmmc_card_available (diskio_sdmmc.c:23)
10 0x4205dbf6 ff_sdmmc_status
11 0x4205d9ea ff_disk_status (diskio.c:66)
12 0x4205aa35 find_volume (ff.c:3244)
13 0x4205c76e f_stat (ff.c:4610)
14 0x4205e66b vfs_fat_stat (vfs_fat.c:597)
15 0x42057699 esp_vfs_stat (vfs.c:580)
16 0x42086fed stat (newlib sysstat.c:11)
```

So the crash is a **stat()** (a `fs_.exists()`, and the `stat` inside `VFSImpl::open`), not a read or a
write of a record. The CPU was spinning to take a FreeRTOS spinlock inside `xQueueReceive` (the SD
driver's event wait) with interrupts/scheduler already off: an SDMMC transaction ran in a context where
blocking is not allowed. The two `[E][vfs_api.cpp:105]` lines just before it are the same mistake one
step earlier.

**2. The caller chain (from our code to that stat).** `tools/cmd.py send` -> `OMCMD1` frame ->
`om::otaPoll()` (loopTask) -> `handleCommandFrame` -> `om::usbCommandExecute` (`Kind::Send`) ->
`om::meshLinkSend` -> `the_mesh.sendGroupMessage(...)` returns -> the append expression
`history::store().appendMsg(histChannelSlot(chan), histFromChannel(m), 0)`:
- `histChannelSlot` -> `HistoryStore::ensureChannel` -> `ensureSlot` (EST-348) called
  `vol_->size(".log")` and `.old` -> `fs_.open(FILE_READ)` -> `VFSImpl::open` -> `stat()`: the **two vfs
  error lines**, then `readChatHeader` -> `vol_->exists(".log")` -> `fs_.exists()` -> `VFSFileImpl` ->
  `stat()`: **the crash**. `appendMsg` would then also have called `scanTail()`/`repairTail()` (a read
  and, with a torn tail, a create/write/rename) from the same send path. Same in the MeshCore
  `wasSeen` callback: `appendUpdate -> histChannelSlot -> ensureChannel` reached the card.
- This is a *read-only `stat`* in the send path, on the same task as the loop but nested inside the
  USB frame handling and immediately after a radio TX. Neither the send nor a callback may do this.

**3. The fix (one owner, RAM first).**
- `appendMsg`, `appendUpdate`, `markSeen` are now **RAM only**: no `size`, no `exists`, no open, no scan,
  no repair. `ensureSlot` no longer looks at the volume at all. A full queue **drops** the record
  (`dropped_++`); it never calls `flushOne()` (EST-348 did that: a write from the send path).
- `HistoryStore::begin()` reads the directory **and every known log's tail once**, on the loop task in
  `setup()` (`scanAllTails`), so `maxLogId`, seen and unread are RAM truth before the first append. A
  slot created later is empty by definition (`scanDirectory` listed everything) and starts at id 1.
- `flushOne()` is the only writer. `history::loop()` moved out of `meshLinkLoop()` to the **top of
  `loop()` in main.cpp, before `otaPoll()`**: one record per pass, two when the queue is above 4. No
  code path reaches the card from a callback, an ISR or a critical section. A late scan safety net in
  `flushOne` drops a record whose id the volume already has instead of rewriting it.
- `switchToFallback()` re-lists and re-scans SPIFFS on the spot; it drops the queued records (their ids
  were assigned against the card's counters) and says so in one line.
- The queue is 16 records now (was 8): 4,832 B, in PSRAM.

**4. Directories and probes (no vfs error lines at boot).** `SdVolume::mount()` checks
`SD_MMC.totalBytes()`, then `exists("/omlite")` before `mkdir("/omlite")` (same for `/omlite/history`),
and verifies both exist; a failure prints `[hist] could not create /omlite/history on the card`. The
same for SPIFFS `/h`. `FsVolume::size()` and `openRead()` probe with `exists()` first, so a missing log
is never opened in read mode just to find out: the old `open()` line cannot appear. The first write
opens with `FILE_APPEND` ("a") and makes the file, which the Arduino FS does without an error line.

**5. Other SD users.** `rg` over `src/`: `SD_MMC` appears only in `board.cpp` (mount), `main.cpp`
(mount + the tiles.pack probe in setup), `map.cpp` (`mapBegin` and the tile/overlay reads in
`mapRender`) and `history_store.cpp`. There is no `xTaskCreate` in `src/` (only the vendored nrf52
Ethernet helper); nothing SD runs in an ISR or a timer. So every SD user is already on loopTask; what
was missing was that the history *write* also happened inside the send/USB call stack. Now history
writes happen once at the top of the pass and map reads later in the same pass, never nested.

**6. RAM and flash.** Build (`pio run -e om_lite_l2`, REBUILD.md command): exit 0, SUCCESS. RAM
**164,112 B** of 327,680 (50.1 %, previous 189,488, **-25,376 B**), flash **1,700,273 B** of 6,553,600
(25.9 %, previous 1,724,589, **-24,316 B**), `firmware.bin` **1,700,640 B** (previous 1,724,960,
**-24,320 B**). Only the two pre-existing RadioLib warnings. Moved to PSRAM: the page window (32 x 288 =
**9,216 B**, `ps_malloc` at setup, paging off with a log line if PSRAM is gone) and the store scratch
(queue 4,832 + window 4,096 + scan ring 9,216 + offsets 512 = **18,656 B**, `ps_malloc` with an internal
heap fallback). 27,872 B left .bss; static RAM fell 25,376 because the queue doubled from 8 to 16
records (+2,416) and the scratch now carries its pointers. `.bss` holds the 32 slots, the 32 extra
`ChatMsgView`s and counters only.

**Tests**: `test/host/run.sh` exit 0 (19 C++ binaries + 5 Python checks). New
`testEnqueueNeverTouchesTheVolume()`: after boot it poisons the in-memory volume (every entry point
fails and counts), then creates a new channel slot, appends a burst, an update and a seen mark, and
overflows the queue: **`vol.ops == 0`**, the overflow drops; then an unpoisoned `flushStep` writes with
`vol.ops > 0`. `FakeVolume` now counts every exists/size/open/remove/rename/list call. The existing
torn-record, paging, rotation, unread and fallback tests now flush between messages the way the loop
does (the caller no longer writes). One test bug fixed on the way: a 5-byte string literal was passed
as a 32-byte public key (g++ 14 only started to see it; now a real 32-byte array).

**Could not check** (no device, no card in the sandbox): the glass, the card and the panic itself. The
host test proves the API contract, not SD timing.

**Exact USB steps for Claude** (same image, one reader on the port):
1. Flash this `firmware.bin` (1,700,640 B). Boot log must show `[hist] page buffer 32 x 288 B in PSRAM`,
   `[hist] working memory 18656 B in PSRAM`, `[hist] store sd, ...` and `[hist] restored ...`, and **no**
   `[E][vfs_api.cpp:105]` line.
2. `tools/cmd.py status` -> `history=sd`, `hist_records=` and `hist_worst_us=`; note the counters.
3. Right after boot: `tools/cmd.py send "Don't Panic" "est349 first send"` -> `[cmd] ok send ...`, no
   reboot, `hist_records` +1 on the next `status`.
4. `tools/cmd.py dm "ERA-3 (SI)" "est349 dm ping"`, wait for the PONG/ACK, `tools/cmd.py msgs 5` ->
   the DM `sending -> delivered`.
5. Under load: send 10-15 messages back to back (`send`, `dm`), then `tools/cmd.py status` ->
   `hist_dropped=0` (or say the number) and a bounded `hist_worst_us`.
6. Reboot by OTA of the SAME image; `tools/cmd.py history "Don't Panic" 5` and
   `tools/cmd.py history "ERA-3 (SI)" 5` -> the messages with `id`, `wall`, `real`, and the DM
   `state=delivered`; `msgs 5` still shows them.
7. Paging with a finger should still print `[hist] page: 16 older message(s), 16 in the window`.

**Next step**: Claude flashes, runs the steps, and if `hist_worst_us` is above a few ms the flush chunk
(one record is at most 298 B) is the knob. If the first send still panics, the remaining suspect is a
`stat()` inside the Arduino FS `exists()` itself; then the next ticket replaces `exists()` with an
`open("a")`-only probe or `SD_MMC` at a lower frequency.

## 2026-11-10 14:58 (Claude): EST-349 on the device: history survives a reboot; the `history` USB command still panics
- Built in the sandbox (exit 0, RAM 164,112 B, flash 1,700,273 B), tests exit 0, reviewed binary flashed (1,700,640 B).
- `send "Don't Panic" ...` right after boot (captured with one reader): ok, no crash. DM ping to ERA-3: delivered + PONG. `status`: history=sd, 6 records written, worst flush 53,455 us (53 ms of loop time per record: noticeable, watch it), 0 dropped.
- Reboot by reflashing the same image: `[hist] restored 1 channel message(s), 2 DM message(s) from the card`; `msgs` lists the same three, unread=1 kept. **History survives a reboot.**
- `history "ERA-3 (SI)" 5` panics (twice): the same class as EST-348, a FatFs stat from inside the USB command handler (core dump in `docs/est349-history-cmd-crash.log`, PC 0x4037b989, sha e20b4cc90d73e57e). Both panics so far came from SD access nested inside `otaPoll()`'s command handling; the screen's own send path (enqueue only, flush at the top of loop) works. Fix next: USB commands queued by the frame handler and executed after otaPoll() returns, and find what in otaPoll() makes SD access unsafe there.

## 2026-11-10 15:15 (DeepSeek): EST-350 — Node row opens the Node page, settings chat selector, USB commands out of otaPoll
- Built in the sandbox: `pio run -e om_lite_l2` exit 0, SUCCESS. RAM **164,472 B** of 327,680 (50.2 %, previous 164,112, **+360 B**), flash **1,701,681 B** of 6,553,600 (26.0 %, previous 1,700,273, **+1,408 B**), `firmware.bin` **1,702,048 B** (previous 1,700,640, **+1,408 B**). Only the two pre-existing RadioLib warnings. `test/host/run.sh` exit 0 (19 C++ binaries + 5 Python checks).
- **1. The Node row.** General > "Node  OM Lite >" now opens the Node page (Name, Public key, Share position); renaming is that page's own Name row (`A_NodeName`; the General row is `A_Node`). Back from the name editor returns to the Node page. Back from the Node page returns to General **at the same scroll position**.
  - The old `settingsBack()` switch (one `case` per page plus the `s_setFromChat` / `s_pickerFromChat` flags) is gone. Back is now one pop of `om::ui::SettingsNav` (settings_layout.h): `setGo(page)` pushes the page it leaves with its scroll, `setBack()` pops it. That is what carries the scroll, and it also removed the two "where did I come from" flags. `SetPage` moved into settings_layout.h so the host test drives the same table (a full stack drops its oldest step; an empty one means Back leaves Settings).
  - **Every chevron row checked** (18): General: Region/preset -> Preset, Path hash -> Hash, **Node -> Node (was NameEdit, fixed)**, Channels -> Channels, Wi-Fi -> Wifi, Bluetooth -> Bt, Sound -> Sound, Time -> Time. Node: Name -> NameEdit (its own page). Chat: Open channel -> Channel, Rename -> NameEdit, Message/Direct message/Mention sound -> SoundPicker. Preset: Custom -> PresetCustom. Channels: one row per channel -> Channel. Channel: Name -> NameEdit. Sound: Message/DM/Mention/New node/Click tone -> SoundPicker. Time: Set manually -> TimeEdit. **No other row skips its page.** (Wi-Fi/Bluetooth have no rows: their page is a "Not built yet" note.)
  - One deliberate change on the way: an action's Back is now a pop, so a *custom* preset returns to the Custom page (it used to jump two levels to Preset), and the sound picker returns to the page that opened it instead of a flag.
- **2. Chat selector inside Settings.** On the Chat page the "THIS CHAT" section header is now a `SetRowKind::Selector` row: mono label, the chat's name in ink, a chevron after it (down closed, up open, gold background while open) — the Chat tab's header shape. Tapping it drops the **Chat tab's own selector** below it: one drawing (`ui/chat_selector.cpp`, moved out of screen_chat.cpp) and one hit rule (`selectorPress`), with the list, the "Channels"/"Direct messages" sections, the ages, the unread counts and the pinned "Manage channels" row. The list scrolls with the finger (`selDragScroll`, same give), a tap on a row picks, a tap anywhere else — Back, a tab, the pads — closes it and is **consumed**.
  - `g_setChat` is the chat the page shows; `g_chatSel` (the Chat tab's open chat) is only read, never written, so picking another chat in Settings leaves the Chat screen where it was. It starts as the open chat at the gear press (`openSettings`). All the Chat page's rows (Open channel, Enabled, Sound, Rename, Key, Delete) act on `g_setChat`.
  - Geometry: `settingsLayoutFor(w, h, subPage, selector)` hands out `selPanel` (body under the selector row), `selList` and `selManage`; the same three rectangles draw and answer. At 320x240 the list is 103 px, so three chat rows fit and it scrolls beyond that.
- **3. USB commands out of otaPoll().** The OMCMD1 frame handler now only copies a ready command into a one-slot buffer (`ota.cpp`); `loop()` runs it through `om::otaCommandPoll()` immediately AFTER `om::otaPoll()` returns — so after `history::loop()` at the top of the pass. A second command that arrives while the slot is full is answered `[cmd] error busy`. `history` still reads through the history store's own path (`meshHistoryDump` -> `HistoryStore::collect/readWindow`, the same calls the screen's paging makes from loop()).
  - **What made it unsafe there: the loop task's stack, measured, not guessed.** From the ELF (pre-change build): `loop()` 1,408 B + `otaPoll` 1,136 + the frame handler 304 + `usbCommandExecute` 1,632 + `meshHistoryDump` 384 + `HistoryStore::collect` 784 + `readWindow` 144 = **5,792 B before FatFs**, then `stat` 32 + `vfs_fat_stat` 368 + `f_stat` 624 + `find_volume` 64 + the disk-IO 32s + sdmmc 48/80/80/32 + `xQueueReceive` 64 (~1,490 B) — **about 7.3 KB, more with the newlib/VFS wrappers we cannot size from the objdump**, on a loop task whose stack is **8,192 B** (`cores/esp32/main.cpp`, no `SET_LOOP_TASK_STACK_SIZE` anywhere in `src/`). The same SD call from `history::loop()` is loop 1,408 + 32 + `flushOne` 352 + the FS write chain: about 3 KB. So the difference is not a lock, a critical section, the USB state or the OTA writer's cache: it is 3 KB of extra frames only that call path had. Whether *that* is what the two panics were cannot be proven without a device, so the fix removes the depth AND makes it visible: `[hb]` now prints `stack=` and `status` ends with `loop_stack_free=` (FreeRTOS high-water mark, bytes free since boot). After the change the command path is `loop` 1,408 + `otaCommandPoll` 304 + `usbCommandExecute` 1,648 + 384 + 784 + 144 = **4,672 B** (the handler's frame is 32 B, and otaPoll's 1,136 is off the stack), i.e. 1,120 B more headroom.
- **Tests**: `test/host/run.sh` exit 0. New in `test_settings.cpp`: `testNav()` — General at scroll 40 > Node > Name > Back > Back lands on General at **40**, per-step scroll, overflow keeps the newest step, an empty history is Back-leaving-Settings, tab `reset()`; `testSettingsSelector()` — the dropdown's three rectangles under the selector row, the clamp is the Chat content height over the window, a row picks (channel 0 and the DM entry 2 after a scroll), a section label / the pinned row / a tap outside / Back all return Close+spent, `selectorPickShown` writes the shown chat while the Chat tab's open chat stays 0, and the drag follows the finger with `selDragScroll`. `run.sh` now links `chat_layout.cpp` for BIN18 (the selector arithmetic is the chat's own).
- **Exact tools/cmd.py steps for Claude** (one reader on the port, `OM_PORT=/dev/ttyACM0`). Coordinates
  are the layout's own numbers at 320x240: status bar 26, tab row 26..51, title row 51..77 on a root
  (26..52 on a sub-page), body from 77; the gear is `(307, 12)`, the Back key `(30, 64)`, the Settings
  tabs `(32, 38)` General / `(160, 38)` Chat, the General > Node row `(160, 202)`, the Node page's Name
  row `(160, 87)`, the Chat page's "THIS CHAT" row `(160, 92)` and its dropdown's second channel row
  `(160, 167)`.
  1. Flash `firmware.bin` (1,702,048 B). Boot: the usual `[hist] ...` lines and `[hb] ... stack=NNNN`;
     note that number.
  2. `tools/cmd.py status` -> ends with `loop_stack_free=NNNN`. Note it.
  3. `tools/cmd.py history "ERA-3 (SI)" 5` and `tools/cmd.py history "Don't Panic" 5` -> the data lines
     then `[cmd] ok history N chat="..."`, **no reboot**, and `tools/cmd.py status` again: `loop_stack_free`
     must not have dropped by more than a few hundred bytes (that is the check for the stack theory).
  4. `tools/cmd.py send "Don't Panic" "est350 send"` -> `[cmd] ok send ...`; `status` -> `hist_records` +1,
     `hist_dropped=0`.
  5. Node page: `tools/cmd.py tap 307 12` (gear) -> Settings; `tap 32 38` (General tab); `tap 160 202`
     -> the **Node page** (Name / Public key / Share position), not the letter grid;
     `tools/cmd.py shot.py` if wanted; `tap 160 87` (Name) -> the letter grid; `tap 30 64` (Back) ->
     the Node page; `tap 30 64` (Back) -> General at the scroll it had (scroll down first with
     `tools/cmd.py drag 160 200 160 140 300` to see the scroll kept).
  6. Selector: from the Chat tab `tap 307 12` (gear, opens Settings on the Chat tab) or `tap 160 38`
     (Chat tab); `tap 160 92` -> the dropdown (name + chevron, "Channels"/"Direct messages", the pinned
     "Manage channels"); `tools/cmd.py drag 160 190 160 140 300` -> the list follows the finger;
     `tap 160 167` (the second channel) -> it becomes the "THIS CHAT" row and the rows below are that
     channel's, still inside Settings; `tap 160 250` (outside, over the tab bar) -> closes and the next
     tap acts normally; `tap 30 64` (Back) out of Settings and `tools/shot.py` -> the **Chat screen still
     shows the chat that was open before** the gear.
- **Could not check** (sandbox: no device, no card): the glass, the dropdown's pixels, the real stack high-water mark, and whether `history` now survives on the card. The host test proves the arithmetic and the hit/consume rules, not timings.

## 2026-11-10 15:18 (Claude): EST-350 on the device
- Built in the sandbox (exit 0, RAM 164,472 B, flash 1,701,681 B), tests exit 0, reviewed binary flashed (1,702,048 B).
- `history "ERA-3 (SI)" 5` and `history "Don't Panic" 5`: answered, no panic (the stored log includes Filip's "HISTORY TEST 15.15" from ERA-3). DS's cause (loop-task stack: ~7.3 KB of the 8 KB used by the nested command + FatFs chain) fits both panics; the command now runs after otaPoll() returns.
- Settings: General > Node opens the Node page (Name / Public key / Share position) (`docs/shots/2026-11-10-1517-settings-node.png`); the Chat tab's "THIS CHAT" row opens the chat dropdown inside Settings (`docs/shots/2026-11-10-1518-settings-chat-picker.png`). Seen: the node-name editor is still the old upper-case grid, not the chat keyboard (restyle ticket).

## 2026-11-10 15:40 (DeepSeek): EST-351 the map and RPTR move to the new layer; the loop stack is 16 KB

Built on `7665823`. Nothing in `lib/` touched, `.git` untouched, nothing committed. Read REBUILD.md,
the end of this log, `ui-mock/index.html` (the L2 Map and RPTR screens, the modal/sheet/toast styles)
and the Nodes/Settings screens first. **This run is deliberately partial**: the map, the RPTR tab and
the loop stack are done; the two old text grids in Settings (name editor, channel key) are not, and
the audit says so. The build is clean and the host tests pass.

**1. Map: the mock's chrome over the same tiles.**
- `src/ui/map_chrome.{h,cpp}` is pure C++: `mapChromeFor(L, zoom, lat, selected)` hands out the chip,
  the two zoom keys, the lock key, the scale rail and the callout; `mapHitAt()` answers with the same
  rectangles. `mapMetresPerPixel()` is Web-Mercator `156543.03392 * cos(lat) / 2^z`; the scale takes
  the largest 1-2-5 distance whose bar fits 92 px, and its labels are the half and the full distance
  ("500m" / "1km"), not ages.
- `src/ui/screen_map.{h,cpp}` draws one frame: `om::mapRender()` (tiles, SD reads, pan and the
  attribution, unchanged), contact markers coloured by MeshCore type (client blue, repeater green,
  room amber, sensor dim), our own fix as the white dot with the gold halo, the "offline · z13" chip,
  `+` / `-` / lock bottom-right, the scale bottom-left and the tapped marker's callout (dark gold,
  gold edge, name plus one mono line of type, path and time). It ends with the status bar and tab
  bar; the toast is the frame overlay.
- `map.cpp` no longer draws any chrome: the in-canvas zoom buttons, the debug state line and the old
  marker dot are gone; it gained `mapProject(lat, lon, x, y)` for the chrome. The tile loader, the
  PSRAM cache and the z18 2x path are untouched. The no-pack case is a normal empty state.
- The map pans with the finger as before; a drag drops the follow lock and closes the callout. Lock
  recentres on the GPS fix on every draw. Tapping a marker opens its callout; tapping empty map
  closes it. "On map" from a message or a node opens the map at that contact with its callout already
  selected.

**2. RPTR: the mock's list and repeater page, honest about the rest.**
- `src/ui/rptr_layout.{h,cpp}` (pure) owns the header, the Scan chip, the 34 px rows, the list scroll
  clamp and the detail page's back key, status card, login field/button and note; `rptrHitAt()` is the
  one hit test. `src/ui/screen_rptr.{h,cpp}` draws the list (green dot, name, path and SNR) and the
  page (back, name, "repeater · path · time", a five-line status card, the password field and Login).
- The list is the real contact table filtered to `ADV_TYPE_REPEATER`; the status card shows what the
  mesh knows today (contact, path, last heard time, hops, public-key prefix). Scan and Login say
  "not built yet" in a 5 s toast; the note says the remote admin set (overview, radio, regions,
  neighbours, actions) is not built. "Admin" from a repeater's Nodes menu opens its page directly.
  The list scrolls with the finger and snaps back like every other list.

**3. Loop task stack (ticket addition b).** `main.cpp` calls `SET_LOOP_TASK_STACK_SIZE(16384)` at
file scope (Arduino-ESP32's own macro; a `-D` flag collides with Arduino.h's function-like
definition). `status` now prints `loop_stack_free=<high-water mark> loop_stack_size=16384`; the
`[hb]` line already printed the mark. `test_build_flags.py` fails if the macro is missing/duplicated,
if it moves back into a `-D` flag, or if `status` loses either field.

**Build** (`pio run -e om_lite_l2`, the REBUILD.md command): exit 0, SUCCESS. RAM **170,384 B** of
327,680 (52.0 %, previous 164,472, **+5,912 B**), flash **1,705,169 B** of 6,553,600 (26.0 %,
previous 1,701,681, **+3,488 B**), `firmware.bin` **1,705,536 B** (previous 1,702,048, **+3,488 B**).
Only the two pre-existing RadioLib warnings. The RAM is the map/RPTR view models (32 markers, 32
repeater rows) plus the new drawing code; flash is the two new screens and their pure layout units.

**Tests**: `test/host/run.sh` exit 0 (**20 C++ binaries + 5 Python checks**). New
`test_est351.cpp`: the Mercator scale (one zoom step halves it, cos(lat) at 46 degrees, a 1-2-5
distance that fits 92 px, the 500m/1km and 250m/500m label pairs); the chrome rectangles (the map
above the tab bar, the chip under the status bar, the three keys right-aligned and non-overlapping in
lock/+/- order, the scale at the bottom left, the callout 8 px in from both edges); the hit test (each
key, the callout, empty map, the status bar answering None, a marker winning over a key, the 11 px
marker radius); the RPTR list (header/list split, 34 px rows, visible-part row hits under scroll, the
scroll clamp) and the detail page (back, card, login field and button, note inside the screen).
`test_build_flags.py` gained the loop-stack checks.

**Audit (the ticket's target 0, measured on this tree).**
- Calls in `main.cpp` that draw straight to the panel: **0**. `rg "fillRect|drawString|fillScreen|
  drawFastHLine" src/main.cpp` returns nothing; `om_display()` in `main.cpp` is only passed to
  `useAppFont` at boot. The old `drawTabs`, `drawStatus`, `drawTopBar`, `drawInfoPanel`,
  `drawDirectToast` and their `kGold/kDim/kBg/kTabY/kTabW/kTabs` are deleted, together with the old
  map/rptr tap branches that repeated those numbers.
- Hard-coded colours outside `src/ui/theme.h`: **1**, and it is allowed: `board.cpp:259`
  `s_frame.fillScreen(0x0000)` in the frame probe. No `TFT_*`, no RGB565 literal, no named local
  palette in any screen (`rg` for `TFT_|0xF800|0x2945|0x0000` finds only that probe; the other
  0xFFFFFFFF hits are CRC sentinels, not colours). `map.cpp`'s old `kGold`/`kCredit` are now
  `theme.h`'s `kAcc`/`kMapCredit`.
- Screens that draw without `omFrameBegin()`: **0**. `map.cpp` and `ui/chat_selector.cpp` contain
  drawing calls but are only ever called from a screen that already opened the frame; every other
  drawing file opens its own. The only push to the panel is `board.cpp`'s `omFrameEnd()` (allowed).
- **Boot screen:** there was none to restyle. `boardBegin()` lights the panel and the first frame
  drawn is the new Nodes screen; the old top bar was deleted with the rest.
- **`main.cpp` is 4,165 lines** (previous 4,084, +81 net). Target setup/loop/dispatch is **not met**:
  the file still holds the app state, the settings row builders, the USB command shell and the new
  map/RPTR view models. Drawing and every coordinate are out of it (0 draw calls), but the data
  building is not. The next cut is `buildSettingsRows` (375 lines) and the USB command cases.

**What is left from this ticket, plainly.**
- **Ticket addition (a) is NOT done**: the node-name editor and the channel-key entry still draw the
  old upper-case 4x10 / 5x13 grids in `ui/screen_settings.cpp`. They need the chat keyboard sheet
  (letters, 123, the s/c/z long press) with their own allowed characters; that means a shared
  keyboard-sheet widget or exposing `drawSheet` from `screen_chat.cpp`, plus its own host tests. This
  is the one screen family still in the old style.
- Every overlay the ticket lists except those editors is already on the new layer from EST-336/344/
  347: the message menu and INFO panel (`message_sheet.cpp`, `screen_node_info.cpp`), toasts as frame
  overlays for 5 s, the confirm pages and the add-channel sheet (`screen_settings.cpp`). They were
  reviewed, not rewritten.
- Map features the mock draws but the mesh cannot answer yet are absent rather than faked: traces,
  the MSGS rail, and "share position" from the map. The lock only follows our own GPS fix.
- RPTR login, Scan and the remote admin set are "not built yet"; the page and list exist and work.

**Exact `tools/cmd.py` steps for Claude** (one reader on the port; `OM_PORT=/dev/ttyACM0`;
`tools/shot.py docs/shots/<name>.png` after each step):
1. Flash `firmware.bin` (1,705,536 B). `tools/cmd.py status` -> ends with
   `loop_stack_free=NNNN loop_stack_size=16384`; note both.
2. **Map chrome:** `tap 160 227` (Map tab) -> shot `2026-11-10-est351-map`. The top chip must read
   `offline · z<zoom>`, the scale bottom-left must show two labels, and `+`/`-`/lock are bottom-right.
3. **Zoom:** `tap 302 169` (`+`) -> shot `...-map-zoom-in`; `tap 302 197` (`-`) -> shot
   `...-map-zoom-out`. `status` is unchanged; the serial prints `[map] zoom N`.
4. **Pan:** `drag 160 100 160 150 400` -> shot `...-map-panned`; the callout must be closed and the
   lock off. `tap 302 141` (lock) with a GPS fix -> shot `...-map-lock`; the map recentres and the
   lock key is gold.
5. **Node callout:** Nodes tab, long-press a contact (`tools/cmd.py` has no long-press command today;
   use the Nodes menu's "On map" from the glass or send a message and use the message sheet's "On
   map"). The map opens centred on that contact; `tap 160 107` (its marker, which is at the view
   centre) -> shot `...-map-callout`. The panel names the node, its type, path and time.
6. **RPTR list:** `tap 280 227` (RPTR tab) -> shot `...-rptr-list`. The header says
   `REPEATERS IN RANGE` with a Scan chip; every repeater contact is a row with path and SNR.
   `tap 283 39` (Scan) -> a `Scan: not built yet` toast, no fake scan.
7. **RPTR page:** `tap 160 69` (first row) -> shot `...-rptr-page`: back, name, `repeater · path ·
   time`, the five-line status card, the password field and Login. `tap 274 180` (Login) -> a
   `Login: not built yet` toast. `tap 40 39` (back) -> the list. A list longer than the body:
   `drag 160 180 160 80 400` then `tap 160 69` opens the right row, and the release snaps back.
8. **Loop stack:** after the RPTR and map steps, `tools/cmd.py status` again. `loop_stack_size=16384`
   and `loop_stack_free` must stay well above the EST-350 number (the point of the change).
9. **Old grids (known gap):** Settings > General > Node > Name and Settings > Channels > a channel >
   Key still show the old grids. Do not shoot them as new; the next ticket replaces them.
10. **Toasts:** any map/RPTR toast lasts 5 s; a touch during it dismisses it and is spent (the next
    press acts). The map's callout is a frame overlay like the toast, so a redraw does not wipe it.

**Could not check** (no device, no card, no radio in the sandbox): the glass, the real marker
positions, the scale's readability over a busy tile, the callout's fit with a long contact name, the
loop task's real high-water mark on the device, and the map still panning on the same tiles. The host
tests prove the rectangles and the arithmetic, not the panel.

## 2026-11-10 15:35 (Claude): EST-351 on the device
- Built in the sandbox (exit 0, RAM 170,384 B, flash 1,705,169 B), tests exit 0, reviewed binary flashed (1,705,536 B). `status`: loop_stack_size=16384, loop_stack_free=11160.
- Map (`docs/shots/2026-11-10-1534-map-colours.png`): the new chrome is there (offline · z13 chip, locate / + / - bottom-right, scale bottom-left), but **the tile colours are wrong** (magenta/cyan noise on the topo map): most likely the decoded RGB565 byte order does not match the off-screen frame now that the map draws into it (the frame stores byte-swapped pixels, EST-? "frame probe 89ed -> push swap ON"). The scale's "500" and "1km" overlap.
- RPTR (`docs/shots/2026-11-10-1534-rptr.png`): "REPEATERS IN RANGE" + Scan, one row (SI-KP-Slavnik), second line "--".
- Not done (DS said so): the name/key editors still use the old grids; main.cpp 4,165 lines.

## 2026-11-10 16:05 (DS): EST-352, finishing EST-351

Built in the sandbox with the REBUILD.md command, **exit 0**: RAM 170,416 B of 327,680 (52.0%), flash 1,708,345 B of 6,553,600 (26.1%), `firmware.bin` 1,708,704 B. `test/host/run.sh` **exit 0** (all suites; the EST-351 binary now also proves the RGB565 round trip, the two scale labels, the keyboard filters/limits/geometry and the RPTR row line).

**1. Map colours (the magenta/cyan tiles).** Claude's read was right, and the exact mechanism was in LovyanGFX: `pushImage(uint16_t*)` takes the SOURCE byte order from the surface's swap flag, not the target's. `s_canvas` holds native RGB565 words but the frame sprite's flag defaults to OFF, so the sprite memcpy'd the native words while it stores `rgb565_2Byte`; the panel then showed every word byte-swapped. Fix, one rule: `board.cpp`'s new `omPushRgb565()` sets the surface's swap flag for the push (source = native words) and restores it, and `map.cpp` pushes every 16-row slice through it. Emoji and icons already draw with `drawPixel()`, which goes through the surface's colour conversion and was never affected; the self-test now covers the image path explicitly.
- `src/ui/rgb565.h` (new, plain C++): the one conversion. `rgb565FromRgb`, `rgb565Swab`, `rgb565FrameStore`, `rgb565FrameRead`. `omFramePixel()` now uses `rgb565FrameRead`, so the screenshot path and the push share the same functions.
- **Boot self-test:** the existing gold frame probe is followed by `[om] colour push probe gold ed89 -> stored 89ed -> back ed89 OK|FAILED`; `mapBegin()` then decodes the centre tile and runs the same round trip on one byte-asymmetric pixel, logging `[map] tile pixel probe z13 <tx>/<ty> at <x>,<y> jpeg XXXX -> stored YYYY -> back XXXX OK|FAILED` (the decoded value IS the JPEG colour). Both lines are FAILED, not a colour that "looks odd", if the order ever drifts again. The frame pixel used by the probe is restored, so the test leaves no mark.
- Host proof: `0xED89` (theme gold) through store + read-back is `0xED89`; a broken push (stored native) is detected because the read-back would be `0x89ED`; all 65,536 words round trip.

**2. Scale labels.** `mapScaleLabelLayout()` (ui/map_chrome) now places the half label at the bar's LEFT end and the end label right-aligned at its RIGHT end, with the gap measured with the same mono face (one space width). If `mid width + gap + right width` does not fit the rail, the half label is dropped and only the end label is drawn. The screen measures both labels and the space with `measureText`, then uses the one returned rectangle pair. At z13 (bar 75 px) that is `500m` at x 6 and `1km` ending at x 81, 27 px apart.

**3. The editors now ARE the chat keyboard sheet.** The old 4x10 and 5x13 grids and their five rectangles per editor are deleted; `screen_settings.cpp` has no key coordinate at all.
- The sheet's geometry is `chatLayoutFor(l2(), true, false, nums, false, field)` and its keys answer with `keyAt(..., field)`: the same rectangles the chat uses. `drawKeyCap()` (ui/keyboard_draw.h, implemented in ui/widgets.cpp) is the one key drawing, moved out of screen_chat.cpp; the accent bubble is one shared `drawAccentBubble()` too.
- One `KeyboardField` per editor (`chat_layout.h`): `NodeName` (also used for channel names) and `Base64Key`. The chat is `Chat`. The field filters the character keys, sets the byte limit and says whether the accent hold and the emoji page belong to it.
- **Node name:** MeshCore's own rule (`AdvertDataParser::isValidName`): printable ASCII except `[ ] \ : , ? *`, 31 bytes (its `node_name[32]` / `ChannelDetails::name[32]`). Space and dot stay, comma is dropped; the s/c/z long press enters š/č/ž (2 bytes, counted against the same 31).
- **Channel key:** base64 only, 24 chars (16-byte key + `==`). The 123 page carries `+ / =` (rows 1 and 3); a space, comma or dot has no key at all. The `Send` key is relabelled with the page's `okLabel` (`OK`, `Join`), Del removes a whole code point, and a `used/max` counter sits at the draft line's right.
- Host tests: the exact allowed/forbidden character sets, the 31/24 byte limits (including a 2-byte accent at the edge), every drawn key is inside its field's set, the 123 page's `+/=`, the emoji page absent from both editors, every row non-empty and tiling the sheet width exactly, each key's centre hits itself, and the chat's own row counts unchanged.

**4. RPTR rows.** The lone `"--"` was the **PATH** (MeshCore's encoded `out_path_len`; `meshContactPathText()` prints `--` when its hash size cannot be decoded), not the last heard and not the hops. `rptrRowMeta()` (ui/rptr_layout) now builds the line only from facts that exist, each labelled, in usefulness order: `heard 14:02 · SNR 7.5 dB · path a1 > b2` (times come from the one `stampText` formatter; unknown `--` values are dropped). Nothing known means an EMPTY string, and `screen_rptr.cpp` then draws no second line at all. The detail page's one mono line uses the same builder, so it has no bare dash either.

**Exact `tools/cmd.py` steps for Claude** (one reader on the port; `OM_PORT=/dev/ttyACM0`; `tools/shot.py docs/shots/<name>.png` after each step):
1. Flash `firmware.bin` (1,708,704 B). Boot log must show `[om] colour push probe gold ed89 -> stored 89ed -> back ed89 OK` and `[map] tile pixel probe z13 ... -> ... OK` (the map line needs the card in). `tools/cmd.py status` -> note `loop_stack_size` and `loop_stack_free`.
2. **Map colours:** `tap 160 227` (Map tab) -> shot `2026-11-10-est352-map`. The topo must be muted beige/green/blue, **no magenta/cyan speckle**; chip `offline · z13`; scale bottom-left reads `500m` then `1km` at the two ends with a clear gap (not `5001km`).
3. **Zoom/scale:** `tap 302 169` (`+`) -> shot `...-est352-map-zoom`; `tap 302 197` (`-`) -> shot `...-est352-map-zoom-out`; the two labels must stay at the ends and never touch at any zoom.
4. **Node name editor (shared sheet):** `tap 307 12` (gear) -> `tap 32 38` (General tab) -> `tap 160 202` (Node) -> `tap 160 87` (Name) -> shot `...-est352-name-sheet`. Sheet: draft line 121..139, row centres y 153/177/201/225. `tap 18 153` (q), `tap 89 177` (d), `tap 215 201` (n): the draft shows `qdn` and `3/31`. `tap 32 201` (shift), `tap 54 177` (S) -> `QdnS`; `tap 39 225` (123) -> the digits page; `tap 18 153` (1); `tap 39 225` (abc) back. Then `tap 280 225` (OK) -> `Name saved` toast and the Node page. The old 4x10 grid must not exist. (Long press `s` at 54,177 for `š` has no `cmd.py` verb; try it by hand on the glass.)
5. **Key entry (base64 sheet):** from General `tap 120 232` (Channels) -> `tap 120 232` (+ Add channel, at the bottom of the 5-channel list) -> `tap 120 117` (Join private) -> the same sheet with title `Channel name`: type a name (e.g. `tap 89 177`, `tap 269 153`, `tap 215 201`, then `tap 280 225` = OK) -> the **Channel key** sheet. Letters page has no space/comma/dot key; `tap 39 225` (123) -> numbers page: digits row 0 (centres 18..301, y 153), `+` 55,177, `/` 159,177, `=` 264,177; `tap 270 225` (`Join`) with a short key -> `That key is not a 16-byte base64 key` toast. Shot `...-est352-key-sheet` (letters) and `...-est352-key-123`.
6. **RPTR:** `tap 280 227` (RPTR tab) -> shot `...-est352-rptr`. The second line must never be a lone `--`: with nothing stored it is absent; with a known path/time it reads `heard HH:MM · SNR x dB · path ...`. The detail page (`tap 160 69`) must show the same facts, no bare `--` in its mono line.
7. **Loop stack:** `tools/cmd.py status` after the steps. The editor is the chat's sheet (drawing only, no new allocation), so `loop_stack_free` must stay at the EST-350 level and `loop_stack_size=16384` unchanged.

**Could not check** (no device, no card): the tile colours and the scale's readability on the glass, the key rectangles under a real finger, the accent long press (no hold verb in `cmd.py`), the boot self-test on a real frame (the host proves the same arithmetic), the 16-byte-key success path (a valid key takes 24 taps; the short-key toast proves the page and the filter), and the loop stack after the editor. The host tests prove the filters, the limits, the rectangles and the conversion, not the panel.

**Next step:** flash and shoot the four screens above; if the map is still wrong, the two `probe` lines in the boot log now say whether the push or the frame order is at fault.


## 2026-11-10 16:00 (Claude): EST-352 on the device
- Built in the sandbox (exit 0, RAM 170,416 B, flash 1,708,345 B), tests exit 0, reviewed binary flashed (1,708,704 B). Boot: `colour push probe gold ed89 -> stored 89ed -> back ed89 OK`.
- **Map colours fixed** (`docs/shots/2026-11-10-1559-map-fixed.png`): proper topo colours. New small overlap: the scale labels ("500m", "1km") run into the map attribution text ("E, Garmin, OSM") at the bottom left.
- Node-name editor is the chat keyboard with a 7/31 counter and OK (`docs/shots/2026-11-10-1559-name-editor.png`); left without saving, name unchanged.

## 2026-11-10 16:10 (DS): EST-353, the diagonal on leaving the map, and the attribution

Built in the sandbox with the REBUILD.md command, **exit 0**: RAM 170,424 B of 327,680 (52.0%),
flash 1,708,785 B of 6,553,600 (26.1%), `firmware.bin` 1,709,152 B. `test/host/run.sh` **exit 0**
(all suites, including the new credit-placement test).

**1. Why the diagonal is there, and why "push faster" cannot remove it.** The push was the only
full-frame push on every tab (checked below), so the question was the push itself, not the screens.
- **The mechanism is the rotation, not the duration.** `board.cpp` configures the NV3031B with
  `panel_width 240 x panel_height 320` and `offset_rotation 1`; LovyanGFX's `update_madctl()` for
  rotation 1 sends `MADCTL = MY | MV` (read from `Panel_NV3031B.cpp` in libdeps). `MV` exchanges the
  two axes, so our frame is written into the panel's memory **rotated**: consecutive pixels of one
  landscape row advance along the panel's native Y (down a column), while the panel's gate scan
  still runs along native X (across the columns). The write pointer and the scan pointer therefore
  cross at an angle, and every pixel the scan has already passed keeps the OLD value for that
  frame: the boundary between old and new is the diagonal, from the lower middle edge to the upper
  right corner, exactly as Filip described. A faster push only changes the slope of that line; it
  cannot remove it. (This is the same artefact EST-336 fixed on wake by pushing with the display
  off.)
- **The computed floor:** 320 x 240 x 2 = 153,600 B at 4 bits per clock (`pin_io0..io3`) and
  `freq_write = 75 MHz` = 153,600 x 8 / 4 / 75e6 = **4.10 ms** of pure wire time, against one panel
  frame at ~60 Hz = 16.7 ms. So the bus is not the limit; even at 4 ms the line is there.
- **The slope is a measurement too.** With the scan stepping one native row per 16.7/320 = 52 us and
  a screen row pushed in P/240, the boundary's slope is 0.75 x 16.7 ms / P. Filip's line (lower
  middle to the upper right corner) has a slope of ~1.5, so the old push took about **8.3 ms**: it
  fits the 4.10 ms wire floor plus five window setups and the PSRAM-sourced DMA, and it is why the
  tear was a whole-screen diagonal rather than a thin band.
- **TE is not available:** the L2's display is QSPI with SCLK/IO0..IO3/CS only (`board.h`), the
  panel config has no `pin_te`, and the bus has `pin_miso = -1`, so the panel's frame phase can
  neither be read nor waited for. A "start at the top of a frame" push is therefore a guess.
- **SD/JPEG is not in the push.** `map.cpp`'s `cacheGet()` seeks, reads and `JPEGDEC.decode()`s
  synchronously inside `mapRender()`; the tile work is finished before `omFrameEnd()` starts. What
  the decode costs is switch *latency*, not push duration.
- **No deferred tile work to cancel.** `mapRender()` composes the canvas and returns; there is no
  queue, no worker and no "pending tile" flag, so leaving the tab cannot leave work behind that
  later paints. (The 8-tile PSRAM cache is read-only after the render.)

**2. The fix: the whole-screen push is made with the display off.** `board.cpp` arms it:
`omFrameScreenChange()` makes the NEXT outer push `DISPOFF` (0x28) -> push -> `DISPON` (0x29 +
dummy), so the panel turns on to a frame that is already complete. That is the wake path's own rule
(EST-336), now at the one place every screen change passes through: `showTab()` arms it for all
four tabs. Ordinary redraws (toast, pan, scroll, a key) do NOT arm it: their pixels are the same
image except for the part that moved, so the same crossing is invisible there, and blanking every
push would make the panel flash continuously. Settings pages are not armed either (same single push,
dark content); say the word and they get the same flag.
- `DISPON` is a plain 0x29: LovyanGFX's `setPowerSave(false)` does **not** re-run the init table
  (only `setSleep(false)` does SWRESET), so MADCTL/rotation/inversion survive the blank.
- **The push is also shorter:** 5 slices of 48 rows instead of 15 of 16. 320 x 48 x 2 = 30,720 B
  stays under the SPI peripheral's `SPI_MS_DATA_BITLEN` cap of 0x3FFFF bits = 32,767 B per DMA
  transfer (a 64-row slice would be 40,960 B and split into two transfers with a 1 ms wait between
  them, `Bus_SPI::writeBytes`), and 240 = 5 x 48 exactly. Each slice is one contiguous DMA (the
  sprite is pushed raw, `no_convert`), so the blank is as short as the bus allows.
- **Measured, once per switch:** `omFrameEnd()` times the five pushes and prints
  `[ui] frame push N us`; `showTab()` counts the pushes and prints
  `[ui] tab <name>: N frame pushes, not 1` if a screen ever draws twice. Expected on the glass:
  ~4.1-6 ms (wire floor 4.10 ms); anything near 16 ms+ would mean the PSRAM-sourced DMA is slow and
  the next step is a small internal-RAM bounce buffer, not a bigger blank.

**3. The attribution has its own place.** It is no longer drawn by `mapRender()` (the canvas is
tiles only): `ui/map_chrome` has `mapCreditRect()` and `screen_map.cpp` draws the one dim line with
`drawTextEllipsis`, measured with the same MonoMeta face, right-aligned to the key column on the
bottom row. At z13: credit **x 146..284**, the scale's rail ends at **81** (`500m` at 6, `1km`
ending at 81), so a **65 px** gap; the zoom keys own 290..314, so the line stops short of them. If a
narrower screen cannot hold both on that row, the credit moves up one line instead of printing on
the labels. Host test (`test_est351.cpp`, `testMapCredit`) proves, for z9..z18 at 46.05 N: no overlap
with either scale label, the rail, the three keys or the chip; right edge = `zoomOut.x - 6`;
`y + h/2` on the scale's label line; the z13 numbers above; and the fallback row. The credit is
only drawn when the pack is open.

**Exact `tools/cmd.py` steps for Claude** (one reader on the port; `OM_PORT=/dev/ttyACM0`;
`tools/shot.py docs/shots/<name>.png` after each step):
1. Flash `firmware.bin` (1,709,152 B). Boot must still print
   `[om] colour push probe gold ed89 -> stored 89ed -> back ed89 OK` and the `[map] tile pixel probe
   ... OK` line. `tools/cmd.py status` -> note `loop_stack_size` and `loop_stack_free`.
2. **Map, then leave it (the tear):** `tap 200 227` (Map tab) -> shot `2026-11-10-est353-map`; the
   serial prints `[tab] Map` then `[ui] frame push NNNN us` (note N). Then `tap 40 227` (Nodes) ->
   sim: the serial prints `[tab] Nodes` + its own `[ui] frame push ... us`; **watch the glass for the
   diagonal**: there must be none, and no line `[ui] tab ...: N frame pushes, not 1`. Shot
   `...-est353-nodes`.
3. **Every other tab switch:** `tap 120 227` (Chat) -> `tap 280 227` (RPTR) -> `tap 200 227` (Map) ->
   `tap 120 227` (Chat) -> `tap 40 227` (Nodes) -> `tap 200 227` (Map) -> `tap 280 227` (RPTR). Each
   switch must print exactly one `[ui] frame push N us`, and the value should be the same within a
   few hundred us whichever screen is left (the push does not depend on the screen). Note the worst
   one; it must be well under 16,700 us.
4. **Attribution vs scale:** on the Map shot, the bottom row must read `500m` and `1km` at the left
   (ending x ~81) and `Esri, HERE, Garmin, OSM` right-aligned ending at x 284, with a clear gap; no
   glyph of one on the other. `tap 302 169` (`+`) -> shot `...-est353-map-zoom`: at every zoom the
   two must stay apart (the credit does not move; the labels re-measure).
5. **The panel comes back correct after a blank:** after the tab switches, the clock and the status
   icons must still be right, not shifted left by one pixel, inverted or rotated: that would mean
   `DISPON` reset something (it must not; it is a bare 0x29).
6. **Loop stack:** `tools/cmd.py status` after the steps; `loop_stack_size=16384` unchanged and
   `loop_stack_free` at the EST-350 level (no new allocation, only 8 B of statics).

**Could not check** (no device, no card in the sandbox): whether the blank is perceptible (or, worse,
whether DISPOFF on this panel shows *white* because `cfg.invert = true`; the wake path never showed
it because the backlight was 0), the measured `N us`, the slope-free result on the glass, and the
credit's look over a bright tile. The host test proves the rectangles and the arithmetic, not the
panel.

**If the blank is visible**, the next option is the push in native scan order: 320 `pushImage(x, 0,
1, 240, column)` transfers of 480 B, which writes one native row per column and matches the scan
axis (4.1 ms of wire time plus 320 window setups, and it needs the MY direction confirmed on the
glass). That was not chosen blind: its failure mode is another line, while DISPOFF's is a short
blank.

## 2026-11-10 16:25 (Claude): EST-353 on the device
- Built in the sandbox (exit 0, RAM 170,424 B, flash 1,708,785 B), tests exit 0, reviewed binary flashed (1,709,152 B).
- Tab switches by USB (Map, Nodes, Map, Chat): `[ui] frame push` 11,376..11,470 us each, i.e. the display is dark for about 11 ms per screen change (DISPOFF -> push -> DISPON). Whether the diagonal is gone and the blink is invisible can only be judged on the glass: Filip.
- Map bottom (`docs/shots/2026-11-10-1625-map-labels.png`): scale "500m / 1km" and the attribution "Esri, HERE, Garmin, OSM" now sit apart.

## 2026-11-10 16:35 (DS): EST-354, Wi-Fi (off by default), its Settings page and NTP

Built with the REBUILD.md command, **exit 0**: RAM 197,456 B of 327,680 (60.3%), flash
2,129,921 B of 6,553,600 (32.5%), `firmware.bin` 2,130,288 B. `test/host/run.sh` **exit 0**
(all suites, including the new `test_wifi.cpp`).

**The pieces.**
- `src/wifi_model.h`: plain C++, host-tested. `SavedStore` (five networks, one row per SSID, adding
  an existing SSID replaces its password in place, forget compacts), `ScanList` (one row per SSID,
  stronger sighting wins, strongest first), and `Machine` (Off / Idle / Scanning / Connecting /
  Connected / Failed, 15 s scan and 20 s connect timeouts, millis() wrap safe). The page and the
  status bar read only this state; the radio side reports facts into it.
- `src/wifi_link.cpp/.h`: ESP32 STA. `WiFi.begin()` is non-blocking, the scan is
  `WiFi.scanNetworks(true)` polled with `scanComplete()`, and both timeouts are the model's, checked
  in `wifiLoop()` on the loop task. `WiFi.persistent(false)` and `setAutoReconnect(false)`: our NVS
  blob is the only credential store and the model owns retries (none while it says Failed).
- Settings > General > Connections > Wi-Fi is now the real page: On/Off toggle (saved in NVS,
  **default off**), the state line (Off / On, not connected / Scanning / Connecting / Connected /
  Failed: reason), Network + IP + Signal when connected, `Scan networks ›`, the visible networks
  strongest first with an RSSI and a vector lock mark for secured ones, and `Saved networks ›` (five
  slots, Forget asks for a second tap). Tapping a network already saved connects with the stored
  password; a new secured one opens the password sheet; an open one connects at once.
- The password sheet is the shared chat keyboard (`KeyboardField::WifiPassword`, 63 bytes, printable
  ASCII, no accented long press, no emoji page) with the chat's own key rectangles. The draft shows
  one dot per byte and a `show` / `hide` key toggles the text. The passphrase is wiped from the
  working and display buffers on Connect, on Back and on switch-off, and is never printed: no serial
  line, and no USB command carries or reads one.
- NTP: on every successful association `esp_sntp_init()` asks pool.ntp.org once. `configTime()` is
  deliberately not used because it would overwrite TZ (the timekeeper owns Ljubljana). The IDF's
  `sntp_sync_time` is weak (`nm liblwip.a`: `W sntp_sync_time`), so our definition captures the
  epoch and does not touch the clock; the loop then calls
  `timeKeeper().setFromSource(epoch, TimeSource::Wifi, millis()/1000)`. GPS stays the higher source:
  if it set the clock in the last 6 h the log says `kept (a higher source holds the clock)` and the
  source stays GPS. That is the ranking, not a failure. Nothing else is started on the network.
- Status bar: `s.wifi` = the switch, `s.wifiConnected` = associated (a scan that keeps the link stays
  ink). The reserved `kIconWifi` slot is drawn dim while on but not associated and in ink with an IP;
  absent when off. A Wi-Fi serial bumps on every visible change and the loop does one redraw
  (never while the password sheet is open).
- USB: `wifi status`, `wifi scan`, `wifi on`, `wifi off` (`Kind::Wifi` in usb_cmd_logic). `wifi scan`
  is asynchronous like the screen: the first call starts the scan and answers `state=scanning`; a
  second call a few seconds later prints one `[wifi] ssid="..." rssi=-NN secured=0|1` line per SSID
  and `[cmd] ok wifi scan N`. Blocking the command until the radio answered would stop the mesh loop,
  which the ticket forbids. `wifi status` prints switch, state, SSID, IP, RSSI, reason, saved count,
  scan count, time source, heap, PSRAM, `service_us_max` and `mesh_serial`; no password exists in
  that line or in any handler.

**RAM, before and after.** Baseline (same sandbox, before this ticket): RAM 170,424 B, flash
1,708,785 B. After: **197,456 B (+27,032 B)** and **2,129,921 B (+421,136 B)**. Of the RAM, our own
objects are small: `SavedStore` 492 B, `ScanList` 612 B, `Machine` 96 B, `Status` 96 B, about 1.3 KB
together; the remaining ~25.7 KB is the Wi-Fi/lwIP library's own static BSS that the link pulls in
even with the radio off. That is why the switch is not the whole memory story: the driver's heap is
allocated only when the switch turns on, and `startDriver()` prints the exact internal-heap and
PSRAM delta with its duration (`[wifi] driver up in N ms: heap A -> B (+/-X B), PSRAM ...`). Turning
it off prints `[wifi] driver down: heap ...`. The device numbers are the ones to quote; they could
not be measured in the sandbox.

**Radio coexistence, what was checked.** The SX1262 is on SPI2 (NSS 21, SCK 4, MISO 5, MOSI 6,
`radio.h`), the panel on SPI3, the card on SPI2/1-bit; Wi-Fi is the SoC's internal radio and has no
SPI bus or pin in common, so there is no bus arbitration to lose. What it does share is CPU: the
Wi-Fi/lwIP tasks run on the other core, and this firmware only polls (`wifiLoop()`); the measured
steady-state cost is in `wifi status` as `service_us_max`. `[hb]` now carries `wifi=<phase>` next to
`mesh=up`, and `wifi status` carries `mesh_serial` (`meshMsgSerial()`), so reception while Wi-Fi is
on can be shown by sending a channel message from another node and seeing `mesh_serial` grow and the
message arrive. Nothing in the history flush moved: `history::loop()` still runs first in the loop.

**Host tests prove**: the saved list (add, replace in place, full at five, sixth refused, forget by
name and index, bad/long input), the scan list (SSID dedupe, stronger sighting, sort, lock flag,
bound) and every machine transition, both timeouts and millis wrap; the password field's 63-byte
limit, printable-ASCII filter, no accents/emoji, the show/hide key in both keyboard pages and its tap
rectangle; and that `wifi status|scan|on|off` parse while `wifi`, `wifi bogus`, `wifi on extra` and
`wifi connect` are refused (no word can ask for a password).

**Exact `tools/cmd.py` steps for Claude** (one reader on the port; `OM_PORT=/dev/ttyACM0`; shot with
`tools/shot.py docs/shots/<name>.png` after each screen step):
1. Flash `firmware.bin` (2,130,288 B). Boot must print
   `[wifi] off at boot: heap=N psram=N saved=0 switch=off` (or the saved count) and the first
   `[hb] ... wifi=off`. The radio is not initialised at boot.
2. `tools/cmd.py status` -> note `heap`, `hist_worst_us`, `loop_stack_free`.
3. `tools/cmd.py wifi status` -> `switch=off state=off saved=0 scan=0 ... heap=... psram=...
   service_us_max=0 mesh_serial=N`.
4. `tools/cmd.py wifi on` -> the serial prints
   `[wifi] driver up in N ms: heap A -> B (+/-X B), PSRAM ...` and
   `[wifi] link check: SX1262 on SPI2, display SPI3, card SPI2; Wi-Fi uses no SPI bus`; the answer
   is `switch=on state=idle` (or `failed` with the reason if the driver could not allocate). One
   more `wifi status` -> `state=idle`, `service_us_max` a few dozen us.
5. `tools/cmd.py wifi scan` -> `[cmd] ok wifi scan 0 state=scanning`. Wait 6 s (the driver's own
   scan timeout is 6 s), then `tools/cmd.py wifi scan` again -> one `[wifi] ssid="..." rssi=-NN
   secured=0|1` per network, strongest first, and `[cmd] ok wifi scan N`. If the first call is
   repeated too early it still answers `state=scanning`, no block.
6. Settings by touch: `tap 307 13` (gear) -> `tap 32 38` (General tab); the General page opens at
   scroll 0. Wi-Fi is under CONNECTIONS, below Channels: drag the body up (`drag 160 210 160 110
   300`) and tap the Wi-Fi row (with that drag it sits near y=167; read the row from the shot if the
   scroll snapped differently). Shot `2026-11-10-est354-wifi-page`: switch OFF, State "Off", note
   "Turn Wi-Fi on to scan for networks"; the networks and Saved rows are not drawn while off.
7. Still on that page: tap the Wi-Fi toggle (y=87 at scroll 0) -> shot: ON, State "On, not
   connected", `Scan networks ›`, the note; the status bar now shows the dim Wi-Fi icon. Tap
   `Scan networks` (y=147) -> "scanning" in the row; after a few seconds the loop redraws to the
   NETWORKS list, strongest first, with a lock mark on secured rows; the first network row is at
   y=219 while the page is at scroll 0. Shot `2026-11-10-est354-wifi-scan`.
8. **Filip enters the secret** (Claude cannot and must not): tap the home network (first row,
   `tap 160 219`), the password
   sheet opens with the SSID in the title, dots only; Filip types the passphrase on the glass (the
   `show` key reveals it, `hide` covers it), taps `Connect`. Claude can then read
   `tools/cmd.py wifi status`: `state=connecting` then `state=connected ssid="..." ip=... rssi=-NN`;
   serial: `[wifi] connected to "..." ip=... rssi=-NN` and, once the reply arrives,
   `[wifi] NTP <epoch> s: applied, source Wi-Fi` (or `kept (a higher source holds the clock)` when
   GPS has already set the clock in the last 6 h; that is the ranking). Shot
   `2026-11-10-est354-wifi-connected`: Network + IP + Signal rows, status bar icon in ink.
9. NTP check: `tools/cmd.py status` -> `clock=Wi-Fi` and the status bar shows HH:MM; with a GPS fix
   already applied the log says `kept`, and `clock=GPS` is correct. Every NTP line is one per
   connection, no other traffic.
10. Auto-connect: `tools/cmd.py wifi off` -> shots/answers `switch=off`; `tools/cmd.py wifi on` ->
    `state=connecting` with the saved SSID within a second and `connected` again; no password entry.
11. Saved list: tap `Saved networks ›` -> five rows at most, each with a `Forget` key; one tap says
    "Tap Forget again", the second removes it and the count drops. Shot
    `2026-11-10-est354-wifi-saved`. Forget the test network only if Filip wants it gone; the list is
    his.
12. Mesh while Wi-Fi is on: note `mesh_serial` from `wifi status`, send a channel message from the
    other node and run `tools/cmd.py msgs 3` and `wifi status` again; `mesh_serial` must grow and the
    message must be there. `[hb]` must still print `mesh=up` and `wifi=connected`, and
    `loop_stack_free` must stay at the EST-350 level (Wi-Fi adds no loop-task stack use; its tasks
    are its own).
13. Password privacy: while the sheet is open, capture the serial and search for the passphrase
    Filip typed; it must not appear. `wifi status` and `wifi scan` show SSIDs, never a password.

**Could not check** (no device, no card, no serial): the measured driver-up duration and heap/PSRAM
delta on the L2 (the numbers printed by step 4 are the real answer), actual scan results and RSSI,
a real association and the NTP reply (the linker accepted the weak `sntp_sync_time` replacement, its
runtime path is unproven), auto-connect on Filip's home AP, the glass (dots, lock mark, icon
colours, the password keys under a finger, the page's scroll), and whether the ESP32-S3's Wi-Fi
buffer allocation behaves with this board's octal PSRAM. If `WiFi.mode(WIFI_STA)` fails the switch
turns itself back off and `[wifi] driver init FAILED` prints the heap, so that failure is not silent.

**Next step:** flash, run steps 1-13, and report the driver-up numbers, the scan list and the NTP
line; then EST-355 (Bluetooth) can reuse the same switch/NVS/status pattern.

## 2026-11-10 17:00 (Claude): EST-354 on the device
- Built in the sandbox (exit 0, RAM 197,456 B = 60.3 %, flash 2,129,921 B, +421 KB for the Wi-Fi stack), tests exit 0, reviewed binary flashed (2,130,288 B). Checked: no log line prints a password (only `s_pendingPass` is set from it).
- By USB: `wifi status` off by default; `wifi on`: heap 128,668 -> 90,872 B (Wi-Fi driver up). **The first scan right after switching on fails** (`state=failed reason="scan failed"`); a second scan: `scan done: 12 visible, 12 rows`. Mesh stayed up. Connecting needs Filip's home password, typed by him on the device.

## 2026-11-10 17:00 (DS): EST-355, the backlight dip on a screen change, and the first Wi-Fi scan

Built with the REBUILD.md command, **exit 0**: RAM 197,576 B of 327,680 (60.3%), flash 2,132,309 B of
6,553,600 (32.5%), `firmware.bin` 2,132,672 B. `test/host/run.sh` **exit 0** (all suites, including
the new `test_est355.cpp` and the new scan-order test in `test_wifi.cpp`). Baseline (EST-354):
RAM 197,456 B, flash 2,129,921 B, so this ticket costs **+120 B RAM and +2,388 B flash**.

**1. Why the dip, and not a cleverer push.** EST-353's DISPOFF/DISPON around the push did not hide the
tear on this panel (Filip, ~16:30), and the mechanism says it cannot: the panel scans the RAM while the
push writes it, so what is scanned before the write arrives is the OLD frame, and a blanked display
still shows the result when it comes back (the panel re-scans its RAM; DISPON does not re-push it).
What cannot depend on the panel's timing is the LIGHT: with the backlight at 0 nothing that is scanned
can be seen, and 17 ms after the push the RAM holds the complete frame. So a screen change now takes
the light down, pushes, waits one panel frame, and brings the light back. The display itself stays ON:
**DISPOFF/DISPON are gone** (`omFrameScreenChange()` is deleted). On the frame timings: Claude measured
11,376..11,470 us per armed push; the blank did not shorten the transfer (it is five DMA slices, whose
time cannot depend on the panel's display state) and did not hide the tear, so it bought nothing. This
build prints the push time without the blank in the change line, which is the A/B that was missing.

**2. Only when it can be seen.** The tear is written on every push; it is SEEN only when the old and
new frames differ strongly in brightness (Filip, 16:35). One rule in `src/frame_metrics.h` (plain C++,
host-tested): `mapInvolved || |lumaOld - lumaNew| > kChangeLumaDelta`. Luma is one number 0..255 per
frame (BT.601 on the 5/6/5 channels, gold = 181, the app background = 18); it is measured from the
frame's own pixels on a 15 px grid (21 x 16 = 336 samples, a few hundred PSRAM reads). Both sides are
measured: the frame on the glass is sampled when the change is asked for, the target tab's last frame
was sampled when it was pushed (`g_tabLuma[]`, four bytes). The map is always dipped, whatever the
numbers say. Everything else (Nodes/Chat/RPTR, all dark) switches instantly, with no blink at all.
- **A bright page seen for the first time** would slip through the prediction, so the composed frame is
  measured before it is pushed: if it is much brighter than what is on the glass, the light is cut then
  (a hard cut, logged as `late dip`, `out 0 ms`), which is honest about what was spent.
- **A second change asked for while the light is still going down coalesces** into the running one: the
  frame that is pushed is the LATEST target, with one push and one dip.
- A change asked for with the light already up again on a new frame (Settle/FadeIn) is a new transition,
  because the first frame is on the glass by then.

**3. The state machine, non-blocking.** `src/screen_change.cpp/.h` (plain C++, no Arduino): FadeOut ->
Push -> Settle -> FadeIn, one `tick()` per loop pass, wall-clock based, no waiting anywhere. It reuses
the wake path's `FadeRamp` (gamma 2.2, EST-339) and the wake path's `TouchGate`. The three durations are
in `theme.h` so they can be tuned after Filip looks: `kChangeOutMs 40`, `kChangeSettleMs 17`,
`kChangeInMs 60`. **17 ms is the ~60 Hz assumption, not a measurement**: the L2 wires no TE line
(board.h), so one full refresh at ~60 Hz is the best that can be said.
- `loop()` ticks it once per pass and runs the push itself: `omFrameHoldPush(true)` composes the target
  into the frame WITHOUT pushing, `omFrameMeanLuma()` measures it, the light is set, `omFrameFlush()`
  pushes once. The loop keeps servicing the mesh, the radio, Wi-Fi and the GPS between the phases, and
  the 1 ms cadence the wake fade already uses is extended to the change.
- **Touches**: nothing is read while the change runs (the sample is forced up and the wake gate is fed
  "no finger"), and when it ends the wake gate is armed with `kChangeGateMs 40`, so a finger that is
  already down must lift first. In-page redraws are dropped while it runs (`redrawCurrentScreen()` and
  the loop's direct draws), so nothing pushes into a light that is on its way down or up.
- In-page updates (a new message, a clock tick, a pan, a key, a toast) keep their plain one push: only
  a screen change (a tab, Nodes > Direct message, Show on map) goes through the machine. Settings pages
  keep their plain push (same dark background on both sides, EST-353's note still applies).

**4. Log lines, one per change** (plus one context line and, when it is slow, the compose time):
```
[ui] change Nodes -> Map: luma 18 -> 0 (unknown), dip
[ui] change: composed in 34 ms (inside the dark)      <- only when >= 5 ms
[ui] change: out 40 ms, push 11376 us, settle 17 ms, in 60 ms
```
`out` is the fade-out that was really spent, `settle` the panel frame after the push, `in` the fade-in;
the push is `omFrameLastPushUs()`. An instant change prints `out 0 ms, push ..., settle 0 ms, in 0 ms`.

**5. Wi-Fi: the first scan after switching on (EST-354's bug).** The driver refuses a scan while its
station is still coming up, and the wrapper turns that into WIFI_SCAN_FAILED, which the page showed as
`Failed: scan failed`. `wifi_model.h` now has a `ScanGate` (plain C++, host-tested) that separates the
REQUEST from the driver call: a scan asked for less than `kScanReadyMs` (250) after the driver came up
waits until it is ready, a refused start (and a scan the driver aborts) is retried after `kScanRetryMs`
(250), at most `kScanAttempts` (3) tries, and only then does the model say Failed. `wifiStartScan()`
now records the request and `wifiLoop()` asks the driver, so **the switch-on path cannot produce
`failed` for this**, and the USB `wifi scan` still answers `state=scanning` at once (it changes nothing
for the caller). Serial: `[wifi] scan requested`, then either `[wifi] scan started` or
`[wifi] scan start refused (try 1 of 3), retrying in 250 ms`.

**Host tests prove** (`test_est355.cpp`, `screen_change.cpp` + `fade.cpp` + `touch_gate.cpp`): the order
of a dipped change (light down only, one push asked exactly at 40 ms with the light at 0, 17 ms of settle
in which no level is written, a fade in that ends at the screen brightness after 60 ms and reports Done);
an instant change (push at once, no level written at all, out/settle/in 0); a second change during the
dip coalescing (one push, the light never jumps back up); a dip asked for before an instant push
escalating in place, and a change asked for after a push being a new transition; the touches (nothing
accepted while it runs, a finger that is down when it ends must lift, the next new press counts); the
luma arithmetic (black 0, white 250, gold 181), the sample mean, the sample count (~350) and the rule
itself (map on either side, the threshold at exactly `kChangeLumaDelta` and one above it, both
directions). `test_wifi.cpp` proves the SCAN ORDER: nothing due in the pass the switch goes on, due at
exactly `kScanReadyMs`, a refusal retried (not failed), 3 refusals the only way to Failed, an accepted
start clearing the request, a scan asked for later starting at once, the switch-off dropping a request,
and the wait surviving the millis() wrap.

**Exact `tools/cmd.py` steps for Claude** (one reader on the port; `OM_PORT=/dev/ttyACM0`;
`tools/shot.py docs/shots/<name>.png` after each step):
1. Flash `firmware.bin` (2,132,672 B). Boot must still print `[om] colour push probe ... OK` and the
   `[map] tile pixel probe ... OK`. `tools/cmd.py status` -> note `loop_stack_size` and
   `loop_stack_free`. **There is no `[ui] frame push` line any more**; every change prints
   `[ui] change: out ... push ... settle ... in ...`.
2. **The map (the page Filip saw the tear on):** `tap 200 227` (Map tab) -> the serial must print
   `[tab] Map`, `[ui] change Nodes -> Map: luma N -> N (unknown), dip`, then (first visit only) the
   compose time, then `[ui] change: out 40 ms, push N us, settle 17 ms, in 60 ms`. **Watch the glass**:
   no diagonal, and the switch is a quick blink, not a black pause (note the `composed in N ms` line
   if the first map entry shows one). Shot `2026-11-10-est355-map`.
3. **Leaving the map:** `tap 120 227` (Chat) -> the same line with `dip` and `luma ~N -> 18`: the light
   must go down and come back with **no diagonal**. Shot `...-est355-chat`.
4. **A dark switch must NOT blink:** `tap 280 227` (RPTR) -> `[ui] change Chat -> RPTR: luma 18 -> 18
   (measured), instant` and `[ui] change: out 0 ms, push N us, settle 0 ms, in 0 ms`. The screen must
   change with no blink at all. Then `tap 40 227` (Nodes) -> the same. Shot `...-est355-nodes`.
5. **Every change is ONE push:** no line `[ui] tab ...: N frame pushes, not 1` anywhere in the log.
6. **The push time without the blank** is the `push N us` in those lines: compare it with EST-353's
   11,376..11,470 us (the same push WITH DISPOFF/DISPON). That is the measurement the ticket asks for.
7. **Touches during a change:** `tap 200 227` (Map) and then, immediately (within ~150 ms), a second
   `tap 160 100`: nothing must be pressed by the second tap, and the screen must stay on the Map. (A
   `tap` that lands while the change runs is dropped; the next one after it works.)
8. **Wi-Fi, the first scan:** `wifi off` then `wifi on`, and **immediately** `wifi scan` -> the serial
   must print `[wifi] scan requested` and then `[wifi] scan started` (or one
   `[wifi] scan start refused (try 1 of 3), retrying in 250 ms` before it), and `wifi status` must
   **never** say `state=failed reason="scan failed"` on that path. A second `wifi scan` a few seconds
   later prints the list (12 networks on the device). Repeat 3 times: the first scan must find the
   networks without a `failed` in between.
9. **Loop stack:** `tools/cmd.py status` after the steps; `loop_stack_size=16384` unchanged and
   `loop_stack_free` at the EST-350 level (the change machine is 40 B of statics and runs on the loop
   task, no new allocation).

**Could not check** (no device, no card, no serial): whether a 40 + 17 + 60 ms dip reads as a blink or
as a flicker on the glass (that is Filip's call; the three numbers are in `theme.h`), the measured push
without the blank, the actual luma values of the real map (the map rule dips it regardless), how long
the map's first compose is on the device (the log says it), the touch feel during a change, and whether
the first Wi-Fi scan now succeeds on the device (the host proves the order, not the driver). The "no TE
line" assumption of ~60 Hz is unchanged and is why 17 ms is an assumption.

**Next step (if the blink is too much or too little):** tune `kChangeOutMs` / `kChangeSettleMs` /
`kChangeInMs` in `theme.h` only. If the map's first entry shows a long `composed in N ms` and Filip
dislikes the black, the next change is to compose BEFORE the fade-out (the frame is already held by
`omFrameHoldPush`, so only the order of two calls in `screenChangeLoop()` moves) and then the dark is
always 40 + push + 17 ms.

## 2026-11-10 17:35 (Claude): EST-355 on the device
- Built in the sandbox (exit 0, RAM 197,576 B, flash 2,132,309 B), tests exit 0, reviewed binary flashed (2,132,672 B).
- Tab switches by USB: `Nodes -> Map: ... dip` (composed in 101 ms inside the dark), `Map -> Nodes: luma 160 -> 14 (measured), dip`, `Nodes -> Chat: ... instant`. Wi-Fi came back on by itself after the reboot (`state=connected`). Whether the dip hides the diagonal and how it feels: Filip.

## 2026-11-10 17:45 (DS): EST-356, Bluetooth as a MeshCore companion node (off by default)

Built with the REBUILD.md command, **exit 0**: RAM 220,744 B of 327,680 (67.4 %), flash 2,716,597 B of
6,553,600 (41.5 %), `firmware.bin` 2,716,960 B. `test/host/run.sh` **exit 0** (all suites, including
the new `test_est356.cpp`). Baseline (EST-355): RAM 197,576 B, flash 2,132,309 B, bin 2,132,672 B, so
this ticket costs **+23,168 B RAM and +584,288 B flash**.

**1. The transport is upstream's, not a second protocol.** `src/ble_transport.cpp` gives
`lib/meshcore/helpers/esp32/SerialBLEInterface.cpp` one translation unit by including it (that file is
not in `lib/meshcore`'s `library.json` `srcFilter`, `+<helpers/*.cpp>` does not reach `helpers/esp32/`)
and wraps it in `BleSerial : BaseSerialInterface`. The wrapper is what `the_mesh.startInterface()` now
receives instead of `null_serial`, and it exists for three reasons: the switch (MyMesh calls
`enable()` at boot, so it must be a no-op while the switch is off), the phone's outgoing frames (see
4), and the heap numbers. `lib/` is untouched. No `lib/` file was edited.

**2. Switch, PIN, states.** NVS namespace `omble`, key `on`, **default OFF**: with the switch off at
boot the BLE stack is not even allocated (no `BLEDevice::init()`), so an unused feature costs no heap.
Switching on: a 6-digit PIN is made at the first enable (`ble_pin.h`, `esp_random()` mapped to
100000..999999, 123456 refused), written to `NodePrefs::ble_pin` + `savePrefs()` (exactly
CMD_SET_DEVICE_PIN's path, `meshBlePinSet()`), then `BLEDevice::init("MeshCore-" + node name, pin)`.
States are `off / advertising / connected`, from the transport's own `isConnected()`.
- Switching off stops the service and advertising and disconnects; the controller stays initialized
  (the radio is off, the heap comes back at reboot only). Re-initialising bluedroid on every toggle
  is the one sequence that cannot be proved without the device, so it was not done. The page's
  "New PIN" (and the phone's CMD_SET_DEVICE_PIN) updates `BLESecurity::setStaticPIN` live, so the
  next pairing uses the new PIN without a reboot; a phone already connected stays connected.
- The BLE advertised name is read at bring-up. A phone-side rename changes the node name (messages,
  adverts, the screens) immediately; the BLE name follows on the next switch-on/reboot. That is
  upstream's behaviour, not a second name.
- `ble status` can report `pin_set=1/0` but **never the PIN**; the reply is
  `[cmd] ok ble switch=... state=... peer="..." pin_set=... heap=... psram=... wifi=... mesh_serial=...`.
  The transport stores no peer name, so a connected peer is `peer="unknown"`; when none is connected
  it is `peer="-"`.

**3. Settings and the status bar.** Settings > General > Connections > Bluetooth no longer says "Not
built yet.": a Toggle (default OFF), State (off / advertising / connected), PIN (six digits, or "made
when switched on"), a New PIN button and a one-line note (off / advertising / phone connected). The
General row shows `off` / `on` / `connected`. The status bar's EST-332 Bluetooth mask is absent when
off, dim while advertising, ink while a phone is connected; the bar redraws on the switch and phase
serials.

**4. One node, two faces: the screens now read MeshCore's own stores on every draw.**
- **Channels**: `mcChannelCount()/mcChannel()/mcChannelName()` used to read our NVS mirror; they now
  read `BaseChatMesh`'s channel table (MyMesh + `/channels2`) on every call, with a UI index mapped to
  a MeshCore slot (empty slots are skipped). Our adds, renames and removes write that table and call
  the same `DataStore::saveChannels()` the phone's CMD_SET_CHANNEL calls. The old NVS store ("mcch")
  is kept only as the one-time migration seed (`mcLegacyChannel*`; `pushChannelsFromNvs()` uses it,
  so a phone-added channel is never overwritten at boot) and as the home of the two flags MeshCore
  does not model: per-channel enabled and mute.
- **Unread** counts are now keyed by the channel HASH (first byte of sha256(secret), MeshCore's own
  match) instead of the UI index, so a phone-side add/remove cannot move a badge to another channel.
- **Radio prefs** were already NodePrefs; after a phone radio change the Settings page's small cached
  copy (`g_radio`) is re-read from NodePrefs. **Name** is `meshNodeName()` (NodePrefs) on every draw.
  **Contacts** were already the MeshCore contact table.
- **Toasts**: a poll in `meshLinkLoop()` compares channel / radio / name signatures once per pass. Our
  own setters bump per-store write counters, so a value the glass just wrote is never reported as the
  phone's. An outside change queues `Channels changed from the phone`, `Radio changed from the phone`
  or `Name changed from the phone` (one toast, one redraw that rebuilds the screen from the stores).
- **Messages the phone sends** never reach the listener (it only sees what arrives), so the transport
  taps the two send commands (`CMD_SEND_CHANNEL_TXT_MSG`, `CMD_SEND_TXT_MSG`) before MyMesh handles
  them and stores them in our chat ring as ours (`[ble] phone sent chN: ...`). A phone-sent DM is
  stored as `Sending`: the frame is MyMesh's, so our ACK table cannot match its ACK, and claiming
  Delivered would be a guess. Caveat: the tap stores the message optimistically, so a send MyMesh
  refuses (packet pool full) would still show in our ring.
- **Messages received** are queued for the phone by upstream (its offline queue, up to 16) AND stored
  in our ring + unread by the listener. The two counts are independent: the phone pulling messages
  from its queue does not clear our per-channel unread, and opening the chat here does not remove
  anything from the phone's queue. Pulling on the phone does not touch our ring.
- **Time**: checked, no change needed. `OmRTCClock::setCurrentTime()` sends CMD_SET_DEVICE_TIME to
  `timeKeeper().setFromSource(..., TimeSource::Phone)` once `target_mesh_begun` is set, ranked below
  GPS and Wi-Fi.

**5. Wi-Fi + BLE on the ESP32-S3: what is measured and what to move to PSRAM.** The statics this
ticket adds are +23,168 B of DRAM (mostly the bluedroid/BLE glue the linker now keeps). Runtime heap
must be measured on the device: `[ble] driver up in N ms: heap A -> B (...), psram ...` and
`[ble] coexist (BLE on): heap=... psram=... wifi=...`, plus `[ble] wifi changed:` when Wi-Fi's phase
changes while BLE is up, and `ble status` gives heap/psram on demand. Expected: EST-354 measured
Wi-Fi alone at ~90 KB free; the BLE controller/bluedroid take their memory from the same internal
heap, so both on is the number to watch. From this ELF (`nm`), the DRAM blocks big enough to matter:
`the_mesh` 28,096 B, `s_dmRing` 10,708 B, `g_dmScratch` 9,472 B, `s_msgs` 9,216 B, `s_jpeg` 17,884 B.
If `[ble] coexist` shows less than ~25 KB free, the order I propose is: (a) the two message rings plus
the chat scratch (29,396 B) into PSRAM, allocated once at boot (they are our structs, no DMA);
(b) the JPEG decode buffer (17,884 B) into PSRAM (slower decode, small images); (c) `the_mesh`
(28,096 B) last, because it needs a pointer indirection through `target.h` and a MyMesh allocated into
PSRAM. The mesh loop and the history flush are untouched by this ticket; the heartbeat and
`hist_worst_us` are the check.

**6. USB surface for the reviewer:** `ble status`, `ble on`, `ble off`. `ble` alone, `ble pin` and
extra words are refused (the parser test proves it). No reply prints the PIN.

**Host tests prove** (`test_est356.cpp`): the switch starts off and follows off -> advertising ->
connected -> advertising -> off with one serial bump per visible change and none on a poll; the phase
words for the row/page/USB; the PIN generator over a million random words (always 100000..999999,
never 123456, 123456 pushed to 123457, both ends reachable), `pinValid()` and the six-digit text; the
change watch (first poll is a baseline, an outside change is the phone's, our own write is not, the
three bits are independent, three changes in one pass are all reported, the toast texts and a short
buffer); the `ble` command (status/on/off accepted, bare/pin/extra refused) and that `wifi`/`status`
still parse.

**Exact `tools/cmd.py` steps for Claude** (`OM_PORT=/dev/ttyACM0`; `tools/shot.py docs/shots/<name>.png`
after each step that matters):
1. Flash `firmware.bin` (2,716,960 B). Boot still prints `[om] colour push probe ... OK`, the map tile
   probe, `[mc] identity ...`, `[mc] sizes: MyMesh=28096 B, ...`, and `[ble] off at boot: switch=off
   pin_set=0 heap=... psram=...`. `tools/cmd.py ble status` -> `switch=off state=off pin_set=0`.
2. Settings > General: the Bluetooth row says `off`; open it: the Toggle is OFF, State `off`, PIN
   `made when switched on`, a New PIN button. Shot `2026-11-10-est356-bt-off`.
3. `ble on` -> serial `[ble] first switch-on: a 6-digit PIN was made and saved`, `[ble] driver up in
   N ms: heap A -> B (...)`, `[ble] coexist (BLE on): heap=... psram=... wifi=...`. `ble status` ->
   `state=advertising pin_set=1`, and **no six-digit number anywhere in the reply**. The page now
   shows the PIN; the status bar icon is dim. Shot `...-est356-bt-advertising`.
4. Filip pairs the MeshCore app with that PIN. Serial `[ble] connected: heap=...`; `ble status` ->
   `state=connected peer="unknown"`; the icon turns ink. Shot `...-est356-bt-connected`.
5. From the phone: add or rename a channel -> toast `Channels changed from the phone`, and Settings >
   Channels / the Chat tab show it; change radio params or TX power -> `Radio changed from the phone`
   and Settings > Radio shows the new values; rename the node -> `Name changed from the phone`.
6. From the phone: send a channel message -> `[ble] phone sent chN: ...` and the message appears in
   our chat as ours. From another node: send a message to a channel -> it appears in our chat with
   unread, and the phone also gets it from its queue. Note the two unread counts are independent.
7. `ble off` -> `[ble] off (stack kept until reboot)`, icon gone, page `off`; `ble on` again -> no new
   PIN (pin_set stays 1); `ble status` confirms.
8. Wi-Fi + BLE together: `wifi on` (saved network auto-connects), then `ble on`; `ble status` and
   `wifi status` give heap/psram on both sides and `[ble] coexist` / `[ble] wifi changed` lines carry
   the delta. `tools/cmd.py status` must still show `mesh=up`, a growing `hist_worst_us` without
   `hist_dropped` growth, and `loop_stack_free` at the EST-350 level.
9. PIN privacy: search the serial log of steps 3-8 for the six digits shown on the page; it must not
   appear. `ble status`/`ble on`/`ble off` replies must never carry it.
10. New PIN: press it; the page shows another six-digit number, `pin_set=1`; if the phone is asked to
    re-pair, it must take the new number (the live passkey path, see "could not check").

**Could not check** (no device, no flash, no serial): that `btStart()`/bluedroid come up on this
board's PSRAM/flash config at all; the real heap with BLE alone and with Wi-Fi + BLE (the numbers in
step 3 are the answer); that `BLESecurity::setStaticPIN` at runtime (New PIN / CMD_SET_DEVICE_PIN)
really changes the passkey the phone is asked for, or whether a BLE restart is needed; pairing and
bonding with the Android/iOS app; that the app's channel add/remove/rename commands land in
`/channels2` and show here; that a phone-sent message is stored once (not twice) and that a phone-sent
DM's "sending" state is acceptable; behaviour under Wi-Fi/BLE coexistence on air (both radios share
one controller); the actual screen of the Bluetooth page (PIN, note, button fits); and the status
bar icon colour transition.

**Next step:** flash, run steps 1-10, and report the `[ble] driver up` / `[ble] coexist` numbers, the
pairing result and whether the phone's channel/radio edits show up with their toasts. If the heap with
both radios is below ~25 KB, the PSRAM move in 5(a) is the next ticket; if the live New PIN does not
take, an explicit BLE restart belongs to that ticket too.

## 2026-11-10 18:05 (Claude): EST-356 crashed and boot-looped the device; recovered by esptool
- Built in the sandbox (exit 0, RAM 220,744 B = 67.4 %, flash 2,716,597 B), tests exit 0, reviewed binary flashed 17:52 (2,716,960 B). `ble status`: off, heap 53,948 B with Wi-Fi connected.
- `ble on` -> USB dropped. The switch was saved on, so every boot started BLE after Wi-Fi and panicked: boot loop, no window for OTA (a "ble off" spammer did not get in). Filip put the chip in download mode (BOOT+RESET); Claude wrote `est355.bin` to 0x10000 and erased otadata (0xe000, 0x2000) with esptool, Filip tapped RESET: EST-355 runs, mesh up, Wi-Fi connected, history intact.
- Decoded core dump (EST-356 ELF): assert in Bluedroid's hash_map_set <- btu_start_timer <- BTM_ReadLocalDeviceNameFromController: an internal-RAM allocation failed. Out of internal RAM with Wi-Fi up.
- Correction of Claude's first read: the fonts are NOT in RAM (nm shows them as D, but their addresses are flash rodata 0x3c2...). The movable internal RAM is the list in the next ticket (~75-80 KB).

## 2026-11-10 18:10 (DS): EST-357 internal RAM for Bluetooth: PSRAM move, start guard, crash guard
- Build in the sandbox (REBUILD command, `EXIT:0`): **RAM 136,224 B = 41.6 %** (was 220,744 B = 67.4 %, **-84,520 B**); **flash 2,703,221 B = 41.2 %**; `firmware.bin` 2,703,584 B. Host tests `sh test/host/run.sh` exit 0 (every suite, including EST-356's).
- `EXT_RAM_BSS_ATTR`: checked, **not usable on this core**. `esp_common/include/esp_attr.h` defines it only under `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`, which is off in the esp32s3 `sdkconfig`; the macro expands to nothing. So the move is runtime `ps_malloc`, one allocation each, with an internal-RAM fallback that the log names.
- New `src/psram_alloc.h` (one zeroed, constructed PSRAM array/object; `ps_calloc`/`ps_malloc`, fallback to `calloc`/`malloc`) and `src/mem_stats.h` (the one number: `heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)` and its largest block, plus the `[mem]` line).
- **What moved out of internal `.bss`** (nm before -> after):
  - `s_jpeg` (JPEGDEC) 17,884 B -> PSRAM object in `mapBegin()`; the four calls now go through the pointer.
  - `copyLegacyContactsIfNeeded()`'s `s_legacyBuf` + `recs` + `s_image`, 14,592 B -> a local `Scratch` that allocates only when the migration runs and frees in its destructor on every return path.
  - `s_dmRing` 10,708 B -> PSRAM object in `meshLinkBegin()` (placement new, `reset()`); the facade's DM getters now answer empty instead of dereferencing a null.
  - `s_msgs` 9,216 B -> PSRAM array in `meshLinkBegin()`.
  - One `allocUiBuffers()` call at boot (PSRAM): `g_dmScratch` 9,472, `g_chatMsgs` 3,584, `g_chats` 3,024, `g_nodes` 4,480, `g_setRows` 5,768, and `refreshNodes()`'s contact copy 5,888 = 32,216 B. The old `sizeof(array)/sizeof(array[0])` counts are now named capacities (a pointer has no element count).
  - The only static arrays left are the small ones (two `MeshContact list[32]` locals in RPTR paths, text buffers); `the_mesh` (28,096 B) stays, as the previous ticket proposed last.
- **Not dropped: TinyUSB `_dfu_ctx` 4,116 B + `_mscd_buf` 4,096 B.** They are not used by this board's USB path (USB-Serial-JTAG, `ARDUINO_USB_MODE=0`), but they come from the precompiled `libarduino_tinyusb.a`: `usbd.c.obj`'s class-init table pulls `msc_device.c.obj` and `dfu_rt_device.c.obj` in unconditionally, so a build flag cannot remove them without rebuilding the core. They are 8,212 B and the target is met without them.
- **Expected internal heap on the device**: EST-356 measured 53,948 B free with Wi-Fi up and BLE off; the move removes 84,520 B of `.bss`, so expect about **138 KB** free before a BLE start (target >= 120 KB). This is an arithmetic expectation from the build, not a measurement: the boot log's `[mem]`, `[wifi] connected` and `[ble] driver up` lines are the real answer.
- **Start guard** (`ble_link.cpp`): before the first Bluedroid allocation it requires `internal >= 81,920 B` **and** `largest internal block >= 8,192 B` (`kStartNeedFree` / `kStartNeedBlock`). A refusal prints `[ble] not started: N B free (largest M B), need 81920 B free and 8192 B largest`, a toast `Bluetooth: N B free, need M B`, and saves the switch OFF so it cannot retry every pass. No panic path. The 80 KB figure is provisional: the EST-356 panic at 53,948 B free is the hard lower bound, and the exact peak is the internal delta on `[ble] driver up ... internal A -> B`; replace the constant with that peak plus margin once Claude has the line.
- **Deferred auto-start**: a saved "on" is not applied while Wi-Fi is `Scanning` or `Connecting`; the guard then measures the coexistence heap, not the pre-Wi-Fi one. A manual `ble on` still measures immediately.
- **Crash guard**: NVS `omble/crashed` is set true *before* `bleTransport().begin()` and cleared after the driver is up (or after a clean init refusal). `bleBegin()` reads it: if it is true, the switch is forced off and saved off, the log says `[ble] previous boot died while starting Bluetooth: switch forced off (guard)` and the toast says `Bluetooth off: the last start crashed`. A new image (`__DATE__ " " __TIME__` in `omble/gbuild`) clears the flag, like the mesh guard. The EST-356 `on=1` therefore cannot boot-loop this image: the worst case is one auto-start, one reboot, then off.
- **Heap printed at all three points**: `[mem] boot (before Wi-Fi, before BLE)`, `[mem] boot (Wi-Fi stack off)`, `[mem] ui buffers ...`, `[wifi] driver up ... internal A -> B, largest ...`, `[wifi] connected ... internal= largest=` (+ `[mem] wifi connected`), `[ble] off at boot ... internal= largest=`, `[ble] driver up ... internal A -> B, largest ...`, `[ble] coexist (BLE on): ... internal= largest=`, and the USB replies `status ... internal= internal_max= heap= psram=`, `ble status ... internal= internal_max= need= need_block= ...`, `wifi ... internal= internal_max=`. `ota.cpp`'s command reply buffer went 256 -> 384 B so `status` cannot truncate its new tail (loop stack is 16 KB).
- **EST-356 features untouched**: PIN (made once, never printed, `pin_set`), page rows, status icon, toasts, `ble on/off/status`, the phone change toasts. The only UI change is the guard's own toast (and "Bluetooth on" is not claimed when the guard refused).
- **Could not check** (no device, no flash, no serial): the real internal heap at boot/with Wi-Fi/with BLE; Bluedroid's real peak (the 80 KB/8 KB constants must be confirmed from the live delta); that PSRAM JPEGDEC decoding still draws map tiles at an acceptable speed; the crash guard end to end (a deliberate panic); the refusal path's toast on the glass.
- **Exact `tools/cmd.py` steps for Claude** (`OM_PORT=/dev/ttyACM0`; `tools/shot.py docs/shots/<name>.png` after the steps that matter). NVS still has `omble/on=1` and the PIN, so the first boot is the guard's first real test:
  1. Flash `firmware.bin` (2,703,584 B). Boot must print `[mem] boot ...`, `[mem] ui buffers ... in PSRAM`, `[ble] new firmware image ... crash guard reset`, `[ble] off at boot: switch=on ... internal=...`. With the saved Wi-Fi: `[wifi] driver up ... internal A -> B, largest ...`, `[wifi] connected ... internal=... largest=...`, `[mem] wifi connected`. Then the deferred BLE start: `[ble] driver up ... internal A -> B, largest ...` and `[ble] coexist ... internal=... largest=...`. If the start panics, the reboot must print the crash-guard line and stay up with BLE off (no loop).
  2. `tools/cmd.py status` -> `internal=`, `internal_max=`, `heap=`, `psram=`; record `internal` with Wi-Fi connected (and BLE up, if step 1 started it).
  3. `tools/cmd.py ble status` -> `switch=`, `state=`, `pin_set=1`, `internal=`, `internal_max=`, `need=81920`, `need_block=8192`; no six digits.
  4. **`ble on` with Wi-Fi connected first** (the ticket): `tools/cmd.py wifi status`, wait for `state=connected`, `tools/cmd.py ble on` (or `ble off` then `on`); record the `[ble] driver up ... internal A -> B` delta, which is Bluedroid's peak need. If it is above ~80 KB, say so and the constant gets replaced.
  5. `tools/cmd.py wifi status`, `tools/cmd.py status`, and `tools/shot.py` the Bluetooth page (`Switch`, `State`, `PIN`) and the status bar icon.
  6. Re-run Claude's EST-356 steps 3-10 (PIN on the page, pairing, phone channel/radio/name toasts, phone-sent message, `ble off`/`ble on` no new PIN, PIN privacy search).
  7. To exercise the refusal: with Wi-Fi connected and BLE off, the serial must show the same fields; a start below the threshold (hard to force now, but possible with `the_mesh`/PSRAM absent) must not panic, only `[ble] not started: ...` and the toast.
- **Next step:** flash and report the four internal numbers (boot, Wi-Fi up, BLE up, BLE delta). If the delta says the guard is too tight or too loose, the constants change in one place; if the map stutters with PSRAM JPEGDEC, that decoder goes back to internal RAM (its 17,884 B would then come from the UI buffers' margin). Then commit as Slofi.


## 2026-11-10 18:30 (Claude): EST-356 + EST-357 on the device: Bluetooth up, with and without Wi-Fi
- Built in the sandbox (exit 0, RAM 136,224 B = 41.6 %, -84,520 B; flash 2,703,221 B), tests exit 0, reviewed binary flashed (2,703,584 B). The NVS switch from the crashed run was still on: the device booted cleanly and BLE started (Filip had Wi-Fi off).
- `ble status`: state=advertising, pin_set=1 (PIN never printed), internal free 113,132 B (largest block 102,388) with BLE up and Wi-Fi off.
- `wifi on` with BLE running: connected; internal 75,196 B free (largest 65,524); `send "Don't Panic" ...` ok, mesh up, history dropped 0. `wifi off` again afterwards (Filip's setting): internal 96,864 B.
- Not yet: the actual phone pairing and the companion protocol against the MeshCore app (Filip).

## 2026-11-10 18:40 (DS): EST-358 RPTR does real work (scan, login, status, admin set)

Built with the REBUILD.md command, **exit 0**: RAM **143,912 B of 327,680 (43.9 %)**, flash
**2,733,297 B of 6,553,600 (41.7 %)**, `firmware.bin` **2,733,664 B**. `sh test/host/run.sh`
**exit 0** (every suite, including the new `test_est358.cpp`). Baseline (EST-357): RAM 136,224 B,
flash 2,703,221 B, bin 2,703,584 B, so this ticket costs **+7,688 B RAM and +30,076 B flash**.

**1. Scan is upstream's own discovery, not a new protocol.** `src/rptr_link.cpp` builds MeshCore's
control-data request itself (`Mesh::createControlData()` + `sendZeroHop()`, the exact body the
companion's `CMD_SEND_CONTROL_DATA` handler builds: type `0x80`, filter `1 << ADV_TYPE_REPEATER`,
random tag, `since = 0`) and listens 20 s. `MyMesh::onControlDataRecv` hands every zero-hop control
packet to the listener (Mesh.cpp does that for `payload[0] & 0x80`), so `mesh_link.cpp`'s `OmListener`
now overrides it and calls `om::rptrOnControlData()`. An answer is one row: public-key prefix, name
from OUR contact table (the answer carries only the key), the SNR the repeater heard us at (`payload[1]`),
the SNR we heard it at (`packet->getSNR()`), the hop count and the heard time. A repeater already in
contacts is marked `saved`. The header says `listening` / `N found, listening` / `N found` /
`none answered`, and the empty state says the request is in flight, so the list is never silently empty.
`FoundList` never grows on a duplicate key, and a stale tag from an older scan is refused.

**2. Login is upstream's `sendLogin`, with the real outcomes.** `BaseChatMesh::sendLogin()` is public,
and the answer arrives at `onUnhandledResponse` (the phone's own `CMD_SEND_LOGIN` path sets MyMesh's
private `pending_login`, so it cannot be reused without editing `lib/`). The answer is decoded by
`om::rptr::loginParse`: current MeshCore (`RESP_SERVER_LOGIN_OK`, `is_admin`, permissions, firmware
level, and the repeater's clock from the tag) and the legacy `"OK"` form. The state machine is
`Idle / Sending / Admin / Guest / Refused / NoAnswer`, with upstream's own `est_timeout` **plus
2,500 ms**; the sheet shows `admin · 1.6 s` with the measured round trip. **A wrong password is
silent on MeshCore v1.18**: `handleLoginReq` returns 0 and the repeater sends nothing, so "refused"
can only be said when a reply arrives that is neither login form; a wrong password therefore reads
"no answer (wrong password, or out of range)". That is the protocol, not a UI shortcut.
The password is typed on the shared keyboard sheet (`KeyboardField::RptrPassword`: printable ASCII,
**15 bytes**, MeshCore's `sendLogin` limit), drawn as dots only. A successful login is remembered per
repeater in NVS (`rptr` namespace, 4 slots, `{name, password}`); it is never logged, never handed to
a reply line and never drawn again. `Forget` removes it. Auto-login (Settings > RPTR, default off)
opens the remembered repeater straight into its admin panel.

**3. Status is upstream's `sendRequest(REQ_TYPE_GET_STATUS)`.** The reply is `RepeaterStats`
(56 bytes, the layout of the repeater's own `MyMesh.h`), decoded in `rptr_model.cpp` with explicit
offsets. The Overview card shows uptime, battery (volts and a 3.0-4.2 V percentage), airtime, packets
rx/tx, noise floor, last SNR, firmware level (from the login answer) and the repeater's clock (also
from the login answer, `HH:MM UTC`), with a `Read all` refresh row. Before the first answer the card
says `not read yet, tap Read` instead of zeros.

**4. The admin set sends exactly the mock's CLI lines.** Every control goes through
`BaseChatMesh::sendCommandData(..., TXT_TYPE_CLI_DATA, line)` (the companion's text-CLI frame), and
the repeater's answer comes back as `TXT_TYPE_CLI_DATA` and is queued for the strip under the rows
(`> set radio 869.525,62.5,8,5` / `OK - reboot to apply`). The lines are built in `rptr_model.cpp`
and the host test compares them with the mock's own strings: `set radio 869.525,62.5,8,5` +
`set tx 14` (Apply; steppers are local until Apply, as the mock does), `region allowf/denyf <name>`,
`region home`, `region default`, `region save`, the 7-command SI chain, `set path.hash.mode 0|1|2`,
`set loop.detect off|minimal|moderate|strict`, `set advert.interval`, `set flood.advert.interval`
(never 1-2 h), `set flood.max`, `set repeat on|off`, `set powersaving on|off`, `gps advert prefs|none`,
`set name`, `set owner.info`, `set lat <5dp>` + `set lon <5dp>`, `advert.zerohop`, `advert`,
`clock sync`, the 4-line silent and resume chains, and `reboot`. Regions are read with the repeater's
own `region` reply (`RegionMap::exportTo` text) and neighbours with `neighbors`
(`hex:secs:snr4` lines, names resolved from our contact table, heard shown as a clock time).
`discover.neighbors` is the Discover action. Two-step confirm (`Confirm`, 6 s) for **reboot** and
**silent mode**; the mock has no factory-defaults control and the repeater's `erase` is serial-only
(`sender_timestamp == 0`), so no such command is offered: a remote factory reset does not exist.

**5. USB for Claude:** `rptr scan` (starts a scan and prints the rows known so far; run it again after
~20 s for the answers) and `rptr status <name>` (uses the remembered password when the session is not
on that repeater, otherwise asks for the status and prints it when it is queued). Neither prints a
password: the reply says `login=sent` at most. `rptr`, `rptr password ...` and extra words are refused
by the parser (host-tested).

**Host tests prove** (`test_est358.cpp`): every CLI line above (including the mock's exact
`set radio 869.525,62.5,8,5`, `region allowf lj`, `region home lj`, `set name rptr`,
`set lat 46.25000`, `discover.neighbors`, the 7 SI commands and the 4 silent/resume lines), the
bands' formatting (`62.5`, `125`, `31.25`), the stepper clamps (freq 300-2500, SF 5-12, CR 5-8,
TX 1-22, local 0-240, flood 0/3-168, flood.max 0-64 step 4); the password filter (printable ASCII,
15 bytes, control bytes dropped, exact truncation at the buffer edge); the discovery list (dedupe,
serial, the 20 s window closing once, a full list refusing the 17th, the state texts); the login state
machine (deadline = estimate + 2,500 ms, RTT, refused, no answer, a millis() wrap) and both answer
shapes; the status decode and every text helper; the neighbour and region parsers; the two-step
confirm; the admin row table for all six views (labels, kinds, ids and the region rows' H/D keys); the
admin layout (chips inside the width, body above the answer strip, row/part hit test under scroll);
and the `rptr` USB command.

**Could not check** (no device, no flash, no serial): that a real repeater answers the zero-hop
DISCOVER request on the SLO mesh and how many answers arrive in 20 s; that the login answer reaches
`onUnhandledResponse` (the code path is the one the ping already uses, but a login is an ANON_REQ with
a PATH return, not the same packet shape as the ping's RESPONSE); that the repeater accepts our CLI
lines (the strings are the mock's and the repeater's own `handleSetCmd`/`handleRegionCmd` were read,
not run); that the status struct offsets match the SLO repeaters' firmware; the real screen (the admin
panel shows 3 rows at a time on 320x240, the rest scroll); the region export's exact text on the
device; and the internal-RAM headroom with Wi-Fi + BLE after this ticket's +7,688 B of statics (the
new `RptrView` is ~3.3 KB of .bss; if `ble status` shows less than ~60 KB free with both radios up,
that view is the first thing to move to PSRAM).

**Next step:** flash, then: RPTR > Scan and watch `[rptr] scan: discover request tag ...` and one
`[rptr] answer <name>` line per repeater; tap one, type Filip's password, and check `[rptr] login ...
admin (N ms)`; the Overview card against the repeater's own `ver`/`stats`; then Regions > Read,
Neighbours > Read, and one control per view, each answered in the strip and in `[rptr] cli ->` /
`[rptr] cli <-`. Then `rptr scan` and `rptr status "<name>"` over USB.


## 2026-11-10 18:30 (Claude): EST-358 on the device
- Built in the sandbox (exit 0, RAM 143,912 B, flash 2,733,297 B), tests exit 0, reviewed binary flashed (2,733,664 B). Internal free 168,656 B (BLE off by Filip).
- `rptr scan`: discover request, two answers within seconds: `86a8eedb744b` (no name) SNR in 11.0 / out 11.75 dB and `Argus mobile RPTR` SNR 13.25 / 8.25 dB, both direct. The RPTR tab lists them, "saved" on the one in contacts (`docs/shots/2026-11-10-1829-rptr-scan.png`). Login, status and the admin set need Filip's repeater password: Filip.

## 2026-11-10 18:45 (DS): EST-359 better sounds: a small synth, one sound per event

Built with the REBUILD.md command, **exit 0**: RAM **143,952 B of 327,680 (43.9 %)**, flash
**2,738,545 B of 6,553,600 (41.8 %)**, `firmware.bin` **2,738,912 B**. `sh test/host/run.sh`
**exit 0** (every suite, including the new `test_est359.cpp`). Baseline EST-358: RAM 143,912 B,
flash 2,733,297 B, bin 2,733,664 B, so this ticket costs **+40 B RAM**, **+5,248 B flash** and
**+5,248 B bin**. At runtime the player also takes a **12,800 B PSRAM** phrase buffer, a 3-slot
queue and a **4,096 B task stack** (internal heap, not .bss): `[sound] player up ... in PSRAM`.

**1. Synth or samples: I chose the synth, and this is the comparison.** The 9 events x 3 variants
at 16-bit / 16 kHz are **6,035 ms = 193,120 B (189 KB)** of PCM if stored in flash. The whole synth
plus player costs **5,248 B of flash**. The synth also gives the picker its 27 variants, follows the
volume setting, needs no recorded/licensed assets, and the same table is what the host test and
`tools/mksounds.cpp` exercise, so a WAV listened to on the laptop is the waveform the device streams.
A small L2 Pro speaker would not reveal the realism 189 KB of samples would buy. If a future voice
really needs a recording, the `play` path and the per-event picker stay; only `sound_synth.cpp` is
replaced.

**2. One voice, one table (`src/sound_synth.h/.cpp`, plain C++).** 16-bit mono at the codec's
16 kHz. Every note is 3 sine partials through an ADSR envelope: soft smoothstep attack **3-8 ms**,
exponential decay to a low sustain, a **16-30 ms** release that reaches exactly zero. Voices:
Bell (inharmonic 1 / 2 / 2.76), Marimba (the bar's 3.93 fourth partial), Pluck (near-harmonic),
Chime (octaves) and Soft (quiet octaves). One gentle one-pole low-pass at **6.5 kHz** over the
phrase, a 3 ms final ramp to zero, and partials above 7.2 kHz are skipped so nothing aliases on this
16 kHz path. Headroom is guaranteed twice: a per-recipe worst-case bound sets the render scale, and
the measured peak is scaled down only if it exceeds **28,000** (14.5 % below full scale, never up),
so no event can clip at maximum volume. The phrase is rendered into PSRAM before the I2S write.

The event table (all lengths include the release and tail):

| event | variants | default | lengths |
|---|---|---|---|
| boot | Bell TK-TK / Chime TK-TK / Soft TK-TK | Bell TK-TK | 195-205 ms |
| message | Chime / Bell / Marimba | Chime | 210-260 ms |
| dm | Warm / Soft / Basso | Warm | 300-340 ms |
| mention | Bright / Bell / Glass | Bright | 200-240 ms |
| sent | Tick / Blip / Tock | Tick | 80-95 ms (deliberately very short) |
| error | Low / Warn / Bell | Low | 300-320 ms |
| newnode | Soft / Marimba / Bell | Soft | 220-250 ms |
| battery | Low / Alert / Bell | Low | 360-380 ms |
| touch | Tick / Tock / Click | Tick | 30-40 ms (very short, off by default) |

Every event also has an **Off** row; its stored value is `variantCount(ev)`. Boot keeps Filip's
"TK-TK" character exactly: **D7 2349 Hz, a 55 ms silent gap, F7 2794 Hz**; the three variants change
only the voice, not the phrase (host-tested by correlation, not by eye).

**3. One sound per event, and where each trigger lives.** `boot` in `setup()` when `Welcome sound`
is on; `message` from the new receive-only `meshMsgSoundSerial()` (not our own send, not a muted
channel); `mention` from `meshMentionSerial()` when the received text names us as `@Name`
(case-insensitive, so the reply path's own `@Name` counts); `dm` from `meshDmRecvSerial()`; `sent`
from a successful UI channel/DM send; `error` from a new radio fault (radio/mesh/TX) and a failed UI
send; `newnode` from `meshNewNodeSerial()`; `low battery` once when the estimate first reaches
**<= 15 %**, re-armed above **20 %**; `touch` from `playClick()` only when `Touch clicks` is on.
When a mention and a normal message change in the same loop pass, mention wins and the message serial
is consumed too, so one message never plays two sounds.

**DND choice:** `Do not disturb` silences every sound **except error and low battery**, which the
ticket names (the table flag `dndExempt` is host-tested). `Master sound` off silences all nine,
including those two. The picker preview bypasses DND (an explicit user action) but not Master sound.
The USB `play` command bypasses both because it is the reviewer's diagnostic. Volume is the existing
`g_volume`, written to the codec before every play.

**4. Settings picker.** Sound has one row per event (Boot, Message, Direct message, Mention, Sent,
Error, New node, Low battery), then Touch clicks, Click tone, Do not disturb, Welcome sound. Each row
opens that event's own list: its 3 variants + Off. Tapping a variant plays that event's sound with
the current volume; `Use this sound` saves it back to the Sound page or the Chat page it came from.
Per-event NVS keys are `boot`, `msg`, `dm`, `men`, `sent`, `err`, `newn`, `batt`, `tone`. Migration
from the old 17-chime table: old **17 (Off)** becomes Off here, any other old value that is out of
range for the new event falls back to that event's default (0). Touch clicks now default **off** for
a fresh device; an explicitly saved `tsnd=true` is kept.

**5. WAVs for the listening check.** `tools/mksounds.cpp` compiles `src/sound_synth.cpp` on the host
and writes **27 WAV files** (16-bit mono 16 kHz) to `docs/sounds/`, named
`<event>-<variant>-<name>.wav`. Build/run:

```
g++ -std=c++17 -O2 -I src tools/mksounds.cpp src/sound_synth.cpp -o /tmp/mksounds
/tmp/mksounds docs/sounds
```

It exits non-zero on a clipped, empty or wrong-length render. Measured peaks (of 32,767 full scale)
and lengths, all well under the 28,000 target:

| file | ms | peak | | file | ms | peak |
|---|---|---|---|---|---|---|
| boot-0-bell-tk-tk | 205 | 11,127 | | error-0-low | 300 | 12,332 |
| boot-1-chime-tk-tk | 195 | 11,401 | | error-1-warn | 310 | 12,268 |
| boot-2-soft-tk-tk | 205 | 12,295 | | error-2-bell | 320 | 12,173 |
| message-0-chime | 240 | 13,483 | | newnode-0-soft | 250 | 12,238 |
| message-1-bell | 260 | 17,062 | | newnode-1-marimba | 230 | 13,706 |
| message-2-marimba | 210 | 15,184 | | newnode-2-bell | 220 | 13,202 |
| dm-0-warm | 300 | 13,125 | | battery-0-low | 380 | 13,190 |
| dm-1-soft | 300 | 16,711 | | battery-1-alert | 380 | 8,428 |
| dm-2-basso | 340 | 13,490 | | battery-2-bell | 360 | 12,477 |
| mention-0-bright | 220 | 11,490 | | sent-0-tick | 80 | 8,736 |
| mention-1-bell | 240 | 12,751 | | sent-1-blip | 90 | 7,713 |
| mention-2-glass | 200 | 11,312 | | sent-2-tock | 95 | 7,831 |
| touch-0-tick | 35 | 2,552 | | touch-1-tock | 40 | 4,266 |
| touch-2-click | 30 | 1,733 | | | | |

**6. The player (`audio.cpp`).** One task pinned to core 0, priority 3, 4 KB stack, drains a
3-slot queue. For each request it renders the phrase into the one PSRAM buffer, then streams it with
`i2s_write`. The loop only does `xQueueSend(..., 0)`: it never renders a sine, never waits for a
phrase and cannot be blocked by the DMA. A full queue drops the newest request and counts it; old
chimes never stack up. Each play logs `[sound] play event=... vN name="..." ms=... samples=... peak=...
render_us=...`, so the real on-device render cost is visible. The old blocking `audioTone()` is gone;
`mesh_link` no longer plays audio on the receive path.

**Host tests prove** (`test_est359.cpp`): the 9 events, their exact keys, 2-3 variants each, unique
names, valid defaults, Off not a playable variant and the `dndExempt` flags (only error and low
battery); every one of the 27 renders has the table's exact length (30-380 ms, 100-400 ms except the
deliberately short Sent/Touch), starts at 0 or +/-1 LSB and ends at exactly 0, has 1,000 < peak
<= 28,000, never touches full scale, and `RenderInfo.peak` equals the measured peak; the boot
envelope (RMS 1,525 in 0-2 ms, 6,665 in 4-6 ms, 5,382 in 20-40 ms, 2,476 in 40-60 ms, **exactly 0**
in the 75-105 ms gap, the second note audible by 118 ms), and the D7/F7 correlation (D7 > 3x F7 in
the first note, F7 > 3x D7 in the second, for all three boot variants); bad event/variant/small/null
render arguments refuse cleanly; and the USB command (`play message`, `play dm 2` accepted, every
table key accepted, `play boot 3` / `play message 9` / bare `play` / `play nope` / non-numeric
variant / extra word all refused with the right error, and `status` still parses).

**7. USB for Claude:** `play <event> [variant]`. Events: `boot`, `message`, `dm`, `mention`,
`sent`, `error`, `newnode`, `battery`, `touch`. Without a variant it plays that event's default.
Examples: `tools/cmd.py play boot`, `tools/cmd.py play boot 2`, `tools/cmd.py play dm 1`. The reply
is `[cmd] ok play event=... variant=N name="..." ms=... queued=1 played=... dropped=...`; the
device serial then shows the `[sound] play ... render_us=...` line. It does not change any setting.

**Could not check** (no device, no flash, no serial): that the ES8311 path actually plays these
waveforms and how they sound on the small speaker; the real `render_us` on the ESP32-S3 (host
rendering is ~2 ms for the longest phrase, the device number is the useful one); that the PSRAM
buffer allocation and the core-0 task behave under Wi-Fi/BLE load; the queue under a burst of
messages; the touch default-off and the per-event pickers on the glass; mention detection on live
traffic (the `@Name` rule is host-side only); the low-battery threshold on a real cell; and every
WAV by ear.

**Next step:** flash `firmware.bin` (2,738,912 B), confirm `[audio] ok` and `[sound] player up ...
in PSRAM`, then `play boot`, `play message`, `play dm`, `play mention`, `play sent`, `play error`,
`play newnode`, `play battery`, `play touch` (and each variant) over USB, and record `render_us` and
what the speaker does. Open Settings > Sound and tap every variant. Listen to `docs/sounds/` first;
if a voice is thin or a phrase is too long, the one place to change is `src/sound_synth.cpp`'s
`kVoices` / `k*Variants`, then re-run the host test and `mksounds`.

## 2026-11-10 19:35 (Claude): EST-359 on the device
- WAVs checked first (27 files, docs/sounds): 30..380 ms, peaks 1,733..17,062 of 32,767, 0 clipped samples, every file starts and ends at 0 (no clicks), DC offset within 10 LSB.
- Built in the sandbox (exit 0, RAM 143,952 B, flash 2,738,545 B), tests exit 0, reviewed binary flashed (2,738,912 B). `play message`: played (Chime, 240 ms). How they sound on the L2's speaker: Filip.

## 2026-11-10 19:30 (DeepSeek): EST-360 regions, nearby reads, clock sync, status offset, volts

**The two field bugs first.** The status card was reading every field four bytes late because of two
strips, not one: `MyMesh::onContactResponse` already hands the Listener `&data[4], len - 4` (the 4
bytes are the tag/timestamp), and `statusParse()` skipped another 4. Battery therefore read the noise
floor (`0xFF88` = -120 dBm shows as 65416 "mV"), noise read the low half of `n_packets_recv`, and so
on. `statusParse()` now starts at `buf[0]` (batt_milli_volts), requires `len >= 56` and accepts longer
replies from newer firmware. The listener's length check became `len >= 56`, not `4 + 56`. The login
answer path was checked too: it was already correct (`buf[0] == RESP_SERVER_LOGIN_OK`, the clock is
the separate `tag`); a host case now feeds a buffer that still has a tag in front and proves it is not
parsed as a login. The first status reply after boot is logged once as
`[rptr] status raw (56 B): ..hex..` for Claude to compare with the upstream struct.

**The SI preset is gone.** Button, `RowId`, `kSiPreset`, its seven strings and its tests were removed.
Nothing sends a hard-coded sub-region any more.

**Regions are manual, one labelled action each, with one confirm step.** The Regions view now has:
Read regions (`region`, then `region home`, then `region default`), Add region (the shared keyboard,
`RptrRegion` field, MeshCore's own rule), Save to repeater (value "makes changes permanent", sends
`region save`), Home and Default scope fact rows, "Home: * (no region)" (`region home *`) and
"Default scope: none" (`region default <null>`), then one row per region (label, the `^ home` marker,
allow/deny, a `>` key). A region row opens its own page: Add child region (`region put <new> <name>`),
Allow flood (`region allowf <name>`), Deny flood (`region denyf <name>`), Set as home
(`region home <name>`), Set as default scope (`region default <name>`), Remove region
(`region remove <name>`, red). No H/D letters anywhere. Every one of those taps only ARMS the line:
the top of the view becomes "Send now" with the exact CLI line (80-byte row) and Cancel; the second
tap sends it. After a home change the device reads `region home` back (and refreshes the tree), after
a default change it reads `region default` back, after add/remove/allow/deny it reads `region`.
The screen now shows the repeater's own answer, not what this device remembers. Checked in CommonCLI:
no-argument `region home` answers " home is X", no-argument `region default` answers " default scope is
X" or "<null>", `region default <null>` clears it, `region home *` sets the wildcard (home_id 0, the
clean "no home"), and `region remove` exists (`findByName`, exact). The region-name rule is
`RegionMap::is_name_char`: '-' '$' '#' digits or any byte >= 'A' (so '['..'`', lowercase and '_' are
legal, while space, '.', '/', ':' and '@' are not); the keyboard calls the same constexpr function, so
it cannot offer a byte the repeater refuses.

**Nearby regions, anonymous, no login.** A new "Regions" chip in the scan header asks every direct
responder of the last scan, one at a time. The mechanism is the phone app's (OverMesh
`_mc_query_responder_regions`): after the 0-hop discovery, a raw ANON_REQ with payload
`[0x01 (ANON_REQ_TYPE_REGIONS), 0x00 (reply path length)]` over a zero-hop DIRECT route; the repeater
answers `clock(4 LE) + comma-separated names` ('*' = unscoped). Only a 0-hop responder is asked,
because the reply is a zero-hop direct back (reply-path-len 0). The response is matched by the
sender's full key and the first clean reply wins; the Sent-ack tag is deliberately not used (OverMesh's
firmware quirk: it does not match the BINARY_RESPONSE tag). A repeater not in our contact table gets
MeshCore's own temporary anon slot so the mesh can decrypt the reply. The scan row's second line
becomes `regions si, lj · clock 14:02 UTC` (`none` is a real empty answer). A login cancels any query
in flight, so a stale regions reply can never be mistaken for a login answer. Note the repeater's own
`anon_limiter` allows 4 anonymous requests per 3 minutes; more than four repeaters in the sweep means
the rest wait for the next round.

**Clock sync.** Actions > "Sync clock" sends `time <our epoch>` (never `clock sync`: OverMesh found
that clock sync cannot move a repeater whose clock is 0, while `time` can, and both refuse to go
backwards). The row shows the exact line it will send. The guard refuses when our clock is not real
(`timeKeeper().realSinceBoot()` false or epoch before 2020), when our clock is more than 86400 s ahead
of the repeater's known clock, and when the repeater is more than 60 s ahead of ours (a backwards
`time` is refused by the repeater anyway). If the repeater's clock is 0/unknown, the guard allows the
send, which is the case it exists to fix. The Actions view has "clock before" and "clock after" rows:
before is the last known repeater clock (login answer or an earlier `clock`), the send is followed by
`clock`, whose reply text ("14:02 - 10/11/2026 UTC") is parsed back to UTC and shown with the
difference to ours ("3s behind", "in sync").

**Battery in volts.** The repeater's battery row and our own (Settings > About) show "4.12 V". A value
outside 2.5..30 V or 0 shows "n/a"; for the repeater the row's detail line adds the raw mV ("n/a
(65416 mV)"). The raw mV for our own cell stays on the serial `[batt] N mV` line. The status bar still
shows its icon + percent (the mock's layout); the numeric displays are the two V rows.

**Build and tests.** Sandbox build exit 0: RAM **148,368 B** (45.3%), flash **2,752,545 B** (42.0%),
`firmware.bin` **2,752,912 B** (was 143,952 / 2,738,545). `test/host/run.sh` exit 0. New
`test/host/test_est360.cpp` proves: the region-name rule and filter (every ASCII byte checked against
the model rule), every new CLI line verbatim (`region put` with and without parent, refused bad
names, allowf/denyf, home, default, remove, `region home *`, `region default <null>`, `time <epoch>`),
the anonymous reply parser (clock + list, '*', a real empty 4-byte answer, NUL-terminated, garbage
dropped, short refused, 12-name cap), the time guard (our clock unset refused, exactly 86400 s ahead
allowed and 86401 refused, the repeater-ahead rule), `clockReplyParse` against real epochs,
`batteryVoltsText`/`batteryRawText`, and the keyboard field (same rule as the model, 30-byte cap, no
space/emoji/accent, `#`, `$`, '-' on the 123 page). `test_est358.cpp` was updated: the status test now
feeds exactly the bytes the Listener hands over, built from upstream's `RepeaterStats` struct with
`static_assert(sizeof == 56)` and offset checks (battery 4120 mV, noise -112, packets 290 ...); a
longer reply is accepted, a 55-byte one refused; the admin-row table, the `>` Open key and the Regions
chip are covered.

**Could not check** (no device in the sandbox): the anonymous read on the air (does a real repeater
answer our zero-hop ANON_REQ, how long it takes, the 4-per-3-minutes limiter, and whether MeshCore
resolves the response sender without a named contact); the actual bytes of a real status reply against
the logged hex; `time <epoch>` on Argus and its `clock` read-back; the `region default` no-argument
reply on a real firmware build; the touch feel of the new region page and the confirm rows; and
whether `region put` with a long parent line fits the CLI frame.

**Next step for Claude (USB):**
1. `tools/cmd.py status` still parses; watch the boot log for `[rptr] ready`.
2. On the RPTR tab tap Scan, wait for the 20 s window. Then `tools/cmd.py rptr scan` prints one
   `[rptr] row ...` line per responder (name, key, key_bytes, hops, regions "?", clock 0).
3. Tap the new Regions chip (or `tools/cmd.py rptr regions`). Watch the serial for
   `[rptr] regions -> ...` and `[rptr] regions <- ...: clock=... regions="si, lj" count=... clean=...`.
   Then `tools/cmd.py rptr regions Argus` (or the 3-byte hex shown in `rptr scan`) prints the stored
   answer: `[cmd] ok rptr regions name="..." clock=... regions="..."`.
4. Log in to Argus, open Overview and confirm the battery is about 4.1 V (not 65416 mV) and the noise
   floor about -120 dBm; send the one-time `[rptr] status raw (56 B)` line with the hex.
5. Regions page: Read regions (Home and Default scope should show the repeater's answers), then tap
   Set as home on a region and check "Send now: region home <name>" and the read-back toast; Add
   region, Save to repeater; the Actions page Sync clock should show `time ...`, then before/after and
   the `clock` reply.


## 2026-11-10 19:58 (Claude): EST-360 on the device
- Built in the sandbox (exit 0, RAM 148,368 B, flash 2,752,545 B), tests exit 0, reviewed binary flashed (2,752,912 B).
- `rptr scan`: Argus answered (Trnovo II did not this time). `rptr regions "Argus mobile RPTR"` (anonymous, no login): `clock=1715779092 (real) regions="*, si, lj"`. **Argus's clock reads 2024-05-15 13:18 UTC**: it is the MeshCore 2024 restart clock, so "Sync clock" is needed there. Small defect: the reply was labelled "(real)" although a 2024 stamp is below the 2025-01-01 cut-off; use the same kMinSaneEpoch rule.
- The status card in volts, login and clock sync need Filip's repeater password: Filip.

## 2026-11-10 19:34 (DeepSeek): EST-361 sounds round 2, one shared library, clock label

**One shared library of 54 sounds, six per event, every note sequence different.** `sound_synth.cpp`
now has a flat library with stable ids (0..53), a family (Bells, Chimes, Marimba, Soft, Alerts, Ticks)
and a home event; each event's own set is six of them. No two library sounds share an ordered
(Hz, start, duration) sequence, so the old boot problem cannot come back: the three old boot variants
were the same D7/F7 notes in three voices, and now every boot choice is a different motif. One sound
is still the original TK-TK (D7, a 55 ms gap, F7).

**Message and new node are the same family, a fourth apart.** Message's six sounds are the old
new-node timbres transposed up a fourth (and a fifth): Soft Hi 880/698 Hz is the default, plus
Marimba Hi, Bell Hi, Soft High Fifth, Marimba High Fifth, Bell High Fifth. New node's six are the
same voices down a fourth/fifth: Soft Lo 494/392 Hz is the default, plus Marimba Lo, Bell Lo and the
two low fifths. Every message-home fundamental is above every new-node-home one (880 vs 784 is the
closest pair).

**Low battery is 1.35 s and at the cap.** It is a three-note descending figure (440, 349, 262 Hz)
played twice, with just enough overlap to be loud, and it has its own target peak: the renderer
normalises it to 27,000 of 32,767 (not clipping). All six battery sounds are 1.25..1.4 s and at the
same target. DND still lets error and low battery through. The other sounds keep the old
"normalise down only" rule, so quiet ticks stay quiet. The per-instant amplitude bound replaced the
old whole-phrase sum, which was what made any multi-note phrase shy.

**Choices are library ids, and EST-359 choices migrate.** NVS now has `sndver=2`; the event keys
(`boot`, `msg`, ...) hold a global sound id, with `0xFF` for Off. A pre-2 value is read as the old
per-event index and mapped by `legacyVariantSound` (0..2 -> the closest new sound, 3/17 -> Off), then
written back as ids once. The picker lists all 54 sounds in six family sections, marks the current
choice, plays on tap, and shows the other events using a sound as a small dim tag (`MSG`, `DM`,
`MENT`, `SENT`, `ERR`, `NODE`, `BATT`, `TAP`, `BOOT`) with no prefix; a tagged sound stays
selectable. The settings row value cap grew to 64 so the tags fit; the Check row now measures the
tag string and clips the name instead of overlapping it. `play <event> [sound-id]` accepts any library
id for any event; the reply says `sound=...`.

**Repeater clock.** The anonymous regions parser, the login answer, the status card and the sync
guard now use the same 2025-01-01 cut-off (`kClockRealSince = om::kMinSaneEpoch`). A 2024 stamp is
not "real": `clockText` prints `not set (2024)` (and the year for any 2020..2024 value), so Argus's
2024-05-15 MeshCore restart clock points straight at Sync clock. The serial regions line now prints
`clock=1715779092 (not set (2024))` instead of `(real)`.

**WAVs and INDEX.** `tools/mksounds.cpp` re-rendered `docs/sounds/`: 54 files named
`<event>-<n>-<name>.wav` plus `docs/sounds/INDEX.md` with file, event, n, global id, family, name,
notes (Hz@at+dur), length and peak. Claude's listen check method, run here with numpy: every file is
1 ch, 16 bit, 16 kHz; 35..1400 ms according to the table; peaks 2,399..27,000 (never 32,767);
every file starts at 0 and ends at exactly 0; the dominant FFT peak matches the recipe's first note
(e.g. boot TK-TK shows 2,794 Hz, message Soft Hi 700 Hz, new node Soft Lo 392 Hz, battery Low 261 Hz).
The four boot examples are clearly different: TK-TK (D7/F7), Bell Pair (784/1047), Soft Rise
(440/554), Chime Arp (523/784/1047).

**Build and tests.** Sandbox build exit 0: RAM **148,368 B** (45.3%), flash **2,757,061 B** (42.1%),
`firmware.bin` **2,757,424 B** (was 148,368 / 2,752,545). `test/host/run.sh` exit 0.
`test_est359.cpp` (now the EST-361 test) proves: 54 sounds, >=6 per event, unique names, no duplicate
note sequences globally or per event, every default is one of its event's sounds, message default
"Soft Hi" (880 Hz) is above new-node default "Soft Lo" (494 Hz) and every message-home sound is above
every new-node-home one, boot is still D7-gap-F7 by correlation, all lengths in range (battery
1.0..1.5 s, ticks 20..130 ms, others 100..400 ms), every render's peak equals `RenderInfo.peak`, no
clipping, first sample 0/+-1 and last exactly 0, battery peak >= 25,000, the EST-359 migration table
(including all three old boot choices -> TK-TK), and the USB `play <event> [sound-id]` accept/refuse
set. `test_est358.cpp` and `test_est360.cpp` were updated for `not set (2023)` / `not set (2024)` and
the 2025 boundary; all other host tests still pass.

**Could not check** (no device, no flash, no serial): how the 54 sounds actually sound on the L2's
small speaker and whether the battery warning is too loud through the ES8311 path; the 63-row
picker's scroll and the dim tags on the glass; the one-time NVS migration on a device that really
has EST-359 values; `play <event> <id>` over USB on the device; `render_us` for the 1.35 s battery
phrase on the ESP32-S3 (host rendering is a few ms; the longer buffer is 48 KB of PSRAM).

**Next step for Claude (USB):** flash `firmware.bin`, check `[sound] player up: 24000 samples
(48000 B) in PSRAM`, then `tools/cmd.py play boot`, `play message`, `play newnode`, `play battery`
(and `play battery 43..47`); the serial line is `[sound] play event=... sound=N name="..." ms=...
peak=... render_us=...`. Open Settings > Sound > Message and confirm the family sections, the dim
tags and the selected dot; pick one sound for two events (double duty) and confirm both rows show it.
On the RPTR tab, `rptr regions "Argus mobile RPTR"` should log `(not set (2024))`, and Sync clock
should now be allowed for it. Filip still listens to every WAV first.

## 2026-11-10 20:45 (Claude): EST-361 on the device
- 54 WAVs checked (0 clipped, edges at zero, none silent); INDEX.md lists notes per file: every boot variant now has its own notes; low battery is a 1.35 s two-pass falling figure. Built in the sandbox (exit 0, RAM 148,368 B, flash 2,757,061 B), tests exit 0, reviewed binary flashed (2,757,424 B), mesh up. Listening: Filip.

## 2026-11-10 20:05 (DeepSeek): EST-362 RPTR redesigned: the list, one page, one Apply, a pop-up, a Console

**The interaction is the ticket's, and the old one is gone.** No more six chips, no "Send now", no
"Save to repeater", no bare H/D keys, no per-control CLI hint, no CLI line shown as a row, no SI
preset (it was already gone), no separate confirm page. The RPTR tab is the list; a row opens one
page; every setting has ONE Apply that sends and makes permanent in the same step; risks are a
pop-up over the page; raw commands and answers exist only in the Console.

- **List.** Opening the tab sends the scan by itself (`showTab` calls `rptrScanStart`; the header's
  Scan is the rescan). Rows are the remembered repeaters first with a small gold key, then the scan
  answers; an unnamed answer is "unnamed 86a8ee" (first 3 key bytes); the second line is the signal
  in words and dB ("good 11 dB", the better of the two directions) plus the heard time.
- **Repeater page, before login.** The page fills itself with what needs no password: Regions (the
  anonymous read, "* , si, lj" or "none"), Clock ("2026-11-10 19:05 UTC", "not set (2024-05-15)"
  for the MeshCore restart clock) and Signal. One big Log in button. A remembered password logs in
  by itself; Forget password sits lower on the page.
- **Repeater page, logged in.** A status card (battery "3.59 V 49 %", noise floor, uptime, airtime,
  clock) with a refresh icon; a "Clock is wrong: <date>" line with one Set clock button when the
  clock is not set or off by more than a minute; then the plain list Name, Position, Radio, TX power,
  Regions, Neighbours, Advertising, Repeat on/off, More, Restart repeater, and Console at the very
  bottom. No CLI text anywhere on these pages.
- **One Apply per page.** Name, Position, Radio, TX power, Repeat, Advertising, More, Regions, Add
  region and the two pickers each have exactly one Apply. It sends the exact CLI lines (unchanged
  from EST-358/360) and, on the Regions page, appends `region save` in the same step, because the
  region map is the only thing that needs it (every `set ...` handler in CommonCLI.cpp calls
  savePrefs itself). The result is a drawn tick and "Applied", or the repeater's error in plain
  words; the page then shows the value read back from the repeater (`get name`, `get radio`,
  `get tx`, `get repeat`, `get advert.interval`, `get flood.advert.interval`, `get flood.max`,
  `get loop.detect`, `get path.hash.mode`, `region`, `region home`, `region default`, `clock`).
- **Confirm only what is risky, as a pop-up.** Radio ("The repeater will drop off the mesh until it
  restarts with the new radio settings."), Restart, Repeat off, removing a region, and a clock jump
  over a day. Two buttons, Cancel and the verb; a tap outside cancels and is consumed. Name,
  Position, TX, Advertising, More and the region checkboxes apply directly.
- **Clock guard fixed as in C.** `clockGuard` refuses only when our clock is not real or the repeater
  is ahead by more than a minute; a jump over a day is allowed after a confirm that shows both dates
  ("Set the repeater's clock from 2024-05-15 13:18 to 2026-11-10 19:00 UTC?"). The old
  `timeSyncCheck`/`TimeVerdict` (which refused a big jump) is removed. `clockStampText` prints the
  full date and keeps "not set (2024-05-15)" for a 2024 stamp.
- **Regions page.** The repeater's regions as checkboxes "Repeats floods here" (tap = draft), a small
  x key with the remove confirmation, Home region and Default scope as normal rows opening pickers
  ("* (anywhere)", "none", the named regions), and Add region with a name and an optional parent
  (the Parent row cycles none and the regions). Everything is one Apply.
- **Console.** A row at the very bottom of the logged-in page. The raw log (" > line", "< answer")
  and a free command line with Send. Log lines are not rows at all, so an answer can never be tapped;
  no other page carries CLI text.

**The model is host-testable and separate.** New `src/rptr_page.h/.cpp`: the pages, the rows, the
Apply plan, the read-back lines, the confirmation rules, the list order and the signal words, with no
Arduino and no LovyanGFX. `rptr_model.h/.cpp` keeps discovery, login, status, parsers and the CLI
builders, and gains `clockGuard`, `clockStampText`/`clockJumpText` and the CommonCLI read-back
parsers (`valueReplyParse`, `intReplyParse`, `radioReplyParse`, `coordReplyParse`,
`switchReplyParse`, `loopReplyParse`, `answerOk`, `answerPlainText`). The old `adminRows`, `RowId`,
`RowKind`, `AdminRow`, `viewName` and `Confirm` are removed. `ui/rptr_layout.*` and
`ui/screen_rptr.*` are rewritten for the list, one page, the pop-up and the Console; `main.cpp`'s
RPTR section is rewritten around a one-line-at-a-time command queue (a lost line times out after
3 s and becomes an error, never a hang).

**Build and tests.** Sandbox build exit 0: RAM **152,272 B** (46.5%), flash **2,764,177 B** (42.2%),
`firmware.bin` **2,764,544 B** (was 148,368 / 2,757,061 / 2,757,424). `test/host/run.sh` exit 0. New
`test_est362.cpp`: the page model before login (3 facts + one big Log in, nothing tappable) and after
it (5 status cells and the 11 rows in order, Console last, Restart dangerous), every Apply plan
verbatim (Name/Position/Radio/TX/Repeat/Advertising/More, the Regions one with allowf/home/default/
put and exactly one `region save` at the end, both pickers, Add region), the clock guard (our clock
unset, 60 s ahead allowed, 61 s ahead refused, 86400 behind direct, 86401 asks, 2024 and 0 ask), the
two dates in the jump sentence, the confirm table (Radio, Restart, Repeat off, Remove; not Name,
Position, TX, Advertising, More, Regions, Repeat on), the Console (6 log lines, no rows, a copied
command line, no "OK" or ">" value on the Regions rows), the read-back parsers and the plain-word
results, the list order/unnamed/signal words, and the layout (one Apply, the pop-up's Cancel/Confirm,
outside = a consumed Cancel). `test_est351` was updated for the new layout rectangles, `test_est358`
dropped the removed row-table/confirm tests, `test_est360`'s guard block now tests `clockGuard`.

**Could not check** (no device in the sandbox): the look and the scroll of the new page on the glass,
in particular that the logged-in repeater page shows only about two rows at once between the 44 px
status card and the Apply slot (it scrolls, but Filip should judge); the pop-up's opaque scrim (no
alpha available, the page behind is covered with panel2); how fast the read-backs fill in, because
the queue sends ONE line at a time and a silent line costs 3 s; the real CommonCLI replies for
`get radio`, `get tx`, `get loop.detect` and friends (read from lib/meshcore v1.18.0, never seen on a
repeater); the anonymous regions read when the repeater is not a direct neighbour (then Regions says
"not read"); the optional parent cycle and `region put <name> <parent>` with a long name; and that
the new Name Apply to the same value is harmless on a real repeater.

**Next step for Claude (USB):**
1. Flash `firmware.bin`; tap RPTR and confirm the tab scans by itself (no Scan tap needed). Serial:
   `[rptr] auto scan`, then `[rptr] row ...` per answer.
2. `tools/cmd.py rptr scan` (the same discovery the tab sends) and `tools/cmd.py rptr regions Argus`:
   the anonymous read. Open Argus in the app instead and watch the page fill with "*, si, lj" and
   "not set (2024-05-15)", with the one big Log in button.
3. Log in to Argus. The page should show the status card (battery about 4.1 V, noise about -120 dBm),
   the "Clock is wrong: not set (2024-05-15)" line with Set clock, and the settings list. Tap Restart
   once: the pop-up must appear and Cancel must come back; tap outside it and nothing happens.
4. Harmless Apply: open Name, leave the value as it is, tap Apply. Serial: `[rptr] > set name <same>`,
   `[rptr] < OK`, then `get name` and its answer; the page shows a tick + "Applied". Then
   `tools/cmd.py rptr status Argus` still parses.
5. Regions: tick one region, tap Apply; serial shows `region allowf/denyf <name>`, `region save`, then
   `region`/`region home`/`region default` and their answers; the page shows the read-back. Try Home
   region and Default scope pickers. Tap a region's x: the remove pop-up names the region.
6. Console (the last row): confirm `get radio` and its answer appear only there; type a harmless
   command such as `clock` in the free line and Send. No other page may show a CLI line.
7. Radio: step the frequency, tap Apply, confirm the pop-up sentence about dropping off the mesh.
   Repeat: set off, Apply, confirm the silent-mesh sentence. Both should be the only two setting
   pages that ask.

## 2026-11-10 21:10 (Claude): EST-362 on the device: list and page good, Log in panics
- Built in the sandbox (exit 0, RAM 152,272 B, flash 2,764,177 B), tests exit 0, reviewed binary flashed (2,764,544 B).
- RPTR tab: the auto scan lists Argus (key icon, "good 8 dB") and "unnamed 86a8ee" ("excellent 14 dB") (`docs/shots/2026-11-10-2007-rptr-list.png`). Argus page before login: Regions "*, si, lj", Clock "not set (2024-05-15)", Signal, Log in (`docs/shots/2026-11-10-2008-rptr-page.png`). Layout defects there: the title "Argus mobile RPTR" is overlapped by its subtitle "repeater, not logged in"; the hint "The password is remembered, so Log in goes st..." is cut; the Signal row's value runs under the Log in button.
- **Tapping Log in panics the device** (twice). Captured with one reader (`docs/est362-login-crash.log`): `[rptr] login sent to Argus mobile RPTR (direct)` then a core dump: task loopTask, PC 0x42158b5c, exc cause 65, sha b9c309a69633ceeb = the EST-362 ELF. Decoded by Claude: __ssprint_r <- _svfprintf_r <- snprintf <- om::rptr::clockStampText (rptr_model.cpp:1096) <- om::rptr::buildScreen (rptr_page.cpp:274) <- buildRptrView (main.cpp:2252/2268) <- drawRptrTab (2288) <- handleTap (2707/4698) <- loop. The buffers passed are fine (clock[40]), so most likely the loop task's stack ran out inside the logged-in page build (exc cause 65 is not an Xtensa exception number; a stack canary/overflow fits): ScreenIn/ScreenOut and the row texts live on the stack.

## 2026-11-10 21:30 (DeepSeek): EST-363 — the Log in panic is the loop stack; the page layout; the picker's Use in the title row

Built in the sandbox with the REBUILD.md command, **exit 0**: RAM **153,464 B** of 327,680 (46.8%,
previous 152,272, **+1,192 B**), flash **2,765,777 B** of 6,553,600 (42.2%, previous 2,764,177,
**+1,600 B**), `firmware.bin` **2,766,144 B** (previous 2,764,544, **+1,600 B**). `test/host/run.sh`
**exit 0** (28 C++ binaries + 5 Python checks). Nothing committed, `.git` untouched, nothing in `lib/`
touched.

### 1. The cause is the loop task's stack, and it is now measured, not guessed

The core dump's app sha256 `b9c309a69633ceeb` **is** the EST-362 `firmware.elf` (sha256
`b9c309a69633ceeb…`), so the ELF I disassembled is exactly the binary that panicked. From that ELF
(`xtensa-esp32s3-elf-nm`/`objdump` for the frames, `readelf --debug-dump=info` for the DWARF sizes):

| what | size |
|---|---|
| `sizeof(ScreenIn)` | **60 B** |
| `sizeof(ScreenOut)` | **3,812 B** (26 `UiRow` x 98 B + 8 log lines x 88 B + the rest) |
| `sizeof(UiRow)` (label 24, value 64, key 4) | **98 B** |
| `sizeof(RptrView)` | **6,204 B** |
| `loop()` frame | **1,456 B** |
| `handleTap()` frame | **1,696 B** |
| `drawRptrTab()` frame | 32 B |
| `buildRptrView()` frame | **7,504 B** = the 6,204 B `RptrView()` temporary + 20 `ListRow` (1,180 B) + locals |
| `buildScreen()` frame | **4,224 B** = the 3,812 B `ScreenOut()` temporary + locals |
| `clockStampText()` frame | 80 B |

The path sums to **1,456 + 1,696 + 32 + 7,504 + 4,224 + 80 = 14,992 B** on a **16,384 B** loop stack
(`kLoopStackSize`), before the ROM's `snprintf`/`_svfprintf_r`/`__ssprint_r` chain (0x42158b5c etc.,
in ROM, not sizeable from the ELF). The crash log agrees: `[hb] … stack=56` at uptime 61 s, i.e. the
page before login left **56 bytes free** (5,748 free while the list was showing, 12,196 at boot). The
next build on the same path had nothing left to spend: `snprintf` inside `clockStampText` overflowed
the stack, which is what exc cause 65 / vaddr 0 looks like.

**Fix (keeps everything off the stack).** `ScreenOut::reset()` and `RptrView::reset()` clear in place
(`memset` of their own fields, no temporary), `buildScreen` calls `out->reset()`, `buildRptrView` calls
`v.reset()`, and `rptrBuildList`'s `ListRow rows[20]` is a function static (only the loop task runs it).
The page model already lived in `g_rptrViewStore` (a static); now nothing copies it. Frames after the
fix, same ELF tools: `buildRptrView` **192 B** (was 7,504), `buildScreen` **528 B** (was 4,224),
`RptrView::reset` 32 B, `drawRptrTab` 48 B, `loop` 1,456, `handleTap` 1,696, `clockStampText` 80 →
the path is **4,000 B**, **10,992 B less**, about 12 KB of the 16 KB stack still free.

**Guards so it cannot come back:** `static_assert` caps in `rptr_page.cpp` (`ScreenOut` <= 4,096 B,
`UiRow` <= 128 B, `ScreenIn` <= 128 B, plus the exact text-buffer sizes) and in `screen_rptr.cpp`
(`RptrView` <= 6,656 B); `test/host/test_est363.cpp` repeats the caps, so a growth stops the build.
On the device: `[rptr] sizes: ScreenIn 60, ScreenOut 3812, UiRow 98, RptrView 6204, loop stack 16384`
at boot (next to `[mem] ui buffers`), and `[rptr] build view page=<name> stack_free=N -> M (of 16384)`
once per RPTR view change, i.e. the loop task's high-water mark **before and after** building that
page (it only ever falls, so `N == M` means the build added nothing to the deepest path).

### 2. The host test: every RPTR state at its largest, and the size guard

New `test/host/test_est363.cpp` (`test/host/run.sh` exit 0). It builds the list at its maximum (8
remembered repeaters plus all 16 scan answers into a 20-row page: the row name 32 and signal 24
buffers, the "unnamed deadbe" fallback, both SNR extremes) and **15 pages x 3 repeater shapes**
(list, before login, logging in, logged in with a full card, Name, Position, Radio, TX, Repeat,
Regions, Add region, Home picker, Default picker, Neighbours, Advertising, More, Console) with the
worst inputs: a 31-character repeater name, 30-character region names, **12 regions (kMaxRegions) and
12 neighbours (kMaxNeighbours) with every slot filled**, a full status card, the extreme numbers
(`latE6 = -90,000,000`, `tx = -128`, `snr4 = -128`, `clock = 0xFFFFFFFF` = "2106-02-07 06:28 UTC"),
a full 8-line console log of 87-character lines, a 63-character command line and a 79-character error.
Every text buffer (`title`, `subtitle`, `banner`, each row's label/value/key, each cell, each log
line, `consoleInput`) is checked to be NUL-terminated **inside its own array**, and the whole
`ScreenOut` is built between guard words (`0xA5C3F00D` x 8 on each side) so a write past the end of the
struct is caught. It also proves the reset leaves nothing behind (Console then Name: no log lines, no
cells, no console input) and that the model is stable for the largest input.

### 3. The three layout defects from `docs/shots/2026-11-10-2008-rptr-page.png`

- **Title over subtitle.** The title was drawn at the centre of a 26 px band and the subtitle at the
  centre of a band starting 16 px below the top, so both glyph rows sat at y ~37-53. The page header
  is now two bands of its own: title 26..44 (`kRptrTitleH` 18) and subtitle 44..58 (`kRptrSubH` 14),
  both drawn at their band's centre; `kRptrPageHeadH` = 32. (`kRptrHeadH` stays 26 for the list header.)
- **The cut hint.** One line cannot hold "The password is remembered, so Log in goes straight in."; it
  is now **"Password remembered: Log in is one tap."** (228 px at 6 px/glyph worst case in a 292 px
  banner). The banner itself shrank from 27 to 22 px (one line + padding) and its Set clock key from
  21 to 18 px.
- **The Signal value under the Log in button.** The row area used to be 85 px for three 30 px rows and
  the body was clipped at the button, so the Signal row was drawn 5 px into it and "good 8 dB" was cut.
  Two changes: the body now **always ends on a row boundary** (`body.h = visibleRows x 30`; a row that
  does not fit is reached by scrolling above the fixed button, never shown sliced, except on the
  Console, which draws log lines, not rows), and the pre-login page has room for all three facts
  without scrolling (body 83..173, three rows, the big Log in 177..211, 4 px gap). The big button is
  34 px instead of 36 so the facts fit; the Apply slot keeps 30 px with a 3 px gap (`kRptrApplyGap`),
  which also gives the 4-row pages (Regions, Neighbours) exactly 120 px again.

The test asserts all of it: `title.bottom() <= sub.y`, `sub.bottom() <= banner.y`, `body.h % 30 == 0`,
`body.bottom() <= big.y`, all three fact rows (and their value boxes) inside the body, the hint fits
the banner, and at the bottom of the scroll the last row is fully visible on every page.

### 4. The sound picker: "Use" in the title row (the `s494-an-picker` ticket, same run)

- The picker's title row (Back, "Message sound") now carries a **Use** key on the right, drawn like the
  keyboard's OK key: gold when the candidate differs from the event's stored sound, **dim** when
  nothing new was picked (then a tap does nothing, and is still spent so it cannot fall through). The
  old in-list "Use this sound" row and its `CHOOSE` header are gone; the list starts with the family
  sections. Tapping a sound still plays it and marks it (check dot); Use stores it and returns to the
  page that opened the picker; Back still leaves without changing the stored value (`choiceFor`).
- **The same rule on the other two Settings pages whose commit action could scroll away:**
  `SetPage::TimeEdit` moved its "Set the clock" button (with five steppers above it the 188 px body cut
  the bottom row) and `SetPage::PresetCustom` its "Apply" into the title row ("Set", "Apply"). Both are
  always live. `ConfirmPreset`/`ConfirmHash` keep their rows (the page is short enough: Apply and Cancel
  are visible without scrolling), and the Channel page keeps its Clear/Delete rows: they act on one
  row's data (and Delete is a two-tap armed action), not a page commit. Wi-Fi's per-network "Forget" is
  a row action too.
- One rectangle drives both: `SettingsLayout::titleAction` (empty when the page has none) is drawn by
  `drawTitleRow` and answered by `settingsTitleActionHitIn`; the title text is ellipsised before the key.
- Tests in `test_settings.cpp` (`settings: all checks passed`): the rect is inside the title row, on the
  right, **outside the body** (so it cannot scroll), disjoint from Back, hit exactly on its edges;
  absent on pages without an action (root and sub-page, 320x240 and 800x480); and
  `settingsActionEnabled(pending, stored)` — dim when nothing new was picked, gold after a pick, dim
  again on the same sound, Back keeps the old choice, Use makes it dim again.

### Could not check (no device in the sandbox)

The glass: that the title no longer overlaps the subtitle, that the hint is whole, that the Signal row
sits fully above the Log in button, the picker's gold/dim Use key at real size, and the title-row Set
and Apply. The new boot line `[rptr] sizes:` prints the model sizes on the device (60/3812/98/6204 on
the S3), and `[rptr] build …` prints the loop stack around each page build, which is the runtime proof
of the fix.

### Next step for Claude (USB, one reader on the port)

1. Flash `firmware.bin` (2,766,144 B). Boot must print
   `[rptr] sizes: ScreenIn 60, ScreenOut 3812, UiRow 98, RptrView 6204, loop stack 16384`.
2. `tools/cmd.py tap 280 227` (RPTR tab) → `[rptr] auto scan`, `[rptr] build list page=- stack_free=N -> M`;
   `tools/cmd.py shot.py` → the list is unchanged.
3. `tools/cmd.py tap 120 70` (the Argus row; the first row of the list) →
   `[rptr] build view page=Repeater stack_free=N -> M`. `shot.py`: **title and subtitle on two lines**,
   hint "Password remembered: Log in is one tap." whole, Regions/Clock/Signal all fully visible.
4. `tools/cmd.py tap 160 194` (**Log in**, 177..211 with the new layout; the old 189 still lands on it).
   Expect `[rptr] login sent to Argus mobile RPTR (direct)`, **no panic**, then the logged-in page and
   its `[rptr] build view page=Repeater …` line with `stack_free` unchanged or barely moved. This is
   the EST-362 reproduction, so it is the check that matters: repeat it twice.
5. `cmd.py status` → `loop_stack_free=`. It must be far above the 56 B of the crash log (expect roughly
   11-12 KB minus whatever the rest of the loop reached). Then the same for the other pages: Regions
   (`tap` the Regions row), Console (the last row, scroll down), Restart (the pop-up).
6. Pickers: `tap 307 12` (gear on the Nodes tab opens Settings on General) →
   `drag 160 210 160 17 400` (scrolls the General page down by 193 px) → `tap 160 92` (the Sound row,
   now at the top of the body) → the Sound page → `tap 160 202` (Message; body 77..240: section 20,
   Master, Volume, Boot = 20+4x30) → the picker with **Use** in the title row.
   `shot.py` with nothing picked: Use is dim. `tap 160 87` (the first sound, under the first family
   header) → it plays and Use turns gold.
   `tap 282 64` (Use) → back on the Sound page, the row shows the new sound, the pocket speaker played
   the preview. Open the picker again, pick nothing, `tap 282 64`: nothing happens (dim). Pick a sound
   and press `tap 30 64` (Back): the stored sound is unchanged.
7. The same title-row rule on the other two pages: General (scrolled down) > Time > Set manually shows
   **Set** in the title row, and General > Region / preset > Custom shows **Apply** there. Neither needs
   a scroll to be pressed.

## 2026-11-10 21:35 (Claude): EST-363 on the device, Argus tested logged in
- Built in the sandbox (exit 0, RAM 153,464 B, flash 2,765,777 B), tests exit 0, reviewed binary flashed (2,766,144 B). `[rptr] build view page=Repeater stack_free=11232`: the stack fix holds, no panic.
- Log in (remembered password): `admin (1424 ms), fw level 2`. The first status request timed out (3.2 s deadline); the refresh answered: raw `fa 0d ...` = 3578 mV (the offset is right now), page "3.57 V 48 %, noise -119 dBm, up 3h 39m, air 3m".
- Set clock: the pop-up "Set the repeater's clock from not set to 2026-11-10 19:33 UTC?" (`docs/shots/2026-11-10-2033-setclock-popup.png`), confirmed: Argus answered `OK - clock set: 19:33 - 10/11/2026 UTC`. **Argus's clock is fixed.**
- Defects seen (`docs/shots/2026-11-10-2034-argus-logged-in.png`): after the OK the page still says "Clock not set" / "Clock is wrong" and a red "No answer from the repeater" (the follow-up `clock` read timed out and the OK answer is not used); a timed-out status request is labelled "Refused: no answer"; the refresh icon overlaps the right column's values; Position "14.501452, 0.00000" (looks like the longitude in the latitude field, the latitude lost: another offset/order bug); the status deadline (3.2 s) is too short for the first request. Filip: times must be shown in local time (CET/CEST, he saw "19:33 UTC" when it was 20:33), and the unread badge should show two digits up to 99 instead of "9+".

## 2026-11-10 20:55 (DeepSeek): EST-364, the six defects from the 20:34 Argus run
Built in the sandbox (exit 0, RAM 153,504 B, flash 2,768,629 B, `firmware.bin` 2,768,992 B, so +40 B RAM and +2,852 B flash on EST-363). `sh test/host/run.sh` exit 0 (29 g++ programs plus the 5 python checks; `test_est364` is new).

### 1. The clock after Set: the OK answer IS the clock
- The red "No answer from the repeater" was the shell's fixed 3 s deadline on the `time <epoch>` line: Argus's answer came at about 3-4 s, so the shell had already failed the write, and the OK then landed on the follow-up `clock` read (which does not parse it), so `clockReplyParse` never ran and the page kept "not set". Root cause of the whole family: **the 3 s deadline**, not the answer.
- New `clockSetReplyParse()` (rptr_model): "OK - clock set: `HH:MM - DD/MM/YYYY UTC`" -> epoch, using the same date code as `clockReplyParse` (which still refuses that line). `rptrTakeReply` (main.cpp) applies it **whatever command the answer is labelled with**, and clears a red result the write's own deadline had produced ("Clock set", green). If an OK for `time ...` carries no parseable stamp, the epoch we sent is used (the ticket's fallback).
- The shell now takes the per-line deadline from the link: `rptrCliEstimateMs()` (upstream's `est_timeout` from `sendCommandData`) plus `kLoginExtraMs`, never under `kCliMinMs` = 8 s (`rptr::requestDeadlineMs`, one constexpr the host test proves). The follow-up `clock` read timing out still changes nothing on the page.
- Card + banner: with the clock confirmed, `clockWrong()` is false, so the "Clock is wrong" bar is gone and the Clock cell shows the confirmed time.

### 2. A status without an answer is not a refusal
- `rptrLinkLoop`: the status deadline is now `requestDeadlineMs(est, kLoginExtraMs, kStatusMinMs)` = `est + 2.5 s`, never under **8 s** (was 3.2 s on Argus). A silence **retries once** (fresh tag, its own full deadline, logged `status ... no answer in N ms, asked once more`).
- Only after that: a new reply kind `RptrReplyKind::StatusWait` instead of an `Error`. The shell turns it into a dim state, not a result line, so nothing red and no "Refused" (that word now only ever comes from a real `Error`/`Err` answer).
- `ScreenIn::statusWait` -> the card's first cell is `Status  no answer yet`, drawn dim (`StatusCell::dim`); a request not even sent still says `tap refresh`, also dim.

### 3. Position: the longitude was in the latitude field because a lost answer shifted the CLI order
- **Not an offset.** A CLI answer carries no command name: `rptr_link` names it by send order (`s_pending` ring). `rptrPumpOutbox` gave up on a line after 3 s and left its entry in the ring, so the next answer took the previous command's name. In the 20:34 run `get name` answered, `get lat`'s answer never came, and the answer to `get lon` (`14.501452`) was parsed as the latitude; `get lon`'s slot then got the `get radio` answer, failed to parse, and stayed 0. That is exactly "14.501452, 0.00000", and `coordText`'s old `%05u` printed the fraction with six digits, which hid the missing zero.
- Three fixes, each host-tested: (a) `rptrForgetPending()` removes an abandoned line from the order, and an answer with nothing outstanding is labelled `""` and never parsed (was: pinned on `s_lastCmd`); (b) `replyFitsCmd()` refuses an answer whose shape is not that command's (an OK is only a write's answer, so "OK - clock set" can never be a latitude, a radio or a `clock` read); (c) a position is a **PAIR**: `CoordRun` holds both replies and the page takes them only when both arrived with no abandoned line in between; per-field limits are +-90 degrees for a latitude and +-180 for a longitude instead of +-180 for both.
- Both zero now reads **"not shared"** (`positionNotShared`), not "0.00000, 0.00000".
- The advert path was checked and is NOT the bug: `lib/meshcore/helpers/AdvertDataHelpers.cpp` (v1.18.0) encodes flags, then latitude int32 LE at offset 1, then longitude int32 LE at 5, then the name; `BaseChatMesh::populateContactFromAdvert` copies them into `ContactInfo::gps_lat/gps_lon` and `src/mesh_link.cpp`'s `fillContact()` copies those two straight into `MeshContact::lat_e6/lon_e6`. `test_est364` builds that exact buffer shape and pins the offsets (and shows that reading offset 5 as the latitude is precisely the swap Argus displayed). The telemetry path (the ticket's "status/telemetry") is checked the same way: `src/target.cpp` hands MeshCore the fix in degrees and `src/CayenneLPP.h` writes LPP GPS as 3 big-endian bytes per coordinate at 1e4, latitude first; the test decodes our encoder's bytes for 46.05 / 14.501452 and for a southern fix (-33.5 / -70.25, two's complement in 24 bits).

### 4. The refresh icon has its own gutter (and a 40x40 target)
- `RptrPageLayout` now carries `refresh` (the 20 px ring), `refreshTap` (a 40x40 finger square, kept inside the card) and `cardColW` (one value column, with `kRptrCardGutter` = 42 px reserved at the right). `drawCard` uses `cardColW`, so a right-aligned value stops 14 px short of the target (before: "-119 dBm" ran under the icon). The hit test uses `refreshTap`; the banner's Set clock and the rows are untouched. The test checks all of it, including the 800x480 arrangement.

### 5. Local time everywhere
- Every clock the RPTR section draws goes through `om::localCivil()` (timekeeper.cpp, the one `CET-1CEST,M3.5.0,M10.5.0/3` rule the status bar and ui/age.cpp use): `clockText` -> "20:33", `clockStampText` -> "2026-11-10 20:33", `clockJumpText` -> "Set the repeater's clock from not set to 10.11.2026 20:33?" (Filip's dotted form), and the model's "not set (2024)" label uses the local year. No line carries "UTC" any more; the wire format is untouched (`clockReplyParse` still reads UTC).
- The console log rewrites a clock answer in place: `localClockAnswer()` turns "OK - clock set: 19:33 - 10/11/2026 UTC" into "OK - clock set: 20:33 - 10.11.2026" and leaves every other answer alone.
- The test builds **every RPTR page, logged in and not** (15 pages x 2) with a real clock and asserts no title, subtitle, banner, row, cell or log line contains "UTC".

### 6. The unread badge counts to 99
- `badgeText()`: 1..99 exactly, "99+" above (with a new '0' glyph). One digit stays the 13 px circle, two the 19x13 pill, "99+" a new 23x13 pill (`kBadgePill3W`); `countBadgeRect()` takes the character count instead of a `wide` flag. Same pixel face, same centre, same 1 px ring. `tools/mkbadge.py` renders 1, 10, 99 and 120 -> "99+" and `docs/shots/badge-preview.png` is regenerated from the same table.

### Tests (host, `sh test/host/run.sh` exit 0)
- New `test/host/test_est364.cpp`: the Argus run reproduced from its own texts (the OK line, the shifted `get lon` answer), the deadline arithmetic, "no answer yet" vs a real status, the position pair (dirty run hidden, clean run shown "46.05000, 14.50145", both zero "not shared"), the advert byte order, the refresh gutter and 40x40 target on both screen sizes, no "UTC" on any page, and the 99/99+ badge.
- Updated: `test_chat.cpp` (badge rule and the three pill widths), `test_est358/360/362` (their clock strings are local now: "01:00", "15:02", "2026-11-10 20:00", "15.05.2024 15:18" + "10.11.2026 20:00"), `run.sh` (links `timekeeper.cpp` into the four RPTR tests and adds the new one).

### Could not check (no device in the sandbox)
The glass: that the card's two columns really end clear of the icon, the 40x40 target's feel with a finger, "Status  no answer yet" dimness at real size, the badge's two digits at 1:1 on the bar, and that Argus actually answers within 8 s (the radar numbers are from the 20:34 run).

### Next step for Claude (USB, one reader on the port)
1. Flash `firmware.bin` (2,768,992 B). Boot must still print `[rptr] sizes: ...`.
2. RPTR tab: `tools/cmd.py tap 280 227`, `tools/cmd.py tap 120 70` (Argus row), `tools/cmd.py tap 160 194` (Log in). Watch for `[rptr] status request to Argus ... est N ms, deadline 8000+ ms` and `[rptr] cli -> Argus ... (est N ms)`.
3. `tools/cmd.py shot.py`: the card's Clock cell is local ("20:33"), Noise "-119 dBm" ends **left** of the refresh ring; if the status has not come back the first cell says `Status  no answer yet` in dim, and after 8 s the log shows `asked once more` then `no answer yet`. A tap at `tap 290 76` (inside the 40x40 target, on the icon) must re-request (`[rptr] status request ...`), and `tap 260 76` (4 px left of the target, on the value) must NOT.
4. Position: after login the read-backs should end `get lat`/`get lon`; `tools/cmd.py shot.py` shows `46.05000, 14.50145` (or `not shared` if Argus shares no position), never "14.501452, 0.00000". If it still shows the swap, the console page (scroll to the bottom) has the raw `<` lines to compare: they must run `get name`, `get lat`, `get lon` in order.
5. Set clock if the banner still shows: `tap` the banner's Set clock, confirm the pop-up reads `... from not set to 10.11.2026 20:33?` (no UTC), then the repeater's `OK - clock set: ...` must appear in the console as local, the red line must NOT, the Clock cell must show the new time and the banner must vanish. A follow-up `clock` read that times out must leave all of that in place.
6. Badge: send enough messages to the Chat tab to pass 9 unread (`tools/cmd.py send` ...), then `shot.py`: the pill shows "10".."99" (19x13), and "99+" (23x13) above 99. `docs/shots/badge-preview.png` is the 1:1 + 8x reference.

## 2026-11-10 21:05 (Claude): EST-364 on the device, Argus re-tested
- Built in the sandbox (exit 0, RAM 153,504 B, flash 2,768,629 B), tests exit 0, reviewed binary flashed (2,768,992 B).
- Login admin (1440 ms); status at the first try (8 s deadline): 3.57 V 48 %, noise -119 dBm, up 4h 07m; read-backs in order (`get name`, 46.042633, 14.5014524, radio, 22, on, clock 20:01 UTC = 21:01 local, regions `*^ F`, `default scope is si`). Page (`docs/shots/2026-11-10-2101-argus-fixed.png`): "Logged in", Clock 21:01 (local), Position 46.04263, 14.50145, Radio 869.618 MHz SF8 BW 62.5. The refresh icon has its own gutter. Badge shows "32" (two digits).
- Note for Filip: Argus's default scope reads `si` (he wants it unscoped for travel); home is `*`.

## 2026-11-10 21:22 (DeepSeek): EST-365, the badge on RPTR, the msgs order, radio stats, names, USB login
Built in the sandbox (build exit 0; RAM **154,528 B**, flash **2,776,877 B**, `firmware.bin` **2,777,248 B**,
so +1,024 B RAM and +8,248 B flash on EST-364). `sh test/host/run.sh` exit 0 (29 g++ programs plus
6 python checks; `test_est365` and `test_tab_bar_calls.py` are new).

### 1. The unread badge on the RPTR page (and the Map page)
- Root cause: the RPTR page called `drawTabBar(..., Tab::Rptr, 0, false)` and so did the Map page;
  Nodes and Chat passed their own `unread` field. The widget was right, the call sites were not.
- The bar no longer takes a count: `ui/tab_bar.h` holds the one `TabBarModel` (active tab, unread,
  new-node dot). Every view carries one (`NodesView`, `ChatView`, `MapView`, `RptrView`), and
  `main.cpp` fills it in ONE place, `currentTabBar()`, from `unreadTotal()` and `g_newNodeDot`. The
  RPTR list, every RPTR page, the confirmation pop-up and the Map all draw `drawTabBar(d, layout,
  v.tabs)`. No screen can pass a different count again; the old signature is gone.
- Tests: `test_est365.cpp` walks all four tabs x counts 0/1/9/10/32/99/100/250 x dot on/off, plus a
  negative count. `test_tab_bar_calls.py` is the call-site guard: every `drawTabBar()` call in
  `src/` must pass a `.tabs` model, `main.cpp` must have the one `currentTabBar()` and one assignment
  per tab, and all four screens must still draw the bar.

### 2. `msgs` listed stale messages last
- Root cause, as suspected: the dump merged the two rings and sorted by `meshMsgTs()` (uptime). A
  message restored from the card carries the uptime of the boot it was recorded in (19:11, ~5 h),
  while the new arrivals at 20:38 had ~2 min, so the restored history sorted last.
- New `src/msg_order.h` is the one cross-boot rule: a REAL wall time first (the field the screens
  print), then this boot's sequence as the tie-break. Uptime is never a key. A non-real 2024 stamp
  sorts with the unknown-clock entries, never after 2026. `cmdPrintMsgs()` builds the key per ring
  entry and insertion-sorts with `msgOrderBefore()`.
- Test: the 20:38 run recreated (restored 19:11/19:12 with 5 h uptime, new 20:37/20:38 with 2 min)
  must print 19:11, 19:12, 20:37, 20:38; plus the no-clock case (sequence keeps restore first), a
  fake 2024 stamp, and equal wall seconds.

### 3. Radio stats (`radio`, `radio packets`, Settings > About)
- `src/radio_stats.h/.cpp` (plain C++, host-tested) keeps what upstream does not: a noise-floor
  window (one sample every 5 s, last 36 = 3 minutes, with last/min/avg) and the last 20 raw
  receptions (time, payload type, route, first 4 payload bytes as `from`, RSSI, SNR, path length).
  The RX boosted-gain value is read back from the chip (register 0x08AC, upstream's own
  `getRxBoostedGainMode()`), not from prefs.
- Upstream's own counters are read in `mesh_link.cpp` `meshRadioStats()`: radio wrapper
  `getPacketsRecv/Sent/RecvErrors`, `getNoiseFloor`, `getLastRSSI/SNR`, Dispatcher flood/direct
  counts and `getTotalAirTime()/getReceiveAirTime()`, and the seen-table duplicate counters. No
  lib/ file was edited.
- Every raw reception is recorded in `OmSX1262Wrapper::recvRaw()` (our class in `src/target.*`),
  so adverts and flood repeats this node does not process still count as "heard". No behaviour
  change, no second SPI read.
- `radio` prints one line: `noise/min/avg/n`, last RSSI/SNR, `pkts/heard/dup/crc/tx`, `air/rxa`,
  flood/direct RX/TX, `gain=boosted|power`, `loop_worst/by`, `minute/minute_by`, `last`, `hist`.
  `radio packets` prints up to 20 `[pkt] at=... type=... route=... from=... rssi=... snr=...
  path=... len=...` lines, oldest first. The USB reply buffer grew from 384 to 512 B for this line.
- Settings > About (General) now shows **Noise floor** (`-117 dBm (-119/-117)` once the window has
  samples), **Packets heard** (`<count> (dup <n>)`) and **Radio gain**.

### 4. Loop latency
- The loop now times the blocks the ticket names (`history`, `wifi`, `ble`, `gps`, `usb`, `mesh`,
  `rptr`, `map`, `draw`, `push`); the remainder is `other`, so the parts add up to the pass. The
  frame compose is measured in `board.cpp` from `omFrameBegin()` to just before the SPI push
  (`omFrameLastComposeUs()`), and the push has its own `omFrameLastPushUs()`. The tile decode is
  charged to `map` and taken out of `draw`, so nothing is counted twice.
- A 10-bucket histogram (1, 2, 5, 10, 20, 50, 100, 200, 500 ms, then longer) and the worst pass per
  60 s window are kept; `radio` prints `loop_worst=<us> by=<part> minute=<us> minute_by=<part>`.
  The pass is timed from `loop()` entry to just before its final `delay(20)`, so the number is WORK
  time, not the 20 ms idle period.
  Nothing was changed in behaviour: no device run was possible in the sandbox, so there is no
  measured stall to report yet. The threshold for action stays one SF8/BW62.5 packet
  (`kOnePacketUs` = 300,000 us, roughly 0.3-1 s of air); if `radio` reports a pass over that, the
  `by=` field names the block and PROGRESS.md must carry the proposed fix.

### 5. Unnamed repeaters
- A discovery answer carries a key, not a name. `simple_repeater` answers ANON_REQ type **0x02**
  (`ANON_REQ_TYPE_OWNER`) with `<node_name>\n<owner_info>`; this is the upstream path read from
  `examples/simple_repeater/MyMesh.cpp` (the same shape as the regions read at 0x01).
- After a discovery answer for a 0-hop, full-key repeater with no known name, `rptr_link.cpp` queues
  one owner request (one anonymous request on the air at a time, never during a login; an open
  page's regions read preempts it). The reply is matched on the sender's full key and the tag; the
  first line becomes the row's name and the list/subtitle stop saying "unnamed 86a8ee". A timeout
  keeps the hex name and logs it. A name is not written into the contact table, so logging in to a
  previously unnamed repeater still needs its advert to arrive first.
- `anonOwnerParse()` is host-tested with name+owner, name only, clock-only, empty first line and a
  truncated buffer.

### 6. `rptr login <name> <password>`
- Parser: `rptr login <name> <password>` (both may be quoted), stored as `text`/`text2`; missing
  password is `args`. Execution calls **`om::rptrLoginStart()`**, the screen's own Log in path, after
  the same `passwordFilter()` the sheet uses. The password is zeroed immediately and never appears
  in a reply. The remember rule is unchanged: `rptr_link` stores the password only when the login
  answer says OK, exactly as for the glass.
- The command is asynchronous: it returns `result=sent` (or `admin`/`guest` when the same repeater
  is already logged in, or `sending`). The answer arrives in the normal loop and `rptr status <name>`
  now carries `login=<state>` plus the status data, so a reviewer can see `login=admin`.
- Serial logging goes through `usb::redactCommand()`: any command whose first two words are
  `rptr login` logs `rptr login name="<name>" (password hidden)`, whatever follows (extra words, a
  malformed quote). Tests cover plain, quoted, extra-word and ordinary commands.

### Tests (host, `sh test/host/run.sh` exit 0)
- New `test/host/test_est365.cpp`: every tab/page state through `TabBarModel`; the `msgs` order
  across a reboot (restored ring plus new arrivals); the noise window (5 s period, min/avg, bounded
  ring); the reception ring decoded from real MeshCore raw headers (flood, transport-flood, short
  frame); the loop histogram and per-part attribution (a 120 ms push-named pass, a per-minute
  rollover); `anonOwnerParse`; `radio` / `radio packets` / `rptr login` parsing and password
  redaction.
- New `test/host/test_tab_bar_calls.py`: the call-site guard described in 1.
- `run.sh` links the new `radio_stats.cpp` and runs both.

### Could not check (no device in the sandbox)
The glass: the badge on the RPTR list and every RPTR page at 1:1, the two new About rows, and that
the RPTR subtitle switches from "unnamed 86a8ee" to the real name. The radio: whether `gain=` says
`boosted` or `power` on the L2 (this is the first time the chip register is read back; if it says
`power`, that is a concrete candidate for "the L2 hears fewer messages than ERA-2/ERA-3", but this
ticket changes no radio behaviour). The loop numbers: there is no device run, so no worst pass has
been observed; `radio` is the measurement. The owner query depends on the repeater running
`simple_repeater` with the 0x02 handler (Argus and the `db744b` node are expected to).

### Next step for Claude (USB, one reader on the port)
1. Flash `firmware.bin` (2,777,248 B). Boot must still print `[rptr] sizes: ...` and `board=ok mesh=up`.
2. Badge: send a few messages, then `cmd.py tap 280 227` (RPTR tab) and `shot.py`: the red badge on
   the Chat icon must show the same count as on Nodes, on the list and on a repeater page
   (`tap 120 70` for the first row). Map must show it too.
3. `cmd.py msgs 8`: the last line must be the NEWEST arrival (20:38-era), not a restored 19:11 one;
   run it right after receiving a fresh channel message.
4. `cmd.py radio`: read `noise`, `min`, `avg`, `pkts`, `heard`, `dup`, `crc`, `tx`, `gain` and
   `loop_worst/by`. Then `cmd.py radio packets`: up to 20 `[pkt]` lines, newest last.
5. `cmd.py tap 307 12` (gear) and scroll General to ABOUT: **Noise floor**, **Packets heard**,
   **Radio gain**.
6. RPTR tab: after the auto scan, an unnamed answer must log `[rptr] name -> unnamed <hex>: owner
   request sent` and then `[rptr] name <- "<name>"`; `shot.py` must show the name instead of
   "unnamed <hex>". If it times out, the row keeps the hex and the log says so.
7. `cmd.py rptr login "Argus mobile RPTR" <password>`: the `[cmd] >` line must read
   `rptr login name="Argus mobile RPTR" (password hidden)` and the answer `result=sent`; wait a few
   seconds, then `cmd.py rptr status "Argus mobile RPTR"` must show `login=admin`. The password must
   not appear in any line, including the `[cmd] >` line and `status`.

## 2026-11-10 21:55 (Claude): EST-365 on the device
- Built in the sandbox (exit 0, RAM 154,528 B, flash 2,776,877 B), tests exit 0, password paths read (the USB command line is redacted before it is logged; the stored password only feeds the login), reviewed binary flashed (2,777,248 B).
- Badge on the Map and RPTR tabs: shown ("32", "33"). `msgs` now ends with the newest (seq 55 at 20:52 after restore). `radio`: gain=boosted, noise -85 dBm (min -85; the average -73 still includes the first samples after boot), RX 4 / CRC 3 in the first minute, `radio packets` lists type, route, sender hash, RSSI/SNR, path. loop_worst 317 ms "by=usb" (my commands). 3 minutes of log: no restart (an earlier "uptime 46 s" reading was the same boot; my misread of the timing).

## 2026-11-10 22:10 (DeepSeek): EST-366, the 21:15 review of the logged-in RPTR page
Ticket: CR, the scrolling status card, the stale region list, Apply only when changed, neighbours,
the Position map picker, one-level Back, the home picker's "*", and the message pop-ups. Firmware
only; no lib/ file was edited and nothing was committed.

### 1. Coding rate
- Argus answers `get radio` as FOUR fields (`869.6179809,62.5,8,8`); the last one is CR. The read-back
  parser already took it, but the Repeater page's Radio row dropped it. `radioText()` now prints
  `869.618 MHz, SF8, BW 62.5, CR8`.
- The Radio page's "Coding rate" row (step keys, CR5..CR8, `radioStep()` clamp) is now part of the
  rule-4 draft/dot logic; `set radio f,bw,sf,cr` still sends the 5..8 value, and the existing
  risky-change pop-up is unchanged.
- Host: `radioReplyParse("> 869.6179809,62.5,8,8")`, the 5/8 clamps, the row text and the CR dot.

### 2. The status card scrolls
- `RptrPageLayout` now treats the card and banner as CONTENT of one scrolling column: `body` is the
  viewport (clip rect), `rowsY` is where row 0 starts below the card/banner, and `card`/`banner`/
  `refreshTap` hold their scroll-0 rectangles. Only the header (back + title/subtitle) is fixed.
- Drawing, the refresh target, the banner button, row rectangles and `rptrPageMaxScroll` all use the
  same scroll offset. At scroll 0 the card starts at the top of the viewport; scrolling moves it up
  and out under the fixed header like any row. The refresh icon scrolls with it and stops answering
  once it is out.
- Host: card at body top, rowsY below the banner, max scroll includes card+banner, refresh hit at
  scroll 0 and not after the card height, and the layout bands on both screen sizes.

### 3. Stale regions (and whether "si" still exists on Argus)
- `regionsParse()` now clears every row before parsing, accepts trailing `\r`/spaces and a `> `
  prompt, scans the whole flag tail (not only its last two bytes), and rejects a CommonCLI error
  (`Err - ...` / `Error`) instead of turning "Err" into a region.
- On login the shell throws away the anonymous scan cache and any previous list, and queues
  `region` FIRST (before `region home`, `region default`, name, position, radio, tx, repeat,
  clock). A logged-in page reads only `g_rptrRegions`; `in.anon` is never consulted there (the code
  says so and the test builds a stale anon cache `*, si, lj` with an empty logged-in list).
- Every region write still ends with the `region`/`region home`/`region default` read-backs
  (`rptrApplyNow`, remove-region, both pickers), so the list is refreshed from the repeater.
- **Does "si" still exist on Argus? No, not as a region.** The 21:05 run's logged-in answer was
  `*^ F`: the region map holds ONLY the wildcard, so `lj` and `si` are both gone. `region default`
  still answers `si` because the default scope is a stored name (31 bytes in NodePrefs) and is not
  checked against the map; it can name a region that no longer exists. That is the repeater's
  stored prefs, not the list the page draws. The anonymous `rptr regions` query can be re-run to
  confirm (`cmd.py rptr regions "Argus mobile RPTR"`), and the Regions page's `region` read-back
  is the direct proof.

### 4. Apply only when something changed
- New `DirtyFields draftChanges()` in `rptr_page.h` is the ONE comparison of draft vs the repeater's
  read-back (name, position, four radio fields, tx, repeat, both adverts, flood max, loop, path
  hash, every region flood flag, home, default scope, add-region).
- `buildScreen()` sets `hasApply` per page from it; a changed row gets `UiRow.changed` and is drawn
  with a small gold dot in its left margin (the screen draws it; the model only says which).
- After a successful apply the shell's existing `rptrSyncDraft()` (facts -> draft) makes the page
  clean, so Apply disappears; when the read-backs arrive the row shows the repeater's own value.
- A FAILED apply now keeps the draft: `rptrTakeReply` skips the read-back `rptrSyncDraft()` while
  the failure is on that page (`g_rptrApplyErr && g_rptrPage == g_rptrApplyPage`), so Apply stays
  visible with the error. Opening a page or a new apply clears the failed flag.
- The map picker's Apply follows the same rule (`rptrMapDirty()`), and the page scroll is clamped
  when Apply appears/disappears.
- Host: 18 change cases (every editable field), clean -> Apply hidden, changed -> Apply + at least
  one dot, reset from a matching read-back -> hidden, failure -> still visible, CR8 read-back ->
  hidden.

### 5. Discover neighbours showed nothing
- Root cause: the button sent `discover.neighbors` and never read the table. `discover.neighbors`
  is NOT in CommonCLI v1.18.0 (grep of the vendored `CommonCLI.cpp` finds no `discover` command at
  all), so the repeater could only answer an unknown command.
- The bundled MeshCore v1.18.0 has only `REQ_TYPE_GET_STATUS` 0x01, `KEEP_ALIVE` 0x02 and
  `GET_TELEMETRY_DATA` 0x03; there is no `REQ_TYPE_GET_NEIGHBOURS` binary request, and the
  companion app source has no neighbour path. The full list with names and SNR is therefore the
  CLI `neighbors` answer: `prefix:secs:snr4` lines or `-none-`. The button (and the page's normal
  read-back) now sends `neighbors`; the raw answer is already logged as `[rptr] cli <- ...`.
- Nothing this device can send makes another repeater advertise, so no "discovery first" step is
  possible or used. The table only holds repeaters whose zero-hop adverts the repeater itself heard.
- `neighboursParse()` now distinguishes a REAL empty answer (`-none-`, `none`, empty) from a
  non-answer (`Err - unknown command` -> valid=false), trims CR/spaces and accepts a `> ` prompt.
  The page says "the repeater has heard no neighbours yet" for a real empty table, never a blank
  page, and the shell resolves names from our contacts by the 4-byte prefix (hex otherwise).
- Host: `-none-`/empty -> valid empty, error -> not valid, two lines with negative SNR, the empty
  message, a named row.

### 6. Position with a map picker
- The Position page gains a "Pick on map" row (the numeric Latitude/Longitude rows stay as the
  secondary option). The picker is `om::mapRender()` (the Map tab's offline tiles) with a crosshair
  at the view centre, pan by dragging, + / - zoom keys, a coordinate line under the map, "Use my GPS
  position" (enabled when there is a fix) and Apply. It opens centred on the repeater's draft
  position, else our GPS fix, else Ljubljana.
- `RptrMapPickLayout`/`rptrMapPickHitAt()` in `ui/rptr_layout.h` hand out every rectangle and answer
  the tap; `drawMapPick()` in `ui/screen_rptr.cpp` draws it. Apply sends the existing
  `set lat <5dp>` / `set lon <5dp>` pair through `applyPlan(Position)`.
- Host: the crosshair is exactly (160,107), all keys inside the width, panel above the tab bar,
  Apply only when `rptrMapDirty()`, the Use-GPS/Apply keys do not overlap.

### 7. Back goes one level
- New `om::rptr::PageNav` (the EST-350 Settings history for RPTR): `rptrGoPage()` pushes the current
  page and scroll; `rptrBack()` pops exactly one and restores that scroll. A Home/Default picker
  opened from Regions now returns to Regions, not to the Repeater page. Only an empty stack falls
  back to the repeater page, and its Back leaves the session.
- Host: every sub-page (Name..Console) pushed over Repeater and popped back with its own scroll; the
  Regions -> HomePicker chain; the overflow drops the oldest entry and keeps the newest scroll.

### 8. Home region picker
- Already offered `* (anywhere)` (value "no home region") as the first row, and `region home *` as
  the "no home" line; the test now pins both it and the default picker's "none" plus its own
  `* (anywhere)` row. MeshCore always has a home, so no "none" row is offered for home.

### 9. Notification pop-ups for messages
- New `src/ui/notify.h` (plain C++, host-tested) owns the decisions: DND silences every pop-up
  except error and low battery; a message in the chat already on screen does not pop up over itself;
  and the burst queue keeps ONE pending slot, always the newest.
- The shell wires it to every sound event: channel message (`<sender> in <chat>: <start>`), mention
  (`Mention: <sender> in <chat>: ...`), DM (`DM <sender>: ...`), new node, low battery
  (`Low battery: 12 %`), radio/mesh error, and an RPTR result when the RPTR page is not showing its
  own result line. Text is one line, whitespace collapsed, UTF-8-safe and ends in "..." when cut.
- The toast is the existing 5 s `ui/toast.h` overlay with the EST-346 consumed-tap dismissal; the
  loop promotes the newest pending notice as soon as no toast is visible. A tap on a toast dismisses
  it and spends the gesture; no second tap is needed.
- `meshDmLatest()` was added to the facade (newest RECEIVED DM), so the DM toast never shows our own
  send.
- Host: DND (all events vs error/battery), open-chat for channel/DM/mention, text forms and
  ellipsis, the ten-message burst keeping #9, and the new-node DND rule (the dot still appears).

### Build and tests
- `pio run -e om_lite_l2` exit 0 (two runs, the second after the last edit): **RAM 154,728 B**
  (47.2% of 327,680), **flash 2,783,989 B** (42.5% of 6,553,600), `firmware.bin` 2,784,352 B.
- `sh test/host/run.sh` exit 0. New `test/host/test_est366.cpp` covers every item above. Existing
  tests were updated only where the ticket changed the old rule: EST-362's "always Apply" row and
  the fixed-card geometry, EST-363/EST-351's body geometry and max-scroll arithmetic, EST-364's
  "refresh target never overlaps the body" assertion, and test_dm's new-node toast under DND.

### Could not check (no device in the sandbox)
The glass: the card really moving under the header, the dots at 1:1, the map picker's pan/zoom feel
and the crosshair over real tiles, and the toast's 48-byte line in the 304 px panel. The radio: that
Argus accepts CR=8 (the parse side is proven). The regions: whether `si` is really gone (the 21:05
logged-in answer says the map has only `*`; `region default` may still NAME si). The neighbours: what
Trnovo II / Argus answer to `neighbors` now, and whether their tables contain any zero-hop repeater
yet. No SSH host was involved.

### Next step for Claude (one reader on the USB port)
1. Flash `firmware.bin` (2,784,352 B). Boot must still print `[rptr] sizes: ...` and
   `board=ok mesh=up`.
2. Login on Argus: `tools/cmd.py rptr login "Argus mobile RPTR" <password>` (EST-365; the reply is
   `result=sent`, never the password), wait for the answer, then `tools/cmd.py rptr status "Argus
   mobile RPTR"` must show `login=admin`. (Or log in on the glass.)
3. `tools/cmd.py tap 280 227` (RPTR tab), `tools/cmd.py tap 120 70` (first row): `tools/shot.py`.
   Scrolling: `tools/cmd.py drag 160 190 160 90 500` then `shot.py`: the status card must move up
   with the rows, the back+name row must stay.
4. Radio row: `shot.py` after scrolling to it must say `..., CR8`. Open Radio (tap the Radio row),
   check the four rows; tap `+` on Coding rate: the dot appears and Apply appears; tap Apply -> the
   risky-change pop-up; Cancel must send nothing (`[rptr] cli ->` lines stay absent).
5. Regions: tap the Regions row; `shot.py` must list only `*` (no `lj`, no `si`). Tap Home region ->
   Back: it must return to Regions (not the Repeater page), at the same scroll. Tap Default scope
   -> Back likewise. `tools/cmd.py rptr regions "Argus mobile RPTR"` re-runs the anonymous query for
   the same question.
6. Neighbours: tap Neighbours (after scrolling), then "Refresh neighbours"; the log must show
   `[rptr] cli -> Argus mobile RPTR: neighbors` and `[rptr] cli <- ...`. An empty table must say
   "the repeater has heard no neighbours yet"; a row with a known prefix must carry the contact's
   name. The raw answer is in the log.
7. Position map: tap Position -> "Pick on map": `shot.py` (tiles, crosshair, coordinate line),
   drag the map and `shot.py` (coordinate changes, Apply appears), tap "Use my GPS position" with a
   fix, tap Apply; the Position page must show the new coordinate after the read-back.
8. Apply rule: on Name (or Radio), make no change: no Apply. Change one field: Apply + one dot.
   Apply and wait for `OK` and the read-back: Apply disappears. Force a failure (an invalid value
   the repeater refuses) and check Apply stays with the error line.
9. Message pop-ups: send a channel message from another node with the Chat tab on Nodes (toast
   appears for ~5 s), with that chat open (no toast), with DND on (no toast; error/low battery
   still show), and send a burst of three (one toast, then the newest). DM: `DM <sender>: ...`.
   Tap the toast once: it goes away and nothing under it is pressed; a second tap is not needed.

## 2026-11-10 22:20 (Claude): EST-366 on the device, Argus re-read
- Built in the sandbox (exit 0, RAM 154,728 B, flash 2,783,989 B), tests exit 0, reviewed binary flashed (2,784,352 B). Login admin; status 3.75 V 62 % (charging), noise -120; Radio row now shows CR8; the status card scrolls away with the page (`docs/shots/2026-11-10-2208-argus-page.png`).
- **Argus's regions, read logged in** (multi-line answer now parsed): `*^ F`, `si F`, `eu F`, `europe F` = home `*`, floods allowed for si, eu, europe; `lj` is gone. `region default` = `<null>`. `region home` (no argument) timed out once (8 s) and the order re-synced.
- **Neighbours: DS's conclusion was wrong.** `discover.neighbors` IS a repeater command in v1.18.0, implemented in examples/simple_repeater/MyMesh.cpp handleCommand (not in CommonCLI, which is why the grep missed it): it sends a zero-hop node-discover request and fills the repeater's neighbour table over ~60 s; `neighbors` then reads the table. The reference is now in `reference/meshcore-repeater-v1.18.0/`.
- Filip: the status card's top edge draws outside its border (the bottom is fine).

## 2026-11-10 22:45 (DeepSeek): EST-367, each chat shows its own messages (and two RPTR fixes)
Ticket: the 22:15 data-visibility bug Filip hit (opening #slovenija with 26 unread showed nothing,
#slovenia showed 3, the unread count dropped anyway), discover-neighbours as a real repeater command,
and the status card's first row drawing on its top border.

### 1. A chat's view is that chat's own log, not the shared ring
- The 32-message RAM ring in `mesh_link` is now only the ARRIVAL buffer. When a chat opens, its own
  newest page (32 messages) is read from its log with ONE seek to the end of the file and one or two
  backwards 4 KB windows (`loadNewest`); nothing scans the log. `src/ui/chat_view.h` is the new plain
  model and `src/main.cpp` holds the buffer.
- The open is deferred: the tap only marks the chat, the loop's history step (`chatViewStep`, right
  after `uiDrawOk`) does the read, and the frame drawn between the two says "Loading messages...".
  So a slow card never sits inside a gesture or inside the mesh pump.
- The view = the stored page(s) (oldest first) + the arrival ring's messages for that chat that are
  NEWER than the newest stored one (or have no log id yet, i.e. still queued). A busy channel can no
  longer evict a quiet one: `chatLiveTail()`/`chatViewFirstKept()` are the two rules, host-tested.
- Scrolling up prepends an older page (16) as before, now into the same PSRAM buffer
  (`kChatViewCap` = 96 stored + the 32-entry arrival tail = `kChatViewMsgs` 128 entries).
  The message band, the hit test and the height arithmetic all grew to that size (they were capped
  at 32, which would have hidden the older pages).
- PSRAM: +27,648 B (96 x `history::Msg`) +6,784 B (per-chat summaries) +4,608 B (chat view entries
  64 -> 128 x `ChatMsgView`). Internal RAM +880 B (`g_chatShown`, the `chats` check's memory).
- SPIFFS fallback: the same path (same store, same volume interface), so it shows what the fallback
  holds. A chat whose log has nothing stored yet shows the live ring only, exactly as before.

### 2. The preview and the unread counts come from the same per-chat source
- `HistoryStore` now keeps a `ChatSummary` per slot: the chat's newest stored message (sender, whole
  text, clocks). It is filled lazily with one backwards read and kept current by `appendMsg`.
- `refreshChats()` builds every selector row from that summary (`meshChatSummary`), not from the
  shared ring. The ring only overrides a preview when it holds something newer (or not stored yet).
- Unread is unchanged in meaning: the number of messages newer than the chat's seen point
  (`unreadAfterSeen`: `maxLogId - seenLogId`, never more than what was already counted). The RAM
  counters (badge, selector) are set from the store after each mark, so they cannot disagree.

### 3. Mark-as-read only for what was loaded and shown
- `markOpenChatRead()` runs on every Chat draw and moves the seen point to the newest message the
  view actually holds (the newest stored page plus any newer stored arrival). `meshHistoryMarkSeen`
  sets the RAM counters from that. The old blanket `meshClearUnread` on open and on every arriving
  message is gone (the Settings "Clear unread" button still clears everything on purpose).
- The normal open loads the newest page, which IS the newest message of the chat, so unread goes to
  0 as before. A load that fell short (no card, a torn window, nothing loaded) leaves the messages it
  did not show unread, and the count no longer drops for a screen that stayed empty.
- Older messages still on the card were read in an earlier session (the seen point is past them), so
  they are not counted again; unread is never a "unseen since boot" count.

### 4. `chats`: the check to run after every flash (EST-367 rule 6)
- New USB command, one `[chat]` line per chat then one answer line:
  `[chat] "#slovenija" unread=0 shown=28 card=28 card_read_ms=3.1 newest_card=20:45 newest_shown=20:45 open_ms=1.2 windows=1`
  - `shown` is the per-view count after the open's load (`-` = never opened since boot); the chat
    open right now reports its live view.
  - `card` is the number of message records in its `.log` + `.old` (one forward read, only this
    command does it; the line says how long it took). It is exact up to 256 KB per file; a bigger log
    stops there and the number carries a `+` (a lower bound), so the check can never freeze the loop.
  - `newest_card` / `newest_shown` are clock times (`-` when the clock was not real then).
  - A line ends with `MISMATCH` when unread > 0 while nothing was shown, or the card holds a newer
    message than the view showed. The answer line carries `mismatches=N`.
- `open_ms`/`windows` are the measured open of that chat, so a slow card is visible in the same check.

### 5. Discover neighbours (RPTR)
- `discover.neighbors` is a real simple_repeater command in v1.18.0 (MyMesh.cpp handleCommand; the
  reference is in `reference/meshcore-repeater-v1.18.0/`). The button (now "Scan neighbours") sends
  it, the page shows a 60 s progress row ("Scanning, 53 s left" plus a thin gold bar), and when the
  window ends `rptrPump` sends `neighbors` ONCE and the table appears.
- A repeater that answers `unknown command`, answers something else, or does not answer at all
  (the CLI deadline) falls back to reading `neighbors` at once, and the page says why ("This
  repeater does not know discover.neighbors, this is its own table").
- The whole sequence is a plain model (`rptr::NeighbourScan`, 5 phases, no waits or sleeps) and is
  host-tested: OK keeps the window, unknown falls back, the tick fires exactly once at 60 s.
- Neighbour rows show the heard time as a clock time now (`SNR -6.5, 20:41`), with the age only when
  the clock was not real at that moment (Filip's rule, the same formatter as everywhere).

### 6. Status card: top padding equals the bottom
- The drawing put row 0's centre at `card.y + 3`, so an 11 px glyph drew on (and above) the card's top
  line while the bottom had air. The layout now owns it: `kRptrCardPad` = (44 - 3x14)/2 = 1 px above
  row 0 and below row 2, and `rptrCardCellAt()` hands the cell's line to the drawing.
- Measured: first row's centre 8 px below the card's top, last row's centre 8 px above its bottom.

### Build and tests
- `pio run -e om_lite_l2` exit 0 (two runs, the second after the last edit): **RAM 155,608 B**
  (47.5% of 327,680), **flash 2,790,817 B** (42.6% of 6,553,600), `firmware.bin` 2,791,184 B.
- `sh test/host/run.sh` exit 0. New `test/host/test_est367.cpp` (own in-memory volume):
  - a quiet chat (28 messages) whose entries a busy chat (40) evicted from the shared ring still
    opens with all 28 from its own log; the arrival-tail rule adds only what is newer, dedupes and
    keeps the newest when the buffer is full; the summary names the quiet chat's newest message
    while the ring holds none of it, and a fresh store over the same volume fills the same summary;
  - the 2,043,023 B log opens in **2 windows, 8,192 B read, 0-1 ms on the host** (windows and bytes are
    counted, so "not a scan" is measured, not claimed); a 5-message log uses no more windows;
  - unread vs shown: the full page clears it, a partial load keeps `maxLogId - shownId` (never more
    than before), and it survives a reload;
  - `chatViewMismatch` fires exactly for "unread > 0 and shown == 0" and "card newer than shown";
  - the card cells are inside the border with equal padding (8 == 8);
  - the neighbour scan: OK keeps the 60 s window, the tick reads once at 60,000 ms, `-none-`/error
    shapes, the unknown-command fallback and the no-answer fallback;
  - the `chats` verb parses (and takes no argument).
- Existing tests updated only where the ticket changed a rule: EST-363's `ScreenIn` size cap 128 ->
  160 B (EST-367 added the scan pointer and two clocks to it; still passed by reference, no copy).

### Could not check (no device in the sandbox)
The glass: that the quiet chat's 28 messages really appear, the "Loading messages..." first frame,
the scroll-up paging into the older pages, the selector preview of a quiet chat, the 60 s progress
bar on Neighbours, and the card's new top padding at 1:1. The radio: what Argus/Trnovo II answer to
`discover.neighbors` now (the parse and the state machine are proven; the answer is not). The
measured open time on a real 2 MB card log (the host number is 0-1 ms because the volume is RAM; the
device prints its own `[hist] open ... in X ms (N windows, B bytes)`). No SSH host was involved.

### Next step for Claude (one reader on the USB port)
1. Flash `firmware.bin` (2,791,184 B). Boot log must contain
   `[hist] working memory ... summaries 6784` and `[mem] ui buffers ... chat view 27648`.
2. `tools/cmd.py chats` BEFORE opening anything: every chat one line, all `shown=-` (never opened),
   `mismatches=0`. This is the baseline.
3. `tools/cmd.py tap 120 227` (Chat tab), `tools/shot.py`: the open chat must show messages, and the
   selector preview line of a chat with unread must name its newest sender (e.g. TehiLL RAK).
4. `tools/cmd.py tap 160 37` (the header) opens the selector, `tools/shot.py`, then tap the row
   whose name is `#slovenija` (rows start at y=64, 30 px each: row n centre is 79 + 30n).
   `tools/shot.py`: the 28 messages must be there, the newest at the bottom, and its time must match
   `tools/cmd.py history "#slovenija" 3` (the newest ids/texts/times on the card).
5. `tools/cmd.py chats` again: `#slovenija` must now be `shown=28 unread=0 newest_card=20:45
   newest_shown=20:45` with `open_ms`/`windows` small, and NO `MISMATCH` on any line. A line with
   `shown=0 unread>0` or `newest_card` newer than `newest_shown` is the bug back.
6. Do the same for `#slovenia` (3 messages) and a DM if one has unread: each must show its own
   messages, not the busy channel's.
7. Scroll up in #slovenija (`tools/cmd.py drag 160 190 160 90 500`, then `shot.py`): older pages must
   prepend (the 28 are all of it, so the drag must snap back with no blank gap).
8. RPTR: log in, open Neighbours, tap "Scan neighbours": the log must show
   `[rptr] cli -> ... discover.neighbors`, the page must show the progress row for 60 s, then
   `[rptr] cli -> ... neighbors` and the table (or the "does not know" note if Argus's firmware is
   older). `shot.py` at ~5 s and at ~70 s.
9. Status card: `shot.py` must show the first row clear of the card's top border, the same 8 px above
   as the last row has below.

## 2026-11-10 22:45 (Claude): EST-367 on the device
- Built in the sandbox (exit 0, RAM 155,608 B, flash 2,790,817 B), tests exit 0, reviewed binary flashed (2,791,184 B).
- `chats` after boot: 6 chats, `mismatches=0` (card counts: #slovenija 28, Don't Panic 23, #test 10, #slovenia 3, DM 2). Opened #slovenija through the dropdown (taps): `shown=28 card=28 newest_card=20:45 newest_shown=20:45 open_ms=16.049 windows=1`; the screen shows the 20:41-20:45 messages with emoji (`docs/shots/2026-11-10-2242-slovenija.png`). Neighbours discovery and the card border: Filip / next Argus run.

## 2026-11-10 23:06 (DeepSeek): EST-368, the Map tab, the message pop-up and two RPTR fixes

The ticket (Filip ~21:35, ~22:25, ~22:30): credits without a plate, no scale, OM's Traces, the unread
badge on the Map, a pop-up that shows the message, a first pick that vanishes, and a way to stop a
repeater sharing its position. The origin of the traces is OverMesh (`static/js/app.js`, MC RX traces)
and the mock (`ui-mock/index.html`, "OM's animated Traces").

### 1. The tile attribution has no box any more
- `ui/screen_map.cpp` draws `Esri, HERE, Garmin, OSM` straight on the tiles: `drawTextOnMap()` writes
  the string in kMapCredit (dim) at the four 1 px neighbours in kMapCreditEdge and then the text
  itself, so it is legible on a bright topo tile and on a dark one without a plate behind it.
  LovyanGFX fills a character cell only when the background differs from the ink, so
  `setTextColor(ink)` alone is a transparent draw (no secret API, one line in the comment).
- Where: bottom LEFT, the space the scale used to own. `mapCreditRect()` returns 6..144 x 199..209 at
  320 px (138 px wide, one 6 px margin in from the edge and above the tab bar). The old
  `kMapCreditGap`/scale-overlap arithmetic is gone.

### 2. The scale is gone
- `mapMetresPerPixel`, the 1-2-5 ladder, `mapScaleLabels` and `MapScaleLabelLayout` are deleted from
  `ui/map_chrome.*`, along with `kMapScaleH` / `kMapScaleMaxW` and the rail/caps/labels drawing.
  `mapChromeFor()` lost its zoom/latitude parameters (nothing else used them); test_est351's scale
  cases were replaced (see below). Filip's words: "remove the distance markers, they are quite
  useless".

### 3. Traces (OM's animation, one route at a time)
- New `ui/map_trace.h/.cpp`, plain C++ and host-tested:
  - `traceMatchHop()` maps a path hash to the contact whose public key starts with it; 1B, 2B and 3B
    all work (the record carries three key bytes; only the packet's own hash size is compared).
  - `traceBuildRoute()` = sender (when its contact has a position) + hops that matched a POSITIONED
    repeater, in order + us (the GPS fix, else the last position we knew). Unknown hops and known
    repeaters without a position are skipped; duplicate consecutive points are dropped.
  - 420 ms PER HOP (`duration = hops x 420`, at least 420), a 35 % tail, 900 ms fade on arrival, and
    a direct ring from 3 to 26 px over 1400 ms.
  - `TraceQueue` replays one route at a time; a newer route from the same sender replaces the older
    pending one (three messages from one node cannot draw three tails on one corridor).
- The shell (`main.cpp`): `traceSeedFromRing()` on opening the Map = the newest message per sender
  (4 at most) plus the newest received DM, oldest first; `traceWatchArrivals()` animates a channel
  message or a received DM the moment its serial changes while the Map is open; a 0-hop arrival is a
  ring at our position (the ticket), not a line, and a route with fewer than two known points is a
  ring at its last known point.
- The frame: `om::mapRepush()` (new in `map.cpp`) pushes the canvas the last `mapRender()` composited,
  so an animation frame costs a push + the chrome, never a JPEG decode or an SD read.
  `mapScreenAnimate()` is the animation path, `mapScreenDraw()` the full one.
- Rate: at most one animation frame per 33 ms (~30 fps), only while a route is live. Nothing is
  redrawn when idle. The touch is read once per loop pass (20 ms delay), so an animating Map adds at
  most one frame's time to a tap's latency.
- Cost per frame is measured and printed when a replay drains:
  `[map] traces 42 frames in 1410 ms, avg 21000 us, worst 28000 us, in flight 0`. The same time is
  already split into MapDecode / Draw / FramePush by `radio`'s loop histogram.
- The toggle: a fourth key on top of the zoom/locate column (`MapHitKind::Traces`), the new
  `kIconTrace` from `tools/mkicons.py` (a route with hop dots), gold when on. Default ON, saved in
  NVS `traces` and read at boot.
- NOT DONE (the ticket's optional part): tapping the Map tab again to open the MSGS rail and freeze a
  route hop by hop. Everything above is the live/replay behaviour; the rail is not in this build.

### 4. The unread badge on the Map tab
- Verified, no code change needed: `buildMapView()` fills `v.tabs = currentTabBar(om::ui::Tab::Map)`
  and `screen_map.cpp` draws the bar with `drawTabBar(d, l2(), v.tabs)`, i.e. the ONE widget that
  carries the badge on the Chat icon. `test_tab_bar_calls.py` (source-level: every screen passes a
  view model, all four tabs come from `currentTabBar`) passes, and test_est368 asserts the model for
  the Map tab (`tabBarModel(Tab::Map, 7, true)`). EST-365's fix already covered this screen; the
  report was from before it landed.

### 5. The message pop-up shows the message
- `ui/notify.h`: a message notice is now two fields, `Notice{title, body}`; title = "TehiLL RAK · #slovenija",
  "Mention · ..." or "DM · ERA-3"; body = the whole text (200 B cap, cut on a UTF-8 boundary).
  `NoticeQueue` (the one pending slot) keeps the body too, so the burst rule still ends with ONE
  newest pop-up, now readable.
- `ui/toast.h`: `ToastState` gained `body` and `showNotice()`; `show()` is still the one-line form for
  pings, refusals and the fault mark. The 5 s deadline and the consumed-tap dismissal are untouched.
- `ui/notice_panel.h/.cpp`: the layout arithmetic, host-tested. The message wraps with the chat's own
  wrapper (`text_wrap.cpp`, so emoji and UTF-8 behave exactly as in the message list) over at most 4
  rows on the L2; the ellipsis appears only when the text really continues, and the last drawn line
  is shortened on a whole unit until it + "..." fit. The panel is 304 px wide, 7 px padded, 14 px
  title + 14 px per body row: 42 / 56 / 70 / 84 px tall for 1..4 rows. Its bottom is the tab bar's
  top minus 8 px and its top never goes under the status bar; on a shorter screen it loses BODY rows
  before the title.
- `widgets.cpp` draws it: title in mono dim, body lines in SansBody, each line through the same
  UTF-8/emoji path the chat uses.

### 6. RPTR: the first pick no longer vanishes
- Cause (the ticket guessed it): every CLI answer whose page was open called `rptrSyncDraft()`, which
  resets the WHOLE draft from the read-backs. A settings page asks for its values when it opens, so
  the finger's first pick was thrown away by the next answer; by the second pick the answers had
  finished, which is why the second one stuck.
- Fix: `Draft` now remembers what `reset()` seeded (`base`, `nameBase`, `latBase`, ...), and
  `Draft::syncClean()` copies a read-back into a field ONLY when that field still equals its seed. A
  touched field is never overwritten. `main.cpp` calls it from the answer path; the whole reset stays
  where it belongs (opening a page, Back, and a successful Apply).
- Host test: pick 30 while the opening answer says 60 -> the draft stays 30, `buildScreen()` shows
  Apply and the row's dot; a field the finger did not touch does take the answer; a radio stepper
  survives a late `get radio`; a failed Apply still keeps the whole draft (EST-366 rule).

### 7. RPTR: Share position
- The Position page has a "Share position" switch (Apply as usual). Off sends, in this order:
  - `gps advert none` when the repeater answered `gps advert` (CommonCLI v1.18.0 really has it:
    `ADVERT_LOC_NONE` / `SHARE` / `PREFS`, and `buildAdvertData()` leaves the position out for NONE),
  - otherwise `set lat 0.00000` + `set lon 0.00000` (MeshCore reads 0/0 as "no position"; our own map
    already skips a contact with `has_pos == false`, which is exactly lat and lon both 0),
  - then ONE `advert` (flood), so the other nodes learn it at once.
  On sends `gps advert prefs` where the command exists, then the coordinates.
- The read-back `gps advert` joined the Position page's opening reads (3 lines, still inside
  `kReadBackMax` 4), so `policyKnown` is the repeater's own answer, never an assumption. An
  "unknown command" answer is refused by `replyFitsCmd()` and the fallback is the lat/lon 0 path.
- The page says it plainly: the banner is "Off: others drop it when they hear the next advert." and a
  Note row (not tappable) says "A node that stored the old point may keep showing it." The banner is
  one ellipsized line in the layout, which is why the sentence is split in two.

### Build and tests
- `pio run -e om_lite_l2` exit 0: **RAM 156,752 B** (47.8% of 327,680; EST-367 was 155,608, so
  +1,144 B: the trace queue/frame state, the Notice buffers and the draft baselines), **flash
  2,799,933 B** (42.7% of 6,553,600; +9,116 B: the trace model, the pop-up layout, the icons and the
  RPTR changes), `firmware.bin` **2,800,304 B**.
- `sh test/host/run.sh` exit 0. New `test/host/test_est368.cpp` (linked with map_trace.cpp,
  notice_panel.cpp, text_wrap.cpp, rptr_model.cpp, rptr_page.cpp, timekeeper.cpp):
  - hop mapping: 2B hash -> the right repeater, 1B and 3B both resolve, a 3B hash that only shares two
    bytes is unknown, an unknown hash is -1, a 1-byte record cannot answer a 2-byte hash;
  - the route: sender + 2 hops + us = 4 points with the exact coordinates; an unknown middle hop is
    skipped (3 points, hops still 3); a hop whose node has no position is skipped (us only); a 0-hop
    path is `direct`; no us position ends the route at the last repeater; a sender sitting on the
    first hop is one point, not a zero-length segment;
  - geometry: 200 px route, point at 0.5 is the vertex, at 0.75 is half way up the second segment, a
    slice carries the vertex, a degenerate route is not a divide by zero;
  - animation: 2 hops = 840 ms, dot at the start at t=0, at the middle vertex at 420, tail starts at
    30 px (35 % of 200 behind the head), arrival holds the last tail then fades over 900 ms, 3 hops =
    1260 ms even with one unknown; the ring grows and fades over 1400 ms;
  - queue: the first route plays, the second does not appear until the first has faded, the direct one
    waits its turn, the same sender's newer route replaces the pending one, an undrawable route is
    refused; no two frames of two routes ever overlap;
  - pop-up: one row for a short text, 4 rows max and "..." on a whole unit for a long one, every line
    of a š/č/ž + emoji text is a whole UTF-8 sequence inside the width, a 120 px-tall screen loses
    body rows (3) and still ends above the tab bar, the title/body builders keep the message and never
    split a sequence;
  - RPTR: the late-answer cases above, the Position page's rows/Apply/note, `gps advert none` +
    `advert`, the lat/lon 0 fallback, `gps advert prefs` + coordinates when back on, the parser and
    `replyFitsCmd("gps advert", ...)`, and the Map badge's model.
- `test_est351.cpp` updated where the ticket changed the rule: the scale tests are gone, the chrome
  test now checks the Traces key on top of the column, and the credit test checks the bottom-left
  outlined box (6..144, 199..209). EST-366's notification tests still pass with the new queue.

### Could not check (no device and no radio in the sandbox)
- The glass: the outlined credit on a bright vs a dark tile, the Traces key, a real dot travelling a
  real path, the direct ring, the 4-row pop-up with a real message, the Share position page, and the
  badge on the Map (the code path is proven, the pixels are not).
- The radio: what Argus and Trnovo II answer to `gps advert` (v1.18.0's CommonCLI has it; a repeater
  on older firmware answers "unknown command" and takes the lat/lon 0 path). No advert was sent.
- The measured animation frame cost on the device (the host cannot measure the panel push); the
  device prints it per replay, so the first run gives the number.

### Next step for Claude (one reader on the USB port)
1. Flash `firmware.bin` (2,800,304 B). Boot log should still show the map tile probe OK.
2. `tools/cmd.py tap 200 227` (Map tab, third of the four 80 px tabs). `tools/shot.py`: no scale at
   the bottom left, the dim "Esri, HERE, Garmin, OSM" there with a 1 px dark edge and NO plate, the
   Traces key at the top of the right column (centre 302,113), the unread badge still on the Chat
   icon if anything is unread. The key is gold because traces default on.
3. `tools/cmd.py tap 302 113` turns Traces off (the key goes plain, log `[map] traces off`), tap again
   to turn it on (`[map] traces on`). Reboot once: it must come back on (or stay off if it was left
   off), i.e. the NVS `traces` key.
4. Make one received message: `tools/cmd.py dm ERA-3 ping` (this refreshes the path), then have ERA-3
   (or any node) send a DM or a #test line to OM-Lite. `shot.py` at ~0.4 s, ~1.0 s and ~2.0 s: the
   dashed route, the dot with its short tail, then the fade; a direct (0-hop) arrival is only the ring
   at our own position. The serial prints `[map] trace <sender> N hop(s), M known point(s), K
   resolved` and, when the replay drains, the per-frame cost line. If avg is above ~33 ms, the frame
   cap is not being met and that number is the finding.
5. RPTR: log in, open Default scope (or Advertising), tap the FIRST option once. Wait 5 s without
   touching anything: the pick must stay and Apply must appear (the 22:30 bug). `shot.py`.
6. RPTR Position: the switch reads on; tap it off, Apply. The console must show `gps advert none`
   followed by `advert` (or `set lat 0.00000` / `set lon 0.00000` / `advert` on a firmware without
   the command). `shot.py` for the banner + Note row. After the advert, `tools/cmd.py nodes` from
   another radio (or ours once the advert lands) must show that repeater without a position, and it
   must not be drawn on our Map.

## 2026-11-10 23:15 (Claude): EST-368 on the device
- Built in the sandbox (exit 0, RAM 156,752 B, flash 2,799,933 B), tests exit 0, reviewed binary flashed (2,800,304 B). `chats`: 6 chats, 0 mismatches.
- Map (`docs/shots/2026-11-10-2313-map-popup.png`): credits drawn on the map with an outline, no plate; no scale; the Traces button (gold, on) above locate / + / -; the unread badge "10" on the tab bar; a repeater marker with its label. A DM ping to ERA-3 brought the bot's answer as a two-line pop-up over the map ("DM . ERA-3 (SI)" + the text with its emoji). No trace was drawn for it: our GPS had no fix indoors and ERA-3 shares no position, so the route had fewer than two known points (by design). The animation itself needs a message whose path runs through repeaters with known positions: to watch on real traffic (Filip).
- From now on (REBUILD.md rule 9): the repo is public; no positions, secrets or other people's messages in docs, logs, tests or screenshots.

## 2026-11-10 23:32 (DeepSeek): EST-369 sounds round 3, a library by shape

The ticket (Filip ~22:45 and 22:55): fewer sounds of the same style, about ten per style, and
different styles from simple beeps to multi-tones, so a notification or the boot welcome has a real
selection. This is the third sound round; EST-361's 54-sound library grouped by timbre is replaced.

**How the clarification was read.** Filip's list named five example styles (Beeps, Solo, Duo, Trio,
Multi-tone), while the ticket's work items define SOLO, DUO and TRIO. This build keeps four shape
sections plus the alert: Solo (one note, which is where the clean beeps live), Duo, Trio, Multi (four
or more notes, a short phrase or chord) and Alert (only the long low-battery warning). Ten sounds per
shape, 41 current sounds in all, down from 54. If Filip wants Beeps as a fifth section of their own,
that is a new ticket and a one-line addition to the shape enum.

### 1. The library is organised by shape first, voice second
- `sound_synth.cpp` now has `Shape {Solo, Duo, Trio, Multi, Alert}` and one `voice` word per sound
  (`beep`, `bell`, `chime`, `glass`, `marimba`, `wood`, `soft`, `organ`, `kalimba`). The name starts
  with the voice ("Beep Clean", "Bell C6", "Kalimba Rise"), so the picker row and INDEX.md say both.
- Three new synth voices joined the old five: **Beep** (one clean partial that holds then releases),
  **Organ** (a mellow 1-2-3 octave stack, 14 ms attack, sustains), **Kalimba** (a tine's 5.4 partial
  over a 55 ms decay). Wood is the existing pluck, glass is the existing chime at high pitch.
- Shape contents and lengths: Solo 1 note 80..250 ms; Duo 2 notes 160..340 ms; Trio 3 notes 250..400
  ms; Multi 4 notes 260..390 ms; Alert the kept 1.35 s battery warning. Every note sequence is
  unique (the host test compares Hz, start and duration across all 41), and inside a shape the ten
  differ in pitch, rhythm and voice: Solo uses 8 voices, Duo 7, Trio 8, Multi 8.
- Examples of the range: "Beep Clean" (880 Hz, 110 ms), "Soft Tick" (quiet 1175 Hz, 80 ms),
  "Bell C6" (one strike, 220 ms), "Organ Mellow" (330 Hz, 240 ms), "Kalimba Tine"; Duo "Bell TK-TK"
  (the boot, unchanged), "Wood Warm", "Organ Low", "Wood Double" (the old Alert Double's rhythm);
  Trio "Glass Trio" (kept), "Bell Rise", "Bell Fall", "Chime Arp"; Multi "Bell Chime", "Soft Chord"
  (four notes, three simultaneous), "Glass Sparkle", "Wood Alert" (the old Alert Triple plus its
  resolving note); Alert "Battery Low".

### 2. The reduction: what was kept and where the retired ids go
Kept exactly (same recipes): **Bell TK-TK** (boot), **Soft Hi** (message), **Soft Lo** (new node,
Filip's soft voice), **Marimba Hi**, **Bell Hi**, **Wood Warm** (DM), **Chime Bright** (mention),
**Glass Trio**, **Battery Low**. That is seven kept duos (inside the ticket's 6-8) plus one trio and
the alert. Everything else from EST-361 is retired.

Old ids 0..53 stay valid forever. `resolveSound(id)` maps a retired id to the kept sound it now
means, `loadChoice` canonicalises it on load so the next save writes the new id, and `soundName`,
`soundShape`, `soundMs` and `renderSound` all resolve through the same table. The mapping
(`kRedirect` in `sound_synth.cpp`; old id -> current id/name):

| old ids | -> kept id and name |
|---|---|
| 0..5 (boot) | 64 Bell TK-TK, 67 Bell Hi, 69 Chime Bright, 70 Soft Lo, 66 Marimba Hi, 75 Chime Arp |
| 6..11 (message) | 65 Soft Hi, 66 Marimba Hi, 67 Bell Hi, 65 Soft Hi, 66 Marimba Hi, 67 Bell Hi |
| 12..17 (DM) | 68 Wood Warm, 70 Soft Lo, 68 Wood Warm, 70 Soft Lo, 70 Soft Lo, 68 Wood Warm |
| 18..23 (mention) | 69 Chime Bright, 67 Bell Hi, 69 Chime Bright, 69 Chime Bright, 64 Bell TK-TK, 74 Glass Trio |
| 24..29 (sent) | 60 Marimba Tock, 55 Beep High, 61 Wood Pluck, 60 Marimba Tock, 60 Marimba Tock, 56 Soft Tick |
| 30..35 (error) | 68 Wood Warm, 70 Soft Lo, 93 Wood Alert, 70 Soft Lo, 73 Wood Double, 67 Bell Hi |
| 36..41 (new node) | 70 Soft Lo, 66 Marimba Hi, 67 Bell Hi, 70 Soft Lo, 66 Marimba Hi, 67 Bell Hi |
| 42..47 (battery) | 94 Battery Low for all six |
| 48..53 (touch) | 55 Beep High, 54 Beep Clean, 59 Glass Ping, 56 Soft Tick, 55 Beep High, 54 Beep Clean |

Why those: nearest kept sound by shape first, then voice, then register. A couple are worth naming:
old Alert Triple (587/523/440) maps to Multi "Wood Alert", which is that same fall plus a resolving
349 Hz note; old Bell Descend and the low bell duos go to Bell Hi, since all higher bell duos were
retired; old Chime Glass/Top go to Chime Bright, and the glass character lives on in Solo Glass Ping,
Trio Glass Top and Multi Glass Sparkle.

### 3. The picker
- `SetPage::SoundPicker` now iterates the current library (41 rows) grouped by shape: sections
  SOLO, DUO, TRIO, MULTI, ALERT, each row named by its voice. The short usage tags (MSG, DM, MENT,
  SENT, ERR, NODE, BATT, TAP, BOOT) and the "Use" button in the title row are unchanged.
- The row action id is now the row's **index** in the library, not the sound id, because current ids
  start at 54. `A_PickBase` (140) still has room for all 41 rows plus Off (A_ChanBase is 200).
- `kSetRowsCap` stays 64 (48 rows: 5 sections, 41 sounds, Off, the note).
- Defaults: boot Bell TK-TK, message Soft Hi, DM Wood Warm, mention Chime Bright, sent Soft Tick,
  new node Soft Lo, error Bell Fall, low battery Battery Low, touch Beep Clean.
- `play <event> [sound-id]` still accepts any valid id, including a retired one (it resolves and the
  reply shows the kept name). Fixed while there: `play boot 300` used to wrap to a valid byte (44)
  and play a wrong sound; an id outside 0..255 is now refused. `play` ids are 0..94.

### 4. WAVs and INDEX
- `tools/mksounds.cpp` walks the current library in picker order and writes
  `<shape>-<n>-<name>.wav` plus INDEX.md with **file, shape, n, voice, id, name, notes, ms, peak**.
  The 54 old files are deleted; 41 new ones are in `docs/sounds/`.
- The renderer now fails on a silent, clipped or non-zero-edged render. This found one real defect:
  the tail fade divided by the fade length without reaching zero, so a held Beep ended on -1. The
  fade is now `(total - 1 - i)`, i.e. exactly zero on the last sample.
- Independent check with Python's `wave` on all 41 files: 1 ch, 16 bit, 16 kHz; first sample 0,
  last sample exactly 0; peaks 6,021..27,000 (never 32,767); lengths 80 ms..1,350 ms (only the
  alert above 400 ms); INDEX has 41 rows, 41 unique files.

### Build and tests
- `pio run -e om_lite_l2` exit 0: **RAM 156,752 B** (47.8% of 327,680), **flash 2,799,289 B**
  (42.7% of 6,553,600), `firmware.bin` **2,799,648 B**. EST-368 was RAM 156,752 (unchanged), flash
  2,799,933, bin 2,800,304, so the smaller library saves 644 B of flash and 656 B of image.
- `sh test/host/run.sh` exit 0. `test/host/test_est369.cpp` replaces `test_est359.cpp` (deleted) and
  proves: `soundCount()` 95 (retired 0..53 plus current 54..94), 41 current sounds, ten in Solo, Duo,
  Trio and Multi and one in Alert; each shape's note count and length range; every name unique, voice
  first, and at least four voices per pickable shape; no two note sequences equal; the seven kept
  duos are the same recipes; TK-TK is still D7 (2349 Hz), a gap, F7 (2794 Hz) by window RMS and tone
  correlation; message sits above new node; the battery warning is 1.35 s at peak 27,000 and
  DND-exempt; every render's peak equals `RenderInfo.peak`, no clipping, first sample 0 or +-1, last
  exactly 0; every retired id resolves to a current sound and renders byte-identical samples; the
  EST-359 migration; and the USB `play` accept/refuse set (including `play boot 300` and `-1`).
  All other host tests still pass.

### Could not check (no device, no speaker in the sandbox)
- How the new voices sound on the L2's small speaker (organ and kalimba especially), and whether 80
  ms Solo sounds are long enough there; the picker's 49-row scroll and the shape sections on the
  glass; the one-time resolution of an old EST-361 id in real NVS; `render_us` per sound on the S3
  (host rendering is a few ms; the longest buffer is unchanged at 48 KB of PSRAM).

### Next step for Claude (one reader on the USB port)
1. Flash `firmware.bin` (2,799,648 B). Boot should still play TK-TK.
2. `tools/cmd.py play boot`, `play message`, `play newnode`, `play dm`, `play error`, `play sent`,
   `play battery`: each reply/log line must show a current name and the expected ms. Then
   `play battery 94`, `play message 54` (a Multi as a message), and `play message 0` (a retired id:
   the log must say `Bell TK-TK`, not id 0's old name).
3. Settings > Sound > Message: the picker must show SOLO, DUO, TRIO, MULTI, ALERT; names start with
   the voice; the stored choice's row is marked; tags and Use are in the same places. Tap a Solo beep,
   a Trio and a Multi: three clearly different previews. Use one and confirm the Sound row shows it.
4. Leave one event on an old EST-361 choice if a device still has one (e.g. message id 9, "Soft High
   Fifth", if the pre-flash NVS had it): it must now be named/played as Soft Hi and the next save
   must store id 65. This cannot be forced from the host; the log line after boot is the evidence.
5. Filip listens to `docs/sounds/` (41 files, names carry shape and voice).

## 2026-11-10 23:40 (Claude): EST-369 on the device
- 41 WAVs checked (0 clipped, edges at zero, none silent): Solo 10 (beeps included: Beep Clean, Beep High, Soft Tick ...), Duo 10, Trio 10, Multi 10, Alert 1. Built in the sandbox (exit 0, RAM 156,752 B, flash 2,799,289 B), tests exit 0, reviewed binary flashed (2,799,648 B); `chats` 0 mismatches; `play message` -> "Soft Hi" (a migrated choice). Listening: Filip.

## 2026-11-10 23:47 (DeepSeek): EST-370 map credits plain, names toggle, centre dot

The ticket (Filip ~23:40): the attribution loses its outline too, a Names key next to the Traces key,
and a small dot at the map's centre. Plain words: three small changes, one new saved setting, and a
check of what the zoom already did.

### 1. Credits: plain text, nothing around it
- `screen_map.cpp` no longer draws the four 1 px outline copies. `drawPlainTextOnMap()` sets only the
  ink colour and draws the string once, so the tiles show through the anti-aliased glyph edges: no
  outline, no plate, no background rectangle. The box is still `mapCreditRect()` (6..144, 199..209)
  and the text is still the small dim mono line "Esri, HERE, Garmin, OSM".
- `theme.h` lost the now-unused `kMapCreditEdge`. The near-black survives only as `kMapBg`, which the
  centre dot uses for its ring. The ellipsis fallback for a screen too narrow for the whole line is
  still there; neither the L2 nor the Tanmatsu takes it.

### 2. The Names key
- Fifth key on the top of the right column, above Traces / locate / + / -:
  `MapChrome::names` is 24x24 at (290, 73), centre (302, 85). The hit test is the same call as the
  drawing (`mapHitAt` -> `MapHitKind::Names`), so a tap and the key can never disagree.
- Icon from `tools/mkicons.py`: `Names`, a 13x13 label tag with its string hole, generated with the
  rest (the preview `docs/shots/icons-preview.png` was regenerated). Ink while off, gold on the dark
  gold key while on, exactly like the Traces route icon.
- `MapView::namesOn` gates the label loop. Off hides EVERY contact label (clients included, so the
  ticket's "nodes, repeaters and rooms" all obey it); the markers stay. Our own marker still has no
  label (it is the tab's "Your position"). When on, an unselected client is labelled too, where the
  old mock rule showed clients only when selected; that is the one visible behaviour change beyond
  the toggle.
- Saved in NVS `names`, default ON, read once at boot. The two switches now share one small store,
  `ui/map_layers.h` (`MapLayerPrefs`), which owns the keys (`traces`, `names`), the defaults and the
  one-key save; `main.cpp` injects the Preferences read/write. Boot prints
  `[map] layers traces=1 names=1`; a tap prints `[map] names on|off`.

### 3. The centre dot
- `kMapCentreDotR = 1`: a 3 px `kInk` core with a 1 px `kMapBg` ring. The ring is only contrast, so
  the dot reads on a bright topo tile and on a dark one; it is not a crosshair.
- `mapCentreOf(chrome)` derives it from the one view rectangle: (160, 107) on the L2. `mapCentreDotRect`
  returns the 3x3 core. The screen draws it after the tiles and the trace route, before the markers,
  labels, chip and keys, so it can never cover a node or a button.
- The same numbers are now `ui/map_chrome.h`'s `kMapCanvasW/H` and `map.cpp` builds its `kViewW/H` from
  them, so the tile renderer, the dot, `mapProject`/`mapScreenToLatLon` and the picker share one source.
- The Position picker is untouched: `drawMapPick` calls `mapRender()` and draws its own crosshair at
  the same point (crosshair box 154,101 12x12 -> 160,107); it never calls `drawCentreDot`. The
  picker's `mapScreenToLatLon` call now uses the shared `kMapCentreX/Y` instead of repeating 160,107.

### 4. Does the zoom really zoom around that centre? YES, and + / - keep it fixed
- `mapZoomBy()` only changes the zoom level, then calls `centreFromLatLon()` from the UNCHANGED
  `s_cLat/s_cLon`; `mapRender()` puts that geographic centre at (kViewW/2, kViewH/2) = (160,107),
  and `mapProject()` / `mapScreenToLatLon()` use the same point. So the point under the dot is the
  zoom focus.
- Checked the arithmetic offline (same formulas as `map.cpp`) for the whole range z9..z18, the z17
  <-> z18 2x magnification, and after a pan then zoom in/out: the centre latitude/longitude projected
  to exactly (160.0, 107.0) every time. The + / - keys call `mapZoomBy(1/-1)`, so they keep the centre
  fixed; the picker's two keys use the same call, so its crosshair stays on the same point too.
- Two limits that are by design, not bugs: at the pack's ends (z9, z18) `mapZoomBy` does nothing, and
  follow/lock recentres on our GPS fix instead of the previous centre.

### Build and tests
- `pio run -e om_lite_l2` exit 0: **RAM 156,768 B** (47.8% of 327,680; EST-369 was 156,752, so +16 B:
  the two bools and the store), **flash 2,800,001 B** (42.7% of 6,553,600; +712 B: the tag mask, the
  extra key, the labels/credits code and the store), `firmware.bin` **2,800,368 B** (+720 B).
- `sh test/host/run.sh` exit 0. New `test/host/test_est370.cpp` (linked with `ui/map_chrome.cpp`):
  - the Names rect is the top key (290..314, 73..97), 4 px gaps, no overlap with the chip or any other
    key; its centre and every pixel answer Names, one pixel outside does not, and a marker sitting on
    it still wins; the credit box never overlaps it;
  - the centre point is (160,107), equals the view's centre and the canvas centre, the dot's core is
    3x3 there, inside the map and clear of the bars, chip and keys; it stays put when the callout
    opens; the picker's crosshair centre is the same pixel;
  - the saved switches: keys "names"/"traces", both default ON with no writes on a first boot, a
    toggle writes only that key and survives a fresh load, set-without-flip persists, a failed write
    still flips the on-screen value and returns false, no reader keeps the defaults, and each key is
    independent of the other.
  - `test_est351.cpp` updated for the fifth key and the plain credit; all its other checks pass.
- Both full runs of `test/host/run.sh` in this session exited 0 (the second re-run was a confirmation).

### Could not check (no device and no radio in the sandbox)
- The glass: whether the plain dim credit is still readable on a bright topo tile (it has no outline
  now), whether the tag reads at 13 px next to the Traces route, whether the 3 px dot is visible on
  both light and dark tiles, labels on/off on real contacts, and the new key's gold/plain states.
- The saved key across a real reboot (the host proves the store, NVS itself is the device's).
- The picker on the glass (the code path is proven: it does not call the dot).

### Next step for Claude (one reader on the USB port)
1. Flash `firmware.bin` (2,800,368 B). Boot log must show `[map] layers traces=1 names=1`.
2. `tools/cmd.py tap 200 227` (Map tab), `tools/shot.py`: the credit at the bottom left is plain dim
   text with NO outline and NO plate; a small dot sits at (160,107); the Names tag key is at the top of
   the right column (centre 302,85) and gold (names default on); Traces (302,113) still gold.
3. `tools/cmd.py tap 302 85`: log `[map] names off`, every label disappears, the markers stay. Tap
   again: `[map] names on`, labels return. Reboot once: the last state must come back.
4. `tools/cmd.py tap 302 113`: Traces still toggles independently (`[map] traces off/on`), names do
   not change.
5. `tools/cmd.py tap 302 169` / `302 197` (+ / -): log `[map] zoom N`; a landmark that was under the
   centre dot must stay under it at every step. Old zoom-out is 302,197 and zoom-in 302,169.
6. RPTR > Position > the map picker: its own crosshair is there and the small centre dot must NOT be.
7. Look at the credit over the brightest tile in the pack at z13: if the plain line is too faint to
   read, the next ticket can give it a shadow again, but that is Filip's call.

## 2026-11-10 23:50 (Claude): EST-370 on the device
- Built in the sandbox (exit 0, RAM 156,768 B, flash 2,800,001 B), tests exit 0, reviewed binary flashed (2,800,368 B), `chats` 0 mismatches. Map (`docs/shots/2026-11-10-2343-map-names-dot.png`): credits plain dim text, the Names key (on) above Traces, a small centre dot; zoom keeps the centre (DS read the code: + / - change only the level around the unchanged centre).

## 2026-11-10 (DeepSeek): EST-371 map opens from its last frame, main.cpp split (partial)

The ticket had two parts: move each screen's state, drawing glue and tap handling out of main.cpp,
and make the Map tab open at once instead of decoding its tiles inside the screen change. The second
part is done and measured as far as the sandbox allows. The first is done for four modules and stops
at a clean build; the rest is listed below.

### 1. Modules moved out of main.cpp (what changed)

main.cpp went from 7,526 to 6,172 lines. setup(), loop() and the dispatch between tabs stay there,
plus the state that more than one screen needs. Every moved file compiles in the same build and no
host test changed its expected behaviour (two tests now scan the new locations, see below).

- `src/screens/map_tab.{h,cpp}` (542 lines): the Map tab's state (follow lock, selected marker, the
  Traces queue and its animation clock, the two saved layer switches), the MapView builder, the trace
  engine and the Map chrome tap handling. Its four shell services (status bar, tab bar model, GPS
  fix, toast) come in through a small `Host` set once in setup().
- `src/screens/toasts.{h,cpp}` (82 lines): the one toast and the burst queue. ui/toast.h still owns
  the deadline and the consumed-tap dismissal, ui/notify.h still owns the DND/open-chat decisions.
- `src/screens/sound_events.{h,cpp}` (176 lines): the per-event library choice, master, volume, DND,
  touch click and welcome switch, the NVS keys and the EST-359 to EST-361 migration.
- `src/usb_commands.cpp` (845 lines): every USB test command. The shell's buffers are read through
  `src/app_shell.h` (read-only accessors), so a command can print what the screens show but cannot
  write them. `SET_LOOP_TASK_STACK_SIZE` stayed in main.cpp.
- main.cpp was reduced by moving, not rewriting: the functions themselves kept their comments and
  their logic. The only new indirection is the Host/callback wiring above.

### 2. Still in main.cpp (what is left)

Nodes (list state, sheet, Info, the new-node dot), Chat (list, view, keyboard sheet, message sheet,
send), RPTR (session, list, pages, picker, console), Settings (pages, editors, sound picker), the
keyboard and accent hold, the generic sheets, and the gesture/scroll dispatch in loop(). These are
the larger, more cross-linked sections; splitting them safely is the next ticket, not a 30-minute
edit.

### 3. Map opens from its last composed frame (EST-371)

- `map.cpp` now remembers the epoch of the view it composited. `mapSetView()`, `mapPan()` and
  `mapZoomBy()` bump `s_viewEpoch` only when the view really changes; `mapRender()` stores the epoch
  it rendered. New calls `mapCanvasReady()` and `mapViewStale()` expose that.
- The tile canvas is the already existing PSRAM composite: 320 x 214 x 2 = 133.8 KB (the full frame
  is 153.6 KB / 150 KiB). Nothing has to be allocated for this change.
- On entering the Map tab, `map_tab::enter()` arms the fast open. `map_tab::draw()` then calls
  `mapScreenAnimate()`, which repushes the canvas with no SD read and no JPEG decode, and redraws all
  the chrome (status, tab bar, markers, labels, chip, keys, callout, trace). Only a changed view
  queues a tile refresh, and `map_tab::pump()` runs that refresh on the next loop pass, after the
  cached frame is already visible. An unchanged view never decodes a tile again, including a marker
  select or the once-a-minute clock repaint.
- New log lines: `[map] open: cached canvas N us, view unchanged` (or `tile refresh queued`) and
  `[map] refresh: tiles after cached open N ms`. The existing `[ui] change: composed in N ms` line
  now shows the open frame's compose time.

Measured before (device log, Claude 2026-11-10 17:35): `Nodes -> Map` composed in 101 ms inside the
dark, with out 40 ms, push 11.376 ms, settle 17 ms and in 60 ms, about 229 ms from tap to light back.
Measured after: not possible in this sandbox (no serial device, `ls /dev/ttyACM*` sees none), so the
after number has to come from the flash and the two new log lines. The change removes the tile
decode and the SD reads from that path; what remains is the canvas repush plus the chrome.

### 4. Build and tests

- `pio run -e om_lite_l2` exit 0: RAM 156,800 B (47.9% of 327,680; EST-370 was 156,768), flash
  2,803,305 B (42.8% of 6,553,600), firmware.bin 2,803,664 B. The growth is the Host/callback glue
  and the accessor calls; the moved code is the same code.
- `sh test/host/run.sh` exit 0. Two tests follow the move: `test_tab_bar_calls.py` now checks the Map
  view's model in `screens/map_tab.cpp` and its Host wiring in main.cpp; `test_build_flags.py` reads
  the `loop_stack_free`/`loop_stack_size` status line from `src/usb_commands.cpp`. Every other test
  is unchanged and passes.

### Could not check (no device, no serial, no glass)

- The after open time and the feel of the Map switch. The log lines above are the evidence.
- That the cached frame looks right on the glass, especially the status time and a callout opened
  before leaving the tab (the chrome is redrawn over the cached tiles, so it should be current).
- The first Map entry still renders every tile (there is no canvas yet); only the second and later
  entries take the fast path.
- The sound picker's stored values across a reboot after the move (the host proves the sound logic,
  NVS itself is the device's).

### Next step for Claude (one reader on the USB port)

1. Flash `firmware.bin` (2,803,664 B). Boot log must still show `[prefs] sound=...` with the same
   values and `[map] layers traces=1 names=1`.
2. Open Map, go to Nodes, open Map again. Second entry must print
   `[map] open: cached canvas N us, view unchanged` and the `[ui] change: composed in N ms` line
   must be much smaller than the 101 ms baseline. Write the two numbers in this file.
3. Nodes sheet > Show on map: the Map must appear from the cached frame and then print
   `[map] refresh: tiles after cached open N ms` one pass later, with the new location's tiles.
4. Tap a marker, the Traces and Names keys, + / -, follow: each still does what EST-368/370 describe.
   A zoom or a pan still decodes (the view changed), a marker select no longer does.
5. Quick regression: Chat send and receive, Settings rows, a sound preview, `tools/cmd.py chats`
   (0 mismatches) and `status` (loop stack line present).
6. Next ticket: split Nodes, Chat, RPTR and Settings out of main.cpp in the same style; the Map's
   Host pattern is the template.

## 2026-11-11 00:30 (Claude): EST-371 on the device
- Built in the sandbox (exit 0, RAM 156,800 B, flash 2,803,305 B), tests exit 0, reviewed binary flashed (2,803,664 B); `chats` 0 mismatches. main.cpp 7,526 -> 6,172 lines (partial split: map tab, toasts, sound events, USB commands moved out).
- Map open: first entry composed in 105 ms; the second entry `[map] open: cached canvas 19790 us, view unchanged`, composed in 23 ms. The dip sequence (out ~42 ms, push ~11.5 ms, settle ~18 ms, in ~61 ms) unchanged.

## 2026-11-11 00:26 (DeepSeek): EST-372 map cross, bolder traces, edge tabs, plain zN, charge probe

Two map tweaks from Filip plus the edge tabs and the charging question. The charge icon is NOT in
this build: no verified charge status source was found from the sandbox. A read-only probe was added
so the next boot log can name 0x34 (item 4 below).

### 1. The centre mark is a thin cross
- `ui/map_chrome.h`: `kMapCentreCrossArm = 4`, `mapCentreCrossRect()` returns the 9x9 box around
  `mapCentreOf()`. For the L2 that is (155..163, 102..110), still centred on (160,107).
- `ui/screen_map.cpp` draws two 1 px kInk lines, 9 px long, over a 1 px kMapBg outline under each
  line, so the white cross reads on a bright topo tile as well as on a dark one. It is not a
  crosshair: the Position picker still calls only `mapRender()` and draws its own crosshair at the
  same pixel; no picker code changed.
- The cross is drawn after the route and before the markers, exactly where the dot was, so it can
  never cover a node or a tab.

### 2. The traces are bolder (theme.h holds the colours and widths)
- Colour: one fuchsia-violet `#c026d3` for route, tail, dot and the direct-arrival ring. The old
  `#8b5cf6` route was 1 px at alpha 86 (about 34 %); the new one is 2 px at full opacity.
- Sizes in `ui/theme.h`: `kTraceRouteW = 2`, `kTraceTailW = 2`, `kTraceDotR = 2` (5 px fill),
  `kTraceDotOutlineW = 1` (a 1 px dark ring, `#14101a`), `kTraceRingW = 2`. The dashed route keeps
  its 2 on / 4 off pattern; that is the "dashed kept for unknown hops" style.
- Measured contrast (sRGB WCAG ratio) against the real z13 tile image `map-preview-ljubljana-z13-2026-11-09.png`
  and a dark-tile sample. The old figure is the effective 34 % blend, not the raw colour:

  | sample | new `#c026d3` | old `#8b5cf6` at 34 % |
  |---|---|---|
  | `#f0f0e0` tile ground (34.6 % of the image) | 4.09:1 | 1.50:1 |
  | `#e0e0e0` tile ground (11.7 %) | 3.57:1 | 1.46:1 |
  | `#d0d0d0` road grey (6.9 %) | 3.05:1 | 1.39:1 |
  | `#181818` dark-tile sample (no dark tiles in this pack) | 3.77:1 | 1.52:1 |
  | `#909090` darker road ink | 1.48:1 | 1.14:1 |
  | `#6c6c6a` darkest 1 % of the real tile | 1.12:1 | 1.05:1 |

  The last two are thin dark map features, not a tile ground; no single colour clears 3:1 against
  both a white ground and a mid-grey line. The 2 px width and the dot's dark outline are what keep
  those crossings readable.
- Preview for Claude before flashing: `docs/shots/2026-11-11-0026-trace-preview.png` (320x240, the
  real z13 tile crop as the background, the new dashed route, 2 px tail, 5 px dot with its outline,
  the 2 px ring, the centre cross, and the measured ratios in the caption bar).
- Host test `test_est370.cpp` now also checks the theme values and the contrast floors.

### 3. Edge tabs and the plain z13 line
- `ui/map_chrome.cpp`: the five tabs start at x = 276 and end at x = 320, so the right edge IS the
  screen edge. `kMapTabW = 44`, `kMapTabGap = 1`. They stack bottom up: zoom out (181..214),
  zoom in (146..180), locate (111..145), Traces (76..110), Names (41..74). 34 px tall each.
- `ui/screen_map.cpp`: `edgeTabBox()` calls `fillRoundRect`/`drawRoundRect` with the rectangle
  extended `kRadius` past the right edge, so the left corners are round and the right corners are
  clipped away: no right border and no gap. The icons and the + / - glyphs are unchanged; the on
  state is still the dark gold fill with a gold edge and gold icon.
- The "offline · z13" chip is gone. `MapChrome::chip` became `zoomLabel` (x 296..319, y 28..38),
  drawn as plain kDim mono text right-aligned at the same edge as the tabs, just above Names. It is
  not a button and it is not a map tap (the hit test falls through to Map).
- **The 40 px tall tap area does not fit.** The map band between the bars is 189 px (240 - 26 - 25);
  five 40 px tabs alone need 200 px, and the z13 line takes another 13 px. The fit is 34 px per tab,
  1 px apart (5*34 + 4 = 174, from y 41 to 215). That is still 10 px taller and 20 px wider than the
  old 24x24 key, and every tab is a separate hit rectangle. If 40 px is a hard requirement, the
  status bar, the tab bar or the one-column rule has to give; that is Filip's call.
- The bottom tab is flush with the tab bar's top (215) and the right edge is flush with the screen;
  the left edge of the tab column moved from 290 to 276, so the map gains 14 px there.

### 4. Charging indicator: no icon, the source is not verified yet
- Boot scan (the device's own log, same list as the ticket): `14 18 21 22 2C 34 48 5D` all ACK.
  0x21 is the PCA9554 expander, 0x18 the ES8311 codec, 0x2C the backlight driver, 0x48 the ADS1115
  battery ADC, 0x5D the GT911 touch. 0x14, 0x22 and 0x34 are unidentified.
- Expander inputs: `board.h` uses all 16 bits (0..15); the boot log prints `cfg=07/00`, so the only
  inputs are port 0 bits 0, 1 and 2 (wake button, I2C interrupt, SD detect), and port 1 is all
  outputs. There is no spare expander input to repurpose, and `board.h` has no charger status pin.
- 0x34 is the only plausible PMU/charger address. `board.cpp` now does a READ-ONLY probe after the
  second I2C scan: `[pmu] 0x34 reg03=..` (an AXP2101 answers 0x4A), `[pmu] 0x34 reg00/reg01=.. (raw,
  not interpreted)`, `[pmu] 0x22 in/cfg=..` and `[pmu] 0x14 reg00=..`. It writes nothing and nothing
  in the app reads the result. The sandbox has no serial device (`/dev/ttyACM*` is absent), so the
  register values are **not visible from here**; the next flash's boot log is the evidence.
- Because no charge status source is verified, there is no bolt, no `[batt] charging yes/no` line,
  and no `StatusModel::batteryCharging`. A battery-voltage trend was not used: it is not a charge
  signal. If the probe reports 0x4A, the next ticket still has to prove which status bit means
  charging before drawing anything.

### Build and tests
- `pio run -e om_lite_l2` exit 0: **RAM 156,800 B** (47.9% of 327,680; EST-371 was the same),
  **flash 2,803,937 B** (42.8% of 6,553,600; +632 B), `firmware.bin` **2,804,304 B** (+640 B).
  The growth is the edge-tab geometry, the thicker line loops, the cross and the read-only probe.
- `sh test/host/run.sh` exit 0 (full suite). `test_est370.cpp` updated: Names is the 44x34 top tab
  with its right edge at 320, the centre cross is 9x9 at (160,107), the trace colour and widths are
  fixed with the contrast floors above, and the saved-switch checks are unchanged. `test_est351.cpp`
  updated to the new tab geometry and the z13 line; its other checks pass.

### Could not check (no device, no serial, no glass)
- The glass: the 34 px tabs, the left-only rounding at the right edge, the plain z13 line on a
  bright tile, the cross on a light tile, the new trace at 2 px and the dot's outline.
- The `[pmu]` answers: the probe is in the build, but this sandbox sees no `/dev/ttyACM*`.
- Whether 34 px tabs feel big enough for Filip; the ticket's 40 px does not fit the 189 px band.

### Next step for Claude (one reader on the USB port)
1. Flash `firmware.bin` (2,804,304 B). Boot log should show the two `[i2c] found:` lines and the
   new `[pmu]` lines. Write what 0x34/0x22/0x14 answered in this file.
2. Open Map, `tools/shot.py`: no "offline" chip, plain z13 at the top right, a thin cross at
   (160,107), five tabs flush with the right edge and rounded on the left only. Names and Traces
   are gold (both default on).
3. Tap each tab centre: Names (298,58), Traces (298,93), locate (298,128), + (298,163), -
   (298,198). Same logs as before (`[map] names ...`, `[map] traces ...`, `[map] follow ...`,
   `[map] zoom N`). The hit area is x 276..319, the full 34 px height.
4. Trigger a trace (a message with hops) and look at the glass; compare with
   `docs/shots/2026-11-11-0026-trace-preview.png`. The route should be a solid fuchsia-violet
   2 px dash, the dot a 5 px fill with a dark ring, the direct ring the same colour at 2 px.
5. If the 34 px tabs are too small, decide with Filip: shrink a bar or accept two columns. The
   current build is the largest one-column fit with the z13 line above it.
6. Charging: if `[pmu] 0x34 reg03=0x4A`, the next ticket can read the AXP2101 status register and
   add the bolt. If not, close the charging item with the probe's answers.

## 2026-11-11 00:36 (Claude): EST-372 on the device
- Built in the sandbox (exit 0, RAM 156,800 B, flash 2,803,937 B), tests exit 0, reviewed binary flashed over USB OTA (2,804,304 B); `mesh: up`, `chats` 6 chats, 0 mismatches.
- Screenshot: the thin cross at the centre, five edge tabs flush with the right edge (rounded on the left only), Traces tab on in gold, plain credits, and `z13` above the top tab. The label is right-aligned at the screen edge and readable, but dim grey is faint on the light topo ground; a darker ink is a possible later tweak.
- Bolder traces are seen only in the preview (`docs/shots/2026-11-11-0026-trace-preview.png`); no real trace arrived during the check, so they are unproven on live traffic.
- Charge probe (read-only), boot log: `0x34 reg00=0x01 reg01=0x01`, `0x34 reg03=0x00` (an AXP2101 answers 0x4A there, so 0x34 is something else), `0x22 in=0x00 cfg=0x24`, `0x14 reg00=0x1E`. No register is identified as a charge status yet, so there is no bolt. Next step: read the same lines once on USB power and once on battery and compare.
- Correction (Claude, 2026-11-11 00:37): the "00:30" stamp on the EST-371 entry above was an estimate, not the clock. That entry was written around 00:16, when `b396da2` was committed.

## 2026-11-11 00:51 (DeepSeek): EST-373 thin dark cross, 25 px tabs, hide-tabs arrow

Three map tweaks from Filip (~00:38) on top of EST-372. Nothing else on the map changed: the traces,
the callout, the attribution, the status and tab bars and the tile path are untouched.

### 1. The centre cross is a bare cross in one dark ink
- `ui/theme.h`: a new `kMapCrossInk = #202020`. `ui/map_chrome.h`: `kMapCentreCrossArm` 4 -> 3, so the
  box is 7x7 (157..163 x 104..110) still centred on (160,107).
- `ui/screen_map.cpp`: the two `fillRect` calls that laid a 3 px `kMapBg` plate under each line are
  gone. Only the two 1 px lines remain, one ink, no outline.
- Measured on the real z13 tile image (sRGB WCAG): **14.16:1** on the `#f0f0e0` ground (34.6 % of the
  image), **12.34:1** on `#e0e0e0`, **10.56:1** on the `#d0d0d0` road grey, **3.10:1** on the darkest
  1 % (`#6c6c6a`). The old white `kInk` line was **1.02:1** there, i.e. visible only because of the
  plate that is now gone.
- The price of one ink: on a dark tile the cross would vanish (**1.09:1** on `#181818`, the same on the
  `#191816` canvas). This tile pack has no dark tiles, so the choice is safe for the installed card;
  the number is in the theme comment and in `test_est373.cpp` so nobody has to rediscover it.
- Still not drawn in the Position picker: it calls `mapRender()` only and keeps its own crosshair.

### 2. The tabs are 25 px wide, and the tap rect IS the drawn rect
- `ui/map_chrome.h`: `kMapTabIconPad = 4`; the widest thing drawn on the column is the Follow lock
  (5 px radius circle plus a 5 px arm reaching 8 px from the centre = **17 px**), so
  `kMapTabW = 17 + 4 + 4 = **25**`. The ticket expected "about 28"; 25 is what the measured widest
  icon gives. The lock's numbers (`kMapLockR`, `kMapLockArmLen`, `kMapLockArmOut`) moved here and
  `screen_map.cpp` draws the icon from them, so the width and the icon cannot drift apart.
  The Traces and Names masks are 13x13 and the `+` / `-` glyphs measure 12 / 5 px of ink in SansTitle
  (read from the VLW glyph records of `font_sans14.cpp`), all narrower than the lock.
- The column now starts at **x 295** (was 276 with 44 px), still flush at 320, and the map gains
  19 px. Heights: **32 px**, not 34. That is not a slip: see item 3, the arrow's 22 px target does not
  fit in the 15 px that were above a 34 px top tab. 32 px is still 8 px taller and wider than the old
  24x24 key.
- New in `ui/map_chrome.h/.cpp`: `MapTab` + `mapTabRects()` - ONE list of the five (kind, Rect) pairs.
  `screen_map.cpp` draws that list and `mapHitAt()` reads that same list, so "the tap zone is the drawn
  rectangle" is a property of the code, not a promise. The 320x240 numbers: Names (295..320,
  51..83), Traces (84..116), follow (117..149), `+` (150..182), `-` (183..215), 1 px apart.
- The old wide tap zone is gone: a tap at x 294 (or at the old 276 edge) is now a map tap. The zooms,
  the follow lock, Traces and Names keep their own 25 x 32 rects, all four corners tested.

### 3. The hide/show arrow beside the zoom label
- Layout (`ui/map_chrome.cpp`): one `zoomTap` rect, **28 x 22** px at the top right (x 292..320,
  y 26..48), from the status bar's bottom to 3 px above the top tab. Inside it the label
  `zoomLabel` (21 px wide, right-aligned at 313) and the 5x7 `zoomArrow` (x 315..320, y 30..37, same
  centre line as the label). The label is at the same place in both states and is drawn in both.
- **The 22 px target does not fit above a 34 px top tab** (the band there is 15 px). Rather than let
  the target overlap the top tab (forbidden) or hang into the status bar's own keys, the five tabs
  give 2 px each: 34 -> 32, and the target is exactly the label band. That is the one place this
  ticket changes a number the ticket said to keep, and it is why.
- Glyph drawn with primitives (`drawArrow`): a filled triangle whose columns are 7, 5, 3, 1, 1 px, so
  its box is 5x7. Points right (`>`) while the tabs are out, left (`<`) while hidden, in `kDim`, the
  same ink as the `z13` label (Claude's own note said that dim ink is faint on the light topo ground;
  this ticket does not change it).
- Hidden: every tab is its **4 px** sliver at the same right edge, same y, same height, same fill
  (gold on a tab that is on), drawn square with a 1 px line on its left edge (`edgeTabSliver`), no
  icon. Instant switch, one `draw()` through the existing frame path, so no tear and no new path.
- **Choice on the slivers: a tap on one brings the tabs back** (the ticket offered "nothing" or
  "show"). A drag that starts on a sliver still pans the map, exactly as a drag over the whole map
  already did; taps in the freed area left of x 316 are plain map taps.
- Saved like Names: new `MapLayerPrefs` key `tabsHidden` (default **false**, i.e. shown), one write per
  tap, read once at boot. `main.cpp`'s boot line is now
  `[map] layers traces=.. names=.. hidden=..`. A tap logs `[map] tabs hidden` / `[map] tabs shown`
  (the state is read back after the toggle, not taken from the write's return).

### Build and tests
- `pio run -e om_lite_l2` **exit 0**: RAM **156,800 B** (47.9 %, unchanged), flash **2,804,373 B**
  (42.8 %; EST-372 was 2,803,937, so **+436 B**), `firmware.bin` **2,804,736 B** (+432 B).
- `sh test/host/run.sh` **exit 0**. New `test/host/test_est373.cpp` (in `run.sh` as BIN35) checks:
  every tab's rect equals its tap zone (four corners + centre answer that tab), `x + w == 320` for
  every tab in both states, 25 = 17 + 2x4, the five heights equal and 32, the arrow's 28x22 target
  inside the band and never overlapping the top tab, the label and the glyph boxes identical in both
  states, the slivers at 316..320 with taps that bring the tabs back, the freed area as map taps, a
  marker still winning over a sliver, the 7x7 cross with the contrast floors above, and the
  `tabsHidden` store (default, one-key write, reload, `set`, a failed write, no reader).
  `test_est351.cpp` and `test_est370.cpp` updated to the new numbers (25 x 32, x 295, y 51..83, 7x7
  cross, the label line is now a key).
- **Preview** for Claude before flashing: `docs/shots/2026-11-11-map-tabs-preview.png`. It is
  **641x267 = two 1:1 320x240 frames side by side** (left: tabs out; right: tabs hidden) plus a 12 px
  title strip and a 15 px footer. One 320x240 file cannot show both states at 1:1, and a downscaled
  one would hide exactly the pixels under review (the 1 px cross, the 5x7 arrow, the 4 px sliver).
  Both panels are drawn from the REAL rectangles and colours: a small host program builds
  `ui/map_chrome.cpp` + `theme.h` and prints every rect and the RGB565 values, and the renderer draws
  those; the two icon masks are parsed from `ui/icons.cpp`, the lock and the `+`/`-` from the same
  constants the firmware uses. The tile background is a crop of
  `map-preview-ljubljana-z13-2026-11-09.png`; there are no node positions and no names. The gold
  dotted box marks the 28x22 tap target (annotation, not drawn by the firmware). The fonts are DejaVu
  stand-ins at the device's 6 px mono advance, not the VLW faces. The renderer itself lives outside
  the repo (scratch), like the EST-372 one; say if it should move into `tools/`.

### Could not check (no device, no serial, no glass)
- The glass: whether `#202020` at 1 px reads on the real topo tiles as well as the measurement says,
  whether a 25 px tab still feels big enough for a fingertip (the ticket's "about 28" would have been
  3 px wider per side), the 5x7 arrow's size, and the 4 px sliver being visible enough at the edge.
- Touch on the arrow target: the test proves the rects; only a finger on the panel proves the feel.
- Nothing was flashed, committed, pushed or deployed from this sandbox.

### Next step for Claude
1. Review the diff and the preview, then flash `firmware.bin` (2,804,736 B). The boot log should say
   `[map] layers traces=1 names=1 hidden=0` on a first boot after the update.
2. On the Map: the cross should be a bare dark cross with no plate; five 25 px tabs, gold on Traces and
   Names; `z13` and a small `>` at the top right.
3. Tap the label line (anywhere in x 292..320, y 26..48): the tabs shrink to 4 px slivers, the arrow
   flips to `<`, the map is drawn to the edge, and `[map] tabs hidden` appears. Tap again (or tap a
   sliver): they come back and the log says `[map] tabs shown`.
4. Tap each tab centre: Names (307,67), Traces (307,100), follow (307,133), `+` (307,166), `-`
   (307,199). Tap x 294 on the same rows: that must now be a map tap, not a tab.
5. Leave the map with the tabs hidden, go to another tab and back: they must still be hidden (the
   setting is saved). Power cycle if convenient: `hidden=1` in the boot line.

## 2026-11-11 00:56 (Claude): EST-373 on the device
- Built in the sandbox (exit 0, RAM 156,800 B, flash 2,804,373 B), tests exit 0, the reviewed binary flashed over USB OTA (2,804,736 B). Boot: `[map] layers traces=1 names=0 hidden=0` (Names off is Filip's saved choice), `mesh: up`, `chats` 6 chats, 0 mismatches.
- On the glass (USB taps + screenshots): plain dark 7x7 cross; five 25 px tabs flush right; a tap on the arrow at (306,37) hides them to 4 px slivers, a second tap brings them back. Left as shown.
- NOT in this build: the four EST-373 comments (the whole bottom bar hiding too, the 22 px chip with an unread dot, a full-height canvas so the cross does not move, the grey + / - left behind while hidden). DS worked from the ticket text only. They go into the next ticket.
- This was the last flash of 2026-11-11 night; the device is switched off.

## 2026-11-11 01:23 (DeepSeek): EST-374 full-screen map canvas, tucked bottom bar, 22 px chip, ghost zooms

The rest of Filip's "hide" idea (2026-11-11 00:38-00:48), on top of EST-373. Seven parts, A to G;
every one of them is in this build. Nothing was flashed, committed, pushed or deployed from the
sandbox, and the device is off: this entry is the sandbox's evidence only.

### A. The map canvas is the full map area, in both states

- `ui/map_chrome.h`: `kMapCanvasTop = kStatusH` (26), `kMapCanvasH = 240 - 26 = 214`,
  `kMapCentreX = 160`, `kMapCentreY = 26 + 107 = **133**` (was 107). The view rectangle IS the
  canvas now: `Rect{0, 26, 320, 214}`, so `mapCentreOf()` and the cross follow it without a second
  number.
- `map.cpp`: new `kViewTop`; `mapRender()` and `mapRepush()` push at `kViewTop + yy` (thirteen 16-row
  slices plus one of 6 rows; 320 x 16 x 2 = 10,240 B each, well under the 32,767 B SPI cap), `mapProject()` returns
  SCREEN y (canvas y + 26) and `mapScreenToLatLon()` subtracts the same origin, so markers, the
  cross, the trace points and the RPTR picker all speak one coordinate system.
- `screens/map_tab.cpp`: the marker cull band is now 22..244 instead of -4..218.
- `ui/rptr_layout.cpp`: the Position picker's map is `Rect{0, 26, 320, 214}` and its crosshair moved
  from a 12x12 box at (154,101) to (154,**127**), i.e. the same (160,133) centre. It used to be the
  same pixel as the cross only by the accident of the canvas starting at y 0.
- `ui/map_chrome.cpp`: the attribution's floor is now the lowest clear row (`creditBottom`): the
  shown bar's top (215) or the tucked strip's top (237), so the licence line stays on the glass in
  both states. Shown it is still exactly x 6..144, y 199..209; hidden it is 221..231.
- Hide/show draws the chrome over the canvas that is already in PSRAM (`mapScreenAnimate` /
  `mapRepush`): no tile is decoded and no view moves, so the cross, the markers and the tiles hold
  still. The EST-371 cached path can no longer push a shorter frame: the canvas covers every row
  from 26 to 240, so a repush replaces all of the map area (before, rows 214 and the 26 rows under
  the status bar were not part of the pushed frame at all).

**RAM/PSRAM, before and after (the ticket asked):**
- Static RAM: **156,800 B** before and **156,800 B** after (47.9 % of 327,680). The new numbers live
  in `MapChrome`, which is a stack temporary, so `.bss`/`.data` did not move by a byte.
- The tile canvas is PSRAM: **320 x 214 x 2 = 136,960 B before and 136,960 B after, so +0 B.** The
  ticket's "the canvas grows by ~30 rows, ~19 KB" does not apply to this tree: the canvas was
  ALREADY 214 rows tall, but it was pushed at y 0, so 26 of its rows sat under the status bar and
  its last row (214) stopped one row short of the tab bar (215). What changed is where those rows
  are, not how many. (If the canvas had been the visible band only, 320 x 189 x 2 = 120,960 B,
  the growth would have been +16,000 B; that is where the estimate's order of magnitude comes
  from.)
- The frame buffer is unchanged (320 x 240 x 2 = 153,600 B PSRAM). `MapChrome` itself grew by 5
  rectangles and one int16 (48 B) on the stack of the Map paths.

### B. The arrow hides the whole bottom bar too

- One tap hides BOTH groups: the five tabs to their slivers and the whole bottom page bar
  (Nodes/Chat/Map/RPTR) to a 3 px strip. One saved key (`tabsHidden`, EST-373) drives both; no new
  preference.
- `ui/map_chrome.cpp`: `barStrip = Rect{0, 237, 320, 3}`, `creditBottom` follows it, and
  `mapHitAt()` has **no rectangle for the strip**: those rows fall through to `Map`, so the strip
  takes no taps and a drag that starts on it pans (the shell's drag path never reads chrome rects,
  and the rows are inside `c.view`).
- `ui/screen_map.cpp` `drawBarStrip()`: kPanel fill for the 3 rows plus the Map tab's gold segment
  in its own x range (160..240) at the bar's own 2 px (`kTabTopLine`). That is what the real bar
  leaves behind when it slides by 22 px, so the strip reads as the bar tucked under. The ticket said
  "in kPanel": the fill is kPanel, the segment is the tab's gold, and the segment is 2 of the 3 rows.
- `main.cpp`: while the Map's bars are tucked the shell does not read the tab bar at all
  (`const bool tucked = mapBarsTucked(tabsHidden, true); const int tab = tucked ? -1 : L.tabAt(x,y)`).
  Shown, the four zones are exactly as before (80 px each at y 215..240).
- "Leaving the Map by any route shows the bar on the next screen": the tuck is the MAP's own chrome
  (only `screen_map.cpp` can draw it), every other screen calls `drawTabBar()`, and `showTab()` is
  the only writer of the page variable. `showTab()` now calls `om::map_tab::leave()` whenever it
  goes from tab 2 to another tab, and `leave()` logs
  `[map] leave: the next screen draws its own full bar (tucked key=N)`.
  The page-switch routes, all of them through `showTab()` (`test_est374.py` lists them on every run):
  1. `main.cpp:1242` `chatOpenDmFor()` (a DM opened from the Nodes sheet or a notification)
  2. `main.cpp:1303` the message sheet's Info action, back to Nodes
  3. `main.cpp:1314` the message sheet's ShowOnMap
  4. `main.cpp:1426` the Chat tab's bar
  5. `main.cpp:4913` the Nodes sheet's ShowOnMap
  6. `main.cpp:4985` the Nodes sheet's LoginRptr
  7. `main.cpp:5041` the Nodes tab's bar
  8. `main.cpp:5073` the Map tab's bar (skipped while tucked)
  9. `main.cpp:5089` the RPTR tab's bar
  The USB `tap` command feeds the same touch samples into the same `handleTap()`, so it takes that
  path too. Settings is the one screen with no bottom page bar at all (since EST-347 it is a full
  page with its own header and Back returns to the tab it came from); nothing can leak into it and
  Back returns to the Map with the user's saved state.

### C. The toggle is a 22 px chip

- `ui/map_chrome.h`: `chip = Rect{298, 26, 22, 22}` (kPanel fill, kLine edge, `kRadius` 4),
  flush with the tabs' right edge (320) and starting on the status bar's bottom row.
- `chevron = Rect{307, 33, 4, 7}`, a 2 px stroke (`kMapChevronStroke`), drawn by
  `drawChevron()` from primitives: ">" while the bars are out, "<" while they are tucked, in kInk.
- `chipTap = Rect{290, 26, 30, 26}`: at least 30 x 26, flush right, and it ends at y 52. The top tab
  therefore starts at 56, so the five tabs are **31 px** each (was 32) and move 51..83 -> 56..87.
  That 1 px per tab is the only number this ticket takes away from EST-373, and it is the price of
  a 26 px target that never overlaps the top tab (26 + 26 + 2 = 54 for the column's top edge).
- `zoomLabel` is now `Rect{275, 31, 21, 11}`: immediately left of the chip (right edge 296, one
  `kMapChipGap` of 2 px), the same vertical centre as the chip, drawn in `kMapCrossInk` (#202020,
  the cross's ink) instead of kDim. It never moves between states and it is not a key any more; the
  chip is.

### D. The unread hint while hidden

- `mapChipShowsUnread(tabsHidden, unread)` = hidden AND unread > 0, and
  `mapChipUnreadDotRect()` = `Rect{298, 26, 6, 6}`: a 6 px box on the chip's top-left corner, drawn
  as a kAcc smooth circle (r = 3, so 7 px of ink with the anti-aliased edge, the same size family
  as the bar's 5 px new-node dot). Its first row IS the map area's first row, so the status bar,
  drawn last, cannot eat it. The count comes from `v.tabs.unread`, the shell's one
  `currentTabBar()` model (no second unread source).

### E. Zoom stays usable while hidden (Filip)

- `ui/theme.h`: `kMapGhostInk = #707070`. `ui/screen_map.cpp`: ONE `drawZoomGlyph()` draws the "+"
  and the "-" for both states (same SansTitle character, same datum, same centre); the shown tab
  passes the kPanel plate and kInk, the hidden state passes no plate and the grey.
- The ghost boxes are `zoomInShown` / `zoomOutShown` in `MapChrome`: the SHOWN-state tab rectangles
  (295..320 x 152..183 and 184..215), so the glyphs sit on exactly the same centres (307,167) and
  (307,199) as on the tab.
- The tap zones are those same rectangles, active only while hidden: a tap in the ghost zone zooms,
  a tap 2 px left of it is a map tap, and the 3 px sliver at 317..320 keeps its EST-373 meaning
  (bring the bars back) because the sliver is checked first.
- Measured contrast of #707070 on the real z13 tile: **4.34:1** on the #f0f0e0 ground (34.6 % of the
  image), **3.78:1** on #e0e0e0, **3.23:1** on the #d0d0d0 road grey, **3.69:1** on the map canvas
  itself (#191816), and 1.08:1 on the darkest 1 % (#6c6c6a): a grey glyph on a dark thin map line
  blends in, which is the honest limit of a mid grey. The glyph is a 2 px stroke several pixels wide,
  so on the light ground (where most of it lands) it reads.

### F. Slivers

- `kMapTabSliverW = 3` (was 4): the five slivers are at x 317..320, at their shown rows and height.
  A tap on one still brings both groups back (nothing invisible fires), and a drag that starts on
  one still pans: the sliver is inside the map's view and the shell's drag path reads no chrome
  rectangle.

### G. Motion

- `kMapSlideSteps = 4`: three interpolated frames (5, 11 and 16 px of the 22) plus the steady frame.
  The tabs ride right by `tabDx` (drawn from the shown geometry, so the icons ride out and are
  clipped by the screen edge) and the bar drops by the same `barDy` through the new
  `drawTabBarAt(d, L, m, dy)`, which is the one tab bar drawing moved by dy (so the last rows of the
  slide are the real bar's top rows). `static_assert(kMapTabSlideDx == kMapBarSlideDy)` keeps the two
  groups on one number.
- Chrome only: every frame repushes the cached canvas and repaints the chrome, through the same
  `omFrameBegin/omFrameEnd` frame buffer; no backlight dip, no tile decode. With no canvas yet (a
  first Map entry after boot) the loop is skipped and the change is the one steady frame.
- Cost estimate from EST-371's measured numbers, as the ticket asked: one frame is a 19,790 us
  cached-canvas repush plus an 11,500 us panel push = **31.3 ms**, under the ticket's ~35 ms limit,
  and 4 x 31.3 = **125 ms**, inside the 120-150 ms window. So the slide is implemented, not skipped.
  The device measures it for real: the new log line is
  `[map] bars tuck: 3 slide frame(s) + 1 in N ms` (and the same for `restore`). If that N/4 exceeds
  35 ms on the glass, set `kMapSlideSteps = 1` in `ui/map_chrome.h` and the switch is instant again;
  the log then says `0 slide frame(s) + 1 ... (instant: no canvas)`.
- The chip, its chevron and the unread dot are drawn in every frame, so they flip with the first
  frame and stay put; the ghost + / - appear only in the steady hidden frame (they are "hidden" as
  soon as the slide starts).

### Build and tests

- `pio run -e om_lite_l2` **exit 0**: RAM **156,800 B** (47.9 %; EST-373 was the same), flash
  **2,805,873 B** (42.8 %; EST-373 was 2,804,373, so **+1,500 B**), `firmware.bin` **2,806,240 B**
  (+1,504 B).
- `sh test/host/run.sh` **exit 0** (full suite). New:
  - `test/host/test_est374.cpp` (BIN36): the canvas is 320x214 at y 26 and the centre (160,133) is
    the same in both states; the chip 22x22 at 298,26 with its 4x7 2 px chevron; the 30x26 target
    inside the map band and never on the top tab (the top tab starts 2 px below it); the tucked
    strip 0,237,320,3 with no tap zone and inside the pan area; `mapBarsTucked()` false for every
    non-Map tab; the unread dot's rule and box; the five 3 px slivers at 317..320 with a tap that
    brings the bars back; the ghost zones EQUAL to the shown zoom rectangles (centre, size), a tap in
    one zooms, 2 px left of one is a map tap, the sliver wins on its 3 px, a marker still wins over
    everything; the attribution above the bar and above the strip; the picker's crosshair on the
    same centre; and the slide's 5/11/16 px steps with the 31.3 ms / 125 ms estimate.
  - `test/host/test_est374.py`: the page-switch funnel. Exactly ONE writer of the page variable (in
    `showTab()`), 9 `showTab()` call sites (listed above, printed on every run), `showTab()` calls
    `map_tab::leave()` when it leaves the Map, every other screen draws its own bar and does not
    read `tabsHidden`, and `mapBarsTucked()` is only read by `main.cpp`.
  - Updated to the new numbers: `test_est351.cpp` (view/chip/label/target, 31 px tabs, the
    attribution's floor), `test_est366.cpp` (the picker's map and crosshair), `test_est370.cpp`
    (Names 56..87, centre 160,133), `test_est373.cpp` (the arrow section moved to est374; 31 px,
    cross at 157,130), `test_tab_bar_calls.py` (definitions are not call sites, so the new
    `drawTabBarAt()` wrapper does not trip its own check).
- **Preview** for Claude before flashing: `docs/shots/2026-11-11-map-fullscreen-preview.png`. It is
  **964x301 = three 1:1 320x240 frames side by side** (bars out / tucked / tucked with an unread
  dot) plus a 16 px title strip, a caption per frame and two footer lines. Every rectangle and
  colour comes from the real `ui/map_chrome.h` + `theme.h` (a small host program prints them and the
  renderer draws them, so no number is copied), the two tab icons are the masks parsed out of
  `ui/icons.cpp`, the fonts are DejaVu stand-ins and the map is a crop of
  `map-preview-ljubljana-z13-2026-11-09.png`. No node positions and no names are drawn. The gold
  dotted box is an annotation: it is the 30x26 chip target, not something the firmware draws. The
  renderer lives outside the repo (scratch), like the EST-372/373 ones; say if it should move into
  `tools/`.

### Could not check (no device, no serial, no glass)

- The glass: whether the 3 px strip's 2 px gold segment really reads as "tucked under", whether the
  grey ghosts are strong enough on a bright tile (4.1:1 says yes) and quiet enough on a busy one,
  whether 22 px of chip is enough for a fingertip in practice (the target is 30x26), and whether the
  31 px tabs still feel big enough.
- The slide: the estimate is 31.3 ms per frame from EST-371's measured repush and push, not a
  measurement of this build. The first flash's `[map] bars tuck ... in N ms` line is the number.
- The device PSRAM figure before/after: the sandbox has no serial device. `status` prints
  `psram=` and the boot line `[om] frame buffer`; the arithmetic above says +0 B for the canvas.
- Touch on the new targets, and the "no tap on the strip" rule with a real finger (the host test
  proves the rectangles, not the feel).
- One thing the ticket did not ask about, left as it was: the callout still opens at 167..207 (above
  where the shown bar starts) in both states, so with the bars tucked there is 30 px of empty map
  under it.

### Next step for Claude

1. Review the diff and the preview, then flash `firmware.bin` (2,806,240 B). The boot line is still
   `[map] layers traces=.. names=.. hidden=..`.
2. Open the Map: the canvas should fill from under the status bar to the very bottom, the cross
   should be at the middle of that area, `z13` immediately left of a 22 px chip with `>` at the top
   right, and the five tabs below it (Names 56..87).
3. Tap the chip: both groups should slide (about 125 ms) to the slivers and the strip, the chevron
   should flip to `<`, and the log should say `[map] bars hidden` then `[map] bars tuck: 3 slide
   frame(s) + 1 in N ms`. Write N in this file. Tap it again: `bars restore`, the same N.
4. While tucked: tap the ghost `+` / `-` (307,167) and (307,199): the zoom changes and the ghost
   moves to the right tab's centre. Tap 2 px left of a ghost zone: it must be a map tap (the callout
   closes, no zoom line in the log). Tap a sliver: the bars come back.
5. Leave the Map with the bars tucked, by the tab bar's own bar and by a Nodes sheet's Show on map,
   then come back: the log line `[map] leave: the next screen draws its own full bar (tucked key=1)`
   followed by the next screen's own bar, and the Map comes back tucked.
6. With an unread message and the bars tucked, the chip's corner should carry a small gold dot; the
   bottom bar's badge is not visible then by design.
7. Quick regression: the Position picker (RPTR > a repeater > Position > pick on map) must show its
   crosshair at the canvas centre (160,133), and `status` should print the same `psram=` as before
   within a few KB.
8. If the slide feels too slow or the ghosts too faint, the two knobs are `kMapSlideSteps` and
   `kMapGhostInk` in `ui/map_chrome.h` / `ui/theme.h`.

## 2026-11-11 01:30 (Claude): EST-374 reviewed and built, NOT on the device
- DS's run ended on the 30-minute cap (exit 124) after the work and this log were written, so Paperclip marked it blocked; nothing was missing except item F's rounded slivers, which were added to the description after DS had started (still square, 3 px).
- Review: projection and its inverse both shift by the same 26 px (`mapProject` +kViewTop, `mapScreenToLatLon` -kViewTop); every outside caller (traces, markers, cull band, the RPTR picker at kMapCentreX/Y) uses the screen form. Preview checked (three frames, no positions or names).
- Sandbox build exit 0 (RAM 156,800 B, flash 2,805,873 B, firmware.bin 2,806,240 B), tests exit 0. Committed locally, **not pushed and not flashed**: the device is off for the night. First flash tomorrow, then push.

## 2026-11-11 01:37 (DeepSeek): EST-375 rounded hidden map tabs, built and tested, NOT on the device

**Ticket (Filip 2026-11-11 00:57):** when the button-tabs are hidden, the remaining edge is sharp;
it should have rounded corners, like when extended. One shape, not a new redraw path.

**What changed**
- `ui/map_chrome.h`: `kMapTabSliverW = kRadius + 2` = **6 px**, so the hidden tab is the left-rounded
  end (both 4 px corners) plus 2 px of straight edge, at **x 314..320**. It has the same rows and
  heights as the shown tabs (56..87, 88..119, 120..151, 152..183, 184..215) and the same fill/edge
  colours (gold `kSelBg2`/`kAcc` when the switch is on, `kPanel`/`kLine` otherwise).
- New `mapEdgeTabDrawRect()` in the same header is the one rectangle the rounded draw paints: the
  visible rect extended right by `kRadius`, so the hidden sliver's box is x 314..324 and the glass
  clips it at 320. `screen_map.cpp`'s `edgeTabBox()` uses it for BOTH the shown tab and the hidden
  sliver; `edgeTabSliver` is deleted and `drawTabsHidden()` calls `edgeTabBox()` on the same
  `mapTabRects()` rectangle, so there is no second sliver drawing.
- Slide: the tab now travels `kMapTabSlideDx = 25 - 6 = 19 px` while the bar still drops
  `kMapBarSlideDy = 22 px`. `slideBars()` carries a `tabOff` and a `barOff` (4, 9, 14 for the tabs;
  5, 11, 16 for the bar). The last tab frame is the shown tab at x 314 and the steady hidden frame is
  the same 6 px tab at x 314; the header's `static_assert`s now check that equality instead of the
  old "both move 22" one.
- Tap rules unchanged: the sliver's 6 px still answers `TabsToggle` and wins over the ghost zoom
  zones; the ghost `+` / `-` centres stay at **(307,167)** and **(307,199)**, 7 px left of every
  sliver, so neither glyph is covered.

**Build** (the `docs/REBUILD.md` command, real exit code)
- `pio run -e om_lite_l2` **exit 0**, SUCCESS in 54.5 s. RAM **156,800 B** of 327,680 (47.9 %,
  unchanged); flash **2,805,865 B** of 6,553,600 (42.8 %, previous 2,805,873, **-8 B**);
  `firmware.bin` **2,806,224 B** (previous 2,806,240, **-16 B**). The shape change is a wash.

**Tests** — `sh test/host/run.sh` **exit 0**.
- `test_est373.cpp`: the hidden rectangle is checked explicitly at **314..320, w 6** (and
  `kMapTabSliverW == 6`) in addition to the existing four-corner draw/tap checks.
- `test_est374.cpp`: `kMapTabSliverW == kRadius + 2 == 6`; all five slivers at x 314..320, same rows
  and heights; `mapEdgeTabDrawRect()` goes to **324 = 320 + kRadius** (the ticket's "otherwise" branch:
  the harness has no canvas, so it asserts the rectangle handed to the rounded draw); the ghost
  centres are 307 and are not inside any sliver; a tap on the sliver's first column and on x 319 is
  `TabsToggle`; the tab slide is **19 px**, the last frame and the steady sliver are the same tab at
  x 314, and the bar still goes 5, 11, 16 of 22 px. Added a check that the two offsets are no longer
  equal, so a future edit cannot quietly make the tab over/undershoot.

**Preview** — `docs/shots/2026-11-11-rounded-slivers-preview.png` (780x430): one 1:1 hidden frame
(Traces on, so its sliver is gold; ghost `+` / `-` visible at 307) plus a **4x** crop of the Traces
sliver with the glass edge marked. Every rectangle and colour is printed by a small host program
linked against the real `ui/map_chrome.cpp` + `theme.h`; the renderer is scratch outside the repo
(like EST-372/373/374), so the rounded pixels are Pillow's stand-in, not the device's.

**Could not check (no device, no serial, no glass)**
- The actual rounded pixels on the L2: the host harness has no canvas, so it proves the rectangle
  handed to `fillRoundRect`/`drawRoundRect` (x 314, w 10 = 6 + kRadius), not the framebuffer. The
  first flash is the real check.
- Whether 6 px reads as a rounded tab end under a fingertip and whether the gold sliver still reads
  as "tucked under"; and the measured slide time (the estimate is still 4 x 31 ms).
- Touch-on-the-corner feel: the tap zone is the full 6 x 31 rectangle, so the visually unset corner
  pixels still bring the bars back, by design (the drawn rect is the tap rect, EST-373's rule).

**Next step for Claude**
1. Review the diff and the preview, then flash `firmware.bin` (2,806,224 B); the boot line is still
   `[map] layers traces=.. names=.. hidden=..`.
2. Open the Map, tap the chip, and look at the right edge: five rounded stubs at 314..320, the Traces
   one gold if traces are on, no square strip. Tap one: the bars come back and the log still says
   `[map] bars hidden` / `[map] bars shown` + `tuck/restore: 3 slide frame(s) + 1 in N ms`.
3. While hidden, tap the ghost `+` / `-` at (307,167)/(307,199): they must still zoom and must not be
   covered by the sliver; a tap at x 314..320 on those rows must still bring the bars back.
4. Quick regression: the bottom bar is still the 3 px strip, the chip/chevron/unread dot unchanged,
   and the Position picker's crosshair still at (160,133).

## 2026-11-11 01:43 (Claude): EST-375 reviewed and built, NOT on the device
- One shape: the hidden tab is the same edgeTabBox through mapEdgeTabDrawRect (6 px on the glass, both 4 px corners), edgeTabSliver removed; the tab slide is 19 px, the bar 22 px. Preview checked. Sandbox build exit 0 (flash 2,805,865 B, firmware.bin 2,806,224 B), tests exit 0. Committed locally, not pushed, not flashed.

## 2026-11-11 02:00 (DeepSeek): EST-376 radio A/B commands and the read-only charge probe, built and tested, NOT on the device

**Ticket:** diagnostics only, for tomorrow's device session. Nothing the user sees changes, no default
moves (TCXO stays 1.8 V, boosted gain stays off, no register is written to 0x22). Everything is a USB
command in `tools/cmd.py` / `src/usb_commands.cpp`.

**What changed**

1. `radio tcxo 1.8|3.0` and `radio boost on|off` (RAM only; a reboot restores `target.h`'s 1.8 V and
   the build's boosted-gain default).
   - The command only RECORDS the request and answers at once (`pending=1`). The RadioLib work runs
     in `rdiag::loop()` on the loop task: `SX1262::setTCXO` / `setRxBoostedGainMode`, then the RX
     re-entry, then the noise restart. That is the EST-350 rule: no radio work on the USB command
     stack. The status code is therefore in the loop's `[radio] apply tcxo=.. tcxo_status=..` line and
     in the next `radio status` (see the how-to below).
   - **`resetAGC()` is the one that really clears `_noise_floor`, not `triggerNoiseFloorCalibrate()`.**
     Read in `RadioLibWrappers.cpp`: `resetAGC()` runs `doResetAGC()` (CustomSX1262Wrapper ->
     `sx126xResetAGC`: warm sleep, standby, Calibrate(0x7F), image calibration), sets `state =
     STATE_IDLE` so the next `recvRaw()` calls `startReceive()` (MeshCore's own path back into RX),
     and sets `_noise_floor = 0`, `_num_floor_samples = 0`, `_floor_sample_sum = 0`.
     `triggerNoiseFloorCalibrate(threshold)` only stores the threshold and, when 64 samples are
     already in, clears the two counters; it leaves `_noise_floor` at its old value. The companion
     build's `getAGCResetInterval()` is `_prefs.n * 4000` with `n = 0` by default, so MeshCore does
     not reset the receiver by itself here; this command is the only reset.
   - Ordering detail: `sx126xResetAGC()` re-applies the RX gain only under `#ifdef
     SX126X_RX_BOOSTED_GAIN`, which this build does not define. So after the reset the chip is read
     back and the boost is set again if the reset lost it; the log prints the read-back, not the
     request.
   - `resetAGC()` refuses mid-packet (and while a received packet is still unread). We check
     `isReceiving()` first, print `[radio] apply waiting: rx busy` once, and keep the request queued.
     If the reset still loses the race, a zero noise floor is `resetAGC`'s own mark: anything else
     re-arms one retry (`[radio] agc reset done`).
2. `radio sample <10..600>`: the command starts the sampler and answers; `rdiag::loop()` prints one
   line every 5 s (`[rsamp] t=.. nf=.. tcxo=.. boost=..`), one line per received packet
   (`[rsamp] pkt rssi=.. snr=.. hash=..`) and one summary at the end (samples, min/median/max/mean
   noise floor, packet count, mean RSSI and SNR). No message text and no names: `hash=` is the first
   PAYLOAD byte (two hex digits), the same byte `radio packets` calls `from=` (a sender prefix for an
   advert/response, the channel hash for a group text). A second `radio sample` while one runs is
   refused. `hash=` is parsed by the new `om::radio::payloadOffset()` so the ring and the sampler
   cannot disagree about which byte it is.
   - The packet line is written from `recvRaw()` (the loop task) and only while a sample runs.
   - The 5 s grid is re-based on the actual print time, so a long loop pass makes the next line late
     instead of printing a burst of duplicate samples. The noise-floor values are MeshCore's own
     `_noise_floor`; a `0` means "reset, still converging (64 quiet samples)", not a reading.
3. `radio status`: one data line `[radio] tcxo=.. boost=.. tcxo_status=.. boost_status=..` and then
   the SAME answer line plain `radio` prints. `-` means no runtime apply yet (1.8 V / boot default).
   `boost` is the chip's RX gain register read back by the loop (refreshed every 5 s and after an
   apply), never the last request; no SPI read happens on the command stack.
4. `pmu probe` (new verb): READ ONLY.
   - Reads 0x01..0x10, 0x3C, 0x3D, 0x40 and 0x41 of the port controller at 0x22 and prints them as
     hex. It never reads 0x3E, 0x3F, 0x42 (interrupt registers, clear on read) or 0x43 (the FIFO, a
     read pops a byte): the list is `diag_logic.h`'s `kFusbReadRegs`, the rule is `fusbReadable()`,
     and both are asserted in the host test (no duplicates, no forbidden register, all 20 present).
     There is no write function for 0x22 anywhere in the tree; the only thing sent to the chip is the
     register pointer of a read transaction.
   - Prints the fields it can: DEVICE_ID 0x01 (version nibble 7:4, product nibble 3:0), POWER 0x0B
     (PWR3..PWR0), STATUS0 0x40 (VBUSOK bit 7, BC_LVL bits 5:4 with the 100 mA / 500 mA / 1.5 A / 3 A
     word, COMP bit 2). **Only VBUSOK's position is specified by the ticket; the BC_LVL 5:4 and COMP
     bit 2 positions come from the FUSB302 datasheet and are not verified against a real read yet**
     (the raw byte is printed next to them, so a mismatch is one constant away in `diag_logic.h`).
   - Then the ADS1115 (0x48) single-ended AIN1, AIN2, AIN3 once each, and AIN0 LAST, all with the
     battery read's own config byte and scale (only the MUX field changes: 0xC3/0xD3/0xE3/0xF3), so
     the chip is left exactly as a battery read leaves it (AIN0 conversion, sense rail off). Printed
     as `ain0=1234mV(9876)` (mV at the pin, plus the ADC's raw counts, which make a divider ratio
     computable tomorrow). Cell voltage is 2 x AIN0, as before.
   - The EST-372 boot probe of 0x22 (`in=0x00 cfg=0x24`, meaningless for this chip) is REMOVED; the
     0x34 and 0x14 raw lines stay.
5. New files: `src/diag_logic.{h,cpp}` (plain C++: summary maths + FUSB302 decode), `src/radio_diag.{h,cpp}`
   (the loop-side A/B apply, sampler and packet line). `target.{h,cpp}` gained the two thin RadioLib
   calls; `board.{h,cpp}` gained `usbPdReadRegister()`, `adcProbe()` and the shared per-channel ADC
   read (the battery path now calls the same one, same numbers).

**Build** (`docs/REBUILD.md` command, real exit code)
- `pio run -e om_lite_l2` **exit 0**, SUCCESS. RAM **157,384 B** of 327,680 (48.0 %; EST-375 was
  156,800, **+584 B**), flash **2,810,929 B** of 6,553,600 (42.9 %; EST-375 was 2,805,865,
  **+5,064 B**), `firmware.bin` **2,811,296 B** (EST-375 was 2,806,224, **+5,072 B**). Only the two
  pre-existing RadioLib `#warning`s (god mode, USB CDC), no warning from the new code.

**Tests** (`sh test/host/run.sh` **exit 0**)
- New `test_est376.cpp` (BIN37, plain g++ with `diag_logic.cpp` + `radio_stats.cpp` +
  `usb_cmd_logic.cpp`):
  - parsing/refusals: `radio tcxo 1.8` / `3.0` (also quoted) accepted, `1.7`, `2.4`, `0`, `3` are
    `value`, a missing or extra word is `args`; `radio boost on|off` accepted, `1`/`true` `value`;
    `radio sample 10|60|600` accepted, `9`/`601`/`0` `value`, `60s`/`-5` `number`, missing/extra
    `args`; `radio status`/`packets`/plain `radio` still parse; `radio nope` and a misspelled
    sub-command are `args`; `pmu probe` parses, `pmu`, `pmu read`, `pmu probe 1` are refused.
  - summary maths: {-80,-90,-100,-110} -> min -110, max -80, median -95, mean -95, sorted in place;
    odd {-1..} middle value; {-1,-2,-3,-4} -> median -2, mean -2 (negative truncation toward zero);
    one value; the empty list is refused with a zeroed result; a full 120-value run (-60..59) ->
    min -60, max 59, median 0, mean 0.
  - FUSB302: 20 registers, no duplicate, every one readable; 0x3E/0x3F/0x42/0x43 all refused;
    DEVICE_ID 0x88 -> version 0x8 / product 0x8; POWER 0x0F; STATUS0 0x92 -> VBUSOK 1, BC_LVL 1,
    COMP 0; STATUS0 0x64 -> VBUSOK 0, BC_LVL 2, COMP 1; VBUSOK is bit 7 and no other single bit sets
    it; missing registers are "no field", not a fabricated zero; the four BC_LVL words.
  - payload offset: flood/direct/transport-coded routes, 1/2/3-byte path hashes, and the short-frame
    -1.
- The rest of the suite is unchanged and green (all 37 C++ binaries + 5 Python checks).

**How to run tomorrow's test** (one reader on the port; `OM_PORT=/dev/ttyACM0`)
1. Flash `firmware.bin` (2,811,296 B). Boot log: the `[i2c]` sweeps, `[pmu] 0x34 reg03=..`, the two
   0x34 raw registers and `[pmu] 0x14 reg00=..`. There must be **no `[pmu] 0x22` line any more**.
2. `tools/cmd.py radio status` -> a `[radio] tcxo=1.8 boost=0 tcxo_status=- boost_status=-` line
   then the usual `[cmd] ok radio noise=..` line. That is the baseline; note both.
3. `tools/cmd.py radio tcxo 3.0` -> `[cmd] ok radio tcxo volts=3.0 pending=1`. The loop then prints
   `[radio] apply tcxo=3.0 tcxo_status=0(ok) boost=0 boost_status=-1(err) nf=<old>->0` (the `-1`
   means that change did not ask RadioLib). `tools/cmd.py radio status` now shows `tcxo=3.0
   tcxo_status=0`. A non-zero status is the honest answer; the chip keeps the last good value.
4. **Watch the sample lines on a reader that stays open** (`pio device monitor -b 115200`, picocom,
   `timeout 200 cat /dev/ttyACM0`): `cmd.py` stops at its answer line and closes the port, so the
   5 s `[rsamp]` lines and the summary are only visible to a reader that is still reading. The
   command can be typed into the monitor itself, because OMCMD1 is plain text: type
   `OMCMD1 16` + Enter, then `radio sample 180` (that is exactly 16 bytes; the length must be
   right). A CR/LF after the 16 bytes is harmless junk before the next magic. `cmd.py` and the
   monitor must not both read the port.
   Expected: `[rsamp] t=5 nf=.. tcxo=3.0 boost=0` ... one per 5 s, a `[rsamp] pkt ...` for each
   packet heard, then `[rsamp] done secs=180 samples=36 nf_min=.. nf_med=.. nf_max=.. nf_mean=..
   pkts=.. rssi_mean=.. snr_mean=..`. Wait ~5 s after a TCXO/boost change before starting the sample,
   otherwise the first line can be the reset's `nf=0` (still converging, 64 quiet samples).
5. A/B, same spot, 3 minutes each: (1.8/off) -> (3.0/off) -> (1.8/on) -> (3.0/on), one `radio tcxo`
   / `radio boost` then `radio sample 180` per step. Compare `nf_mean`, `nf_min` and the packet
   count at the same spot.
6. `tools/cmd.py pmu probe`, once on USB and once on battery: three `[pmu]` data lines (raw register
   dump, the FUSB302 fields, the four ADC inputs) then `[cmd] ok pmu probe usb=0x22 regs=20/20
   vbusok=<0|1> adc=4/4`. On USB, VBUSOK should be 1 and BC_LVL should name the charger's level; on
   battery VBUSOK should be 0. If 0x22 does not answer at all, the fields line is all `-` and
   `regs=0/20`: that is a finding, not an error.
7. Regression: nothing on the glass changes. `status` and `radio` print the same lines as before
   (`radio` is untouched; only the new `radio status` adds the tcxo/boost line). `radio sample` never
   writes SD.

**Could not check (no device, no radio, no USB-C charger, no glass)**
- The real RadioLib status of `setTCXO(3.0f)` on this chip, whether the TCXO restarts at 3.0 V after
  the warm sleep, and whether Wio-SX1262 really wants 3.0 V (the research note's Meshtastic PR).
- The actual noise floor at each combination: that is the point of tomorrow's run.
- The FUSB302's identity and the BC_LVL/COMP bit positions: first real read decides. VBUSOK = bit 7
  is the ticket's own rule.
- Whether AIN1/AIN2/AIN3 are a VBUS or solar divider (the mV numbers tomorrow say if any moves).
- The ADS1115's scale: the config byte sets PGA = 000 (+/-6.144 V) while every read scales with the
  existing 4.096 V divisor, so the printed mV (and today's battery mV) are 2/3 of the true pin volts.
  **Not changed here** (it would move the battery percentage); worth a separate ticket.
- The wire timing of `pmu probe` (about 20 register reads plus four ADC conversions at 5 + 12 ms each:
  roughly 80-100 ms on the command stack, no radio and no SD).

**Next step for Claude**
1. Review the diff. The decisions to check: `resetAGC()` over `triggerNoiseFloorCalibrate()`, the
   apply-on-the-loop split (and therefore `radio status` as the place the status code is read), and
   the four forbidden registers being absent by construction.
2. Flash, run the how-to above, and write the A/B table (nf mean/min, packet count) here.
3. If VBUSOK works, the next ticket is the charge bolt in the status bar (research section 2, step 3);
   the chip-id read is the go/no-go.

## 2026-11-11 02:06 (Claude): EST-376 reviewed and built, NOT on the device
- Checked: the 0x22 read sends only the register pointer (no data write anywhere), the forbidden FUSB302 registers are excluded by a list + rule with a test, all radio work runs from the loop, setTCXO/setRxBoostedGainMode are plain RadioLib calls followed by MeshCore's own resetAGC.
- One gap, fixed in EST-377: a later TCXO-only change runs resetAGC, which can drop a boost set earlier at runtime (status then shows boost=0, honest but it would mix the A/B). Until then, after every tcxo change run `radio boost on|off` again and check `radio status`.
- Sandbox build exit 0 (RAM 157,384 B, flash 2,810,929 B, firmware.bin 2,811,296 B), tests exit 0. Committed locally, not pushed, not flashed.

## 2026-11-11 02:20 (DeepSeek): EST-377 battery log on the card + boost survives a radio reset, built and tested, NOT on the device

**Ticket:** diagnostics for tomorrow's device session. Nothing the user sees changes, no default moves
(the boost has no default, the log switch defaults ON as asked). No SD work from the USB frame
handler; the log is written by the loop only.

**1. Battery log (`/log/power-YYYYMMDD.csv`, local date)**
- `log power on|off` saves the switch in NVS `"omlite"`, key `plogen`, default ON. A missing key takes
  the default WITHOUT writing it (same rule as the map layers); `off` then `on` writes both ways.
  `log power tail` prints the last 5 lines of today's file (the header counts as a line).
- One line per minute, 12 CSV columns:
  `time,batt_mv,screen,brightness,gps,fix,sats,wifi,ble,pkts,noise,uptime_s`
  (local `YYYY-MM-DD HH:MM`, cell mV, screen 0/1, applied backlight 0..255, GNSS rail 0/1, fix 0/1,
  satellites used, Wi-Fi 0/1, BLE 0/1, packets heard in that minute, MeshCore noise floor, uptime s).
  The header line is written only when the file is new (a `stat` before the append decides it).
- The first line is written as soon as the switch is on (so a full card or a wrong switch shows in the
  first minute, not after one); then exactly every 60 s of monotonic time, including the `millis()`
  wrap. A minimum of two SD calls per line: `exists` + append; the directory `/log` is made lazily.
- **The loop is the only writer**: `plog::loop()` is called from `loop()`; the USB command only flips
  the NVS switch or reads the tail. The radio counters are read only on a due minute, because
  `meshRadioStats()` reads the SX1262 gain register over SPI.
- `log power tail` runs in `usbCommandExecute`, i.e. `otaCommandPoll` on the loop task after
  `otaPoll()` returned (EST-350), like `history`; the 1 KB tail buffer is static so the command stack
  does not grow.
- **One difference from the ticket's wording:** today the SD rail (expander bit 14) is powered once by
  `board::sdMount()` at boot and stays up; nothing unmounts it. The log therefore uses the same
  mounted card as the history store and the map, and adds no second mount or power cycle. A per-write
  power cycle would break those two users, so it is NOT done here (Claude decides if it is wanted).
- The GPS rail has no switch yet; `board::gpsRailOn()` returns the latched expander bit (1 today), so
  the column follows the day GPS duty cycling lands instead of being a constant.

**2. Boost choice across `resetAGC()` (`src/radio_diag.cpp`)**
- A new plain-C++ `diag::BoostChoice` (in `diag_logic.h`) remembers the last runtime `radio boost
  on|off` ("none set" until the first command). `requestBoost()` notes the choice.
- After EVERY `resetAGC()` this module runs: the normal apply (TCXO change and/or boost change) and
  the AGC retry. `reassertBoost()` reads the chip back and, when a choice exists and the register
  differs, calls `radioDiagSetBoost(choice)` again. The apply line's `boost=` is the read-back and
  `boost_status=` carries the re-apply's RadioLib status; the AGC retry prints its own apply line
  when it had to restore the boost, and updates `radio status`.
- With no boost command since boot, a reset never invents a value (the host test pins this).

**Build** (`docs/REBUILD.md` command, real exit code)
- `pio run -e om_lite_l2` **exit 0**, SUCCESS. RAM **158,440 B** of 327,680 (48.4 %; EST-376 was
  157,384, **+1,056 B**), flash **2,814,253 B** of 6,553,600 (42.9 %; EST-376 was 2,810,929,
  **+3,324 B**), `firmware.bin` **2,814,624 B** (EST-376 was 2,811,296, **+3,328 B**). Only the two
  pre-existing RadioLib `#warning`s (god mode, USB CDC), none from the new code.

**Tests** (`sh test/host/run.sh` **exit 0**)
- New `test_est377.cpp` (built with `power_log_logic.cpp`, `diag_logic.cpp`, `usb_cmd_logic.cpp`):
  - the exact CSV line for a fixed sample and local minute, 12 columns (11 commas) in header and
    line, the local `/log/power-20261111.csv` name, a too-small buffer refused (never truncated);
  - `formatAppend`: header + one line for a new file, line only for an existing file, too-small
    buffer refused;
  - the gate: first call writes, then 59.999 s is not due, 60.000 s is, wrap at `0xFFFFFF00` + 60 s;
    `reset()` re-arms;
  - the switch: missing key -> ON with zero writes, stored OFF loads, a set writes `plogen`, a failed
    write still flips RAM but returns false; wrong key never touched; packet delta incl. wrap;
  - commands: `log power on|off|tail` (also quoted), missing/extra/unknown word refused, `history` and
    `radio status` still parse;
  - the ticket's boost sequence: boost on -> chip on -> TCXO reset loses it -> `needsReapply` true ->
    set again -> chip on; off after a reset that left it on is put back; "none set" never re-applies.
- The rest of the suite is unchanged and green (38 C++ binaries + 5 Python checks).

**Could not check (no device, no card, no radio, no glass)**
- The real card write, the `exists`+append timing on the loop, and whether the VFS logs an error line
  on `/log` the first time. The host test proves the bytes and the rules, not FatFs.
- That `radio boost on` then `radio tcxo 3.0` really keeps the SX1262 gain on this board; the test is a
  model of the read-back rule, the device is the proof.
- The log's own power draw (one open+stat per minute) and the CSV's value over a night on battery.

**Next step for Claude**
1. Review the diff (the SD-rail wording above is the one deliberate difference).
2. Flash `firmware.bin` (2,814,624 B) and check the boot line `[plog] battery log on (key plogen,
   default on)`.
3. `tools/cmd.py log power tail` -> `[cmd] ok log power tail N` (0 on a fresh card), then wait one
   minute and `tools/cmd.py log power off` / `on` to see the switch answer and the first `[plog]
   write ...` line. The file is `/log/power-YYYYMMDD.csv`.
4. For the boost: `radio boost on`, wait for the apply line, then `radio tcxo 3.0`; the second apply
   line must show `boost=1`, and `radio status` must keep `boost=1`. Then the A/B run from EST-376.


## 2026-11-11 02:24 (Claude): EST-377 reviewed and built, NOT on the device
- Battery CSV (12 columns, no position, so the file is safe to share), one line a minute from the loop only; switch `log power on|off` (NVS `plogen`, default ON), `log power tail`. DS was right that the SD rail stays mounted from boot (the ticket wording was wrong), so no per-write power cycle.
- Boost choice is now re-asserted after every resetAGC that radio_diag runs.
- Watch on the first flash: static RAM is +1.6 KB over EST-375 (the 1 KB `s_tail` buffer is .bss); Bluetooth once ran out of internal RAM (EST-356), so switch BLE on and check its start line before trusting this build. Moving `s_tail` to PSRAM is the easy fix if needed.
- Sandbox build exit 0 (RAM 158,440 B, flash 2,814,253 B, firmware.bin 2,814,624 B), tests exit 0. Committed locally, not pushed, not flashed.

## 2026-11-11 02:47 (DeepSeek): EST-379 the image says which build it runs, built and tested, NOT on the device

**Ticket:** the handover test (EST-378) found that nothing on the device names its build. One id, in
three places: the boot log, the USB `status` answer and Settings > General. No date and no path in
the id, so the same tree always produces the same one.

**What changed**
- New `tools/build_id.py`, wired as `extra_scripts = pre:tools/build_id.py` in `platformio.ini`. It
  runs exactly `git describe --always --dirty --abbrev=7` with cwd = `$PROJECT_DIR`, so the id is
  the short commit hash and `-dirty` when the tree has changes. `git describe` only reads `.git`,
  so the sandbox's read-only `.git` is enough. If git is missing or returns non-zero/empty, it
  prints `build_id: WARNING ... OM_BUILD_ID = unknown` and exports `unknown`; the build continues.
  The define goes through PlatformIO's `env.StringifyMacro()`: a plain quoted string in
  `CPPDEFINES` lost its quotes and `1ee888c-dirty` compiled as arithmetic (the first build failed
  on `'dirty' was not declared`, fixed before the sizes below).
- New `src/om_build.h`: uses the `-D OM_BUILD_ID="..."` from the build, with an `"unknown"` fallback
  for a non-PlatformIO compile.
- `main.cpp` prints `[om] build <id>` once, immediately after the existing first boot line
  `build: <date> <time>`.
- `usb_commands.cpp` puts `build=<id>` in the one `status` answer line, after `name=`.
- Settings > General > ABOUT, directly under the existing `Firmware <date>` row, gets a dim Note row
  `Build <id>` (the page's small dim text).

**Build** (`docs/REBUILD.md` command, real exit code)
- `pio run -e om_lite_l2` **exit 0**, SUCCESS in 133.7 s. The pre-build line was
  `build_id: OM_BUILD_ID = 1ee888c-dirty` (HEAD `1ee888c` plus the uncommitted ticket edits; once
  Claude commits and rebuilds it becomes the new hash with no suffix). RAM **158,440 B** of 327,680
  (48.4 %, unchanged from EST-377); flash **2,814,357 B** of 6,553,600 (42.9 %; EST-377 was
  2,814,253, **+104 B**); `firmware.bin` **2,814,720 B** (EST-377 was 2,814,624, **+96 B**).
  `strings firmware.bin` contains `1ee888c-dirty`, so the id really is in the image. Only the two
  pre-existing RadioLib `#warning`s, none from the new code.

**Tests** (`sh test/host/run.sh` **exit 0**)
- New `test_est379.py`, with a fake git: the exact command and cwd; a clean answer `1ee888c`; a dirty
  answer `1ee888c-dirty`; a non-zero exit -> `unknown` + a warning line; a missing git binary ->
  `unknown` + a warning; an empty answer -> `unknown`. The module imports cleanly outside SCons
  (the `Import("env")` NameError is the guard), so the test needs no PlatformIO.
- The rest of the suite is unchanged and green (38 C++ binaries + 8 Python checks).

**Could not check (no device, no serial, no glass)**
- The three places on the real device: the `[om] build ...` boot line, `build=` in the `status`
  answer, and the dim `Build ...` row in ABOUT (and whether it reads well at 11 px).
- That a build with git unavailable really succeeds: the fallback and its warning are unit-tested,
  but the full `pio run` with git missing was not run (it would need a second PATH).

**Next step for Claude**
1. Review the diff, **commit, then rebuild**: the commit changes the id, so flash the image built
   after the commit, not the `1ee888c-dirty` one from this run.
2. Flash and check the boot log's second line: `[om] build <new hash>`.
3. `tools/cmd.py status` -> the answer line starts
   `[cmd] ok status mesh=.. name=".." build=<same id> path=..`.
4. Settings > General, scroll to ABOUT: `Build <same id>` in dim text under `Firmware <date>`.


## 2026-11-11 02:40 (Claude): EST-379 reviewed
- `tools/build_id.py` runs only `git describe --always --dirty --abbrev=7` (reads .git, no date, no path), falls back to "unknown" with a warning; `env.StringifyMacro` keeps the quotes. Boot line `[om] build <id>`, `build=` in `status`, dim `Build <id>` row in Settings > General > ABOUT.
- Note: a change of id changes every compile command line, so the first build after each commit is a full rebuild (~2 min). Fine.
- Correction (Claude): the DS entry above is stamped 02:47; the ticket went to in_review at 02:39 by the clock, so that stamp is an estimate.
- Committed, then rebuilt from the clean commit (see the next line for the id and size). Not pushed, not flashed.
- 02:43 (Claude): rebuilt from `092542b`: build id `092542b` (no -dirty), RAM 158,440 B, flash 2,814,341 B, firmware.bin 2814704 B, the id string is in the image. Reviewed copy: `~/.cache/pio-om-lite/reviewed/est379.bin` (this is the tip to flash; it contains EST-374..379). Later notes-only commits do not change the image.


## 2026-11-11 08:32 (DeepSeek): EST-380 the card's open-file limit covers every SD handle, and a failed append is retried, never dropped silently

**Ticket:** URGENT, Filip lost chat history. Claude proved on the device (2026-11-11 08:10, build
`092542b`) that the mount took the Arduino default `maxOpenFiles = 5`; the map keeps five files open
for good (main pack 1 + z16 idx+pack + z17 idx+pack), so after four taps on `+` every later
`fopen`/`exists`/history append failed, and every message after that lived in RAM only. The status
line said `hist_dropped=0` while it happened. Nothing on the card or the glass was available in this
sandbox, so this run changes the code, the boot lines and the tests; the device proof is the flash.

**A. The file budget (one place, checked by the compiler and the host test)**
- New `src/sd_budget.h`, named constants and the sum the ticket asks for:
  map main pack **1** + overlays **2 levels x 2 files = 4** + history pool (history_store.cpp
  `kFiles`) **4** + battery log **1** + transient **6** + margin **2** = **18**.
  The transients are the ones `rg "SD_MMC\.open|\.open\(|exists\("` finds: the `exists()` probe 1, the
  `size()` probe's read 1, `mapBegin`'s one-time `tiles.idx` read 1, `printTail` 1, a directory
  listing (dir + one entry) 2, and the screenshot 0 (the frame travels over USB; the device opens no
  card file for it). They run one after another on the loop task, so the real peak is lower than six;
  they are all counted on purpose, the two margin slots then cover a combination nobody thought of.
- `board.cpp` mounts with `sd_budget::kBeginMaxOpenFiles` (18) and prints
  `[sd] open-file budget 18 = map 1 + overlays 4 + history 4 + power log 1 + transient 6 + margin 2`
  at boot. The counting comment is next to the `begin()` call. `static_assert`s: the limit is >= the
  sum, the margin is >= 2, it fits the `uint8_t` the Arduino API takes; `map.cpp` asserts the overlay
  table has `kMapOverlayLevels` entries and `history_store.cpp` takes `kFiles` from the budget and
  asserts it is 4. A later change to any of the three now fails the build, not a card at z17.
- **What a slot costs, and where it comes from.** This build has `CONFIG_WL_SECTOR_SIZE_4096` and
  `CONFIG_FATFS_PER_FILE_CACHE=1`, so `FF_MAX_SS = 4096` and `FF_FS_TINY = 0`: every FATFS `FIL`
  carries its own 4096 B sector buffer. From the shipped `libfatfs.a`
  (`vfs_fat.c.obj`, `esp_vfs_fat_register`) the table is ONE `ff_memalloc(max_files * 4136 + 6248)`:
  4,136 B per slot, 6,248 B of context. 18 slots is **80,696 B** (the old 5 was 26,928 B, so +53,768 B).
  `ff_memalloc` in the same archive calls
  `heap_caps_malloc_prefer(size, 2, MALLOC_CAP_DEFAULT|MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT|MALLOC_CAP_INTERNAL)`
  (SPIRAM first) and the build sets `CONFIG_FATFS_ALLOC_PREFER_EXTRAM=1`, so the table is **PSRAM**,
  not the internal heap Bluetooth needs (EST-356). The LFN working buffer is on the stack
  (`FF_USE_LFN=2`), not the heap.
- **The measurement the ticket asks for.** Two new lines bracket the mount:
  `[mem] boot (before the SD mount): internal=N largest=M psram=P heap=H` and the same after it, so
  the next flash answers it exactly. It cannot be answered here: no device, no serial in the sandbox.
  The static RAM number did not carry it: **+8 B** (158,440 -> 158,448 B, the retry flags/counters in
  the store, not the table). If the flashed before/after internal number moves by tens of KB, the
  PSRAM allocation fell back to internal (PSRAM short, or `ff_memalloc`'s second preference taken);
  then 18 is too big for this board and the slot count can come down (the true peak is 11: 5 map +
  4 history + 1 power log + 1 probe). With the `[hb] psram=` number in the same log this is one look.

**B. A message is no longer lost silently (`src/history/history_log.h`, `src/history_store.*`, `src/main.cpp`)**
- A card error no longer pops the record: `flushOne()` returns `Written / Dropped / Failed`, and
  `Failed` (open, write, header, repair, rotate) leaves the record at the head of the RAM queue
  untouched. `history::loop()` retries it on the next pass, forever, without the send path ever
  touching the card. A cap (`full_`) and a late-scan id collision still drop on purpose and are
  counted in `hist_dropped`; those are not card errors.
- `status` has the new `hist_write_fail=` field: the number of RECORDS whose append failed at least
  once (a per-record flag in the queue), not retry attempts, so it does not climb while one record
  waits. A clean card reads 0.
- `[hist] write FAILED <reason>` (`open` / `write` / `header` through `write`, `repair`, `rotate`) is
  printed at most once a minute (`micros()`, wrap-safe); the first failure of an episode prints at
  once. The reason is in the text.
- One pop-up per failure episode, through the existing toast door and a one-shot notice the store
  clears when taken: `History not saved: card error`. It is shown by the loop right after
  `history::loop()`, so a repaint or the next pass cannot repeat it and new messages do not each get
  one. **One declared extra:** if the RAM queue overflows during that episode, the number of records
  really lost is in `hist_dropped` and the pop-up is shown once more, `History not saved: card error,
  N dropped` (a burst that overflows while the card is fine gets `History not saved: queue full, N
  dropped`). Without that second line the loss would again be silent, which is the ticket's subject.
- Recovery: the first successful write drains the whole backlog in that one pass (bounded by the 16
  queue slots) and logs exactly
  `[hist] write recovered, N record(s) flushed`. A short write marks the chat's tail torn
  (`needsRepair`, the bytes after the last known-good length) so the retry repairs it first; otherwise
  every later record would sit behind a torn one and read as lost.
- **Battery log:** `plog` counts its card errors (the `/log` directory, the open, a short write) and
  `log power tail` now ends with `write_fail=N` in both the ok and the error answer. A lost minute is
  no longer only a serial line.
- **One deliberate change Claude should review:** the EST-349 runtime switch to the SPIFFS fallback
  on a failed WRITE is gone. That path cleared the queue (its ids were assigned against the card's
  counters), i.e. it dropped exactly what this ticket forbids. A card error now retries; the SPIFFS
  fallback still happens when the card cannot be MOUNTED at boot (`begin()`). If Claude wants the
  runtime fallback back, it needs a re-encode of the queued ids, which is a separate ticket.

**C. Tests**
- New `test/host/test_est380.cpp` (39th C++ binary, `run.sh` exit 0):
  - the budget: every named constant, the overlay x2 rule, `kHistoryPoolFiles == 4`, the sum ==
    `kMaxOpenFiles`, margin >= 2, above the old 5, fits `uint8_t`;
  - retry: a refused `openAppend` keeps the record (`queued` 1, `hist_dropped` 0), `hist_write_fail`
    is 1 and stays 1 across retries, `[hist] write FAILED open` is printed once, a retry inside the
    minute adds no line, past 60 s it adds one; one `CardError` notice, taken once;
  - recovery: the pass drains the backlog, the file holds the message, one
    `[hist] write recovered, 1 record(s) flushed`, a new episode prints its first FAILED at once;
  - short write: the record is kept, `write FAILED write`, the retry repairs and writes, a fresh store
    reading the same volume sees the message (the tail really is intact);
  - queue full: 16 queued records are all kept, the 17th is dropped (`hist_dropped` 1) and the notice
    is `CardErrorDropped` with `dropped=1`; recovery drains all 16 and logs `16 record(s) flushed`;
    a healthy burst's overflow is `QueueFull`.
- The rest of the suite is unchanged and green: **39 C++ binaries + 8 Python checks**, `run.sh` exit 0.

**Build** (`docs/REBUILD.md` command, real exit code)
- `pio run -e om_lite_l2` **exit 0**, SUCCESS. `build_id: OM_BUILD_ID = 12c24f3-dirty` (HEAD plus these
  edits; Claude's commit changes the id and needs a full rebuild). RAM **158,448 B** of 327,680
  (48.4 %; `092542b` was 158,440, **+8 B**), flash **2,815,265 B** of 6,553,600 (43.0 %; `092542b`
  was 2,814,341, **+924 B**), `firmware.bin` **2,815,632 B** (`092542b` was 2,814,704, **+928 B**).
  Only the two pre-existing RadioLib `#warning`s. `strings firmware.elf` carries the new budget line,
  `hist_write_fail=`, both recovery lines and both `write_fail=` answers.

**Could not check (no device, no card, no serial, no glass)**
- The real boot heap numbers (the two new `[mem]` lines are the measurement), the z17 path on the
  card, the pop-up on the panel, and the real `chats card=` after z17. The host tests prove the rules
  and the arithmetic, not FatFs or the glass.
- That 18 slots fit the PSRAM at boot on this board. The archive says PSRAM-first; the `[mem]` lines
  say whether it really went there.

**How to check after a flash (this ticket adds the z17 steps)**
1. Boot log: `[sd] open-file budget 18 = map 1 + overlays 4 + history 4 + power log 1 + transient 6 +
   margin 2`, then `[mem] boot (before the SD mount): internal=A ... psram=P1` and
   `[mem] boot (after the SD mount): internal=B ... psram=P2`. Write A-B and P1-P2 here. A-B should
   be a few hundred bytes (static work), P1-P2 about 80 KB; a B far below A or an internal drop of
   tens of KB means the table went to internal.
2. `tools/cmd.py status` -> `history=sd ... hist_dropped=0 hist_write_fail=0`.
3. Open the Map, tap `+` four times to z17; the log says `[map] overlay z17 opened: N tiles`. After
   that there must be **no** `[E][vfs_api.cpp:301] fopen(...) failed` line and no `[plog] write failed`.
4. `tools/cmd.py chats` -> for every chat that has history, `card` is **> 0** (and `mismatches=0`).
5. Receive/send a message with the map at z17, then `status` -> `hist_records` grew,
   `hist_write_fail=0`, `hist_dropped=0`; OTA the same image and `tools/cmd.py history "<chat>" 5`
   shows the message (it really reached the card, not RAM).
6. `tools/cmd.py log power tail` -> `[cmd] ok log power tail N write_fail=0` (a lost minute would show
   a number > 0 here even when no serial log is being watched).

**Next step for Claude**
1. Review the diff, then commit and rebuild (the id changes). The two decisions to look at: no
   runtime fallback on a write error (the old one dropped the queue), and the one extra pop-up when
   the queue overflows.
2. Flash and run the list above; the boot `[mem]` pair is the number this ticket still owes.
3. Then the separate map pop-up and GPS time tickets.

## 2026-11-11 08:28 (Claude): EST-380 reviewed
- Mount now passes sd_budget::kBeginMaxOpenFiles (18) with static_asserts; failed appends stay queued and retry; hist_write_fail in status; one pop-up per episode; plog counts write_fail. Agreed: the runtime SPIFFS fallback had to go (it cleared the queue).
- Follow-up risk (not blocking): a record that fails forever at the queue head (one broken chat file) blocks every other chat behind it until the queue overflows. Per-chat skip-ahead after N failures of the same file is a later ticket.
- The 80 KB slot table is claimed to be PSRAM (FATFS prefers external RAM); the new [mem] boot lines before/after the mount are the proof on the first flash. If internal drops by tens of KB, lower the slot count (true peak 11).
- Correction (Claude): the DS entry above is stamped 08:32; the ticket went to in_review at 08:26 by the clock.

## 2026-11-11 08:55 (DeepSeek): EST-381 the Map's message pop-up, the `notify test` hook and what the log will say

Filip (~08:00): on the Map a new message plays its sound but no pop-up appears, bars shown or tucked;
every other screen (the Chat tab's own chat excepted) shows it, and a touch dismisses it. This ticket
adds the USB hook FIRST (`notify test "<title>" "<body>"`, the same `toasts::notifyNotice` door as a
real channel message, kind Channel, never the open chat), one log line per notice, the fix, and host
tests. Nothing was flashed, committed or pushed from the sandbox.

### A. What I could and could not prove in the source

Claude had already shown the door, the DND/open-chat rule and `redrawCurrentScreen` -> `map_tab::draw()`
-> `drawMapFrame()` -> `omFrameBegin/End` (whose overlay paints the toast) are shared with the screens
that DO show it. I went through the four suspects and this is the honest result:

1. **A frame that bypasses the overlay: no.** `drawMapFrame()` is the only map drawing function and
   its last act is one `omFrameEnd()`. The guard paths close their own frame (`omFrameBegin()` may
   fail) and draw nothing. `screen_map.cpp` never touches the panel: the only writer of the panel in
   the whole tree is `board.cpp`'s `pushFrame()`, and `omFrameEnd()` calls the overlay *before* it.
   The 30 fps trace frames and the EST-374 slide frames are the same `drawMapFrame()`.
2. **The redraw dropped and never repeated: REAL, and now fixed.** `redrawCurrentScreen()` answered
   nothing, and it returns early while a screen change owns the frame (EST-355). A notice that landed
   in that ~300 ms window was drawn by nobody: the toast state was visible for its 5 s with no frame
   carrying it. The shell's callback now answers whether it drew (`bool redrawCurrentScreen()`), the
   toasts module keeps `s_needFrame` and asks again on the next pass until a frame is really drawn,
   and it logs `[notice] redraw deferred: the screen change owns the frame, retrying` once when that
   happens. That is one way a Map pop-up (and any other screen's) could vanish with correct drawing
   code, so it is fixed for every screen, not just the Map.
3. **Drawn under the chrome on the map layout: no.** The overlay is drawn last on the frame, and the
   pop-up's box comes from `noticeLayout()` (screen width, tab bar top, status bar bottom) which knows
   nothing about the map's chrome: 304 x 84 px at (8,123) for a four-line message, inside the canvas,
   above the bar and above the tucked 3 px strip. Pinned by a host test.
4. **The consumed tap: already correct, now pinned.** The toast is consulted on the PRESS edge, before
   the map's `cont` (pan continuation), every action is behind `!g_spent`, and the release is behind
   `!spent && !g_drag && !g_acted`. A test proves it on the map's own hit areas, with the no-toast
   case as the control.

Two more real holes found while in there, both fixed:

- a **queued** notice was taken in `pump()` even when the panel was off (`takeNotice` before the
  `screenOn` test), i.e. silently thrown away in the dark; now it waits for the panel;
- `toasts::text()` was the ONE door with no screen check and no log; it now goes through the same
  decision (shown / skip(screen-off)) but keeps its old "replace the toast on screen" meaning, because
  a shell line ("Your position", "Sound saved") means "say this now". `notify()`/`notifyNotice()` keep
  the EST-366 rule 9 queue (newest pending wins).

**So the cause is at run time and the device will name it.** Three new serial lines do it, one per
notice, all from the one place that decides (ui/notify.h, host-tested):

- `[notice] show kind=channel` / `[notice] queue kind=channel` / `[notice] skip(<reason>) kind=..`
  with the reason `empty`, `screen-off`, `dnd` or `chat-open`, this says whether the notice even
  reached the toast state;
- `[notice] painted after N ms (M frame(s))`, the overlay counts paintings into a new `paints`
  counter; this says the toast really reached a composed, pushed frame;
- `[notice] redraw deferred ...`, the screen change refused the frame and it is being retried.

If the log says `show` + `painted` and the glass is still empty, the fault is between the composed
frame and the panel (not the notification path); if it says `skip(...)`, the reason is in the line; if
neither line ever appears, the notice never reached the door. Whichever it is, the next run answers it
in one line instead of another reading pass.

### B. The hook and the log

- `notify test "<title>" "<body>"` (`usb_cmd_logic.*`, `usb_commands.cpp`): both words required, the
  body may be quoted or be the rest of the line, a missing one is `[cmd] error args` and an unclosed
  quote `[cmd] error quote`. It builds a `ui::Notice` and calls
  `toasts::notifyNotice(NoticeKind::Channel, n, /*chatOpen=*/false, dnd)`, the exact call the channel
  arrival makes off the Chat tab. The reply repeats the outcome so a host tool can assert it:
  `[cmd] ok notify test kind=channel title="..." bytes=N result=show|queue|skip(<reason>) dnd=0|1`.
  It is not a diagnostic that bypasses the rules: DND and a dark panel refuse it with the reason,
  because that is what a real message would get.
- The decision moved into `ui/notify.h` (`noticeDecide`, `noticeLogLine`, `noticeKindToken`,
  `noticeDeliveryToken`, `NoticeDelivery`), and `noticeWanted()` is now the same two rules, so the
  old callers and the new door cannot drift. `NoticeQueue` keeps the queued notice's kind, so the
  line printed when a queued notice finally takes the screen names its real kind.
- Code size: the decision is a header (no new TU), the log line is one `char[96]` on the stack.

### C. Tests

- New `test/host/test_est381.cpp` (40th C++ binary, `run.sh` exit 0):
  - **the decision**: `empty` / `screen-off` / `dnd` (with LowBattery and Error exempt, and every other
    kind refused) / `chat-open` (only Channel, Dm and Mention) / a toast already up -> `queue`; every
    combination also agrees with `noticeWanted()`, so the hook and a real message are judged alike;
  - **the line**: the four exact forms, a null kind and null reason, and a 8-byte buffer that must
    stay NUL-terminated inside itself;
  - **the queue**: newest wins, the message keeps its body AND kind, taken once, the one-line form is
    a channel notice, an empty title is refused, `clear()` drops it whole;
  - **the toast**: `kToastMs == 5000`, visible at `until - 1` and gone at `until`, the press-edge
    dismissal (and an expired toast does not eat the touch), and the paint counter the device line
    reads;
  - **the box on the Map**: inside the canvas, below the status bar, above the bar AND above the
    tucked strip in both states; a 400-character message stays 4 lines with `...` and never grows off
    the screen;
  - **the consumed tap**: for a marker, the chip, a zoom tab and a plain map tap with a callout open,
    in BOTH bar states: with a visible toast the press dismisses it and *nothing* on the map acts (no
    select, no chip toggle, no zoom, no pan even when the gesture drags, no callout close), and the
    same tap without a toast DOES act (the control); a toast whose 5 s ran out does not eat the tap.
  - **the command**: the ticket's quoted form, a one-word title with the body as the rest, and the
    refusals (no body, no title, no `test`, a wrong sub-command, a trailing word, an unclosed quote).
- New `test/host/test_est381.py` (a source scan, like the EST-374 one, `run.sh` exit 0): every map
  draw entry calls `drawMapFrame()`, the LAST statement of `drawMapFrame()` is its `omFrameEnd()`,
  `screen_map.cpp` never pushes to the panel, `board.cpp` draws the overlay before the push and before
  the hold, the press edge spends the gesture and every map act is behind `!g_spent`, the release tap
  is inside `if (!spent)` with `!g_drag && !g_acted`, the two `notifyNotice()` calls in `main.cpp`
  have no `g_tab == 2` special case, `notify test` goes through `notifyNotice(NoticeKind::Channel, ..,
  false, ..)`, and `toasts.cpp` prints only the shared formatter.
- The rest of the suite is unchanged and green: **40 C++ binaries + 9 Python checks**, `run.sh`
  exit 0.

### D. Build (`docs/REBUILD.md` command, real exit code)

- `pio run -e om_lite_l2` **exit 0**, SUCCESS, and none of the changed files produced a warning
  (the build recompiled the seven touched sources; RadioLib was not rebuilt, so its two known
  `#warning`s are not in this log), `build_id: OM_BUILD_ID = a9cc5aa-dirty`.
- RAM **158,480 B** of 327,680 (48.4 %; `a9cc5aa` was 158,448, **+32 B**), flash **2,817,253 B** of
  6,553,600 (43.0 %; was 2,815,265, **+1,988 B**), `firmware.bin` **2,817,616 B** (was 2,815,632,
  **+1,984 B**).
- `strings firmware.elf` carries `[notice] %s kind=%s`, `[notice] skip(%s) kind=%s`,
  `[notice] painted after %lu ms (%lu frame(s))`, both deferred lines and
  `[cmd] ok notify test kind=channel ...`, so the flashed image can be checked by name.

### E. Device test steps (this is the list to run after the flash)

Coordinates are the L2's 320x240: tab bar y 215..240 (four 80 px tabs), chip (298..320, 26..48),
zoom + (295..320, 152..183), the pop-up's own box about (8..312, 123..207).

1. Boot: the log says `[om] build a9cc5aa-dirty` (or whatever id Claude's commit produces) and the
   tick lines are as before. `tools/cmd.py status` -> `dnd=0` in the heartbeat. With DND on every
   step below is expected to answer `skip(dnd)`; turn it off first.
2. `tools/cmd.py tap 200 227` -> the Map opens (`[tab] Map`, `[map] open: cached canvas ...`).
3. **The pop-up with the bars SHOWN:**
   `tools/cmd.py notify test "TehiLL RAK - #slovenija" "hello from the map test"`.
   Expect the reply `[cmd] ok notify test kind=channel title="..." bytes=<the body's length>
   result=show dnd=0`; then `[notice] show kind=channel`; then
   `[notice] painted after N ms (1 frame(s))` (N is tens of ms); and on the glass the two-part
   pop-up (mono title, the text under it) for ~5 s, then gone with the next frame. No `[map]` line
   is needed for it.
4. **With the bars TUCKED:** `tools/cmd.py tap 305 37` -> `[map] bars hidden`, `[map] bars tuck: 4
   slide frame(s) + 1 in ... ms`; repeat step 3. The pop-up must appear above the 3 px strip (its box
   is y 123..207, the strip is y 237..240).
5. **The consumed tap:** while the pop-up is up, `tools/cmd.py tap 160 160`. Expect exactly
   `[tap] 160,160 dismissed the toast, gesture spent` and NO `[map] select ...`, NO `[map] bars ...`,
   NO `[map] zoom`, no marker highlight and no pan. Tap again in the same place after it is gone: now
   the map acts (that is the control).
   In particular a tap on a marker under the pop-up must not select it, and a tap on the chip's area
   must not tuck the bars.
6. **It survives a trace:** with Traces ON, have another node send a channel message (real traffic).
   Expect, in one pass: `[map] trace <name> N hop(s), ...`, `[notice] show kind=channel`,
   `[notice] painted after ...`, the animation `[map] traces N frames in ... ms, avg ... us, worst ...`
   and the pop-up still on the glass for its 5 s while the dot moves underneath it.
7. **Two in a burst:** send two messages within a second (or two `notify test` commands). The second
   is `[notice] queue kind=channel`, and 5 s later `[notice] show kind=channel` for it, once.
8. **The screen-change case** (the fixed hole): the Map is opened and `notify test` is sent within the
   ~300 ms transition (`tools/cmd.py tap 200 227` and the command in the same second). Expect
   `[notice] redraw deferred: the screen change owns the frame, retrying` and then
   `[notice] painted after ...`; the pop-up must be on the glass.
9. Write the numbers from step 3 (N and the frame count) and from step 6 here, and say what the glass
   did. If it does NOT appear, the three lines decide the next step:
   `show` + `painted` and no pixels -> the fault is between the frame and the panel (say so and stop);
   `skip(<reason>)` -> the door refused it for that reason; no `[notice]` line at all -> the notice
   never reached the door.

### F. Could not check (no device, no glass, no serial)

- Whether the pop-up actually appears on the Map (that is the whole ticket, and it needs the flash).
- Which of the mechanisms above the device hit: the log lines in E name it. This entry does not claim
  a cause it could not see.
- The panel's real behaviour during the 30 fps trace animation with a toast in the frame (the frame
  content is right; whether the panel shows every pushed frame is a device fact).
- The `[notice] painted` count on a slow map compose (the line prints the real number).

## 2026-11-11 08:58 (Claude): EST-381 built, NOT reviewed in detail, NOT on the device
- Sandbox build + tests green. Committed locally so the next ticket starts from a clean tree; detailed review and the device test (`notify test` on the Map, bars shown and tucked) are still owed. Not pushed.

## 2026-11-11 (DeepSeek): EST-382 chat list dots, Clear messages, manual adverts
Built in the sandbox, not flashed. This entry is the build/test record; the glass and the air are still
Claude's to check.

### A. Chat list dots now mean UNREAD (part one)
- `ui/chat_layout.h` has ONE row layout, `selRowLayoutFor(list, channelCount, dmCount, entry, scroll,
  unread, selected)`: the row rect, the left dot rect, the right count rect, the count text and the name's
  left edge. `chat_selector.cpp` draws from it and `selRowAt()` (the selector's hit test) now takes the
  row from the same function, so drawing and taps cannot drift.
- The dot is `entry.unread > 0`, the number is `badgeText(entry.unread)` (1..99 then "99+", the EST-364
  rule) in the same MonoMeta font and at the same right edge as before. One field, so they cannot
  disagree. Selected = the highlight only; selected + unread shows all three; no unread shows neither.
  Muted chats keep the same number rule (no second mute rule was added).
- This is the shared selector, so the Settings > Chat selector gets the same rule as the Chat tab.
- New `test/host/test_est382.cpp` covers selected+0, not selected+3, selected+5, 150 -> "99+", a DM row
  against a channel row, and a tap on the dot/count resolving to that row.

### B. Clear messages, one channel (part two)
- Settings > Chat > Manage channels > a channel's page: "Clear unread" is now **Clear messages**. It
  opens a confirmation POP-UP (`settingsModalLayoutFor`): "Clear all messages in <channel>? This cannot
  be undone.", Cancel on the left drawn as the default key, Clear on the right in the danger colours; a
  tap outside is Cancel and is consumed.
- `history::HistoryStore::clearChat(slot)` deletes that chat's `.log` and `.old`, resets its ids / seen /
  unread / preview, and drops exactly that slot's records from the EST-380 write queue. It never touches
  another slot: the host test writes two chats, clears one, and proves the other's file, queued records,
  unread and preview survive, then flush.
- `meshChatClear(channel)` also marks that channel's entries in the shared 32-message arrival ring dead
  (ch = 0xFF; every reader already filters by channel, and `msgs` was taught to skip them). The channel,
  key, name, sound and switches stay.
- Log: `[hist] cleared <ch-id> records=N file=deleted|missing`. N is the number of THIS chat's queued
  history records dropped (message + Meta are separate records). A failed delete logs
  `[hist] clear FAILED <id>: the card copy stayed`, resets nothing and shows "Could not clear the card
  copy" in the toast, so RAM keeps what the card keeps. No message text is logged.
- A DM "clear" is not built (Manage channels lists channels). Where it would fit: the same button on a
  future DM settings page (ChatRoot has no DM page yet), calling a `meshDmChatClear(thread)` that mirrors
  this one for the DM ring and the `dm-...log` slot; the store method is already per slot.

### C. Adverts are manual only (part three)
- Removed the boot flood advert. The readiness block still runs; the checkpoint is now `[mc] 6 ready`.
  Nothing goes on the air by itself after a boot or a flash.
- ONE function, `meshSendAdvert(bool flood)`, used by the Chat page key and by USB `advert local|flood`:
  createSelfAdvert with lat/lon only when `advert_loc_policy` is not NONE and a position is known (the
  exact boot rule), zero-hop via `Mesh::sendZeroHop`, flood via `sendFlood(pkt, 0, meshPathHashBytes())`.
  It keeps a 30 s guard PER KIND and returns 0 / seconds left / -1. Log on send only:
  `[mc] advert <local|flood> pos=<0|1>`, never a coordinate.
- Chat header: the selector title is shortened and a 62 x 26 ADVERT key sits in the freed right. The
  Chat header is now 26 px (`kChatHeadH`) because the ticket requires a >= 30 x 26 tap target and the DRAWN
  rect IS the target; Nodes/Info keep the 23 px `kHeadH`. The title room is 207 px minus the chevron
  (about 195 px), so a 31-character channel name ellipsises cleanly.
- The key opens a 76 px wide dropdown under it with **Flood** and **Local**, two 36 px rows, right-aligned
  with the key. A tap on an option sends and closes; a tap on the panel elsewhere only closes; a tap
  anywhere else closes it on the PRESS and the gesture is CONSUMED (the keyboard's rule). The selector and
  this menu are exclusive: opening either closes the other. UI pop-up after a send: "Advert sent (flood)"
  / "Advert sent (nearby)"; the guard says "Advert already sent, wait Ns".
- USB: `advert local|flood` (exactly one word, anything else is an argument error) answers
  `[cmd] ok advert <kind> sent=1`, `sent=0 wait=N`, or `[cmd] error advert radio not ready`.
- Preview: `docs/shots/2026-11-11-chat-list-dots-preview.png` (four row cases, Channel A / DM B, and the
  advert dropdown; 1:1 layout drawn 2x, not a device shot).

### D. Build and tests (real exit codes)
- `pio run -e om_lite_l2` **exit 0**, SUCCESS, `build_id: OM_BUILD_ID = 96173e3-dirty`; only RadioLib's
  two known `#warning`s in the log, no warning from a changed file.
- RAM **158,648 B** of 327,680 (48.4 %; 158,480 before, **+168 B**); flash **2,821,573 B** of 6,553,600
  (43.1 %; 2,817,253 before, **+4,320 B**); `firmware.bin` **2,821,936 B** (was 2,817,616, **+4,320 B**).
- `test/host/run.sh` **exit 0**: **41 C++ binaries + 9 Python checks**. The new binary is the 41st
  (`test_est382.cpp`); `test_chat.cpp`'s three header expectations moved to `kChatHeadH` / the key's left
  edge because the Chat header intentionally grew 3 px.
- `strings firmware.elf` carries `[mc] advert %s pos=%d`, `[hist] cleared %s records=%d file=%s`,
  `[mc] 6 ready`, `ADVERT`, `Advert sent (flood)` / `(nearby)` and both `[cmd] ok advert ...` forms.

### E. Device test steps (after the flash)
1. Boot: no `[mc] advert` line. `advert local` once -> `[mc] advert local pos=<0|1>` and
   `[cmd] ok advert local sent=1`; immediately again -> `sent=0 wait=<30..1>` and no second `[mc]` line.
   `advert flood` is independent of the local guard.
2. Chat page header: the name is shorter, the ADVERT key is on the right. Tap it -> Flood / Local menu
   under it; tap Flood -> the toast, the menu closes; tap the key again and tap away in the message band
   -> the menu closes and nothing behind it is tapped.
3. Open the channel/DM selector: the open chat has the highlight only; give another chat unread and check
   the dot on its left plus the number on the right; a selected chat that receives a message must show
   highlight + dot + number.
4. Settings > Chat > Manage channels > a channel: Clear messages -> the pop-up (Cancel default, outside =
   Cancel). Cancel keeps everything. Clear removes that chat's messages and unread; another channel's
   `history <name>` count and `chats` card count must be unchanged.
5. Watch the serial: `[hist] cleared <id> records=N file=deleted|missing`, and on a forced card failure
   (or a write-protected card) the "Could not clear the card copy" toast with the chat's messages still
   readable.

### F. Could not check (no device, no glass, no serial, no card)
- Whether the advert really leaves the radio: the two copies above have not been sent on the air here.
- The look of the 26 px Chat header, the ADVERT key, and the 36 px option rows on
  the glass; the preview is a layout drawing, not a device screenshot.
- The Clear pop-up's wrapping on the glass with the longest real channel name (the host only proves the
  geometry and the 3-line wrapper), and the actual SD delete on the card.
- The two dropdowns' exclusive state under real taps (the model proves one overlay at a time; the shell's
  open/close order is source-checked, not device-run).

## 2026-11-11 09:18 (Claude): EST-382 built, NOT reviewed in detail, NOT on the device
- Sandbox build + tests green (RAM 158,648 B, flash 2,821,573 B). The boot-time advert comment and send are gone from the boot block (checked by grep; the device boot log is the real proof). Committed locally; detailed review and device tests owed. Not pushed.

## 2026-11-11 09:45 (DeepSeek): EST-383 GPS time without a position fix, diagnostic first, `clock=gps-nofix`

### A. Does this L76K send ZDA? Yes, per the device's own log, so no PCAS03 was added
- The ticket's step 2 is conditional on the module NOT sending ZDA. This unit already does: the 2026-11-10
  device reads recorded in this file (EST-331 08:20, EST-333 08:53) and in `pocket-mesh.md` (09:28) show
  ZDA arriving and the clock being set from it while RMC was void ("the time came from ZDA (RMC was
  void)"). So no command was added to the GNSS init, nothing new was written to the module, and the baud
  rate, PCAS04,7 and every other init byte are unchanged. The new `gps raw` window settles it with numbers.
- If the diagnostic prints `zda=0`, the known command is `$PCAS03,1,1,1,1,1,1,1,1` (fields in CASIC /
  Quectel L76K PCAS order: GGA, GLL, GSA, GSV, RMC, VTG, ZDA, ANT). The repo already proves the field
  order at position 4: EST-338 used `$PCAS03,1,1,1,0,1,1,1,1` to turn GSV OFF and it went off, so the
  4th field is GSV and ZDA is the 7th. It would be sent once next to the existing `$PCAS04,7`, from the
  same one-shot block after the first checksum-valid sentence, only if the raw window shows no ZDA.

### B. `gps raw <5..120>`: what it prints (new `src/gps_raw.h/.cpp`)
- The command starts a time-boxed window in `gpsPoll()` (the USB command never blocks the loop; the
  EST-350 rule). Every complete RMC/ZDA/GGA/GSA line prints ONE data line with only these fields:
  `[gpsraw] t=<elapsed>s type=GNRMC time=123456.00 status=V date=101126`,
  `type=GNZDA time=... date=10.11.2026`, `type=GNGGA time=... quality=0 sats=00`,
  `type=GNGSA time=- fixmode=1` (GSA has no time field). At the end, ONE answer line names every
  talker+type that arrived: `[cmd] ok gps raw secs=30 types="GNGGA:30,GNGSA:30,GNRMC:30,
  GPGSV:15,GNVTG:30,GNZDA:30" zda=1 bad=0 dropped=0`.
- Rule 9 is enforced by construction, not by trust: the formatter never reads RMC fields 3..6 or GGA
  fields 2..5, and every copied field must match its NMEA shape (the time field is exactly hhmmss[.sss];
  dates/quality/satellites/fixmode are digits only). A refused field prints `-`. A position-shaped value
  in the time column prints `time=-`; a garbled field can never leak a coordinate into another column.
  Bad-checksum lines are still tallied and printed with `badsum=1` (that is a fact worth seeing), but
  they never move the fix or the clock.
- `type=` is the talker+type token (GNRMC, GNGGA, ...). The summary is printed once even when the module
  sends nothing (`types="none"`). At most 12 distinct types are remembered; `dropped=` says if more came.
- Protocol shape: the immediate reply is `[gpsraw] start secs=N` (a data line, not an answer), then the
  data lines, then the one `[cmd] ok gps raw ...` answer when the window closes. `tools/cmd.py` gained
  `answer_timeout(argv)`: for `gps raw N` it waits N+5 s instead of the default 10 s, so the diagnostic
  can be read with the project's own tool (`tools/cmd.py gps raw 30`).

### C. Time without a fix (new `GpsNoFix` source + `NoFixTimeGate`)
- `parseNmeaTime(sentence, allowNoFix)` now also parses RMC status V, and marks `hasFix` (A = true).
  A void RMC's date must pass the SAME build-year gate as ZDA (`buildYear() .. buildYear()+1`), so the
  2080 default is refused on both paths; 1980 already fails the civil-date range. The fix path (RMC A)
  is unchanged.
- `NoFixTimeGate::offer(epoch, satsTracked)` accepts a no-fix time only when ALL hold: the first
  sentence is never accepted (nothing to compare); the step from the previous offered sentence is 1..2 s
  (0 = frozen clock, >2 = jump/restart, both refused); at least one satellite is being tracked
  (`GpsState.heardTotal > 0`, i.e. a GSV C/N0 above zero). Every offered sentence moves the chain on,
  so one sentence with nothing tracked only withholds that sentence. `gpsBegin()` resets the chain.
- Both RMC V and ZDA now go through that gate. This is deliberate: the ticket ranks a no-fix GPS time
  BELOW Wi-Fi/phone, and ZDA is exactly such a time. A ZDA from a module's free-running clock no longer
  counts as top-priority "GPS".
- New `TimeSource::GpsNoFix`, priority 1 (Gps 5 > Wifi 4 > Phone 3 > Manual 2 > GpsNoFix 1 > None 0).
  `timeSourceName()` returns `gps-nofix`, so `status` prints `clock=gps-nofix`; `TimeKeeper::approximate()`
  is true while that is the source. A later fix/Wi-Fi/phone/manual time replaces it immediately (the
  6-hour hold only stops LOWER sources); the no-fix time can never overwrite any of them. `main.cpp`
  applies `g_gps.timeEpoch` as `Gps` when `timeEpochFix` is true and as `GpsNoFix` otherwise, and it
  compares the flag too, so a fix arriving on the same second still upgrades the source.
- If the device shows a default or frozen V/ZDA clock, the gate rejects it (step 0 or the year gate) and
  the clock keeps the last better source or stays `--:--`: today's behaviour in effect, no special case.

### D. Build and tests (real exit codes)
- `pio run -e om_lite_l2` (the REBUILD.md command): **exit 0**, SUCCESS, `OM_BUILD_ID = e73fb14-dirty`;
  only RadioLib's two known `#warning`s, no warning from a changed file.
- RAM **158,824 B** of 327,680 (48.5 %; 158,648 before, **+176 B**); flash **2,825,533 B** of 6,553,600
  (43.1 %; 2,821,573 before, **+3,960 B**); `firmware.bin` **2,825,904 B** (was 2,821,936, **+3,968 B**).
- `test/host/run.sh` **exit 0**: now **42 C++ binaries + the 9 Python checks** (test_cmd's unittest count
  went 11 -> 12). New `test/host/test_gps_raw.cpp` proves: RMC/ZDA/GGA/GSA formatting with distinctive
  lat/lon digits that must never appear (including a position-shaped time field printing `time=-`); GSV
  and VTG are not formatted; the 5..120 s window, one-window rule, type tally/counts, `zda=0|1`,
  bad-checksum count and the 12-type cap; the gate (first sentence refused, +1 and +2 accepted, 0 and +5
  refused, nothing-tracked refused, reset); the 2080 default refused for RMC V and ZDA while RMC A is
  unaffected; and the full precedence matrix (each better source replaces a no-fix clock at once; a
  no-fix time never replaces fix/Wi-Fi/phone/manual). `test_usb_cmd.cpp` adds the `gps raw` parser cases
  (5/30/120 accepted; 4/121 `value`; `x` `number`; missing/extra words refused). `test_cmd.py` adds the
  `[gpsraw] start` is-not-an-answer case and the per-command timeout.
- `strings firmware.elf` carries `[gpsraw] start secs=%d`, `[gpsraw] t=%lus %s%s`,
  `[cmd] ok gps raw secs=%d types="%s" zda=%d bad=%lu dropped=%d`, `gps-nofix` and the busy error.

### E. Exact device test (indoors, no fix expected)
1. `cd /home/slofi/Projects/pocket-mesh/firmware`
2. `tools/cmd.py gps raw 30` (it now waits up to 35 s). Expected first line
   `[gpsraw] start secs=30`, then one `[gpsraw] t=Ns ...` line per RMC/ZDA/GGA/GSA, then one answer:
   `[cmd] ok gps raw secs=30 types="..." zda=1 bad=0 dropped=0`.
3. **Comment on the ticket**: the exact `types="..."` list and `zda=` value (does ZDA arrive by
   default?), the RMC `status=` (A or V), whether the `time=` values advance by exactly 1 s between
   consecutive lines, and `bad=`.
4. `tools/cmd.py status` right after: **comment** the `clock=` token. It should be `clock=gps-nofix`
   with the top bar showing HH:MM if the module's V/ZDA clock advanced and at least one satellite is
   heard; `clock=GPS` with a fix; anything else means the gate refused the no-fix time (say which
   field failed: frozen step 0, year gate, or no satellite heard).
5. Walk outside until a fix: the next `status` must show `clock=GPS` and the log one
   `[time] GPS -> ...` line; the approximate time must be replaced immediately.
6. With Wi-Fi connected (Settings > Wi-Fi, or `tools/cmd.py wifi on` once saved), `status` shows
   `clock=Wi-Fi`; a later `gps raw 30`/V sentence must not produce a `[time] gps-nofix ->` line while
   Wi-Fi is the source.

### F. Could not check (no device, no sky, no serial)
- What THIS L76K really sends while it hears 1-2 satellites and has no fix: the whole point of `gps raw`.
  The V/ZDA clock may be frozen or default; the gate is built to reject exactly that, but only the device
  can say which it is.
- Whether one tracked satellite is enough for the module to publish UTC, and how long after the first
  satellite the V time starts advancing (the first sentence is never accepted by design).
- The top bar with `gps-nofix` (it uses the same `realSinceBoot()` path as a fix, so it should show
  HH:MM), and the Settings > General Source row reading `gps-nofix, just now`.
- The Wi-Fi/phone handover on the device; the host test proves the policy, not the real NTP call.

### G. Next step
- Claude flashes and runs E1-E4, then comments the measured `types`/`zda`/`status`/`clock` numbers here.
  If `zda=0`, a small follow-up adds the `$PCAS03,1,1,1,1,1,1,1,1` enable after the existing
  `$PCAS04,7` (and only then, per the ticket's condition). If RMC V/ZDA time is frozen or default, the
  gate already keeps today's rule and no further change is needed.

## 2026-11-11 09:45 (Claude): EST-383 reviewed
- ZDA already arrives on this unit (DS cites the 2026-11-10 logs), so no PCAS03 was sent; RMC V and ZDA now pass one gate (plausible year, 1..2 s steps, a satellite tracked) as GpsNoFix, priority below Wi-Fi/phone/manual; a fix replaces it at once. gps raw strips lat/lon by construction.
- Note: the year gate is build year .. +1, so a 2026 build refuses no-fix time from 2028 on until rebuilt (fix time unaffected). Acceptable.

## 2026-11-11 11:25 (DeepSeek): EST-384 own position, Fixed on the map beats GPS (GPS then only for the clock)

### A. One position source, two ways to set it
- New `src/position_source.h` (plain C++, host-tested) owns the node's own point and the ONE rule:
  Fixed beats GPS; with GPS selected the latest fix is used; with neither there is no point. While
  Fixed is on the GPS still runs and still feeds the clock (EST-383), but a fresh fix is kept only
  for the clock path and never becomes the node's position.
- New `src/position.cpp` is the only NVS and MeshCore glue. NVS (`omlite`): `psrc` (0 GPS, 1 Fixed),
  `plat` and `plon`, written together on every change; a Fixed record with a missing or out-of-range
  point falls back to GPS at boot. `sensors.node_lat/lon` is now written by exactly ONE function,
  `posApplyToNode()`: the Fixed point when Fixed is selected, the GPS fix when GPS is selected, else
  0/0 (MeshCore's "no position").
- Every writer found and routed: `OmSensors::setGpsFix()` (the GPS poll's `targetSetGpsFix`) goes to
  `posSetGpsFix()`; `MyMesh::begin()` loads its old NodePrefs lat/lon into `sensors` and the shell
  re-applies the source right after `meshLinkBegin()`; the phone command is covered in C below.
  Adverts, telemetry, the phone's self-info and the map all read that one applied point.

### B. Settings > General > Node > Position, and the SAME picker
- The Node page has a `Position` row whose value is the source token (`gps` / `fixed` / `none`), never
  a coordinate. It opens a `Position` page with two Check rows, `GPS` (default) and `Fixed`, and,
  while Fixed is on, a `Change fixed position` button.
- Choosing Fixed opens the SAME map picker the RPTR Position page uses. The picker's pixels were moved
  out of `ui/screen_rptr.cpp` into `ui/map_pick.{h,cpp}`; RPTR and Settings both call it, and both use
  the same `rptrMapPickLayoutFor()` geometry and hit test. Nothing was copied into a second picker.
  Its back key is `Cancel` and its action key is `Save`; RPTR keeps `< back` / `Apply`.
- Start point: current GPS fix when there is one, else the map pack's centre. `Change fixed position`
  (and tapping Fixed when it is already on) reopens it on the saved point. Pan and zoom are the
  picker's own; the crosshair's point is the candidate.
- Save stores the point and switches to Fixed. Cancel changes no source and no saved point, so a node
  that was on GPS stays on GPS. The two choices live in the existing settings history, so Back is one
  level as everywhere else.
- The General tab's GPS `Position` value row used to print the raw fix as text; it now prints the same
  source token, because that row is about the position the node uses.

### C. The phone's set-location command (which one it is)
- The companion command is `CMD_SET_ADVERT_LATLON` (id **14**) in
  `lib/meshcore_companion/MyMesh.cpp` (the `else if (cmd_frame[0] == CMD_SET_ADVERT_LATLON ...)`
  branch, around line 1266). Upstream decodes two int32 micro-degrees and writes
  `sensors.node_lat/lon`, then `savePrefs()`.
- We do not edit that vendored file. `bleTransportFrame()` in `src/ble_link.cpp` already sees every
  frame the phone sends BEFORE MyMesh parses it; it now recognises id 14, decodes the same two int32
  values and calls `posPhoneSet()`. That point becomes Fixed, is saved to NVS and is applied at once.
  MyMesh then writes the same values, so there is no fight; a later GPS fix cannot move the node.
- A per-pass `posPollNode()` is the safety net for anything that writes `sensors.node_lat/lon` outside
  those paths: a different value is taken as the phone's set-location and becomes Fixed. It never
  fires for our own GPS writes because `posApplyToNode()` records them.

### D. The map
- The Map tab, locate/follow, the traces' "us" endpoint and the RPTR picker's fallback start point now
  read `posOwn()` (the effective point), not the raw GPS parser. The GPS parser still drives the clock,
  the satellite counters and the diagnostics.
- Our own marker uses the same rect and the same projection as before; when the point is Fixed it is
  drawn as a small gold pin (head plus a 5 px tip, white centre, 1 px screen outline) instead of the
  white GPS dot with the gold halo. `MapMarker` gained the one `fixed` flag.

### E. Status and USB
- `status` gained `pos=gps|fixed|none` (the source token; never coordinates).
- USB: `pos fixed-here` takes the current GPS fix as the Fixed point (device test shortcut; answers
  `[cmd] error no gps fix` when there is none) and `pos gps` switches back. Both answer
  `[cmd] ok pos source=gps|fixed`. Parser: exactly one word, one of the two.
- Log lines are source-only: `[pos] source=gps|fixed`, `[pos] phone set-location -> fixed`. No format
  string in the new code carries a coordinate, and `test_est384.py` greps for that.

### F. Build and tests (real exit codes)
- `pio run -e om_lite_l2` (the REBUILD.md command): **exit 0**, SUCCESS, `OM_BUILD_ID = 769be92-dirty`;
  only RadioLib's two known `#warning`s, no warning from a changed file.
- RAM **158,952 B** of 327,680 (48.5 %; 158,824 before, **+128 B**); flash **2,829,753 B** of 6,553,600
  (43.2 %; 2,825,533 before, **+4,220 B**); `firmware.bin` **2,830,112 B** (was 2,825,904, **+4,208 B**).
- `test/host/run.sh` **exit 0**: **43 C++ binaries + 10 Python checks**. New `test_est384.cpp` proves:
  the default (GPS, no fix -> none); a GPS fix is used when GPS is selected; Fixed beats a fresh fix
  and the point does not move when a later fix arrives; switching back to GPS uses the latest fix at
  once; the phone set-location switches to Fixed, survives a later fix, a second set replaces the
  point and an invalid pair is refused; the NVS record round-trips (source + point, GPS clears the
  point) and a missing/out-of-range Fixed record falls back to GPS; the token is exactly gps/fixed/none
  with no digits or dot. `test_usb_cmd.cpp` adds the `pos gps` / `pos fixed-here` parser cases (one
  word only; `pos`, `pos somewhere`, `pos gps extra` and `position gps` refused).
  `test_est384.py` greps every log/answer format string in the new position code and in the `status`
  and `pos` handlers for a float specifier or a coordinate name.
- `strings firmware.elf` carries `[pos] source=%s`, `[pos] phone set-location -> fixed`,
  `[cmd] ok pos source=%s`, `[cmd] ok status ... clock=%s pos=%s ...`, `fixed-here`,
  `Change fixed position` and `Fixed position saved`.

### G. Preview
- `docs/shots/2026-11-11-est384-node-position.png` (2x): the Node page with the POSITION section,
  `Position  fixed` and `Share position ON`.
- `docs/shots/2026-11-11-est384-map-fixed-pin.png` (2x): the Map with the Fixed pin at the canvas
  centre on the default map centre. The pin is the same marker rect as the GPS dot.

### H. Device test steps (after the flash)
1. `tools/cmd.py status` -> `pos=gps` with a fix, `pos=none` indoors with no saved point. The GNSS
   count and `clock=` lines must be unchanged, so the GPS still runs.
2. Settings > General > Node: the Position row reads `gps`; open it, the GPS row has the filled dot.
   Tap Fixed -> the map picker opens at the current fix (or the map centre indoors). Tap the map away
   from the centre, Save -> the Position page shows Fixed, the Change button appears, the Node row
   reads `fixed`.
3. Walk until the GPS has a fix: `status` must stay `pos=fixed`; the Map marker is the pin and stays
   on the saved point; locate/follow recentres on the pin; adverts (with Share position on) carry the
   fixed point. The clock and `[gps] fix` lines keep running.
4. Open the picker again with Change fixed position: it starts on the saved point. Cancel -> the point
   and the source are unchanged. Saving a new point moves the pin.
5. Tap GPS -> `pos=gps` and the marker is the white dot again (or none indoors).
6. Reboot: the source and point survive (`status`, the Node row and the pin are unchanged).
7. With Bluetooth on, use the phone app's "set location" on a point: `status` becomes `pos=fixed` and
   the `[pos] phone set-location -> fixed` line appears; walking with a fresh fix must not move it.
8. `tools/cmd.py pos fixed-here` with a fix -> `[cmd] ok pos source=fixed`; without a fix ->
   `[cmd] error no gps fix`. `tools/cmd.py pos gps` -> `source=gps`.

### I. Could not check (no device, no sky, no phone)
- The real picker pan/zoom and the Cancel/Save taps on the glass; the preview is a layout drawing.
- The phone app's own set-location screen and the BLE frame ordering with the real app (the host
  proves the decode and the rule, not the phone).
- Whether the pin reads clearly on the z13 tiles at 320x240 (the preview is 2x).
- The boot-time NVS read on a real power cycle (the host proves the record round trip).

### J. Next step
- Claude reviews the diff and the two previews, flashes, and runs H1-H8. Comment the `pos=` values,
  the `[pos]` lines and what the pin looks like on the glass.

## 2026-11-11 10:22 (Claude): EST-384 built, NOT reviewed in detail, NOT on the device
- Sandbox build + tests green. Committed so the NTP ticket starts from a clean tree; review + device test owed.

## 2026-11-11 10:40 (DeepSeek): EST-385, Wi-Fi NTP measured, retried on a schedule, server rotation

**The measured bug.** Claude, device 2026-11-11 10:18 (build 5cf263f): Wi-Fi connected, one
`[wifi] NTP: pool.ntp.org (one query)`, 20 s later `NTP: no answer, clock unchanged`, `clock=None`.
The same LAN answers a PC in 3-38 ms. So the fault is on the L2, and the old code gave it exactly one
window per association.

### A. What this stack actually does (Arduino-ESP32 2.0.17 / ESP-IDF 4.4, PlatformIO core)
Paths under `/home/slofi/.cache/pio-om-lite/core/packages/framework-arduinoespressif32/tools/sdk/esp32s3/`:
- **The `esp_sntp_*` wrappers already marshal to the tcpip thread.** `nm`/`objdump -r` on
  `lib/liblwip.a` member `sntp.c.obj`: `esp_sntp_init`, `esp_sntp_stop` and
  `esp_sntp_setoperatingmode` are a `tcpip_callback(...)` (asynchronous), and
  `esp_sntp_setserver`/`setservername` a `tcpip_api_call(...)` (blocking until the tcpip thread runs
  it). `esp_sntp_getserver`/`getservername`/`enabled` call the lwIP getter directly.
- **`LOCK_TCPIP_CORE()` does not exist on this build.** `CONFIG_LWIP_TCPIP_CORE_LOCKING` is not set
  (`sdkconfig:1449`), so `include/lwip/port/esp32/include/lwipopts.h:58-61` takes the `#else` branch:
  only `LWIP_TCPIP_CORE_LOCKING 0` is defined, `LOCK_TCPIP_CORE`/`UNLOCK_TCPIP_CORE` are not. Writing
  them would not compile, and wrapping `esp_sntp_setservername()` in the lock would **deadlock**
  against its own `tcpip_api_call`. So no missing lock is the cause, and no lock was added.
- **A wrong-thread call fails silently here.** `LWIP_ASSERT_CORE_LOCKED()` is the empty default
  (`include/lwip/lwip/src/include/lwip/opt.h:226-228`) because `CONFIG_LWIP_CHECK_THREAD_SAFETY` is not
  set (`sdkconfig:1450`). Silence proves nothing, which is why the client's own state is now logged
  (`enabled`, `addr`, `sync`, `reach`).
- **DNS names are enabled**: `SNTP_SERVER_DNS 1` (`lwipopts.h:1209`), so a name is resolved by lwIP in
  the tcpip thread. `SNTP_MAX_SERVERS 3`, `LWIP_DHCP_GET_NTP_SRV y`, update delay 3 h
  (`sdkconfig:1570-1573`); `DNS_MAX_SERVERS 3` (`sdkconfig:1579`), so `WiFi.dnsIP(0..2)` is in range.
- **The startup delay and the retry timeout are the real trap.** `LWIP_RAND` is `esp_random`
  (`lwipopts.h:86`), so `SNTP_STARTUP_DELAY` turns on and the first request is delayed by
  `LWIP_RAND() % 5000` ms (`include/lwip/lwip/src/include/lwip/apps/sntp_opts.h:144-157`).
  `SNTP_RECV_TIMEOUT` is the 15000 ms default and is also `SNTP_RETRY_TIMEOUT` (`sntp_opts.h:163-190`).
  One 20 s window therefore ends 0-5 s before or exactly when lwIP's own second send is due: the old
  "no answer" is what cutting that retry looks like. `SNTP_UPDATE_DELAY` is
  `sntp_get_sync_interval()` (`lwipopts.h:1214`), which we do not rely on: the schedule stops and
  restarts the client per attempt.
- **Per-send tracing**: `CONFIG_LWIP_SNTP_DEBUG` is not in the sdkconfig, so lwIP prints nothing per
  send/receive. The closest per-exchange fact it exposes is `sntp_getreachability(idx)`
  (`sntp.h:61-63`, `SNTP_MONITOR_SERVER_REACHABILITY` on, `sntp_opts.h:207-209`) plus the sync status
  and the capture hooks. All three are logged.
- The capture path is intact and verified in the built ELF: the weak `W sntp_sync_time` is replaced by
  ours (`T sntp_sync_time` at 0x421708a8) and `sntp_set_system_time` holds its address in the literal
  at 0x4207fd2c, so a reply still lands in `s_ntpGot` and goes through
  `timeKeeper().setFromSource(..., TimeSource::Wifi)` with the floor checks untouched. Note for the
  record: IDF's own `sntp_sync_time()` body is what calls the notification callback, so with our
  replacement that callback never fires on this stack; it stays registered as the second hook and its
  call count is logged (`notify=`), so a future core that does call it is visible, not silent.

### B. The fix
- **`src/ntp_plan.h` (new, plain C++, host-tested)**: the retry schedule and the rotation.
  Attempts at **0 s, 30 s, 2 min** counted from the connection (a slow attempt cannot push the next
  one out), then **every 10 min**, and every 10 min after a success (a refresh, not a stop). Per
  attempt the server rotates: **pool.ntp.org, time.google.com, 162.159.200.1 (Cloudflare),
  the DHCP default gateway**, then round again. An attempt's window is **25 s** (5 s maximum SNTP
  start delay + 15 s `SNTP_RECV_TIMEOUT` + margin), which still fits inside the 30 s slot.
- **Stop rule, one function, pinned against the real ranking**: `ntp::wanted(clockReal, priority)` is
  false only when a real clock is held by a source ranked above Wi-Fi. `timeSourcePriority()` is
  GPS 5, Wi-Fi 4, phone 3, manual 2, gps-nofix 1, so **only a GPS fix stops the retries**; a phone or
  hand-set time does not, and a Wi-Fi time keeps being refreshed. When it stops, one line:
  `[ntp] stop: clock is from GPS, better than Wi-Fi (N attempt(s))`.
- **`src/wifi_link.cpp`**: one attempt at a time, non-blocking.
  - the station's DNS servers are logged as **count + set/unset only** (`dns=3 main=set backup=set
    fallback=unset (addresses not printed)`);
  - `dns_gethostbyname()` is called directly with our own callback (the same call
    `WiFi.hostByName()` makes at `libraries/WiFi/src/WiFiGeneric.cpp:1574`), but it is **polled with a
    3 s timeout** instead of blocking up to 16 s + 15 s; the answer is logged with the **public IP**
    (`[ntp] dns pool.ntp.org ok public_ip=... in N ms`) or the error;
  - a name that resolved is handed to the client **as an IP** (no second DNS); a name that failed is
    handed over as a name so lwIP still tries it; the cached address is zeroed first
    (`esp_sntp_setserver(0, nullptr)`) because lwIP's `sntp_setservername()` stores only the pointer
    and keeps the old resolved address otherwise;
  - modem sleep is logged and **turned off only while an attempt is in flight** (`WiFi.setSleep(false)`
    at the start, back on when the attempt ends), with the driver's return value logged;
  - 100 ms after init the client's own state is logged (`enabled`, `addr`, `sync`, `notify`, `reach`);
    if `esp_sntp_enabled()` is false the attempt ends at once as `not-started` instead of sitting out
    the window (that is what a failed `tcpip_callback` looks like, and its return value is not visible
    through the API);
  - a failed attempt logs `result=timeout|dns-fail` with `addr=set|unset` and `reach=`, then
    `next attempt in N s (attempt K, server=...)`;
  - the success line is unchanged in shape: `[wifi] NTP <epoch> s: applied, source Wi-Fi`.
- **USB**: `ntp now` (one attempt through the same path; refused with `wifi off`,
  `wifi not connected`, `clock from gps`, `ntp busy`) and `ntp status`
  (`attempts= active= last= server= last_age= last_ms= next_in= next_server= clock= dns= dns_set=
  client= sleep=`). Parser: exactly those two words.
- No NTP log or answer print can carry a LAN address or an SSID: the only address formatter is
  `ntpIpText()`, fed a resolved **public** address; the gateway is the word `gateway`. `test_est385.py`
  greps every `[ntp]` literal and the whole `ntp` answer for an address format, `ssid`, `bssid`, a
  password word, `ip=`/`gw=`/`gateway=`, and proves `ntpIpText()` is only fed the public sources.

### C. Build and tests (real exit codes)
- `pio run -e om_lite_l2` (the REBUILD.md command): **exit 0**, SUCCESS, `OM_BUILD_ID = b96a392-dirty`
  (HEAD moved from 473b6f1 to b96a392, Claude's Python-cache cleanup, while this ticket ran).
- RAM **159,008 B** of 327,680 (48.5 %; 158,952 before, **+56 B**); flash **2,834,873 B** of 6,553,600
  (43.3 %; 2,829,753 before, **+5,120 B**); `firmware.bin` **2,835,232 B** (was 2,830,112, **+5,120 B**).
  Only RadioLib's two known `#warning`s; none from a changed file.
- `test/host/run.sh`: **exit 0**, **44 C++ binaries + 11 Python checks**. New `test_ntp.cpp` proves:
  the schedule (0 s, 30 s from the connection, 2 min from the connection, then 10 min from the last
  end; one attempt at a time; the 25 s window), the rotation and its wrap, the stop rule against the
  real `timeSourcePriority()` (GPS stops; Wi-Fi, phone, manual and gps-nofix do not; nothing is due
  when not wanted), `force()` (the `ntp now` path) on an idle plan and its refusal while one is in
  flight, reset, and millis() wrap. `test_wifi.cpp` adds the `ntp now|status` parser cases and the
  refusals (`ntp`, `ntp server`, `ntp now extra`, `ntp pool.ntp.org`). `test_est385.py` finds 21 NTP
  literals and clears them.

### D. Device test for Claude (one reader on the port)
1. Flash. Run `tools/cmd.py wifi on` (the saved network auto-connects) and watch the serial.
   Expected, in this order:
   `[ntp] attempt 1 window=25000 ms server=pool.ntp.org (dns)` /
   `[ntp] dns=3 main=set backup=set fallback=unset (addresses not printed)` /
   `[ntp] sleep=on -> off for the attempt (set=1)` /
   `[ntp] dns pool.ntp.org ok public_ip=<public IP> in N ms` (or `failed err=N` / `asked, waiting`) /
   `[ntp] client server=pool.ntp.org addr=<public IP> (no second DNS)` /
   `[ntp] client enabled=1 addr=<public IP> sync=0 notify=0 reach=0`.
2. `tools/cmd.py ntp status` at once: `attempts=1 active=1 last=none server=- ... next_in=0
   next_server=pool.ntp.org clock=None client=1 sleep=0`.
3. **The one number that matters**: after the reply, `[wifi] NTP <epoch> s: applied, source Wi-Fi`,
   `[ntp] attempt 1 server=pool.ntp.org result=ok after N ms`, then `tools/cmd.py status` ->
   **`clock=Wi-Fi`** and the top bar shows HH:MM; `ntp status` -> `last=ok server=pool.ntp.org
   last_age=... next_in=600 next_server=time.google.com clock=Wi-Fi sleep=1`.
4. If it still fails, the new lines name the cause, so **comment them verbatim**:
   - `addr=unset` (and `result=dns-fail`) -> the station's DNS is the fault;
   - `addr=set` with `reach=0` at the timeout -> a packet went out and nothing came back (UDP path or
     modem sleep; the `sleep=` line says whether it was off);
   - `reach` bit set but no `result=ok` -> a reply came and was not accepted;
   - `client enabled=0` -> the tcpip callback did not run (`not-started`);
   - `result=timeout` on names but `result=ok` when attempt 3 uses the fixed `162.159.200.1` (2 min
     in) -> DNS, not NTP.
   Attempt 2 (30 s, time.google.com) and attempt 3 (2 min, the fixed public IP) run by themselves;
   `ntp status` between them shows `next_in` and `next_server`.
5. `tools/cmd.py ntp now` -> `[cmd] ok ntp now attempt=N server=...`; again while one is in flight ->
   `[cmd] error ntp busy`; with a GPS fix holding the clock -> `[cmd] error clock from gps`.
6. With a GPS fix, watch for `[ntp] stop: clock is from GPS, better than Wi-Fi (N attempt(s))` and no
   further `[ntp] attempt` lines; `ntp status` keeps the last attempt.

### E. Could not check (no device, no serial)
- Whether any attempt now gets an answer, and which of DNS / a lost packet / modem sleep it was: the
  host proves the policy, the device lines above prove the network fact. The gateway path can only be
  judged on the device (a router may answer NTP or ignore it).
- The measured cost of `dns_gethostbyname()` and the two `tcpip_api_call` setters on the loop:
  `wifi status`'s `service_us_max` will show it.
- The status bar HH:MM after a Wi-Fi set (unchanged path, `realSinceBoot()`), and that a 10 min refresh
  does not disturb anything.

### F. One thing for Claude to decide (rule 9)
The PRE-EXISTING lines `[wifi] connected to "<SSID>" ip=<LAN IP>` and
`[wifi] status ... ssid="..." ip=...` still print an SSID and a LAN address. This ticket leaves them
untouched (his device measurements quote them), and the rule is enforced on the whole NTP surface.
Say the word and a small follow-up strips the SSID/address from those two lines too.

### G. Next step
Claude reviews the diff, flashes, and runs D1-D6. Comment the `[ntp]` lines of attempts 1-3, the two
`ntp status` answers and `status`'s `clock=`.

## 2026-11-11 10:44 (Claude): EST-384 + EST-385 reviewed
- EST-384: one writer of the node position (position.cpp via PositionSource), the phone set-location taken as Fixed, MeshCore's boot copy of its saved lat/lon overwritten right after meshLinkBegin(); coordinates on screen only.
- EST-385: root-cause analysis accepted (0-5 s SNTP start delay + 15 s lwIP retry vs the old 20 s window); schedule, rotation, stop rule and the no-LAN-address test checked. One gap: WiFi.setSleep(false) during an attempt has no Bluetooth check (coexistence needs modem sleep with BLE on) -> EST-386 before flashing.

## 2026-11-11 (DS, EST-386): NTP keeps Wi-Fi modem sleep on while Bluetooth is on

### A. The gap
EST-385 turned modem sleep off for EVERY attempt (`WiFi.setSleep(false)`, the old
`src/wifi_link.cpp:292`) without looking at Bluetooth. With Wi-Fi and BLE both up (Filip runs them
together) the ESP32 coexistence rule is the opposite: modem sleep must stay on.

### B. What the framework really does (read from the PlatformIO core, no SDK source)
- `esp_wifi_set_ps()` (libnet80211.a, `ieee80211_api.o`) has NO Bluetooth check. It returns
  `ESP_ERR_WIFI_NOT_INIT` when the driver is not started, rejects a type above 2 (`ESP_ERR_WIFI_ARG`),
  and otherwise posts `ieee80211_ioctl(24, ...)` to the Wi-Fi task and returns 0. The caller therefore
  sees success BEFORE the change is applied.
- The check lives in libpp.a `pm.o`, function `pm_set_sleep_type()` (run by the Wi-Fi task). For a
  type that is neither MIN_MODEM nor MAX_MODEM (WIFI_PS_NONE is 0) and with `g_mesh_is_started == 0`
  (we are plain STA, not mesh), it calls the coexistence adapter's "BT enabled" hook (offset 0x190 of
  the OSI func table, `g_osi_funcs_p`) and, when that answers non-zero, it logs
  `Error! Should enable WiFi modem sleep when both WiFi and Bluetooth are enabled!!!!!!` (rodata
  section `.rodata_wlog_error.47`) and then calls `abort()` (both calls are in the relocation table
  of that same section). libcoexist.a `coexist_core.o` carries the same sentence, with a `[%u] `
  prefix, for its own condition check.
- So on THIS build it is worse than "refuses and logs": the Wi-Fi task aborts asynchronously, after
  `WiFi.setSleep(false)` has already returned true. The guard below is what keeps us out of that path.
- Not verified: which BT stack registers that adapter hook at runtime (Bluedroid's `libbt.a`), and
  whether the abort really fires on the device. Only the device log can show it.

### C. What changed
- New `src/ntp_sleep.h`: `om::ntp::SleepGuard`, the whole decision as plain C++ (header only, no
  Arduino), with `begin(bluetoothOn)` / `offResult(offNow)` / `poll(bluetoothOn)` / `end()`.
  - BT off at the start: `begin()` says TurnOff; the attempt owns the off state and `end()` restores
    it exactly once.
  - BT on at the start: `begin()` is neutral; `poll()` and `end()` can never touch the radio.
  - BT starts mid-attempt: `poll()` returns TurnOn exactly once; a later BLE stop in the same attempt
    does not turn sleep off again (the guard never fights a BLE start/stop).
  - A refused change (`offResult(false)`) leaves the guard neutral, so `end()` cannot turn sleep on
    for a change this attempt did not make.
- `src/wifi_link.cpp`: includes `ble_link.h` + `ntp_sleep.h`, one static guard; `ntpBeginAttempt()`
  asks `om::bleIsEnabled()` and either turns sleep off (existing `[ntp] sleep=...` line) or logs
  `[ntp] modem sleep kept on (Bluetooth is on)`; `pollNtp()` puts sleep back at once if BLE starts
  (`[ntp] modem sleep back on (Bluetooth started during the attempt)`); the three restore sites
  (`ntpEndAttempt`, `ntpStop`, the stop-rule branch) only call `WiFi.setSleep(true)` when the guard
  owns an off state. BLE code untouched.
- Both the BLE change and the NTP poll run on the loop task, so nothing can change BLE between the
  `poll()` check and the `setSleep()` call (read from `src/main.cpp`'s loop; not measured).

### D. Build and host tests
- Build (`pio run -e om_lite_l2`): exit 0. RAM **159,016 B** of 327,680 (48.5 %; 159,008 before,
  **+8 B**); flash **2,835,217 B** of 6,553,600 (43.3 %; 2,834,873 before, **+344 B**).
- `test/host/run.sh`: exit 0, all suites green including the new `test_est386.cpp` (BT off -> TurnOff,
  then one TurnOn at the end; BT on -> begin/poll/end all neutral, a stray off-report cannot create
  ownership; BT starts mid-attempt -> one TurnOn, a later BLE stop does not re-off; refused change ->
  neutral; a stale attempt is dropped, not fought; and a fake radio composes exactly one off + one on
  for the BT-off attempt). EST-385's own suite still passes and `test_est385.py` still sees no LAN
  address, SSID or password in any NTP literal.
- Nothing committed, nothing pushed.

### E. Device test for Claude
1. Bluetooth OFF, `tools/cmd.py wifi on`: the EST-385 D1-D6 lines are unchanged, including
   `[ntp] sleep=on -> off for the attempt (set=1)` and `ntp status` -> `sleep=1` once the attempt ends.
2. Bluetooth ON (`tools/cmd.py ble on`) and then Wi-Fi on: each attempt must log
   `[ntp] modem sleep kept on (Bluetooth is on)` and NOT the `sleep=... -> off` line; `ntp status`
   keeps `sleep=1` before, during and after an attempt. This is a safety fix, not a fix for a lost
   reply: with modem sleep on an attempt may still time out, and `sleep=1` is then the expected
   evidence, not a bug.
3. Start an attempt with BT off, then `tools/cmd.py ble on` while it is in flight: exactly one
   `[ntp] modem sleep back on (Bluetooth started during the attempt)`, and the attempt's end must not
   print or perform another restore (no second change in the log).
4. `tools/cmd.py ntp status` after each attempt: `sleep=` is 1.

### F. Could not check (no device, no serial)
- The abort in `pm_set_sleep_type()`: the disassembly is clear, but only the device shows the adapter
  hook answered "BT enabled".
- Whether an NTP reply arrives at all with modem sleep on (the reason EST-385 turned it off was a
  timeout; this ticket removes the unsafe workaround, it does not explain a lost packet).
- The real timing of a BLE start against an attempt (both are loop-task calls; not measured).

### G. Next step
Claude reviews the diff, builds, flashes, and runs EST-385's D1-D6 plus E2-E3 above. The `sleep=` and
`modem sleep` lines answer whether the device now stays out of the coexistence abort and whether NTP
still gets an answer with BLE on.

## 2026-11-11 10:57 (Claude): EST-386 built (guard), GPS no-fix time PROVEN on the device
- EST-386: modem sleep is switched off for an NTP attempt only while Bluetooth is off; DS read the framework: esp_wifi_set_ps(PS_NONE) with BT up makes the Wi-Fi task abort, so the guard prevents a crash. Build + tests green.
- Device, build 5cf263f, 10:52 power-off/on by Filip, L2 near the window: at ~10:55 `clock=gps-nofix`, `gps raw` ZDA 09:55:05 UTC 11.11.2026 matching TestBox to the second, 2 heard / 0 used / best 29 dB-Hz. The EST-383 no-fix path works.

## 2026-11-11 (DS, EST-387): warm boots keep the GPS (no reset on every boot) + AID-INI built, switched off
Status: in_review (not committed, not pushed).

### A. What the ticket found, and what this run confirms in the code
`expanderBegin()` staged expander bit 9 (GNSS reset) HIGH before the first latch and released it LOW
on every boot. The polarity, from the code and the device: the line has been left LOW since the boot
sequence and the module talks, so LOW is "running" and HIGH is "held in reset". The pulse was therefore
the whole span from the latch to the release, on every flash and every crash restart, with the power
rail (bit 13) never dropping: exactly the cold start the ticket describes, next to a receiver that
could have stayed warm.

### B. 1. Warm boots keep the receiver
- New `src/gps_boot.h` (plain C++, host-tested): the reason table, by number, in ESP-IDF's
  `esp_reset_reason_t` values. RESET for PowerOn, Brownout, DeepSleep, Unknown; KEEP for Software
  (ESP.restart(), an OTA reboot), External, Panic, IntWdt, TaskWdt, Wdt, Sdio.
  Two rows are this run's reading of the ticket, and both are argued in the header: **DeepSleep**
  resets because the rails were down (nothing in this firmware deep-sleeps today, but a future duty
  cycle must never assume a running receiver after a wake) and **Unknown** resets because the RTC
  domain that carries the reason was lost, i.e. it looks like a cold start; a needless reset costs one
  start, a wrongly kept receiver after a real power cut costs the whole boot.
- `board.cpp`: `espBootReason()` maps the SDK enum onto that table with `static_assert`s on every
  number, so an SDK that renumbers them fails the DEVICE build instead of silently changing the rule.
  `stageOutput(kBitGnssReset, resetReceiver)`: on a warm boot that stages the level the line already
  has (low), which is not an edge, so the receiver does not notice the restart. The release and its
  40 ms stay for the reset path, so a cold boot runs the same sequence as before.
- One extra input, because the decision cannot be the reason alone: the latched level is read back
  before staging. If the previous boot died between the latch and the release it left the line
  ASSERTED, and the module would sit in reset for ever; that case resets and logs
  `[gps] boot: reset (left asserted by the last boot)`.
- Log lines (the ticket's wording): `[gps] boot: kept running (reset reason N)` or
  `[gps] boot: reset (power-on|brownout|deep-sleep|unknown|left asserted by the last boot)`.
- Init commands: `$PCAS04,7` is the ONLY one this firmware has (`$PCAS03` went with EST-339), and
  nothing in the repo proves whether the L76K restarts its engine on a mode change (the module never
  acknowledges the command either). So on a warm boot nothing is sent and the command is marked as
  already applied (`GpsModeCommand::onModuleWarmStart()`), with `[gps] init: mode 7 kept (receiver kept
  running)`; after a reset it is armed as before (`... armed (receiver was reset)`) and goes out once,
  on the first checksum-valid sentence, as today. Without the new `onModuleWarmStart()` the fresh
  object's `sent_ = false` would have sent the command anyway: that was the trap in "just stop pulsing".
- `$PCAS10,0` (hot start) is NOT used, and this is why: it is only worth sending when the receiver
  needs a known state. On a warm boot it needs nothing (we do not touch it at all, which is strictly
  less than a hot start), and after a reset the pin already gives a clean start that a hot-start
  command could not improve.
- Known limitation, written down rather than hidden: if the module restarts on its own while we run
  (its own watchdog, a GNSS-rail glitch), we cannot see it, so mode 7 is not re-sent until the next
  boot. A detector would be "no bytes for N s, then bytes again", which is a separate ticket.

### C. 2. Aiding: the whole path is built and host-tested, the switch is OFF
- `src/gps_aid.h` (plain C++, host-tested) has four parts: the CASIC frame (`BA CE | len | class | id
  | payload | sum`), the AID-INI payload builder (56 bytes, field by field), a byte-stream reader that
  pulls ACK/NAK frames out of the NMEA stream (a bad checksum or a silly length is dropped and the
  reader resynchronises), and the ticket's once-per-boot rule as one object (`AidGate`: only a real
  clock from Wi-Fi/phone/manual with no fix, once, and again only when a BETTER source arrives).
- `board.cpp` wires it: one frame in flight at most, a 2 s answer window, and one line
  `[gps] aid time=ok|nak|no-answer pos=sent|none src=<source>` (tokens only, never a value; rule 9).
  The own position is `posOwn()`, i.e. the same point the node advertises (Fixed or the last fix).
- **`constexpr bool kEnabled = false;` and this is the honest part of the run**: this sandbox has no
  network (DNS fails), so the CASIC AID-INI field table could not be read; the offsets in
  `aidIniPayload()` are written from memory of the specification and every field that matters is
  marked `[unverified offset]` in the header. A wrong-but-accepted frame would seed the receiver with
  a wrong time, and indoors the receiver's time is what sets our clock (EST-383), so a wrong aid is
  worse than no aid. The ticket's own rule is "keep only if the module ACKs"; there is no ACK to read
  in here, so the switch stays off and the device boots with `[gps] aid: off (kEnabled=false)`.
- Turning it on is one line (`kEnabled = true`): then the boot says `[gps] aid: armed`, and the first
  moment a real non-GPS clock exists with no fix it prints the `time=... pos=... src=...` line. If
  that line says `nak` or `no-answer`, it goes back to false and the field table is checked against
  the spec. A host test (`testSwitchIsOff`) fails the run the moment the constant is flipped, so the
  plan above cannot be forgotten.

### D. Tests
- `test/host/run.sh`: exit 0, all suites green.
- New `test_est387.cpp` (g++, over the same headers the firmware compiles): the reset table reason by
  reason (including the SDK numbers and an unknown number mapping to Unknown), the two boot-line words,
  the AID-INI frame BYTE BY BYTE against a hand-computed example (66 bytes: sync, length 62,
  class/id, payload offsets 4/6/8/16/24/28..34, sum 0x0302), the same with a position (lat 46.0 /
  lon 14.5 as IEEE-754 doubles), the refusals (2026-02-30, hour 24, lat 91, lon -181, a short buffer),
  the ACK and NAK frames (hand-computed sums 474/475) through the reader, NMEA passing through
  untouched, a bad checksum, a nonsense length followed by a good frame, a frame split one byte at a
  time, the aid gate (no fix, no real clock, GPS sources excluded, once, better source only, a worse
  source with a very different time still silent) and the UTC civil round trip (1970, the ticket's
  timestamp, a leap day, the last second of a year).
- New `test_est387.py`: every `[gps`/`[gpsraw` log line in `board.cpp` and every log line in
  `gps_aid.h`/`gps_boot.h`/`gps_raw.cpp` carries no float conversion and no lat/lon name; the aid line
  has the ticket's shape and all five tokens; both boot lines exist; `kEnabled` is one false constant.
- `test_gps_mode.cpp` grew the warm-start case: after `onModuleWarmStart()` twenty sentences send
  nothing, and a later real restart arms it exactly once.

### E. Build (sandbox, real exit code)
`pio run -e om_lite_l2`: **exit 0**. RAM **159,024 B** of 327,680 (48.5 %; 159,016 before, **+8 B**);
flash **2,835,921 B** of 6,553,600 (43.3 %; 2,835,217 before, **+704 B**). The aiding code is compiled
but its switch is a constant, so the optimizer keeps almost none of it: the +704 B is the boot
decision, the three new log lines and the warm-start method.

### F. Device test for Claude (indoors, this is the ticket's own plan)
1. Flash this build. The first boot after the flash is an OTA reboot, i.e. a SOFTWARE reset: the log
   must say `[gps] boot: kept running (reset reason 3)` and `[gps] init: mode 7 kept (receiver kept
   running)`, and `$PCAS04,7` must NOT appear (`[gps] mode 7 sent` absent). `[gps] aid: off
   (kEnabled=false)` appears once.
2. Immediately `tools/cmd.py gps raw 30` and keep the `[gpsraw]` lines and the `[gps] ... used=
   expected= heard= best= maxheard=` lines.
3. Flash the SAME build again (a second OTA, another software reset) and, as soon as the second boot
   is up, `gps raw 30` again. Compare: after the second boot the ZDA/RMC time should be there at once
   (`[gpsraw] t=0s type=GNZDA time=... date=...`) and `maxheard` must not start over: the receiver was never restarted, so its RTC is still right and
   its almanac is not being downloaded again. The two `gps raw 30` summaries (`types=`, `zda=`, `bad=`)
   and the two `[gps] no-fix` lines are the numbers to compare.
4. Power OFF (the switch) and ON: now the log must say `[gps] boot: reset (power-on)` and
   `[gps] init: mode 7 armed (receiver was reset)`, followed later by `[gps] mode 7 sent`; that is the
   control case proving the table has both branches on the device.
5. Phone time: set the clock from the phone (or Wi-Fi NTP) with no fix, watch for the aid line. With
   this build it is `[gps] aid: off (kEnabled=false)` at boot and nothing else, by design (see C). To
   read `[gps] aid time=... pos=... src=phone`, one line changes (`kEnabled = true` in `gps_aid.h`)
   and the same test prints it; then either the ACK confirms the frame or the line says `nak` /
   `no-answer` and the switch goes back to false.

### G. Could not check (no device, no serial)
- That a warm boot really leaves the L76K's RTC and ephemeris intact on THIS module: the code no
  longer pulses the line, but only the device can show the clock still being right after a reflash.
- Whether `$PCAS04,7` restarts the receiver's engine: no source in the repo proves it either way, which
  is why the command is not sent to a warm receiver at all rather than "probably harmless".
- The AID-INI field table and the frame length convention, and therefore whether the module ACKs:
  no network in the sandbox (see C).
- That the expander's output register survives a warm boot with bit 9 low on this board: it is what the
  existing comment and `gpsRailOn()` already assume for bit 13, and the new read-back makes a wrong
  assumption visible in the log rather than silent.

### H. Next step
Claude reviews the diff and runs F1-F4 (two flashes and one power cycle, indoors). The one open
decision is C's switch: if Filip wants the aiding tried, it is `kEnabled = true` and one flash; the
`[gps] aid` line then says whether the L76K speaks AID-INI. The reset-reason work needs nothing
further: `[gps] boot:` plus the two `gps raw 30` summaries settle it.

## 2026-11-11 11:18 (Claude): EST-387 reviewed
- Reset table (reset only on power-on, brown-out, deep-sleep, unknown, or a line left asserted), the warm-boot path stages the level the line already has (no edge), PCAS04 not re-sent on a warm boot via onModuleWarmStart(). AID-INI built but kEnabled=false: field offsets written from memory, unverified; keep off until checked against the CASIC spec. Not on the device yet.

## 2026-11-11 (DS, EST-388): `https probe`, how much internal RAM a TLS handshake needs
Status: in_review (not committed, not pushed). Measurement only: nothing the user sees changes, and
nothing is tuned.

### A. What this run answers, and what it does not
The ticket's number is the internal-RAM cost of one HTTPS GET to `game.cl0.eu` with certificate
validation ON. The command now exists and reports it; the five device runs are Claude's (F below).
This sandbox has no network and no device, so the handshake itself was NOT measured here. Two facts
about the target are also unverified from here: that `game.cl0.eu` still chains to ISRG Root X1, and
that its certificate matches the host name (the probe reports those as `result=tls err=...`).

### B. The command, and where it runs (EST-350)
- `https probe`, one closed form: no word carries a host, a path or a query, so the host can point
  the probe nowhere else. Target: `game.cl0.eu`, `/api/v1/health`, 443, 10 s, from
  `src/https_probe.h`.
- The handler (`usb_commands.cpp`) only records the request. `wifiLoop()` runs it, never the USB
  command stack: phase 1 starts the lwIP lookup with a callback (no blocking DNS), phase 2 polls it
  (3 s cap), phase 3 does the exchange. The handler answers with the data line
  `[https] probe start host=... path=... port=443 timeout_ms=10000 ca=isrg-root-x1`, then the loop
  prints `[https] probe N lookup ...`, the report line and one `[cmd] ok https probe N result=...`.
  `tools/cmd.py` waits 20 s for this command (its own budget is 10 s) and the start line is not an
  answer line, like `gps raw`.
- Refusals: `wifi off`, `wifi not connected`, `https busy`. A link that drops while the probe is
  queued cancels it (`[https] probe N dropped: link lost`).
- The one blocking call, and its justification: Arduino's `WiFiClientSecure::connect()` is DNS +
  TCP + one `mbedtls_ssl_handshake()` call, with no public way to step it, so the exchange holds the
  loop task until it ends (bounded by the 10 s deadline; the connect timeout is the remaining budget,
  `setHandshakeTimeout()` the same). Splitting it means writing a second TLS client, which is not a
  measurement ticket. The screen and MeshCore's RX may stall for the length of a probe; it is a
  host-only diagnostic, and the alternative (a private task) would cost 4 to 8 KB of internal RAM
  and pollute the number being measured. The loop task has 16 KB of stack; the probe adds about
  1 KB of frames plus mbedTLS's own.

### C. What is measured, and what "lowest during" means here
One report line, every number the ticket names:
`[https] probe N result=... stage=... status=... bytes=... body=... dns_ms=... conn_ms=...
tls_ms=... hs_ms=... total_ms=... err=... internal_before=... internal_min=... internal_after=...
largest_before=... largest_min=... largest_after=... psram_before=... psram_after=...
idf_min=... ble=on|off ntp_active=0|1`.
- `conn_ms` is a plain-TCP connect on its OWN connection to port 443 (no TLS), measured just before
  the real one; `tls_ms` is the secure connect (TCP + handshake, no DNS); `hs_ms` is `tls_ms -
  conn_ms`, an ESTIMATE from two connections, printed signed. Arduino returns only the sum, so this
  is the closest split without a custom client.
- `internal_min` / `largest_min` come from a 2 ms sampler, an `esp_timer` created once in
  `wifiBegin()` and started only for the exchange: the handshake is one blocking call, so only a
  second context can see the bottom. The timer is created at boot, not per probe, so run 5 starts
  from the same heap as run 1. `idf_min` is `heap_caps_get_minimum_free_size(INTERNAL|8BIT)` after
  the probe, i.e. IDF's all-time low since boot; it can be LOWER than `internal_before` from an
  earlier moment (a BLE start, for example), which is why the sampled `internal_min` is the "during"
  number and `idf_min` is the cross-check.
- Caps are `MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT`, the same mask as `status`/`wifi status`
  (`mem_stats.h`), so the numbers compare with the ones already in PROGRESS.
- `ble=` says whether Bluetooth is on at the exchange (`bleIsEnabled()`); `ntp_active=` says an SNTP
  attempt was in flight (it is not refused, it is recorded, so a noisy run can be spotted).
- Certificate validation: pinned ISRG Root X1 (`src/https_probe_ca.h`, PEM, SHA-256 96:BC:EC:06:...).
  The ticket allowed the built-in x509 bundle instead; on Arduino core 2.0.17 the bundle path
  (`WiFiClientSecure::setCACertBundle`) needs a bundle binary the application supplies and nothing
  in this tree supplies it (read in the framework's `esp_crt_bundle.c`), so pinning one root is both
  the available and the quieter choice (one 2 KB PEM parse instead of a search over 136 roots).
  `test_est388.py` decodes the PEM and checks that exact fingerprint. Note for the report: this
  build has `CONFIG_MBEDTLS_HAVE_TIME_DATE` OFF, so mbedTLS checks the chain, signatures and the host
  name but NOT certificate dates; that is a fact about the stack, not a change made here.
- Freed after every run: `client->stop()` and `delete` free the mbedTLS contexts and the two 16 KB
  record buffers, the plain client closes its socket, the read buffers are stack. The 5-run test
  compares `internal_after` and `psram_after` for exactly this.

### D. The mbedTLS levers that would move buffers to PSRAM (the ticket's item 3)
Read from this core's own `tools/sdk/esp32/sdkconfig` and `mbedtls/port/include/mbedtls/esp_config.h`
(Arduino core 2.0.17, IDF 4.4):
1. `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` (now internal: `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y`): makes
   mbedTLS's allocator use PSRAM. The allocator is `esp_mbedtls_mem_calloc` in the PREBUILT
   `libmbedcrypto.a`; objdump of that object shows the caps literal `0x00000804`
   (`MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT`) compiled in. So: compile-time only, needs rebuilt IDF
   libraries; not settable from `platformio.ini`, a `-D` or code. (PSRAM is in the heap here:
   `CONFIG_SPIRAM_USE_MALLOC=y`, `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, but that threshold only
   affects plain `malloc()`, and mbedTLS never goes through it on this build.)
2. `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN` (16384) and `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN` (off):
   the two record buffers are 16 KB + 16 KB of internal RAM. Setting 4096, or asymmetric (e.g.
   1024 in / 4096 out), is the biggest single lever (32 KB down to 5 KB). Compile-time in the same
   prebuilt libraries: needs rebuilt libs, not settable today.
3. `CONFIG_MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH` (off) plus `mbedtls_ssl_conf_max_frag_len()`: would
   shrink the buffers at runtime to the negotiated fragment length.
   `MBEDTLS_SSL_MAX_FRAGMENT_LENGTH` IS compiled in, but the variable-buffer option is not, and
   `WiFiClientSecure` does not expose `ssl_conf`, so it needs both a rebuilt library and a custom
   client.
4. `CONFIG_MBEDTLS_DYNAMIC_BUFFER` (frees the in/out buffers after the handshake): not present in
   this IDF build's sdkconfig, so not available at all here.
5. Not an mbedTLS setting, but the one hook that needs no rebuilt library: a linker wrap
   (`-Wl,--wrap=esp_mbedtls_mem_calloc` plus `__wrap_esp_mbedtls_mem_calloc` allocating with
   `MALLOC_CAP_SPIRAM`) redirects every mbedTLS allocation. It is a build-flag change, but it is
   tuning and it changes cipher speed, so it stays out of this ticket; noted for a later decision.
Nothing in 1 to 4 is settable on this stack without rebuilding the prebuilt mbedTLS/Arduino
libraries; therefore this ticket measures the handshake AS IT IS, which is what the ticket asks.

### E. Build and host tests
- Build (`pio run -e om_lite_l2`), real exit code 0. RAM **160,120 B** of 327,680 (48.9 %; 159,024
  before, **+1,096 B**); flash **2,977,089 B** of 6,553,600 (45.4 %; 2,835,921 before, **+141,168 B**).
  The flash delta is the finding worth reading twice: the PEM is only 1.9 KB. The TLS code itself is
  the cost: in the linked ELF the mbedTLS symbols sum to about 102 KB today against about 29 KB in
  the 09:27 review build (a different tree, so an estimate), WiFiClientSecure's own three objects are
  about 12 KB of .text, and ssl_client.cpp's name check pulls in some of the C++ string code. The
  built-in 136-root bundle is NOT linked (no `_binary_x509_crt_bundle` symbol; checked). Carrying
  this TLS client costs about 141 KB of flash on this build; the ticket's WHY deserves that number
  as much as the RAM one.
- `test/host/run.sh`: exit 0, every suite green.
- New `test_est388.cpp`: the fixed target (host, path, 443, 10000 ms, the DNS and idle caps inside
  it), the three refusal words and the accepted case, the HTTP status line (200/404/503/204/418, a
  partial line, a bad prefix, a bad version, a four-digit "2000" that must not pass, the first status
  winning over a second one), the header terminator (CRLF and bare LF, an unterminated header, the
  empty line at the buffer's end), the two mbedTLS allocation codes and four errors that must not
  classify as OOM, and the report line field by field (a negative `hs_ms`, a 32-byte buffer
  truncation, a null buffer).
- New `test_est388.py`: decodes the embedded PEM and checks its SHA-256 is ISRG Root X1, no private
  key, the report format keeps every ticket field, the parser's https block accepts exactly `probe`
  and no URL literal, the start line is not an answer line, and rule 9 (no coordinate or secret in
  the new files).
- `test_cmd.py` and `test_usb_cmd.cpp` grew the `https probe` cases (the 20 s answer window, the
  parser cases, the refused spellings).

### F. Device test for Claude (the ticket's plan; indoors, Wi-Fi on)
1. Flash this build, `tools/cmd.py wifi on`, wait for `connected`, then `tools/cmd.py https probe`.
   The first line is `[https] probe start ...`, then `[https] probe 1 lookup ...`, then one
   `[https] probe 1 ...` line and one `[cmd] ok https probe 1 result=...`.
2. BLE OFF: run `https probe` five times, keep the five `[https]` lines. Compare `internal_after`,
   `largest_after` and `psram_after` across the five: equal values are the no-leak proof. Run 1 may
   show a larger `dns_ms` (cold lookup); `conn_ms`/`tls_ms`/`total_ms` show the network jitter.
3. `tools/cmd.py ble on`, wait until Bluetooth reports advertising, then five more `https probe`
   runs. The interesting comparison: `internal_before` (lower with BLE up, the ticket's ~65 KB),
   `internal_min` (the handshake's own bottom) and whether `result=ok`.
4. If a run says `result=oom` (`err=-32512` or `err=-10368`), that IS the answer: the handshake does
   not fit with that much internal RAM free. Do not tune; send the line.
5. Also paste `wifi status` and `ble status` before the first run (their `internal`/`largest` are the
   baseline) and the heartbeat `stack=` after a probe (the exchange's own stack high-water mark).

### G. Could not check (no device, no network here)
- Whether `game.cl0.eu` still chains to ISRG Root X1 and presents a matching name. If the device says
  `result=tls` with an X509 error, the first check is the chain, not the RAM.
- The real numbers: `internal_min`, the handshake time and whether the 10 s budget is enough.
- That the 2 ms sampler catches the true bottom. It samples the same counters IDF itself keeps, and
  `idf_min` (exact, all-time) is printed beside it, so a miss would show as `idf_min < internal_min`;
  that would make `idf_min` the answer for that run, at the cost of possibly including an earlier low.
- That the mbedTLS allocation-failure codes arrive as `MBEDTLS_ERR_SSL_ALLOC_FAILED` on this IDF: the
  device build static_asserts the two numbers against the real macros, so a silent change fails the
  build rather than producing a wrong result word.

### H. Next step
Claude reviews the diff, flashes, and runs F2 and F3 (five runs each). The two headlines for Filip
should be: internal free at the handshake's bottom with BLE off and with BLE on, and the fact that a
TLS client adds about 141 KB of flash to this build.


## 2026-11-11 11:44 (Claude): EST-388 built; its pinned CA does not match the server
- Build + tests green (RAM 160,120 B, flash 2,977,089 B: +~147 KB for TLS). The pinned ISRG Root X1 is correct (SHA-256 96:BC:EC:06:...), but game.cl0.eu serves the newer Let's Encrypt chain leaf -> YE1 -> Root YE -> ISRG Root X2 (openssl s_client from TestBox, verify 0). With only X1 pinned the probe would fail on the certificate and look like a memory failure -> EST-389 pins X1 + X2. Not on the device.
- ISRG Root X2 on TestBox: 69:72:9B:8E:15:A8:6E:FC:17:7A:57:AF:B7:17:1D:FC:64:AD:D2:8C:2F:CA:8C:F1:50:7E:34:45:3C:CB:14:70

## 2026-11-11 (DS, EST-389): the HTTPS probe also trusts ISRG Root X2 (game.cl0.eu's chain)
Status: in_review (not committed, not pushed). Certificate validation stays ON; the only behaviour that
changes is which root the probe accepts.

### A. What changed, and why
- Claude's s_client check (2026-11-11 11:43) found leaf CN=game.cl0.eu -> CN=YE1 -> CN=Root YE ->
  issued by ISRG Root X2. EST-388 pinned X1 only, so the probe would have failed certificate
  validation, and that failure would have looked like the out-of-memory answer the ticket hunts for.
- `src/https_probe_ca.h` now holds ONE buffer, `kCaPem`, built from two adjacent raw string literals:
  X1 then X2. Two literals, one buffer: the compiler joins them, nothing is duplicated in flash, and
  the parse call still takes one string. That is the documented mbedTLS behaviour (vendored header,
  `mbedtls_x509_crt_parse`: "one or more concatenated PEM-encoded certificates"; 0 when all parse, a
  positive count when some fail, negative when none).
- Fingerprints in the header, in buffer order: X1 `96:BC:...:08:C6` (unchanged), X2
  `69:72:9B:8E:...:CB:14:70`. The X2 value was re-read here from `/etc/ssl/certs/ISRG_Root_X2.pem`
  (`openssl x509 -sha256`) and matches Claude's note exactly.
- `src/wifi_link.cpp` passes `kCaPem` to the same `WiFiClientSecure::connect()`. Arduino's
  `start_ssl_client()` sets `MBEDTLS_SSL_VERIFY_REQUIRED` whenever a root buffer is passed (checked in
  the framework), so validation is on before and after this ticket.
- The report line gains `ca=isrg-x1+x2` as its LAST field (`kCaLabel` in `https_probe.h`); being last,
  its presence also proves no earlier field was truncated. The command's start line printed
  `ca=isrg-root-x1` as a second, hand-written literal; it now prints the same constant, so the two
  lines cannot drift apart. (The EST-388 entry above still shows the old word; this supersedes it.)

### B. Build and host tests
- Build (`pio run -e om_lite_l2`), real exit code 0. RAM **160,120 B** of 327,680 (48.9 %), unchanged
  (the PEM lives in .rodata in flash, not in RAM); flash **2,977,921 B** of 6,553,600 (45.4 %);
  EST-388 was 2,977,089, so **+832 B**, about the size of the added PEM text (~790 B). The 22 warnings
  in the log are the pre-existing RadioLib `#warning` lines; none come from the changed files.
- In the linked image: exactly two real PEM headers, at 0xc2998 and 0xc312c, and the bytes between them
  are `-----END CERTIFICATE-----` then a blank line then `-----BEGIN CERTIFICATE-----`, so the two
  literals really did become one contiguous NUL-terminated buffer. A third "BEGIN CERTIFICATE" string
  exists at 0x1002ad and it belongs to mbedTLS's own error-message table ("The certificate validity has
  expired" follows it), not to a third certificate.
- `test/host/run.sh`: exit 0, every suite green.
- New `test_est389.py` decodes every PEM in the header and proves: one buffer symbol only, two
  adjacent literals, exactly two certificates in X1-then-X2 order, each SHA-256 equal to BOTH the
  fingerprint in the comment and the published value, no private key, no `setInsecure()` or bundle
  switch in the probe path, and the `ca=` word agreeing with the two roots. The fingerprints are
  parsed out of the comments, so a stale comment fails the run.
- That test was checked against four mutations of a copy in /tmp (it is not a test that cannot fail):
  X2's block removed, one hex digit of the X2 comment changed, one base64 character of X2 changed, and
  `wifi_link.cpp` pointed back at a single-root symbol. Each failed with its own message; the
  unmutated copy passed.
- `test_est388.py` loses its single-root assertions (now false by design) and keeps the validation-ON
  guard; the CONTENT of the anchors is EST-389's test. `test_est388.cpp` asserts the `ca=` word is the
  last field of the line.

### C. Could not check (no device, no network here)
- That the handshake now succeeds against the live server. The expected line is `result=ok` with
  `ca=isrg-x1+x2`; a `result=tls` with an X509 error now points at the chain or the name, not at RAM.
- The parse count on the device: Arduino checks the parse result only for `< 0`, so one malformed PEM
  inside a two-certificate buffer would be accepted silently (mbedTLS returns a positive count). Both
  PEMs decode cleanly here, so this is not expected, but the device build cannot assert it.
- The extra internal-RAM cost of parsing one more certificate inside the measured window: X2's DER is
  543 B, so `internal_min` in the five runs will include a one-time parse that EST-388's build did not
  have. The linker's static numbers do not show it.
- Whether the server ever serves the older X1 chain again: both roots are pinned, so either chain
  validates.

### D. Next step
Claude reviews the diff, flashes, and runs the ticket's five `https probe` runs (BLE off, then on).
The two headlines: does it say `result=ok` now, and what `internal_min` reaches with both roots pinned.

## 2026-11-11 (DS, EST-390): touch feedback, the pressed look on every element and a key preview
Status: in_review (not committed, not pushed).

### A. The one rule, and why it is an overlay
The L2 has no vibration motor, so the pressed look IS the feedback. It is implemented once:
- `src/ui/theme.h` gains ONE look: `kPressedTint` (the OM gold accent) and `kPressedAlpha` = 56 (about
  22 %). On the darkest plate (`kKeyBg` #232220) that lifts a pressed key to about #4a3c28 while its
  white glyph keeps its contrast.
- `src/ui/press.h` (plain C++, host-tested) holds the whole rule: `PressTarget` (the rectangle + the
  key's character), `PressTracker` (arm on the press edge, keep while the finger is on the rect, clear
  on slide-off or release), `pressBlend()` (the one mix), `pressBubbleRect()` (the key preview's
  placement) and `PressLatencyLog` (the once-a-minute worst case).
- `src/ui/press_targets.h` (plain C++) maps a point to that rectangle PER SCREEN, and every function
  asks the screen's EXISTING hit test first and then returns the rectangle that hit test just used:
  the tab bar / gear / fault mark, the Nodes rows and its sheet, the shared dropdown rows (Chat and
  Settings), the Settings tabs / rows / stepper keys / Back / title action / confirmation keys, the
  keyboard keys (`keyHit`, the tap target), the emoji cells, the Map tabs / chip / hidden ghosts /
  markers / callout, and the RPTR list, page and map-picker keys.
- `src/ui/press_feedback.{h,cpp}` owns the one instance and draws it as a FRAME OVERLAY
  (`omFrameBlendRect()` in board.cpp mixes the accent into the frame's own pixels). The overlay is
  registered before the toast, so the pressed look is on the finished frame, on exactly that
  rectangle, and every redraw path carries it. No screen draws a pressed plate of its own.
- The shell (main.cpp) arms it on the press edge with `pressArmAt()`, only when the gesture is NOT
  spent and not a pan continuation (so a tap that only dismissed a toast or closed a pop-up draws
  nothing on what is under it), clears it before the release action runs, and clears it on a screen
  change or when the finger slides off the rect.

What a tap DOES is unchanged, and this is deliberate: the ticket's own item 4 says "Nothing changes in
what a tap DOES", so a slide-off clears the LOOK but the release still runs the same action it always
did (the shell's press/drag/release logic was not touched). The ticket's "then no action, as today"
would have been a behaviour change to the tap path, so it is called out here for Filip/Claude to
decide; if he wants slide-off to cancel the action too, that is a one-line change in the release path
and not a feedback change.

Latency: `pressArmAt()` composes the frame at the press edge (no waiting for the next 20 ms pass) and
samples the time to the END of that frame's push; `[press] latency worst=N ms over M presses in 60 s`
is printed once a minute. The number is NOT measured here (no device): it is the one number the
device test reads.

### B. The keyboard
- Every key gets the pressed look on `keyHit` (the tap target: the outer keys reach the screen edges
  and the bottom row reaches the floor), so the tint can spill a few px into the sheet's 4 px side pad
  on the outer keys. That is the ticket's own rule ("exactly the rect the hit test uses") and is the
  one place where the drawn cap is smaller than its pressed rect.
- The preview bubble reuses the long-press accent drawing: `drawAccentBubble()` now delegates to a new
  `drawPreviewBubble(d, at, cp)` (same panel2 plate, gold rim, `SansTitle` face, 26x24 box), so the two
  bubbles cannot drift apart. It exists only for keys that type a character (space, backspace, shift,
  the page keys, Send and the smiley get the pressed look only), and with shift it previews the capital
  that will actually be typed.
- Placement is `pressBubbleRect()`: centred on the DRAWN key, its bottom 2 px above the key's top,
  clamped inside the screen and below the status bar, AND clamped so its bottom never goes below the
  composer's draft line minus 2 px. For a lower row that changes nothing (the accent bubble already
  sits in the gap between the draft and row 0); for a TOP-row key it lifts the bubble above the draft
  line instead of covering the text being typed. The host test walks every key of both pages (with and
  without a reply bar) and proves no bubble overlaps `g.draft`.
- When the s/c/z long-press accent preview appears at that same spot, `pressDropPreview()` drops the
  key's character bubble and keeps the pressed tint, so one bubble is shown, the accent's.

### C. The optional click sound
It already existed as "Touch clicks" (EST-359), OFF by default, played from `handleTap()` (so never on
a drag). This ticket:
- renames the Settings row to "Touch sound" (Settings > General > Sound; the NVS key `tsnd` and the
  OFF default are unchanged),
- and changes the Touch event's default library sound from "Beep Clean" (110 ms, beep at level 200) to
  **"Soft Tick"** (library id 56): 80 ms, ONE 1175 Hz soft-voice note at level 70, the quietest short
  sound in the library. The host test renders every sound of 120 ms or less and proves none is quieter.
  The picker still offers every sound; only the default changed.

### D. Build and tests (real exit codes)
- `pio run -e om_lite_l2` (the REBUILD.md command): **exit 0**, SUCCESS.
- RAM **160,168 B** of 327,680 (48.9 %; 160,120 before, **+48 B**); flash **2,985,501 B** of 6,553,600
  (45.6 %; 2,977,905 before, **+7,596 B**); `firmware.bin` **2,985,872 B** (+7,600 B). Only the two
  known RadioLib `#warning`s; none from a changed file.
- `test/host/run.sh`: **exit 0**, **48 C++ binaries + 15 Python checks** (one new binary, one new
  Python check).
- New `test_est390.cpp`: the state machine (arm, stay, a slide-off clears and does NOT re-arm on
  re-entry, the half-open edge, an invalid/empty target arms nothing, clear forgets the target); the
  blend's two ends and monotone ramp; and pressed-rect == hit-rect for a sample of elements on EVERY
  screen: the 4 tabs pixel-covering the whole bar, the gear and the fault mark, every visible Nodes row
  at three scrolls, every sheet row, every chat row of the dropdown and the Manage row, every Settings
  row at two scrolls, the stepper's -/+ (and its label/value being the row), the settings tabs / Back /
  title action / both confirmation keys, EVERY key of both keyboard pages (pressed rect == keyHit,
  bubble == the accent box, centred, above the key, on screen, never over the draft, and only for
  character keys), the emoji cells (with the hit test's own index round trip), the Map's five tabs (and
  their hidden slivers), the chip's 30x26 target, the hidden zoom ghosts, a marker's box and the
  callout, the RPTR list rows at four scrolls and the Scan chip, the page's Back / Apply / row / row
  key, the picker's keys, the latency log's 60 s window and its millis() wrap, and the touch sound's
  default (id 56, 80 ms, 1175 Hz, the quietest short one).
- New `test_est390.py`: one theme constant and one owner, no screen draws its own pressed plate, the
  shell arms only for an unspent non-drag press from the shared maps, the overlay order is press then
  toast, `drawAccentBubble` draws through `drawPreviewBubble`, the preview exists only for character
  keys, the touch sound's stored default is OFF, `playClick()` is called from the tap path only, the
  "Touch sound" label, and the latency line's format.

### E. Preview
`docs/shots/2026-11-11-est390-touch-feedback.png`, generated by the new `tools/mkpress.py`: its C++
dumper compiles the firmware's own `ui/chat_layout.h` / `ui/layout.h` / `ui/theme.h` and prints the
real rectangles and colours, and the pressed fills are `pressBlend()`'s own output. Left panel: the
keyboard sheet with "k" held, the pressed key and its bubble above the draft line ("helo|" stays
readable). Right panel: the Nodes list with the second row held. Invented names and texts only
("Alice", "Bob", "Rptr Hill", "Sensor 4").

### F. Could not check (no device, no serial)
- The real press-to-pixels latency: only the device's `[press] latency worst=...` lines measure it.
  The ticket's ~30 ms target depends on the compose + push of the frame that is redrawn at the press
  edge (a full screen compose, then the ~4 ms SPI push). If the worst case is too high, the levers are
  a partial push of the pressed rectangle (a second drawing path, which rule 4 dislikes) or drawing the
  tint straight to the panel for the press edge only.
- How the tint reads on the glass in daylight, on the Map's light tiles (the overlay tints them too:
  the marker box and the callout), and whether the outer-key spill on the keyboard looks right.
- Whether the redraw at every press edge is acceptable in use on the Map (a cached canvas repaint per
  press) and on the Chat page: the ticket's own cost, measured by the `[press]` line.
- The click sound's real loudness at 60 % volume: the host only proves the render's peak is the
  quietest of the short sounds.
- One deliberate deviation is written in A: a slide-off clears the look but does not cancel the
  action, because item 4 says nothing about what a tap DOES may change.

### G. Next step
Claude reviews the diff, flashes, and checks on the glass: a tab, a Nodes row, a settings row, a
keyboard key (pressed + bubble, top row and a lower row), a selector row, a sheet row, the Map's tabs
and chip, and an RPTR row. Then watch one minute of `[press] latency worst=...` on the Chat page with
the keyboard open (the worst screen: a full chat + sheet compose). If Filip wants the slide-off to
cancel the action as well, that is his call and a one-line change.

## 2026-11-11 (DS, EST-391): the NTP capture's 32-bit `time_t` overflow, fixed in 2 checks
Status: in_review (not committed, not pushed).

### A. Root cause and fix
Claude proved on 2026-11-11 15:58 that the device GETS an NTP reply (`[ntp] ... reach=1`) but the
capture drops it (`sync=0 notify=0`). On this toolchain (esp-2021r2-patch5, GCC 8.4, xtensa-esp32s3)
`sizeof(time_t) == 4`, so the bound cast `(time_t)om::kMaxSaneEpoch` is **-192522496** (the built
`.word`); `tv->tv_sec >= -192522496` was true for EVERY real reply, so `src/wifi_link.cpp:44` and `:55`
returned before storing. That was the whole "Wi-Fi time never arrives".

- `src/ntp_plan.h` now holds the check in ONE small inline function, `ntp::replyEpochSane(int64_t)`,
  which widens the seconds to `int64_t` before comparing against `kMinSaneEpoch` / `kMaxSaneEpoch`
  (the timekeeper's own constants, `#include "timekeeper.h"`). One place, so the two hooks and the
  host test cannot drift.
- `src/wifi_link.cpp`: `sntp_sync_time()` and `sntpNotify()` both go through one `captureNtpReply()`
  helper that does `const int64_t s = (int64_t)tv->tv_sec;`, calls the check, then stores
  `s_ntpEpoch = (uint32_t)s` and `s_ntpGot = true` exactly as before. Nothing else on EST-385's path
  changed (the schedule, the rotation, the sleep guard, the "after its attempt was closed" drop and
  `timeKeeper().setFromSource()` are untouched).
- The capture now logs one line, `[ntp] reply captured: epoch accepted` (no time value), printed in
  the hook itself so the device log shows the catch even when the reply lands after its attempt was
  closed (the loop still drops that one, honestly logged).
- Grep of the whole `src/` tree: the only `(time_t)kMinSaneEpoch` / `(time_t)kMaxSaneEpoch` casts
  were those two; `saneEpoch()` and `ui/age.h` already compare in `uint32_t` and are correct
  (`kMaxSaneEpoch` fits in `uint32_t`), so they were left alone.

### B. Host test
New `test/host/test_est391.cpp`, built and run by `test/host/run.sh` (BIN49):
- a `DeviceTimeval` struct with `int32_t tv_sec` holding **1794409120** (2026-11-11 14:58:40 UTC);
- the OLD expression, replicated with the device's `int32_t` casts, is asserted to REJECT it (so the
  test fails if the old code comes back), and `ntp::replyEpochSane()` (the firmware's own function)
  is asserted to ACCEPT it, plus the combined assertion;
- the proof as numbers: `(int32_t)kMaxSaneEpoch == -192522496` while `(int64_t)kMaxSaneEpoch ==
  4102444800`; the window's edges (`min-1` and `max` refused, `min` and `max-1` accepted), `0` and
  `-192522496` refused.

### C. Build and tests
- `pio run -e om_lite_l2` inside the sandbox: **SUCCESS**, exit 0. RAM **160168 / 327680 B (48.9 %)**,
  flash **2985705 / 6553600 B (45.6 %)**.
- `test/host/run.sh`: **exit 0**, all suites pass, including the new
  `est391: the NTP capture accepts a 2026 reply on a 32-bit time_t`, and EST-385's rule-9 format grep
  still passes with the new line (no address, SSID or password in it).

### D. Could not check (no device, no serial)
- That the reply now really lands: requires a flash and a Wi-Fi connection. The expected device log is
  `[ntp] reply captured: epoch accepted`, then `[wifi] NTP <epoch> s: applied, source Wi-Fi`, then
  `[ntp] attempt 1 ... result=ok`.
- Whether `Serial.println()` from the two hooks (both run in the tcpip thread) ever stalls the stack:
  it is one short line per accepted reply, at most one per attempt, and the ticket asks for the line
  in the hook. If it ever shows up as a timing problem, the flag-based alternative is to let the loop
  print it.
- The real `sizeof(time_t)` on the device is taken from Claude's proof of the built `.word`; the host
  test reproduces the 32-bit arithmetic deliberately, since host `time_t` is 64 bits.

### E. Next step
Claude reviews the diff and flashes, turns Wi-Fi on and watches the serial log for the three lines
above; if `[ntp] reply captured: epoch accepted` appears and the clock sets, EST-391 is closed and the
Wi-Fi time path can be checked on the glass (status bar time, then a reboot to see it survive).

## 2026-11-11 (DS, EST-392): Settings > General: GPS becomes Position, with the GPS / Fixed choice in one place
Status: in_review (not committed, not pushed).

### A. What was there before (the ticket asks for both, so here they are)
- **General**: a section header `GPS` with four rows: `Satellites` (used/heard), `Fix` (3D / no fix),
  `Position` and `Share position`. The `Position` row was a READ-ONLY `Value` row and showed
  `om::posStatusToken()`, i.e. the token `gps` / `fixed` / `none` (EST-384 changed it from the raw
  fix to that token). It was not tappable at all.
- **EST-384's GPS/Fixed row** lived on a SEPARATE page: General > `Node` > a second `POSITION`
  section > a `Position` chevron row opened `SetPage::Position`, titled "Position", with two `Check`
  rows (`GPS` "live fix" / `Fixed` "picked on the map"), a `Change fixed position` button while Fixed
  was on, and a warning. Nothing else in the tree opened that page (`A_PosPage` had exactly one
  call site, and no tab, status cell or shortcut reached it).
- So Filip saw "only GPS in General" because the source control was two pages deep and the General
  row that carried the answer was inert. One clarification for the ticket's wording: the `GPS` thing
  on General is a SECTION HEADER (`setSection("GPS")`), not a row with a page, and there was no page
  titled "GPS" anywhere (grep: no `SetPage::Gps`, no `setTitle("GPS")`). The only position page was
  the one EST-384 added, and it was already titled "Position".

### B. The change
- **One control.** General's `POSITION` section (renamed from `GPS`) now starts with a `Position`
  CHEVRON row whose value is the source in use, `om::posSourceLabel()`: `GPS` or `Fixed`. Unlike the
  old token it does not need a fix: a node set to GPS indoors still reads `GPS`. A tap drops a small
  **two-row choice under the row** (the app's existing dropdown style, the one the chat selector
  uses: a panel with the app's own edge, the current option highlighted, the Check row's green dot
  on it): `GPS` (live fix) and `Fixed` (picked on the map).
- **GPS** selects the GPS source (same call and same toast as EST-384's Check row). **Fixed** opens
  EST-384's map picker EXACTLY as before (`ownPickOpen(om::posSource().sourceFixed())`: the current
  fix, else the map centre, else the saved point when it is already Fixed) and the picker's `Save`
  is still the only place the shell stores a Fixed point. Its `Cancel` still changes nothing.
- **Cancel**: a tap outside the panel (the row above it, Back, anywhere else) closes the choice and
  writes NOTHING: a node on Fixed keeps its saved point and a node on GPS stays on GPS.
- **The GPS details stay**: `Satellites`, `Fix` and `Share position` are unchanged under the row.
  (There is no separate "signal" row in General today; the one signal number is the status bar's
  `GPS n` cell, untouched, and the Map/diagnostics still read the GPS parser.)
- **One place.** EST-384's duplicate is gone: the Node page's `POSITION` section and its `Position`
  chevron row are removed and `Share position` is back where it was before EST-384 (under IDENTITY).
  `SetPage::Position`, the `A_PosPage` / `A_PosGps` / `A_PosFixed` / `A_PosChange` actions, the
  `Change fixed position` button and the warning paragraph are all deleted, so there is no second
  page and no second row to reach the source. The one door (the General row) and every tap rect come
  from `ui/settings_layout.h`.
- **The row moved to the top of its section** (one deliberate deviation, layout only): with
  `Satellites`/`Fix` above it, the page's floor at 320x240 left only 50 px under the row, so the
  60 px panel would have been cut by 10 px. First in the section, it drops DOWN with the details
  under it. `settingsChoiceLayoutFor()` also puts the panel ABOVE a row when a page's floor leaves no
  room below at all, so a longer page can never cut the choice off.
- **Every other door**: there was none (A, above). The status bar's GPS cell is not tappable (only
  the gear and the fault mark are), the Map and the RPTR picker never opened the Settings page, so
  nothing else needed rewiring. `g_posChoice` is cleared by `setGo`, `setBack`, `settingsBack`, a
  settings tab tap and `openSettings`, so the choice can never be left hanging on another page.
- Touches: while the choice is down it owns every tap (Back included), a drag does not scroll the
  page under it, and the pressed look is drawn on the option's own rect
  (`pressTargetForPositionChoice`); a tap outside is a consumed press with no look, like the chat
  dropdown's close.

### C. Files
- `src/ui/settings_layout.h`: the choice's pure half, one place: `PosChoiceAct` / `posChoiceAction()`
  (GPS -> Gps, Fixed -> OpenPicker, anything else -> Keep), the labels (`kPosSectionLabel`,
  `kPosRowLabel`, `posOptionLabel()`, `posOptionDesc()`), `SettingsChoiceLayout` +
  `settingsChoiceLayoutFor()` / `settingsPositionChoiceFor()` / `settingsChoiceOptionAt()` and
  `settingsChoiceScrollFor()` (the least lift that puts the whole panel on the glass). `SetPage`
  lost `Position`.
- `src/ui/screen_settings.{h,cpp}`: the view carries `posChoice` / `posChoiceRow` / `posFixed`; the
  drawing (`drawPositionChoice`, clipped to the body) and the hit test both call
  `settingsPositionChoiceFor()`.
- `src/ui/press_targets.h`: `pressTargetForPositionChoice()` (the option's rect, no target outside).
- `src/position_source.h` + `src/position.{h,cpp}`: `sourceLabel()` / `posSourceLabel()` ("GPS" or
  "Fixed", no coordinate).
- `src/main.cpp`: the General rows, the Node page, the one action id `A_PosSource`, the choice
  handler, the press target and the drag guard.

### D. Build and tests (real exit codes)
- `pio run -e om_lite_l2` (the REBUILD.md command): **exit 0**, SUCCESS. Baseline build of this tree
  before the change was RAM **160,168 B** of 327,680 and flash **2,985,721 B** of 6,553,600; after:
  RAM **160,168 B** (unchanged, 48.9 %) and flash **2,986,733 B** (**+1,012 B**, 45.6 %);
  `firmware.bin` **2,987,104 B**. Only the two known RadioLib `#warning`s; none from a changed file.
- `test/host/run.sh`: **exit 0**, **50 C++ binaries + 16 Python checks** (one new binary, one new
  Python check; `test_est384.py` updated, see E).
- New `test/host/test_est392.cpp` (built and run by run.sh, BIN50), over the firmware's own headers:
  the labels (POSITION / Position / GPS / Fixed and their descriptions, no digit, dot or em dash);
  the choice's rectangles at 320x240 and 800x480 (panel from the row's own bottom edge, two rows
  tiling it with no gap, on the glass at the scroll the shell opens it at, and the drop-up fallback
  when there is no room below); the scroll rule (never back up, exact lift, clamped, unset row);
  the hit test (both options at their corners and last pixel, one pixel below is the next option or
  outside, the row above / Back / the far corner are -1, every pixel of the panel is an option);
  the answers (`posChoiceAction`) plus a real `PositionSource`: Cancel after GPS keeps GPS and the
  fix, Cancel after Fixed keeps Fixed and the saved point (and a later fix cannot move it), choosing
  GPS switches, choosing Fixed stores the point, and `sourceLabel()` reads GPS/Fixed with no fix;
  and the pressed rect == the drawn option rect with no target outside the panel.
- New `test/host/test_est392.py` (the shell half): exactly one `POSITION` section and one
  `A_PosSource` row built from the header labels, the value is `posSourceLabel()`, the section order
  is Position then Satellites/Fix then Share position, the Node page has no source row, the removed
  ids and `SetPage::Position` / `Change fixed position` are nowhere, `ownPickOpen()` has one call
  site, `posSelectGps()` one and `posSelectFixed()` one (the picker's Save), the choice handler
  stores no point itself and formats no float, the labels live once in the header, and the drawing,
  the hit test and the pressed look all ask the one layout function.

### E. One older test edited (why)
- `test/host/test_est384.py` asserted the string `Change fixed position` was in `main.cpp`. EST-392
  removes that button on purpose (the picker reopens by choosing Fixed again), so the check now
  asserts the position row still exists and keeps EST-384's real rule (no coordinate, no
  `%.4f, %.4f` display). It is a test of EST-384's own rule, not of the removed string.

### F. Preview
- `docs/shots/2026-11-11-est392-position-choice.png` (2x), generated by the new `tools/mkpos.py`: a
  tiny C++ dumper builds the General page's real rows, asks `ui/settings_layout.h` for the real
  rectangles (tabs, title/back, every visible row at the scroll the shell opens at, the choice
  panel) and prints `ui/theme.h`'s real colours; Python only fills them. Left: the page with the
  choice closed, `Position  GPS >`, `Satellites 9 used, 14 heard`, `Fix 3D`. Right: the same page
  with the choice open, GPS highlighted with its dot, `Fixed  picked on the map` under it, the panel
  ending exactly at the body's floor. Invented counts only, no coordinates anywhere.

### G. Could not check (no device, no serial)
- The taps on the glass: the row to open the choice, GPS, Fixed, and a tap outside for Cancel.
  The preview is a layout drawing from the firmware's own rectangles, not a photo of the panel.
- Whether the two-row panel over the Satellites/Fix rows reads clearly at 1x, and whether the row
  first in the section is the order Filip wants (it is a layout choice, made so the panel drops
  DOWN; easy to move back if he prefers the details first).
- Switching to Fixed and back with a real sky (the host proves the source rule, not the GNSS).
- The scroll lift in use: the host proves the arithmetic (347 px at the top of the General page),
  the feel of the page moving by itself when the choice opens is unproven.

### H. Next step
Claude reviews the diff and the preview and flashes. On the glass: Settings > General > POSITION,
`Position` reads `GPS`, tap it (the page lifts, the choice drops under the row with GPS dotted), tap
Fixed (EST-384's picker on the fix or the map centre; Save -> the row reads `Fixed`, the Map shows
the pin), tap Position again -> Fixed, pick GPS, tap an empty spot to Cancel (the row must not
change), and check the Node page has no Position row and no second POSITION section. That is the
whole ticket.

## 2026-11-11 (DS, EST-393): map picker banner on the screen floor, the Map's cross instead of the dot
Status: in_review (not committed, not pushed).

### A. What was there before
- **The banner.** EST-374 made the map canvas the full map area (320 x 214, from y 26 to y 240), and
  the picker's `map` rectangle followed it. The banner did not: `rptrMapPickLayoutFor()` still placed
  it at `L.tabbar.y - 42`, a leftover from when the canvas stopped above the 25 px tab bar. On the L2
  that is y 173..215, so it hovered 25 px above the bottom edge with map showing under it (Filip:
  "hovering above the bottom edge").
- **The mark.** `drawMapPick()` drew its own crosshair: a 7 px radius ring, four 5 px ticks 14 px out
  and a 2 px gold centre dot, all in `kInk` / `kAcc`, around the centre of a 12 x 12 box at 154,127.
  The Map tab had already moved to EST-373's plain cross (two 1 px dark lines, 7 px, one ink, no
  outline), so the two screens disagreed.
- **The centre.** Both pickers already saved `mapScreenToLatLon(kMapCentreX, kMapCentreY)`, i.e. the
  pixel (160,133), and `kMapCentreY` is the canvas centre under the status bar. The drawing was the
  only thing off that point.

### B. The change
1. **Banner flush with the floor.** `rptrMapPickLayoutFor()` now places `m.panel` from `L.height`:
   `Rect{0, L.height - kRptrPickPanelH, L.width, kRptrPickPanelH}`, with the new
   `kRptrPickPanelH = 42` in `ui/rptr_layout.h`. On the L2 the banner is 0,198,320,42: full width,
   **bottom = 240**. `coords`, `useGps` and `apply` were already placed from `m.panel`, so they moved
   with it (keys now at y 218..238); the hit test and `pressTargetForRptrMapPick()` already read
   those same rectangles, so drawing and tapping moved together. The `tabbar.y - 42` expression is
   gone. At 800x480 the same rule puts the banner's bottom at 480.
2. **The Map's own cross.** The EST-373 drawing is now ONE helper, `drawMapCentreCross(Device*,
   Rect)` in `ui/map_chrome.h` (a template, so the header stays LovyanGFX-free; the screens pass
   their `lgfx::LovyanGFX*`, the host test passes a recorder). It paints exactly two `fillRect`
   strokes: `(cx - 3, cy, 7, 1)` and `(cx, cy - 3, 1, 7)` in `kMapCrossInk`, with no outline, plate
   or dot. `screen_map.cpp`'s `drawCentreCross()` now calls it with `mapCentreCrossRect(c)`, and
   `map_pick.cpp` calls it with `m.cross`; the ring/ticks/dot block is deleted. `mapCentreCrossAt()`
   is the one 7x7 box builder, used by `mapCentreCrossRect()` and by the picker's
   `m.cross = mapCentreCrossAt(kMapCentreX, kMapCentreY)`. On the L2 that is 157,130,7,7, centre
   **(160,133)**, the same rectangle the Map screen draws.
3. **The saved point is still under the cross.** No coordinate maths changed: the RPTR picker and
   the own-position picker both still call `om::mapScreenToLatLon(om::ui::kMapCentreX,
   om::ui::kMapCentreY, ...)`; `test_est393.py` checks both functions. The Q&A path that opens the
   picker also seeds the draft from the view's centre, so an untouched picker saves the point the
   cross marks.
- Both users of the picker (the RPTR Position page and Settings' Fixed position) share
  `rptrMapPickLayoutFor()` and `drawMapPick()`, so this one change fixes both doors, as the ticket
  asked.

### C. Files
- `src/ui/map_chrome.h`: `mapCentreCrossAt()`, `mapCentreCrossRect()` built from it, and the one
  `drawMapCentreCross()` template; the stale "the picker keeps its own crosshair" comment is fixed.
- `src/ui/screen_map.cpp`: `drawCentreCross()` is now one call to the shared helper.
- `src/ui/map_pick.cpp` + `src/ui/map_pick.h`: the picker calls `drawMapCentreCross(d, m.cross)`.
- `src/ui/rptr_layout.h` + `.cpp`: `kRptrPickPanelH`, the floor-placed panel, `Rect cross`, and the
  one `mapCentreCrossAt(kMapCentreX, kMapCentreY)`.
- Comments in `src/main.cpp`, `src/ui/press_targets.h` and `src/ui/screen_rptr.h` now say "centre
  point / cross" instead of "crosshair"; no code changed there.

### D. Build and tests (real exit codes)
- `pio run -e om_lite_l2` (the REBUILD.md command): **exit 0**, SUCCESS, RAM **160,168 B** of 327,680
  (48.9 %, unchanged), flash **2,986,657 B** of 6,553,600 (45.6 %; EST-392 was 2,986,733, so
  **-76 B**), `firmware.bin` **2,987,024 B** (was 2,987,104, **-80 B**). The build id is
  `3c21d6f-dirty`.
- `test/host/run.sh`: **exit 0**, **51 C++ binaries + 17 Python checks** (one new of each).
- New `test/host/test_est393.cpp`: the banner is 42 px, full width, bottom == 240 (and at 800x480
  bottom == 480); the coordinate line and keys are inside it; both keys are 20 px above the floor,
  never overlap, and their drawn rects are the same rects `rptrMapPickHitAt()` and
  `pressTargetForRptrMapPick()` return (a tap at the old key centre is plain map now); the picker's
  cross rectangle is exactly `mapCentreCrossRect(mapChromeFor(...))` (157,130,7,7, centre 160,133);
  the shared helper is called with a recorder and writes exactly two 1 px lines in `kMapCrossInk`
  (7 px across and 7 px down) with no dot/ring/outline, and the Map's own box produces identical
  operations; the cross is above the banner and that pixel hits `Map`.
- New `test/host/test_est393.py`: the picker draws the shared helper once and has no `crosshair`,
  `drawCircle`/`fillCircle` or raw `kMapCrossInk` stroke left; `screen_map.cpp` calls the same helper;
  only `map_chrome.h` contains a `fillRect(... kMapCrossInk)` stroke; `rptr_layout.cpp` places the
  banner from `L.height` (no `tabbar.y - 42`) and builds `m.cross` from `mapCentreCrossAt()`; and both
  shell centre functions still call `mapScreenToLatLon(kMapCentreX, kMapCentreY)`.
- One older test changed on purpose: `test_est366.cpp` asserted the picker's banner bottom was `<=
  L.tabbar.y` and its mark was a 12 x 12 crosshair box. EST-393 supersedes both (the ticket moves the
  banner to the floor and replaces the crosshair), so it now asserts bottom == `L.height` and the
  7 x 7 `cross`. `test_est374.cpp` and `test_est370.cpp` got the same field/box update (the centre
  checks themselves are unchanged).

### E. Preview
- `docs/shots/2026-11-11-est393-picker.png` (2x), generated by the new `tools/mkpick.py`: the
  Settings picker with a real rectangle dump from `ui/rptr_layout.h` and real colours from
  `ui/theme.h`. The banner is full width and ends on the screen floor, `Cancel` / `+` / `-` / `z15`
  are where the layout puts them, and the mark is the Map's 7 x 7 cross at (160,133). The banner's
  digits are the placeholder `00.00000, 00.00000`, never a coordinate. The photo-like backdrop is the
  existing z13 reference image's centre cropped to the 320 x 214 canvas (the picker's label still
  says z15); it is a backdrop for the layout, not a new device capture.

### F. Could not check (no device, no serial)
- The taps on the glass: Cancel, +, -, Use my GPS and Save, and a drag/tap on the map with the banner
  now over its old hovered strip.
- Whether the cross reads as well on the real z15 tiles as it does on the preview's z13 backdrop; the
  host proves the pixels and the EST-373 contrast measurement still applies to the same ink.
- That the point under the cross survives a real pan/zoom/Save round trip on the device (the host
  proves the rectangle and the shell's one centre call, not the map projection on glass).

### G. Next step
Claude reviews the diff and the preview and flashes. On the glass: Settings > General > POSITION,
choose Fixed (the banner must sit flush with the bottom edge, its Save hit rect where it is drawn),
check the cross at the map centre, drag the map, tap, Save, reopen the picker (the saved point must
be under the cross), then open the RPTR Position page's picker and check the same banner and cross.
That is the whole ticket.

## 2026-11-11 (DS, EST-394): traces stay on their repeaters when the map is panned or zoomed
Status: in_review (not committed, not pushed).

### A. What was there before
- **The pixels were fixed at arrival.** `traceEnqueue()` projected the route through
  `om::mapProject()` once, when the message arrived, and pushed those `TracePt` screen pixels into
  the queue. `TraceQueue`/`TraceAnim` then kept only pixels (and a pixel geometry). `build()` copied
  the frame as it was. So a pan, a zoom or the bars' slide moved the tiles and the markers but left
  the trace (route, dot, tail, direct-arrival ring) at the old screen coordinates. Filip's "traces
  are all screwed up if/when I move the map" is exactly this.
- **A second, smaller problem during a drag.** `pump()` draws its 30 fps animation frames with
  `mapRepush()`, the composited canvas. During a pan that canvas still shows the previous view, so
  even a correctly projected trace would have sat on old tiles for those frames. The drag's own
  `draw()` (at most every ~60 ms) was the only frame with fresh tiles, and the release did not force
  one, so the last part of a pan could stay undrawn.
- **The picker's zoom label.** `map_pick.cpp` drew `v.hint` ("z15") at `m.back.right() + 8`, on the
  Cancel key's own line, 8 px from its box. On the Settings picker the label was cramped against
  "Cancel" (Claude's EST-393 preview check).

### B. The change
1. **The route stays in micro-degrees.** `ui/map_trace.h` gained `TraceGeoPt {latE6, lonE6}` and a
   projector type `TraceProject`. `TraceQueue` and `TraceAnim` store `TraceGeoPt`; `push()`,
   `begin()`, `tick()` and `sample()` take them. `traceEnqueue()` now copies `route.latE6/lonE6`
   straight into the queue and never calls `mapProject()` (the direct-arrival ring is our own E6
   point). Nothing is thrown away when the pack is not open: the route stays live and draws nothing
   until a view exists.
2. **Every frame is projected in the current view.** `TraceAnim::sample()` projects the route once
   per sample (at most 12 `mapProject()` calls), builds the pixel geometry from those points, and
   derives the dot, tail and ring exactly as EST-368/372 did. The output `TraceFrame` is still
   pixels, so `screen_map.cpp`, the palette and the clipping are unchanged.
3. **Every MapView re-samples.** New `TraceQueue::sampleActive()` re-projects the RUNNING route
   without starting a pending one or resetting its clock. `map_tab.cpp`'s `build()` calls the new
   `traceSampleNow()` before copying `v.trace`, so the full draw, the animation frame, the bars'
   slide and the tap's hit-test view all carry a frame for the view under them. `pump()` passes
   `om::mapProject` to `tick()`.
4. **Drag choice: the trace is NOT skipped on the drag's own frames.** The 60 ms drag redraw already
   re-renders the tiles and every marker; the trace adds one projection of at most 12 points plus
   the same line/tail drawing it always had. Measured on the host: **2.25 us per 12-point sample**
   (a second run 3.32 us) and 3.32 us includes all 12 projections, the geometry and the tail slice.
   That is negligible next to EST-371's 19.8 ms canvas re-push / 31 ms frame, so the trace stays in
   the drag frames. What IS skipped are the 30 fps replay frames while `mapViewStale()` is true:
   they would re-push the pre-pan canvas under a trace projected for the new view. They resume on
   the next pass after a full `draw()` has refreshed the canvas.
5. **The release draws the final pan.** `main.cpp` now calls `om::map_tab::draw()` on the release of
   a map drag (`g_tab == 2`, not Settings, screen on, `g_drag`), so the final pan and its trace are
   on the glass even if the last 60 ms were not, and the replay has a matching canvas to resume on.
6. **The zoom label has its own rectangle.** `RptrMapPickLayout` gained `Rect hint`, placed UNDER
   the back key (`m.back.y + m.back.h + 6`, left-aligned to it, 60 x 14); `map_pick.cpp` draws
   `v.hint` from `m.hint` in the same MonoMeta face and `kDim` ink. No "Cancel" width can reach it.

### C. Files
- `src/ui/map_trace.h` + `.cpp`: `TraceGeoPt`, `TraceProject`, geo queue/animation, per-sample
  projection, `TraceQueue::sampleActive()`.
- `src/screens/map_tab.cpp`: `traceEnqueue()` queues E6, `traceSampleNow()` in `build()`, `tick()`
  takes `mapProject`, replay frames guarded by a matching canvas.
- `src/main.cpp`: one full draw on the release of a map drag.
- `src/ui/rptr_layout.h` + `.cpp`: `Rect hint` under the back key.
- `src/ui/map_pick.cpp`: the zoom label is drawn from `m.hint`.
- `test/host/test_est368.cpp`: its animation/queue cases now pass the flat test view (same pixel
  expectations); `test/host/test_est394.cpp` + `test_est394.py` are new; `test/host/run.sh` runs them.

### D. Build and tests (real exit codes)
- `pio run -e om_lite_l2` (the REBUILD.md command): **exit 0**, SUCCESS. RAM **160,304 B** of
  327,680 (48.9 %; EST-393 was 160,168, **+136 B**), flash **2,986,889 B** of 6,553,600 (45.6 %;
  EST-393 was 2,986,657, **+232 B**), `firmware.bin` **2,987,248 B** (was 2,987,024, **+224 B**). The
  build id is `fe865cd-dirty`.
- `test/host/run.sh`: **exit 0**, **52 C++ binaries + 18 Python checks** (one new of each).
- New `test_est394.cpp`: one 4-point route sampled at mid-route in view A and in view B (pan
  +40/-25 px). Every route point, the dot and every tail vertex in B equals view B's projection of
  the same lat/lon, and exactly A's pixel plus (40, -25). At one zoom level in, every point equals
  the new projection, the polyline scales about (160,133), and the dot is recomputed on the new
  geometry at the same route fraction. `sampleActive()` re-projects without restarting the clock
  (the dot moves by the pan, it does not jump back to the start); a direct ring follows too. With
  the pack not open the frame is live with no points and the route is not lost. The 12-point cost
  is measured and printed. The last section checks the picker's `hint` rect at 320x240 and 800x480:
  under the back key with a gap, no overlap with back / +/- / cross / panel, inside the map.
- New `test_est394.py`: the model stores `TraceGeoPt` and takes a projector; `traceEnqueue()` has no
  `om::mapProject()` call; `build()` re-samples before copying `v.trace`; `pump()` hands
  `mapProject` to `tick()` and skips replay frames over a stale canvas; `main.cpp` draws on release;
  `screen_map.cpp` still draws the pixel frame; `map_pick.cpp` draws `m.hint` and no longer uses
  `m.back.right() + 8`.

### E. Could not check (no device, no serial)
- The glass: pan and zoom a live trace and watch route, dot, tail and ring ride the repeaters; the
  drag feel and the one extra full frame on release; the 30 fps replay resuming after a pan.
- The device-side cost of the drag frame with the trace kept (the host number is x86, not the
  ESP32-S3; the projection chain is 12 `log/tan` calls and the drawing is unchanged from EST-372).
- The picker label under Cancel on the real panel (the rects are proven; the font is the same).
- No new screenshot was generated: `tools/mkpick.py` is the EST-393 artifact and still draws the old
  label position. Claude can capture the picker on the glass if a shot is wanted.

### F. Next step
Claude reviews the diff, flashes, and on the glass: open the Map with Traces on, let a route play,
pan during it (the trace must stay on the same repeaters), zoom + / - during it (same), tuck and
restore the bars, then let go (the final pan and the trace must be drawn at once). Then Settings >
General > POSITION > Fixed: "z15" must sit under Cancel, clear of it.

## EST-395 sweep: core and system

Read-only sweep of the core and system code, DeepSeek (Paperclip worker, 2026-11-11). **No code was changed and
nothing was committed.** Screens and UI are the next ticket; a UI file appears here only where a core finding
reached into it (map.cpp's log line).

**Counts: 14 findings: S1 0, S2 5, S3 3, S4 2, S5 4.** Ranked most severe first. Every finding quotes the real
line; "not verified" marks what I inferred instead of measuring.

### 1. S2 · timekeeper.h:41 + timekeeper.cpp:289 · the clock's sanity gate admits epochs that a 32-bit `time_t` cannot hold
The gate accepts anything below 2100, but on this toolchain `time_t` is 32 bits and signed, so the top of that
window is a negative number, and it is handed straight to `settimeofday()`.
```
timekeeper.h:41     constexpr uint32_t kMaxSaneEpoch = 4102444800UL;   // 2100-01-01 00:00:00 UTC
timekeeper.h:73-74  constexpr bool saneEpoch(uint32_t epoch) { return epoch >= kMinSaneEpoch && epoch < kMaxSaneEpoch; }
timekeeper.cpp:289    tv.tv_sec = (time_t)epoch;
```
Trigger: one NTP/GPS/phone time source returns anything from 2038-01-19 onward (a bad packet, a 2100-ish date from
the L76K before its first fix, a stray value in NVS read as the floor). `saneEpoch()` passes it, `(time_t)epoch` is
negative, `settimeofday` sets the system clock into 1901, and every stamp written afterwards (history, battery log,
message ages) is wrong until the next restart. This is EST-391's root cause in a second place: the fix there was
applied to the comparison, not to the bound that feeds the cast. Fix: bound `kMaxSaneEpoch` at `INT32_MAX`
(2038-01-19 03:14:07 UTC) and add a host test that `saneEpoch` rejects the first value `(time_t)` turns negative.
Effort S.

### 2. S2 · board.cpp:729 · two `Wire.read()` calls in one expression: the ADC bytes can arrive swapped
```c
value = (uint16_t)((Wire.read() << 8) | Wire.read());
```
The order in which the two `Wire.read()` calls run is unspecified in C++ (any evaluation order, and the operands of
`|` are unsequenced). If the compiler picks right-to-left, the ADS1115's high and low byte are swapped and the
battery reads a garbled milli-voltage. GCC happens to order these calls left to right today, so the device looks
correct, but nothing in the code or a test pins it and a toolchain change or `-O2` reordering can flip it.
Trigger: a rebuild with a different optimisation or GCC version; the symptom is a battery percentage that jumps
wildly, hardest to diagnose precisely because it looks like an ADC problem. Fix: read into two locals, then combine.
Effort S.

### 3. S2 · board.cpp:67-77 · a half-failed expander latch rolls the shadow copy back, leaving it disagreeing with the chip
```c
bool latch() {
  return writeRegisters(0x02, s_output, sizeof(s_output)) &&
         writeRegisters(0x06, s_config, sizeof(s_config));
}
...
board.cpp:74-76    if (latch()) return true;
                   s_output[0] = o0; s_output[1] = o1; s_config[0] = c0; s_config[1] = c1;
```
`&&` short-circuits: when the FIRST write succeeds and the SECOND (config, 0x06) fails, the PCA9554's output
register has already taken the new levels, but the function reports failure and restores the old shadow. From then on
`s_output` is a lie; every later `stageOutput()` + `latch()` recomputes from it, and `gnssResetAsserted()`,
`gpsRailOn()` and `sdPresent()` all read that shadow rather than the hardware. One transient I2C error (the ticket's
own 2026-11-09 touch failure was in this function) can silently leave a rail in a state the firmware believes is the
opposite, and the boot-time GPS reset decision reads exactly these bits. Fix: only roll back when the first write
failed; when the second fails, re-read 0x02/0x06 from the chip into the shadow and log that one line. Effort S.

### 4. S2 · board.cpp:998 · the GNSS fix flag never expires, so a receiver that stops talking keeps advertising the old point
```c
out.valid = atoi(fields[6]) > 0;
```
`valid` is only ever written when a `$..GGA` line arrives. While the module talks this is fine (a lost fix arrives as
quality 0 within a second), but if the UART goes quiet, or its RX pin fails as in the EST-383 pin-swap episode, the
last `valid = true` and the last `lat/lon` stand for ever. The node then keeps advertising a position it no longer
has and the screens keep showing "Fixed". Evidence that nothing else clears it: `GpsState` (gps_sky.h:88-99) has no
timestamp field at all. Trigger: GPS antenna unplugged, UART wedged, or a Jamming-style hot start; the position on
the map freezes at the last good value and nobody can tell it from a live fix. Fix: stamp `lastFixMs` in `GpsState`
and clear `valid` in `gpsPoll()` after e.g. 5 s without a GGA, or gate every `valid` read on a fresh stamp. Effort S.

### 5. S2 · history_store.cpp:83-90 · an exhausted file pool returns NULL and is counted nowhere
```c
HistoryFile* openAppend(const char* path) override {
  for (int i = 0; i < kFiles; ++i) {
    if (files_[i].busy()) continue;
    return files_[i].begin(fs_, path, true) ? &files_[i] : nullptr;
  }
  return nullptr;
}
```
The budget in sd_budget.h is now correct and static_asserted on both sides (see "clean" below), so the pool running
dry should be rare, but when it does happen the store gets a NULL handle and the EST-380 lesson repeats: no counter,
no line, `hist_dropped` stays 0 while messages are lost. Same shape for a card error inside `begin()`. Not verified
whether `HistoryStore` counts a NULL append handle (I did not read history_log.h); if it does, this downgrades to S5.
Fix: one `openFailures` counter on the volume, reported in the `history` status line. Effort S.

### 6. S3 · map.cpp:520 · the map log prints the position to five decimals, i.e. to about a metre
```c
Serial.printf("[map] z%d%s %.5f,%.5f -> %d tiles\n", s_z, mag ? " (2x)" : "", s_cLat, s_cLon, drawn);
```
Rule 9 (REBUILD.md: the repo is public) allows no exact positions anywhere, and this line puts the node's real
centre into every serial log and from there into whatever Claude pastes into a ticket or PROGRESS.md. It is written
on every map open and every zoom. Fix: drop the pair, or print `%.1f` as rule 9 allows; if the number is needed to
debug tiles, print the tile indices instead of the degrees (which the same line could carry instead). Effort S.
Same line family: `[map] overlay z%d opened` (map.cpp:222) is safe.

### 7. S3 · mesh_link.cpp:561, 884, 2530, 2553 · message texts, DM texts and node names go into the log
```c
mesh_link.cpp:561   Serial.printf("[mc] DM from %s: %s (hops %d snr %.2f)\n", from.name, text ? text : "", ...);
mesh_link.cpp:884   Serial.printf("[mc] channel %d: %s (hops %d snr %.2f)%s\n", channelIdx, s_lastText, ...);
mesh_link.cpp:2530  Serial.printf("[ble] phone sent ch%d: %s\n", ui, m.text);
mesh_link.cpp:2553  Serial.printf("[ble] phone sent a DM to %s: %s\n", ci->name, m.text);
```
Four receive and send paths log the full body, and one also logs the sender's name. Rule 9 forbids "other people's
message texts or names from real chats" in logs, and the log is the file that gets quoted into the public PROGRESS.md
(and into tickets) whenever something is debugged. Trigger: any real chat while a serial capture runs, which is the
normal way this project debugs. Fix: log the length and the channel/DM seq only, e.g. `[mc] channel %d: %u B
(hops %d snr %.2f)`; keep the text in RAM where it already is. Effort S for all four.

### 8. S3 · wifi_link.cpp:808 (also 799, 801, 838, 994) · the SSID and the LAN address are printed
```c
Serial.printf("[wifi] connected to \"%s\" ip=%s rssi=%d internal=%u largest=%u\n", ssid, ipText, ...);
```
Rule 9 names SSIDs and LAN addresses explicitly. The connect line prints both, and the save/forget lines print the
SSID. Trigger: every Wi-Fi use, which EST-389's https probe makes routine. Fix: print the network's index in the saved
list (or a hash prefix) and the DHCP success as a boolean; `[ntp] dns=%d main=%s ... (addresses not printed)` at line
204 already shows the house style, so the connect line should follow it. Effort S.

### 9. S4 · board.cpp:179 · about 700 ms of hard `delay()` before anything else in boot
```c
delay(500);   // (plus 40 + 10 + 50 + 20 + 10 + 60 in the same function, all blocking)
```
`expanderBegin()` alone blocks ~700 ms waiting for the panel's reset/SLPOUT timing (500 + 60 + 40) and the SD card
power (50). None of it needs the loop task: SPIFFS/NVS/identity loading, the map pack open and the history store
could run inside that window, which is what the boot log's own `[om] boot ... ms` stamps would show. Estimated gain
300-500 ms of the visible boot (the 500 is the largest single block and the panel only needs it before the first
pixel, not before NVS). Not verified how much of the rest of boot is already overlapped with it. Fix: split the
function into "rails on" and "panel reset/release", and run the non-panel half of boot in between. Effort M.

### 10. S4 · audio.cpp:185-191 · the sound buffer's fallback lands 48 KB in internal RAM
```c
s_buf = (int16_t*)ps_malloc((size_t)sound::kMaxSamples * sizeof(int16_t));
if (s_buf == nullptr) { s_buf = (int16_t*)malloc((size_t)sound::kMaxSamples * sizeof(int16_t)); }
```
Deliberate and logged, but with the measured internal-RAM budget in this ticket (boot ~162 KB, BLE ~65 KB, Wi-Fi
~22 KB, TLS ~58 KB, and the loop stack 16 KB) a 48 KB internal allocation is the single largest user and would push
a Wi-Fi + BLE + TLS peak past the edge. Trigger: PSRAM refused once (a boot glitch, not a rare board) and then the
first https probe during a BLE session. Fix: when PSRAM is absent, cut `kMaxSamples` to a quarter and log the shorter
phrase, rather than take internal RAM for sound that nobody needs at full length. Effort S.

### 11. S5 · board.cpp:132 and 162/185 · dead staging line and a duplicated assignment
```c
board.cpp:132  stageInput(kBitLcdControl);       // overridden 30 lines later by setOutput(kBitLcdControl, true)
board.cpp:162  s_expanderReady = true;           // assigned again at 185, before the only console line that uses it
```
`stageOutput()` clears the config bit, so staging LCD control as an input first is a no-op that reads as intent. The
double `s_expanderReady = true` is harmless but hides which one matters (162 is the one that makes the pulses work).
Fix: delete both. Effort S.

### 12. S5 · board.cpp:748-754 vs 729 · the battery scale exists twice
```c
adsPinMilliVolts():  const float volts = (float)(int16_t)raw * 4.096f / 32767.0f;  ... return mv <= 0.0f ? 0 : (uint16_t)mv;
batteryMilliVolts(): const float volts = (float)(int16_t)raw * 4.096f / 32767.0f;  return (uint16_t)(volts * 2.0f * 1000.0f);
```
Two copies of the same ADC constant, one of them clamped at 0 and the other not, and the comment above them states
the PGA field is 000 (+/-6.144 V) while the divisor is the 4.096 V one. The device's readings were accepted as
correct, so I am not calling the number wrong ("not verified" either way), but any future calibration has to be made
in two places and one of them will be missed. Fix: `batteryMilliVolts() { uint16_t raw; if (!adsReadChannel(0,&raw) ||
!raw) return 0; return (uint16_t)(adsPinMilliVolts(raw) * 2u); }`. Effort S.

### 13. S5 · board.cpp:1027 · the NMEA line buffer drops the overflow silently
```c
if (s_nmeaLen < sizeof(s_nmea) - 1) s_nmea[s_nmeaLen++] = c;
```
No `else`, no counter. A longer-than-127-byte line (or, with the CASIC reader off, a run of binary bytes that never
contains `\n`) is truncated and then parsed as a sentence, fails its checksum and lands in `checksumBad` where it
cannot be told from radio noise. Bounded and self-recovering, so not a crash; it only costs diagnosis. Fix: count
`out.lineOverflow` in the `else`, and print it in the `gps` status line. Effort S.

### 14. S5 · audio.cpp:210-215 · `codecMute()` writes 0 into the codec when its read fails
```c
if (Wire.endTransmission(false) == 0 && Wire.requestFrom((int)kAddr, 1) == 1) v = (uint8_t)Wire.read();
v = (uint8_t)(v & 0x9F);
w(0x31, mute ? (uint8_t)(v | 0x60) : v);
```
If the register read fails, `v` stays 0 and the write clears every other bit of 0x31 with no line in the log. The
mute bit ends up right and the rest of the register is silently reset, which is the class this ticket hunts (a failed
read presented as a value). Trigger: one I2C error during standby, which `audioStandby()` calls on every idle
transition. Fix: on a failed read, write only the two mute bits from a cached copy of the register, or skip the write
and count it. Effort S.

### Clean (checked, nothing found)
- **SD open-file budget (EST-380): consistent.** `history_store.cpp:117-118` pins the pool to the budget
  (`static constexpr int kFiles = sd_budget::kHistoryPoolFiles; static_assert(kFiles == 4, ...)`) and map.cpp holds
  exactly the counted five handles (main pack 346, two overlay idx/pack pairs 202/214). The `[sd] open-file budget`
  line prints the sum at boot.
- **crash_report.cpp: clean.** Boot checkpoint, coredump check, the erase-only-after-a-good-summary rule and the
  `cs.task[16]` copy into `char task[17]` are all bounded correctly; the only ignored return is
  `esp_core_dump_image_erase()`, whose failure only means the same dump is re-read next boot.
- **gps_boot.h: clean and well guarded.** The reset table is the right shape and `board.cpp:88-99` static_asserts
  the SDK's numbers onto it, so an SDK renumbering fails the build instead of silently changing the rule.
- **I2C from a second task: not a fault today.** Only `board.cpp` and `audio.cpp` touch `Wire`, and `audio.cpp`'s I2C
  calls (`audioBegin`, `audioVolume`, `codecMute`, `audioStandby`) are all loop-context; the `om_audio` task does
  render + `i2s_write` only (audio.cpp:147-180). So there is no bus race despite there being no mutex. If any future
  ticket moves a codec call into the task, that becomes an S1.

### Coverage
**Read in full:** `board.cpp` (all 1061 lines), `sd_budget.h`, `crash_report.cpp`, `audio.cpp:120-282`. **Read in
part:** `board.h:1-120`, `timekeeper.cpp:225-300`, `gps_boot.h:1-60`, `gps_sky.h:88-129`, `wifi_link.cpp:560-760`,
`history_store.cpp:1-129`, `power_log.cpp:1-60`, `REBUILD.md`. **Read only through targeted grep (not a full read,
so absences there are not evidence of a clean file):** `mesh_link.cpp`, `map.cpp`, `timekeeper.h`, `ntp_plan.h`.
**Not reached at all (left for the next run):** `main.cpp` setup()/loop() orchestration, `usb_commands.cpp`,
`usb_cmd_logic.cpp`, `ota.cpp`, `mesh_link.cpp` body, `target.cpp/.h`, `ble_link.cpp`, `ntp_sleep.h`,
`https_probe.h`, `https_probe_ca.h`, `radio.cpp`, `radio_diag.cpp`, `radio_stats.cpp`, `position.cpp/.h`,
`position_source.h`, `power_log.cpp` body, `history/history_log.h`, `sound_synth.cpp`, `timekeeper.cpp` body,
`gps_raw.cpp`, `gps_aid.h`, `gps_mode.h`, `wifi_link.cpp` body outside 560-760.
**Not done:** the HTTPS probe's ~200 B per-probe loss. I read the whole `httpsExchange()` path (wifi_link.cpp:584-720)
and every exit deletes the `WiFiClientSecure`; I found no leak there, so the loss is either inside mbedTLS/lwIP
(TIME_WAIT, a cached session) or outside this function, and it needs a read of `httpsFinish()` plus a device
measurement to settle. Say "not verified", not "leak fixed".

### Next step
S1-free this time, but #1 and #2 are one-line crashes-in-waiting and #3 can leave a rail inverted; Claude decides
which of them to ticket first. The privacy family (#6-#8) is a single small commit and would make the log safe to
paste. Everything above #9 is a few lines each; #9 and the not-reached list need a second 30-minute run.

## EST-396 sweep: UI

Read-only sweep of the UI layer, DeepSeek (Paperclip worker, 2026-11-11). **No code was changed and nothing was
committed.** Scope: `src/ui/` (screens, map chrome/pick, notice panel, toast, widgets, theme, layouts, fonts, icons,
keyboard, chat layout/selector, settings layout), `src/screens/` and `main.cpp`'s tap, drag, settings, keyboard and
pop-up handlers. The core was swept in EST-395 and is only mentioned here where a UI file reached into it.

**Counts: 8 findings: S1 0, S2 0, S3 2, S4 0, S5 6.** Ranked most severe first. Every finding quotes the real line;
"not verified" marks what I inferred instead of measured. **The UI layer came out much cleaner than the core did:**
the two S3s are log lines, and everything else is a constant or a dead line. I found no crash, no data loss, no
wrong behaviour proven, and no memory or speed problem in this layer.

### 1. S3 · main.cpp:1359 and main.cpp:5246 · a node's exact position and name go into the serial log
```c
main.cpp:1359    Serial.printf("[chat] map %s %.6f,%.6f\n", m.name, lat, lon);
main.cpp:5246    Serial.printf("[nodes] map %s %.6f,%.6f\n", name, lat, lon);
```
Rule 9 (REBUILD.md: the repo is public) allows no exact positions anywhere, and these two lines print another node's
real coordinates to six decimals (about 0.1 m) plus its name, every time someone taps INFO > Show on map or a node's
sheet > Show on map. Trigger: one tap on either row for a node that has a position. This is the same family as
EST-395 #6 (`map.cpp:520`); these two are the UI handlers that feed it, so fixing map.cpp alone leaves the position
in the log from the tap side. Fix: drop the numbers, `Serial.printf("[chat] map %s (position)\n", m.name)`, or one
decimal at most. Effort S.

### 2. S3 · main.cpp:1172, 1372, 1375 · the message text itself is printed on send and on resend
```c
main.cpp:1172    Serial.printf("[mc] UI DM send: %s '%s'\n", sent ? "ok" : "failed", g_kbText);
main.cpp:1372    Serial.printf("[chat] DM resend %s '%s'\n", sent ? "ok" : "failed", m.text);
main.cpp:1375    Serial.printf("[chat] resend %s '%s'\n", sent ? "ok" : "failed", m.text);
```
Same log family as EST-395 #7, in the UI paths that were not covered there. Being precise about severity: rule 9's
wording bans *other people's* message texts, and these are the user's own draft and own sent message, so this is a
lesser case than #7. It still puts chat content into the one artefact that gets pasted into tickets. Not verified
whether a clip is needed for the receiver's text elsewhere; I only checked these three. Fix: log the byte count and
the verdict, not the text. Effort S.

### 3. S5 · screen_nodes.cpp:38 · the sheet row reserves a hard-coded 70 px and puts the value on the panel edge
```c
drawTextEllipsis(d, FontRole::SansBody, kInk, kSheet, r.x, cy, r.w - 70, label);
if (value != nullptr && value[0] != 0) {
  drawTextRight(d, FontRole::MonoMeta, value[0] == 'o' && value[1] == 'n' ? kAcc : kDim, kSheet,
                r.right(), cy, value);
```
Two problems in one row: `70` is a pixel number that belongs in the layout (the ticket's own class), and `r.right()`
puts the value flush against the panel's edge while every other sheet row keeps `kSheetPadX`/`kSheetGap`. The label
is cut at `r.w - 70` whether or not the value needs that room, so a long repeater name in the trace sheet is
ellipsised earlier than necessary even when the value is short ("SNR 3.2" is about half of 70 px). The same function
also decides colour by testing the string (`value[0] == 'o' && value[1] == 'n'`), so any future value starting with
"on" is drawn as an online state. Trigger: a trace hop whose node name is long, or any row whose value begins "on".
Fix: `kNodeSheetValueW` in theme.h, right-align at `r.right() - kSheetPadX`, and pass an "is-online" flag instead of
sniffing the text. Effort S.

### 4. S5 · screen_map.cpp:466-469 with map_chrome.cpp:70 · the callout's two text rows are literals, its box is a theme constant
```c
map_chrome.cpp:70   c.callout = Rect{8, (int16_t)(barTop - 8 - kMapCalloutH), (int16_t)(w - 16), kMapCalloutH};
screen_map.cpp:467  drawTextEllipsis(d, FontRole::SansBody, kAccInk, kSelBg, c.callout.x + 7, c.callout.y + 12, c.callout.w - 14, v.calloutTitle);
screen_map.cpp:468  drawTextEllipsis(d, FontRole::MonoMeta, kDim, kSelBg, c.callout.x + 7, c.callout.y + 28, c.callout.w - 14, v.calloutLine);
```
The box's height comes from `kMapCalloutH` (40) but the two baselines (12 and 28) and the padding (7, 14) do not, and
the box's x/width use `8`/`16` where `kMapMargin` (6) exists. It looks right today; it stops being right the moment
someone changes the callout height, and nothing in the build or a host test would say so. Trigger: any change to
`kMapCalloutH` or to kMapMargin. Fix: `kMapCalloutPadX`, `kMapCalloutLine1Cy`, `kMapCalloutLine2Cy` in
map_chrome.h, and `kMapMargin` for the box's inset. Effort S.

### 5. S5 · main.cpp:5546 · a dead variable is the old coordinate hit test, left standing in front of the new one
```c
static void handleTap(int x, int y, bool longPress) {
  Serial.printf("[tap] %d,%d\n", x, y);
  om::sounds::playClick();
  const bool topRight = (y < 40 && x >= 260);
```
`topRight` is never read anywhere in the tree (checked with ripgrep for the identifier: this line only). It is exactly
the "stale branch left in front of the new one" pattern that bit three times in S492, in its smallest form: a
coordinate rule that no longer decides anything. Harmless at run time, but the next person adding a top-right tap
will find it and may trust it. Fix: delete the line (and, if the tap log is kept, keep it, it is screen coordinates,
not a position). Effort S.

### 6. S5 · widgets.cpp:387-401 · the one-line toast has no ellipsis and no clip, so a long line runs off its plate
```c
const Rect box{(int16_t)kToastMargin, (int16_t)(L.tabbar.y - kToastMargin - kToastH),
               (int16_t)(L.width - 2 * kToastMargin), kToastH};
...
setRole(d, FontRole::SansBody);
d->setTextDatum(lgfx::textdatum_t::middle_center);
d->setTextColor(kInk, kPanel2);
d->drawString(text, box.x + box.w / 2, box.y + box.h / 2);
```
This is the one text path in the UI that neither ellipsises nor clips: `drawString` centred on the plate draws past
both rounded ends onto whatever is behind, and `ToastState::text` holds up to 63 bytes without a guard
(`toast.h:67 snprintf(text, sizeof(text), "%s", t);`). The box is 304 px wide, about 48 characters at 11 px sans.
**Not verified** that a current single-line caller exceeds that: the longest I found in the tap handlers is
`"History not saved: card error, 4294967295 dropped"` (main.cpp:5704), which is 48 characters and therefore exactly on
the edge, so this is one long count away from showing. Fix: `drawTextEllipsis(d, FontRole::SansBody, kInk, kPanel2,
box.x + kToastMargin, box.y + box.h / 2, box.w - 2 * kToastMargin, text)`, or clip to the box. Effort S.

### 7. S5 · screen_chat.cpp:520 and screen_nodes.cpp:56 · the keyboard sheet's corner radius is used as another sheet's radius
```c
screen_chat.cpp:520    d->fillRoundRect(g.panel.x, g.panel.y, g.panel.w, (int16_t)(g.panel.h + kOskRadius), kOskRadius, kSheet);
screen_nodes.cpp:56    d->fillRoundRect(g.panel.x, g.panel.y, g.panel.w, (int16_t)(g.panel.h + kOskRadius), kOskRadius, kSheet);
```
Both are message/action sheets, not keyboard sheets: they use `kOskRadius` to round their top corners and to overflow
the bottom edge deliberately. `kRadiusLg` exists for exactly this. Trigger: retune the on-screen keyboard's corners
and the chat sheet and the Nodes sheet silently follow. Fix: `kSheetRadius` (or `kRadiusLg`) in theme.h. Effort S.

### 8. S5 · screens/toasts.cpp:80-98 · the decision is logged before the show can still be refused, so the log can say `show` for nothing
```c
const ui::NoticeDecision dec = ui::noticeDecide(kind, title, s_screenOn, dnd, chatOpen, busy);
logDecision(kindToken, dec);
...
case ui::NoticeDelivery::Shown:
default:
  if (!ui::toastState().showNotice(title, body, millis(), ui::kToastMs)) {
    return ui::NoticeDelivery::Skipped;
```
`logDecision()` prints `[notice] show kind=...` first; if `showNotice()` then refuses (empty title, zero duration) the
function returns Skipped **after** the log has already claimed the show. This matters more than it looks: that
`[notice]` line is the instrument EST-381 added to settle the Map pop-up question, and an instrument that can report
a show that never happened is the exact failure mode this ticket hunts (a write whose failure is not surfaced).
**Not verified** that a caller can reach it: `noticeDecide` refuses an empty title first, in which case this branch
is unreachable dead code rather than a lying log — both want the same one-line fix. Fix: log after the show succeeds
(or log the refusal with its own token). Effort S.

### Clean (checked, nothing found)
- **One rectangle drives the draw and the tap: holding in this layer.** `ui/press_targets.h` asks each screen's own
  hit test and returns that rectangle (`t.rect = L.rowVisible(row, scroll);`, `t.rect = g.keyHit[...]`), the chat's
  message heights come from one `chatMsgHeights()` used by both the draw and the hit test (screen_chat.cpp:120), and
  the Map's tabs come from one `mapTabRects()` list (`screen_map.cpp:437`: "drawn from the SAME list the hit test
  reads"). I checked the four screens' row/key/selector rects and found no second set of numbers.
- **Frame buffer: all six screens go through it, and every early exit closes the frame.**
  `screen_chat.cpp:539`, `screen_settings.cpp:389`, `screen_rptr.cpp:418`, `screen_map.cpp:348`,
  `screen_nodes.cpp:82`, `screen_node_info.cpp:23` all open `omFrameBegin()` and the failure branch is
  `omFrameEnd(); return;`. No `pushSprite` exists anywhere outside board.cpp.
- **Nothing in the UI writes to SD, NVS or the radio from a draw or a tap** (grep for those calls in `src/ui/` and
  `src/screens/`: none), so the EST-348/350 rule holds here; the heavy work is deferred to the loop (`chatViewStep`,
  `map_tab::pump`).
- **Em dashes: only in comments**, never in a drawn string (`screen_settings.h:56`, `main.cpp:4782`). The screen
  strings I read use commas and colons.
- **Ages where the rule wants times: clean.** Every stamp I read goes through `ui/age.cpp::stampText`, which returns
  the clock time first and the age only as the documented fallback (`drawListRow` shows `row.age`, which is already a
  formatted stamp; the chat line and the selector use the Clock form; the sheet header uses DayTime).
- **millis() arithmetic in the UI is wrap-safe**: the three UI sites use the `(uint32_t)(millis() - start)` form
  (screens/toasts.cpp:142, screens/map_tab.cpp:345), and `ToastState::visible()` compares with an `int32_t`
  difference (toast.h:48).

### Coverage
**Read in full:** `ui/theme.h`, `ui/toast.h`, `ui/age.cpp`, `ui/notice_panel.cpp`, `ui/screen_nodes.cpp`,
`screens/toasts.cpp`, `ui/press_targets.h:1-120`, `ui/widgets.cpp` (in pieces, ~all of it). **Read in part:**
`ui/screen_chat.cpp:1-120, 190-260, 450-545`, `ui/map_chrome.cpp:40-115`, `ui/map_chrome.h` (the map constants),
`ui/screen_map.cpp:400-475`, `ui/press.h`, `main.cpp:1395-1415, 1416-1560, 4039-4075, 4490-4530, 4625-4660,
4739-4870, 4960-5010, 5180-5232, 5238-5252, 5320-5400, 5543-5860, 6280-6420` (the gesture press/release path),
`docs/REBUILD.md`.
**Read only through targeted grep (so absences there are NOT evidence of a clean file):** `ui/screen_settings.cpp`,
`ui/screen_rptr.cpp`, `ui/settings_layout.h`, `ui/rptr_layout.cpp`, `ui/chat_layout.cpp`,
`ui/chat_layout.h`, `ui/chat_selector.cpp`, `ui/text_wrap.cpp`, `ui/layout.h`, `ui/keyboard_draw.h`, `ui/node_menu.*`,
`ui/message_sheet.cpp`, `ui/emoji_picker.cpp`, `ui/compose_text.h`, `ui/notify.h`, `screens/map_tab.cpp`,
`screens/sound_events.cpp`, `ui/icons.cpp`, `ui/fonts.cpp`.
**Not reached at all:** `ui/map_pick.cpp`, `ui/map_trace.cpp`, `ui/press_feedback.cpp`, `ui/screen_node_info.cpp`
(body), `ui/screen_settings.cpp` body, `ui/screen_rptr.cpp` body, `ui/emoji*.cpp` bodies, `ui/font_*.cpp` (blobs),
`ui/dm_delivery.h`, `ui/dm_ring.h`, `ui/long_press.h`, `ui/status_cells.h`, `ui/new_node.h`, `ui/node_info.h`,
`ui/map_layers.h`, `ui/settings_prefs.h`, `ui/badge_font.h`, `ui/rgb565.h`, `main.cpp` 9000-line remainder
(compose/send paths 1100-1400, RPTR pumps 1700-3100, chat view build 600-900, the USB handlers).
**Not done, deliberately:** the 30-minute cap. I worked severity first and stopped with the two S3s and the constants
above; a second run should read the four screens I only grepped (Settings, RPTR, chat_layout and text_wrap) with the
same "drawn rect == tapped rect" and "text can overflow" lens, because those are the largest files in scope and the
place a real S2 would most likely hide. There was no build or host-test run: this ticket changes no code.

### Next step
The two S3s are one small commit and would make the tap-path log safe to paste (they belong with EST-395's #6-#8,
which is the same fix in the same style). #5 is a one-line deletion that removes the last coordinate rule from
`handleTap`; #3, #4 and #7 are constants moving into theme.h and can be one commit. #6 and #8 are one line each and
Claude can decide whether they are worth a ticket at all. Then the second UI run for the four screens not reached.

## EST-397 fix: the verified EST-395 core findings

DeepSeek (Paperclip worker, 2026-11-11). **Changed, not committed** (`.git` is read-only in the sandbox). This
ticket fixes the EST-395 findings Claude verified, and skips #5 (it is not a bug: `history_log.h:1669-1670`
refuses a NULL append handle in the counted EST-380 path) and #9 (boot time, a later ticket). #5's S2 and #12's
duplicate-scale risk are therefore not touched.

| Finding | Fix | Test |
|---|---|---|
| #1 S2 clock gate admitted epochs a 32-bit `time_t` cannot hold | `timekeeper.h`: `kMaxSaneEpoch = 2147483647UL` (`INT32_MAX`, 2038-01-19 03:14:07 UTC) replaces the 2100 bound, so the ONE gate feeds every source, `ntp_plan.h` and the shell. `timekeeper.cpp` `systemSet()` refuses an out-of-window epoch before the `(time_t)` cast, i.e. before `settimeofday()` | `test_est397.cpp`: the first value a 32-bit `time_t` turns negative (2147483648) is refused, the bound is exclusive, the last value that fits is accepted, a 2039 Wi-Fi time never reaches the setter (fake setter sees no write), `setHardware()` refuses it too, and a sane time still goes through (positive control) |
| #2 S2 `(Wire.read()<<8) \| Wire.read()` — unsequenced ADC bytes | `board.cpp` `adsReadChannel()`: two locals `hi`, `lo`, then combine | build only: the ADC path needs the I2C device. The fix removes the unsequenced pair, which is provable by reading the line |
| #3 S2 half-failed latch rolled the shadow back | `board.cpp`: `latch(bool* outputLatched)` reports which write landed; `setOutput()` rolls back ONLY when the first (0x02) write failed, otherwise re-reads 0x02/0x06 from the chip into the shadow and logs one line | build only (I2C device); the two branches are the ticket's rule, one per branch |
| #4 S2 the fix never expired | `GpsState.lastFixMs`; a GGA with quality > 0 restamps it; `gpsExpireFix(out, now)` in `gps_sky.h` clears `valid` after `kGpsFixExpiryMs` = 5 s, called once at the end of `gpsPoll()` after the parser ran. `main.cpp` hands `g_gps.valid` to `targetSetGpsFix()` and to the status bar AFTER that, so the node's position, the position source and the status bar all follow it | `test_est397.cpp`: fresh stays valid, 4999 ms valid, 5000 ms expired, restamp revives, expiry idempotent, and the `millis()` wrap case |
| #6 S3 `map.cpp` printed the centre to 5 decimals | the line now prints the tile ranges (`tx a..b ty c..d`) and the count, no degrees | `test_est397.py`: the new shape is present, `%.5f` is gone from `map.cpp`, and the source-wide float-beside-a-coordinate rule |
| #7 S3 four `mesh_link.cpp` lines logged message texts and a sender name | `[mc] DM seq %lu: %u B (hops, snr)`, `[mc] channel %d: %u B (...)`, `[ble] phone sent ch%d: %u B`, `[ble] phone sent DM seq %lu: %u B`. Lengths and our own ids only | `test_est397.py`: the four exact new format strings must be present |
| #8 S3 five `wifi_link.cpp` lines logged the SSID and the IP | `saved index %d`, `not saved: the list already holds %d networks`, `connected idx=%d rssi=%d internal=%u largest=%u (name and address not printed)`, `connecting idx=%d%s`, `forgot index %d`, following the `[ntp]` house style. The address is still handed to the WiFi model, just not printed | `test_est397.py` (the four exact formats) + `test_est385.py` still passes (its address-formatter owner list is unchanged) |
| #10 S4 the PSRAM fallback took 48 KB of internal RAM | `audio.cpp`: when `ps_malloc` fails the internal buffer is a QUARTER (12 KB, 375 ms) and one line says so. `sound_synth`: `renderPass()` takes the caller's `cap` and CUTS a longer phrase with the normal tail fade instead of returning 0, so a short buffer still plays the attack | `test_est369.cpp`: a 10-sample buffer now returns 10 with the last sample faded to 0 (the old expectation `== 0` was this ticket's behaviour change) |
| #11 S5 dead `stageInput(kBitLcdControl)` + duplicate `s_expanderReady` | both removed (the LCD control line is an output; the assignment after the rails are latched is the one the pulses use) | build |
| #12 S5 the battery scale existed twice | `batteryMilliVolts()` is `adsPinMilliVolts(raw) * 2u`, one ADC constant | build. Note: one mV of rounding can differ from the old expression (`pin_mv` truncates before the doubling); a battery percentage cannot see it |
| #13 S5 a too-long NMEA line was dropped silently | `GpsState.lineOverflow`, counted in the `else` (and the parser resets, so the rest of the line cannot be parsed as a sentence); printed as `over=` in the `[hb] gps` line | `test_est397.cpp`: the counter exists and is not `checksumBad`; `test_est397.py`: the field is in the heartbeat line |
| #14 S5 `codecMute()` wrote 0 into 0x31 when its read failed | a failed read uses the last value read from 0x31 (cached) and writes only the two mute bits; with no cached value the write is skipped and counted, one log line either way | build (I2C) |
| #9 guard: a future log line could reintroduce the four patterns | new source-wide test, see below | `test_est397.py` |

### The one new guard test

`test/host/test_est397.py` reads **every** `Serial.printf/println` statement in `src/` recursively (437 statements
in 178 files), whole statements rather than lines, and fails on: a float conversion in a statement that also names a
coordinate; an `ip=` field; an `ssid=` field; a `%s` fed by `m.text`/`s_lastText`/a bare `text`/`body`; a `%s` fed by
`from.name`/`ci->name`; a `%s` fed by an `ssid`. A `strlen(...)`/`sizeof(...)` argument is stripped first, because a
LENGTH is the fix, not the leak. It also asserts the four rewritten mesh lines, the four wifi lines and the map line
are the NEW shapes, and that `gpsExpireFix(out,` is called exactly once.

**12 whitelisted statements, each with its reason in the file** (look-alikes, all read in context): app-composed UI
notice strings, our OWN outgoing resends, the `msg`/`history`/`wifi scan` USB answers an operator asks for by hand, a
repeater's own CLI answer, and the PUBLIC NTP server's address (allowed by `test_est385.py`).
**Claude: this whitelist is the known remainder of the rule.** `usb_commands.cpp:331` (`[msg] ... text=`),
`mesh_link.cpp`'s `[hist]` dump, `rptr_link.cpp:887` and `ble_link.cpp:60` do print a stored message body or a notice
string, on explicit request and never on a receive or send path. I did not change them (a USB answer's fields are what
`tools/cmd.py` reads, and it is outside this ticket's list); say if you want them reduced to lengths as well.

### Two extra lines the new test found (fixed here)

`main.cpp:1359` (`[chat] map`) and `main.cpp:5248` (`[nodes] map`) printed a contact's EXACT position to five
decimals on every map open from a chat or the node sheet. Both are the same rule-9 family as #6 and would have made
the guard test red, so they now print the name only. They are the two lines EST-396 reported as UI tap-path leaks.

### Build and tests (real exit codes, this tree)

- `pio run -e om_lite_l2`: **SUCCESS**, exit 0. **RAM 160,328 / 327,680 B (48.9 %)**, **Flash 2,987,489 / 6,553,600 B
  (45.6 %)**. RAM and flash are unchanged from before the ticket to the byte at this resolution (the fixes are a few
  bytes of code; the sound buffer only shrinks on a board whose PSRAM refused).
- `test/host/run.sh`: **exit 0**, all suites green, including the two new halves (`est397` C++ and `est397.py`) and
  the updated `est369`/`est391` expectations.
- Existing tests had to move with the fixes and were updated, not deleted: `test_est369.cpp` (a short buffer now
  truncates, `== 10` rather than `== 0`) and `test_est391.cpp` (its "the bound as a 32-bit time_t is negative" proof
  now uses a local `kOld2100Bound` 2100 constant as history, plus a new check that the real bound is `INT32_MAX` and
  positive). `test_est385.py` needed no change.

### What I could not check

- **No device**: none of the I2C/latch/ADC/audio paths (#2, #3, #10, #14) or the GPS expiry (#4) can be exercised on
  this host, so "tested" there means the logic compiles and the branch/format is as the ticket states. The fix that
  matters for the glass is #4: the map and the status bar go to "no fix" 5 s after the last GGA, which nobody has
  watched on the panel yet.
- **`#3`'s re-read path** assumes a PCA9554 read of 0x02/0x06 answers right after a failed config write; the board
  has never been in that state on purpose.
- **`#10`'s truncation** is a real behaviour change: on a board without PSRAM a phrase longer than 375 ms is now cut
  (attack kept, tail faded) instead of the buffer being 48 KB. No PSRAM-less board was available to hear it.
- The `[hb] gps over=` field: the heartbeat line length grows by ~13 characters; the USB tool's parser reads named
  fields, so nothing should care, but nobody ran `tools/cmd.py` against a device with it.

### Next step

Claude reviews the diff and commits as Slofi; then flash and check four things on the glass: the status bar drops to
"no fix" ~5 s after the GPS antenna is pulled, the battery percentage is stable (the ADC byte order), the boot log's
`[wifi] connected idx=` line has no name or address, and a long sound (the low-battery warning) still plays on the
board whose PSRAM is fine. #9 (the ~700 ms boot delay) stays for a later ticket, and so does the `[msg]`/`[hist]`
whitelist question above.


## 2026-11-11 18:01 (Claude): EST-397 reviewed
- All 13 verified findings fixed with tests. Whitelist decision: the USB `msgs` command may keep printing message text (an on-demand debug command; Claude strips texts before quoting). NOT covered yet and the guard does not catch them: main.cpp:1279/1361 (node names), 1374/1377 (message text on resend), from the EST-396 UI sweep -> fixed in the UI fix ticket, where the guard must be tightened to catch `m.text` and `%s` names.

## EST-398 sweep: UI part 2

Read-only sweep of the files EST-396 did not read in full, DeepSeek (Paperclip worker, 2026-11-11). **No code was
changed and nothing was committed.** Scope: the four Settings/RPTR screens and their layouts, the chat layout and
selector, the message sheet, the emoji picker, the map picker, the text wrapper, the press targets and the compose/send
paths in `main.cpp`.

**Counts: 12 findings: S1 0, S2 2, S3 1, S4 1, S5 8.** Ranked most severe first. Every finding quotes the real line;
"not verified" marks what I inferred. No crash and no proven data loss: the two S2s are a confirmation that can be
armed for the wrong row and a pressed look that lands off its key. **The Settings tap order is clean**: the pop-up, the
Position choice, the selector, Back, the title action, the keyboard sheet, the tabs and the rows are consumed in the
drawing order (`main.cpp:4752, 4763, 4786, 4810, 4818, 4823, 4860, 4872`), so nothing reaches a row under an overlay.

### 1. S2 · main.cpp:4515-4524 · one armed flag is shared by every "Forget" row, so a second row is forgotten without its own confirmation
```c
if (!g_deleteArmed) {
  g_deleteArmed = true;
  showToast("Tap Forget again");
} else {
  g_deleteArmed = false;
  om::wifiForget(at);
```
`g_deleteArmed` is one global (`main.cpp:143`) and the Saved-networks page builds one Forget button per saved network
(`main.cpp:3834`, `A_WifiForgetBase + i`). The handler checks only that SOME row was armed, never that it was THIS row.
Trigger: tap Forget on network A (armed, toast), then tap Forget on network B: B's password is deleted on the FIRST tap
of B, with no second tap and no chance to cancel. Two more oddities in the same block: the button's label never changes
(the channel page's does: `main.cpp:3674` "Tap again to delete"), and a tap anywhere else on the page does not disarm.
Fix: store the armed index (`static int g_deleteArmAt = -1;` and compare), reset it on every other row and on `setGo`.
Effort S.

### 2. S2 · ui/press_targets.h:262-263 · the RPTR card's refresh and banner keys light up at their SCROLL-0 place
```c
case RptrHit::Refresh: t.kind = PressKind::Button; t.rect = a.refreshTap; break;   // inside body
case RptrHit::BannerButton: t.kind = PressKind::Button; t.rect = a.bannerButton; break;
```
The page's hit test moves both rectangles with the scroll before testing them
(`ui/rptr_layout.cpp:180-181` `Rect rt = a.refreshTap; rt.y = (int16_t)(rt.y - scroll);` and the same two lines for
`a.bannerButton`), and the drawing moves too (`ui/screen_rptr.cpp:156` `const int cy = (int16_t)(a.refresh.y - scroll)`,
`:169` `button.y = (int16_t)(button.y - scroll);`). The pressed look does not: `omFrameBlendRect(t.rect...)` paints the
unscrolled rectangle. Trigger: scroll a repeater page at all, then hold the refresh ring: the gold press tint appears
lower down, over a settings row, while the ring that was tapped stays unlit. The tap itself is correct, so this is the
pressed look only. Fix: build both rects with `-scroll` and `intersect(a.body)`, exactly as the hit test does. Effort S.

### 3. S3 · main.cpp:1279, 1313, 1334 · three more log lines print another node's name
```c
Serial.printf("[chat] no contact named %s, no DM\n", name ? name : "?");
Serial.printf("[chat] reply to %s\n", g_replyName);
Serial.printf("[chat] mention %s\n", tag);
```
Rule 9 (the repo is public) allows no other people's names in logs. These three are on the message sheet's paths (reply,
mention, a DM that could not be opened) and print a contact's name on every tap. They are NOT EST-396 #1/#2 (those were
the position at `1359` and the message text at `1172/1372/1375`); `1361` (name only, after EST-397) is already on
Claude's list for the UI fix ticket, so I am not re-reporting it or the text lines. The EST-397 guard only knows
`from.name`/`ci->name`, so none of the four is caught. Fix: `[chat] reply opened`, `[chat] mention prepended`,
`[chat] DM refused: unknown contact`, and add `m.name`/`g_replyName`/`name` to the guard's `%s` list. Effort S.

### 4. S4 · main.cpp:1213 with 396-407 · every emoji tap writes NVS, and the write's result is dropped
```c
g_emojiRecent.use(cp);
saveEmojiRecent();                       // main.cpp:1213
...
g_prefs.putBytes("erec", buf, sizeof(buf));   // main.cpp:406, return value ignored
```
A pick is a per-event flash write (69 bytes into the `omlite` NVS partition) for a 16-entry convenience list, and the
caller cannot tell whether it landed: exactly EST-380's "hist_dropped = 0 while losing data" shape. Trigger: typing a
message with six emoji = six NVS commits; nothing on the glass or in the log if one fails. Fix: mark the list dirty and
flush it when the sheet closes (or in `chatSendDraft`), and count a failed `putBytes`. Effort S.

### 5. S5 · main.cpp:4457-4469 and main.cpp:4645-4658 · the whole "apply the manual time" block exists twice
```c
const bool made = om::localEpochFromCivil(g_setTime.year, g_setTime.month, g_setTime.day,
                                          g_setTime.hour, g_setTime.minute, 0, epoch);
if (made && om::saneEpoch(epoch) &&
    om::timeKeeper().setFromSource(epoch, om::TimeSource::Manual, millis() / 1000)) {
  saveTimeFloor();
```
Identical in both places (the title row's Set key and `settingsEditorOk()`), including `saveTimeFloor()` and the two
toasts. A third copy of the same rule would be the easiest way to re-introduce the EST-391 32-bit bug. Fix: one
`applyManualTime()` returning the toast text. Effort S.

### 6. S5 · ui/settings_layout.h:473-474 · the year steppers offer 2000..2099, the clock accepts up to 2038
```c
t.year += dir;
if (t.year > 2099) t.year = 2000;
```
The one gate is `src/timekeeper.h:45` `constexpr uint32_t kMaxSaneEpoch = 2147483647UL;  // INT32_MAX = 2038-01-19`.
So the page lets the user step to 2050 and then answers the generic `"Time not set"` (`main.cpp:4467`), with no hint
that the bound is 2038 or why. This is the UI half of EST-391 that the fix left in place: the layout's range still
assumes a 64-bit `time_t`. Fix: clamp the year to the civil year of `kMaxSaneEpoch` (2038) and say so on the Note row.
Effort S.

### 7. S5 · ui/screen_settings.cpp:124-128 · the Warn box is left-flush, and its text ends 1 px outside the plate
```c
d->fillRoundRect(r.x, r.y + 3, r.w - 2 * kSetPadX, r.h - 6, kRadius, kWarnBg);
...
const int x = r.x + kSetPadX + 6;
const int w = r.w - 2 * (kSetPadX + 6);
```
With `kSetPadX` 7 the plate is 14 px narrower than the row but starts at the row's own left edge, so it is inset on the
right only; the text's right edge is `r.x + r.w - 13` against the plate's `r.x + r.w - 14`, i.e. one pixel of ink past
the background it was drawn on. Every other row insets from `r.x + kSetPadX`. Fix: `r.x + kSetPadX` for the plate and
`r.w - 2 * kSetPadX` for the text. Effort S.

### 8. S5 · ui/screen_settings.cpp:214 and :223 · the two row kinds that never check their label's width
```c
d->drawString(row.label, labelX, cy);                          // Stepper, line 214
d->drawString(row.label, labelX, cy);                          // Toggle, line 223
```
Every other kind ellipsises (`:159`, `:236`, `:254`, `:263`). A Stepper's label can therefore run under its minus key
(which starts at `row.right() - 119`) and a Toggle's under its right-aligned value. Today's labels are short
("Frequency", "Share position"), so this is a latent overflow, not a visible one. Fix: `drawTextEllipsis` with the room
the keys leave. Effort S.

### 9. S5 · ui/screen_rptr.cpp:187-213 · a private word-wrapper, with a 48-byte line cap and a silent tail
```c
const int lineH = 15;
char line[48];
...
while (p[n] && n + 1 < sizeof(line)) { ... }
```
This is a second wrapper beside `ui/text_wrap.cpp` `wrapText()`, which the Settings screens use for the same job. It
breaks only at a space (`if (p[n] && lastSpace > 0) n = lastSpace;`), so one long unbroken token is cut at 47 bytes
mid-word, and the loop stops at the box's floor with no ellipsis, unlike `drawTextEllipsis`. **Not verified** that a
current `modalText` reaches the cap (the strings I saw are short); the risk is that nothing would tell us. Fix: the
shared `wrapText()` + `wrapConsumed()` + an ellipsis on the last line. Effort S.

### 10. S5 · ui/map_pick.cpp:29-42 vs ui/screen_rptr.cpp:29-42 · the same three drawing helpers in two files
```c
// screen_rptr.cpp:29  and  map_pick.cpp:29, byte for byte
void smallKey(lgfx::LovyanGFX* d, const Rect& r, const char* text, bool strong) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, strong ? kSelBg2 : kPanel2);
```
`smallKey` and `drawBack` are identical copies (map_pick's comment explains why it "does not need the RPTR screen's
private drawing helpers", which is the drift EST-384 moved the picker to avoid), and `drawPickAction` is `drawButton`
without the `danger` branch. `ui/screen_settings.cpp:475-494` and `ui/screen_rptr.cpp:454-471` are a second pair:
`settingsEditorKeyAt` and `rptrEditorKeyAt` are the same 15 lines. Fix: one `ui/keys.cpp` for the small controls and
one `editorKeyAt()`; Effort S.

### 11. S5 · ui/map_pick.cpp:50 · a screen-sized hard-coded rectangle inside the shared picker
```c
drawEmptyState(d, Rect{8, 60, 304, 100}, "Map pack not open.",
               "The card needs /maps/slo/tiles.pack.");
```
`304` is `kMapCanvasW` and `8/60/100` are the L2's numbers; this file is drawn on both devices' pickers, and rule 3
says the screen size is a parameter. Fix: `Rect{(int16_t)kMapMargin, ..., (int16_t)(L.width - 2 * kMapMargin), ...}`
from the layout. Effort S.

### 12. S5 · ui/screen_node_info.cpp:47-63 · the row list has no clip and no scroll, and meets the floor hint
```c
NodeInfoLine lines[kMaxNodeInfoLines];
const int n = nodeInfoLines(m, lines, kMaxNodeInfoLines);
const int16_t rowH = 20;
...
drawTextEllipsis(d, FontRole::MonoMeta, kDimmer, kScreen, kRowPadX, L.height - 10,
                 L.width - 2 * kRowPadX, "tap the header to go back");
```
Nine rows from `back.bottom() + 5` would end at y 234 while the hint is drawn at `L.height - 10` (230), on top of the
last row. **Not reachable today** (`hasSnr` is never true, so the page has at most eight rows and ends at 214), which is
why this is S5 and not S2: the ninth row is one field away. Fix: clip to a body rect and put the hint in the layout.
Effort S.

### Clean (read, nothing found)
- **The Settings tap order** (`main.cpp:4741-4880`): the pop-up, the Position choice, the selector, Back, the disabled
  title action, the keyboard sheet and the tab row are all consumed in the order they are drawn, and the Position
  choice's own scroll is lifted by `settingsChoiceScrollFor()` before it opens. This is the class that bit us three
  times in S492 and it is right here.
- **One rectangle drives draw and tap** in `settings_layout.h`, `rptr_layout.h` and `chat_layout.h`: the rows, the
  stepper keys, the sheet's keys (`g.keyHit` built beside `g.keys` at `chat_layout.cpp:316`), the selector rows
  (`selRowLayoutFor` asked by both the drawing and `selRowAt`) and the emoji pad all come from one function. Apart from
  finding 2's pressed look I found no second set of numbers on a tap path.
- **Allocating the row list in PSRAM** (`main.cpp:352`, `psramArray<SettingRow>(64)`, ~15 KB away from internal RAM):
  the Settings model is not part of the internal-RAM pressure, and the boot line prints which heap answered.
- **No SD, NVS or radio call from a draw**: the two `putBytes` writes are in the tap handlers (finding 4 is one of
  them), and the map's `om::mapRender()` in `map_pick.cpp:47` is reached from the loop's draw, not from the USB
  command handler. Nothing in this scope touches the radio.
- **`ui/chat_selector.cpp` and `ui/message_sheet.cpp`**: the row count is capped at `kMaxSheetRows` in both the builder
  and the layout, and every string is measured or ellipsised. Clean.
- **`ui/press_feedback.cpp`** and **`ui/emoji_picker.cpp`**: clean (the latter is data only; the strip's arithmetic
  fits: 16 recents plus the Send key end at x 314 inside the 316 px area).
- **`ui/text_wrap.cpp`**: the UTF-8 walk does not split a code point, does not run past a NUL, and refuses to loop on a
  refused unit. Clean.

### Coverage
**Read in full:** `ui/settings_layout.h`, `ui/screen_settings.cpp`, `ui/screen_rptr.cpp`, `ui/rptr_layout.cpp`,
`ui/chat_layout.cpp`, `ui/chat_selector.cpp`, `ui/text_wrap.cpp`, `ui/map_pick.cpp`, `ui/press_feedback.cpp`,
`ui/screen_node_info.cpp`, `ui/message_sheet.cpp`, `ui/emoji_picker.cpp`, `ui/press_targets.h:118-310`, parts of
`main.cpp` (95-205, 320-390, 1100-1420, 3186-4663, 4700-4979), `ui/node_info.h`, `docs/REBUILD.md`.
**Read in part or only through targeted grep (so absences there are NOT evidence):** `ui/chat_layout.h`,
`ui/chat_selector.h`, `ui/message_sheet.h`, `ui/theme.h` (the constants the layouts use).
**In scope but NOT reached** (the 30-minute cap; I worked severity first and stopped): `ui/map_trace.cpp`,
`screens/map_tab.cpp`, `ui/rptr_layout.h` (only its constants), `ui/settings_prefs.h`, `ui/emoji_picker.h`, and
`main.cpp`'s remaining settings-adjacent handlers outside 3186-4979 (the RPTR pumps 1700-3100 and the USB handlers).
No build and no host test was run: this ticket changes no code.

### Next step
Findings 1 and 2 are the two that change what the user sees and should be the next fix ticket (both one-line
comparisons); finding 3 belongs with the EST-396 #1/#2 UI log fix and needs the guard extended to `m.name`,
`g_replyName` and a bare `name`. #4 (NVS per emoji), #5 (the duplicated time apply) and #6 (the 2038 bound in the
year steppers) are small and independent. A third sweep should read `map_trace.cpp` and `screens/map_tab.cpp`, which
are the two files in this scope nobody has read yet.

## 2026-11-11 (DS, EST-399): the serious UI sweep findings fixed (Forget confirmation, scrolled pressed look, log privacy, emoji NVS)
Status: in_review (not committed, not pushed).

DeepSeek (Paperclip worker). Fixes the EST-398 #1/#2/#3/#4 findings and the EST-396 #1/#2 privacy lines, and tightens
the EST-397 log guard as Claude's review note asked. The S5 polish stays for the next ticket.

### Finding -> fix -> test

| Finding | Fix | Test |
|---|---|---|
| EST-398 #1 S2: one `g_deleteArmed` for every Wi-Fi Forget row, so Forget on B after arming A forgot B on its first tap (main.cpp:4515-4524) | New `src/ui/delete_arm.h`: a `DeleteArm` holds the ARMED ROW INDEX plus its millis() stamp. Three separate arms replace the one flag: `g_wifiForgetArm` (index = the saved network's row), `g_chatDeleteArm` (the channel the Chat page shows) and `g_chanDeleteArm` (the channel the Channel page shows). A second tap on the SAME row is the only confirmation, a tap on any other row re-arms THAT row, `handleSettingsRow()` clears the other row families on any other row action, every page-leave path (`setGo`, `setBack`, `openSettings`, `settingsBack`, the tab switch, the selector pick, the A_ChatPick toggle) calls `clearDeleteArms()`, and `kDeleteArmMs` = 6000 ms expires it. Every label now comes from `armedRow()` (the Wi-Fi row says **"Tap again to forget"**, the two channel rows keep "Tap again to delete"), and `deleteArmTick()` in the loop clears an expired arm and repaints the page once, so the label can never outlive the window a tap honours | `test_est399.cpp` `testDeleteArm()`: arm A then tap B -> **B is armed and NOT confirmed**, arming B disarms A, the second tap on B confirms and disarms, a third tap arms again; the window at 5999 ms confirms and at 6000 ms does not (a late tap only re-arms); `clear()`; two arms are independent; the millis() wrap. `test_est399.py`: `g_deleteArmed` gone, three arms declared, each `om::mcChannelRemove(...)`/`om::wifiForget(...)` behind its own `Tap::Confirmed`, both labels from `armedRow()`, >= 7 leave-path clears, the timeout tick called from the loop |
| EST-398 #2 S2: the RPTR card's refresh key and banner button lit their scroll-0 rectangle (ui/press_targets.h:262-263) while the hit test moved them by -scroll | One helper, `rptrPageScrolledRect(r, scroll, body)` in `ui/rptr_layout.h` (scroll the y, cut to the viewport). `rptrPageHitAt()` and `pressTargetForRptrPage()` both ask it, so the plate and the tap are one calculation. Audit of every other scrolled list (Nodes rows, the selector, the Settings rows and their stepper keys, the RPTR list, the RPTR page rows, the chat message band) found no second set of coordinates: they already used the layout's own scrolled rect | `test_est399.cpp` `testRptrPageScrolled()`: at every scroll 0..max the refresh key and the banner button answer their hit test, the pressed rect **contains the finger** and IS the scrolled slice; Back stays put; a moved scroll really moves the plate. The old behaviour is reproduced in the same test: the scroll-0 rect does NOT contain the finger at 13 of those taps (a counter fails the run if that proof ever goes vacuous). `testOtherScrolledLists()` runs the same "the pressed rect contains the finger" sweep on the Nodes list, the RPTR list, the Settings rows and stepper keys, and the selector. `test_est399.py`: the helper is defined once, the hit test calls it twice, `t.rect = a.refreshTap` is gone |
| EST-396 #1 S3: a node's exact position and name in the log (`map` lines) | `[chat] map:` / `[nodes] map:` now say only that the node's position is being opened. EST-397 had already removed the degrees; this removes the name | `test_est397.py` (tightened): the two new shapes are asserted, and the old lines are in the negative controls |
| EST-396 #2 S3: the message text on send and resend (1172, 1372, 1375) plus 1187 (`[mc] UI send: '%s'`, `wire`) | Verdict + length: `[mc] UI DM send: ok, 12 B`, `[mc] UI send: ok, 34 B, reply`, `[chat] DM resend ok, 12 B`, `[chat] resend ok, 12 B` | `test_est397.py`: the four new shapes asserted; `g_kbText`/`wire`/`tag` added to the message-text matcher (they were the three shapes the first guard could not see), with a negative control each |
| EST-398 #3 S3: three more node names (1279, 1313, 1334) plus 1361 | `[chat] DM refused: unknown contact`, `[chat] reply opened: seq %lu`, `[chat] mention prepended: %u B`, `[chat] map: opening the node's position` | `test_est397.py` shapes + negative controls; the `name` matcher now also catches `g_replyName`/`s_currentName` (identifiers ending in `Name`, but not a call such as `timeSourceName(`) |
| #4 TIGHTEN the EST-397 guard | The scan now fails on a `%s` fed by ANY `name`/`.name`/`->name` (plus our own `...Name` stores) or by ANY `text`/`.text`/`->text`/`body`/`g_kbText`/`wire`/`tag`, and on a float beside a coordinate name (`lat`, `lon`, `latitude`, `longitude`, `lat_e6`, `lon_e6`, `.lat`, `.lon`). The argument expression is taken with its STRING LITERALS stripped (a format that explains itself, "name and address not printed", is not a false hit) and with `strlen()`/`sizeof()` stripped (a length is the fix). 15 NEGATIVE CONTROLS re-run the matchers on the exact shapes this repo has shipped, so a broken regex fails the run; a whitelist entry that no longer matches anything also fails (no silent blanket). `python3 test_est397.py <tree>` runs the scan alone against another tree. Whitelist: 16 entries, one reason each (the USB `msg`/`msgs`, `history`, `nodes`, `chats`, `rptr scan`, `rptr status` answers Claude approved, the app's own toasts, a repeater's own CLI answer, our font asset names, the public NTP server's name), plus TWO argument-level exemptions (the public NTP server's `srv.text`, the toast's own `.text`) that exempt one expression instead of a whole line | **The guard fails on the old lines.** Old `test_est397.py` against the pre-fix tree (a copy in /tmp): **exit 0**, "437 Serial statements in 180 files carry no position ... message text or name", i.e. it missed all of it. New guard against the SAME pre-fix tree: **exit 1, 50 findings**, 14 of them in `main.cpp` (567, 601, 838, 1172, 1187, 1279, 1313, 1334, 1361, 1374, 1377, 4794, 5249, 5610) = every line this ticket names. New guard on this tree: exit 0, 439 statements in 180 files. `test_est399.py` also fails if the whitelist grows past 20 entries |
| EST-398 #4 S4: every emoji tap wrote NVS and dropped the result (main.cpp:1213 with 396-407) | New `src/ui/emoji_persist.h`: a pick only marks the list dirty (`g_emojiSave.changed(now)`). One `emojiPersistStep(pageShowing, now)` in the loop writes it when the emoji page is no longer showing (however it was left: the abc key, Send, a tap away, a tab change, the gear) or after 30 s with no pick; `saveEmojiRecent()` now RETURNS `putBytes(...) == sizeof(buf)` (0 = failed), the gate counts writes and failures, a failure keeps the list dirty and retries no sooner than 5 s, and one log line per attempt says `[emoji] recent saved: N pick(s), write N` or `[emoji] recent NOT saved: the NVS write failed (N of N writes so far)` | `test_est399.cpp` `testEmojiGate()`: nothing dirty writes nothing; a pick is not due while the page shows; 29999 ms is not enough and 30000 is; six picks in a row are one write and the quiet window restarts on each; leaving writes at once; a success clears the count; a failure keeps it dirty, counts, waits 4999 ms and retries at 5000; a later success keeps both counts; the millis() wrap. `test_est399.py`: the pick path no longer calls the writer, exactly ONE call site, the result is checked, the loop calls the step with the page state, the failure line exists |
| every other user of the old shared flag (the ticket asked) | `g_nodeMenuRemoveArmed` (the Nodes long-press Remove) and `g_clearAsk` (Clear messages) were checked: the node arm is cleared when the sheet opens and closes and the sheet is modal on one node, and the Clear-messages pop-up names its channel when it opens, so neither can be armed for the wrong row. They are left as they are and NOT folded into `DeleteArm` (they are not a "tap again" pair) | reasoning recorded here; the pop-up/sheet tests already in `test_est362`/`test_est382` still pass |

### The extra lines the tightened guard found (fixed in this ticket, all rule 9)

The ticket's list was the UI half. The tightened scan then failed on 36 more lines that no sweep had reported,
because they print a CONTACT's name or a chat's name on a NORMAL path (not a USB answer): `[mc] new node`, `[mc] DM
open`, `[mc] DM to`, `[mc] ping` (x3), `[mc] trace` (x2), `[mc] path to ... reset`, `[mc] ... favourite`, `[mc]
contact ... removed`, `[mc] channel slot N renamed/added` (mesh_link.cpp); the repeater's name on the RPTR session
lines and `regions`/`name <-` answers (rptr_link.cpp); `[nodes] menu/info/select`, `[hist] open`, `[hist] no older
messages`, `[set] chat picked`, `[rptr] open/auto-login` (main.cpp); `[map] select` (screens/map_tab.cpp). Each now
prints an ID or a length instead of the name: a contact is `id %08lx` (the FNV-1a hash of its public key, the same
hash `KnownKeySet` already uses), a repeater is its 6-byte path-hash prefix (already on most of those lines), a
channel is its slot and a byte count, a chat is its index. Nothing was deleted: every line still says what happened.

### Build and tests (real exit codes, this tree)

- `pio run -e om_lite_l2`: **SUCCESS**, exit 0. **RAM 160,360 / 327,680 B (48.9 %; before this ticket 160,328,
  +32 B)**, **Flash 2,988,245 / 6,553,600 B (45.6 %; before 2,987,473, +772 B)**, `firmware.bin` 2,988,608 B.
  No compiler warning.
- `test/host/run.sh`: **exit 0**, every suite green, including the two new halves: `test_est399.cpp` (the arm, the
  scrolled pressed rects, the emoji gate) and `test_est399.py` (the shell wiring), and the tightened `test_est397.py`
  (439 statements, 16 whitelisted, 15 negative controls).
- No existing test had to change its expectation: the log formats this ticket rewrote are not asserted by
  `test_est365.py`/`test_est385.py` (they read the USB answers, which are untouched).

### What I could not check

- **No device.** Nothing here was seen on the glass. On the panel, please check: (a) saved networks, tap Forget on
  A, then Forget on B -> B's row says "Tap again to forget" and only a second tap deletes it; (b) wait 6 s after
  arming -> the label goes back to "Forget" by itself; (c) on RPTR, scroll a repeater page and hold the refresh
  ring -> the gold plate lands on the ring, not 20 px lower; (d) type three emoji and leave the page -> one
  `[emoji] recent saved` line in the log, not three commits.
- The `[emoji]` line's counts are only visible on the serial console.
- The delete-arm timeout repaints the Settings page from the loop; on a page with the keyboard sheet open the tick
  does not repaint (it waits), so a label can stay stale for as long as the sheet is up. No delete row lives on a
  page that has a sheet, so this cannot show today.
- A tap consumed by a pop-up or a sheet does not disarm an arm (they cannot coexist with an armed delete row on the
  same page); every row action, page change and tab switch does.
- The guard's `name`/`text` rules still cannot see a store with a different spelling (e.g. `s_selName` was caught,
  `s_currentName` was caught by the `...Name` rule, but a future `s_who` would not be). The SHAPES assertions and the
  negative controls are what keep the fixed lines from regressing.

### Next step

Claude reviews the diff and commits as Slofi. Then the S5 polish ticket: the EST-398 #5-#12 list (the duplicated
manual-time apply, the 2038 year steppers, the Warn box, the two unellipsised row kinds, the private RPTR wrapper,
the duplicated drawing helpers, the hard-coded picker rect, the node-info overflow) plus the EST-396 #3-#8
constants, and the third sweep of `map_trace.cpp` and `screens/map_tab.cpp`.


## 2026-11-11 18:34 (Claude): EST-399 reviewed
- Guard proof re-run by Claude: the NEW test_est397.py against the OLD source exits 1 with 65 failures; against the fixed source exits 0. (A first check with git stash was invalid: it restored the old test too.) The tightened guard found 36 more log lines no sweep reported; all fixed.

## 2026-11-11 19:00 (DS, EST-400): L2 Pro firmware, the S5 polish from the sweeps
Status: in_review (not committed, not pushed).

DeepSeek (Paperclip worker). Fixes the 14 S5 polish findings EST-399 left in place: six constants/dead lines
from EST-396 (#3-#8) and eight from EST-398 (#5-#12). No behaviour change beyond what each finding names; two
existing tests carried the OLD expectation of a changed rule and were updated (named below).

### Finding -> fix -> test

| Finding | Fix | Test |
|---|---|---|
| EST-396 #3 S5: `screen_nodes.cpp` reserved a hard-coded 70 px value column, put the value on the panel edge and chose gold ink by testing `value[0]=='o' && value[1]=='n'` | `theme.h` `kNodeSheetValueW = 70`; the label ellipsises before it and the value is right-aligned at `r.right() - kSheetPadX` (every other sheet row's inset). `NodeMenuRow` gained `valueAccent`, set from `c.favourite`, so the drawing never sniffs a string | `test_est400.cpp` `testMapConstants`: the sheet's row is wider than the constant leaves; `test_est400.py`: no `r.w - 70`, no `'o'/'n'` test, the flag is passed and set |
| EST-396 #4 S5: the map callout's two text rows (7/12/28/14) and its 8/16 inset were literals beside `kMapCalloutH` | `map_chrome.h`: `kMapCalloutPadX/PadY/Line1Cy/Line2Cy` with two `static_assert`s that the lines stay inside the box and off each other; the box insets by `kMapMargin` | `test_est400.cpp` `testMapConstants`: x/w/pad, both line centres inside, no overlap with the tab bar; `test_est400.py`: the constants are used and the literals are gone |
| EST-396 #5 S5: `main.cpp` `handleTap` kept `const bool topRight = (y < 40 && x >= 260);`, the old coordinate rule that decided nothing | deleted (with a comment in the test, not in the shell: the tap log stays) | `test_est400.py`: `topRight` appears nowhere |
| EST-396 #6 S5: the one-line toast drew a centred `drawString` with no ellipsis and no clip, so a long line ran off both rounded ends | `drawToast` measures the text, keeps it centred while it fits the plate's inner width (304 - 2*8 = 288 px) and draws through `drawTextEllipsis` when it does not | `test_est400.py`: the `drawToast` body uses `drawTextEllipsis`/`measureText`, no `drawString`, no `middle_center`; the cut arithmetic is `testEllipsisFit` (shared with `drawTextEllipsis`) |
| EST-396 #7 S5: the chat and Nodes message/action sheets rounded with `kOskRadius`, the KEYBOARD sheet's constant | `theme.h` `kSheetRadius = kRadiusLg` (6, the same value today) and `screen_chat.cpp`/`screen_nodes.cpp` use it; the keyboard sheet keeps `kOskRadius` | `test_est400.py`: both sheets use `kSheetRadius`, neither draws `g.panel.h + kOskRadius`; `test_est400.cpp` asserts the two constants are independent names with the same 6 px value |
| EST-396 #8 S5: `toasts.cpp deliver()` logged `[notice] show` BEFORE `showNotice()` could still refuse it | the decision is mutable, every path updates it to the delivery that really happened (`"refused"` / `"queue-full"` on a refusal) and the ONE `logDecision()` call is after the switch | `test_est400.py`: one log call in `deliver()`, after `showNotice`, a refused show sets its own reason |
| EST-398 #5 S5: the "apply manual time" block was duplicated in the title row's Set key and the editor's OK (localEpochFromCivil, saneEpoch, setFromSource, saveTimeFloor, both toasts) | one `applyManualTime()` in `main.cpp`: builds the epoch, gates it, sets the source, saves the floor, shows the one toast and returns whether the clock was set; both doors call it | `test_est400.py`: exactly ONE `localEpochFromCivil(` in `main.cpp`, one definition + two call sites, the old Note row string gone |
| EST-398 #6 S5: the year steppers offered 2000..2099 while the clock's gate is `kMaxSaneEpoch` (INT32_MAX = 2038-01-19), so 2050 answered only "Time not set" | `timekeeper.h` derives `kMinSaneYear`/`kMaxSaneYear` from `kMinSaneEpoch`/`kMaxSaneEpoch` with a constexpr `civilYearOfEpoch()`; `stepCivilTime` wraps in that window; the Note row now says "2025 to 2038. The next GPS fix takes over." | `test_est400.cpp` `testYearStepper`: 2038+1 -> 2025, 2025-1 -> 2038, every year in the window is `saneEpoch` on Jan 1, the leap-day clamp, the month wrap; `test_settings.cpp` updated (it asserted 2099->2000) |
| EST-398 #7 S5: the Settings Warn plate started at the row's own left edge with a width already inset on both sides, so it was inset on the right only and the text ended 1 px past its background | `drawWarn` builds the plate at `r.x + kSetPadX`, `r.w - 2 * kSetPadX` and keeps its 6 px inner text pad, so the text ends 6 px inside the plate | `test_est400.py`: the plate uses `r.x + kSetPadX`, the left-flush fills are gone; `docs/shots/2026-11-11-est400-polish.png` panel 2 |
| EST-398 #8 S5: the Stepper and Toggle row kinds drew their label with no width check, so it could run under the minus key / the right-aligned value | `settings_layout.h` hands out `settingsStepperLabelW(row)` (up to the minus key minus `kSelGap`) and `settingsToggleLabelW(row, valueW)`; both kinds draw through `drawTextEllipsis` with that room, the Toggle measuring its value first | `test_est400.cpp` `testLabelRooms`: exact room arithmetic at 320, no overlap, zero (never negative) on a narrow row; `test_est400.py`: both row kinds use the helpers and only the Selector's own section label is still an unclipped `drawString` |
| EST-398 #9 S5: `screen_rptr.cpp` had a private word-wrapper with a 48-byte line buffer, a space-only break and a silent stop at the box's floor | it now uses `ui/text_wrap.h` `wrapText()` + `wrapConsumed()` on the real measure callback; the box's line count comes from its height; a tail that did not fit ends with "..." (and `drawTextEllipsis` still guards the width) | `test_est400.py`: no `char line[48]`, no `lastSpace`, the shared wrapper and `wrapConsumed` are used and the cut appends dots; the wrapper itself is the EST-368-tested one |
| EST-398 #10 S5: `map_pick.cpp` and `screen_rptr.cpp` had byte-identical `smallKey`/`drawBack` (and `drawPickAction` was `drawButton` without the danger branch); `settingsEditorKeyAt` and `rptrEditorKeyAt` were the same 15 lines | new `ui/keys.h`/`keys.cpp`: `drawSmallKey`, `drawSheetBackKey`, `drawActionKey(danger)` and ONE `editorKeyAt()`; `EditorKeyHit` is the one struct (`SettingsKeyHit`/`RptrKeyHit` are aliases). Both screens and the picker call it | `test_est400.py`: each helper and the hit test are defined exactly once, no private copy in either screen, both wrappers return `editorKeyAt(...)`; `test/host/test_est351.cpp` etc. still link and pass |
| EST-398 #11 S5: `map_pick.cpp` drew the empty state in a screen-sized literal `Rect{8, 60, 304, 100}` | `RptrMapPickLayout` gained `empty`, built in `rptrMapPickLayoutFor()` inside `m.map` with `kMapMargin` on each side and `kMapPickEmptyH` high; `drawMapPick` passes `m.empty` | `test_est400.cpp` `testMapConstants`: the band is one margin inside the map, centred, clear of the back key, the zoom label and the coordinate panel; `test_est400.py`: the literal is gone, `m.empty` is used |
| EST-398 #12 S5: the Info page's rows had no clip and no scroll, and the hint was a literal baseline: a ninth row (one field away) ended on top of it | new pure `ui/node_info_layout.h`: one header, a clipped `body` (54..220 at 320x240), a `hint` rect on the same line the old baseline drew (centre 230), `nodeInfoMaxScroll`/`nodeInfoDragScroll`/`nodeInfoSnapScroll`; `screen_node_info.cpp` clips the rows and draws the hint from its rect; `nodeInfoLineCount()` is the cheap count the scroll limit uses; the shell keeps `g_nodeInfoScroll` and drags/snaps it like every other list | `test_est400.cpp` `testNodeInfoLayout`: body never reaches the hint, 8 rows fit and 9 need exactly 14 px, the scrolled last row lands on the body's floor, the give/snap, and `nodeInfoLineCount == nodeInfoLines` over all 32 model shapes; `test_est400.py`: the clip, the hint rect, no `rowH = 20`/`L.height - 10`, the shell passes the scroll |

### Previews

`tools/mkest400.py` -> `docs/shots/2026-11-11-est400-polish.png` (2x, four panels). It compiles a dumper over the
firmware's own headers and draws ONLY the rectangles and RGB565 colours those print: the toast plate and its 288 px
room, the Settings Warn row/plate, and the Info page's header/body/hint with the nine real row labels. The text is the
real UI copy or invented placeholders ("Alice", `<position>`, an invented key and an invented log line); no real name,
coordinate or message. Panels: the toast ellipsised inside its plate, the Warn box inset and its text inside it, the
Info page at the top (the ninth row cut by the clip, clear of the hint) and at its maximum scroll (the ninth row on the
body's floor).

### Build and tests (real exit codes, this tree)

- `PLATFORMIO_CORE_DIR=/home/slofi/.cache/pio-om-lite/core pio run -e om_lite_l2`: **SUCCESS**, exit 0.
  **RAM 160,400 / 327,680 B (49.0 %; before 160,360, +40 B)**, **Flash 2,988,985 / 6,553,600 B (45.6 %; before
  2,988,245, +740 B)**, `firmware.bin` 2,989,344 B. No compiler warning.
- `test/host/run.sh`: **exit 0**, every suite green, including the two new halves: `test_est400.cpp` (year range,
  label rooms, ellipsis, node-info layout/scroll/line count, callout, picker band) and `test_est400.py` (the source
  wiring for all 14 findings). `test_est397.py` now reads 183 files / 439 statements (three new UI files, no new
  Serial), 16 whitelisted, 15 negative controls.
- **Two existing tests changed their expectation, both because a finding names the change:**
  `test_settings.cpp` asserted `stepCivilTime` wrapping 2099->2000 (the old range; now `om::kMaxSaneYear` ->
  `om::kMinSaneYear`), and `test_est351.cpp` asserted the callout at x 8..312 (now `kMapMargin` -> 306..314 at 320).

### What I could not check, and what is left

- **No device.** Nothing here was seen on the glass; the PNG is a host render of the real rectangles, not a
  screenshot. On the panel, please check: (a) a long toast (the History-not-saved line with a big count) ends with
  "..." inside its plate; (b) the Preset confirmation's Warn text starts and ends inside its plate; (c) the Info page
  of a contact with a key, position, path and SNR: the ninth row is cut at the bottom and a drag brings it up.
- **The 2038 bound is a YEAR.** kMaxSaneEpoch is 2038-01-19, so the stepper can still name 2038-02..12 and the Set
  key then answers "Time not set": a year stepper cannot express the 19 January cut-off. The window is now 11 months
  of one year, not 22 years, and the Note says the range out loud.
- The Info page has no visible scrollbar: the ninth row is simply clipped until the finger drags it, like the Nodes
  list. Its body is 166 px, so 8 rows fit and the ninth needs 14 px of scroll.
- `kSheetRadius` and `kOskRadius` are both 6 today, so the sheet-radius fix changes no pixel; it changes WHO owns the
  number.
- The third sweep (`ui/map_trace.cpp`, `screens/map_tab.cpp`) is still not done; it was the EST-398 report's "next
  step" and is outside this ticket.

## 2026-11-11 19:20 (DS, EST-401): L2 Pro firmware, charging bolt step 1 (measure block on, VBUSOK + AIN1..3 in the battery log)
Status: in_review (not committed, not pushed).

DeepSeek (Paperclip worker). Step 1 of the charging bolt, per `research-2026-11-11.md` section 2 and the
2026-11-11 "Measured on the device" note: the chip at 0x22 (AW35615, FUSB302 family, DEVICE_ID 0x91) reported
POWER 0x01 (bandgap and wake only), so STATUS0's VBUSOK read 0 although a cable was in. Nothing in the UI changes;
no bolt is drawn anywhere.

### 1. The one write: POWER (0x0B) = 0x07, and why that value

`POWER` is read first and the byte written is `read-back | PWR0 | PWR1 | PWR2`, i.e. **0x01 -> 0x07** on this unit.
The three bits, by the datasheet's names: **PWR0 (bit 0) bandgap and wake circuitry**, **PWR1 (bit 1) receiver and
current references**, **PWR2 (bit 2) the measure block**. So 0x07 = those three on, nothing else: the bandgap was
already on and stays on, the references and the measure block (the ADC behind MEASURE/SLICE and STATUS0) come up.
The register is written as a read-modify-write on purpose, so a bit this build does not name (bit 3) survives
untouched instead of being cleared by a constant byte.

Two guards on top of the ticket's instruction, both about "one careful write":
- **Bits 7:4 are never written back.** They are not the PWR field. A read that returns any of them (for example a
  bus answering 0xFF) is treated as "not ready": the attempt FAILS CLOSED, logs the byte and writes nothing
  (`diag::powerReadSane`). Bit 3 is left to the chip and only preserved.
- **The write is only ever sent from one static function**, `usbPdWritePower()` in `board.cpp`, and it is reached
  from `usbPdPowerBegin()` in `boardBegin()` (after the display init re-owns the I2C bus) with **one retry 20 ms
  later** if the chip was not ready. Then POWER is read back and the log is exactly one line:
  `[pmu] measure block on (POWER=0x07)` or `[pmu] measure block FAILED (<why>)` (a failed first attempt logs
  `[pmu] measure block not ready (<why>), retrying` in between). "Accepted" is never claimed from the write's return
  code: the read-back must carry the three bits (`diag::powerBitsPresent`).

**Never written: SWITCHES0 (0x02) and every CC, termination and PD register.** §`test_est401.py` proves it from the
sources, not from a comment: every I2C transmission to 0x22 in `src/` is classified (1 byte = a register pointer for
a read, 2 bytes = a real write), and exactly ONE two-byte transmission exists, its register byte is `diag::kRegPower`,
its data byte is that helper's parameter, and the helper is file-static.

**Where the bit names come from (state of verification).** The register map is confirmed from a source on this
machine: the running kernel's own FUSB302 driver (`fusb302.ko`, decompressed and disassembled) does
`i2c_smbus_write_byte_data(0x0B, 0x0F)` at init and `0x0C = 0x01` for SW_RESET, so 0x0B is POWER and 0x0C is RESET,
not the other way round. The **individual bit names are not verified against a datasheet in my reach** (no network,
no datasheet in the sandbox, and the driver writes all four bits at once so the binary cannot separate them): they are
the ticket's three groups in the order the measured 0x01 supports (bit 0 = the bandgap that is already on). If the
device test shows VBUSOK still 0 with a cable, the documented next try is **0x0F** - the byte the kernel driver
writes, i.e. the three bits plus bit 3 - one line in `diag_logic.h` (`kPowerMeasureOnMask`), and the OR then carries
whatever the chip reports for bit 3 anyway, so a chip that really needs bit 3 would already survive it if the
hardware sets that bit itself.

### 2. The minute line: four appended columns

`/log/power-YYYYMMDD.csv` gains `vbus,ain1,ain2,ain3` **appended after `uptime_s`**, so every position a reader
already used keeps its meaning (16 columns now):

```
time,batt_mv,screen,brightness,gps,fix,sats,wifi,ble,pkts,noise,uptime_s,vbus,ain1,ain2,ain3
```

- `vbus` = STATUS0 (0x40) bit 7 VBUSOK read once a minute: **1** present, **0** absent, **-1** the I2C read failed.
  The -1 matters: an unreadable chip must not look like "no cable" in the evidence.
- `ain1..3` = the ADS1115's AIN1, AIN2, AIN3 at the pin in mV, same scale as the battery read. 0 = that channel's
  read failed (the measured values on USB were 3285 / 576 / 575 mV).
- AIN0 is NOT read here: `batt_mv` stays the number the status bar shows. The three channels cost about 55 ms of I2C,
  once a minute, in `plog::loop()` on the loop task, next to the radio counters (the EST-349 rule is untouched: the
  loop is still the only card writer).
- `log power tail` needed no change and already follows: it prints the file's lines verbatim (header counts as a
  line). `pmu probe` also needed no change and now shows `vbusok=1` with a cable, because the measure block is on.

### Files

- `src/diag_logic.h/.cpp`: POWER bit constants + `kPowerMeasureOnMask` (0x07), `powerValueToWrite()`,
  `powerReadSane()`, `powerBitsPresent()`, `status0VbusOk()`; `fusb302Fields()` now uses `status0VbusOk()` for VBUSOK
  (one bit test, not two).
- `src/board.h/.cpp`: `usbPdPowerBegin()`, the static `usbPdWritePower()`, `usbPdMeasureAttempt()`, `usbPdStatus0()`,
  `adcAuxProbe()` (AIN1..3 only); the boot call at the end of `boardBegin()`.
- `src/power_log_logic.h/.cpp`: the four `Sample` fields, the header, the format string.
- `src/power_log.cpp`: the once-a-minute STATUS0 + ADC reads and the four values in the `[plog] write ...` line.
- `test/host/test_est401.cpp` (new), `test/host/test_est401.py` (new), `test/host/run.sh` (wired, `BIN56`),
  `test/host/test_est377.cpp` (its exact CSV line and its comma count moved from 12 to 16 columns - the ticket
  changes the CSV, so that expectation had to move with it).

### Build and tests (real exit codes, this tree)

- `PLATFORMIO_CORE_DIR=/home/slofi/.cache/pio-om-lite/core pio run -e om_lite_l2`: **SUCCESS**, exit 0, no warning.
  **RAM 160,400 / 327,680 B (49.0 %; unchanged)**, **Flash 2,990,089 / 6,553,600 B (45.6 %; before 2,988,985,
  +1,104 B)**, `firmware.bin` 2,990,448 B. RAM is flat because only the stack copy of `Sample` grew; the constants
  are compile-time.
- `test/host/run.sh`: **exit 0**, all suites green (log: 0 lines containing FAIL). Two new halves:
  `test_est401.cpp` (the three bits and 0x01 -> 0x07, bit 3 preserved, 0x10/0x81/0xF0/0xFF refused, the read-back
  rule, the STATUS0 bit-7 decode against fixed bytes and against `fusb302Fields`, the address/register map, and the
  four CSV columns with `-1` for a failed STATUS0 read) and `test_est401.py` (the source scan above, the boot call
  being in `boardBegin()` after the display init, the read-before/read-after/verify shape, the exact `[pmu]` line, and
  the once-a-minute wiring of STATUS0 + the ADC).
- **The scan can fail:** checked on a doctored snippet with a second two-byte transmission to 0x22 (`Wire.write(0x02);
  Wire.write(0x03)`) - it is classified as a write with register 0x02 and the test then fails on it; a commented-out
  write is ignored.
- `test_est377.cpp` is the one existing test whose expectation changed (12 -> 16 columns), and it changed because the
  ticket changes the CSV.

### Device test plan (for Claude, USB and a battery)

Everything below is on the battery log, one line per minute, `vbus` + `ain1..ain3`. Flashing and screenshots stay
yours. **Have the log switched on** (`log power on` writes the NVS key; the default is on) and a card inserted.

1. **Battery only, 3 minutes** (unplugged, screen state as it is): expect `[pmu] measure block on (POWER=0x07)` at
   boot and `vbus=0` on every line. Record `ain1..ain3`: this is the baseline the cable must move.
2. **A phone charger or a power bank with no data, 3 minutes** (a dumb 5 V source, no USB host): expect `vbus=1` on
   every line. That is the half that says "USB power present" without a data connection - the solar/charger case.
   (If the app is off/Hung, the chip keeps the measure block on until the next boot; nothing here needs the CPU.)
3. **USB to TestBox, 3 minutes**, then `log power tail` and `pmu probe`: expect `vbus=1`, `power=0x07`, `vbusok=1`,
   `bc_lvl` naming the port's current level, and `batt_mv` rising over the three minutes (the "charging" half).
4. **Back to battery for 3 minutes**: expect `vbus=0` again - the flag must FOLLOW the cable, both ways, or there is
   no bolt.

Read it as: `vbus` 1 with a cable in all three supply shapes and 0 on battery = a real signal. Then step 2 of the
bolt (a status-bar icon plus "charging" vs "full" from the voltage trend) has something honest to draw.

### What I could not check, and what is left

- **No device.** I cannot see the chip, so the value that matters (does VBUSOK really follow the cable with the
  measure block on) is exactly what step 1 of the plan above decides. If 0x07 is not enough, the fallback is 0x0F
  (see §1); say so and it is a one-line change plus the mask test.
- **Whether the chip answers at 0x22 on battery at boot.** If it does not, the boot log says
  `[pmu] measure block not ready (...)`, retries once, and may end in `FAILED`; the minute lines then carry
  `vbus=-1`. That is not a bug, it is the measurement: the battery-only phase is where that will show. (A later
  retry on the first USB insert is NOT in this ticket and is not built.)
- **The measure block's current draw** is not measured. PWR1/PWR2 are two small internal blocks; the device test's
  battery-only night is what would show it if it matters.
- Nothing in the UI changed: no bolt, no icon, no string. The status bar and the screens do not read the new columns.
- Next step (after the device test says the signal is real): the bolt in the status bar, with "charging" only when
  the cell voltage is also rising (and `bc_lvl` as the port's claim, never as proof).

## 2026-11-11 19:51 (DS, EST-402): L2 Pro firmware, the charging bolt in the status bar (step 2)

Status: in_review (not committed, not pushed).

DeepSeek (Paperclip worker). Step 2 of the charging bolt. The signal was proven on the device on
2026-11-11 19:26-19:33 (Claude + Filip, battery log): on battery vbus=0 (4150 -> 4148 mV), on a
power-only 100 W charger vbus=1 (4188-4190 mV), on USB to the PC vbus=1; AIN1..AIN3 did not move.
STATUS0 (0x40) bit 7 VBUSOK is the one signal, and it means "external power is present", nothing more.

### 1. The poll: one read of 0x40 every 2 s, two equal reads decide

- `src/charge_state.h` (new, plain C++): `kChargePollMs = 2000`, `VbusRead {Absent, Present,
  Unavailable}`, `VbusState {Unknown, Absent, Present}`, and `ChargeBolt`. `sample()` adopts a new
  value only after TWO equal reads in a row (`streak_ < 2`); one bad read neither raises nor drops
  the bolt.
- `main.cpp`: `g_chargeBolt.begin(om::usbPdReady())` in setup; the loop calls
  `om::usbPdVbusPresent(&present)` when the 2 s gate is due and feeds Present / Absent /
  Unavailable. One read, no write. A failed read is Unavailable, never "no cable". A state change
  asks for one repaint (`redrawCurrentScreen()`), and only when the screen is on and no change owns
  the frame.
- `board.cpp/.h`: `usbPdReady()` returns the boot result; `usbPdVbusPresent()` decodes STATUS0
  through the existing `usbPdStatus0()` read and `diag::status0VbusOk()`. No new I2C transmission
  exists: test_est401.py still finds exactly one write to 0x22 (POWER) and one read block.

### 2. Unknown is no bolt, logged once

`usbPdPowerBegin()` now keeps its result in a static (`s_usbPdReady = ok`, after the one retry and
only when the read-back carries the three PWR bits). If the chip did not answer or the POWER write
was not verified, `begin(false)` latches Unknown forever: the loop does not even read, and no sample
can raise a bolt. The failure branch prints one line, `[pmu] charging bolt off (power flag unknown)`;
there is no second copy (the source test counts it across `src/`). A poll read that fails mid-run is
Unknown too: two consecutive failures drop the bolt, and this build does not log that episode (only
the boot failure is logged, as the ticket asks).

### 3. The drawing: a 5 x 7 gold bolt inside the battery icon

- `theme.h`: `kBoltW = 5`, `kBoltH = 7` (the icon's own height), centred on the 16 px battery.
- `ui/battery_bolt.h` (new): the seven pixel runs (2,2,2,5,2,2,2 px), one source for the panel and
  the preview. Integer `fillRect` rows, so it is crisp at 1:1; no anti-aliased mask.
- `ui/widgets.cpp` `drawBolt()`: gold `kAcc` rows. Gold on the green fill measures about 1.1:1, so
  each interior row gets a 1 px `kBar` edge (the run expanded 1 px sideways and 1 row up/down is a
  1 px dilation) and the gold is drawn over it. The two outer rows sit on the icon's outline. This
  stays readable on `kGreen`, `kWarn` and `kBad`, and on the empty fill when the level is low.
- `drawBattery()` now takes `bool externalPower`; the bolt is drawn even when the percentage is
  unknown (the port can answer before the ADC reads). The percentage and every rectangle are
  unchanged: `statusBarRects()` was not touched, so nothing moves and no tap target changes.
- The code comment says it: external power present, NOT "charging vs full". The cell stays about
  4.19 V in both states and BC_LVL is the port's claim, so no guess is made (ticket item 3).

### Files

- `src/charge_state.h` (new), `src/ui/battery_bolt.h` (new).
- `src/main.cpp` (include, g_chargeBolt, buildStatus, setup begin, loop poll).
- `src/board.h/.cpp` (usbPdReady, usbPdVbusPresent, s_usbPdReady, the one boot line).
- `src/ui/theme.h` (kBoltW, kBoltH), `src/ui/widgets.h/.cpp` (externalPower, drawBolt).
- `test/host/test_est402.cpp` + `.py` (new), `test/host/run.sh` (BIN57).
- `tools/mkest402.py` (new), `docs/shots/2026-11-11-est402-charging-bolt.png` (preview).

### Build and tests (real exit codes, this tree)

- pio run -e om_lite_l2: **SUCCESS**, exit 0, no warning in our files (the two RadioLib `#warning`s
  are pre-existing). **RAM 160,416 / 327,680 B (49.0 %; before 160,400, +16 B)**, **Flash 2,990,593
  / 6,553,600 B (45.6 %; before 2,990,089, +504 B)**, `firmware.bin` 2,990,960 B.
- `test/host/run.sh`: **exit 0**, every suite green, 0 FAIL lines.
  - `test_est402.cpp`: one read is not a decision; two equal reads raise/drop the bolt; a single bad
    read does not flicker; two Unavailable reads become Unknown and hide the bolt; flapping never
    raises it; `begin(false)` refuses 100 Present reads; `begin` resets to Unknown; the 5 x 7 shape
    is connected, every row non-empty, 17 gold px, inside the icon.
  - `test_est402.py`: the 2 s constant, one STATUS0 read in loop(), the readiness from the real boot,
    the single boot log across `src/`, the drawing under `externalPower`, the theme colours, and no
    BC_LVL / charge-level word in code.
- The preview is a host render of the firmware's own rectangles and the shared bolt runs, not a
  screenshot: two full status bars (78 %, plain and with the bolt) and three 8x icon crops (plain,
  bolt at 78 %, bolt at 12 %).

### What I could not check, and what is left

- **No device.** Nothing here was seen on the glass. On the panel, please check: (a) the bolt is
  readable inside the green fill at 100 % and on the yellow/red fills; (b) it is centred and does
  not touch the terminal nub; (c) with a cable in, the bolt appears within about 4 s (two 2 s polls)
  and disappears within about 4 s after unplugging; (d) `pmu probe` / the minute log still agree
  (`vbusok=1` with a cable).
- **The I2C cost** of one STATUS0 read every 2 s is not measured. It is one byte read, but the loop
  histogram will show it if it matters.
- **A mid-run failed read is not logged.** Only the boot failure prints the one line (ticket item 4);
  two consecutive failed polls hide the bolt silently. Say so if a log for that episode is wanted.
- **Charging vs full is deliberately not guessed.** If Filip wants it later, it needs a real signal
  (a voltage trend over minutes, with BC_LVL as the port's claim only); that is a new ticket.

Next step: Claude flashes over USB OTA and takes the screenshots; the device check above is the
review. Nothing else is pending in this ticket.

## 2026-11-11 20:25 (DS, EST-403): L2 Pro firmware, a real clock across a warm restart
Status: in_review (not committed, not pushed).

DeepSeek (Paperclip worker). Filip, 2026-11-11 ~19:40: "the time is not set on L2 again" after a
flash. After a restart the timekeeper waits for a real source (GPS, Wi-Fi, phone, manual); indoors
with Wi-Fi and Bluetooth off there may be none for a long time, and the battery log then runs about
4 minutes behind. This ticket keeps the last REAL epoch in RTC_NOINIT memory and restores it after a
software/OTA/panic/watchdog restart.

### 1. First the framework question: does gettimeofday() survive esp_restart()?

**No. The hardware RTC counter survives, the wall clock does not.** Checked in this tree's framework
(`framework-arduinoespressif32` 3.20017.241212 = Arduino 2.0.17, `tools/sdk/versions.txt`: esp-idf
v4.4.7 38eeba213a):

- `libnewlib.a:esp_time_impl.c.obj` (disassembled) implements `esp_time_impl_init()` ->
  `esp_set_time_from_rtc()`, and that function computes
  `s_microseconds_offset = esp_rtc_get_time_us() - esp_system_get_time()`.
  `esp_time_impl_get_time_since_boot()` is `esp_system_get_time() + s_microseconds_offset`.
- `libnewlib.a:time.c.obj`: `_gettimeofday_r()` calls `adjust_boot_time()` and then
  `esp_time_impl_get_time_since_boot()`, dividing the microseconds into `tv_sec`/`tv_usec`;
  `settimeofday()` turns its `timeval` into `esp_time_impl_set_boot_time()`.
- The epoch offset is ordinary DRAM BSS: `s_microseconds_offset` is at **0x3fcb5830 in
  `.dram0.bss`** in the built `firmware.elf` (readelf). BSS is zeroed on every boot.
- `esp_newlib_time_init()` (which calls `esp_time_impl_init()`) is called from `start_cpu0` on every
  boot (the ELF's call at 0x42084b08, right after `esp_brownout_init()`). So every boot re-derives
  the offset from the RTC counter.
- The RTC counter state itself IS kept in RTC noinit: `s_rtc_last_ticks` and `s_esp_rtc_time_us` are
  in `.rtc_noinit` (0x50000048 / 0x50000050 in this build) and `esp_rtc_get_time_us()` reads the
  RTC_CNTL 48-bit counter. That is the one clock that keeps counting through a software restart and
  stops at a real power-on/brown-out.

Net effect after `esp_restart()`: `gettimeofday()` returns the RTC counter (time since the RTC
domain powered up), not the UTC epoch. The clock is continuous but not real. **Path taken: keep the
epoch + the RTC counter in RTC_NOINIT and reconstruct the epoch at boot** (the ticket's second
option), never trusting `gettimeofday()` alone.

### 2. The record and the boot decision

- `src/clock_keep.h` (new, plain C++, host-tested): `ClockKeepRecord {magic, epoch, rtcUs, source,
  real, reserved}` and the one `clockKeepDecide()`. The reset-reason input is EST-387's own table
  (`gps_boot.h`, `gpsBootResetsReceiver()`), so the two cannot drift: Software/External/Panic/
  IntWdt/TaskWdt/Wdt/Sdio may keep; PowerOn/Brownout/DeepSleep/Unknown never do. On KEEP with a valid
  record the restored epoch is `record.epoch + (rtcNow - record.rtcUs) / 1e6`; it must be sane
  (2025-01-01 .. INT32_MAX, the clock's one window) and >= the floor. A bad magic, `real != 1`, an
  unknown source, an insane saved epoch, an RTC counter that went backwards, and a restored epoch
  below the floor all stay at "no time" as today.
- `src/clock_keep.cpp` (new): the record is `RTC_NOINIT_ATTR` at 0x50000000, 24 bytes in
  `.rtc_noinit` (`.rtc_noinit` goes 0x40 -> 0x58; it does not overlap `s_ckpt` at +0x18 or the
  framework's `s_esp_rtc_time_us` at +0x50). The magic is written LAST (fields first, compiler
  barrier, magic last), so a panic/power cut mid-save leaves a refused record. `clockKeepBegin()`
  reads `esp_reset_reason()` and the record; on a RESET reason it also clears the magic, so a later
  crash restart in the same power session can never resurrect a pre-power-off record.
- `src/timekeeper.h/.cpp`: new `TimeSource::Kept` (`timeSourceName()` = "kept", so `status` prints
  `clock=kept`), `TimeKeeper::setKept()` (real at once, remembers the original source), `keptFrom()`,
  and an optional `KeepFn` callback called on EVERY accepted source, including same-second refreshes
  that need no clock write, so the record's RTC counter is fresh. `setFromSource(..., Kept)` is
  refused: only the boot decision may set it.
- `src/main.cpp`: `begin(..., &om::clockKeepStore)` and, right after the floor line,
  `clockKeepBegin(floor)` -> `setKept()` -> `[time] kept across restart (was <source>)`.
- Before each restart we control: `ota.cpp` (after `Update.end()`, before `ESP.restart()`),
  `target.cpp` `OmBoard::reboot()`, and `mesh_link.cpp`'s crash-guard retry each call
  `clockKeepStoreNow()`.

**Priority, and a deliberate reading of the ticket.** `kept` has rank 0, below gps-nofix (1): every
fresh GPS fix, Wi-Fi, phone, manual or gps-nofix time replaces it immediately. The ticket says
"priority just below its original source (so any fresh GPS fix / Wi-Fi / phone time replaces it
immediately)"; with the 6-hour hold in `maySet()`, a rank just below the original would still block
a fresh PHONE time behind a kept Wi-Fi record for six hours, which contradicts the parenthetical.
The original source is therefore remembered (`keptFrom()`) for the log and for `approximate()` (a
clock kept from a gps-nofix record is still approximate), but it never outranks a fresh observation.

### Files

- `src/clock_keep.h`, `src/clock_keep.cpp` (new).
- `src/timekeeper.h`, `src/timekeeper.cpp` (Kept, setKept, keptFrom, KeepFn, realTimeSource).
- `src/main.cpp` (include, begin callback, restore + one log line).
- `src/ota.cpp`, `src/target.cpp`, `src/mesh_link.cpp` (one `clockKeepStoreNow()` each).
- `test/host/test_est403.cpp` (new), `test/host/run.sh` (BIN58).

### Build and tests (real exit codes, this tree)

- `pio run -e om_lite_l2`: **SUCCESS**, exit 0. **RAM 160,416 / 327,680 B (49.0 %; unchanged)**,
  **Flash 2,991,529 / 6,553,600 B (45.6 %; before 2,990,593, +936 B)**, `firmware.bin`
  2,991,888 B. No warning in our files (the two RadioLib `#warning`s are pre-existing).
- `test/host/run.sh`: **exit 0**, every suite green. `test_est403.cpp` covers: every KEEP reason
  keeps a good record and every RESET reason refuses it; unknown SDK numbers map to Unknown/RESET;
  bad magic, `real` 0/2/255, source None/kept/200; saved epoch 0 / below 2025 / at INT32_MAX / above
  it; a restored epoch crossing INT32_MAX; an RTC counter that went backwards; elapsed 0.999 s /
  1 s / 3.9 s truncation; floor equal vs one second above; `kept` loses to all five fresh sources;
  `setKept` refusals; `keptFrom` and the approximate rule; and the KeepFn calls (including the
  same-second refresh) carry the ORIGINAL source, never "kept".
- The existing timekeeper/NTP/GPS suites still pass with the new `TimeSource` value and the extra
  optional `begin()` argument.

### What I could not check, and the device test

- **No device.** The RTC counter's continuity across `esp_restart()` is from the framework's own
  time code and the linker placement above, not observed on the glass. The end-to-end check is
  Claude's:
  1. Set the time via Wi-Fi (`ntp now` or the switch), confirm `status` says `clock=Wi-Fi`.
  2. Flash the same image over USB OTA (a SOFTWARE reset; an EN tap / power cycle is a reset that
     must NOT keep).
  3. Right after the boot, the log should say `[time] kept across restart (was Wi-Fi)` and `status`
     should say `clock=kept` (the status bar shows HH:MM instead of `--:--`). A GPS fix or a fresh
     NTP reply then takes over: `clock=GPS` / `clock=Wi-Fi`.
  4. Power-cycle (or tap RESET): no kept line and `--:--` until a real source, as before.
- A watchdog/panic restart is not forced here. The same record is used; the RTC counter advances by
  the real elapsed time, so it should recover too, but that path was not seen on the device.
- The RTC slow-clock drift between the last source and the restart is not measured. It is the same
  counter the framework uses for `gettimeofday()`, and the window here is seconds to hours, not
  months.

Next step: Claude flashes over USB OTA and runs the four device checks above; the kept line and
`clock=kept` are the review. Nothing else is pending in this ticket.

## 2026-11-11 20:46 (DS, EST-404): L2 Pro firmware, a proper charging bolt over the battery icon

Status: in_review (not committed, not pushed).

DeepSeek (Paperclip worker). EST-402 drew a 5 x 7 bolt INSIDE the 16 x 7 battery icon, and Claude's
screenshot showed what Filip saw on the device at 20:00: both tips were cut by the frame, so the bolt
looked clipped and bad. This ticket draws the bolt last, over the icon (fill and frame), with the top
and bottom tips standing clear of it.

### 1. The shape and the drawing

- `src/ui/battery_bolt.h`: the pixel rows are now **7 x 11** instead of 5 x 7, in the same one source
  the panel and the preview share. The silhouette is the classic bolt: the top tip is at the upper
  right (row 0 is `....###`), the upper edge steps left into a one-row middle bar (`#######`, row 5),
  then the lower edge starts at the bar's right end and steps left again to the bottom tip at the
  lower left (`##.....`, row 10). Every row is non-empty and 8-connected to the one above it; 37 gold
  pixels in all.
- `src/ui/theme.h`: `kBoltW` 5 -> 7 and `kBoltH` 7 -> 11. 7 of the 16 px body is the middle **44 %**,
  inside the ticket's 40-50 %; 11 is **4 px taller** than the 7 px body, so `(kBoltH - kBattH) / 2`
  is exactly the ticket's **2 px** above and below.
- `src/ui/widgets.cpp` `drawBolt()`: `by = y - (kBoltH - kBattH) / 2`, so the tips stand 2 px above
  and below the frame. The 1 px `kBar` edge is now drawn for **every** row (the old code skipped the
  first and last, because they sat on the outline); the diamond of row edges is exactly a 1 px
  dilation, and all gold rows are drawn afterwards, so the bolt is the last thing over the fill, the
  frame and the nub side. Gold fill and the "external power present" meaning are unchanged.
- `drawBattery()` still calls it after the frame, the nub and the fill, and only under
  `externalPower`; the debounce and the "unknown = no bolt" rule from EST-402 are untouched.

### 2. Does the status bar have room? Yes, checked by arithmetic and by a test

- The bar is 26 px (`kStatusH`), the icon is centred at cy = 13, so its rows are 10..16. The bolt's
  rows are 8..18, and with its 1 px edge 7..19. All of that is inside 0..25: **the 2 px above and
  below need no layout change**, and `statusBarRects()` was not touched.
- In x the edge stays inside the icon's 16 px (bolt box x 4..10, edge 3..11), short of the terminal
  nub at 16..17. The percent text starts 6 px right of the icon and the GPS cell ends 6 px left of
  it, so neither can be reached.
- `test/host/test_est404.cpp` proves it on the **worst-case row**: clock, `100%` (4 glyphs), `GPS 14`
  (6 glyphs) and every optional icon on. The bolt's edge rectangle overlaps none of `battText`, `gps`,
  `gear`, `time`, `fault`, `dnd`, `wifi` or `bt`, and it is inside the 26 px bar.

### 3. Readable on the red low fill and the green full fill

Gold alone is weak on the fills (measured WCAG ratios: gold/green 1.07:1, gold/warn 1.07:1,
gold/red 1.55:1), which is why the dark edge carries the shape. The edge is `kBar` and measures
9.87:1 against green, 9.89:1 against warn, 5.97:1 against red and 6.83:1 against the grey frame, so
the gold is separated from whatever is under it. Every row keeps at least 2 px of gold and a >= 4 px
dark outline. The 8x preview below shows 12 % (red) and 100 % (full green) side by side.

### Files

- `src/ui/battery_bolt.h`, `src/ui/theme.h`, `src/ui/widgets.cpp`.
- `test/host/test_est402.cpp` (the shape test now expects 7 x 11, 37 px, tip direction),
  `test/host/test_est404.cpp` (new), `test/host/run.sh` (BIN59, EST-402 comment).
- `tools/mkest404.py` (new), `docs/shots/2026-11-11-est404-bolt-tips.png` (preview).

### Build and tests (real exit codes, this tree)

- `pio run -e om_lite_l2`: **SUCCESS**, exit 0. **RAM 160,416 / 327,680 B (49.0 %; unchanged)**,
  **Flash 2,991,497 / 6,553,600 B (45.6 %; before 2,991,529, -32 B)**, `firmware.bin` 2,991,856 B.
  No warning in our files (the two RadioLib `#warning`s are pre-existing).
- `test/host/run.sh`: **exit 0**, every suite green, **0 FAIL lines**.
  - `test_est404.cpp`: the box is 4 px taller than the body with 2 px overhang each way and 40-50 %
    of the width; the tips are outside the body; the edge is inside the 26 px bar and inside the
    icon's width; the worst-case row is not overdrawn; every row has >= 2 px gold and the middle bar
    is the only full-width row.
  - `test_est402.cpp` still proves the debounce and "unknown = no bolt"; only its shape assertions
    moved to 7 x 11 (37 gold px, tip direction, middle bar on the body's centre row).

### Preview

`docs/shots/2026-11-11-est404-bolt-tips.png` is a host render of the firmware's own rectangles, the
real theme colours and the shared bolt runs, at **1x** and **8x**, in four states: plain, bolt at
78 %, bolt at 12 % (red fill) and bolt at 100 % (full green fill). The icon is drawn straight onto the
bar, so the tips outside the frame are visible exactly as on the panel. It is not a screenshot.

### What I could not check, and the device test

- **No device.** Nothing here was seen on the glass. Please check on the panel: (a) both tips are
  fully visible above and below the frame and are not cut by the status bar; (b) the dark edge keeps
  the gold crisp over the green, amber and red fills; (c) the percent text and the GPS cell are
  untouched; (d) the bolt still appears within about 4 s of plugging in and leaves within about 4 s
  of unplugging (the EST-402 poll was not changed).
- The 0.5 px horizontal off-centre is inherent to centring a 7 px box in 16 px (`(16 - 7) / 2 = 4`);
  it is the same convention the old 5 px bolt used and was not worth an odd 1 px shift.

Next step: Claude flashes over USB OTA and takes the screenshot; the tips and the edge on the glass
are the review. Nothing else is pending in this ticket.

## 2026-11-11 21:15 (DS, EST-405): L2 Pro firmware, optional screen auto-off

Status: in_review (not committed, not pushed).

DeepSeek (Paperclip worker). Filip, 2026-11-11: screen auto-off "should be a toggleable feature with
adjustable time-till-off; a good-to-have but not essential, since there is a convenient wake button.
I generally do not like auto screen-off features." So: DEFAULT OFF, a missing NVS key is OFF, the
time row appears only while it is on, and with the switch off nothing below runs at all.

### 1. The switch and the time

- Settings > General > DISPLAY: Brightness, then **Screen auto-off** (toggle, value OFF in the dimmer
  ink, ON in green), then, ONLY while it is on, **Auto-off after** with the current time. The default
  page therefore has exactly the rows it had before this ticket.
- A tap on the time row opens a sub-page with five Check rows, the chosen one dotted: 30 s, 1 min,
  2 min, 5 min, 10 min, from `om::kIdleTimes` so the label, the milliseconds and the order cannot
  drift. Picking stays on the page (the dot moves), Back returns to General at its scroll. The page
  cannot be reached while the switch is off because its row is not built.
- First switched on: the time is the stored one, and a fresh install has no key, so it reads as the
  1 min default. Nothing is written just because the switch went on (or because a boot loaded it).
- NVS, namespace "omlite": `saoff` (one byte, 0/1) and `saoft` (one byte, 0..4). Both are read with
  one key-generic u8 callback pair (the same shape as the map layers' and the battery log's bool
  pair). A set writes ONLY when the value really changes; the host test's fake NVS counts the writes
  to prove it. A value out of range in NVS falls back to 1 min, and a non-zero switch byte is ON.

### 2. The timer (`src/screen_idle.h`, new, plain C++)

`ScreenIdle::step(nowMs, screenOn, blockers)` runs once per loop pass and returns true on the ONE
pass that must sleep the screen. Its rules, in order:

1. the screen is on and the switch is on, or nothing happens;
2. a blocker (see 4) or a USB command inside its 30 s window holds the count at zero;
3. otherwise `now - lastActivity >= timeout` sleeps it.

`onActivity()` (a touch sample with the finger down, or a wake press) and `onUsbCommand()` restart
the count. Everything is `uint32_t` subtraction, so a `millis()` wrap is one of the tested cases.

**Judgment call A, a blocker PAUSES the count.** The ticket says "do not turn the screen off while
X". Holding the count at zero while X is up, instead of only postponing the moment, means that when
the keyboard closes or the pop-up goes the user gets the WHOLE time again. The alternative would
sleep the screen the instant a five-minute-old pop-up closes, which is the one behaviour nobody
asked for. The test checks both halves for every blocker: no sleep however long it is blocked, and a
full timeout after it lifts.

**The pop-up counts as a blocker (rule 3).** A message pop-up has to keep the screen on for its 5 s
when the screen was on, so a visible toast holds the count too. A pop-up that arrives on a screen the
timer already slept never reaches this code (`step()` is a no-op while the screen is off) and the
notice door already refuses it with `screen-off`, so a message still cannot wake an auto-off screen:
the wake button and the chime are the alerts, as the ticket says.

### 3. One sleep path and one wake path

The wake button's two transitions moved out of `loop()` into `screenSleep(why, fromTimer)` and
`screenWake(why)`, and the state they own (`g_screenOn`, `g_sleepPending`, the press edge, and the
three gesture flags the sleep drops) moved from loop locals to file-scope statics. The bodies are
the old code, not a second copy: the timer and the button both end in the same fade-out over
`kFadeOutMs` (150 ms), the same panel sleep after the light reaches 0 (the `g_sleepPending` tick),
and the same wake order (panel wakeup, DISPOFF, sleep-out, the frame push, DISPON, one frame period,
then the backlight ramp, with the TouchGate armed). The auto-off calls `screenSleep("idle", true)`;
the button calls `screenSleep("button", false)` / `screenWake("button")`.

### 4. Touch wakes only a screen the TIMER slept (a deliberate reading, please review)

The ticket says in rule 2 "any touch while it is off only wakes (consumed, never presses what is
under it), like the wake button today", and in rule 4 "when off (default), nothing changes from
today". Today a touch on a dark screen does nothing at all: only the wake button brings it back.
Read together, rule 2 is about the screens this feature creates, so a touch now wakes a screen the
AUTO-OFF put to sleep and nothing else: `g_sleepByTimer` is the one difference, and a screen slept
with the button still needs the button. That keeps rule 4 literally true (with the switch off, and
for button sleeps, the behaviour is the old one) and avoids a pocket tap waking the panel.

- The dark glass is polled every `kIdleTouchPollMs` (40 ms), not every pass, so the GT911 read stays
  off the hot path.
- The wake arms the SAME gate with `downAtWake` true, so the waking press is swallowed until it
  lifts and can never also press the row under it. This is the tested TouchGate rule, unchanged.
- If Filip would rather have a touch wake a button-slept screen too, it is dropping
  `g_sleepByTimer &&` from the one condition in `loop()`. I did not do it because rule 4 says the
  default must not change.

### 5. The blockers, from the shell's own state

- **Keyboard/composer:** `g_kb.open` (the chat composer, the DM composer and the message sheet's
  REPLY/COPY/MENTION all use the one sheet), `g_rptrOsk`, and any Settings text editor.
- **Pop-up/confirmation:** the Clear-messages confirmation, the chat selector and the ADVERT
  dropdown, the Position choice, the message sheet, the node sheet and the trace sheet, the RPTR
  confirmation, and both map pickers (the own-position picker and the RPTR one: the user is choosing
  a point, not idling).
- **OTA:** `om::otaActive()`.
- **Map drag/zoom:** `g_drag` or `om::mapViewStale()` while the Map is on screen (a stale canvas is a
  pan, zoom or recentre whose tiles are still decoding), plus the two pickers' own drag flags.
- **USB command:** `om::otaCommandSerial()` (new: a monotonic count bumped in `otaCommandPoll()` when
  a command is EXECUTED, not when its frame arrives). The OMSHOT screenshot does not bump it; it is a
  frame read, not a command.
- **Pop-up toast:** `om::ui::toastState().visible()`.

### Files

- `src/screen_idle.h` (new): the times, `IdleBlockers`, `ScreenIdle`, `ScreenIdlePrefs`.
- `src/main.cpp`: the file-scope screen/gesture state, `screenSleep()`/`screenWake()`,
  `idleBlockersNow()`, the u8 NVS callbacks, the boot load, the DISPLAY rows, the time page, the
  three action handlers, the touch-wake poll, the USB command watch, and the once-per-pass step.
- `src/ui/settings_layout.h`: `SetPage::AutoOff`.
- `src/ota.h`, `src/ota.cpp`: `otaCommandSerial()`.
- `test/host/test_est405.cpp` (new), `test/host/run.sh` (BIN60).
- `test/host/test_est394.py`: ONE literal, `screenOn` -> `g_screenOn`, because the state that test
  checks moved to file scope. The check itself is the same.
- `tools/mkest405.py` (new), `docs/shots/2026-11-11-est405-auto-off-rows.png` (preview).

### Build and tests (real exit codes, this tree)

- `pio run -e om_lite_l2`: **SUCCESS**, exit 0. **RAM 160,480 / 327,680 B (49.0 %; before 160,416,
  +64 B)**, **Flash 2,993,097 / 6,553,600 B (45.7 %; before 2,991,497, +1,600 B)**, `firmware.bin`
  2,993,456 B. No warning in our files (the two RadioLib `#warning`s are pre-existing).
- `test/host/run.sh`: **exit 0**, every suite green, **0 FAIL lines**, `est405` ran and passed.
- `test_est405.cpp` covers: the five times and their labels in order with 1 min as the default; a
  fresh timer and a missing key both OFF, and a disabled timer never sleeping (an hour of passes);
  the exact timeout boundary and the restart right after a sleep; a touch, a wake press and a USB
  command each restarting the count; the switch off/on and a new time choice restarting it; out of
  range time indices refused; `millis()` wrapping for the count and for the USB window; `idleBlocked`
  for every field and every blocker alone (keyboard, pop-up, OTA, map gesture, toast) never sleeping
  however long it is up and needing the WHOLE time again when it lifts; the 30 s USB window on its
  own; and the store: missing keys write nothing, a set writes once, setting the same value again
  writes nothing, values reload, an out-of-range stored time falls back to 1 min, a failed write
  still flips RAM, no backend at all still loads defaults, and the saved ON + 2 min really sleeps at
  120 s from boot.

### Preview

`docs/shots/2026-11-11-est405-auto-off-rows.png`: two panels at 2x, the switch OFF (default, no time
row) and ON (green ON and "Auto-off after  1 min >" under it). The rectangles come from
`ui/settings_layout.h` (`settingsLayoutFor`, `settingsRowRect`, the stepper's keys) and the colours
from `ui/theme.h`, dumped by a small C++ program the tool compiles; the text is desktop DejaVu, not
the firmware's VLW faces, and the status and tab bars are drawn only as context. It is not a
screenshot.

### What I could not check, and the device test

- **No device.** Everything here is source, host tests and arithmetic. The one fact that needs the
  glass is whether the GT911 still answers while the panel is asleep: touch wake depends on it. If it
  does not, a touch will simply not wake and the button still will.
- Claude's device checks:
  1. Settings > General > DISPLAY shows "Screen auto-off  OFF" and no "Auto-off after" row; the boot
     log says `[idle] screen auto-off off, 1 min`.
  2. Turn it on: `[idle] screen auto-off on, 1 min`, the row "Auto-off after  1 min" appears, and
     opening it shows the five rows with the dot on 1 min.
  3. Leave the screen alone for about 61 s: `[wake] screen off (idle, fade out 150ms from 160)` and
     then `[wake] panel asleep`. The fade and the sleep must look like the button's.
  4. Touch the dark glass: `[wake] screen on (touch, finger held at wake), 300ms masked`, the frame
     comes back, and the touch does NOT press anything.
  5. Open the chat keyboard and wait 2 minutes: no sleep. Close it and wait the full time: it sleeps.
  6. A USB OTA upload: no sleep during it. A map drag and a zoom: no sleep during it.
  7. Run any command (`python3 tools/cmd.py status`): nothing sleeps for 30 s, and the full time
     starts after that.
  8. A message arriving on a dark screen: the chime plays, the screen stays off, the log says
     `[notice] skip(screen-off)`. On a lit screen the pop-up stays its 5 s.
  9. Sleep it with the wake button: a touch then does NOT wake it (rule 4, the reading in 4). A
     wake-button press does.
  10. Switch it off: the page is as before this ticket and nothing sleeps.

Next step: Claude flashes over USB OTA and runs the checks above. The review points are the two
judgment calls (a blocker pauses; touch wakes only a timer sleep), the GT911 read while the panel is
asleep, and whether the page's wording is what Filip wants.

## 2026-11-11 21:42 (DS, EST-406): L2 Pro firmware, Send moves from the keyboard onto the message input line

Status: in_review (not committed, not pushed).

DeepSeek (Paperclip worker). Filip, 2026-11-11: "Send sits right under Backspace, easy to send instead
of delete", option A of three: move Send onto the message input line, like on phones. So the chat's
keyboard keeps no Send, the space bar takes its room, and one gold send-arrow button ends the draft
line above the keyboard.

### 1. The keyboard: Send gone, space bar wider, fingers not re-trained

- The CHAT field's bottom rows are now five keys: letters `123, emoji, comma, space, dot` and 123
  `abc, emoji, space, comma, dot`. `KeyKind::Send` is not on any chat row.
- Send's `kKeyUnitWide` (22 units) went to the space bar: `kKeyUnitSpaceChat` = 62 on the chat field.
  The row is one gap shorter, so the keys before the space moved by at most 1 px (123 4..56 →
  4..57, emoji 58..111 → 59..112, comma 113..137 → 114..138); the space bar grew 96 → 149 px on the
  letters page, and the trailing keys moved into Send's old slot (dot 291..316 on letters; comma and
  dot 265..316 on the 123 page), still ending on the same right edge at x = 316.
- Backspace is EXACTLY where it was on both pages, verified against a dump of the pre-ticket tree:
  letters `[258,190,58,22]`, 123 `[263,190,53,22]`. Only the line ABOVE the keys grew (see 2), so the
  key rows keep their y.
- The Settings / RPTR editors are untouched: their field still gets its `Send` key (it is the sheet's
  commit key, drawn as OK / Log in / Apply / Send), their space stays 40 units and their draft line
  stays 18 px. `sheetHeight()` now takes the field for exactly this reason.

### 2. The send button on the message input line

- `ChatLayout::sendBtn`, at the right end of the CHAT draft line: exactly **30 x 26** (the ticket's
  minimum), `sendBtn.right() = screen - kOskPad`, `y` and `h` equal to the draft line's. It is one
  rectangle: `drawSendButton()` paints it and `sendButtonHit()` (inline in chat_layout.h) hits it.
- The chat draft line grew to the button's 26 px (`kSendBtnH`; the editors keep `kOskDraftH` = 18), so
  the button never overhangs the line. The sheet grows **8 px upward** only: the draft line's floor,
  the four key rows and their y are unchanged, and the message band is 8 px shorter while the sheet
  is open.
- The draft plate shrank by the button plus one `kOskKeyGap`: 312 → **280 px**. `drawDraft()` is
  unchanged and measures the tail inside that narrower plate, so the existing tail scroll still
  works and text cannot run under the arrow.
- The icon is drawn from primitives (a 2 px shaft and a solid triangular head), no font glyph. Bright
  state: `kSelBg2` plate, `kAcc` edge, `kAccInk` arrow. Dim state: `kKeyBg` plate, `kKeyLine` edge,
  `kDimmer` arrow. The EST-390 pressed tint is the shared overlay on `sendBtn`, exactly the hit rect;
  a disabled button is no press target at all (no pressed look, no action).

### 3. Empty and whitespace-only drafts

`sendEnabled(const char*)` in chat_layout.h is the ONE rule: null, empty or only spaces/tabs/CR/LF =
false. The drawing, `chatPressTarget()` and the shell all ask it. I also replaced `chatSend()`'s old
`g_kbText[0] == 0` guard with this same call: without that, a draft of only spaces would have passed
the old check and been sent from a dim button (the ticket says no action).

### 4. One send door

`ChatHit::SendDraft` is the only hit; `handleChatTap` calls `chatSendDraft()`, which every send
already went through (channel, DM, reply are the same call; the wire text and the reply mention are
untouched). Judgment call, please review: the **emoji page's own Send button is gone** (its
`EmojiPadLayout::send`, `EmojiPadHit::send`, `ChatHit::EmojiSend` and the `kEmojiSendW` drawing/const
were removed). Requirement 4 says the emoji page uses "this button", and the composer button stays
visible on the emoji page above the grid; the Recent strip now has its right space free. Nothing else
about sending changed (the same draft, budget, sounds, log lines and `g_kb.close()`).

Judgment call 2, please review: the compose line shown with the keyboard CLOSED (the 27 px row at the
bottom; `g.compose`/`g.field`) has **no** send button. The ticket names "the composer text line above
the keyboard", which is the sheet's draft line; adding a 26 px button to the closed row would mean
growing that row too, and tapping it still opens the keyboard (the existing door). If Filip wants the
button on the closed row as well, it is a second rect with the same `sendEnabled()` and
`chatSendDraft()`, one more small ticket.

### Files

- `src/ui/chat_layout.h`: `ChatLayout::sendBtn`, `sendEnabled()`, `sendButtonHit()`, the emoji page
  loses `send` / `EmojiPadHit::send`.
- `src/ui/chat_layout.cpp`: `sheetHeight(replying, field)`, the chat key rows, the draft line +
  send button, `sendEnabled()`, emoji layout without Send.
- `src/ui/theme.h`: `kSendBtnW` / `kSendBtnH` (30 / 26), `kKeyUnitSpaceChat` (62), removed
  `kEmojiSendW`.
- `src/ui/screen_chat.cpp`: `drawSendButton()`, drawing it on the draft line, `ChatHit::SendDraft`
  in the hit test and the press target, emoji Send drawing gone.
- `src/ui/screen_chat.h`: `ChatHit::SendDraft` replaces `EmojiSend`.
- `src/main.cpp`: the `SendDraft` tap, `chatSend()` asks `sendEnabled()`.
- `test/host/test_chat.cpp` and `test/host/test_est351.cpp`: the rows that pinned the old 6-key
  bottom row and the old draft width follow the new layout.
- `test/host/test_est406.cpp` (new), `test/host/run.sh` (BIN61).
- `tools/mkest406.py` (new), `docs/shots/2026-11-11-est406-send-button.png` (preview).

### Build and tests (real exit codes, this tree)

- `pio run -e om_lite_l2`: **SUCCESS**, exit 0. **RAM 160,480 / 327,680 B (49.0 %; unchanged)**,
  **Flash 2,993,433 / 6,553,600 B (45.7 %; before 2,993,097, +336 B)**, `firmware.bin` 2,993,792 B.
  No warning in our files (the two RadioLib `#warning`s are pre-existing).
- `test/host/run.sh`: **exit 0**, every suite green, **0 FAIL lines**, `est406` ran and passed.
  - `test_est406.cpp` proves: no `KeyKind::Send` on any chat row of either page; the five-key bottom
    rows and the space bar's 62 units; Backspace's exact pre-ticket rects on both pages (and the row
    2 rectangles equal to the old arithmetic); the keys before the space within 1 px of the old row
    and the row ending on the old right edge; the send button 30 x 26 at the line's right end, the
    same rect on the 123 page and with a reply bar; the draft plate stopping before it (no overlap);
    `sendButtonHit()` true on all four corners, the centre and a grid over the rect, false 1 px
    outside and false on the text plate; no button on a closed sheet or on any editor field; and
    `sendEnabled()` false for null / "" / spaces / tabs / CRLF and true for a letter, a dot, a UTF-8
    accented letter and an emoji.
  - `test_chat.cpp` and `test_est351.cpp` updated only where they pinned the old layout (row counts,
    draft width, the chat sheet's height); `test_est351.cpp` now also proves the editors' sheet is
    unchanged and the chat's key rows sit on the same y.

### Preview

`docs/shots/2026-11-11-est406-send-button.png`: two panels at 2x, the empty draft (dim grey arrow,
"message" placeholder) and the draft "hello" (bright gold arrow, gold caret). Rectangles and colours
are dumped from the firmware's own headers by a small C++ program the tool compiles; the keys are
drawn with the same shapes as `drawKeyCap()`. DejaVu text, not the VLW faces; the status bar, header
and the two invented messages ("Alice", "hello" - rule 9) are context only. It is not a screenshot.

### What I could not check, and the device test

- **No device.** Everything here is source, host tests and arithmetic. Please check on the glass:
  1. Open the chat keyboard: the bottom row reads 123, emoji, comma, space, dot and the space bar is
     about half the row. Backspace is where it was; the letters feel unchanged.
  2. The draft line ends in a dim arrow; the placeholder "message" and the caret are not under it.
  3. Type "hello": the arrow turns gold; the text stays left of it; a long draft scrolls its tail
     inside the narrower plate.
  4. Tap the gold arrow: the message sends, clears, the keyboard closes, the Sent sound plays, the
     `[mc] UI send:` line is the same as before. Try a DM and a reply too.
  5. Empty the draft (or type only spaces): the arrow is dim and tapping it does nothing at all
     (no send, no sound, no log line).
  6. Open the emoji page: no Send in its Recent strip, the composer arrow is still there and sends
     the picked emoji.
  7. Settings > a text editor: its keyboard still has the Send / OK key and its draft line is the
     old full-width 18 px one.
- The 8 px taller chat sheet is drawn upward into the message band; the newest message may be hidden
  behind the sheet while it is open (it is a bottom sheet, so that is expected, but please confirm it
  reads well).

Next step: Claude flashes over USB OTA and takes the screenshot; the two judgment calls (emoji page's
Send removed; no send button on the closed compose row) are the things to confirm with Filip. Nothing
else is pending in this ticket.

## EST-408 sweep: UI part 3 (map)

Read-only sweep of the two files EST-396 and EST-398 did not read in full, DeepSeek (Paperclip worker, 2026-11-12).
**No code was changed and nothing was committed.** Scope: `ui/map_trace.cpp` and `screens/map_tab.cpp`, both read in
full, plus the headers they own and the few callers needed as evidence.

**Counts: 12 findings: S1 0, S2 2, S3 1, S4 2, S5 7.** Ranked most severe first. Every finding quotes the real line;
"not verified" marks what I inferred. The two S2s are a route point that is cast to 16 bits without a clip (the trace
then draws at the wrong place and the tail blends pixels outside the frame buffer) and the Traces key reading the NVS
write's result as the new state. There is no SD, radio, socket, timer, I2C or allocation in either file, so classes 1,
4, 5, 6 and 7 of the hunt are clean here apart from what is listed; the model itself (`traceMatchHop`,
`traceBuildRoute`, `traceGeom`, `tracePointAt`, `traceSlice`, the queue) is well covered by `test_est368.cpp` and
`test_est394.cpp` and I found no arithmetic error in it.

### 1. S2 · ui/map_trace.cpp:173-175 · a projected route point is cast to int16 with no clip, so an off-screen hop wraps and the tail blends outside the frame buffer
```c
if (!project((double)route_[i].latE6 / 1e6, (double)route_[i].lonE6 / 1e6, x, y)) return true;
px[i].x = (int16_t)x;
px[i].y = (int16_t)y;
```
`project` is `om::mapProject` (`map.cpp:424-433`), which returns screen pixels in an `int` and never bounds them:
`x = (int)lround((lonToXf(lon, rz) * kTile - cpx) * scale + kViewW / 2.0);` with `scale = 2.0` at z18
(`map.cpp:426-428`, `kZMax = 18` at `map.cpp:41`, "18 == z17 magnified 2x"). At z18 the projection is 256 * 2^17 / 360
* 2 pixels per degree of longitude, about 186,000, so a hop 0.18 deg of longitude from the centre (about 14 km in mid
latitudes) sits at +-33,500 px and anything further wraps to the opposite sign. `build()` guards the MARKERS with
`x < -4 || x >= ui::kMapCanvasW + 4` (`map_tab.cpp:280`), the trace has no such guard. Trigger: zoom in to z17/z18 with
a route whose sender or repeater is tens of km away (the z18 window is about 190 m wide), and the dashed route and the
tail are painted through a wrapped coordinate: the line crosses the map in the wrong place. The tail is worse: it is
drawn with `blendLine`, which reads the frame for every pixel (`screen_map.cpp:176`
`d->drawPixel((int16_t)xx, (int16_t)yy, mixOverFrame(xx, yy, col, alpha));`) and `omFramePixel` has no bounds check
(`board.cpp:523` `[(size_t)y * s_frame.width() + (size_t)x]`). For x above 320 the index leaves the 320x214 canvas and
at y 213 it is about 48 KB past the end of the 150 KB frame buffer (a wrong-row read in most cases, a read of whatever
is mapped next at the edge), and for a negative x the `(size_t)` cast makes the index wrap modulo 2^64. So: wrong
drawing certainly, a memory-safety defect probably, a load fault possibly (not verified on the device). The same
coordinates also make `drawTraceRoute` walk up to 65,535 steps per segment per frame (`screen_map.cpp:186-197`).
Fix: clamp in one place before the cast, e.g. skip a point (or clamp to the canvas plus a margin) when the projected
value is outside int16, and keep the route in int32 `TracePt` if the wrap is ever wanted for clipping; the cheap
version is to reject the whole frame when any point is outside +-16384 and let the animation keep its clock (the model
already survives an unprojectable frame). Effort S.

### 2. S2 · screens/map_tab.cpp:572 and :581 · the Traces/Names keys read the NVS write's result as the new state, so turning them off re-seeds a replay and the log says "on"
```c
const bool on = s_layers.toggleTraces();
traceMapReset();
if (on) traceSeedFromRing();
Serial.printf("[map] traces %s\n", on ? "on" : "off");
```
`MapLayerPrefs::toggleTraces()` flips the value and then `return store(kMapPrefTraces, traces_);`
(`ui/map_layers.h:60-63`), i.e. the return is the WRITE, not the state. `test_est370.cpp:238-239` proves it:
`CHECK(p.toggleTraces()); CHECK(!p.traces());`. So with a healthy NVS, `on` is true on BOTH taps: turning Traces OFF
still queues up to `kTraceSeedMax` routes (they are then never pumped, because `pump()` returns at
`if (!s_layers.traces()) return;`) and the log prints "traces on" while the chip shows off. With a FAILING write the
opposite happens: tapping Traces ON returns false, `traceSeedFromRing()` is skipped, and the log says "off" although
the chip is on, so nothing replays until the tab is reopened. The same file gets this right two cases below, with the
comment `// the return is the write's, so read the state back below` (`map_tab.cpp:591`). `Names` has the same read at
`:581` and prints the same wrong word (`:582`). Fix: `const bool on = s_layers.traces();` after the toggle (and
`s_layers.names()`), exactly as the TabsToggle case does; if the write's failure should be visible, log it separately.
Effort S.

### 3. S3 · screens/map_tab.cpp:158 and :165 · both trace log lines print another node's name (rule 9)
```c
Serial.printf("[map] trace %s %u hop(s), ring only\n", sender ? sender : "?", (unsigned)route.hops);
Serial.printf("[map] trace %s %u hop(s), %d known point(s), %d resolved\n", sender ? sender : "?", ...);
```
`sender` is a contact's name (from `meshMsgName()` or `MeshDm::name`), printed on every traced arrival and every tab
open that seeds a replay. This is the exact line EST-399 fixed on the neighbouring screen, whose comment is three
hundred lines below: `// EST-399: the marker's index, not the node's name (rule 9)` (`map_tab.cpp:604`). No rule-9 guard
covers it: the guards are per-ticket Python checks (`test_est384.py`, `test_est385.py`, `test_est387.py` in
`run.sh:21-26`), none of them looks at `screens/map_tab.cpp`. Fix: print the hop count, the known-point count and the
resolved count only, or the marker index the map already logs; keep the name out. Effort S.

### 4. S4 · screens/map_tab.cpp:272 (inside build()) · the whole contact table is copied and insertion-sorted on every animation frame, and again for every tap and every pressed look
```c
const int n = om::meshContactList(list, 32);
```
`build()` is called from `draw()`, from `drawAnimFrame()` (up to 30 times a second through `pump()`), from `tap()`
(`:548`) and from `pressTarget()` (`:541`), i.e. twice per tap. `meshContactList` walks every contact slot and
insertion-sorts up to 32 `MeshContact` records (about 104 bytes each) into the caller's array with struct copies
(`mesh_link.cpp:2117-2138`): roughly 32 `getContactByIdx` calls plus up to ~500 record copies, about 50 KB of memcpy
per call. At 30 fps that is 1.5 MB/s of internal-RAM copying for a table that changes only when an advert arrives, and
`om::meshContactSignature()` exists for exactly this (`mesh_link.h:238` "Cheap change detector"). Fix: keep the marker
list (positions and names only, ~12 bytes per row) and rebuild it only when the signature changes; it also makes
`pressTarget()` cheap. Gain estimated at 0.1-0.4 ms per frame, not measured (no build and no device in this run).
Effort M.

### 5. S4 · screens/map_tab.cpp:325-347 slideBars() · four full panel pushes and a draw() inside one tap, about 125 ms with the loop stalled
```c
ui::mapScreenSlide(v, (int16_t)tabOff, (int16_t)barOff);
...
draw();   // the steady frame of the new state
```
The function's own comment budgets 31 ms per frame (`map_tab.cpp:320-324`: "a frame is a 19.8 ms canvas repush plus an
11.5 ms panel push (31 ms ...), so 4 frames is about 125 ms"), and all of it runs synchronously inside `tap()`, which
the loop calls (`main.cpp:5880`). For that ~125 ms the loop task services nothing else: no radio, no audio, no USB.
Fix: the trace pump already shows the pattern (a per-pass state machine with a frame clock), so give the slide its own
`slideNext(nowMs)` called from `pump()` and return early while it runs. Same 125 ms of pixels, spread over passes.
Effort M (only worth it if the stall is felt on the glass).

### 6. S5 · ui/map_trace.cpp:220 and :235 · the slice capacity is a hand-computed duplicate of the array size, exactly 13 of 13 elements today
```c
out->tailCount = traceSlice(geom, px, from, head, out->tail, kTraceMaxPoints + 1);
```
`TraceFrame::tail` is `TracePt tail[kTraceMaxPoints + 1]` (`map_trace.h:120`) and `traceSlice` can write
`g.count - 1` vertices plus the two endpoints, i.e. 13 for a 12-point route: the call is correct and has no spare
element. If `tail[]` is ever sized like the route, or the route grows, this is a one-element overflow with no compiler
complaint (`g++ -Warray-bounds` will not see it through the reference). Fix: pass
`(int)(sizeof(out->tail) / sizeof(out->tail[0]))`, or `static_assert` the relation. Effort S.

### 7. S5 · screens/map_tab.cpp:128, :139, :263 · three silent drops on the trace path: no counter, no log
```c
if (route.count == 0) return false;                         // :128
if (!s_traceSelfKnown) return false;                        // :139
if (count_ >= kTraceQueueMax) removeAt(0);                  // :263
```
A message whose sender and hops all lack a position (and we have no fix either) produces no trace and no line; a
direct arrival without our own position is dropped the same way; a fifth route in a burst silently evicts the oldest.
This is EST-380's shape (`hist_dropped = 0` while losing data): the device log says nothing, so "the trace did not
show" cannot be told apart from "traces off" or "no message". Fix: one `s_traceDropped` counter with a reason byte,
printed in `traceCostLine()` (`[map] traces ... dropped %lu (no point %lu, no self %lu, queue %lu)`), or at least a
line per drop. Effort S.

### 8. S5 · screens/map_tab.cpp:242-276 · the marker budget is `kMapMaxMarkers = 32` and our own dot takes one slot, so the 32nd contact is silently never drawn or tappable
```c
v.markerCount = 0;                                          // :242
ui::MapMarker& m = v.markers[v.markerCount++];              // :259 (our own dot, unguarded)
for (int i = 0; i < n && v.markerCount < ui::kMapMaxMarkers; ++i) {   // :276
```
With a full 32-row contact table the loop stops at index 30 and the oldest contact loses its marker, its label and its
tap target, with nothing in the log. The self marker is also the only one added without a capacity test (`:259`), which
is safe today only because it is added first. The cull margin is a bare `4` twice (`:280-281` `x < -4 || x >=
ui::kMapCanvasW + 4`), which is `kMapMarkerR` by hand (`map_chrome.h:36`) and does not account for a label drawn to
the right of an off-screen marker. Fix: size the array `kMapMaxMarkers + 1` or count the self dot outside the contact
budget, log the count actually drawn (`%d of %d`), and use `kMapMarkerR` in the cull. Effort S.

### 9. S5 · ui/map_trace.cpp:45 with :53-55 · `out->hops` keeps the raw packet hop count while the drawn path is silently clamped to 8
```c
out->hops = hopCount;                                       // :45
int hops = hopCount;
if (hops > kTraceMaxHops) hops = kTraceMaxHops;             // :55
```
A packet with more than `kTraceMaxHops` (8) hops draws 8 of them, animates 8 * 420 ms, and still reports (and logs) the
untruncated count from `route.hops` at `map_tab.cpp:166`. Not verified whether a >8-hop packet can arrive: the header
calls 8 "MeshCore's own trace limit" (`map_trace.h:32`), but `hopCount` is a `uint8` straight from the packet, so the
code has no way to know. Fix: clamp once and either report the clamp in `resolved`/a flag or log it. Effort S.

### 10. S5 · ui/map_trace.cpp:77-82 with :259-262 · the "newest route per sender wins" key is one byte, and the dedupe loop skips an element after each removal
```c
return (uint8_t)h;                                           // map_tab.cpp:81
for (int i = 0; i < count_; ++i) {
  if (pending_[i].key == key) removeAt(i);                   // map_trace.cpp:260-261
}
```
At most `kTraceQueueMax` = 4 routes are ever pending, so the birthday risk is about 1 % per four-sender burst rather
than the 50 % the comment's "one byte is enough" reasoning would suggest at the 32-contact scale: still, a collision
means one sender's route silently replaces another's, and the comment's justification is only true by accident of the
queue size. The loop is also written unsafely: `removeAt(i)` shifts the next item into slot `i` and `++i` then skips it,
which is harmless only because `push()` dedupes on entry. Fix: keep the full 32-bit hash in `Item` (4 bytes x 4 slots)
and step `i` back after a removal. Effort S.

### 11. S5 · screens/map_tab.cpp:305 · build() clears the saved selection, and it is called from the hit test and from the pressed look
```c
if (s_selName[0] && !v.callout) s_selName[0] = 0;
```
`build()` is used as a query by `pressTarget()` (`:541`) and `tap()` (`:548`), so a touch-down can clear the selection
before the tap is even resolved, and a drag that pans the selected marker off the canvas clears it for good: pan back
and the callout is gone, with no way to know why. It also makes the trace animation advance from a hit test, because
`build()` ends in `traceSampleNow()` (`:248`, `millis()` inside `sampleActive`). Not verified whether clearing on
"marker not drawn" is intended; if it is, do it in the draw path only, not in the two query paths. Effort S.

### 12. S5 · screens/map_tab.h:57, :59, :64, :65 · four public functions nobody calls, two of them not even defined
```c
void select(const char* name);
bool locked();
void tracesToggle();
void namesToggle();
```
`select()` is only called from inside this module (`showPoint()`), `locked()` has no caller at all, and
`tracesToggle()`/`namesToggle()` have no definition in `map_tab.cpp`, so the header advertises two entry points that
would fail to link. The header's own comment promises `select` is "used by the shell and by the Nodes sheet". Fix:
delete the four (the Map's toggles are the two hit kinds in `tap()`), or define the two toggles if a USB command is
meant to drive them. Effort S.

### Clean areas (checked, nothing found)
- **Shared resources with a hard count (class 1):** no file, socket, queue, timer or I2C handle in either file. The
  only counts are the marker table (finding 8), the 4-slot route queue (finding 7) and the 13-slot tail (finding 6).
- **Integer width and casts (class 2) other than finding 1:** no `time_t`, `long` or `size_t` cast of a large
  constant; the two millis() comparisons are wrap-safe by the unsigned difference
  (`map_trace.cpp:182` `const uint32_t elapsed = nowMs - startedAt_;`, `map_tab.cpp:518`
  `(int32_t)(now - s_traceNextFrameMs) < 0`); no signed/unsigned compare and no `uint8` counter that can wrap.
- **Silent failure (class 3):** only the three trace drops (finding 7); every other failure path here is a
  documented "draw nothing" (a frame with no projection stays live and draws nothing, `map_trace.cpp:170-176`).
- **Boot side effects (class 4):** `begin()` copies the host, nothing else; `loadLayers()` only reads the three keys.
  No GPS, radio, advert, SD or NVS write on the boot path of either file.
- **Retry (class 5) and wrong task (class 6):** nothing one-shot and retryable here, and nothing runs off the loop
  task: `tap()` is reached from the loop (`main.cpp:5880`) and USB touch samples are consumed there too, so the slide
  (finding 5) is a stall, not a cross-task call. No stack growth worth naming: the largest locals are
  `ui::MapView v` (a 32-row marker array plus one `TraceFrame`, about 1 KB) and `TracePathHop table[32]` (512 B), both
  well inside the loop's 16 KB stack.
- **Internal RAM (class 7):** no allocation at all in these files; the statics are `s_contacts` (32 x ~104 B = 3.3 KB)
  and the queue (`TraceQueue`, about 0.5 KB). The 133 KB canvas stays map.cpp's PSRAM buffer. Nothing here accounts
  for the ~200 B per HTTPS probe in this ticket's scope.
- **Flash wear (class 9):** the three layer keys are written only on a tap (`map_layers.h:60-72`), never on a timer,
  per packet or at boot.
- **Privacy (class 8):** only finding 3. No format in either file can print a coordinate, an address, an SSID, a
  password or a message text; `[map] select marker %d` and `[map] zoom %d` are already clean.
- **UI classes:** one rectangle drives draw and tap (`pressTarget()` and `tap()` both call the same `build()` and
  `mapScreenChrome()`/`mapHitAt()` pair, `:539-551`), the `tap()` switch has no stale branch in front of another and
  every case returns, a settings overlay cannot reach the map's handlers (the shell gates with `!g_settings`), the
  screen draws through `ui::mapScreen*` rather than straight to the panel, the callout's stamp is a clock time
  (`StampForm::DayTime`), its two strings are ellipsised at draw time (`screen_map.cpp:466-471`) and the names are
  copied into 32-byte fields that exactly match MeshCore's `char name[32]`, and there is no em dash in any string that
  reaches the glass.

### Coverage
**Read in full:** `ui/map_trace.cpp` (296 lines), `ui/map_trace.h` (183), `screens/map_tab.cpp` (629),
`screens/map_tab.h` (69), `ui/map_layers.h`, `docs/REBUILD.md`.
**Read in part, only as evidence for a finding (so absences there are NOT evidence):** `ui/map_chrome.h:1-140`,
`ui/screen_map.h:1-70`, `ui/screen_map.cpp:136-230, 460-478`, `map.cpp:28-41, 300-395, 424-434`,
`board.cpp:385-410, 520-545`, `mesh_link.h:1-250` (targeted greps only), `mesh_link.cpp` (the accessors quoted),
`mesh_text.h:1-30`, `ui/dm_ring.h:1-120`, `test/host/test_est368.cpp`, `test_est394.cpp`, `test_est370.cpp`
(targeted), `test/host/run.sh:15-40`, `docs/PROGRESS.md:7395-7600`.
**In scope but not read:** nothing. Both files in this ticket's scope were read in full; the neighbouring drawing and
tile files (`ui/screen_map.cpp` beyond the lines quoted, `map.cpp` beyond the projection, `ui/map_chrome.cpp`,
`ui/map_pick.cpp`) were not this ticket's scope and are not covered by this report.
No build and no host test was run: this ticket changes no code, and the sandbox has no device.

### Next step
Finding 2 is the one to fix first (one line, and the file's own TabsToggle case shows the shape); finding 1 next
(a clamp before the `int16` cast, which also removes the out-of-bounds frame reads); finding 3 goes with the rule-9 log
work and should extend a guard to `screens/map_tab.cpp`. #4, #5, #6, #7 and #8 are small and independent; #9-#12 are
polish.

## EST-409 map fixes

Fixes for the EST-408 sweep: findings 1, 2, 3, 4 and 6-12. Finding 5 (the 125 ms slide) is left out as the
ticket says. DeepSeek (Paperclip worker, 2026-11-12). **No code was committed, no device was touched, and
nothing below claims anything about the glass.**

### 1. S2 - `ui/map_trace.cpp` - a far route point no longer wraps
`TraceAnim::sample()` now rejects the whole frame when ANY projected point is outside +-16384, before the
`int16_t` cast. It returns true with `live` set and no route, tail or dot, so the animation clock keeps
running and the route reappears at the right place and time. Second belt: `omFramePixel()` (board.cpp:522)
returns 0 when there is no buffer or the coordinate is outside the frame, and `blendLine()` (screen_map.cpp)
checks every pixel against `d->width()`/`d->height()` BEFORE `mixOverFrame()` reads the frame and
`drawPixel()` writes it. Behaviour otherwise unchanged.

Host test (`test_est409.cpp`, `testFarPointRejected`): a route point 50 km east of the centre at 46 N, z18,
projects to about 120,000 px. The test asserts the point is outside the int16 band, that the old cast would
wrap it, and that the frame draws zero points, zero tail pixels and no dot while the animation stays active.
A near route still draws (positive control). The frame-bounds belt is compiled in the firmware but is NOT
host-tested: board.cpp and screen_map.cpp need LovyanGFX, so that half is code-inspected only.

### 2. S2 - `screens/map_tab.cpp` - Traces and Names read the state, not the write
Both cases now do what TabsToggle already did: call the toggle as a statement, then read
`s_layers.traces()` / `s_layers.names()`. The old line `const bool on = s_layers.toggleTraces();` treated a
healthy NVS write (always true) as "on", so turning Traces OFF re-seeded a replay and logged "on"; a failed
write did the opposite.

Host test: `map_tab.cpp` cannot be host-compiled (Arduino + mesh_link), so the repo's source-level host test
`test_est409.py` is the test. It pins the statement, the state read, `if (on) traceSeedFromRing();` and the
log word built from the same `on`. It failed on the old line with exactly:
```
FAIL the Traces key treats the NVS write's result as the state (turning it off would seed):
     still present 'const bool on = s_layers.toggleTraces();'
FAIL the Traces key must read s_layers.traces() after the toggle:
     missing 'const bool on = s_layers.traces();'
FAIL the Names key treats the NVS write's result as the state: ...
FAIL the Names key must read s_layers.names() after the toggle: ...
```
A separate red harness against the real `MapLayerPrefs` printed `old map_tab seed decision (the write's
return) = 1, real state = 0` after turning Traces OFF; the existing `test_est370.cpp` already proves that
mismatch in C++ (`CHECK(p.toggleTraces()); CHECK(!p.traces());`).

### 3. S3, rule 9 - the two trace log lines print counts only
`[map] trace %u hop(s)%s, ring only` and `[map] trace %u hop(s)%s, %d known point(s), %d resolved` (the
`%s` is `" (hops clamped)"` or `""`). The sender's name is gone.

The guard is ARMED: `test_est397.py`'s `NAME_ARG` now also matches a bare `sender` or `who` (a local that
merely HOLDS a name), with the existing lookbehind so `m.sender` and `senderName` do not match. It also got
two new negative controls. Run on the unfixed tree it failed with exactly two lines, exit 1:
```
FAIL src/screens/map_tab.cpp:158: log carries a %s fed by a name: Serial.printf("[map] trace %s %u hop(s), ring only\n", sender ? sender : "?", ...
FAIL src/screens/map_tab.cpp:165: log carries a %s fed by a name: Serial.printf("[map] trace %s %u hop(s), %d known point(s), %d resolved\n", sender ? sender : "?", ...
```
No other real violation was found in 189 files, and there was no false positive to narrow. After the fix the
same guard passes: 448 Serial statements, 189 files, 17 negative controls, 0 findings.

### 4. S4 - the contact table is cached on `om::meshContactSignature()`
New `src/ui/signature_cache.h` (`SignatureCache<T, N>`, plain C++). `map_tab.cpp` holds
`ui::SignatureCache<MeshContact, 32> s_contacts`; `contactsEnsure()` is the ONLY caller of
`om::meshContactList()` and rebuilds only when the signature moves. `build()`, `traceHopTable()`,
`pressTarget()` and `tap()` all reuse the same snapshot. `s_markerBudgetLogged` resets on a rebuild.

Host test: `testSignatureCache` in `test_est409.cpp` proves first build, same-signature reuse (rows keep
their values), rebuild on a moved signature, rebuild when the signature moves back, an empty list as a valid
snapshot, and `commit()` clamping to capacity. Old-tree evidence: the source half failed with "the Map does
not use the signature cache" and "om::meshContactList() must be called only from the cache rebuild (2
calls)".

### 6. S5 - the tail capacity is the array's real size
Both `traceSlice()` calls now pass `(int)(sizeof(out->tail) / sizeof(out->tail[0]))`. Source test fails on
the old `out->tail, kTraceMaxPoints + 1)` (0 of 2 on the old tree).

### 7. S5 - the three silent trace drops are counted and printed
`s_traceDropped` plus `s_traceDropNoPoint`, `s_traceDropNoSelf` and `s_traceDropQueue`. The queue's
reason comes from a new optional `bool* evicted` on `TraceQueue::push()`, set only when the oldest pending
route is really dropped. `traceCostLine()` prints them and now also prints when there are drops but zero
frames (the old early return hid exactly that case):
```
[map] traces %lu frames in %lu ms, avg %lu us, worst %lu us, in flight %d,
dropped %lu (no point %lu, no self %lu, queue %lu)
```
Source test pins all four counters and the three reason strings. Old tree: all missing (4 failures).

### 8. S5 - the self dot is outside the 32-contact budget
`MapView::markers` is now `kMapMaxMarkers + 1`; the contact loop keeps its own `drawn < kMapMaxMarkers`
budget, so a full table draws all 32 contacts plus our dot. The cull margin uses `ui::kMapMarkerR` instead
of the bare 4. When the budget truncates, the Map logs once per rebuilt list:
`[map] markers %d of %d drawn (budget %d)` where the denominator is the number of positioned, in-view
contacts (not the whole table). Source test pins the array size, the `kMapMarkerR` cull and the log line.

### 9. S5 - hops are clamped once and the clamp is reported
`TraceRoute::hopsClamped` is set from the raw packet count; `out->hops` is the clamped count used by both the
drawn path and every caller's log. `traceBuildRoute()` no longer keeps the raw value anywhere. The Map's
trace log appends `" (hops clamped)"`. Old-tree red print: `old route.hops after a 12-hop packet = 12` while
the path drew 8. `testHopsClamp` now asserts `hops == kTraceMaxHops`, the flag, and that a direct route is
not flagged.

### 10. S5 - the full 32-bit sender hash, and a safe dedupe loop
`traceKeyForName()` returns the full FNV value, `traceEnqueue()` and `TraceQueue::push()` take `uint32_t`,
and `Item::key` is `uint32_t`. The dedupe walks backwards (`for (int i = count_ - 1; i >= 0; --i)`), so a
removal cannot skip the element shifted into its slot. Host test: two keys that share the low byte
(`0x00000123` and `0x00000223`) both stay queued (old code kept one), while the same full key still
replaces. A second check pushes A, B, A and asserts B's 3-point route is the one that plays. Old-tree red
harness printed `FAIL two senders that share the low byte do not replace each other` (the other checks in
that harness were the two finding-1 checks, 3 failures, exit 1).

### 11. S5 - `build()` is a query when the hit test calls it
`build(v, bool sampleTrace)`: `draw()`, `drawAnimFrame()` and `slideBars()` pass true;
`pressTarget()` and `tap()` pass false, so neither advances the trace clock. The
`if (s_selName[0] && !v.callout) s_selName[0] = 0;` line is gone: a marker panned off-screen keeps the
selection and the callout returns when it is panned back. Only an explicit tap elsewhere clears it (the
existing Map/Callout case). `test_est394.py` was updated to the new signature and pins true for `draw()`,
false for the two query paths. Source test also fails on the old clear line and old `build(v)` calls.

### 12. S5 - the header advertises only what exists
Removed `select()` (now `static` in map_tab.cpp), `locked()` (definition and declaration), and the two
undefined `tracesToggle()` / `namesToggle()`. `showPoint()`, `clearSelection()` and `setLock()` still
remain for the shell. Source test fails on all four old declarations and on the old dead `locked()`
definition.

### Files
Changed: `src/screens/map_tab.cpp`, `src/screens/map_tab.h`, `src/ui/map_trace.cpp`, `src/ui/map_trace.h`,
`src/ui/screen_map.cpp`, `src/ui/screen_map.h`, `src/board.cpp`, `test/host/run.sh`,
`test/host/test_est394.py`, `test/host/test_est397.py`.
New: `src/ui/signature_cache.h`, `test/host/test_est409.cpp`, `test/host/test_est409.py`.

### Build and tests (real exit codes, this tree)
Build, same command as REBUILD.md (no device):
```
cd /home/slofi/Projects/pocket-mesh/firmware
PLATFORMIO_CORE_DIR=/home/slofi/.cache/pio-om-lite/core \
  /home/slofi/.local/share/pipx/venvs/platformio/bin/pio run -e om_lite_l2
```
- Before these edits: exit 0, RAM 160,480 B of 327,680 (49.0 %), flash 2,993,417 B of 6,553,600 (45.7 %).
- After these edits: exit 0, RAM 160,528 B (+48 B), flash 2,994,089 B (+672 B), same percentages.

Host suite, one command:
```
sh test/host/run.sh
```
- REAL exit 0, zero FAIL lines. 62 C++ binaries and 24 Python checks ran.
- New: `test_est409.cpp` (binary 62) and `test_est409.py`. Changed: `test_est397.py` (armed) and
  `test_est394.py` (build signature).
- `test_est409.py` against the pre-fix files from `git show HEAD`: exit 1, 40 failures. The first four are
  the toggle state (quoted above); the rest are the missing signature cache (2 meshContactList calls), the
  hand-computed tail cap (0 of 2), the four missing drop counters, the missing marker `+ 1`, the missing
  `kMapMarkerR` cull, the missing marker-count log, the missing hop flag and `out->hops = hopCount`, the
  one-byte key and `return (uint8_t)h;`, the forward dedupe loop, the selection clear, `build(v)` in both
  query paths, and the four old header declarations.
- `test_est409.cpp` cannot compile against the old tree because `TraceRoute::hopsClamped` and
  `ui/signature_cache.h` are themselves part of the fix. To prove the model checks can fail, a temporary
  harness linked the OLD `map_trace.cpp` and printed:
  ```
  FAIL no route point is placed outside the int16-safe band
  FAIL no wrapped coordinate reaches the tail or the dot
  FAIL two senders that share the low byte do not replace each other
  3 red check(s) FAILED on the old tree
  ```
  exit 1. The old hop value was printed separately (`12`), and the old toggle decision printed
  `write's return = 1, real state = 0`.
- The signature-cache test is a new-code test (the class does not exist on the old tree); the source half
  is what failed there.

### What I could not check
- No device in this run, so nothing about the glass: the new log lines (`markers %d of %d`,
  `dropped ... (no point ...)`), the `(hops clamped)` suffix and the callout returning after a pan are all
  untested on hardware.
- No host test for the frame bounds second belt (board.cpp / screen_map.cpp need LovyanGFX).
- The cache is keyed on `meshContactSignature()`, which hashes lastmod/type/name/favourite but not lat/lon.
  A position that changed without a lastmod change would wait for the next advert. MeshCore stamps every
  advert, so this should not happen in practice, but it is the one assumption in finding 4 and worth a
  reviewer's eye.
- Finding 5 (the 125 ms slide) is untouched, as the ticket says.

### Next step
Claude's review and commit; then flash and check the map log lines and the panned-off callout on the device.


## EST-410 main.cpp split: RPTR

The RPTR tab is out of main.cpp: `src/screens/rptr_tab.{h,cpp}`, in the same shape as
`screens/map_tab.{h,cpp}` from EST-371. Moved: the tab's own state (session, list/page scroll, the
CLI queue, Apply, the console log, the confirmation, the map picker, the keyboard sheet), the view
builder, `drawRptrTab()`, `handleRptrTap()`, `rptrPump()`, the page taps, the sheet commit, the key
handling and the RPTR-only helpers. `rptr_link`, `rptr_page` and `rptr_model` are untouched.

### Size, build and tests
- `src/main.cpp`: 7,178 -> 5,526 lines (-1,652).
- New `src/screens/rptr_tab.cpp`: 1,801 lines; new `src/screens/rptr_tab.h`: 78 lines.
- The moved block is 1,585 lines of HEAD's main.cpp (the EST-362 design comment, the
  `drawRptrTab()` forward declaration, and lines 1780..3350). 1,557 of them are byte-identical;
  28 lines were edited inside the block and are listed below.
- Build with the REBUILD.md command (`PLATFORMIO_CORE_DIR=/home/slofi/.cache/pio-om-lite/core
  /home/slofi/.local/share/pipx/venvs/platformio/bin/pio run -e om_lite_l2`): REAL exit 0.
  RAM 160,808 B (49.1% of 327,680), was 160,528 B (+280 B).
  Flash 2,995,605 B (45.7% of 6,553,600), was 2,994,073 B (+1,532 B).
  `firmware.bin` 2,995,968 B, was 2,994,432 B (+1,536 B).
- Why the growth (bigger than a few hundred bytes in flash): this build has no LTO, so the six Host
  services (`buildStatus`, `tabBar`, `gps`, `toast`, `screenVisible`, `redraw`) are indirect calls
  where they were direct calls in the same translation unit, and the exported wrappers are real calls too. The moved code itself is text-identical. The +280 B RAM is the Host (6 pointers, 24 B)
  plus the null fallback `static GpsState none{}` in `gps()` (sizeof(GpsState) = 264 B), used only
  before `begin()`. No heap allocation, no new task, no changed task affinity.
- `sh test/host/run.sh`: REAL exit 0, 0 FAIL lines.

### git diff
`git diff --stat` (tracked files; the two new files are untracked, their `--no-index` stat follows):

```
 src/main.cpp                    | 1766 ++-------------------------------------
 test/host/test_est393.py        |   11 +-
 test/host/test_est397.py        |    4 +-
 test/host/test_tab_bar_calls.py |    9 +-
 4 files changed, 74 insertions(+), 1716 deletions(-)
 /dev/null => src/screens/rptr_tab.h   |   78 +++++++++++++++++++++++++++++++++
 /dev/null => src/screens/rptr_tab.cpp | 1801 +++++++++++++++++++++++++++++++++
```

`git diff --color-moved=zebra --stat` prints the same four tracked lines: git's stat has no
cross-file moved-line count, so the equivalent measure is a unified diff of the block itself:

```
original moved block: 1585 lines
new moved block:      1587 lines
diff lines (+/-):     58  (+30 / -28)
```

The 58 are 28 changed source lines (each a `-`/`+` pair) plus 2 blank separator lines. The other
1,557 lines are a pure move. This was produced with `python3` `difflib` over
`git show HEAD:src/main.cpp` lines 1743..1755 + 1757 + 1780..3350 and the same block in
`src/screens/rptr_tab.cpp`.

### Non-move edits (rule 1, one per line)

Inside the moved block (28 lines, 30 substitutions):
- `showToast(...)` -> `toast(...)` on 14 lines: the toast is a Host service now.
- `redrawCurrentScreen()` -> `redraw()` on 6 lines: the shell redraw is a Host service.
- `g_gps.valid` / `.lat` / `.lon` -> `gps().valid` / `.lat` / `.lon` on 5 lines (6 occurrences): the
  raw parser state is a Host service, and it stays the RAW parser state, not `posOwn()`.
- `buildStatus(v.status)` -> `status(v.status)`: the status bar is a Host service.
- `currentTabBar(om::ui::Tab::Rptr)` -> `tabBar(ui::Tab::Rptr)`: the tab bar is a Host service.
- `g_tab == 3 && !g_settings && !g_rptrList && !g_rptrMapPick` -> `visible() && ...`: the shell's
  screen test is a Host service.
- `g_tab == 3 && !g_settings && !g_rptrList` -> `visible() && !g_rptrList`: same test, pump path.

New code that is not a move:
- `src/screens/rptr_tab.h`: the `Host` struct and the module's public surface (`begin`, `draw`,
  `tap`, `pressTarget`, `pump`, `beginDrag`, `dragMove`, `endPickerDrag`, `releaseSnap`, the six
  state accessors, `open`, `clearSession`, `openNodeName`, `coordText`).
- `src/screens/rptr_tab.cpp`: the Host helper functions (`toast`, `status`, `tabBar`, `gps`,
  `visible`, `redraw`), the local `constexpr unsigned kLoopStackSize = 16384` (same value; main.cpp's
  copy is static to that file and only the `[rptr] build` log line prints it), the exported wrappers
  named above, and `pressTarget()`, which is the old press-target block moved into the module.
- `pressTarget()` body was moved from main.cpp's `pressTargetAt()`; the tab bar, gear and fault
  mark checks stayed in main.cpp, exactly where the Map's are.
- The loop's RPTR drag branch became `rptr_tab::dragMove(x, y, dy, g_drag, g_lastX, g_lastY,
  g_lastRender)`: same code, with the shell's drag latch, pan base and 60 ms frame clock passed by
  reference so they keep their old values and stamp order.
- The release clamp became `rptr_tab::releaseSnap(g_drag)`: same code; the `g_tab == 3 &&
  !g_settings` guard stayed in main.cpp.
- `rptrOpen`, `rptrClearSession`, `rptrCoordText`, `drawRptrTab`, `handleRptrTap` and `rptrPump`
  keep their old names and bodies inside the module; public wrappers `open`, `clearSession`,
  `coordText`, `draw`, `tap` and `pump` are the only new entry points.
- main.cpp: `#include "screens/rptr_tab.h"`; three Host adapters `appRawGps()`,
  `appRptrVisible()` and `appRedraw()`; the `rptrHost` wiring in `setup()`; `showToast`'s forward
  declaration stayed where it was, `drawRptrTab`'s was removed.
- main.cpp Settings: `g_rptrNode` reads now use `om::rptr_tab::openNodeName()` (the Open row and
  the Forget guard); `rptrClearSession()` -> `om::rptr_tab::clearSession()`.
- main.cpp drawing: `drawRptrTab()` -> `om::rptr_tab::draw()` in `redrawMainTab()` and
  `redrawCurrentScreen()`.
- main.cpp showTab: `g_rptrList` -> `om::rptr_tab::listShowing()`.
- main.cpp Nodes menu: `rptrOpen(name)` -> `om::rptr_tab::open(name)`.
- main.cpp press path: `g_rptrOsk` -> `keyboardOpen()`, `handleRptrTap` -> `tap()`; main.cpp
  pressTarget: `g_rptrOsk` -> `keyboardOpen()` and the old block -> `pressTarget()`.
- main.cpp idle blockers: `g_rptrOsk` -> `keyboardOpen()`; `g_rptrAskOpen || g_rptrMapPick` ->
  `modalOpen() || mapPickerOpen()`; `(g_rptrMapPick && g_rptrPickDragging)` ->
  `(mapPickerOpen() && mapPickerDragging())`.
- main.cpp loop: `rptrPump()` -> `om::rptr_tab::pump()`; `g_rptrPickDragging = false` ->
  `endPickerDrag()`; the two drag-base assignments -> `beginDrag()`; the drag branch ->
  `dragMove()`; the release clamp -> `releaseSnap(g_drag)`.
- main.cpp Settings own-position picker: `rptrCoordText(...)` -> `om::rptr_tab::coordText(...)` on
  its two lines (the same helper, now owned by the module that also uses it).
- Tests, see below.

No rename changed a string, an NVS key, a log line or a USB answer. No bug was found while moving,
so nothing was fixed here.

### State another screen touches (rule 3)
- `g_rptrNode` was read by Settings (Open repeater row, Forget guard) and the Forget action also
  cleared the session. It moved with the session and is exposed as `openNodeName()` /
  `clearSession()` in `rptr_tab.h`; `app_shell.h` was not touched because the USB commands never
  read RPTR state.
- `g_rptrList` was read by showTab's auto scan: `listShowing()`.
- `g_rptrOsk`, `g_rptrAskOpen`, `g_rptrMapPick` and `g_rptrPickDragging` were read by the loop's
  idle blockers: `keyboardOpen()`, `modalOpen()`, `mapPickerOpen()`, `mapPickerDragging()`.
- Everything else is RPTR-only and moved. `g_tab`, `g_settings` and `g_gps` are shell state and
  stayed in main.cpp; they reach the module through the Host.

### Tests changed and armed (rule 5)
- `test/host/test_tab_bar_calls.py`: the Rptr model check now reads
  `src/screens/rptr_tab.cpp` (`v.tabs = tabBar(ui::Tab::Rptr);`) and main.cpp must wire
  `rptrHost.tabBar = appTabBar;`. Armed: replacing that one line in rptr_tab.cpp makes it exit 1
  with `FAIL screens/rptr_tab.cpp: the RPTR view does not take its tab model from the Host`; the
  file was restored and the test passes again.
- `test/host/test_est393.py`: `rptrMapPickCentreToDraft` is now read from
  `src/screens/rptr_tab.cpp`; `ownPickCentreToDraft` stays in main.cpp. Armed: removing the pinned
  `om::mapScreenToLatLon(om::ui::kMapCentreX, om::ui::kMapCentreY, lat, lon)` line makes it exit 1
  with that function's two failures; restored, it passes.
- `test/host/test_est397.py`: the two RPTR log lines now point at `src/screens/rptr_tab.cpp`
  (`answer does not fit` in the whitelist, `auto-login started` in the shapes). Armed: deleting
  both lines makes it exit 1 with the stale whitelist entry and the missing shape; restored, it
  passes.
- No other host test pinned RPTR code in main.cpp. `test_est374.py` still sees the same tabAt()
  count because the tab bar/gear/fault checks stayed in main.cpp; `test_est390.py` still sees the
  same pressArmAt call count because only the screen's own target map moved.

### Could not check (no device in this run)
- Nothing about the glass: the RPTR list, a page after login, the picker, the keyboard sheet, the
  confirmation, the console and the tab badge are untested on hardware here. The new code is the
  same text and the same draw call (`ui::rptrScreenDraw`) as before, and the build is clean, but
  that is not a glass check.
- The loop stack high-water line `[rptr] build ... stack_free=` is unchanged in text; the actual
  number can only be read on the device.
- `rptr_link` / `rptr_model` behaviour is unchanged by construction and their host tests pass.

### Next step
Claude: review the diff and commit. On the device: RPTR list scan, open a repeater, log in, one
setting Apply, the console, Back, the Position picker, and the Settings > RPTR Open repeater row.
The serial log should still print `[rptr] open (found N)`, `[rptr] build list/page=... stack_free=...`
and the same `[rptr] cli` lines, and `status` should show the same RAM/stack numbers.


## EST-411 main.cpp split: Nodes

The Nodes tab is out of main.cpp: `src/screens/nodes_tab.{h,cpp}`, in the same shape as
`screens/rptr_tab.{h,cpp}` from EST-410. Moved: the tab's own state (the row copy and its scroll
bases, the node sheet, the Info page, the trace sheet, the new-node dot and the long-press timing),
`refreshNodes()`, `buildNodeMenu()`, `openNodeMenu()`, `closeNodeSheets()`, `buildNodeInfoModel()`,
`openNodeInfo()`, `buildNodesView()`, `drawHome()`, the Nodes tap handling, `handleNodeAction()` and
the loop's hold/drag/release blocks. `mesh_link`, the contact table and the favourites store are
untouched; `ui/node_menu.cpp`, `ui/screen_nodes.cpp`, `ui/screen_node_info.cpp` and
`ui/node_info_layout.h` still own the rows and pixels.

### Size, build and tests
- `src/main.cpp`: 5,526 -> 5,135 lines (-391).
- New `src/screens/nodes_tab.cpp`: 577 lines; new `src/screens/nodes_tab.h`: 82 lines.
- The four principal moved blocks are BYTE-IDENTICAL to HEAD's main.cpp: 128 lines (Nodes data +
  sheet + Info, HEAD 628..755), 46 lines (`buildNodesView` + `drawHome`, HEAD 786..831), 107 lines
  (`handleNodeAction`, HEAD 3918..4024) and 27 lines (the state block, HEAD 345..371), 308 lines in
  all. With the `difflib` line matcher over the whole file, 344 of the 577 lines match a HEAD line
  verbatim; the other 233 are the new Host wrappers, the public entry points and the four branch
  rewrites listed below.
- Build with the REBUILD.md command (`PLATFORMIO_CORE_DIR=/home/slofi/.cache/pio-om-lite/core
  /home/slofi/.local/share/pipx/venvs/platformio/bin/pio run -e om_lite_l2`, after `-t clean`):
  REAL exit 0. RAM 160,856 B (49.1% of 327,680), was 160,808 B (+48 B).
  Flash 2,996,449 B (45.7% of 6,553,600), was 2,995,589 B (+860 B).
  `firmware.bin` 2,996,816 B, was 2,995,952 B (+864 B).
- The +48 B RAM is exactly the Host (6 pointers). The +860 B flash is larger than a few hundred
  and is the same effect EST-410 measured: this build has no LTO, so each Host service is an
  indirect call where it was a direct call in the same translation unit, and the public wrappers
  are real calls too. The moved bodies are text-identical. No heap allocation, no new task, no
  changed task affinity.
- Baseline note: the same HEAD re-measured here was 2,995,589 B flash (EST-410's entry says
  2,995,605, 16 B more; the build id was `b43ba03-dirty` in both). That 16 B is link/section-order
  noise between builds, so the comparison above uses the baseline measured on this tree today.
- `sh test/host/run.sh`: REAL exit 0, 0 FAIL lines.

### git diff
`git diff --stat` (tracked files; the two new files are untracked, their `--no-index` stat follows):

```
 docs/PROGRESS.md                | 169 +++++++++++++
 src/main.cpp                    | 545 ++++++----------------------------------
 test/host/test_est397.py        |   8 +-
 test/host/test_est400.py        |  12 +-
 test/host/test_tab_bar_calls.py |  19 +-
 5 files changed, 270 insertions(+), 483 deletions(-)
 /dev/null => src/screens/nodes_tab.h   |  82 ++++++++++++++++++++++++++
 /dev/null => src/screens/nodes_tab.cpp | 577 ++++++++++++++++++++++++++++++++++
```

`git diff --color-moved=zebra --stat` prints the same five tracked lines (git's stat has no
cross-file moved-line count), so the equivalent measure is the block comparison above. The
PROGRESS.md line is this entry itself.

### Non-move edits (rule 1, one per line)
Interface and new code in `src/screens/nodes_tab.{h,cpp}`:
- `Host` struct + `begin(host, items, scratch)`: the module cannot see main.cpp's statics.
- Host wrappers `showToast()`, `buildStatus()`, `currentTabBar()`, `chatOpenDmFor()`, `showTab()`,
  `visible()`: the moved bodies keep their old call names, so they stay text-identical.
- A `static void drawHome();` forward declaration: `openNodeMenu()` and `openNodeInfo()` call it
  before its definition, exactly as main.cpp did.
- `kNodeCap` moved from main.cpp to `nodes_tab.h` (`constexpr int kNodeCap = 64`, same value);
  main.cpp's allocUiBuffers() uses `om::nodes_tab::kNodeCap` on 4 lines: one constant, and the
  module owns the list size.
- `g_nodes` / `g_contactScratch` are still allocated by main.cpp's allocUiBuffers() (EST-357's one
  PSRAM point) and handed over in `begin()`: no new allocation, same buffers, same sizes.
- `draw()` is a one-line wrapper around the moved `drawHome()`: the shell's entry point, while the
  moved body keeps its old name.
- `tap(x, y, longPress)` is the old handleTap Nodes branch minus the shell chrome, answering bool:
  main keeps the tab bar, gear and fault mark exactly as on the RPTR branch.
- `pressTarget(x, y)` is the old pressTargetAt Nodes branch with `out->` rewritten to a local `t`
  and a return-by-value: same shape as map_tab::pressTarget and rptr_tab::pressTarget.
- `overlayOpen()` (Info page or a sheet) and `modalOpen()` (a sheet only): main needs the old
  early-return tests and the idle blocker.
- `count()`, `item(i)`, `refresh()`, `seenSignature()`, `newNodeDot()`, `noteNewNode()`,
  `selectByName()`, `setTrace()`: state the shell, Chat, Settings, the USB commands and the async
  loop read or set.
- `beginDrag()`, `holdPress()`, `holdDisarm()`, `holdArmed()`, `holdReset()`, `holdUpdate()`,
  `holdRelease()`, `closeSheetAwayFrom()`, `dragMove()`, `releaseSnap()`: the loop's press,
  release and drag blocks, while the shell keeps `g_drag`, `g_spent`, `g_acted` and the 60 ms
  frame clock.

main.cpp:
- `#include "screens/nodes_tab.h"`.
- the 29 Nodes state declarations replaced by a comment; the two PSRAM pointers stay.
- `refreshNodes()`, `buildNodeMenu()`, `openNodeMenu()`, `closeNodeSheets()`, `buildNodeInfoModel()`,
  `openNodeInfo()`, `buildNodesView()` and `drawHome()` removed (moved); `buildStatus()` stayed in
  main.cpp because the Map and RPTR Hosts take it from there too.
- the `drawHome()` forward declaration removed.
- `currentTabBar`: `g_newNodeDot` -> `om::nodes_tab::newNodeDot()`.
- Chat `SheetAction::NodeDetails`: the refresh/find/set block -> `selectByName(m.name)`.
- Settings `NodesRoot`: `g_nodeCount` -> `om::nodes_tab::count()`.
- 8 draw call sites (`drawHome()` -> `om::nodes_tab::draw()`): redrawMainTab, setup, showTab,
  redrawCurrentScreen, the battery redraw, the GPS redraw, the 1 s Nodes refresh, the trace result.
- `pressTargetAt` Nodes branch: the info/sheet/row block -> `overlayOpen()` / `pressTarget()`;
  `pressTargetForChrome()` stays in main.
- `handleTap` Nodes branch: the info/sheet/row block -> `overlayOpen()` / `tap()`; tab bar, gear and
  the fault toast stay in main.
- idle blockers: `g_nodeMenu >= 0 || g_nodeTraceOpen` -> `om::nodes_tab::modalOpen()`.
- the 1 s refresh: `g_nodeSig` -> `om::nodes_tab::seenSignature()`.
- the new-node advert: `g_newNodeDot = true` -> `om::nodes_tab::noteNewNode()`.
- the trace result: four assignments -> `om::nodes_tab::setTrace(tv, trace.node)`.
- release: `g_nodeHoldArmed = false; g_nodeHold.release()` -> `holdRelease()`.
- the Info/list release clamps: two blocks -> `releaseSnap(g_drag)`.
- the press base: two `scroll0` assignments -> `beginDrag()`.
- a new press: `g_nodeHoldArmed = false` -> `holdDisarm()`.
- the hold arm: the five-condition block -> `holdPress(x, y, millis())`.
- the press away from a sheet: the close block -> `closeSheetAwayFrom(x, y)` plus the shell's
  three latches.
- the hold step: `g_nodeHoldArmed` / `reset()` / `update()` -> `holdArmed()` / `holdReset()` /
  `holdUpdate()`; the release still calls the shell's `handleTap(x, y, true)`, so the `[tap]` line
  and the click sound are unchanged.
- the drag branches: the Info and list blocks -> `dragMove(dy, g_lastRender)`.
- `app_shell`: `nodeCount()`, `node(i)` and `refreshNodesNow()` delegate to `count()`, `item(i)`
  and `refresh()`.
- setup(): three adapters (`appNodesVisible`, `appOpenDm`, `appShowTab`) and the `nodesHost`
  wiring right after `allocUiBuffers()`.

No rename changed a string, an NVS key, a log line or a USB answer. No bug was found while moving,
so nothing was fixed here.

### State another screen touches (rule 3)
- The Chat tab's "Node details" action selects a Nodes row: `selectByName(name)`.
- Settings > Nodes shows the count: `count()`.
- The tab bar's new-node badge reads it: `newNodeDot()`.
- The `nodes` USB answer reads the list: `app_shell` delegates to `count()`, `item(i)`, `refresh()`.
- The async loop's new-advert and trace events write it: `noteNewNode()`, `setTrace()`.
- The shell's own `g_tab`, `g_settings`, `g_drag`, `g_spent`, `g_acted` and `g_lastRender` stay in
  main.cpp; the module reaches the first two through `Host::screenVisible` and the rest are passed
  in or back by the loop hooks.

### Tests changed and armed (rule 5)
- `test/host/test_tab_bar_calls.py`: `currentTabBar()` must now read
  `om::nodes_tab::newNodeDot()`; the Nodes view check reads `src/screens/nodes_tab.cpp`
  (`view.tabs = currentTabBar(om::ui::Tab::Nodes);`) and main.cpp must wire
  `nodesHost.tabBar = appTabBar;`. Armed: replacing that one line in nodes_tab.cpp makes it exit 1
  with `FAIL screens/nodes_tab.cpp: the Nodes view does not take its tab model from the Host`;
  restored, it passes.
- `test/host/test_est397.py`: the four Nodes `SHAPES` entries (`[nodes] map`, `[nodes] info`,
  `[nodes] menu`, `[nodes] select`) now point at `src/screens/nodes_tab.cpp`; the `[nodes] %s`
  whitelist entry stays in main.cpp (the ping toast is still there). Armed: deleting the
  `[nodes] select %d` line makes it exit 1 with `the rewritten log line is missing`; restored, it
  passes.
- `test/host/test_est400.py`: the five Info-page wiring checks now read
  `src/screens/nodes_tab.cpp` (`nodeInfoDraw(...)`, `g_nodeInfoScroll0 = g_nodeInfoScroll;`,
  `nodeInfoDragScroll(`, `nodeInfoSnapScroll(`, `g_nodeInfoScroll = 0;`). Armed: deleting the drag
  base line makes it exit 1 with `FAIL the drag base: missing 'g_nodeInfoScroll0 = ...'`;
  restored, it passes.
- No other host test pinned Nodes code in main.cpp. `test_est374.py` still sees the same tabAt()
  and showTab() counts because the tab bar/gear/fault checks stayed in main.cpp; `test_est390.py`
  still sees the same `pressArmAt` count; `test_est381.py` still sees the same handleTap shape
  (the release still goes through it).

### Could not check (no device in this run)
- Nothing about the glass: the Nodes list, a row tap, every sheet row (Info, Direct message, Show
  on map, Ping, Trace, Favourite, Reset path, Remove contact, Login), Back from Info, the two
  scroll drags and the new-node dot are untested on hardware here. The moved code is the same text
  and the same draw calls (`ui::nodesScreenDraw` / `ui::nodeInfoDraw`) as before, and the build is
  clean, but that is not a glass check.
- The contact table, the favourites store and `mesh_link` are unchanged by construction.
- The USB `nodes` answer is the same code path through the new accessors; no device was present to
  run it.

### Next step
Claude: review the diff and commit. On the device: open Nodes, tap a row, long-press a few rows
through every sheet action that applies, Back from Info, drag the list and the Info page, watch the
new-node dot on an advert and a trace's hop sheet. The serial log should still print
`[nodes] menu N, N row(s)`, `[nodes] info N`, `[nodes] select N`, `[nodes] map: opening the node's
position`, `[nodes] trace: N hop(s), M ms` and `[tap] x,y away from the node sheet: closed, gesture
spent`.

## EST-412 CardKB on Grove

The M5Stack Unit CardKB (v1.1, ATmega8A, 0x5F) can now be used on the L2's back Grove port. The
switch is Settings > General > CONNECTIONS > **Keyboard (Grove)**, DEFAULT OFF and saved in NVS as
`grovekb`. OFF (and a boot with the key missing) leaves Grove power off, starts no bus and touches no
Grove pin. ON switches Grove VCC through expander bit 7, waits 50 ms for the ATmega, opens a SECOND
I2C bus (`Wire1`, 100 kHz) on GPIO14/GPIO13, probes 0x5F and logs `[kb] cardkb found (sda=.., scl=..)`
or `[kb] cardkb not found` (plus the short toast). The internal bus (expander, touch, ADS1115, USB-C
controller) is never touched. If the probe fails the 5 V rail stays on (the switch is on) and the bus
is closed; toggling off/on probes again. There is no hot-plug rescan.

### Safety: the data lines, and what the sources say
- **Pull-ups: no source in this tree answers it.** I searched `src/board.cpp`, `src/board.h`, all of
  `lib/` (only the MeshCore/MeshCore-companion/ed25519 trees are vendored), `reference/`, and there
  is no Meshtastic or MeshCore Wio-Tracker-L2 variant here. No file says the Grove SDA/SCL have
  pull-ups, or to which rail.
- The only pull-ups this firmware can enable are the ESP32-S3's **internal** ones. The Arduino core's
  `i2cInit()` sets `sda_pullup_en`/`scl_pullup_en` to `GPIO_PULLUP_ENABLE`
  (`framework-arduinoespressif32/cores/esp32/esp32-hal-i2c.c`, lines 81-82), and those are tied to
  the chip's 3.3 V rail; `Wire1.begin()` is the only thing that turns them on. This code has no way
  to select another rail. They do NOT protect the pins from a pull-up on the keyboard that goes to
  5 V, so the meter still decides.
- `grove on` therefore writes expander bit 7 and **nothing else**: no `Wire1`, no `pinMode`, no
  pull-up. A meter on SDA/SCL with the keyboard connected sees the keyboard's own pull-ups, not ours.
  `grove off` clears the bit; `grove status` reads the latched bit without writing anything.
- The expected reading is about 3.3 V. **Above 3.6 V: stop, do not turn the keyboard switch on.**

### Which pin is SDA
Seeed's wiki names only `GROVE_DAT_1` = GPIO14 and `GROVE_DAT_2` = GPIO13. Nothing in `src/`,
`lib/` or `reference/` assigns SDA/SCL, so the driver does what the ticket says: at enable it tries
(14 = SDA, 13 = SCL) once, then (13 = SDA, 14 = SCL) once, and logs which order answered. There are
never more than those two probes per enable.

### Keys
- Printable ASCII 0x20..0x7E is inserted through the same door as the on-screen key
  (`composeAppend` for the chat draft, `settingsEditAppendChar` for a Settings field,
  `rptrAppendText` for the RPTR sheet). The draft cursor is the end of the text: these editors have
  no movable cursor, so insertion is at the same place the on-screen key puts it.
- 0x08 Backspace is `composeBackspace` (one whole code point, as the Del key); 0x0D Enter is the
  sheet's own Send/OK (`chatSendDraft`, `settingsEditorOk`, the RPTR commit); 0x1B Esc is the
  sheet's Back (`g_kb.close`, `settingsBack`, the RPTR sheet close).
- Arrows are not ASCII. M5Stack's CardKB v1.1 key-code table (its CardKB firmware source, the same
  table published for the Unit CardKB) gives 0xB4 left, 0xB5 up, 0xB6 down, 0xB7 right. This sandbox
  had no network and there is no local copy of that source, so the four bytes could not be
  re-fetched today. `mapCode(code, hasCursor)` exposes left/right, but the shell passes
  `hasCursor = false` because no editor has a movable cursor, so both are ignored; up/down are
  ignored too. An unknown code is counted and logged (`[kb] ignored code 0xNN (n)`) and never
  inserted, so a different arrow byte on a real unit would show up there instead of corrupting a
  draft.
- A key press wakes a screen the button put to sleep, exactly like a touch, and is not applied on
  that press. The touch-feedback click is left on the TAP path only (the ticket allows it here but
  does not require it; EST-390's one-call-site rule stays).
- Polling is one call from the loop task: `cardKbStep()` runs only when the switch is on, the probe
  found the keyboard and `textFieldOpen()` is true (Chat composer incl. DM/reply, any Settings
  editor, RPTR sheet). The read is metered to ~30 ms; the idle loop sleeps 20 ms, so in practice it
  is one read per 40 ms pass (30 ms while a fade is running). A code held down is returned once; the
  0x00 between presses re-arms it, so one physical press is one character.
- The on-screen keyboard is unchanged; both work together.

### Size, build and tests
- New `src/cardkb_logic.h` (87 lines, the testable rules), `src/cardkb.{h,cpp}` (30 + 114 lines, bus
  and power). No heap and no task anywhere in the driver.
- Build with the REBUILD.md command (`PLATFORMIO_CORE_DIR=/home/slofi/.cache/pio-om-lite/core
  /home/slofi/.local/share/pipx/venvs/platformio/bin/pio run -e om_lite_l2`): REAL exit 0.
  RAM **160,872 B** (49.1% of 327,680), was 160,856 B, **+16 B**.
  Flash **2,998,821 B** (45.8% of 6,553,600), was 2,996,449 B, **+2,372 B**.
  `firmware.bin` 2,999,184 B, was 2,996,816 B (+2,368 B). Budget: +2,372 <= 8,192 B flash,
  +16 <= 256 B RAM. (An incremental build of the same tree measured 16 B more, 2,998,837 B flash
  and 2,999,200 B bin: the link/section-order noise EST-410/411 also saw. The clean build's numbers
  are the ones above.)
- The +16 B static is the seven scalars (`g_kbEnabled`, `g_kbIgnored`, and cardkb.cpp's
  found/bus/have-last/last-code/last-poll state). `Wire1` is a 76 B core global in `.bss`; the RAM
  delta shows it was already linked, so it is not a new cost.
- While the switch is ON, the Arduino core's `TwoWire::begin()` allocates its own RX and TX buffers
  before it starts the bus: `I2C_BUFFER_LENGTH` is 128 B each, so **2 x 128 = 256 B heap**
  (`libraries/Wire/src/Wire.cpp`, `allocateWireBuffer()`; `TwoWire::end()` frees both again in
  `freeWireBuffer()`). Then `i2cInit()` calls `i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0)`
  (`esp32-hal-i2c.c:90`), so the IDF's slave RX/TX ring buffers are **0 bytes each**. The IDF driver
  also allocates its own driver object and semaphores on install and a command link per transaction,
  freed after it; those are heap and were not measurable without a device. With the switch OFF none
  of that exists: no `Wire1.begin()`, no buffers, no driver install, no task, no delay. The OFF boot
  path is one `Preferences` bool read and one log line, so no boot delay was added.
- `sh test/host/run.sh`: REAL exit 0, 0 FAIL lines (the full suite, including the two new tests).
- `test/host/test_est412.cpp` proves the mapping (every printable byte, 0x00 not a key, 0x08/0x0D/
  0x1B, left/right with and without a cursor, up/down, and 17 unknown bytes), the OFF default
  (`kDefaultEnabled == false`, a missing key is OFF whatever the buffer held) and the gate (all
  eight switch/found/field combinations; only true/true/true polls).
- Armed, each against a broken header in /tmp: setting unknown codes to Char makes the mapping test
  exit 1; `kDefaultEnabled = true` makes the default test exit 1 (3 failures); changing the gate to
  `enabled && found && (textFieldOpen || !textFieldOpen)` makes the gate test exit 1 (2 failures).
  Restored, all pass.
- `test/host/test_est412.py` is the shell half: the `grove` verb/handler touches only
  `boardGrovePower`/`boardGrovePowerOn`, the row and its OFF default, the `grovekb` key, the boot
  and toggle paths, the one `cardkb::poll` call site behind `shouldPoll`, the on-screen doors, and no
  heap/task in the driver. Armed by removing the `shouldPoll` guard from a copied `main.cpp`: it
  exits 1 with `the poll is not behind the switch + found + text-field gate`.

### Could not check (no device in this run)
- Nothing about the glass or the keyboard: `grove on|off|status` has not run on hardware, no meter
  reading was taken, no CardKB was probed, and no key has been typed. The SDA/SCL order, the actual
  idle level, the CardKB's own pull-up rail and the arrow bytes are all unconfirmed here.
- The IDF driver's heap footprint while ON is not measurable without a device.

### Next step
Claude: review the diff and commit. Filip, before the keyboard switch: `grove on`, measure SDA and
SCL to GND with the CardKB connected (expect ~3.3 V, above 3.6 V stop), then `grove off`. Then turn
on Settings > General > Keyboard (Grove), watch `[kb] cardkb found (sda=.., scl=..)` (or the
not-found toast), open a chat composer and type, then try Backspace, Enter (send), Esc (close), a
deliberately ignored key, and the Settings and RPTR editors. `grove status` answers the rail state.

## EST-413 clock floor is not real time

Filip's L2 wrote power-log rows stamped 13:27, 13:28 ... at uptime 8, 68, 128 s after a power-on
while the wall clock was about 13:36, so those rows sat next to the previous boot's real 13:28-13:35
rows in the same day's CSV. Cause: `timeKeeper().begin()` seeds the clock at max(saved NVS floor,
build time) with `realSinceBoot() == false`, and the floor may be up to 10 minutes old plus the off
time. `power_log.cpp`'s `localNow()` formatted `timeKeeper().now()` without asking whether the clock
was real, so the floor was written as if it were the wall clock.

### What changed
- **Power log (`power_log.cpp`, `power_log.h`, `power_log_logic.{h,cpp}`).** The stamp rule is now
  plain, host-tested C++: `logLocalStamp(epoch, realSinceBoot, out)` refuses a not-real clock, and
  `MinuteClock::stamp()` is the one decision for a due minute: not real writes NO row and increments
  `skipped`; the first minute after a real source stamps and writes normally. `localNow()` delegates
  to it (it no longer calls `localCivil(now())` itself). `begin()` clears the count.
  `skippedMinutes()` is reported by `log power on|off|tail`; the log line is
  `[plog] no local time yet, line skipped (N skipped since boot, not real)`.
  `printTail()` returns -1 before it names a file while the clock is not real, so a floor date can
  never pick another day's run; `usb_commands.cpp` answers
  `[cmd] error log power tail: no real time yet skipped=N write_fail=M` instead of a file error.
- **Manual time (`main.cpp` `timeInitFromClock`, `timekeeper.h`).** The steppers used to start from
  `timeKeeper().now()` even when that was the floor, so the "Set time" page showed a stale floor and
  one tap on Set made it real. The new one rule, `manualTimeSeedEpoch(real, now, build)`, starts
  them from the build epoch while the clock is not real (also not a real time, but not the stale
  saved floor); the Time page above still shows "Now --:--" for the same reason.
- **Repeater clock (`screens/rptr_tab.cpp` `rptrSetClockNow`).** The button's `clockGuard` already
  refused a not-real clock, but the confirm retry reaches this write path directly, so it now checks
  `realSinceBoot()` itself before building the CLI line.
- **Not changed, by the ticket:** the floor, `kFloorSaveIntervalSec` (10 min), and the
  `clock_keep.h` kept-clock decision. A power-on stays a cold start. MeshCore's own
  `OmRTCClock::getCurrentTime()` is untouched.

### The 29 `timeKeeper().now()` / `tk.now()` sites (pre-fix line numbers) and a verdict each
| site | who reads it | verdict |
|---|---|---|
| board.cpp:1060 `aid::AidGate::shouldSend` | GPS AID send decision | OK: refuses when `!clockReal` inside `shouldSend()` |
| board.cpp:1062 `aidTimeFromEpoch` | AID payload time | OK: reached only after 1060 accepted |
| clock_keep.cpp:67 `storeRecord` | RTC_NOINIT record | OK by design: stores `real` too, and the next boot refuses `real != 1` |
| main.cpp:637 `buildStatus` | status-bar HH:MM | OK: gated, else `--:--` |
| main.cpp:994 `buildChatView` | chat/list stamp source | OK: carries `nowReal` into `stampText`, which needs both flags |
| main.cpp:1708 `clockLine` | Settings > Time "Now" | OK: gated, else `--:--` |
| main.cpp:1731 `timeInitFromClock` | manual-time steppers | **BUG, FIXED**: now seeds from the build epoch when not real |
| main.cpp:2420 settings selector preview | chat preview times | OK: `nowWall` + `nowReal` pair |
| main.cpp:4477 status-bar repaint | status-bar HH:MM | OK: gated, else `--:--` |
| mesh_link.cpp:540 `storeDm` | received DM `wall` | OK: `wallReal` stored, formatter refuses |
| mesh_link.cpp:739 `onACKRecv` | delivered ACK `wall` | OK: `ackReal` passed to `markDelivered` |
| mesh_link.cpp:862 received channel msg | message `wall` | OK: `wallReal` stored |
| mesh_link.cpp:1836 `meshDmSend` | sent DM `wall` | OK: `wallReal` stored |
| mesh_link.cpp:2543 phone channel send | message `wall` | OK: `wallReal` stored |
| mesh_link.cpp:2568 phone DM send | DM `wall` | OK: `wallReal` stored |
| mesh_link.cpp:2610 `meshLinkSend` | sent channel `wall` | OK: `wallReal` stored |
| power_log.cpp:42 `localNow` | CSV row / tail file date | **BUG, FIXED**: no row and no file name until real; skipped minutes counted |
| rptr_link.cpp:559 scan answer | repeater "heard" stamp | OK: `heardReal` stored |
| screens/map_tab.cpp:319 | map marker age/time | OK: `nowWall` + `nowReal` pair |
| screens/nodes_tab.cpp:96 | Nodes heard time / last hour | OK: `nowReal` gates the last-hour count and `stampText` |
| screens/rptr_tab.cpp:457 `rptrSetClockNow` | repeater set-clock write | **HARDENED**: its own `realSinceBoot()` check added (the guard upstream already refused) |
| screens/rptr_tab.cpp:473 `rptrClockPressed` | repeater clock guard | OK: passes `real` to `clockGuard()` |
| screens/rptr_tab.cpp:987 `rptrBuildList` | list "heard" stamps | OK: `f.heardReal` + `nowReal` |
| screens/rptr_tab.cpp:1036 `rptrBuildPage` | page clocks | OK: `nowWall` + `nowReal` pair |
| screens/rptr_tab.cpp:1042 `clockWrong` | repeater clock warning | OK: passes `real` |
| target.cpp:72 `noteRawPacket` | radio reception stamp | OK: `realSinceBoot()` stored with it |
| target.cpp:130 `OmRTCClock::getCurrentTime` | MeshCore's own clock | OK by design (the ticket's known exception: MeshCore needs rising timestamps; the floor exists for that) |
| usb_commands.cpp:131 `cmdStamp` | USB `msgs`/`node` stamps | OK: passes `nowReal` to `stampText` |
| usb_commands.cpp:192 `cmdPrintChats` | USB `chats` check | OK: `nowWall` + `nowReal` pair |

No user-facing site other than the three above reads the floor as if it were real.

### Size, build and tests
- Build with the REBUILD.md command: REAL exit 0.
  RAM **160,880 B** (49.1% of 327,680), was 160,872 B, **+8 B** (the `MinuteClock` count).
  Flash **2,999,305 B** (45.8% of 6,553,600), was 2,998,845 B, **+460 B**.
  `firmware.bin` 2,999,664 B, was 2,999,216 B (+448 B).
- `sh test/host/run.sh`: REAL exit 0, 0 FAIL lines (the whole suite, including the two new tests).
- `test/host/test_est413.cpp`: while not real, two due minutes write no row and count 2 skips; after
  a real source the same minute stamps 13:36 local (CET) and the count stops at 2; the next minute
  stamps 13:37. `logLocalStamp(false, ...)` is refused and leaves the caller's stamp untouched
  (no half-filled row). `manualTimeSeedEpoch(false, floor, build)` is the build epoch, never the
  floor; `true` uses the clock.
- **Armed against the old `localNow()`**: with the real check removed from `logLocalStamp()` and the
  `MinuteClock` gate changed to the old path (both in a temporary /tmp tree edit, restored
  afterwards), the same test exits 1 with 8 failures: the two not-real minutes both stamp, the skip
  count stays 0, and the sentinel stamp is overwritten. Restored, it exits 0.
- `test/host/test_est413.py` is the source half: the logger stamps through `MinuteClock`,
  `localNow()` no longer calls `localCivil(now())` itself, `printTail()` checks `realSinceBoot()`
  before `formatFileName`, `usb_commands.cpp` says "no real time yet" before `printTail()` and
  reports `skipped`, `timeInitFromClock()` uses `manualTimeSeedEpoch(...)`, `rptrSetClockNow()`
  checks `realSinceBoot()` before `cliTime`, and the floor interval / kept-clock decision are
  unchanged.

### Could not check (no device in this run)
- Nothing about the glass or a real power cycle: no L2 was switched off/on, no CSV was written, and
  `log power tail` was not run on hardware. The fix is host-tested logic plus the build; the
  "13:27 next to 13:28" file has not been reproduced or re-checked on the card.
- The exact skip count the device prints after a real cold start (it depends on when the first GPS
  or Wi-Fi source lands) was not observed.

### Next step
Claude: review the diff and commit. Filip: power the L2 off for a minute and on, let it boot without
a fix, run `log power tail` (expect `no real time yet skipped=N`), then let GPS or Wi-Fi set the
clock and run it again (expect today's file and the same `skipped` count); the first CSV row after
that should carry the real wall minute.

## EST-415 UI tweaks (EST-414 continued)

EST-414 was stopped by the 30-minute cap (exit 124) with no report and nothing committed. This entry
starts from its working tree, which already holds the five glass-test changes; EST-415 finishes and
verifies them, then folds the six per-ticket preview generators into one `tools/preview.py`.

State of the five items in the tree EST-415 inherited (evidence against the working tree, which is
what will be committed):

1. **Scroll indicator on the Nodes list, the chat list and an open chat: DONE.**
   One geometry (`src/ui/scroll_indicator.h`, `scrollIndicatorFor()`) and one drawing helper
   (`src/ui/scroll_indicator.cpp`, `drawScrollIndicator()`); the three lists pass their own layout
   rectangle:
   - Nodes: `src/ui/screen_nodes.cpp:130-137` (`body = L.listBody()`, top-down, `false`).
   - chat/channel list: `src/ui/chat_selector.cpp:128-134` (`list`, top-down, `false`).
   - open chat: `src/ui/screen_chat.cpp:268-274` (`g.msgs`, bottom-anchored, `true`).
2. **The end bounce on those three, reusing the Node Info rule: DONE.**
   `listGiveScroll()` / `listSnapScroll()` are the ONE rule in `src/ui/layout.h:156-167`; the old
   `nodeInfoDragScroll` / `nodeInfoSnapScroll`, `settings*` and `selDragScroll` names forward to it
   (`src/ui/node_info_layout.h:47-56`, `src/ui/settings_layout.h:371-381`,
   `src/ui/chat_layout.cpp:544-550`). Nodes keeps the drag's give through `refreshNodes()`
   (`src/screens/nodes_tab.cpp:141-146`, drag `:548-554`, release `:577-579`); the chat's channel list
   and message band use it in `src/main.cpp:1077-1081`, `:5183`, `:5195`, with the release snap at
   `:4904` and `:4912`.
3. **RPTR does not auto scan when the tab opens: DONE in this tree, contrary to the ticket note.**
   `src/main.cpp:3793-3797` replaces the EST-362 `rptrScanStart()` block; `rptrScanStart` and
   `[rptr] auto scan` no longer occur anywhere in `src/main.cpp` (`grep` confirms). The Scan chip
   still starts one (`src/screens/rptr_tab.cpp:1559`).
4. **Settings > Channels without the unread count: DONE.** `src/main.cpp:2103-2114`: the row value is
   only ON / muted / OFF; the `meshUnread` / `"+%d"` append is gone. The Chat tab's own unread
   badge and per-chat dot/count are untouched.
5. **Settings > "Open repeater" tappable, jumping to the RPTR tab with the open session: DONE.**
   Live Chevron while a session is open and a plain `none` value otherwise
   (`src/main.cpp:1979-1985`); `A_RptrOpen` (`:1608`) is handled at `:2828-2836`: it leaves Settings
   through `settingsLeaveState()` and calls `showTab(3)` (or repaints the RPTR tab in place), without
   clearing or re-opening the session. The tap still comes from `settingsRowHit()`, the same rows the
   drawing uses.

What EST-415 itself changes:
- Re-read the five items against the inherited tree. All five are present; item 3 is fixed in this
  tree even though the ticket note said it was not: the EST-362 block is gone from `showTab()` and
  `grep -n "rptrScanStart" src/main.cpp` finds nothing and `"auto scan"` finds only the explanatory
  comment.
- One small hardening in the bounce (item 2): `nodes_tab::beginDrag()` now clears `g_nodeDrag`, so an
  interrupted gesture (a screen change before the finger lifts) cannot leave the "keep the give" flag
  true and stop the next refresh from snapping (`src/screens/nodes_tab.cpp:488-495`). The release
  and the new test still use the same path.
- Folded the six preview generators into `tools/preview.py` and deleted
  `tools/mkest400.py`, `mkest402.py`, `mkest404.py`, `mkest405.py`, `mkest406.py` and `mkest414.py`
  (the last was untracked; git keeps the committed five). The previews in `docs/shots/` are untouched.

### Preview tool (EST-415)
`tools/preview.py` is the one host renderer for the previews the tickets needed. It still compiles a
small dumper with the system `g++` over the firmware's own headers, so the rectangles and colours are
the firmware's, and fills them with PIL. It is NOT a screenshot: no frame comes off the device and the
text is desktop DejaVu, not the VLW faces.

How to call it:
```
python3 tools/preview.py                    # every preview
python3 tools/preview.py est414             # one (est400, est402, est404, est405, est406, est414)
python3 tools/preview.py est400 est405      # any set
PREVIEW_OUT=/tmp/png python3 tools/preview.py   # write elsewhere; default is docs/shots
```
What it renders, and the output each name writes:
| name | output in `docs/shots/` | what it shows |
|---|---|---|
| `est400` | `2026-11-11-est400-polish.png` | the toast plate, the Settings Warn row, the Info page clip at two scrolls |
| `est402` | `2026-11-11-est402-charging-bolt.png` | the status bar with and without the charging bolt, 8x icon crop |
| `est404` | `2026-11-11-est404-bolt-tips.png` | the bolt over the icon, four states, 1x and 8x |
| `est405` | `2026-11-11-est405-auto-off-rows.png` | Settings > General display rows, switch OFF and ON |
| `est406` | `2026-11-11-est406-send-button.png` | the send key on the composer line, empty vs "hello" |
| `est414` | `2026-11-12-est414-scroll-indicator.png` | the indicator on the Nodes list, the channel list and an open chat |

### Size, build and tests
- Build with the REBUILD.md command: REAL exit 0 (after the final edit).
  RAM **160,888 B** (49.1% of 327,680), was 160,880 B, **+8 B** (the drag flag's reset).
  Flash **3,000,161 B** (45.8% of 6,553,600), was 2,999,305 B, **+856 B**.
  `firmware.bin` 3,000,528 B.
- `sh test/host/run.sh`: REAL exit 0, 0 `FAIL` lines, including both new EST-414 tests.
- `test/host/test_est414.cpp` (compiled by run.sh): indicator geometry on all three lists (proportional
  thumb, top/middle/end, bottom-anchored chat at rest, minimum thumb, nothing when the content fits,
  track never over text) and the one give/snap rule with every old name answering the same numbers.
- `test/host/test_est414.py`: REAL exit 0 on the current tree. **Armed**: against an export of HEAD
  (`git archive HEAD`, the pre-EST-414 tree) it exits 1 with 59 failures, including the old
  `[rptr] auto scan` line, the `meshUnread` append, the missing `A_RptrOpen` and the old chat clamps.
- `tools/preview.py`: `python3 -m py_compile` OK; all six renderers ran with REAL exit 0 to a temp
  directory. Five regenerated PNGs are byte-identical to the committed previews. `est402` differs
  only inside the bottom 8x icon crop (the committed PNG is slightly older than the current generator);
  the committed preview was left as it is, as the ticket asks.

### Could not check (no device in this run)
- Nothing about the glass: no L2 was flashed, the indicator has not been seen at the real fonts and
  panel, no finger has pulled a list past its end, the RPTR tab has not been opened to confirm it stays
  quiet, and the "Open repeater" row has not been tapped on hardware. All of that is host geometry,
  source checks and the build.
- The previews are host renders, not device screenshots; they can be wrong about the VLW text but not
  about the rectangles and colours, which come from the headers.

### Next step
Claude: review the diff, the deleted generators and `tools/preview.py`, then commit. Filip: flash and
check the five on the glass, especially that the list gives a little at either end and that Settings >
RPTR > Open repeater lands on the open session's page.

## EST-416 map: the top layer wins the tap, no jump on a fast re-touch

### What was wrong (both found by Filip on the glass, 2026-11-12)
1. On the Map, the "-" zoom tab had a repeater marker under it. Tapping "-" opened the repeater's
   info pop-up instead of zooming out. `ui/map_chrome.cpp`'s `mapHitAt()` tested MARKERS FIRST (the
   EST-374 design choice "a finger on a dot is a node, even if the dot is under a tab"). That is
   wrong: whatever is DRAWN on top must win.
2. After moving the map, touching it again too soon made it jump (no fast panning). The press path in
   `main.cpp` treated a press within 400 ms / 80 px of the previous release as a pan continuation
   (right: the panel lifts between samples), but then called `om::mapPan(g_lastX - x, ...)` while
   `g_lastX/Y` were still the PREVIOUS swipe's end point. A fast new swipe starts back near where the
   last one started, so the map jumped by up to 80 px at touch-down.

### The fix (one order, no second copy)
- `ui/map_chrome.cpp` `mapHitAt()` is now the DRAW order, top layer first, and it depends on the bars:
  - **shown**: status bar -> chip -> the five tabs -> the callout -> markers -> map;
  - **hidden**: status bar -> chip -> the 6 px slivers -> the visible ghost + / - glyph boxes ->
    the callout -> markers -> the REST of the ghost zoom zones -> map.
  So a marker under a shown button never wins; under a hidden ghost glyph it does not win either;
  elsewhere in a hidden ghost zone it does.
- `ui/map_chrome.h` adds `mapZoomGlyphRect(zone)`: the small icon square a ghost glyph is drawn into,
  centred on the zone's centre. `ui/screen_map.cpp`'s `drawTabsHidden()` hands that same rect to
  `drawZoomGlyph()`, so the drawn pixels and the hit box are one rectangle (the glyph's ink, 12 x 9
  for "+", 5 x 4 for "-", is inside it). No new number: it is the existing `kMapTabIconW` square the
  tab width was already built from.
- `mapZoomGlyphRect()` is plainly visible in the drawing and the test, so it cannot drift.
  `pressTargetForMap()` was not touched: it already derives from `mapHitAt()`, so the pressed look
  follows (the shown tab's rect, the chip's 30 x 26 target, the callout, the hidden ghost's shown
  rect).
- `ui/map_chrome.h` also adds `mapPressPan()` (plain C++, host-testable): it keeps the continuation
  (400 ms, 80 px, on the Map, not spent, a previous release exists) but returns the press point as
  the pan base and no delta. `main.cpp` now sets `g_lastX/Y` from that base and does not pan on the
  press; the one remaining `om::mapPan(g_lastX - x, ...)` is the MOVE path, so the first move pans by
  its own delta from where the finger landed.
- `test/host/test_est381.py` updated for the new call shape: it still checks that the continuation is
  behind `g_tab == 2` and `!g_spent`.

### Tests
- New `test/host/test_est416.cpp` (added to `run.sh`, BIN66), plain g++:
  - shown: a marker under the status bar, the chip, each of the five tabs and the callout does NOT
    win; the button's kind answers, and `pressTargetForMap()` returns that button's rect. A marker on
    the plain map, and one 2 px left of the tab column, still wins.
  - hidden: a marker under the visible ghost +/- glyph loses (ZoomIn / ZoomOut); a marker elsewhere in
    the ghost zone (left of the glyph box, above it) wins, and the same point with no marker still
    zooms; the 6 px sliver, the chip, the callout win over a marker; the 3 px tucked strip is still a
    map tap; a marker on the plain map wins.
  - pan: a press 60 px from the release within 120 ms gives zero delta at the press and the press
    point as the base; the next move to (180,130) pans by its own 20 px each way, not the old 80.
    Edges 399/400 ms and 79/80 px, a leftward press, a millis wrap, plus not-on-Map / spent / no
    release. A source check: `main.cpp` has exactly ONE `om::mapPan(g_lastX - (int)x` call (the move
    path) and calls `mapPressPan()`.
- Turned the old markers-first assertions around: `test_est351.cpp` (a marker on the zoom tab is
  now ZoomIn), `test_est370.cpp` (a marker on Names is now Names), `test_est374.cpp` (a marker at the
  hidden ghost centre is now ZoomIn; one elsewhere in the zone is still Marker).
- **Armed**: the new test built against the pre-fix `src/ui/map_chrome.cpp` exported from HEAD exits 1
  with 38 failures, all the marker-under-chrome cases (chip, every tab, callout, hidden ghost glyphs,
  slivers); the retained cases pass. The pan source check fails on the old two-call `main.cpp`.

### Size, build and tests
- Build with the REBUILD.md command: REAL exit 0.
  RAM **160,888 B** (49.1% of 327,680), was 160,888 B, **+0 B**.
  Flash **3,000,485 B** (45.8% of 6,553,600), was 3,000,161 B, **+324 B**.
  `firmware.bin` 3,000,848 B (was 3,000,528 B, +320 B).
- `sh test/host/run.sh`: REAL exit 0, 0 `FAIL` lines, including the new EST-416 test and the three
  turned-around map tests.

### Could not check (no device in this run)
- Nothing about the glass: no L2 was flashed. Not seen at the real fonts and panel: the ghost glyph
  box's exact feel under a finger (it is the tab's 17 px icon square, deliberately small), nor the
  re-touch feel after a pan (the host test proves the arithmetic only). Filip has to confirm both on
  the glass: the "-" tab must zoom out with a repeater under it, and a quick second swipe must pan
  without a jump.

### Next step
Claude: review this diff, then commit. Filip: flash and check the two on the glass: (1) with a
repeater dot under the "-" tab, tapping "-" zooms out; with the bars hidden, tapping the grey "+" /
"-" zooms, while a dot elsewhere in that zone still opens the node; (2) a quick re-touch after a pan
follows the finger with no jump.

## EST-417 CardKB arrows navigate

Filip's design (2026-11-12), from the bytes he measured on his own CardKB (the EST-412
`[kb] ignored code ...` log): the arrows now navigate every screen, Fn+left / Fn+right step the tabs
everywhere, the Map's plain arrows pan, and a keyboard pulled out while the switch is ON is noticed
instead of being polled forever.

### What changed, rule by rule (the ticket's numbers)
1. **Poll gate.** `shouldPoll(enabled, found)`: switch ON + keyboard found, on ANY screen (it was
   ON + found + a text field open). Same ~30 ms cadence, still the one call in `cardKbStep()` from
   the loop task. Switch OFF is untouched: no power, no bus, no reads.
2. **List screens** (no text field, no sheet/pop-up): left/right switch to the previous/next tab and
   STOP at the ends (no wrap), up/down scroll the current list one row per press: the Nodes list,
   the Nodes Info page, the Chat channel list (the selector), an open chat's messages, the Settings
   pages, the RPTR list and RPTR pages. Each list steps by its OWN row height (`kRowH` 30,
   `kSelRowH` 30, `kNodeInfoRowH` 20, `kRptrRowH` 36, `kRptrPageRowH` 30) and through the SAME
   `ui::listSnapScroll` clamp a finger's release uses, so EST-414's indicator and ends follow with no
   second rule. A key has no fingertip, so there is no `kSelGive` stretch: the ends stop dead.
3. **Fn+left / Fn+right** step the tab row on EVERY screen that is not a text field or a sheet. The
   row that is on the glass is the one stepped: the four main tabs through `showTab()` normally, and
   Settings' own five tabs inside Settings (the same page change a tab tap makes, its history reset
   included).
4. **Map:** plain arrows pan one fixed step, a quarter of the canvas (`ui::kMapKeyPanX` 80,
   `kMapKeyPanY` 53), through `map_tab::panStep()`, which takes the DRAG's own path: close the
   callout, drop the follow lock, `om::mapPan()`, draw. So markers, the callout and a running trace
   re-project exactly as under a finger. Fn+left/Fn+right still switch tabs; **Fn+up / Fn+down zoom**
   in/out. The zoom is one constant (`keynav::kFnZoomOnMap`) plus two table entries, so Filip can
   drop it without touching anything else.
5. **Text field open** (composer, DM, reply, Settings editors, RPTR sheet): up/down scroll the chat
   behind the composer, and only there (a Settings editor and the RPTR sheet have no chat to scroll,
   so they do nothing); left/right and both Fn pairs do nothing; Esc/Enter/Backspace are exactly as
   EST-412 left them.
6. **Sheet or pop-up open** (message menu, advert menu, Settings dropdown, clear ask, Position
   choice, own-position picker, the Nodes sheets, the RPTR confirmation and map picker): every arrow
   does nothing, Esc closes as today. The set is ONE function, `popupOpen()`, which is the auto-off
   timer's EST-405 list moved into a name, so the timer and the arrows cannot disagree.
7. **A key press still wakes a sleeping screen first** and is not applied on that press, arrows
   included (the check is before the arrow routes).
8. **Unplugged keyboard:** `cardkb::poll()` now counts consecutive failed reads (a refused
   `requestFrom` or a bad `read()`; the 30 ms metering early-return does not count). At
   `kFailLimit = 10` in a row (~0.3-0.4 s) it logs `[kb] keyboard disconnected` ONCE, closes the
   bus, clears found, drops Grove power through `boardGrovePower(false)` (the not-found path's own
   ending) and raises a one-shot flag; `cardKbStep()` takes that flag and shows the short toast
   "Keyboard disconnected". A single failed read changes nothing (a good read clears the run). The
   switch stays ON; OFF then ON probes again. Counter and flag are 5 bytes.
9. **Pin order:** the probe now tries SDA = GPIO13, SCL = GPIO14 FIRST (the order that answered on
   Filip's unit) and keeps (14, 13) as the one fallback; still never more than two probes, on enable
   only. Both log lines print the pair that answered.
10. **The arrow bytes:** 0xB4 left, 0xB7 right, 0xB5 up, 0xB6 down and the one-shot Fn bytes 0x98
    Fn+left, 0xA5 Fn+right, 0x99 Fn+up, 0xA4 Fn+down are in `cardkb_logic.h` with `arrowFor()`, the
    one byte-to-arrow door. Every arrow is decided there BEFORE `mapCode()`, so no arrow byte can be
    swallowed as an ignored code any more; a byte that is neither an arrow nor a named key is still
    counted and logged, never inserted.

### Where the code is
- New `src/keynav_logic.h` (88 lines, plain C++): `Context` (List / Map / TextField / Sheet),
  `Act`, the one `actFor(arrow, context)` table, `tabStep()` (no wrap) and `keyScroll()` (one row,
  then `ui::listSnapScroll`). It is the whole rule; `main.cpp` only wires each action to the
  function a tap, a drag or a tab tap already uses.
- `src/main.cpp`: `popupOpen()`, `keynavContext()`, `keynavTabStep()`, `keynavScroll()`,
  `cardKbArrow()`, and `cardKbKey()` asking `arrowFor()` first. `cardKbStep()` takes the lost notice
  before the gate and calls the two-argument `shouldPoll`.
- `src/screens/nodes_tab.*` and `src/screens/rptr_tab.*`: `scrollByRow(dir)`, each screen's own
  scroll state, row heights and ceiling, the same clamp.
- `src/screens/map_tab.*`: `panStep(dx, dy)` and `zoomStep(delta)`; `src/ui/map_chrome.h` holds the
  quarter-canvas step next to the canvas it moves.
- `src/cardkb.{h,cpp}`: the pin order, the failed-read counter, `noteFailedRead()` and
  `takeLostNotice()`.

### One decision to flag (Claude)
The Chat selector is a dropdown (a pop-up for the auto-off timer) but the ticket names the "Chat
channel list" as one of the lists the arrows scroll. So for the ARROWS it is a list: up/down scroll
it, and left/right step tabs like any list screen (the dropdown closes on a tab switch, as it
already did). Every other pop-up stays a sheet. Say so if Filip wants left/right to do nothing in
that dropdown; it is one condition in `keynavContext()`.

Also flagged: the Nodes Info page is not a sheet, so it scrolls with up/down and Fn+left/right still
switches tabs while it is up (the page stays open behind, like the RPTR page and its scroll). The
ticket does not name it either way.

### Size, build and tests
- Build with the REBUILD.md command: REAL exit 0 (the clean build, and again after the final edit).
  RAM **160,896 B** (49.1% of 327,680), was 160,888 B, **+8 B** (the failed-read counter and the lost
  notice, padded).
  Flash **3,001,925 B** (45.8% of 6,553,600), was 3,000,485 B, **+1,440 B**.
  `firmware.bin` 3,002,288 B (was 3,000,848 B, +1,440 B). The final incremental build measured 16 B
  more (Flash 3,001,941 B, bin 3,002,304 B), the link/section-order noise EST-410/411/416 also saw;
  the clean build's numbers are the ones above.
- `sh test/host/run.sh`: REAL exit 0, 0 `FAIL` lines, including the new EST-417 test and the updated
  EST-412 gate test.
- `test/host/test_est417.cpp` (plain g++, no Arduino): every measured byte -> its Arrow (and all 256
  bytes checked for "only those eight are arrows"), the key-to-action TABLE written out by hand for
  all eight arrows in all four contexts (list / map / text field / sheet), the no-wrap tab step at
  both ends of 4- and 5-tab rows, one press = one row of each list through the release clamp (stops
  dead at both ends, no give, content that fits never moves), the quarter-canvas map step, the
  lost-keyboard limit (1..N-1 keep, N loses, 10 x 30 ms inside 0.3-0.4 s) and the gate (switch off
  never reads).
- `test/host/test_est417.py`, the source half: SDA 13 probed FIRST with the (14, 13) fallback kept
  and exactly two probes; the disconnect path (one log line, bus down, found cleared, Grove power
  off, the toast flag, both failure kinds counted, a good read clearing the run, the switch never
  touched, the notice taken before the gate); the two-argument gate with no text field in it and one
  poll call site; `arrowFor()` before `mapCode()`; every `Act` handled; `popupOpen()` defined once
  with both readers and all its members; the tab stepper through `keynav::tabStep` with no `%`
  wrapper; every list through `keynav::keyScroll` with its own height and ceiling, the bottom-anchored
  chat sign, and a text field scrolling nothing but a chat; the Nodes/RPTR `scrollByRow` (release
  clamp, own rows, no give, refuses while a sheet is up); the map key pan through
  `clearSelection`/`s_lock = false`/`mapPan`/`draw` (never a `mapSetView` jump), the key zoom through
  `mapZoomBy`, and the shell keeping exactly its two drag `mapPan` callers.
- **Armed.** C++ copy in /tmp, one mutation at a time: `kFnZoomOnMap = false` -> exit 1 (5 failures,
  the two Map zoom rows plus the constant); a wrapping `tabStep` -> exit 1 (4 failures, both ends of
  both rows); plain left on the Map returning TabPrev (the bug the table caught while writing it) ->
  exit 1 (1 failure); `kFailLimit = 11` -> exit 1 (1 failure); removing the sheet early-return ->
  exit 1 (12 failures). Restored, exit 0.
  Python against an export of HEAD (the pre-EST-417 tree, `git archive HEAD`): exit 1 with 92
  failures (no `keynav_logic.h`, the old gate, no arrow routing, SDA 14 first, no disconnect path).
  `test_est412.py` against the same HEAD export: exit 1 with 3 failures (the old three-argument gate
  text); the updated `test_est412.cpp` against HEAD's `cardkb_logic.h`: compile error, exit 1 (too
  few arguments to `shouldPoll`).

### Could not check (no device in this run)
- Nothing about the glass or the keyboard: no L2 was flashed and no CardKB was polled here. Not
  tested on hardware: that the eight measured bytes are what a press really sends (Filip measured
  them), that the ~0.3 s unplug detection feels right, that a pulled keyboard's toast is seen, that
  one row per press is the right amount to scroll on each list, that a quarter canvas is the right
  pan step, and that Fn+up/down zoom is wanted. The SDA/SCL order change is still the pin order
  Filip's probe reported; no meter reading was taken in this run, so EST-412's safety note stands
  (about 3.3 V expected, above 3.6 V stop).
- The scrolling arithmetic is host geometry, not what a row looks like at the real fonts.

### Next step
Claude: review this diff (especially the two flagged decisions) and commit. Filip: flash, turn the
keyboard on, and on the glass: (1) on Nodes press up/down to scroll and left/right to change tab,
including at both ends (no wrap); (2) on Chat with the composer open scroll the messages behind it
with up/down, and left/right must do nothing; (3) open the Map and pan with the plain arrows, then
Fn+up / Fn+down to zoom and Fn+left / Fn+right to switch tabs; (4) open a node sheet and a dropdown
and press every arrow (nothing moves, Esc closes); (5) pull the keyboard out while the switch is on:
one `[kb] keyboard disconnected` line, the toast, and the 5 V rail off, then toggle the switch off
and on to probe again.

## EST-418 battery log: satellites heard

**The finding behind it (S497, Claude):** the log's `sats` column is GGA field 7, the satellites
USED in the fix, so under a window `sats 0` meant only "no fix", not "no sky": 9.5 hours of it was
read as "no satellites" and had to be corrected. The number that says whether the GPS can hear the
sky at all (enough for GPS time) is the satellites HEARD, which the heartbeat already prints as
`heard=N ... maxheard=M`. This ticket puts that number in the battery log.

### What changed
- `power_log_logic.h`: `Sample` gains `uint8_t heard` (satellites heard at that minute, the same
  `gps.heardTotal` the heartbeat prints). `kPowerLogHeader` gains ONE name at the END,
  `...,ain1,ain2,ain3,heard` (17 columns), and the comment beside it now says what the two GPS
  columns mean: `sats` = used in the fix, `heard` = heard.
- `power_log_logic.cpp`: the row formatter prints `heard` last. Every earlier field keeps its
  position, so a reader of the old 16 columns reads the same bytes.
- `power_log.h/.cpp`: `plog::State` gains `heard`; the line takes it from the caller (the logger
  does not touch the GPS) and the `[plog] write` serial line prints `heard=%u` last, like it already
  prints the other row values.
- `main.cpp`: the one place that fills `plog::State` now passes `g_gps.heardTotal`, the very value
  the heartbeat's `heard=` prints.
- No file already on the card is rewritten: a day file that exists keeps its old header line, and
  the new column simply appears as the row's 17th field (the host reader copes with a longer row).

### Tests
- New `test/host/test_est418.cpp` (plain g++, no Arduino), in `run.sh` as `$BIN68`:
  1. the header is the OLD 16 names byte for byte, then exactly `,heard` (16 commas, 17 columns);
  2. a formatted row: the first 16 fields equal the pre-EST-418 row, pinned literally in the test
     (the arm), and the row continues with exactly `,15\n`; read as 17 columns, `sats` keeps its
     place (field 7) and `heard` is the one after `ain3`;
  3. the S497 case: `fix 0` + `sats 0` with `heard 9` is a real line, and a dark sky is a real 0 in
     both columns, not an "unknown" mark;
  4. edges: a default `Sample` writes `heard 0`, the full uint8 range (`255`) prints whole, and
     header + row still fit the 256-byte buffer the device hands to `File::write()`; an existing
     file gets the row only (its old header line stays).
- Updated for the one new field: `test_est377.cpp` (the exact line, 16 commas) and
  `test_est401.cpp` (the exact line, the split into 17 columns, the four EST-401 fields now followed
  by `heard`). No other test touches the row.
- `sh test/host/run.sh`: REAL exit 0, 0 `FAIL` lines, including the new EST-418 test.
- **Armed.** Copies of `test_est418.cpp` + `power_log_logic.*` in /tmp, one mutation at a time:
  `heard` printed right after `sats` instead of last (format and args moved together) -> exit 1,
  8 failures; the header's `,heard` removed -> exit 1, 3 failures; the formatter printing `sats` in
  the new field (the S497 confusion itself) -> exit 1, 6 failures. Restored, exit 0.

### Size, build and tests
- Build with the REBUILD.md command: REAL exit 0 (incremental after the edit, then a clean build;
  the clean numbers are the ones below).
  RAM **160,896 B** (49.1% of 327,680), unchanged: the new `uint8_t` fits the padding the two
  `uint8_t`s before it already had.
  Flash **3,001,973 B** (45.8% of 6,553,600), was 3,001,925 B, **+48 B**.
  `firmware.bin` 3,002,336 B (was 3,002,288 B, +48 B). The incremental build measured 16 B more
  (Flash 3,001,989 B, bin 3,002,352 B), the same link/section-order noise EST-410/411/416/417 saw.

### Could not check (no device in this run)
- Nothing on the glass: no L2 was flashed and no row was written to a card here, so the log itself
  was not seen. The formatter is host-tested; that `g_gps.heardTotal` really reaches the file is one
  line in `main.cpp` and Claude can check it in the `[plog] write ... heard=` serial line after a
  flash (that line now prints the same number last).
- The host-side reader of these CSV files is not in this folder, so "a longer row still parses" is
  the ticket's own statement, not something this run could run. Nothing here writes a header into
  an existing day file, so an old file keeps its 16-name header and gains a 17th value per row.
- The battery cost of the extra number is a `uint8_t` read and a few characters a minute: not
  measured, and far below the EST-401 I2C reads that already run on the same minute.

### Next step
Claude: review and commit. Filip: after a flash, `log power tail` should show the header with
`,heard` on a NEW day file (fresh card or a new date), and the last number of each row should follow
the heartbeat's `heard=`, not `used=`; a night in a window should show `sats 0` with a `heard` that
is still above zero whenever the GPS can hear the sky.

## EST-421 CardKB start window

**The bug (Filip, 2026-11-13):** on startup the L2 said "CardKB not found" although the keyboard was
plugged in and the switch ON, and also after toggling the switch. **The cause, proven on the device by
Claude 08:52-08:54:** `cardkb::begin()` powered the Grove 5 V rail, waited a fixed `kSettleMs = 50`,
probed twice and, on no answer, dropped the rail again. The CardKB's own ATmega8A needs longer than
50 ms after power-up: with the switch OFF, `grove on` over USB, about 70 s of waiting and then the
switch ON, it logged `[kb] cardkb found (sda=13 scl=14)` at once and worked. Yesterday's "works" at
14:27 was masked the same way (`grove on` had been sent minutes earlier). Not the pin order, not the
EST-417 lost-keyboard rule.

### What changed
- `src/cardkb_logic.h`: new `StartWindow` state machine, plain C++ (no Arduino, no Wire) so `test/host`
  proves it. `kStartWindowMs = 3000`, `kProbeIntervalMs = 100`. `next(nowMs)` returns one of
  `WindowAction::Idle / Probe / TimedOut` in a `WindowStep` (the pin order for that attempt and the
  elapsed ms). The first probe is due at once (elapsed 0, the keyboard may answer immediately if the
  rail never went down), then one every 100 ms; even attempts are SDA 13 / SCL 14 (Filip's measured
  order, EST-417), odd attempts swap. The timeout is checked BEFORE the due time, so the last probe is
  at 2900 ms and the window ends at exactly 3000 ms with no probe at the edge. The next due time is
  `nowMs + 100`, not `due += 100`, so a slow loop pass cannot bunch two probes up. `cancel()` closes
  the window (switch OFF), `start()` resets time, order and count.
- `src/cardkb.cpp`: `begin()` now powers the rail and arms the window, then returns at once: no
  `delay(`, no probe, no settle. Its bool now means "the rail is on / the window is running" and is
  false only when the expander write failed. New `step(nowMs)` is the only place the window advances:
  on `Probe` it opens `Wire1` in the step's order, probes 0x5F, keeps the bus on the answer and logs
  `[kb] cardkb found (sda=%d scl=%d) after %lu ms` (N = ms since the rail went on), closing the
  window; on a failed attempt it closes that half-open bus. On `TimedOut` it closes the bus, drops the
  Grove rail and logs `[kb] cardkb not found after 3000 ms, Grove power off`, setting a one-shot
  not-found notice for the shell. `end()` also cancels an open window and clears that notice, so
  switching OFF mid-window means no probes, no timeout and no toast. `found()` is still
  `s_found && s_busUp`, so while a window is open it is false and `shouldPoll()` reads nothing.
  `takeNotFoundNotice()` is the driver's one flag to the shell, exactly like EST-417's lost notice.
  No allocation, no task, no second call site.
- `src/cardkb.h`: `step()` and `takeNotFoundNotice()` declared, `begin()`/`end()` comments say what
  they now do.
- `src/main.cpp`: the boot and the Settings toggle call `cardkb::begin()` and do not wait; a false
  return (expander only) toasts "Grove power failed". `cardKbStep()` calls `om::cardkb::step(nowMs)`
  once per loop pass BEFORE the gate (so the window advances on every pass, on any screen), then takes
  the EST-417 lost notice and the new not-found notice, which toasts
  "CardKB not found (switch off/on to retry)". Then the unchanged
  `shouldPoll(g_kbEnabled, om::cardkb::found())` and the one `poll` call. Turning the switch OFF
  during the window goes through `end()` and cancels it. `textFieldOpen`/arrow handling untouched.
- Kept from EST-412/417: OFF means no power and no bus, the lost-keyboard rule and its toast, the
  arrows, the pin order, the one poll call site and the gate.

### Tests
- New `test/host/test_est421.cpp` (plain g++, no Arduino), in `run.sh` as `$BIN69`. It drives the
  state machine 1 ms at a time: an answer at 0, 400 and 2900 ms is found with N = 0, 400, 2900 and
  the right attempt count (1, 5, 30); no answer is a timeout at exactly 3000 ms (the loop to 2999
  never times out, the closing millisecond does, and later passes are Idle, so no second toast); the
  switch OFF at 150 ms leaves 2 probes, no timeout; cancel before the first probe probes nothing;
  cancel after the answer changes nothing; attempts are exactly 100 ms apart and alternate
  (13 first on even attempts); a slow 250 ms pass gives one probe then the next at 350, not a burst;
  a fresh `start()` after OFF/ON probes at once with 13 first and count 0; `kStartWindowMs == 3000`
  and `kProbeIntervalMs == 100`.
- New `test/host/test_est421.py`, the source half: the constants and the state machine exist; `begin()`
  has `boardGrovePower(true)` + `s_window.start(millis())` and no probe and no blocking wait (no
  `delay(` anywhere in `cardkb.cpp`, no `kSettleMs`); `step()` asks the state machine, probes through
  `openAndProbe(sda, scl)` in both orders, prints the after-N line, cancels on the answer, and in the
  timeout branch drops the rail BEFORE the not-found line and raises the notice; `end()` cancels the
  window and clears the notice; the shell advances the window once, before the gate, and shows both
  toasts; the boot/toggle paths no longer claim "CardKB not found" from `begin()`; the poll still has
  one call site.
- Updated for the new enable path: `test_est412.py` (the two probe orders now live in `step()`, one
  probe call site, the not-found line is the 3000 ms one with the rail dropped first, and the toggle
  no longer toasts "not found") and `test_est417.py` (SDA 13 first is checked in `step()`, `begin()`
  must not probe, one probe call site). Their C++ halves are unchanged.
- `sh test/host/run.sh`: REAL exit 0, 0 `FAIL` lines, including the new EST-421 test (`est421: CardKB
  start window: answers at 0/400/2900 -> found N, timeout at 3000, cancel, 100 ms alternating probes`
  and the source line).
- **Armed against the current 50 ms code.** The new `test_est421.cpp` against an export of HEAD
  (`git archive HEAD`, the pre-fix tree): compile error, exit 1 (no `StartWindow`, no
  `kStartWindowMs`). `test_est421.py` against the same export: exit 1 with 33 failures (the old
  `delay(`, the two probes in `begin()`, no `step()`, the old toast). Mutations of the new header,
  one at a time in /tmp: `kStartWindowMs = 3001` -> exit 1 (4 failures, the exact 3000 timeout);
  `sda13First = true` (no alternation) -> exit 1 (17); `cancel()` empty -> exit 1 (14); first probe
  after 100 ms -> exit 1 (13); `kProbeIntervalMs = 50` -> exit 1 (12); timeout 100 ms early -> exit 1
  (11); `dueMs_ += kProbeIntervalMs` (the catch-up burst) -> exit 1 (3). Restored, exit 0.

### Size, build and tests
- Build with the REBUILD.md command: REAL exit 0 (clean build, 191.6 s).
  RAM **160,912 B** (49.1% of 327,680), was 160,896 B, **+16 B** (the `StartWindow`'s bool plus its
  three `uint32_t`s and the one new notice bool fit the padding the old scalars already had).
  Flash **3,002,189 B** (45.8% of 6,553,600), was 3,001,973 B, **+216 B**.
  `firmware.bin` 3,002,560 B, was 3,002,336 B (+224 B).
- `sh test/host/run.sh`: REAL exit 0, 0 `FAIL` lines (the full suite).
- A second build after this entry was written (incremental, 22.5 s): REAL exit 0 and the same RAM
  160,912 B / Flash 3,002,189 B, so the numbers above are the final tree's.

### Could not check (no device in this run)
- Nothing about the glass or the keyboard: no L2 was flashed and no CardKB was probed here. Not tested
  on hardware: that the real start-up time after a cold rail is inside 3000 ms (the 08:52 proof only
  says "longer than 50 ms", and the after-N log will tell us the real number), that 30 probes at
  100 ms do not disturb the keyboard's own boot, that a key pressed during the window is not lost in
  a way the user notices (nothing is read until the answer, by design), and that the timeout toast
  reads right on the glass. The rail drop on timeout is host-tested only as a source ordering check
  (state machine says TimedOut, `cardkb.cpp` drops the rail before logging); no meter was attached.
- The 5 V/safety note of EST-412 still stands unchanged (about 3.3 V expected on SDA/SCL, above
  3.6 V stop).
- The heap cost of a window's 30 `Wire1.begin()`/`end()` pairs (Arduino allocates 2 x 128 B per
  begin and frees them in end, the same size every time) was not measured: the boot line prints the
  static RAM only, and no device was attached. Nothing else allocates on this path.

### Next step
Claude: review and commit. Filip: flash with the keyboard switch ON and the keyboard plugged in; the
boot should NOT pause and `[kb] cardkb found (sda=13 scl=14) after N ms` should appear within a second
or so, N telling us the real start-up time. Then: boot with the keyboard unplugged (after 3000 ms one
`[kb] cardkb not found after 3000 ms, Grove power off` and the toast, rail off), switch it OFF during
a window (no toast, rail off), and switch it on again with the keyboard plugged in.

## EST-422 flick is not a tap + confirm important settings

S495-phase run (DeepSeek, 2026-11-13). Two changes from Filip's use: a fast flick was taken as a tap
(it changed the setting under the finger), and the settings that matter had no confirmation.

### 1. A flick is not a tap (one rule, the whole app)

`src/ui/tap_rule.h` is new and is the ONLY place that decides whether a release is a tap. Its
`TapTrace` keeps the gesture's press point, the last point the panel reported and how many down
samples the loop saw; `tapAllowed(releaseX, releaseY, drag, acted)` says:

- not a tap if the loop's own drag latch fired, or a hold already acted (the old guard, unchanged);
- not a tap if the finger was seen only ONCE. That is the reported flick: it began and ended between
  two samples, so a tap cannot be proven, and tapping the press point is exactly the bug;
- not a tap if the last reported point (or the release edge) is more than 12 px from the press point,
  per axis. This also catches a sideways flick, which the old drag latch never saw (it only looked
  at Y).

The loop (`src/main.cpp`) feeds it: `g_tap.press()` on the press edge, `g_tap.sample()` on every pass
the finger is down, and the release edge asks `tapAllowed`. `x`/`y` now start from
`g_tap.lastX/lastY` each pass, because LovyanGFX's `getTouch` returns before writing x/y when the
panel reports no finger (checked in LGFXBase.hpp), so the release pass carries the last real point
instead of 0,0.

**Why the flick is really caught:** the blind window was the fix's other half. With a 20 ms loop plus
an ~11 ms frame push, a hard flick could travel 40+ px between two samples, so no sample ever crossed
the 12 px drag test. While a finger is DOWN the loop now samples every ~2 ms (`idleDelay`), so a
flick's first 12 px of travel are seen (a hard flick is about 2 px/ms) and the drag latch fires
before it lifts; and a real tap of any human length is seen many times, which the one-sample rule
needs. Everything else is untouched: a still finger of any duration is a tap, the long-press rules
(EST-342/346) fire while the finger is down, and the map pan continuation (EST-416) still spends the
gesture at the press. An ignored release logs one `[tap] x,y ignored: N sample(s), last x,y (flick,
not a tap)` line.

### 2. One confirm sheet for the settings that matter

`src/ui/settings_actions.h` is new: the shell's `SetAct` ids (moved out of main.cpp), the ONE table
`confirmKindForAction()` (presets and the custom preset, TX power, path hash, both channel deletes,
Wi-Fi Forget, repeater Forget, the Wi-Fi / Bluetooth / keyboard switches, Clear messages) and the
sheet's state machine `ConfirmAsk` + `confirmTap()`. Two changes name their kind at the commit point
instead of through a row id: the node name (the editor's OK) and the Position mode (the GPS choice and
the map picker's Save).

The sheet itself is the EST-382 pop-up (`settingsModalLayoutFor`, Cancel on the left as the default
side, a tap outside is Cancel and consumed), now drawn by `drawAsk` with the right key labelled Apply,
Delete or Forget and red for the destructive ones. The shell's one apply is
`settingsApplyConfirmed()`; Cancel and a tap outside never reach it. The two old confirmation pages
(`ConfirmPreset`, `ConfirmHash`) and EST-399's per-row "tap again" arms are gone, so there is ONE
confirmation pattern in the app: `ui/delete_arm.h` and its C++ test stay, but the shell has no caller
for the struct any more (Claude: say if it should be deleted outright).

Small switches still change at the touch: sounds, volume, brightness, auto-off, touch/DND/welcome
sounds, share position, RPL auto-login, channel enabled/sound, the auto-off time rows.

### Tests

- `test/host/test_est422.cpp` (new, in run.sh): the tap rule against the old one. A still press taps;
  press + release 40 px away with NO move sample between does NOT (the old rule taps, armed); 11 px
  taps and 13 px does not; a gesture seen once does not (old rule taps); a sideways flick does not
  (old rule taps); a drag and a fired hold never do. Then the confirm list (every listed setting asks,
  every small switch does not), the flow (the question quotes the value, Cancel and Outside change
  nothing, Apply runs once with kind/arg/value) and the keys (the modal layout's own rectangles are
  the hit rects, the two keys are disjoint, the box fits 320x240).
- The source checks of the earlier tickets were updated for the one sheet, not weakened: test_est381
  (the release tap must be decided by `tapAllowed` with g_drag and g_acted), test_est392 (the id is
  declared in the header now; the choice asks), test_est399 (no arms in the shell, every destructive
  call inside the one apply), test_est412/est421 (the keyboard switch asks, the ON/OFF work is in the
  apply), test_est417 (the pop-up list names `g_ask.open`).

### Size, build and tests
- Build with the REBUILD.md command: REAL exit 0 (full build 245.3 s; a second, incremental build of
  the final tree 14.8 s, REAL exit 0, same numbers).
  RAM **160,952 B** (49.1% of 327,680), was 160,912 B, **+40 B** (the gesture record: three `int`s
  and the sample count, plus the ask sheet in place of two pointers and a bool).
  Flash **3,002,869 B** (45.8% of 6,553,600), was 3,002,189 B, **+680 B** (the confirm table and its
  question strings, the tap rule, and the one apply that absorbed the row code).
  `firmware.bin` 3,003,232 B, was 3,002,560 B (+672 B).
- `sh test/host/run.sh`: REAL exit 0, 0 FAIL lines (the whole suite, test_est422 included).

### Could not check (no device in this run)
- Nothing about the glass: no L2 was flashed and no touch was performed here. Not tested on
  hardware: that a real tap still registers (the one-sample rule plus the ~2 ms sampling says it
  will, and USB taps hold 90 ms so `tools/tap.sh` is unaffected), that a fast flick now scrolls
  instead of tapping (it cannot be shown without a finger), that ~2 ms sampling during a touch does
  not disturb the mesh/GPS pass timing in a way the glass notices, and the confirm sheet's look,
  wrapping and key sizes on the panel.
- A flick that is seen exactly once is refused rather than guessed; if Filip finds a flick that now
  does nothing at all, the next step is the GT911's own last-point register, not a looser rule.

### Next step
Claude: review the diff (main.cpp, the three new ui headers, screen_settings' renamed sheet, the six
updated source checks), flash, and have Filip test: flick through the Settings list (nothing may
change), tap a row (must still work), then try each confirming setting's Cancel and Apply.
