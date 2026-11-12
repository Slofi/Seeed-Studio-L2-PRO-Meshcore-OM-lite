#!/bin/sh
# Host tests for the om_lite_l2 tree. Non-zero exit on any failure; no device and no extra
# packages needed (system g++ and python3 only).
set -eu
cd "$(dirname "$0")"
# -B: no __pycache__ left behind in the tree the reviewer diffs.
python3 -B test_shot.py
# EST-343: tools/cmd.py quotes argv for the device's tokenizer, frames OMCMD1 and stops on the one
# answer line (data lines are printed first).
python3 -B test_cmd.py
# The vendored MeshCore folders must match lib/MESHCORE_VERSION (a hand edit fails this).
python3 -B test_meshcore_manifest.py
# EST-341: one MAX_CONTACTS for every TU (a second layout made the global MyMesh too small).
python3 -B test_build_flags.py
# EST-346: the four generated VLW faces really carry ASCII + U+00A0..U+017F (every letter the ticket
# names has a bitmap), so the fonts and the range constant cannot drift.
python3 -B test_fonts.py
# EST-379: the build id's one command, the dirty suffix and the "unknown" fallback with a warning,
# against a fake git (success, dirty, failure, git missing).
python3 -B test_est379.py
# EST-384: the new position code's log and answer format strings carry no coordinate (rule 9).
python3 -B test_est384.py
# EST-385: no NTP log or answer format prints a LAN address, an SSID or a password (rule 9).
python3 -B test_est385.py
# EST-387: no GPS log or aid format prints a coordinate, and the boot/aid lines are the ones the
# ticket names (rule 9).
python3 -B test_est387.py

# Layout / hit testing, the migration bytes and the other pure-C++ logic (no Arduino): build them
# with the system g++.
BIN="$(mktemp /tmp/om-lite-layout.XXXXXX)"
BIN2="$(mktemp /tmp/om-lite-migration.XXXXXX)"
BIN3="$(mktemp /tmp/om-lite-chat.XXXXXX)"
BIN4="$(mktemp /tmp/om-lite-time.XXXXXX)"
BIN5="$(mktemp /tmp/om-lite-gate.XXXXXX)"
BIN6="$(mktemp /tmp/om-lite-age.XXXXXX)"
BIN7="$(mktemp /tmp/om-lite-gps.XXXXXX)"
BIN8="$(mktemp /tmp/om-lite-bar.XXXXXX)"
BIN9="$(mktemp /tmp/om-lite-gpsmode.XXXXXX)"
BIN10="$(mktemp /tmp/om-lite-fault.XXXXXX)"
BIN11="$(mktemp /tmp/om-lite-fade.XXXXXX)"
BIN12="$(mktemp /tmp/om-lite-crash.XXXXXX)"
BIN13="$(mktemp /tmp/om-lite-dm.XXXXXX)"
BIN14="$(mktemp /tmp/om-lite-usbcmd.XXXXXX)"
BIN15="$(mktemp /tmp/om-lite-est344.XXXXXX)"
BIN16="$(mktemp /tmp/om-lite-emoji.XXXXXX)"
BIN17="$(mktemp /tmp/om-lite-est346.XXXXXX)"
BIN18="$(mktemp /tmp/om-lite-settings.XXXXXX)"
BIN19="$(mktemp /tmp/om-lite-history.XXXXXX)"
BIN20="$(mktemp /tmp/om-lite-est351.XXXXXX)"
BIN21="$(mktemp /tmp/om-lite-wifi.XXXXXX)"
BIN22="$(mktemp /tmp/om-lite-est355.XXXXXX)"
BIN23="$(mktemp /tmp/om-lite-est356.XXXXXX)"
BIN24="$(mktemp /tmp/om-lite-est358.XXXXXX)"
BIN25="$(mktemp /tmp/om-lite-est359.XXXXXX)"
BIN26="$(mktemp /tmp/om-lite-est360.XXXXXX)"
BIN27="$(mktemp /tmp/om-lite-est362.XXXXXX)"
BIN28="$(mktemp /tmp/om-lite-est363.XXXXXX)"
BIN29="$(mktemp /tmp/om-lite-est364.XXXXXX)"
BIN30="$(mktemp /tmp/om-lite-est365.XXXXXX)"
BIN31="$(mktemp /tmp/om-lite-est366.XXXXXX)"
BIN32="$(mktemp /tmp/om-lite-est367.XXXXXX)"
BIN33="$(mktemp /tmp/om-lite-est368.XXXXXX)"
BIN34="$(mktemp /tmp/om-lite-est370.XXXXXX)"
BIN35="$(mktemp /tmp/om-lite-est373.XXXXXX)"
BIN36="$(mktemp /tmp/om-lite-est374.XXXXXX)"
BIN37="$(mktemp /tmp/om-lite-est376.XXXXXX)"
BIN38="$(mktemp /tmp/om-lite-est377.XXXXXX)"
BIN39="$(mktemp /tmp/om-lite-est380.XXXXXX)"
BIN40="$(mktemp /tmp/om-lite-est381.XXXXXX)"
BIN41="$(mktemp /tmp/om-lite-est382.XXXXXX)"
BIN42="$(mktemp /tmp/om-lite-gpsraw.XXXXXX)"
BIN43="$(mktemp /tmp/om-lite-est384.XXXXXX)"
BIN44="$(mktemp /tmp/om-lite-ntp.XXXXXX)"
BIN45="$(mktemp /tmp/om-lite-est386.XXXXXX)"
BIN46="$(mktemp /tmp/om-lite-est387.XXXXXX)"
BIN47="$(mktemp /tmp/om-lite-est388.XXXXXX)"
BIN48="$(mktemp /tmp/om-lite-est390.XXXXXX)"
BIN49="$(mktemp /tmp/om-lite-est391.XXXXXX)"
BIN50="$(mktemp /tmp/om-lite-est392.XXXXXX)"
BIN51="$(mktemp /tmp/om-lite-est393.XXXXXX)"
BIN52="$(mktemp /tmp/om-lite-est394.XXXXXX)"
BIN53="$(mktemp /tmp/om-lite-est397.XXXXXX)"
BIN54="$(mktemp /tmp/om-lite-est399.XXXXXX)"
BIN55="$(mktemp /tmp/om-lite-est400.XXXXXX)"
BIN56="$(mktemp /tmp/om-lite-est401.XXXXXX)"
BIN57="$(mktemp /tmp/om-lite-est402.XXXXXX)"
BIN58="$(mktemp /tmp/om-lite-est403.XXXXXX)"
BIN59="$(mktemp /tmp/om-lite-est404.XXXXXX)"
BIN60="$(mktemp /tmp/om-lite-est405.XXXXXX)"
BIN61="$(mktemp /tmp/om-lite-est406.XXXXXX)"
BIN62="$(mktemp /tmp/om-lite-est409.XXXXXX)"
BIN63="$(mktemp /tmp/om-lite-est412.XXXXXX)"
BIN64="$(mktemp /tmp/om-lite-est413.XXXXXX)"
BIN65="$(mktemp /tmp/om-lite-est414.XXXXXX)"
trap 'rm -f "$BIN" "$BIN2" "$BIN3" "$BIN4" "$BIN5" "$BIN6" "$BIN7" "$BIN8" "$BIN9" "$BIN10" "$BIN11" "$BIN12" "$BIN13" "$BIN14" "$BIN15" "$BIN16" "$BIN17" "$BIN18" "$BIN19" "$BIN20" "$BIN21" "$BIN22" "$BIN23" "$BIN24" "$BIN25" "$BIN26" "$BIN27" "$BIN28" "$BIN29" "$BIN30" "$BIN31" "$BIN32" "$BIN33" "$BIN34" "$BIN35" "$BIN36" "$BIN37" "$BIN38" "$BIN39" "$BIN40" "$BIN41" "$BIN42" "$BIN43" "$BIN44" "$BIN45" "$BIN46" "$BIN47" "$BIN48" "$BIN49" "$BIN50" "$BIN51" "$BIN52" "$BIN53" "$BIN54" "$BIN55" "$BIN56" "$BIN57" "$BIN58" "$BIN59" "$BIN60" "$BIN61" "$BIN62" "$BIN63" "$BIN64" "$BIN65"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_layout.cpp -o "$BIN"
"$BIN"
# Migration (EST-340): identity bytes, the 16-byte -> 32-byte channel secret mapping, the
# path-hash mode and the old EST-329 /contacts file decode.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_mesh_migration.cpp -o "$BIN2"
"$BIN2"
# Chat: the word wrapper and its UTF-8 fallback, the header and the chat selector arithmetic, the
# message band, the keyboard sheet, the message sheet (rows per message kind, hit rects, path
# naming) and the held message sequence numbers (a full ring plus one new message must not move a
# reply target).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_chat.cpp \
  ../../src/ui/chat_layout.cpp ../../src/ui/text_wrap.cpp ../../src/ui/message_sheet.cpp -o "$BIN3"
