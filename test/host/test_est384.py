#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-384: the new position code must never print a coordinate (REBUILD.md rule 9).
#
# This is a source-level grep, deliberately: the point is the FORMAT STRINGS, not a run. Every log
# line the position code and the two new USB answers can produce is checked for a float format or a
# lat/lon field name; `status` must carry the source token and nothing else.
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FAIL = 0


def fail(msg):
    global FAIL
    print("FAIL", msg)
    FAIL += 1


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def log_lines(text):
    """Every line that can write a log/answer format string."""
    for i, line in enumerate(text.splitlines(), 1):
        if "Serial.printf" in line or "Serial.println" in line or "snprintf(reply" in line:
            yield i, line


FLOAT = re.compile(r"%(?:[-+ 0#]*\d*(?:\.\d+)?)?[fFeEgG]")
COORD_NAME = re.compile(r"\b(?:lat|lon|latitude|longitude)\b", re.IGNORECASE)


def check_no_coordinate(rel):
    text = read(rel)
    for i, line in log_lines(text):
        if FLOAT.search(line):
            fail(f"{rel}:{i}: float format in a log/answer line: {line.strip()}")
        if COORD_NAME.search(line):
            fail(f"{rel}:{i}: coordinate name in a log/answer line: {line.strip()}")


def main():
    check_no_coordinate("src/position.cpp")
    # position_source.h has no logs today; if it grows one, the same rule applies.
    check_no_coordinate("src/position_source.h")

    usb = read("src/usb_commands.cpp")
    # The status answer must carry the source token, not a position.
    status_format = "\n".join(
        line for line in usb.splitlines() if 'ok status mesh=%s name=' in line
    )
    if "pos=%s" not in usb:
        fail("status does not carry pos=%s")
    if FLOAT.search(status_format):
        fail("status format has a float specifier")
    if COORD_NAME.search(status_format):
        fail("status format names a coordinate")

    # The pos answers name the source only.
    pos_lines = [line for line in usb.splitlines() if '"[cmd] ok pos' in line]
    if not pos_lines:
        fail("no [cmd] ok pos answer line found")
    for line in pos_lines:
        if FLOAT.search(line) or COORD_NAME.search(line):
            fail(f"pos answer carries a coordinate: {line.strip()}")

    # The token itself is one of the three words, in the one place that owns it.
    src = read("src/position_source.h")
    for token in ('"gps"', '"fixed"', '"none"'):
        if token not in src:
            fail(f"position_source.h does not return {token}")

    # And the settings strings never show a coordinate on the two new pages.
    main_cpp = read("src/main.cpp")
    for needle in ("Position", "Change fixed position"):
        if needle not in main_cpp:
            fail(f"settings row {needle!r} missing")
    if "%.4f, %.4f" in main_cpp:
        fail("main.cpp still formats a coordinate for display")

    if FAIL:
        print(f"{FAIL} est384 source check(s) FAILED")
        return 1
    print("est384: no coordinate in the new log/answer formats")
    return 0


if __name__ == "__main__":
    sys.exit(main())
