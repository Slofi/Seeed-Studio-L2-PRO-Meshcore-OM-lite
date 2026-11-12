#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-397 (#6-#8 and the guard), TIGHTENED by EST-399: no log line may carry a position, an SSID, a LAN
# address, a message text or a person's name (REBUILD.md rule 9, the repo is public).
#
# Source-level, deliberately: the point is the FORMAT STRINGS and the arguments handed to them, not a
# run. It scans EVERY Serial.printf/println statement in src/ (all files, recursively), so a new log
# line cannot reintroduce these patterns:
#   1. a float conversion in a statement that also names a coordinate (lat, lon, latitude, longitude,
#      the advert's lat_e6/lon_e6 fields, a member called .lat/.lon),
#   2. an `ip=` field in the format,
#   3. a `%s` fed by a message body: text, m.text, ->text, .text, s_lastText, body,
#   4. a `%s` fed by a name: name, m.name, ->name, .name, from.name, ci->name, g_replyName,
#   5. a `%s` fed by an SSID, or an `ssid=` field in the format.
#
# EST-399 tightened 3 and 4. The first version knew only the two spellings it had just fixed
# (from.name, ci->name) plus m.text/s_lastText, so it walked past main.cpp:1279/1361 (node names) and
# 1374/1377 (message text on resend) and printed a clean bill of health for them (Claude's EST-397
# review note). A `%s` fed by ANY identifier whose last word is `name` or `text`, through `.` or `->`,
# is refused now. The argument expression is taken with its STRING LITERALS stripped first, so a
# format that explains itself ("name and address not printed") is not a false hit, and a LENGTH
# (strlen(x) / sizeof(x)) is stripped too, because a length is the fix, not the leak.
#
# The scan is proven on known-bad input (NEGATIVE_CONTROLS below): if the matchers stop catching the
# shapes this repo has shipped, the test fails even when the tree is clean. Run the scan alone against
# an older tree with
#   python3 test_est397.py /path/to/old/tree
# (the shape checks belong to the tree being fixed, so they only run without an argument).
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FAIL = 0

LOG_CALL = re.compile(r"Serial\.(?:printf|println)\s*\(")
FLOAT = re.compile(r"%(?:[-+ 0#]*\d*(?:\.\d+)?)?[fFeEgG]")
COORD_NAME = re.compile(
    r"\b(?:lat|lon|latitude|longitude|s_cLat|s_cLon|lat_e6|lon_e6)\b"
    r"|(?:\.|->)(?:lat|lon)\b",
    re.IGNORECASE)
IP_FIELD = re.compile(r"\bip=")
SSID_FIELD = re.compile(r"\bssid\s*=\s*%", re.IGNORECASE)
# A message body, however it is spelled: `text`, `m.text`, `msg->text`, `s_lastText`, `body`. The
# shell's own holders of chat text are named here too (the draft g_kbText, the wire buffer a send
# builds, the `@Name ` tag a mention prepends): none of them is spelled text, and EST-396 #2 was
# exactly those three lines.
TEXT_ARG = re.compile(r"(?:\.|->)(?:text|body)\b|\bs_lastText\b|(?<![\w.>(])(?:text|body)\b"
                      r"|(?<![\w.>(])(?:g_kbText|wire|tag)\b")
# Any name: `name`, `m.name`, `ci->name`, `from.name`, and our own stores whose last word is `Name`
# (g_replyName, s_currentName). A FUNCTION whose name ends in Name (om::timeSourceName(...)) is not a
# string, so `Name` followed by `(` is not a hit.
# EST-409: a SENDER is a node's name too. The Map's trace lines printed `sender` (`sender ? sender :
# "?"`) and the guard walked past them, because none of the spellings above matches a bare local that
# merely HOLDS a name. `who` is the same kind of holder in map_tab.cpp (meshMsgName() results).
NAME_ARG = re.compile(r"(?:\.|->)name\b|(?<![\w.>(])name\b|\b[A-Za-z_]\w*Name\b(?!\s*\()"
                      r"|(?<![\w.>(])(?:sender|who)\b")
SSID_ARG = re.compile(r"(?<![\w.>(])ssid\b", re.IGNORECASE)