# Time: NMEA RMC/ZDA (including the ZDA build-year gate), the source ranking, the floor and the
# Ljubljana DST clock.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_timekeeper.cpp \
  ../../src/timekeeper.cpp -o "$BIN4"
# Wake touch gate: a finger held through the wake is ignored, a new press after 300 ms counts. Also
# the tap-away guard (EST-339): a press outside an overlay spends its drag and its release.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_touch_gate.cpp \
  ../../src/touch_gate.cpp -o "$BIN5"
# The one timestamp formatter (today / this week / older, the fixed 2025-01-01 cut-off, the age
# fallback when the clock was not real) and the GPS cell's one number. It links the real timekeeper
# for the Ljubljana local fields.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_age.cpp \
  ../../src/ui/age.cpp ../../src/timekeeper.cpp -o "$BIN6"
# GSV: expected vs heard (an empty C/N0 field is not a reception), per constellation, the new-cycle
# reset, the 60 s best-C/N0 window and the most heard at once since boot.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_gps.cpp -o "$BIN7"
# The new top bar (EST-337): time left, gear/battery/GPS/optional icons right-aligned with one gap,
# the measured worst-case fit, the fault mark's rect, and the Nodes header text.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_status_bar.cpp -o "$BIN8"
# The L76K mode command (EST-339): ONE $PCAS04,7, only after the module talks, never a second time.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_gps_mode.cpp -o "$BIN9"
# The radio-fault mark's state: each reason, a good TX clears, a radio re-init clears, silence never.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_radio_fault.cpp -o "$BIN10"
# The backlight ramp: gamma 2.2 both ways (monotonic, exact ends, slow start), 300 ms in, 150 ms out,
# many distinct 8-bit levels, a wake reverses a fade-out.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_fade.cpp \
  ../../src/fade.cpp -o "$BIN11"
# The boot crash reporter (EST-341): reset reasons that count as a crash, the RTC_NOINIT checkpoint
# (magic, truncation, seq), and the two one-line formats (task/PC/cause/backtrace, none/corrupt).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_crash_report.cpp \
  ../../src/crash_report_logic.cpp -o "$BIN12"
# EST-342: the DM frame budget, the delivery state from the ACK and the companion's timeout, the
# Nodes long-press rule, the node sheet's rows per node type, the new-node notice, the message
# sheet's DM row, the Info page's lines and the Nodes drag sign.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_dm.cpp \
  ../../src/ui/chat_layout.cpp ../../src/ui/text_wrap.cpp ../../src/ui/node_menu.cpp \
  ../../src/ui/message_sheet.cpp -o "$BIN13"
