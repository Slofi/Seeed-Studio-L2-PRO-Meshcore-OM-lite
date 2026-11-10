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
trap 'rm -f "$BIN" "$BIN2" "$BIN3" "$BIN4" "$BIN5" "$BIN6" "$BIN7" "$BIN8" "$BIN9" "$BIN10" "$BIN11" "$BIN12" "$BIN13" "$BIN14" "$BIN15" "$BIN16"' EXIT
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
  ../../src/usb_cmd_logic.cpp -o "$BIN14"
# EST-344: the DM ring's one allocation path (send, reply, two more, wrap), the channel ring's shared
# sequence rule, the toast overlay state and MeshCore's encoded out_path_len (1B/2B/3B/unknown).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_est344.cpp -o "$BIN15"
# EST-345: the generated emoji table (sorted, every cell decodes), the UTF-8 sequencer (selector,
# skin tone, ZWJ, flag), emoji width measuring and the Recent row.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_emoji.cpp \
  ../../src/ui/text_wrap.cpp ../../src/ui/emoji_picker.cpp -o "$BIN16"
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