# (path, needle that identifies the statement, why it is safe)
# Every entry was read in its context. The rule these all sit on is "no other people's message texts
# or names in a log": these print a string on an EXPLICIT command answer (the operator asked for that
# one record on the physical device) or a notice the app itself composed, never a receive path.
# Claude approved the USB `msg`/`msgs` and `history` answers (EST-397 review note): on-demand debug
# commands, never automatic.
WHITELIST = [
    ("src/ble_link.cpp", '"[ble] notice: %s',
     "a toast the app itself shows; the text is our own notice wording, not a chat body"),
    ("src/main.cpp", '"[hist] notice: %s',
     "a UI notice (toast) string the app composed, not a message from the air"),
    ("src/main.cpp", '"[nodes] %s',
     "the Nodes screen's notice/toast line, app-composed text"),
    ("src/mesh_link.cpp", '"[mc] message for an unknown channel, ignored: %s',
     "STORES NOTHING; the one debug line for a message we cannot even attach to a channel"),
    ("src/mesh_link.cpp", '"[hist] id=%lu seq=%lu up=%lu wall=%s real=%u from=\\"%s\\"',
     "`history` USB answer, printed only when the operator asks for that record (Claude, EST-397)"),
    ("src/rptr_link.cpp", '"[rptr] cli <- %s: %s%s',
     "a repeater's CLI ANSWER (its own status text), not a person's message"),
    ("src/screens/rptr_tab.cpp", '"[rptr] answer does not fit \\"%s\\", ignored: %s',
     "the same repeater CLI ANSWER, logged because it did not fit our buffer"),
    ("src/usb_commands.cpp", '"[msg] seq=%lu where=\\"%s\\" from=\\"%s\\"',
     "`msg` USB answer for one record the operator names; never automatic (Claude, EST-397)"),
    ("src/usb_commands.cpp", '"[wifi] ssid=\\"%s\\" rssi=%d secured=%d',
     "`wifi scan` answer: a SCANNED access point's name, asked for by hand"),
    # EST-399: the four remaining USB answers. Same class as `msg`/`msgs` above: one line per command
    # the operator types on the device, read by tools/cmd.py, never printed on a receive or a send.
    ("src/usb_commands.cpp", '"[node] name=\\"%s\\" type=%s key=%s heard=%s hops=%s',
     "`nodes` USB answer: one bench listing, asked for by hand"),
    ("src/usb_commands.cpp", '"[chat] \\"%s\\" unread=%d shown=%s card=%s',
     "`chats` USB answer: the per-chat mismatch check, asked for by hand"),
    ("src/usb_commands.cpp", '"[rptr] row %d name=\\"%s\\" key=%02x%02x%02x',
     "`rptr scan` USB answer: one discovered row, asked for by hand"),
    ("src/usb_commands.cpp", '"[rptr] status name=\\"%s\\" uptime=\\"%s\\"',
     "`rptr status` USB answer: the repeater's own status, asked for by hand"),
    ("src/ui/fonts.cpp", '"[ui] font %s: %s (%u bytes)',
     "a FONT asset's file name from our own face table (theme.h), not a person"),
    ("src/wifi_link.cpp", '"[ntp] client server=%s addr=%s',
     "the PUBLIC NTP server's resolved address (`text` is the address text here), allowed by test_est385"),
    ("src/wifi_link.cpp", '"[ntp] dns %s ok public_ip=%s',
     "the same public NTP address, allowed by test_est385"),
]

# Argument-level exemptions: narrower than a statement whitelist, because they exempt ONE expression
# while the rest of the line is still scanned by every rule. Each entry was read in its context.
ARG_EXEMPT = [
    (r"(?:srv|s_ntp\.(?:next)?[Ss]erver\(\))\s*\.\s*text",
     "the PUBLIC NTP server's host name (test_est385 allows it by name; it is not a person)"),
    (r"toastState\(\)\.text",
     "the toast's own text, which this app composed (the whitelisted [hist]/[nodes] notice lines)"),
]