# EST-343: the OMCMD1 frame reader (magic in one byte at a time, split frames, junk before the magic,
# wrong length, header and body timeouts) and the command language (quoted names such as
# "Don't Panic", arguments, and the refusal of everything not on the list).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_usb_cmd.cpp \
  ../../src/usb_cmd_logic.cpp ../../src/sound_synth.cpp -o "$BIN14"
# EST-344: the DM ring's one allocation path (send, reply, two more, wrap), the channel ring's shared
# sequence rule, the toast overlay state and MeshCore's encoded out_path_len (1B/2B/3B/unknown).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est344.cpp -o "$BIN15"
# EST-345: the generated emoji table (sorted, every cell decodes), the UTF-8 sequencer (selector,
# skin tone, ZWJ, flag), emoji width measuring and the Recent row.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_emoji.cpp \
  ../../src/ui/text_wrap.cpp ../../src/ui/emoji_picker.cpp -o "$BIN16"
# EST-346: a touch during a toast dismisses it and fires nothing (the next press acts, an expired
# toast does not eat the touch), the keyboard always opens on letters with shift off, the s/c/z long
# press (plain letter on a tap, the accented letter on a release after the hold) and the composer's
# 2-bytes-per-letter budget at the limit.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est346.cpp \
  ../../src/ui/chat_layout.cpp ../../src/ui/text_wrap.cpp -o "$BIN17"
# EST-347: the settings tab row (five 64 px taps, labels measured with the mono advance, the tab
# chosen per screen), the row rectangles and scroll clamp, the stepper keys, the manual-time stepper
# (month lengths, leap years, wraps) and the radio prefs store (seed, ranges, round trip).
# EST-350: the page history Back walks (General > Node > Name > Back > Back at the same scroll) and
# the chat selector inside Settings, which is the Chat tab's own list arithmetic (chat_layout.cpp).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_settings.cpp \
  ../../src/ui/chat_layout.cpp -o "$BIN18"
# EST-348: the on-card chat history over an in-memory volume: record encode/decode and CRC, a torn
# last record (detected and repaired), update records applied on load, backwards paging, rotation to
# .old, unread/seen across a reload, the SPIFFS fallback and the chat ids.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_history.cpp -o "$BIN19"
# EST-351 / EST-352: the map chrome's scale arithmetic and the two end-anchored labels; the RPTR
# list and page and the row's labelled second line; the native RGB565 byte-order round trip; the
# shared keyboard sheet's per-field filters, limits, key tables and rectangles (chat_layout.cpp).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est351.cpp \
  ../../src/ui/map_chrome.cpp ../../src/ui/rptr_layout.cpp ../../src/ui/chat_layout.cpp -o "$BIN20"
# EST-354: the saved Wi-Fi network list (add, replace, forget, five at most), the scan list, the
# asynchronous state machine (off / scanning / connecting / connected / failed, both timeouts and
# millis() wrap) and the `wifi` USB command (no word reads or carries a password).
# EST-355 adds the scan's order: a scan asked for right after the switch goes on waits for the driver
# and a refusal is retried, so the switch-on path cannot show "Failed: scan failed".
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_wifi.cpp \
  ../../src/usb_cmd_logic.cpp ../../src/sound_synth.cpp ../../src/ui/chat_layout.cpp -o "$BIN21"
# EST-355: the screen-change backlight transition (fade out -> one push -> settle -> fade in), the
# coalescing of a second change during the dip, the touches consumed by it, and the dip rule itself
# (the map on either side, or a brightness difference above the threshold).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est355.cpp \
  ../../src/screen_change.cpp ../../src/fade.cpp ../../src/touch_gate.cpp -o "$BIN22"
# EST-356: the Bluetooth switch and its three states, the PIN generator (six digits, never 123456),
# the change watch that separates the phone's writes from ours, and the `ble` USB command.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est356.cpp \
  ../../src/usb_cmd_logic.cpp ../../src/sound_synth.cpp -o "$BIN23"
"$BIN3"
"$BIN4"
"$BIN5"
"$BIN6"
"$BIN7"
"$BIN8"
"$BIN9"
"$BIN10"
"$BIN11"
"$BIN12"
"$BIN13"
"$BIN14"
"$BIN15"
"$BIN16"
"$BIN17"
"$BIN18"
"$BIN19"
"$BIN20"
"$BIN21"
"$BIN22"
"$BIN23"

# EST-358: the RPTR tab's logic. The exact CLI line every admin control sends (the mock's asserted
# strings), the discovery list, the login state machine and its timeouts, the status decode, the
# neighbour/region parsers, the two-step confirm, the admin row table, the admin layout's hit test
# and the `rptr` USB command.
# EST-364: the model now formats every clock in LOCAL time, i.e. through om::localCivil() in
# timekeeper.cpp, so the tests that link it link the real timekeeper too.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est358.cpp \
  ../../src/rptr_model.cpp ../../src/ui/rptr_layout.cpp ../../src/usb_cmd_logic.cpp \
  ../../src/sound_synth.cpp ../../src/timekeeper.cpp -o "$BIN24"
"$BIN24"

# EST-369: the shared sound library, organised by shape. The synth is the same sound_synth.cpp the
# firmware and tools/mksounds.cpp build: 41 current sounds (ten Solo, ten Duo, ten Trio, ten Multi and
# the one long Alert), the voice starts every name, unique names and note sequences, the shape's note
# count and length range, the kept TK-TK / soft voices / duos / Glass Trio / battery warning, retired
# EST-361 ids resolving to a kept sound with the same waveform, edges at zero, no clipping, the
# EST-359 choice migration, and the `play <event> [sound-id]` USB command.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est369.cpp \
  ../../src/sound_synth.cpp ../../src/usb_cmd_logic.cpp -o "$BIN25"
