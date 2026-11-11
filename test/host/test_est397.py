#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-397 (#6-#8 and the guard): no log line may carry a position, an SSID, a LAN address, a message
# text or a person's name (REBUILD.md rule 9, the repo is public).
#
# Source-level, deliberately: the point is the FORMAT STRINGS and the arguments handed to them, not a
# run. It scans EVERY Serial.printf/println statement in src/ (all files, recursively), so a new log
# line cannot reintroduce the four patterns this ticket removed:
#   1. a float conversion in a statement that also names a coordinate (lat/lon/latitude/longitude),
#   2. an `ip=` field in the format,
#   3. a `%s` fed by a message body (text, m.text, s_lastText, body) or by a sender's name
#      (from.name, ci->name),
#   4. a `%s` fed by an SSID, or an `ssid=` field in the format.
# The four whitelisted lines are proven safe one by one, each with its reason; the list is short on
# purpose and empty by default would be a lie (there are legitimate diagnostic lines that mention a
# coordinate's ABSENCE, e.g. "no position").
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FAIL = 0

LOG_CALL = re.compile(r"Serial\.(?:printf|println)\s*\(")
FLOAT = re.compile(r"%(?:[-+ 0#]*\d*(?:\.\d+)?)?[fFeEgG]")
COORD_NAME = re.compile(r"\b(?:lat|lon|latitude|longitude|s_cLat|s_cLon)\b", re.IGNORECASE)
IP_FIELD = re.compile(r"\bip=")
SSID_FIELD = re.compile(r"\bssid\s*=\s*%", re.IGNORECASE)
TEXT_ARG = re.compile(r"(?:\bm\.text\b|\bs_lastText\b|(?<![\w.>(])(?:text|body)\b)")
NAME_ARG = re.compile(r"(?:\bfrom\.name\b|ci->name)")
SSID_ARG = re.compile(r"(?<![\w.>(])ssid\b", re.IGNORECASE)

# (path, needle that identifies the statement, why it is safe)
# Every entry was read in its context. The rule these all sit on is "no other people's message texts
# or names in a log": these print a string on an EXPLICIT command answer (the operator asked for that
# one record on the physical device) or a UI notice the app itself composed, never a receive path.
# Claude: if any of them should be tightened, they are the known remainder (see PROGRESS, EST-397).
WHITELIST = [
    ("src/ble_link.cpp", '"[ble] notice: %s',
     "a toast the app itself shows; the text is our own notice wording, not a chat body"),
    ("src/main.cpp", '"[chat] DM resend %s',
     "our OWN outgoing DM being resent, length/state debugging; the body is ours, not a contact's"),
    ("src/main.cpp", '"[chat] resend %s',
     "the same for a channel message we sent"),
    ("src/main.cpp", '"[hist] notice: %s',
     "a UI notice (toast) string the app composed, not a message from the air"),
    ("src/main.cpp", '"[nodes] %s',
     "the Nodes screen's notice/toast line, app-composed text"),
    ("src/mesh_link.cpp", '"[mc] message for an unknown channel, ignored: %s',
     "STORES NOTHING; the one debug line for a message we cannot even attach to a channel"),
    ("src/mesh_link.cpp", '"[hist] id=%lu seq=%lu up=%lu wall=%s real=%u from=\\\"%s\\\"',
     "`history` USB answer, printed only when the operator asks for that record"),
    ("src/rptr_link.cpp", '"[rptr] cli <- %s: %s%s',
     "a repeater's CLI ANSWER (its own status text), not a person's message"),
    ("src/usb_commands.cpp", '"[msg] seq=%lu where=\\\"%s\\\" from=\\\"%s\\\"',
     "`msg` USB answer for one record the operator names; never automatic"),
    ("src/usb_commands.cpp", '"[wifi] ssid=\\\"%s\\\" rssi=%d secured=%d',
     "`wifi scan` answer: a SCANNED access point's name, not the user's own network; asked for by hand"),
    ("src/wifi_link.cpp", '"[ntp] client server=%s addr=%s',
     "the PUBLIC NTP server's resolved address (`text` is the address text here), allowed by test_est385"),
    ("src/wifi_link.cpp", '"[ntp] dns %s ok public_ip=%s',
     "the same public NTP address, allowed by test_est385"),
]


def fail(msg):
    global FAIL
    print("FAIL", msg)
    FAIL += 1


