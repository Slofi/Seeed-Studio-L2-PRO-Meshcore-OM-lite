#!/usr/bin/env python3
"""Render the unread badge preview: 1:1 and 8x, for counts 1, 10, 99 and "99+".

The digits come from the SAME table the firmware draws (src/ui/badge_font.h), and the geometry from
theme.h, so this contact sheet cannot drift from the device. The circle/pill edge is the only thing
approximated: LovyanGFX blends the analytic rim (fillSmoothCircle / fillSmoothRoundRect), this script
composites a 16x supersampled shape with the same outer radius, which gives the same one pixel rim.

What it shows, per count: the Chat tab exactly as the bar draws it (the gold 2 px selected line, the
icon, the label) with the badge on the icon's top-right corner, and the badge itself at 8x
(nearest neighbour, so every pixel of the pixel face is visible).

Usage:
  tools/mkbadge.py                       # writes docs/shots/badge-preview.png
  tools/mkbadge.py --out /tmp/badge.png
"""
import argparse
import os
import re

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src")
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
SUPERSAMPLE = 16

# --- theme.h ------------------------------------------------------------------------------------
COLOURS = {
    "kSelBg": 0x1F1A10,
    "kBar": 0x171614,
    "kAcc": 0xE8B04F,
    "kBad": 0xFF5C5C,
    "kWhite": 0xFFFFFF,
    "kInk": 0xE8EDF5,
    "kDim": 0x93A0B4,
}


def parse_int_consts(path):
    """Every `constexpr int16_t NAME = <int expression>;` in a header, evaluated in order."""
    text = open(path, encoding="utf8").read()
    values = {}
    for name, expr in re.findall(r"constexpr\s+int16_t\s+(\w+)\s*=\s*([^;]+);", text):
        expr = expr.split("//")[0].strip()
        try:
            values[name] = int(eval(expr, {"__builtins__": {}}, dict(values)))  # noqa: S307
        except Exception:
            continue
    return values


def parse_badge_face(path):
    """{char: [7 rows]} from src/ui/badge_font.h."""
    text = open(path, encoding="utf8").read()
    face = {}
    for ch, rows in re.findall(r"\{'(.)',\s*\{([^}]*)\}\}", text):
        face[ch] = [int(v, 2) for v in re.findall(r"0b([01]+)", rows)]
    return face