"$BIN25"

# EST-360: the region/clock rework. The region-name rule and its filter, every region CLI line
# (put with/without parent, remove, home/default including the * / <null> choices, time), the
# anonymous nearby-regions reply (clock + '*', list, a real empty answer, garbage, short), the time
# guard (our clock unset refused, the one-day ceiling, the repeater ahead), the clock reply parser,
# battery volts, and the keyboard's RptrRegion field (it calls the same rule as the model).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est360.cpp \
  ../../src/rptr_model.cpp ../../src/ui/chat_layout.cpp ../../src/timekeeper.cpp -o "$BIN26"
"$BIN26"

# EST-362: the RPTR section's redesigned interaction. The page model before and after login, one
# Apply per setting page (regions append `region save` in the same step), the clock guard (a jump over
# a day confirms with both dates, a repeater ahead is the only refusal), which actions confirm, the
# console (log lines are not rows and nothing in them is tappable), the read-back parsers, the list
# order and the one pop-up's Cancel/Confirm/outside.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est362.cpp \
  ../../src/rptr_model.cpp ../../src/rptr_page.cpp ../../src/ui/rptr_layout.cpp \
  ../../src/timekeeper.cpp -o "$BIN27"

# EST-363: the RPTR page model at its largest inputs (every page, the longest names, kMaxRegions,
# kMaxNeighbours, a full console log) with every text buffer checked to be NUL-terminated inside its
# own array and the whole ScreenOut built between guard words; the static_assert size guard for the
# loop stack; and the three layout defects from the 20:08 screenshot (title vs subtitle band, the
# hint that was cut, and the Signal row's value under the Log in button).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est363.cpp \
  ../../src/rptr_model.cpp ../../src/rptr_page.cpp ../../src/ui/rptr_layout.cpp \
  ../../src/timekeeper.cpp -o "$BIN28"
"$BIN27"
"$BIN28"

# EST-364: the 20:34 Argus run. The confirmed clock set becomes the page's clock (its own stamp or
# the epoch we sent) in LOCAL time with no banner; a status without an answer says "Status  no answer
# yet" in dim ink (never "Refused") with the 8 s floor and its one retry; the position is a PAIR
# (the "14.501452, 0.00000" longitude-in-latitude run is reproduced and must stay hidden), the
# advert's own lat-then-lon byte order is pinned down, the refresh icon has its own gutter and a
# 40x40 target, no RPTR screen says "UTC", and the badge counts to 99 ("99+" above).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est364.cpp \
  ../../src/rptr_model.cpp ../../src/rptr_page.cpp ../../src/ui/rptr_layout.cpp \
  ../../src/timekeeper.cpp -o "$BIN29"
"$BIN29"

# EST-365: the tab bar badge has one source (a source-level call-site check, plus the C++ model in
# test_est365); the `msgs` order across a reboot; the noise window, the last-20 reception ring and the
# loop histogram with per-part attribution; the anonymous owner/name answer for an unnamed repeater;
# and the `radio` / `rptr login` command surface, including that no log form carries the password.
python3 -B test_tab_bar_calls.py
# EST-374: every page switch goes through showTab -> map_tab::leave(), so leaving the Map always
# draws the bottom bar on the next screen; no other screen reads the saved tabsHidden key.
python3 -B test_est374.py
# EST-381: every map draw ends with the one overlay frame (steady, animation and slide), board.cpp
# draws the toast before the push, the toast's press edge comes first and spends the gesture (no
# marker, chip, pan or callout behind it), the notice door has no Map exception, and `notify test`
# enters the same notifyNotice() with kind Channel.
python3 -B test_est381.py
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est365.cpp \
  ../../src/radio_stats.cpp ../../src/rptr_model.cpp ../../src/usb_cmd_logic.cpp \
  ../../src/sound_synth.cpp ../../src/timekeeper.cpp -o "$BIN30"
"$BIN30"

# EST-366: the 21:15 review. CR in the radio read-back and the Radio page; the scrolling card (the
# card is content, only the header is fixed); a complete region list parse (multi-line, CRLF, no
# stale tail, the wildcard-only answer); Apply only while draft != read-back with a dot per changed
# row; neighbours "-none-" vs an error; the map picker's layout/hit test; PageNav one level for every
# sub-page; the home/default pickers; and the notification rules (DND, open chat, newest-wins queue,
# text forms).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est366.cpp \
  ../../src/rptr_model.cpp ../../src/rptr_page.cpp ../../src/ui/rptr_layout.cpp \
  ../../src/timekeeper.cpp -o "$BIN31"
"$BIN31"

# EST-367: a chat's own view instead of the shared 32-message arrival ring (a busy channel evicting a
# quiet one must not empty it), the selector preview from the chat's own log, the measured open of a
# full 2 MB log (windows/bytes counted, never a scan), unread vs shown, the `chats` MISMATCH rule,
# the status card's symmetric padding and the neighbour discovery (discover, 60 s window, neighbors,
# with the unknown-command fallback).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est367.cpp \
  ../../src/rptr_model.cpp ../../src/ui/rptr_layout.cpp ../../src/usb_cmd_logic.cpp \
  ../../src/sound_synth.cpp ../../src/timekeeper.cpp -o "$BIN32"
"$BIN32"