# The matchers must still catch these, or "clean" means nothing. Every entry is the exact shape of a
# line this repo has shipped (EST-396 #1/#2, EST-397 #6-#8, EST-398 #3), so a regression in the
# regexes fails the test loudly.
NEGATIVE_CONTROLS = [
    ('Serial.printf("[chat] map %s %.6f,%.6f\\n", m.name, lat, lon);',
     "a float format beside a coordinate name"),
    ('Serial.printf("[nodes] map %s\\n", name);', "a %s fed by a name"),
    ('Serial.printf("[chat] map %s\\n", m.name);', "a %s fed by a name"),
    ('Serial.printf("[nodes] info %s\\n", g_nodeInfoModel.name);', "a %s fed by a name"),
    ('Serial.printf("[chat] reply to %s\\n", g_replyName);', "a %s fed by a name"),
    ('Serial.printf("[nodes] menu %d (%s)\\n", node, ci->name);', "a %s fed by a name"),
    ('Serial.printf("[chat] DM resend %s \'%s\'\\n", sent ? "ok" : "failed", m.text);',
     "a %s fed by a message text"),
    ('Serial.printf("[mc] UI send: %s \'%s\'\\n", sent ? "ok" : "failed", wire.text);',
     "a %s fed by a message text"),
    ('Serial.printf("[hist] open \\"%s\\"\\n", text);', "a %s fed by a message text"),
    ('Serial.printf("[mc] UI DM send: %s \'%s\'\\n", sent ? "ok" : "failed", g_kbText);',
     "a %s fed by a message text"),
    ('Serial.printf("[mc] UI send: %s \'%s\'\\n", sent ? "ok" : "failed", wire);',
     "a %s fed by a message text"),
    ('Serial.printf("[chat] mention %s\\n", tag);', "a %s fed by a message text"),
    # EST-409: the Map's trace lines. Both printed the sender's name through `sender ? sender : "?"`;
    # the widened NAME_ARG must catch the bare holder, and `who` (meshMsgName()'s result) too.
    ('Serial.printf("[map] trace %s %u hop(s), ring only\\n", sender ? sender : "?", hops);',
     "a %s fed by a name"),
    ('Serial.printf("[map] trace %s %u hop(s)\\n", who, hops);', "a %s fed by a name"),
    ('Serial.printf("[wifi] connected %s\\n", ssid);', "a %s fed by an SSID"),
    ('Serial.printf("[wifi] up ip=%s\\n", ip);', "an ip= field"),
    ('Serial.printf("[wifi] ssid=%s\\n", s);', "an ssid= field"),
]

