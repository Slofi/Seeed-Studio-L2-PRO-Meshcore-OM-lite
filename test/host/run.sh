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
trap 'rm -f "$BIN" "$BIN2" "$BIN3" "$BIN4"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_layout.cpp -o "$BIN"
"$BIN"
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_contact_store.cpp -o "$BIN2"
"$BIN2"
# Chat: the word wrapper and its UTF-8 fallback, the header and the chat selector arithmetic, the
# message band, the keyboard sheet and the held message sequence numbers (a full ring plus one new
# message must not move a reply target).
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_chat.cpp \
  ../../src/ui/chat_layout.cpp ../../src/ui/text_wrap.cpp -o "$BIN3"
# Time: NMEA RMC/ZDA, the source ranking, the floor and the Ljubljana DST clock.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_timekeeper.cpp \
  ../../src/timekeeper.cpp -o "$BIN4"
"$BIN3"
"$BIN4"