def statements(text):
    """Every Serial.printf/println call, whole (from the call to its closing paren)."""
    for m in LOG_CALL.finditer(text):
        i = m.end()
        depth = 1
        while i < len(text) and depth:
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
            i += 1
        yield m.start(), text.count("\n", 0, m.start()) + 1, text[m.start():i]


def format_literals(stmt):
    return re.findall(r'"((?:[^"\\\n]|\\.)*)"', stmt)


def whitelisted(rel, stmt):
    for wpath, needle, _why in WHITELIST:
        if wpath == rel and needle in stmt:
            return True
    return False


def check_file(path):
    rel = path.relative_to(ROOT).as_posix()
    text = path.read_text(encoding="utf-8", errors="replace")
    count = 0
    for _pos, line, stmt in statements(text):
        count += 1
        literals = format_literals(stmt)
        fmt = " ".join(literals)
        args = stmt[stmt.find("(") + 1:]
        # A LENGTH is not the body: strlen(m.text) / sizeof(...) must not look like a printed text.
        args = re.sub(r"\b(?:strlen|sizeof)\s*\([^()]*\)", "", args)
        bad = []
        if FLOAT.search(fmt) and COORD_NAME.search(stmt):
            bad.append("a float format beside a coordinate name")
        if IP_FIELD.search(fmt):
            bad.append("an ip= field")
        if "%s" in fmt and TEXT_ARG.search(args):
            bad.append("a %s fed by a message text")
        if "%s" in fmt and NAME_ARG.search(args):
            bad.append("a %s fed by a sender's name")
        if "%s" in fmt and SSID_ARG.search(args):
            bad.append("a %s fed by an SSID")
        if SSID_FIELD.search(fmt):
            bad.append("an ssid= field")
        if not bad:
            continue
        if whitelisted(rel, stmt):
            continue
        for what in bad:
            fail(f"{rel}:{line}: log carries {what}: {stmt.splitlines()[0].strip()}")
    return count


def main():
    files = sorted(list((ROOT / "src").rglob("*.cpp")) + list((ROOT / "src").rglob("*.h")))
    if len(files) < 60:
        fail(f"only {len(files)} source files found; the scan is not looking at the tree")
    total = 0
    for path in files:
        total += check_file(path)
    # Positive control: the scan must SEE the log lines, otherwise "clean" means nothing.
    if total < 200:
        fail(f"only {total} Serial statements scanned; the matcher is broken")

    # And the four lines this ticket rewrote must be the new shape, not merely pattern-free.
    src = (ROOT / "src")
    mesh = (src / "mesh_link.cpp").read_text(encoding="utf-8")
    for needle in ('"[mc] DM seq %lu: %u B (hops %d snr %.2f)\\n"',
                   '"[mc] channel %d: %u B (hops %d snr %.2f)%s\\n"',
                   '"[ble] phone sent ch%d: %u B\\n"',
                   '"[ble] phone sent DM seq %lu: %u B\\n"'):
        if needle not in mesh:
            fail(f"mesh_link.cpp: the rewritten log line is missing: {needle}")
    wifi = (src / "wifi_link.cpp").read_text(encoding="utf-8")
    for needle in ('"[wifi] saved index %d (%d of %d)\\n"',
                   '"[wifi] connected idx=%d rssi=%d internal=%u largest=%u (name and address not printed)\\n"',
                   '"[wifi] connecting idx=%d%s\\n"',
                   '"[wifi] forgot index %d (%d left)\\n"'):
        if needle not in wifi:
            fail(f"wifi_link.cpp: the rewritten log line is missing: {needle}")
    mapc = (src / "map.cpp").read_text(encoding="utf-8")
    if '"[map] z%d%s tx %d..%d ty %d..%d -> %d tiles\\n"' not in mapc:
        fail("map.cpp: the tile log line is missing or still prints a position")
    if "%.5f" in mapc:
        fail("map.cpp: a five-decimal position format is still in the file")

    # The fix expiry is enforced in exactly one place, called from gpsPoll().
    board = (src / "board.cpp").read_text(encoding="utf-8")
    if board.count("gpsExpireFix(out,") != 1:
        fail("board.cpp: gpsExpireFix() must be called exactly once (one expiry rule)")
    if "out.lastFixMs = millis();" not in board:
        fail("board.cpp: a GGA with a fix does not restamp lastFixMs")

    if FAIL:
        print(f"{FAIL} est397 source check(s) FAILED")
        return 1
    print(f"est397: {total} Serial statements in {len(files)} files carry no position, SSID, LAN address, "
          f"message text or name ({len(WHITELIST)} whitelisted, each proven safe)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
