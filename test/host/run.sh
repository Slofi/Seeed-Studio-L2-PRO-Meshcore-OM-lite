#!/bin/sh
# Host tests for the om_lite_l2 tree. Non-zero exit on any failure; no device and no extra
# packages needed (system g++ and python3 only).
set -eu
cd "$(dirname "$0")"
# -B: no __pycache__ left behind in the tree the reviewer diffs.
python3 -B test_shot.py

# Layout / hit testing and the contact file + save policy are pure C++ (no Arduino): build them
# with the system g++.
BIN="$(mktemp /tmp/om-lite-layout.XXXXXX)"
BIN2="$(mktemp /tmp/om-lite-contacts.XXXXXX)"
BIN3="$(mktemp /tmp/om-lite-chat.XXXXXX)"
BIN4="$(mktemp /tmp/om-lite-time.XXXXXX)"
BIN5="$(mktemp /tmp/om-lite-gate.XXXXXX)"
BIN6="$(mktemp /tmp/om-lite-age.XXXXXX)"
BIN7="$(mktemp /tmp/om-lite-gps.XXXXXX)"
BIN8="$(mktemp /tmp/om-lite-bar.XXXXXX)"
BIN9="$(mktemp /tmp/om-lite-gpsmode.XXXXXX)"
BIN10="$(mktemp /tmp/om-lite-fault.XXXXXX)"
BIN11="$(mktemp /tmp/om-lite-fade.XXXXXX)"
trap 'rm -f "$BIN" "$BIN2" "$BIN3" "$BIN4" "$BIN5" "$BIN6" "$BIN7" "$BIN8" "$BIN9" "$BIN10" "$BIN11"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_layout.cpp -o "$BIN"
"$BIN"
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_contact_store.cpp -o "$BIN2"
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
# Wake touch gate: a finger held through the wake is ignored, a new press after 300 ms counts.
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
# The L76K one-boot diagnostic: $PCAS03 GSV off/on TX-path probe, mode 7, mode 5, mode 3 restored.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_gps_mode.cpp -o "$BIN9"
# The radio-fault mark's state: each reason, a good TX clears, a radio re-init clears, silence never.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_radio_fault.cpp -o "$BIN10"
# The backlight ramp: start, end, monotonic, many distinct 8-bit levels, a wake reverses a fade-out.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_fade.cpp \
  ../../src/fade.cpp -o "$BIN11"
"$BIN3"
"$BIN4"
"$BIN5"
"$BIN6"
"$BIN7"
"$BIN8"
"$BIN9"
"$BIN10"
"$BIN11"
