#!/usr/bin/env python3
"""Render the OM Lite icon masks and the C++ table the widgets blend over the frame.

The status bar's gear is a VECTOR gear (13x13, 8 teeth) and Bluetooth is the rune drawn as one
polyline, 13 px tall (9 px wide). The do-not-disturb moon and the radio-fault triangle (both 11x11,
EST-337) are vector shapes too. All are rendered 16x supersampled and reduced with LANCZOS to an
8-bit alpha mask at the exact on-screen size (Claude's shapes, chosen 2026-11-10; the old U+2699
glyph read as a ring with a dot and the old Bluetooth mask as a diamond). Wi-Fi stays the simple
three-arc shape for its later tab.

Usage:
  tools/mkicons.py                 # writes src/ui/icons.cpp/.h and docs/shots/icons-preview.png
  tools/mkicons.py --out-dir /tmp/x --preview /tmp/icons.png

Every pixel is 0..255 coverage; widgets/drawIcon() blends the requested colour over the background
with it. The preview is a contact sheet: each icon at 1:1 and at 8x (nearest neighbour), so the glyph
can be judged before flashing.
"""
import argparse
import math
import os

from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
SUPERSAMPLE = 16

# name, kind, screen size (px, the tall side; the Bluetooth rune is 9 px wide and 13 px tall)
ICONS = [
    ("Gear", "gear", 13),
    ("Wifi", "wifi", 11),
    ("Bluetooth", "bluetooth", 13),
    ("Dnd", "dnd", 11),
    ("Fault", "fault", 11),
    # EST-368: the Map's Traces toggle: a route seen from above, a line through three hops with the
    # moving packet as a dot at the end. 13 px, the same box as the gear and the lock.
    ("Trace", "trace", 13),
]


