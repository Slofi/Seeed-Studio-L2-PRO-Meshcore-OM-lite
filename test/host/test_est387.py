#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-387: no GPS log format may print a coordinate (REBUILD.md rule 9).
#
# Section 1 of the ticket adds boot/init/aid lines to board.cpp and a whole aiding surface in
# gps_aid.h. This is a source-level grep, deliberately: the point is the FORMAT STRINGS, not a run.
# Every GPS log line in the new code is checked for a float conversion or a lat/lon field name, and
# the aid answer line must carry the two tokens the ticket names and nothing of the payload.
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


FLOAT = re.compile(r"%(?:[-+ 0#]*\d*(?:\.\d+)?)?[fFeEgG]")
COORD_NAME = re.compile(r"\b(?:lat|lon|latitude|longitude)\b", re.IGNORECASE)


def check_lines(rel, keep):
    """Every Serial.printf/println in `rel` whose line matches `keep` must be coordinate-free."""
    for i, line in enumerate(read(rel).splitlines(), 1):
        if "Serial.printf" not in line and "Serial.println" not in line:
            continue
        if not keep(line):
            continue
        if FLOAT.search(line):
            fail(f"{rel}:{i}: float format in a GPS log line: {line.strip()}")
        if COORD_NAME.search(line):
            fail(f"{rel}:{i}: coordinate name in a GPS log line: {line.strip()}")


def main():
    # 1. The GPS/aid log lines added by this ticket (board.cpp) and the two new headers.
    check_lines("src/board.cpp", lambda line: "[gps" in line or "[gpsraw" in line)
    check_lines("src/gps_aid.h", lambda _line: True)
    check_lines("src/gps_boot.h", lambda _line: True)
    # The existing diagnostic surface stays clean too.
    check_lines("src/gps_raw.cpp", lambda _line: True)

    # 2. The aid answer line carries the ticket's two tokens and a source name, never a value.
    board = read("src/board.cpp")
    if '"[gps] aid time=%s pos=%s src=%s\\n"' not in board:
        fail("the aid answer line does not have the ticket's time=/pos=/src= shape")
    for token in ('"ok"', '"nak"', '"no-answer"', '"sent"', '"none"'):
        if token not in board:
            fail(f"the aid answer never prints {token}")

    # 3. The boot lines the ticket names, both of them, exactly as asked.
    if '"[gps] boot: reset (%s)' not in board:
        fail("the reset boot line is missing")
    if '"[gps] boot: kept running (reset reason %d)' not in board:
        fail("the kept-running boot line is missing")

    # 4. The aid code is switched off by a constant (the ticket: keep it off until the module ACKs),
    #    and the switch is one place.
    aid = read("src/gps_aid.h")
    if "constexpr bool kEnabled = false;" not in aid:
        fail("gps_aid.h: kEnabled is not a single false constant")

    # 5. And nothing in the aiding path formats a payload byte for a log.
    if "payload[%d]" in aid or "%.6f" in aid:
        fail("gps_aid.h: a payload byte reaches a format string")

    if FAIL:
        print(f"{FAIL} est387 source check(s) FAILED")
        return 1
    print("est387: no coordinate in any GPS log format, the aid line and boot lines are the ones asked for")
    return 0


if __name__ == "__main__":
    sys.exit(main())
