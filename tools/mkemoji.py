#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Render Noto Color Emoji into the compact blob the L2 Pro draws from flash.

The panel's chat line is 14 px tall, so every emoji is rendered into a 13 x 13 cell
(one px of air below it): RGB565 plus a 4-bit coverage mask.  A cell is 169 px:
169 * 2 = 338 B colour, 13 rows * 7 B = 91 B alpha, padded to 432 B, so the whole
table is about 0.6 MB for the 1,416 single-code-point emoji this installed font has
(the ticket's 1.5 MB estimate assumed ~3,700).

Output src/ui/emoji_data.bin:

    header 16 B   "OMEM", u16 version, u16 cellW, u16 cellH, u16 count, u32 dataOffset
    index         count * (u32 codePoint, u32 dataOffset), sorted by code point
    data          one cell per entry: w*h*2 B little-endian RGB565, then
                  h * ceil(w/2) B of 4-bit alpha (high nibble = left pixel)

The indexed forms are aliases: a code point that needs U+FE0F to be shown as an emoji
glyph is rendered with it, and stored under the base code point.  The draw-time
sequence walker consumes FE0F/FE0E and skin-tone modifiers with their base, so the
table never has to contain a second entry for them.  Flags are not in the table:
regional indicators are drawn as two placeholder boxes (the ticket's rule).

Usage: tools/mkemoji.py [--out src/ui/emoji_data.bin] [--preview file.png]
Requires Pillow and fontTools (build-time only; the firmware needs neither).
"""
import argparse
import os
import struct
import sys

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf"
STRIKE_PX = 109          # the bitmap strike Noto Color Emoji ships
CELL_W = 13
CELL_H = 13
ROW_ALPHA = (CELL_W + 1) // 2
DATA_BYTES = CELL_W * CELL_H * 2 + CELL_H * ROW_ALPHA
PADDED = (DATA_BYTES + 3) & ~3


def wanted(cp):
    """A single drawable emoji code point, not one of the sequence-only code points."""
    if cp < 0xA9 or cp > 0x10FFFF:
        return False
    # ZWJ joins, variation selectors, combining keycap and the tag characters only
    # ever appear inside a sequence.
    if cp in (0x200D, 0x20E3, 0xFE0E, 0xFE0F) or 0xE0000 <= cp <= 0xE007F:
        return False
    if 0x1F3FB <= cp <= 0x1F3FF:      # skin tones: modifiers, drawn with the base
        return False
    if 0x1F1E6 <= cp <= 0x1F1FF:      # regional indicators: the flag rule uses boxes
        return False
    return True


def render(font, cp):
    img = Image.new("RGBA", (STRIKE_PX * 2, STRIKE_PX * 2), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.text((STRIKE_PX // 4, STRIKE_PX // 4), chr(cp), font=font, embedded_color=True)
    bbox = img.getbbox()
    if bbox is None:
        return None
    crop = img.crop(bbox)
    w, h = crop.size
    scale = CELL_H / max(w, h)
    nw = max(1, min(CELL_W, int(round(w * scale))))
    nh = max(1, min(CELL_H, int(round(h * scale))))
    # Premultiplied resize: a straight-alpha LANCZOS leaves dark fringes in the
    # transparent corners, which read as a black outline on the panel.
    crop = crop.convert("RGBa").resize((nw, nh), Image.LANCZOS).convert("RGBA")
    cell = Image.new("RGBA", (CELL_W, CELL_H), (0, 0, 0, 0))
    cell.paste(crop, ((CELL_W - nw) // 2, (CELL_H - nh) // 2))
    return cell


def rgb565(r, g, b):
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def pack(cell):
    px = cell.load()
    out = bytearray()
    for y in range(CELL_H):
        for x in range(CELL_W):
            r, g, b, _ = px[x, y]
            out += struct.pack("<H", rgb565(r, g, b))
    for y in range(CELL_H):
        row = bytearray(ROW_ALPHA)
        for x in range(CELL_W):
            a = px[x, y][3]
            q = (a * 15 + 127) // 255
            if q == 0:
                continue
            if x & 1:
                row[x // 2] |= q
            else:
                row[x // 2] |= q << 4
        out += row
    while len(out) < PADDED:
        out.append(0)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "src", "ui",
                                                  "emoji_data.bin"))
    ap.add_argument("--preview", help="write an 8x nearest-neighbour preview sheet")
    args = ap.parse_args()

    font_file = TTFont(FONT)
    cps = sorted(cp for cp in font_file.getBestCmap() if wanted(cp))
    render_font = ImageFont.truetype(FONT, STRIKE_PX)

    cells = []
    for cp in cps:
        cell = render(render_font, cp)
        if cell is not None:
            cells.append((cp, cell))

    count = len(cells)
    data_off = 16 + 8 * count
    header = struct.pack("<4sHHHHI", b"OMEM", 1, CELL_W, CELL_H, count, data_off)
    index = bytearray()
    body = bytearray()
    for i, (cp, cell) in enumerate(cells):
        index += struct.pack("<II", cp, data_off + i * PADDED)
        body += pack(cell)
    blob = header + bytes(index) + bytes(body)

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(blob)

    print(f"font       {FONT}")
    print(f"cmap       {len(font_file.getBestCmap())} code points")
    print(f"table      {count} emoji, {CELL_W}x{CELL_H} cells")
    print(f"per emoji  {PADDED} B ({DATA_BYTES} used + {PADDED - DATA_BYTES} pad)")
    print(f"index      {8 * count} B")
    print(f"total      {len(blob)} B = {len(blob) / 1024.0:.1f} KiB")
    print(f"written    {out}")

    if args.preview:
        cols = 24
        rows = (count + cols - 1) // cols
        sheet = Image.new("RGB", (cols * (CELL_W + 1), rows * (CELL_H + 1)), (16, 15, 13))
        for i, (_, cell) in enumerate(cells):
            sheet.paste(cell, ((i % cols) * (CELL_W + 1), (i // cols) * (CELL_H + 1)), cell)
        sheet.resize((sheet.width * 8, sheet.height * 8), Image.NEAREST).save(args.preview)
        print(f"preview    {args.preview}")


if __name__ == "__main__":
    sys.exit(main())
