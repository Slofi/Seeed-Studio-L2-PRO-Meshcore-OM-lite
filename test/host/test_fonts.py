#!/usr/bin/env python3
"""EST-346: every VLW face carries the letters the ticket needs.

The four src/ui/font_*.cpp blobs are generated data, and the whole point of the ticket is that the
DATA changed: U+00A0..U+017F (Latin-1 Supplement + Latin Extended-A) was added to every face. This
test parses the generated C arrays exactly as written, walks the VLW header and the 28-byte glyph
records, and fails if a face is missing a code point, out of order, or has a broken bitmap length.
It also checks that the letters the ticket names have a real bitmap (not just an advance), so a
"glyph" that would draw nothing cannot pass.

No device, no LovyanGFX: python3 only.
"""
import re
import struct
import sys

FACES = [
    ("../../src/ui/font_sans9.cpp", "kSans9Data"),
    ("../../src/ui/font_sans11.cpp", "kSans11Data"),
    ("../../src/ui/font_sans14.cpp", "kSans14Data"),
    ("../../src/ui/font_mono10.cpp", "kMono10Data"),
]

FIRST, LAST = 0x20, 0x7E
LATIN_FIRST, LATIN_LAST = 0xA0, 0x17F
# The ticket's proof set, at both cases (č š ž ć đ Č Š Ž), plus German and Italian marks.
NAMED = [0x010D, 0x0161, 0x017E, 0x0107, 0x0111, 0x010C, 0x0160, 0x017D,
         0x00E4, 0x00F6, 0x00FC, 0x00DF, 0x00E8, 0x00F2, 0x00B7]

fails = 0


def check(cond, msg):
    global fails
    if not cond:
        print("FAIL " + msg)
        fails += 1


def load(path, name):
    text = open(path, encoding="utf-8").read()
    m = re.search(r"const uint8_t %s\[\] PROGMEM = \{(.*?)\};" % name, text, re.S)
    if not m:
        print("FAIL %s: no %s array" % (path, name))
        return None
    body = m.group(1)
    return bytes(int(tok, 16) for tok in re.findall(r"0x([0-9a-fA-F]{2})", body))


def test_face(path, name):
    blob = load(path, name)
    if blob is None:
        return
    check(len(blob) >= 24, "%s: blob shorter than the VLW header" % path)
    count, version, y_advance, _unused, ascent, descent = struct.unpack(">IIIIII", blob[:24])
    check(version == 3, "%s: VLW encoder version %d, expected 3" % (path, version))
    check(y_advance == ascent + descent, "%s: yAdvance is not ascent+descent" % path)
    records_end = 24 + 28 * count
    check(records_end <= len(blob), "%s: glyph count runs past the blob" % path)
    codes = []
    advances = {}
    bitmaps = 0
    named_bitmap = {}
    for i in range(count):
        off = 24 + 28 * i
        code, h, w, adv, dy, dx, pad = struct.unpack(">IIIIiiI", blob[off:off + 28])
        codes.append(code)
        advances[code] = adv
        check(pad == 0, "%s: U+%04X has nonzero padding" % (path, code))
        check(w <= 255 and h <= 255, "%s: U+%04X does not fit the byte fields" % (path, code))
        check(adv >= 1, "%s: U+%04X has a zero advance" % (path, code))
        if code in NAMED:
            named_bitmap[code] = (w * h) > 0
        bitmaps += w * h
    check(codes == sorted(codes), "%s: glyph records are not sorted by code point" % path)
    check(len(set(codes)) == len(codes), "%s: duplicate code points" % path)
    if path.endswith("font_mono10.cpp"):
        # EST-347: the settings tab labels are mono, so their width comes from this advance.
        # The firmware's kSetMonoAdvance is 6; every ASCII glyph must agree and the five labels
        # (Longest: General) must fit one of the five 64 px tabs with their 5 px padding.
        ascii_adv = sorted(set(advances[c] for c in range(FIRST, LAST + 1)))
        check(ascii_adv == [6], "%s: mono ASCII advance is %s, expected [6]" % (path, ascii_adv))
        for label in ("General", "Nodes", "Chat", "Map", "RPTR"):
            w = sum(advances[ord(ch)] for ch in label)
            check(w + 2 * 5 <= 320 // 5,
                  "%s: settings tab label %r is %d px, does not fit 64 px" % (path, label, w))
    expect = list(range(FIRST, LAST + 1)) + list(range(LATIN_FIRST, LATIN_LAST + 1))
    check(codes == expect, "%s: glyph set is not ASCII 0x20-0x7E + U+00A0..U+017F "
                           "(missing %s, extra %s)" %
          (path,
           ", ".join("U+%04X" % c for c in sorted(set(expect) - set(codes))[:8]) or "-",
           ", ".join("U+%04X" % c for c in sorted(set(codes) - set(expect))[:8]) or "-"))
    check(len(blob) == records_end + bitmaps,
          "%s: blob is %d bytes, records+bitmaps say %d" % (path, len(blob), records_end + bitmaps))
    for cp in NAMED:
        check(named_bitmap.get(cp) is True, "%s: U+%04X has no bitmap" % (path, cp))
    print("%-28s %6d B  %3d glyphs" % (path, len(blob), count))


def main():
    for path, name in FACES:
        test_face(path, name)
    if fails:
        print("%d font check(s) failed" % fails)
        return 1
    print("fonts: all four faces carry ASCII + U+00A0..U+017F")
    return 0


if __name__ == "__main__":
    sys.exit(main())