def rgb(v):
    return ((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)


def smooth_shape(w, h, r):
    """Coverage of a rounded rectangle w x h with corner radius r, 16x supersampled."""
    S = SUPERSAMPLE
    big = Image.new("L", (w * S, h * S), 0)
    d = ImageDraw.Draw(big)
    d.rounded_rectangle((0, 0, w * S - 1, h * S - 1), radius=r * S, fill=255)
    return big.resize((w, h), Image.BOX)


def composite(img, box, shape, colour):
    """Composite `colour` through the coverage in `shape` at box=(x, y)."""
    x, y = box
    w, h = shape.size
    layer = Image.new("RGB", (w, h), rgb(colour))
    img.paste(layer, (x, y), shape)


def badge_text(count):
    """The same rule as src/ui/badge_font.h badgeText(): the exact count to 99, then "99+"."""
    if count < 1:
        return ""
    if count <= 99:
        return str(count)
    return "99+"


def draw_badge(img, tab_x, tab_y, count, g):
    """The same order the widget draws in: ring first, then the fill, then the pixel digits."""
    chars = badge_text(count)
    if len(chars) <= 1:
        w = g["kBadgeD"]
    elif len(chars) == 2:
        w = g["kBadgePillW"]
    else:
        w = g["kBadgePill3W"]
    h = g["kBadgeD"]
    cx = tab_x + g["kTabW"] // 2 + g["kBadgeCxOfs"]
    cy = tab_y + g["kBadgeTop"] + h // 2
    x, y = cx - w // 2, cy - h // 2
    r = h // 2
    ring = g["kBadgeRing"]
    composite(img, (x - ring, y - ring), smooth_shape(w + 2 * ring, h + 2 * ring, r + ring),
              COLOURS["kBar"])
    composite(img, (x, y), smooth_shape(w, h, r), COLOURS["kBad"])

    tw = len(chars) * g["kBadgeGlyphW"] + (len(chars) - 1) * g["kBadgeGlyphGap"]
    gx = cx - tw // 2
    gy = cy - g["kBadgeGlyphH"] // 2
    d = ImageDraw.Draw(img)
    for ch in chars:
        rows = g["face"].get(ch)
        if rows:
            for ry, bits in enumerate(rows):
                for rx in range(g["kBadgeGlyphW"]):
                    if bits & (1 << (g["kBadgeGlyphW"] - 1 - rx)):
                        d.point((gx + rx, gy + ry), fill=rgb(COLOURS["kWhite"]))
        gx += g["kBadgeGlyphW"] + g["kBadgeGlyphGap"]
    return (x, y, w, h)


def draw_tab(img, ox, oy, g, label_font):
    """A Chat tab at 1:1: selected background, the gold line, the icon, the label."""
    w, h = g["kTabW"], g["kTabH"]
    d = ImageDraw.Draw(img)
    d.rectangle((ox, oy, ox + w - 1, oy + h - 1), fill=rgb(COLOURS["kSelBg"]))
    for i in range(g["kTabTopLine"]):
        d.line((ox, oy + i, ox + w - 1, oy + i), fill=rgb(COLOURS["kAcc"]))
    cx = ox + w // 2
    cy = oy + g["kTabPadY"] + g["kTabIcon"] // 2
    acc = rgb(COLOURS["kAcc"])
    d.rectangle((cx - 5, cy - 4, cx + 5, cy + 4), outline=acc)
    d.line((cx - 5, cy - 4, cx, cy + 1), fill=acc)
    d.line((cx + 5, cy - 4, cx, cy + 1), fill=acc)
    d.text((cx, oy + h - g["kTabLabelPadB"]), "Chat", font=label_font, anchor="mm", fill=acc)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(HERE, "..", "docs", "shots", "badge-preview.png"))
    args = ap.parse_args()

    theme = parse_int_consts(os.path.join(SRC, "ui", "theme.h"))
    face = parse_badge_face(os.path.join(SRC, "ui", "badge_font.h"))
    face_header = open(os.path.join(SRC, "ui", "badge_font.h"), encoding="utf8").read()
    g = {
        "face": face,
        "kBadgeGlyphW": int(re.search(r"kBadgeGlyphW\s*=\s*(\d+)", face_header).group(1)),
        "kBadgeGlyphH": int(re.search(r"kBadgeGlyphH\s*=\s*(\d+)", face_header).group(1)),
        "kBadgeGlyphGap": int(re.search(r"kBadgeGlyphGap\s*=\s*(\d+)", face_header).group(1)),
    }
    g["kTabW"] = theme.get("kScreenW", 320) // 4      # four tabs split the bar
    for key in ("kTabH", "kTabTopLine", "kTabPadY", "kTabIcon", "kTabLabelPadB",
                "kBadgeD", "kBadgePillW", "kBadgePill3W", "kBadgeRing", "kBadgeTop",
                "kBadgeCxOfs"):
        g[key] = theme[key]

    label_font = ImageFont.truetype(FONT, 9)
    title_font = ImageFont.truetype(FONT, 13)
    cap_font = ImageFont.truetype(FONT, 10)

    counts = [1, 10, 99, 120]   # 120 draws as "99+" (EST-364: the badge counts to 99)
    zoom = 8
    tab_w, tab_h = g["kTabW"], g["kTabH"]
    pad = 10
    # one column holds the 1:1 tab AND the 8x crop (the pill plus a 10 px margin each side)
    col_w = max(tab_w, (g["kBadgePill3W"] + 20) * zoom) + 16
    width = pad * 2 + col_w * len(counts)
    ones = Image.new("RGB", (width, 320), rgb(COLOURS["kBar"]))
    d = ImageDraw.Draw(ones)
    d.text((pad, 6), "OM Lite unread badge, 1:1 then 8x", font=title_font, fill=rgb(COLOURS["kInk"]))
    d.text((pad, 24), "pixel digits, 13 px circle / 19x13 pill / 23x13 for 99+, 1 px ring, clear of the gold line",
           font=cap_font, fill=rgb(COLOURS["kDim"]))

    y1 = 44
    for i, count in enumerate(counts):
        ox = pad + i * col_w + (col_w - tab_w) // 2
        draw_tab(ones, ox, y1, g, label_font)
        box = draw_badge(ones, ox, y1, count, g)
        d.text((pad + i * col_w + col_w // 2, y1 + tab_h + 4),
               "%d -> %s" % (count, badge_text(count)),
               font=cap_font, anchor="ma", fill=rgb(COLOURS["kDim"]))
        # 8x of the badge, cropped from the 1:1 tab so every pixel is the device's
        # the whole tab height and a margin either side: the gold line above the badge and the
        # label below it are both in the 8x, so their clearance can be judged
        crop = (box[0] - 10, y1, box[0] + box[2] + 10, y1 + tab_h)
        crop = tuple(max(0, min(v, s - 1)) for v, s in
                     zip(crop, (ones.width, ones.height, ones.width, ones.height)))
        big = ones.crop(crop).resize(((crop[2] - crop[0]) * zoom, (crop[3] - crop[1]) * zoom),
                                      Image.NEAREST)
        bx = pad + i * col_w + (col_w - big.width) // 2
        by = y1 + tab_h + 22
        ones.paste(big, (bx, by))
        d.rectangle((bx - 1, by - 1, bx + big.width, by + big.height), outline=rgb(0x333230))

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    ones.save(out)
    print("%s: %dx%d (%d counts, %dx zoom)" % (out, ones.width, ones.height, len(counts), zoom))


if __name__ == "__main__":
    main()
