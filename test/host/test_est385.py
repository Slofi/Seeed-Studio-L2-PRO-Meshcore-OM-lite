#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-385: the NTP logs and answers must not print an address of the user's network or an SSID
# (REBUILD.md rule 9, as the ticket states it: no LAN IPs, no SSIDs, no passwords).
#
# Source-level grep, deliberately: the point is the FORMAT STRINGS, not a run. Every literal the NTP
# code and the two new USB answers can print is checked. The one address an NTP log may carry is the
# PUBLIC NTP server's, and it only ever comes from ntpIpText() with a resolved public address; the
# gateway is printed as the word "gateway" and a DNS server only as set/unset.
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FAIL = 0

# What a single format literal may never carry.
BAD_IN_NTP = [
    (re.compile(r"%[0-9]*[ud]\.%[0-9]*[ud]\."), "an address format"),
    (re.compile(r"ssid", re.IGNORECASE), "an SSID"),
    (re.compile(r"bssid", re.IGNORECASE), "a BSSID"),
    (re.compile(r"pass(?:word|wd|phrase)?", re.IGNORECASE), "a password word"),
    (re.compile(r"\bip="), "a station address field"),
    (re.compile(r"\bgw="), "a gateway-address field"),
    (re.compile(r"gateway="), "a gateway-address field"),
]


def fail(msg):
    global FAIL
    print("FAIL", msg)
    FAIL += 1


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def literals(text):
    """Every C string literal in the file, unescaped enough for a token check."""
    return re.findall(r'"((?:[^"\\\n]|\\.)*)"', text)


def ntp_formats(rel):
    """The literals an NTP log or answer can print.

    In wifi_link.cpp every log line starts with "[ntp]", so the literal itself is the unit. The USB
    answer is one statement over several literals, so its whole statement is taken (from the
    `"[cmd] ok ntp` line to the closing `);`), otherwise a continuation literal would escape the check.
    """
    text = read(rel)
    found = [lit for lit in literals(text) if "[ntp]" in lit]
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if '"[cmd] ok ntp' not in line:
            continue
        chunk = []
        for follow in lines[i:]:
            chunk.append(follow)
            if follow.rstrip().endswith(");"):
                break
        found.extend(literals("\n".join(chunk)))
    return found


def enclosing_function(text, pos):
    name = "?"
    for match in re.finditer(r"^(?:[A-Za-z_][A-Za-z0-9_:<>*&\s]*?)\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*\{?\s*$",
                             text[:pos], re.MULTILINE):
        name = match.group(1)
    return name


def check_formats(rel):
    found = ntp_formats(rel)
    for lit in found:
        for rx, what in BAD_IN_NTP:
            if rx.search(lit):
                fail(f"{rel}: NTP format carries {what}: {lit!r}")
    return found


def main():
    wifi = read("src/wifi_link.cpp")
    usb = read("src/usb_commands.cpp")

    found = check_formats("src/wifi_link.cpp") + check_formats("src/usb_commands.cpp")
    # Positive control: the greps must see the real lines, otherwise "clean" means nothing.
    if len(found) < 12:
        fail(f"only {len(found)} NTP format literals found; the check is not looking at the code")

    for needle in ("[ntp] attempt", "[ntp] dns", "[ntp] client", "[ntp] next attempt", "addr=unset"):
        if not any(needle in lit for lit in found):
            fail(f"missing expected NTP log: {needle}")

    # The measurement of the client's own state is what tells a silent tcpip_callback failure apart
    # from a lost packet; keep it in the code and in the answer.
    for needle in ("enabled=%d addr=%s sync=%d notify=%lu reach=%u", "next_in=%lu next_server=%s",
                   "dns_set=%d%d%d"):
        if not any(needle in lit for lit in found):
            fail(f"missing NTP evidence field: {needle}")

    # The address formatters in wifi_link.cpp: ntpIpText() (NTP, public addresses only) and the
    # PRE-EXISTING station line `[wifi] connected ... ip=` in onConnected(), which this ticket does not
    # touch (reported to Claude in PROGRESS.md). No NTP function may grow one.
    positions = [m.start() for m in re.finditer(r"%u\.%u\.%u\.%u", wifi)]
    owners = sorted({enclosing_function(wifi, pos) for pos in positions})
    if owners != ["ntpIpText", "onConnected"]:
        fail(f"unexpected address formatters in wifi_link.cpp: {owners}")
    for src in ("ntpIpText(s_dnsAddr", "ntpIpText(ip,", "ntpIpText(ip4_addr_get_u32(ip_2_ip4(server))"):
        if src not in wifi:
            fail(f"ntpIpText is not fed the expected public source: {src}")
    for bad in ("ntpIpText((uint32_t)WiFi.", "ntpIpText((uint32_t)st.", "ntpIpText((uint32_t)gw"):
        if bad in wifi:
            fail(f"ntpIpText is fed a LAN address: {bad}")

    # A printf line may never format the DNS servers, the gateway, or the station's own address.
    for i, line in enumerate(wifi.splitlines(), 1):
        if "Serial.printf" not in line and "Serial.println" not in line:
            continue
        for call in ("dnsIP(", "gatewayIP(", "localIP("):
            if call in line:
                fail(f"src/wifi_link.cpp:{i}: a log line formats {call}: {line.strip()}")

    # The gateway's printed token is a word with no address in it.
    plan = read("src/ntp_plan.h")
    line = '    {ServerKind::Gateway, "gateway"},'
    if line not in plan:
        fail("ntp_plan.h does not carry the gateway's word token")
    if re.search(r"ServerKind::Gateway,\s*\"[0-9.:]+\"", plan):
        fail("the gateway entry looks like an address")
    # And the two public names + the public IP are exactly the ticket's rotation.
    for token in ('"pool.ntp.org"', '"time.google.com"', '"162.159.200.1"'):
        if token not in plan:
            fail(f"the rotation is missing {token}")

    # The status answer names the schedule, not a network: the last server and the next server are
    # table tokens, and the clock is the source token.
    status = "\n".join(l for l in usb.splitlines() if '"[cmd] ok ntp attempts=' in l
                       or "next_in=%lu next_server=%s" in l)
    if "last=%s" not in status or "next_server=%s" not in status:
        fail("the ntp status answer does not carry last/next server tokens")
    if re.search(r"ssid", status, re.IGNORECASE) or re.search(r"%[0-9]*[ud]\.%[0-9]*[ud]\.", status):
        fail("the ntp status answer carries a network name or an address format")

    if FAIL:
        print(f"{FAIL} est385 source check(s) FAILED")
        return 1
    print(f"est385: {len(found)} NTP log/answer literals carry no LAN address, SSID or password")
    return 0


if __name__ == "__main__":
    sys.exit(main())
