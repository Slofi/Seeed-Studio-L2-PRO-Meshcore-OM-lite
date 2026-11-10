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
trap 'rm -f "$BIN" "$BIN2" "$BIN3" "$BIN4" "$BIN5" "$BIN6" "$BIN7"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_layout.cpp -o "$BIN"
"$BIN"
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_contact_store.cpp -o "$BIN2"
"$BIN2"
# Chat: the word wrapper and its UTF-8 fallback, the header and the chat selector arithmetic, the
# message band, the keyboard sheet and the held message sequence numbers (a full ring plus one new
# message must not move a reply target).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_chat.cpp \
  ../../src/ui/chat_layout.cpp ../../src/ui/text_wrap.cpp -o "$BIN3"
# Time: NMEA RMC/ZDA (including the ZDA build-year gate), the source ranking, the floor and the
# Ljubljana DST clock.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_timekeeper.cpp \
  ../../src/timekeeper.cpp -o "$BIN4"
# Wake touch gate: a finger held through the wake is ignored, a new press after 300 ms counts.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_touch_gate.cpp \
  ../../src/touch_gate.cpp -o "$BIN5"
# "Last heard" text (a stamp older than the build says "--") and the status bar: the GPS cell, the
# node count cell, and the fitting order that keeps the two groups kStatusGap apart at 320 px.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_age.cpp \
  ../../src/ui/age.cpp -o "$BIN6"
# GSV: expected vs heard (an empty C/N0 field is not a reception), per constellation, the new-cycle
# reset, the 60 s best-C/N0 window and the most heard at once since boot.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_gps.cpp -o "$BIN7"
"$BIN3"
"$BIN4"
"$BIN5"
"$BIN6"
"$BIN7"