# EST-368: the Map tab's Traces. The hop mapping from path hashes (1B/2B/3B, unknown hops, a node
# without a position, the sender and us endpoints), the route builder, the screen geometry, the
# animation model (420 ms per hop, 35 % tail, 900 ms fade, the direct ring) and the one-at-a-time
# queue; the message pop-up's wrap (UTF-8, emoji, at most four rows, an ellipsis only when cut) and
# its height limit above the tab bar; the RPTR read-back that never overwrites a dirty draft; and the
# Position page's "Share position" (gps advert none / lat,lon 0, then one advert).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est368.cpp \
  ../../src/ui/map_trace.cpp ../../src/ui/notice_panel.cpp ../../src/ui/text_wrap.cpp \
  ../../src/rptr_model.cpp ../../src/rptr_page.cpp ../../src/timekeeper.cpp -o "$BIN33"
"$BIN33"

# EST-370 + EST-372 + EST-373: the Map's Names tab, the centre cross, the trace palette and the saved
# switches. The tab is the top of the right column (EST-373: 25x32, right edge 320, tap, no overlap
# with the z13 line or the credit box, a marker still wins), the cross is 7x7 at (160,107), the same
# point map.cpp projects the view centre onto and the Position picker's mark uses, the trace
# colour and widths are fixed in theme.h with a measured contrast floor, and the names/traces store
# proves its defaults, one-key writes, reloads, a failed write and a missing key.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est370.cpp \
  ../../src/ui/map_chrome.cpp -o "$BIN34"
"$BIN34"

# EST-373: the three map tweaks. The tabs are 25 px (widest icon + 4 px each side) and every tab's tap
# rectangle IS the rectangle the screen draws (one list, mapTabRects: all four corners, x + w == 320);
# the hide/show arrow's 28 x 22 target sits above the top tab, hides the column down to 4 px slivers,
# never moves or hides the zN label, and is saved in the layer store; the centre cross is 7 px in one
# dark ink with measured contrast floors on the real z13 tile colours.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est373.cpp \
  ../../src/ui/map_chrome.cpp -o "$BIN35"
"$BIN35"

# EST-374: the rest of the Map's hide idea. The canvas is the full map area (320 x 214 at y 26) in
# both states and the centre (160,133) never moves; the 22 px chip with its 2 px chevron and 30 x 26
# target; the tucked bottom bar (a 3 px strip with no tap zone, every other screen keeps its bar);
# the unread dot; the grey ghost + / - on the shown zoom rectangles (and 2 px left of one is a map
# tap); EST-375's 6 px rounded slivers at 314..320 (the drawn box extends to 324, the ghost centres
# stay 7 px left); and the 4-frame slide (19 px tabs, 22 px bar) and its 31 ms per-frame estimate.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est374.cpp \
  ../../src/ui/map_chrome.cpp ../../src/ui/rptr_layout.cpp -o "$BIN36"
"$BIN36"

# EST-376: the diagnostics ticket. `radio tcxo 1.8|3.0`, `radio boost on|off`, `radio sample <10..600>`,
# `radio status` and `pmu probe` parse and refuse bad values; the sampler's summary arithmetic
# (min/median/max/mean, sorted in place, negative truncation, the empty list, a full 120-line run);
# the FUSB302 decode from fixed register bytes, with 0x3E/0x3F/0x42/0x43 provably never read; and
# the payload offset the sampler's `hash=` uses.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est376.cpp \
  ../../src/diag_logic.cpp ../../src/radio_stats.cpp ../../src/usb_cmd_logic.cpp \
  ../../src/sound_synth.cpp -o "$BIN37"
"$BIN37"

# EST-377: the battery log. The exact CSV line and its columns (12, then EST-401's four), the header
# only on a new file, the local /log/power-YYYYMMDD.csv name, the once-per-minute gate (first line at
# once, the millis wrap), the saved switch (missing key = ON, a set writes it, a failed write still
# flips RAM), the `log power on|off|tail` command surface, and the boost choice that survives a later
# resetAGC.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est377.cpp \
  ../../src/power_log_logic.cpp ../../src/diag_logic.cpp ../../src/usb_cmd_logic.cpp \
  ../../src/sound_synth.cpp ../../src/timekeeper.cpp -o "$BIN38"
"$BIN38"

# EST-380: the SD open-file budget (the named constants sum to the mount limit, with the two slots of
# margin) and the history retry: a refused open or a short write keeps the record, hist_write_fail is
# counted once per record, [hist] write FAILED is throttled to one a minute, the recovery drains the
# backlog and logs one `write recovered, N`, the queue overflow is the only loss, and the pop-up
# notice is taken once.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est380.cpp -o "$BIN39"
"$BIN39"

# EST-381: the Map's message pop-up. The one decision (empty / screen-off / DND and its two
# exemptions / the open-chat rule / "a toast is up -> queue") and its one log line, the burst queue
# keeping the newest message's body AND kind, the toast's 5 s deadline and consumed-tap dismissal
# with the paint counter the device line reads, the pop-up's box inside the map canvas above the bar
# in BOTH bar states, the consumed tap on the map's own hit areas (marker, chip, zoom tab, plain map
# with a callout open) with the no-toast control beside it, and the `notify test` command line.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est381.cpp \
  ../../src/ui/map_chrome.cpp ../../src/ui/notice_panel.cpp ../../src/ui/text_wrap.cpp \
  ../../src/usb_cmd_logic.cpp ../../src/sound_synth.cpp -o "$BIN40"
"$BIN40"

# EST-382: the chat list's dots mean unread (selected+0 -> highlight only, any unread -> dot AND the
# EST-364 number, a DM row the same), one row layout for drawing and hit tests, the ADVERT key's
# 62 x 26 rect and its 36 px Flood / Local rows with their hit test, the `advert local|flood` parser,
# and Clear messages one channel at a time over the real HistoryStore (exactly that chat's queue
# records and files go; another chat's records, file, unread and preview stay; a refused delete
# changes nothing).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est382.cpp \
  ../../src/ui/chat_layout.cpp ../../src/usb_cmd_logic.cpp ../../src/sound_synth.cpp -o "$BIN41"
"$BIN41"