def fit(img, size):
    """Scale an L canvas to fit `size`, preserving aspect, and centre it on a size x size canvas."""
    w, h = img.size
    if w <= 0 or h <= 0:
        return Image.new("L", (size, size), 0)
    if w >= h:
        nw, nh = size, max(1, int(round(h * size / w)))
    else:
        nh, nw = size, max(1, int(round(w * size / h)))
    small = img.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("L", (size, size), 0)
    canvas.paste(small, ((size - nw) // 2, (size - nh) // 2))
    return canvas


def gear_mask(px=13, teeth=8):
    """Vector gear: one polygon of 4 points per tooth, a disc, and a hole for the hub."""
    S = SUPERSAMPLE
    N = px * S
    img = Image.new("L", (N, N), 0)
    d = ImageDraw.Draw(img)
    c = N / 2
    ro = N * 0.5
    ri = N * 0.36
    rh = N * 0.16
    tw = math.pi / teeth * 0.55
    pts = []
    for i in range(teeth):
        a = 2 * math.pi * i / teeth
        for r, k in ((ri, -1.3), (ro, -0.8), (ro, 0.8), (ri, 1.3)):
            ang = a + tw * k
            pts.append((c + r * math.cos(ang), c + r * math.sin(ang)))
    d.polygon(pts, fill=255)
    d.ellipse((c - ri, c - ri, c + ri, c + ri), fill=255)
    d.ellipse((c - rh, c - rh, c + rh, c + rh), fill=0)
    return img.resize((px, px), Image.LANCZOS)


def bluetooth_mask(px=13):
    """The rune as ONE polyline: (x0,q1) (x1,q3) (xm,y1) (xm,y0) (x1,q1) (x0,q3)."""
    S = SUPERSAMPLE
    W = int(px * 0.7) * S
    H = px * S
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    x0 = W * 0.18
    x1 = W * 0.82
    xm = W * 0.5
    y0 = H * 0.05
    y1 = H * 0.95
    q1 = H * 0.28
    q3 = H * 0.72
    d.line([(x0, q1), (x1, q3), (xm, y1), (xm, y0), (x1, q1), (x0, q3)],
           fill=255, width=int(S * 1.3), joint="curve")
    return img.resize((W // S, px), Image.LANCZOS)


def wifi_mask(size):
    work = size * SUPERSAMPLE
    canvas = Image.new("L", (work, work), 0)
    d = ImageDraw.Draw(canvas)
    cx = work // 2
    base = int(work * 0.88)
    dot_r = max(1, int(round(work * 0.055)))
    stroke = max(1, int(round(work * 0.065)))
    d.ellipse((cx - dot_r, base - dot_r, cx + dot_r, base + dot_r), fill=255)
    for frac in (0.28, 0.49, 0.70):
        r = int(round(work * frac))
        d.arc((cx - r, base - r, cx + r, base + r), start=225, end=315, fill=255, width=stroke)
    return fit(canvas, size)


def dnd_mask(size=11):
    """Do-not-disturb: a crescent moon. A filled disc with an offset disc erased, so the moon's
    opening faces up-right and the lit part is a clean crescent at 11 px."""
    S = SUPERSAMPLE
    N = size * S
    img = Image.new("L", (N, N), 0)
    d = ImageDraw.Draw(img)
    c = N / 2
    r = N * 0.47
    d.ellipse((c - r, c - r, c + r, c + r), fill=255)
    ox = N * 0.28
    oy = -N * 0.16
    rr = N * 0.42
    d.ellipse((c + ox - rr, c + oy - rr, c + ox + rr, c + oy + rr), fill=0)
    return img.resize((size, size), Image.LANCZOS)


def fault_mask(size=11):
    """Radio fault: a filled warning triangle with the exclamation cut out of it. Small and solid so
    it stays legible in kBad at 11 px; the cut-out keeps it from reading as a plain blob."""
    S = SUPERSAMPLE
    N = size * S
    img = Image.new("L", (N, N), 0)
    d = ImageDraw.Draw(img)
    d.polygon([(N * 0.5, N * 0.06), (N * 0.96, N * 0.94), (N * 0.04, N * 0.94)], fill=255)
    stroke = max(1, int(round(N * 0.11)))
    cx = N * 0.5
    d.rectangle((cx - stroke / 2, N * 0.32, cx + stroke / 2, N * 0.58), fill=0)
    dot_r = max(1, int(round(N * 0.075)))
    d.ellipse((cx - dot_r, N * 0.735 - dot_r, cx + dot_r, N * 0.735 + dot_r), fill=0)
    return img.resize((size, size), Image.LANCZOS)


def trace_mask(size=13):
    """EST-368: Traces. A route seen from above: one polyline through two relay points, drawn as a
    thin line with a filled dot at every hop and a bigger dot for the moving packet at the end, so at
    13 px it reads as a path, not as a scribble."""
    S = SUPERSAMPLE
    N = size * S
    img = Image.new("L", (N, N), 0)
    d = ImageDraw.Draw(img)
    pts = [(N * 0.10, N * 0.80), (N * 0.42, N * 0.52), (N * 0.66, N * 0.66), (N * 0.90, N * 0.20)]
    d.line(pts, fill=255, width=max(1, int(round(S * 1.15))), joint="curve")
    hop_r = N * 0.085
    for (x, y) in pts[1:-1]:
        d.ellipse((x - hop_r, y - hop_r, x + hop_r, y + hop_r), fill=255)
    end_r = N * 0.13
    ex, ey = pts[-1]
    d.ellipse((ex - end_r, ey - end_r, ex + end_r, ey + end_r), fill=255)
    return img.resize((size, size), Image.LANCZOS)


def clean(mask):
    """Drop the faint LANCZOS dust and stretch the remaining coverage to full 0..255."""
    px = mask.load()
    w, h = mask.size
    raw_peak = 0
    for y in range(h):
        for x in range(w):
            if px[x, y] > raw_peak:
                raw_peak = px[x, y]
    if raw_peak <= 8:
        return mask, 0
    scale = 255.0 / (raw_peak - 8)
    for y in range(h):
        for x in range(w):
            a = px[x, y]
            px[x, y] = 0 if a < 8 else min(255, int(round((a - 8) * scale)))
    return mask, 255


def c_array(name, mask):
    w, h = mask.size
    data = mask.tobytes()
    lines = ["const uint8_t %s[] PROGMEM = {" % name]
    for y in range(h):
        row = data[y * w:(y + 1) * w]
        lines.append("  " + ", ".join("0x%02x" % b for b in row) + ",")
    lines.append("};")
    return "\n".join(lines)


def write_cpp(icons, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    h_lines = [
        "// Generated by tools/mkicons.py - do not edit by hand.",
        "// 8-bit alpha masks; widgets draw them by blending a colour over the background.",
        "// SPDX-License-Identifier: MIT (generated data, vector shapes drawn by our own tool).",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "namespace om {",
        "namespace ui {",
        "",
        "struct IconMask {",
        "  uint8_t w;",
        "  uint8_t h;",
        "  const uint8_t* alpha;",
        "};",
        "",
    ]
    c_lines = [
        "// Generated by tools/mkicons.py - do not edit by hand.",
        '#include "ui/icons.h"',
        "",
        "#include <Arduino.h>",
        "",
        "namespace om {",
        "namespace ui {",
        "",
    ]
    for name, _kind, _size in icons:
        ident = "k" + name + "Mask"
        mask = _kind[1]
        h_lines.append("extern const IconMask kIcon%s;" % name)
        c_lines.append(c_array(ident, mask))
        c_lines.append("const IconMask kIcon%s PROGMEM = {%d, %d, %s};"
                       % (name, mask.size[0], mask.size[1], ident))
        c_lines.append("")
    h_lines += ["}  // namespace ui", "}  // namespace om", ""]
    c_lines += ["}  // namespace ui", "}  // namespace om", ""]
    with open(os.path.join(out_dir, "icons.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(h_lines))
    with open(os.path.join(out_dir, "icons.cpp"), "w", encoding="utf-8") as f:
        f.write("\n".join(c_lines))


def write_preview(icons, path):
    bg = (0x17, 0x16, 0x14)
    fg = (0x93, 0xa0, 0xb4)
    ink = (0xe8, 0xed, 0xf5)
    zoom = 8
    label_font = ImageFont.truetype(FONT, 12)
    margin = 12
    row_h = 8 * 15 + 34
    width = 460
    height = margin * 2 + row_h * len(icons)
    sheet = Image.new("RGB", (width, height), bg)
    d = ImageDraw.Draw(sheet)
    y = margin
    for name, kind, _size in icons:
        mask = kind[1]
        w, h = mask.size
        d.text((margin, y), "%s  %dx%d" % (name, w, h), font=label_font, fill=ink)
        d.text((margin, y + 18), "1:1", font=label_font, fill=fg)
        one = Image.new("RGB", (w, h), bg)
        one_px = one.load()
        mpx = mask.load()
        for yy in range(h):
            for xx in range(w):
                a = mpx[xx, yy]
                if a:
                    one_px[xx, yy] = tuple(
                        (fg[i] * a + bg[i] * (255 - a)) // 255 for i in range(3))
        sheet.paste(one, (margin + 44, y + 12))
        d.text((margin + 84, y + 18), "8x", font=label_font, fill=fg)
        big = mask.resize((w * zoom, h * zoom), Image.NEAREST)
        big_rgb = Image.new("RGB", big.size, bg)
        bpx = big_rgb.load()
        bmpx = big.load()
        for yy in range(big.size[1]):
            for xx in range(big.size[0]):
                a = bmpx[xx, yy]
                if a:
                    bpx[xx, yy] = tuple(
                        (fg[i] * a + bg[i] * (255 - a)) // 255 for i in range(3))
        sheet.paste(big_rgb, (margin + 118, y + 6))
        y += row_h
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    sheet.save(path)
    print("preview %s" % path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=os.path.join(os.path.dirname(__file__), "..", "src", "ui"))
    ap.add_argument("--preview", default=os.path.join(
        os.path.dirname(__file__), "..", "docs", "shots", "icons-preview.png"))
    args = ap.parse_args()

    icons = []
    for name, kind, size in ICONS:
        if kind == "gear":
            mask = gear_mask(size)
        elif kind == "wifi":
            mask = wifi_mask(size)
        elif kind == "bluetooth":
            mask = bluetooth_mask(size)
        elif kind == "dnd":
            mask = dnd_mask(size)
        elif kind == "fault":
            mask = fault_mask(size)
        elif kind == "trace":
            mask = trace_mask(size)
        else:
            raise SystemExit("unknown icon kind %s" % kind)
        mask, peak = clean(mask)
        nonzero = sum(1 for b in mask.tobytes() if b)
        print("%-9s %2dx%-2d  %3d nonzero, peak %d" % (name, mask.size[0], mask.size[1], nonzero,
                                                       peak))
        icons.append((name, (kind, mask), size))

    write_cpp(icons, os.path.abspath(args.out_dir))
    write_preview(icons, os.path.abspath(args.preview))


if __name__ == "__main__":
    main()
