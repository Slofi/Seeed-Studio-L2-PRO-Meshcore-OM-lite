#!/usr/bin/env python3
"""Host test for EST-388: the parts of `https probe` that a C++ test cannot see.

1. Certificate validation stays ON in the probe path: the device hands the pinned CA buffer to
   WiFiClientSecure::connect(), which sets MBEDTLS_SSL_VERIFY_REQUIRED, and nothing in wifi_link.cpp
   calls setInsecure() or switches to the built-in bundle. WHICH roots that buffer holds, and that
   they are exactly ISRG Root X1 and X2, is EST-389's test (test_est389.py) since the buffer now
   carries two PEMs.
2. The report line still carries every number the ticket names (status, bytes, connect / handshake /
   total, internal before / lowest / after, largest before / lowest / after, PSRAM before / after,
   BLE on), so a later refactor cannot quietly drop one of them from the five device runs.
3. The command surface stays closed: `https` has one sub-word (`probe`) and no word can name a host,
   a path or a query, and the start line is not an answer line for tools/cmd.py.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "src")
CA = os.path.join(SRC, "https_probe_ca.h")
MODEL = os.path.join(SRC, "https_probe.h")
LINK = os.path.join(SRC, "wifi_link.cpp")
PARSER = os.path.join(SRC, "usb_cmd_logic.cpp")
COMMANDS = os.path.join(SRC, "usb_commands.cpp")


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def check(cond, message):
    if not cond:
        raise SystemExit("FAIL test_est388: " + message)


def test_validation_stays_on():
    # The buffer's CONTENT is EST-389's test; this only keeps the probe on the validating path.
    link = read(LINK)
    check("kCaPem" in link, "wifi_link.cpp must pass the pinned CA buffer to the client")
    check("setInsecure" not in link,
          "certificate validation must stay ON: no setInsecure() in the probe path")
    check("setCACertBundle" not in link,
          "the pinned CA is the documented choice; a bundle change needs a ticket")


def test_report_fields():
    model = read(MODEL)
    for field in ("status=", "bytes=", "body=", "dns_ms=", "conn_ms=", "tls_ms=", "hs_ms=",
                  "total_ms=", "internal_before=", "internal_min=", "internal_after=",
                  "largest_before=", "largest_min=", "largest_after=", "psram_before=",
                  "psram_after=", "idf_min=", "ble=", "ntp_active="):
        check(field in model, "the report line lost the field %r" % field)
    check('kHost = "game.cl0.eu"' in model, "the host must be the ticket's game server")
    check('kPath = "/api/v1/health"' in model, "the path must be the ticket's health endpoint")
    check("kTimeoutMs = 10000" in model, "the probe's budget must be 10 s")
    check("kErrSslAllocFailed" in model and "isAllocFailure" in model,
          "the out-of-memory result must be classified")


def test_command_surface():
    parser = read(PARSER)
    start = parser.find('strcmp(verb, "https")')
    check(start >= 0, "the parser has no https verb")
    # The block runs to the next verb, so a later `wifi`/`ble` block cannot leak in.
    nxt = parser.find('strcmp(verb, "', start + 10)
    block = parser[start:nxt if nxt > start else len(parser)]
    check('strcmp(sub, "probe")' in block, "the one accepted sub-word is probe")
    check("https://" not in block and "http://" not in block,
          "no word may carry a URL; the target is a firmware constant")
    # Exactly one sub-word: every strcmp on `sub` in this block names probe.
    subs = re.findall(r'strcmp\(sub, "([^"]+)"\)', block)
    check(subs == ["probe"], "https accepts exactly one sub-word, found %r" % (subs,))

    link = read(LINK)
    check("[cmd] ok https probe" in link, "the one answer line must terminate the probe")
    check('httpsprobe::kHost' in link, "the device code must use the model's host")
    # The start line is data for cmd.py, not an answer: it must not begin with "[cmd]".
    commands = read(COMMANDS)
    for line in commands.splitlines():
        stripped = line.strip()
        if stripped.startswith('"') and "https] probe start" in stripped:
            check(not stripped.startswith('"[cmd]'), "the start line must not be an answer line")


def test_rule9():
    # The public repo rule: no coordinate, no secret anywhere the probe prints.
    for path in (CA, MODEL):
        text = read(path).lower()
        for bad in ("latitude", "longitude", "lat=", "lon=", "password", "ssid="):
            check(bad not in text, "rule 9: %r must not appear in %s" % (bad, os.path.basename(path)))


def main():
    test_validation_stays_on()
    test_report_fields()
    test_command_surface()
    test_rule9()
    print("test_est388.py: all checks passed")


if __name__ == "__main__":
    main()