# EST-383: the `gps raw` diagnostic and the no-fix clock. The four sentence types print only their
# safe fields (a latitude/longitude never appears, even one shaped like a time); the window's 5..120 s
# bounds, type tally and zda flag; the advance-by-1s gate; the 2080 default-year rejection for RMC V
# and ZDA; and the precedence gps-nofix < manual < phone < Wi-Fi < GPS fix, with each better source
# replacing an approximate clock at once and the approximate one never overwriting any of them.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_gps_raw.cpp \
  ../../src/gps_raw.cpp ../../src/timekeeper.cpp -o "$BIN42"
"$BIN42"

# EST-384: the one own-position source. Fixed beats a fresh GPS fix and a later fix never moves the
# node; GPS is used when GPS is selected and none when there is no fix; the phone's set-location
# becomes Fixed; the NVS record round-trips and an invalid Fixed record falls back to GPS; the status
# token is gps/fixed/none and carries no coordinate. The log-format grep is test_est384.py above.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est384.cpp -o "$BIN43"
"$BIN43"

# EST-385: the Wi-Fi NTP retry schedule (0 s / 30 s / 2 min / every 10 min), the four-server rotation,
# the stop rule against the real timeSourcePriority() (a GPS fix stops it, Wi-Fi/phone/manual do not),
# one attempt at a time, the forced attempt (USB `ntp now`), the 25 s window and millis() wrap.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_ntp.cpp ../../src/timekeeper.cpp -o "$BIN44"
"$BIN44"

# EST-386: the attempt's modem-sleep decision. Bluetooth off -> off for the attempt, restored once at
# its end; Bluetooth on -> the radio is never touched; Bluetooth starting mid-attempt -> sleep back on
# at once, once, and a later BLE stop does not turn it off again; a refused change -> neutral, so the
# end never turns sleep on behind the caller's back; and the four actions composed with a fake radio.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est386.cpp -o "$BIN45"
"$BIN45"

# EST-387: the GNSS warm-boot decision (power-on / brown-out / deep-sleep / unknown reset the receiver,
# software / OTA / panic / watchdog / external leave it running), the AID-INI frame against a
# hand-computed example (sync, length, class/id, the 56-byte payload layout and the 32-bit sum), the
# ACK/NAK reader out of the NMEA stream (bad checksum, silly length, split frames), the once-per-boot
# aid gate and the UTC civil time the payload carries.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est387.cpp \
  ../../src/timekeeper.cpp -o "$BIN46"
"$BIN46"

# EST-388: the HTTPS probe's pure parts. The one target (game.cl0.eu, /api/v1/health, 443, 10 s), the
# three refusal words (wifi off / wifi not connected / https busy), the HTTP status line as it arrives
# in pieces, the header terminator (CRLF and bare LF), the mbedTLS out-of-memory codes, and the one
# report line the five device runs are compared by (every field, a negative hs_ms, truncation safe,
# the ca= word last). The python part keeps the probe on the validating path (no setInsecure, no
# bundle switch).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est388.cpp -o "$BIN47"
"$BIN47"
python3 -B test_est388.py
# EST-389: the probe's trust anchors. Exactly TWO roots in ONE CA buffer, each PEM decoded by the
# test: its SHA-256 must match the fingerprint in the file's comment and the published value (the
# game server's chain now ends at ISRG Root X2, not X1). Two separate constants, a third PEM, a
# stale comment or an altered base64 line fails the run.
python3 -B test_est389.py

# EST-390: touch feedback. The pressed state machine (arm, stay, slide off, clear; an invalid target
# arms nothing), the one blend arithmetic, and pressed-rect == hit-rect for a sample of elements on
# every screen (tab bar + status keys, Nodes rows and its sheet, selector rows, Settings rows/steps/
# tabs/back/action/modals, the shared keyboard keys, emoji cells, Map tabs/chip/ghosts/markers/callout,
# RPTR list/page/picker keys). Plus: the key preview bubble for every key of the sheet is centred,
# above the finger, on screen and never over the composer draft line; the latency log once-a-minute
# window (millis wrap included); and the touch sound is the quietest short library sound (Soft Tick).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est390.cpp \
  ../../src/ui/chat_layout.cpp ../../src/ui/text_wrap.cpp ../../src/ui/map_chrome.cpp \
  ../../src/ui/rptr_layout.cpp ../../src/ui/node_menu.cpp ../../src/rptr_model.cpp \
  ../../src/sound_synth.cpp ../../src/timekeeper.cpp -o "$BIN48"
"$BIN48"
# The source-level half: one theme constant and one owner for the pressed look, no screen draws its
# own, the look is a frame overlay under the toast, the preview exists only for character keys, the
# touch sound stored default is OFF and plays only on a tap, and the latency line exists.
python3 -B test_est390.py

# EST-391: the NTP capture's sanity check compares in 64 bits. A struct with the device's 32-bit
# tv_sec holding 2026-11-11 is REJECTED by the old expression (the bug, reproduced) and ACCEPTED by
# ntp::replyEpochSane(), the firmware's own inline check, plus the window's four edges.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est391.cpp -o "$BIN49"
"$BIN49"

# EST-392: Settings > General's ONE position control. The labels (POSITION, Position, GPS / Fixed,
# no digit or dot in any of them), the two-row choice hanging from the row's own rectangle (tiling it
# exactly, on the glass at the scroll the shell opens it at, dropped above only when there is no room
# below), its hit test (0 / 1 on an option, -1 = Cancel on the row above, Back and the page around
# it), the answer (GPS selects GPS, Fixed opens EST-384's picker, Cancel keeps the source and the
# saved point), the pressed rect being the drawn option rect, and ONE control (no Position page in
# the page list). test_est392.py is the shell half: the General row is built from the header labels,
# the value is the selected source, the Node duplicate and the page are gone, and draw / hit / press
# all ask the one layout function.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est392.cpp -o "$BIN50"
"$BIN50"
python3 -B test_est392.py

