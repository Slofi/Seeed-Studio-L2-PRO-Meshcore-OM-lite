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
