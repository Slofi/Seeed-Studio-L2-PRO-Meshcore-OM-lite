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
trap 'rm -f "$BIN" "$BIN2"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_layout.cpp -o "$BIN"
"$BIN"
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../src test_contact_store.cpp -o "$BIN2"
"$BIN2"