# EST-393: the map picker's banner and its mark. The banner is 42 px and flush with the screen
# floor (bottom == 240, full width) at 320x240 and at 800x480; the coordinate line and both keys
# hang off its rectangle, so their drawn rects and tap rects move with it and the old hovered
# position is plain map. The picker's mark is the Map's own 7x7 centre cross: the same rectangle as
# mapChromeFor()/mapCentreCrossRect(), the same shared drawing helper, two 1 px lines in
# kMapCrossInk, no dot or ring or outline, at (160,133). test_est393.py is the source half: the
# picker and the Map call the one helper, the banner is placed from L.height (not the tab bar), and
# the shell's centre point is still mapScreenToLatLon(kMapCentreX, kMapCentreY).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est393.cpp \
  ../../src/ui/map_chrome.cpp ../../src/ui/rptr_layout.cpp -o "$BIN51"
"$BIN51"
python3 -B test_est393.py

# EST-394: a trace belongs to its repeaters, not to the screen. One route sampled in view A and then
# in view B (the content panned by +40/-25 px, and a zoom level in) lands EXACTLY where mapProject()
# puts the same lat/lon in view B: every route point, the dot, the tail and the direct-arrival ring.
# A running route is re-projected by sampleActive() without restarting its clock; with the pack not
# open the route stays live and draws nothing; the added cost of a 12-point sample is measured on
# this host. test_est394.py is the source half: the Map tab queues micro-degrees (no projection at
# arrival), build() re-samples every MapView, the 30 fps replay frames are skipped over a stale
# canvas and a drag's release draws the final position, and the picker's zoom label is drawn from its
# own rectangle UNDER the back key.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est394.cpp \
  ../../src/ui/map_trace.cpp ../../src/ui/map_chrome.cpp ../../src/ui/rptr_layout.cpp -o "$BIN52"
"$BIN52"
python3 -B test_est394.py

# EST-397: the core sweep fixes. The clock bound at INT32_MAX (the first epoch a 32-bit time_t turns
# negative is refused) and the 5 s fix expiry, both against the device headers; then the source half:
# every Serial statement in src/ is grepped for a position, an SSID, a LAN address, a message text or a
# name, with the look-alikes whitelisted and each reason stated. EST-399 TIGHTENED it: any `%s` fed by
# a name (name, m.name, ->name, .name) or a message text (text, m.text, ->text) fails, and the scan
# proves itself on 12 known-bad lines before it reads the tree.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est397.cpp ../../src/timekeeper.cpp -o "$BIN53"
"$BIN53"
python3 -B test_est397.py

# EST-399: the serious UI sweep findings. One "tap again" arm PER ROW (arm A, tap B -> B is armed and
# not forgotten; the window; the wrap; the label and the tap ask the same function), the pressed plate
# of the RPTR page's card key and banner button at EVERY scroll (the same helper the hit test uses,
# with the old scroll-0 plate proven to miss the finger), the same pressed-rect-contains-the-finger
# sweep over the other scrolling lists, and the emoji Recent row's batched NVS write (leave the page or
# 30 s, failure counted, 5 s retry, the wrap). test_est399.py is the shell half: the single
# g_deleteArmed flag is gone, each row family has its own arm, the destructive calls sit behind
# Tap::Confirmed, every page-leave path clears them, the pick no longer writes NVS, one writer checks
# putBytes' result, and the guard's whitelist stays minimal.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est399.cpp \
  ../../src/ui/rptr_layout.cpp ../../src/ui/chat_layout.cpp -o "$BIN54"
"$BIN54"
python3 -B test_est399.py

# EST-400: the S5 polish fixes. The manual-time stepper's range derived from the clock's own
# kMinSaneEpoch/kMaxSaneEpoch, the Stepper/Toggle label rooms, the one ellipsis cut (text fit, UTF-8
# never split) shared by drawTextEllipsis and the toast, the Info page's clipped/scrolled body and
# its line counter, the map callout's own text rows, and the picker's empty band from the layout.
# test_est400.py is the source half: one manual-time helper, the year wrap, the shared ui/keys.cpp,
# the RPTR wrapper, the toast, the notice log after the show, the Warn plate and the node-info clip.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est400.cpp \
  ../../src/ui/text_wrap.cpp ../../src/ui/map_chrome.cpp ../../src/ui/rptr_layout.cpp \
  ../../src/ui/node_menu.cpp -o "$BIN55"
"$BIN55"
python3 -B test_est400.py

# EST-401: step 1 of the charging bolt. POWER (0x0B) is written once at boot so the measure block and
# its references come up and STATUS0's VBUSOK means something; then the once-a-minute battery log line
# carries vbus + AIN1..AIN3. The C++ half: the three PWR bits (0x07 built from any read-back, unnamed
# bits preserved), the read-back rule that decides "the write took", the STATUS0 bit-7 decode (and the
# probe's own decode agreeing with it), the address/register map, and the four appended CSV columns
# (-1 for a failed STATUS0 read, never 0). test_est401.py is the source half: every I2C transmission to
# 0x22 is classified, and exactly one data write exists, to POWER, from the static helper the boot
# call reaches; SWITCHES0 is never written.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est401.cpp \
  ../../src/power_log_logic.cpp ../../src/diag_logic.cpp ../../src/timekeeper.cpp -o "$BIN56"
"$BIN56"
python3 -B test_est401.py