# The lines this ticket rewrote must be the NEW shape, not merely pattern-free: the guard must not be
# satisfiable by deleting the log line instead of fixing it.
SHAPES = [
    ("main.cpp", '"[chat] map: opening the node\'s position\\n"'),
    ("main.cpp", '"[nodes] map: opening the node\'s position\\n"'),
    ("main.cpp", '"[chat] DM refused: unknown contact\\n"'),
    ("main.cpp", '"[chat] reply opened: seq %lu\\n"'),
    ("main.cpp", '"[chat] mention prepended: %u B\\n"'),
    ("main.cpp", '"[mc] UI DM send: %s, %u B\\n"'),
    ("main.cpp", '"[mc] UI send: %s, %u B%s\\n"'),
    ("main.cpp", '"[chat] DM resend %s, %u B\\n"'),
    ("main.cpp", '"[chat] resend %s, %u B\\n"'),
    ("main.cpp", '"[nodes] info %d\\n"'),
    ("main.cpp", '"[nodes] menu %d, %d row(s)\\n"'),
    ("main.cpp", '"[nodes] select %d\\n"'),
    ("main.cpp", '"[set] chat picked: %d of %d (chat screen still on %d)\\n"'),
    ("main.cpp", '"[hist] no older messages (chat %d, %s)\\n"'),
    ("screens/rptr_tab.cpp", '"[rptr] auto-login started (found %d)\\n"'),
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


def argument_expression(stmt):
    """The call's arguments with every string literal and every length removed.

    The format string is one of the arguments, so it has to go: a format that says "name and address
    not printed" is not a printed name. `strlen(x)` / `sizeof(x)` are stripped too, because a LENGTH is
    the fix, not the leak.
    """
    args = stmt[stmt.find("(") + 1:]
    args = re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', args)
    args = re.sub(r"\b(?:strlen|sizeof)\s*\([^()]*\)", "", args)
    return args


def whitelisted(rel, stmt):
    for wpath, needle, _why in WHITELIST:
        if wpath == rel and needle in stmt:
            return True
    return False


def arg_exempt(args):
    """The one expression this line is allowed to print, with its reason (None when none applies)."""
    for pattern, why in ARG_EXEMPT:
        if re.search(pattern, args):
            return why
    return None


def leaks(fmt, args):
    """Every rule this statement breaks (empty when it is clean)."""
    bad = []
    exempt = arg_exempt(args)   # narrows the three "%s fed by" rules only, never the field rules
    if FLOAT.search(fmt) and COORD_NAME.search(args):
        bad.append("a float format beside a coordinate name")
    if IP_FIELD.search(fmt):
        bad.append("an ip= field")
    if "%s" in fmt and TEXT_ARG.search(args) and exempt is None:
        bad.append("a %s fed by a message text")
    if "%s" in fmt and NAME_ARG.search(args) and exempt is None:
        bad.append("a %s fed by a name")
    if "%s" in fmt and SSID_ARG.search(args) and exempt is None:
        bad.append("a %s fed by an SSID")
    if SSID_FIELD.search(fmt):
        bad.append("an ssid= field")
    return bad


def check_file(path, root):
    rel = path.relative_to(root).as_posix()
    text = path.read_text(encoding="utf-8", errors="replace")
    count = 0
    for _pos, line, stmt in statements(text):
        count += 1
        fmt = " ".join(format_literals(stmt))
        bad = leaks(fmt, argument_expression(stmt))
        if not bad or whitelisted(rel, stmt):
            continue
        for what in bad:
            fail(f"{rel}:{line}: log carries {what}: {stmt.splitlines()[0].strip()}")
    return count


def self_test():
    """The matchers catch every shape this repo has shipped, and none of the fixes."""
    for stmt, expected in NEGATIVE_CONTROLS:
        bad = leaks(" ".join(format_literals(stmt)), argument_expression(stmt))
        if expected not in bad:
            fail(f"negative control not caught: {expected} <- {stmt}")
    clean = [
        'Serial.printf("[chat] DM resend %s, %u B\\n", sent ? "ok" : "failed", (unsigned)strlen(m.text));',
        'Serial.printf("[chat] map: opening the node\'s position\\n");',
        'Serial.printf("[wifi] connected idx=%d rssi=%d (name and address not printed)\\n", idx, rssi);',
        'Serial.printf("[nodes] info %d\\n", node);',
        'Serial.printf("[time] %s -> %lu s (%s)\\n", om::timeSourceName(gpsSource), a, b);',
        'Serial.printf("[nodes] menu %d, %d row(s)\\n", node, n);',
    ]
    for stmt in clean:
        bad = leaks(" ".join(format_literals(stmt)), argument_expression(stmt))
        if bad:
            fail(f"false positive on a fixed line: {bad} <- {stmt}")


def main(argv):
    root = pathlib.Path(argv[1]).resolve() if len(argv) > 1 else ROOT
    scan_only = len(argv) > 1
    files = sorted(list((root / "src").rglob("*.cpp")) + list((root / "src").rglob("*.h")))
    if len(files) < 60:
        fail(f"only {len(files)} source files found; the scan is not looking at the tree")
    self_test()
    if not scan_only:
        # A whitelist entry that no longer matches anything is stale: it would quietly turn into a
        # blanket exemption later, so it fails here.
        for wpath, needle, _why in WHITELIST:
            text = (root / wpath).read_text(encoding="utf-8", errors="replace")
            if needle not in text:
                fail(f"whitelist entry no longer matches anything in {wpath}: {needle}")
    total = 0
    for path in files:
        total += check_file(path, root)
    # Positive control: the scan must SEE the log lines, otherwise "clean" means nothing.
    if total < 200:
        fail(f"only {total} Serial statements scanned; the matcher is broken")

    if not scan_only:
        src = root / "src"
        for rel, needle in SHAPES:
            if needle not in (src / rel).read_text(encoding="utf-8"):
                fail(f"src/{rel}: the rewritten log line is missing: {needle}")
        # The EST-397 rewrite of the mesh/wifi/map lines must still be in place.
        mesh = (src / "mesh_link.cpp").read_text(encoding="utf-8")
        for needle in ('"[mc] DM seq %lu: %u B (hops %d snr %.2f)\\n"',
                       '"[mc] channel %d: %u B (hops %d snr %.2f)%s\\n"',
                       '"[ble] phone sent ch%d: %u B\\n"',
                       '"[ble] phone sent DM seq %lu: %u B\\n"'):
            if needle not in mesh:
                fail(f"mesh_link.cpp: the EST-397 log line is missing: {needle}")
        wifi = (src / "wifi_link.cpp").read_text(encoding="utf-8")
        for needle in ('"[wifi] saved index %d (%d of %d)\\n"',
                       '"[wifi] connected idx=%d rssi=%d internal=%u largest=%u (name and address not printed)\\n"',
                       '"[wifi] connecting idx=%d%s\\n"',
                       '"[wifi] forgot index %d (%d left)\\n"'):
            if needle not in wifi:
                fail(f"wifi_link.cpp: the EST-397 log line is missing: {needle}")
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

    where = "scan only" if scan_only else "scan + shapes"
    if FAIL:
        print(f"{FAIL} est397 source check(s) FAILED ({where}, root {root})")
        return 1
    print(f"est397: {total} Serial statements in {len(files)} files carry no position, SSID, LAN address, "
          f"message text or name ({len(WHITELIST)} whitelisted, each proven safe; "
          f"{len(NEGATIVE_CONTROLS)} negative controls) - {where}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
