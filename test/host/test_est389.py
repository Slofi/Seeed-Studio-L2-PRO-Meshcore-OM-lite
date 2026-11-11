#!/usr/bin/env python3
"""Host test for EST-389: the probe's trust anchors are TWO roots, in ONE buffer.

EST-388 pinned ISRG Root X1 only, but game.cl0.eu serves leaf -> YE1 -> Root YE -> ISRG Root X2
(checked from TestBox 2026-11-11 11:43, `openssl s_client -showcerts`, verify 0). With one root the
probe would fail certificate validation and the failure would read like a memory problem. This test
pins the fix down without a device:

1. src/https_probe_ca.h must build exactly ONE CA buffer (`kCaPem`) and it must hold exactly TWO
   certificates, as two adjacent raw string literals (the compiler joins them; mbedTLS parses a
   concatenation of PEMs into one chain). Two separate symbols, or a stray third PEM, fails.
2. Every PEM is decoded here and its SHA-256 must equal the fingerprint written in the file's
   comments, in buffer order, AND the published value for that root (X1 or X2). A right PEM with a
   wrong comment fails, and so does a wrong PEM with a matching comment.
3. Certificate validation stays ON: the probe passes this buffer to WiFiClientSecure, there is no
   setInsecure() in the probe path, and no bundle switch.
4. The report line's `ca=` word names exactly the two roots this buffer holds.
"""
import base64
import hashlib
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "src")
CA = os.path.join(SRC, "https_probe_ca.h")
MODEL = os.path.join(SRC, "https_probe.h")
LINK = os.path.join(SRC, "wifi_link.cpp")
COMMANDS = os.path.join(SRC, "usb_commands.cpp")

# The published SHA-256 of each root's DER. The X2 value was taken from
# /etc/ssl/certs/ISRG_Root_X2.pem on TestBox and matches Claude's s_client check.
PUBLISHED = [
    ("ISRG Root X1",
     "96BCEC06264976F37460779ACF28C5A7CFE8A3C0AAE11A8FFCEE05C0BDDF08C6"),
    ("ISRG Root X2",
     "69729B8E15A86EFC177A57AFB7171DFC64ADD28C2FCA8CF1507E34453CCB1470"),
]
CA_LABEL = "isrg-x1+x2"

BEGIN = "-----BEGIN CERTIFICATE-----"
END = "-----END CERTIFICATE-----"
FINGERPRINT = re.compile(r"(?:[0-9A-F]{2}:){31}[0-9A-F]{2}")


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def check(cond, message):
    if not cond:
        raise SystemExit("FAIL test_est389: " + message)


def test_one_buffer_two_certs():
    text = read(CA)
    check("PRIVATE KEY" not in text, "a private key must never sit in the tree")
    # One buffer symbol only: two separate PEM constants are a regression (the device would again
    # pass just one of them).
    symbols = re.findall(r"constexpr\s+const\s+char\s*\*\s*(\w+)\s*=", text)
    check(symbols == ["kCaPem"], "the file must define exactly one CA buffer, found %r" % (symbols,))

    # The definition runs from `kCaPem =` to its semicolon; inside it every literal is part of the
    # one buffer, so several `R"PEM(` blocks are a concatenation, not a second constant.
    start = text.index("kCaPem")
    stop = text.index(";", start)
    body = text[start:stop]
    check(body.count('R"PEM(') == 2, "the buffer must be two adjacent PEM literals")
    check(body.count(')PEM"') == 2, "the buffer must close both PEM literals")
    # Between the first literal's end and the second's start there may be only whitespace and //.
    between = body.split(')PEM"', 1)[1].split('R"PEM(', 1)[0]
    for line in between.splitlines():
        stripped = line.strip()
        check(stripped == "" or stripped.startswith("//"),
              "only a comment may sit between the two PEMs, found %r" % stripped)

    # Exactly two certificates, in this order, and no PEM outside the buffer.
    blocks = re.findall(re.escape(BEGIN) + r"(.*?)" + re.escape(END), text, re.S)
    check(len(blocks) == 2, "the CA buffer must hold exactly two certificates, found %d" % len(blocks))
    check(text.count(BEGIN) == 2 and text.count(END) == 2,
          "no certificate may sit outside the one buffer")
    return text, blocks


def colon(hexdigest):
    """01AB... -> 01:AB:... (the form the comments and openssl print)."""
    return ":".join(hexdigest[i:i + 2] for i in range(0, len(hexdigest), 2))


def test_fingerprints():
    text, blocks = test_one_buffer_two_certs()
    decoded = []
    for block in blocks:
        der = base64.b64decode("".join(block.split()), validate=True)
        decoded.append(hashlib.sha256(der).hexdigest().upper())

    # 1. The PEMs are the two published roots, in buffer order (X1 then X2).
    check(decoded == [fp for _, fp in PUBLISHED],
          "the two certificates are not X1 then X2: %r" % (decoded,))

    # 2. The comments say the same thing. Every fingerprint in a comment, in file order, must be the
    #    decoded one for that position: a stale comment is a failure, not a note.
    comments = [line for line in text.splitlines() if line.strip().startswith("//")]
    in_comments = [m for line in comments for m in FINGERPRINT.findall(line.upper())]
    check(len(in_comments) == 2, "the file must name two fingerprints in its comments, found %r"
          % (in_comments,))
    check(in_comments == [colon(fp) for fp in decoded],
          "the comment fingerprints do not match the decoded certificates, in order")

    # 3. And both roots are named, next to their fingerprint.
    check("ISRG Root X1" in text and "ISRG Root X2" in text,
          "the header must say which two roots it pins")


def test_validation_stays_on():
    link = read(LINK)
    check("kCaPem" in link, "wifi_link.cpp must pass the two-root buffer to the client")
    check("kIsrgRootX1Pem" not in link, "the single-root symbol must be gone from the probe path")
    check("setInsecure" not in link,
          "certificate validation must stay ON: no setInsecure() in the probe path")
    check("setCACertBundle" not in link,
          "the pinned roots are the documented choice; a bundle change needs a ticket")


def test_report_label():
    model = read(MODEL)
    check('kCaLabel = "%s"' % CA_LABEL in model,
          "the report line's ca= word must be %s" % CA_LABEL)
    # The word names both pinned roots, X1 and X2, and no third one.
    check(re.findall(r"x(\d)", CA_LABEL) == ["1", "2"],
          "the ca= word must name exactly X1 and X2")
    # The one line prints it (the C++ test reads the formatted line itself).
    check('"ca=%s"' in model, "ca= must be a literal field on the one report line")
    check("kCaLabel" in model.split("formatLine", 1)[1],
          "formatLine must print kCaLabel")


def test_start_line_agrees():
    # The command's start line names the anchors too. It must print the same constant: a second
    # literal is how the two lines end up disagreeing after the next change.
    commands = read(COMMANDS)
    check("ca=%s" in commands, "the start line must print the ca= word")
    check("kCaLabel" in commands, "the start line must print kCaLabel, not a literal")
    stale = "isrg-root-x1"
    check(stale not in commands and stale not in read(MODEL),
          "a stale one-root CA word must not survive beside kCaLabel")


def main():
    test_fingerprints()
    test_validation_stays_on()
    test_report_label()
    test_start_line_agrees()
    print("test_est389.py: all checks passed")


if __name__ == "__main__":
    main()
