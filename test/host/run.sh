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
trap 'rm -f "$BIN" "$BIN2" "$BIN3" "$BIN4" "$BIN5" "$BIN6" "$BIN7" "$BIN8" "$BIN9" "$BIN10" "$BIN11" "$BIN12" "$BIN13" "$BIN14" "$BIN15" "$BIN16" "$BIN17" "$BIN18" "$BIN19" "$BIN20" "$BIN21" "$BIN22" "$BIN23" "$BIN24" "$BIN25" "$BIN26" "$BIN27" "$BIN28" "$BIN29"' EXIT
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

# EST-359/361: the shared sound library. The synth is the same sound_synth.cpp the firmware and
# tools/mksounds.cpp build: 54 sounds, >=6 per event, unique names and note sequences, all lengths in
# range (low battery 1.0..1.5 s and near the cap), the boot D7-gap-F7 phrase, no clipping at full
# scale, the EST-359 choice migration, and the `play <event> [sound-id]` USB command.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est359.cpp \
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
