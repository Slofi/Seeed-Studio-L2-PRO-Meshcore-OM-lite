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