# EST-402: the charging bolt. One STATUS0 (0x40) read every 2 s from the loop (no writes), debounced
# by two equal reads in a row in charge_state.h; the bar draws a gold bolt over the battery icon
# (EST-404: 7 x 11, tips 2 px clear above and below the frame) while the debounced flag is true,
# and "unknown" (the chip did not answer at boot, the POWER write was not verified, or a poll read
# failed) never draws one. The C++ half is the debounce, the unknown-means-no-bolt rule and the
# shape's bounds; test_est402.py is the source half: the 2 s gate, the single read, the boot
# readiness, the one log line and the drawing path.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est402.cpp -o "$BIN57"
"$BIN57"
python3 -B test_est402.py

# EST-403: the clock kept across a warm restart. clock_keep.h holds the ONE boot decision; it uses
# EST-387's reset-reason table, so a KEEP restart (software/OTA/panic/watchdog/external/sdio) with a
# valid record restores the epoch, and every RESET reason (power-on/brown-out/deep-sleep/unknown)
# stays at "no time" as before. Bad magic, real != 1, an unknown source, an insane saved or restored
# epoch, an RTC counter that went backwards, and a restored epoch below the floor are all refused.
# The record's RTC counter advances the saved epoch by the seconds that really elapsed, and `kept`
# has the lowest rank, so every fresh source (GPS, Wi-Fi, phone, manual, gps-nofix) replaces it at
# once. The same test proves TimeKeeper::setKept()/keptFrom() and the KeepFn callback that refreshes
# the record on every accepted source.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est403.cpp ../../src/timekeeper.cpp -o "$BIN58"
"$BIN58"

# EST-404: the charging bolt is drawn LAST, over the battery icon and its frame, not clipped to the
# interior. 7 x 11: the middle 44 % of the 16 px body and 4 px taller than the 7 px body, so the top
# and bottom tips stand 2 px above and below the frame, each with a 1 px bar-colour edge. test_est404
# repeats the geometry on the WORST-CASE status row (100 %, "GPS 14", every optional icon on) and
# proves the bolt and its edge overdraw no other cell; it also checks the gold is at least 2 px wide
# in every row so it stays readable over the red low-battery fill and the green full one.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est404.cpp -o "$BIN59"
"$BIN59"

# EST-405: the screen auto-off timer and its two saved values (src/screen_idle.h, plain C++). The
# ticket's own rules: default OFF and a missing NVS key is OFF (a disabled timer never sleeps); the
# five times 30 s / 1 min / 2 min / 5 min / 10 min with 1 min as the default; the count restarts on a
# touch, a wake-button press and a USB command, and survives a millis() wrap; each blocker on its own
# (keyboard/composer, pop-up, OTA, map drag/zoom, the 5 s message pop-up) keeps the screen on and
# gives the whole time again when it lifts; the 30 s USB quiet window; and the store writing ONLY
# when a value changes, with a failed write still flipping RAM.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est405.cpp -o "$BIN60"
"$BIN60"

# EST-406: Send left the chat keyboard for the message input line. The chat's letters and 123 bottom
# rows have no Send key and the space bar took its room; Backspace is exactly where it was and the
# other keys are within 1 px of their old place (the editors keep their own Send key and sheet). The
# new send button on the draft line is one 30 x 26 rect that draws and hits the same pixels, the text
# plate stops before it, and it is dim and inert while the draft is null, empty or only whitespace.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est406.cpp ../../src/ui/chat_layout.cpp \
  -o "$BIN61"
"$BIN61"

# EST-409: the Map sweep fixes. C++: a route point 50 km off-centre at z18 rejects the frame (no
# int16 wrap), a 12-hop packet reports the clamped 8 with its flag, two senders that share the low
# byte no longer replace each other (the full 32-bit key), the dedupe loop keeps the next route, and
# the signature cache rebuilds only when its source moves. test_est409.py is the source half: the
# toggles read the state after the NVS write, the trace logs carry counts only, the contact table is
# cached, the tail capacity is the array's own size, the drops are counted by reason, the self dot is
# outside the contact budget, hops are clamped once, and the query paths do not advance the trace.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est409.cpp \
  ../../src/ui/map_trace.cpp -o "$BIN62"
"$BIN62"
python3 -B test_est409.py

# EST-412: the M5 CardKB on Grove. Every key-code class (printable ASCII, backspace, enter, esc,
# left/right with and without a cursor, up/down, and a list of unknown bytes), the OFF default with a
# missing NVS key, and the polling gate (switch on AND found AND a text field open). test_est412.py
# is the shell half: the row's default, the NVS key, the boot/enable paths, and the one poll inside
# the loop under that gate.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est412.cpp -o "$BIN63"
"$BIN63"
python3 -B test_est412.py

# EST-413: the timekeeper's floor is not a real time. While the clock is not real since boot a due
# battery-log minute writes NO row and is counted as skipped; the first minute after a real source
# stamps and writes normally. The manual-time steppers seed from the build time, never the stale
# saved floor. test_est413.py is the source half: the logger's gate and tail refusal, the command's
# no-real-time answer and skip count, the manual seed, and the repeater clock write's own check.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est413.cpp \
  ../../src/power_log_logic.cpp ../../src/timekeeper.cpp -o "$BIN64"
"$BIN64"
python3 -B test_est413.py

# EST-414: Filip's five glass-test changes. C++: the scroll indicator's geometry on all three lists
# (thumb size = visible / total, top = scroll / max, top / middle / end, the bottom-anchored chat
# band at rest, the minimum thumb, nothing when the content fits), the track never over text, and the
# ONE give-and-snap rule with every old name answering the same numbers. test_est414.py is the source
# half: one indicator helper called by the three lists, the one bounce rule (and the Nodes refresh no
# longer clamping the give away), no scan on the RPTR tab open, Settings > Channels without an unread
# count, and the live "Open repeater" row.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est414.cpp \
  ../../src/ui/chat_layout.cpp -o "$BIN65"
"$BIN65"
python3 -B test_est414.py
