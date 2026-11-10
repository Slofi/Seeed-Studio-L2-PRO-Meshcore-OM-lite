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
