#!/usr/bin/env python3
"""EST-393 preview: the map position picker with its banner on the screen floor and the Map's cross.

Nothing here invents a rectangle or a colour. A tiny C++ dumper (compiled with the system g++) asks
the firmware's own ui/rptr_layout.h for the picker's real rectangles (the banner, the coordinate
line, both keys, the cross box) and prints ui/theme.h's real colours. Python only fills those
rectangles and writes the labels the shell passes in.

The map under the picker is the existing z13 reference image's centre, cropped to the 320x214
canvas: it is a real topo backdrop for the layout, not a fresh device capture, and the picker's own
zoom label still says z15. The banner carries PLACEHOLDER digits ("00.00000, 00.00000"), never a
real coordinate.

Output: docs/shots/2026-11-11-est393-picker.png (2x)
"""
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'docs', 'shots', '2026-11-11-est393-picker.png')
MAP_SRC = os.path.join(ROOT, '..', 'map-preview-ljubljana-z13-2026-11-09.png')

DUMPER = r'''
#include <stdio.h>
#include "ui/map_chrome.h"
#include "ui/rptr_layout.h"
#include "ui/theme.h"
using namespace om::ui;

static void row(const char* name, const Rect& r) {
  printf("%s %d %d %d %d\n", name, r.x, r.y, r.w, r.h);
}
static void col(const char* name, uint16_t c) { printf("%s %u\n", name, (unsigned)c); }

int main() {
  const Layout& L = l2();
  const RptrMapPickLayout m = rptrMapPickLayoutFor(L, true);
  row("status", L.status);
  row("map", m.map);
  row("back", m.back);
  row("zoomIn", m.zoomIn);
  row("zoomOut", m.zoomOut);
  row("panel", m.panel);
  row("coords", m.coords);
  row("useGps", m.useGps);
  row("apply", m.apply);
  row("cross", m.cross);
  printf("radius %d\n", (int)kRadiusSm);
  col("kScreen", kScreen);
  col("kBar", kBar);
  col("kLine", kLine);
  col("kPanel", kPanel);
  col("kPanel2", kPanel2);
  col("kInk", kInk);
  col("kDim", kDim);
  col("kDimmer", kDimmer);
  col("kAcc", kAcc);
  col("kSelBg2", kSelBg2);
  col("kAccInk", kAccInk);
  col("kGreen", kGreen);
  col("kMapBg", kMapBg);
  col("kMapCrossInk", kMapCrossInk);
  return 0;
}
'''


def geometry():
    src = '/tmp/est393_preview_geo.cpp'
    with open(src, 'w') as f:
        f.write(DUMPER)
    cmd = ['g++', '-std=c++17', '-O1', '-I', os.path.join(ROOT, 'src'), src,
           os.path.join(ROOT, 'src', 'ui', 'rptr_layout.cpp'),
           '-o', '/tmp/est393_preview_geo']
    subprocess.run(cmd, check=True)
    out = subprocess.run(['/tmp/est393_preview_geo'], check=True, capture_output=True, text=True)
    rects, cols = {}, {}
    for line in out.stdout.splitlines():
        p = line.split(' ', 1)
        if len(p) != 2:
            continue
        key, rest = p
        parts = rest.split()
        if len(parts) == 4:
            rects[key] = tuple(int(v) for v in parts)
        else:
            cols[key] = int(rest)
    return rects, cols


def rgb565(v):
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


S = 2   # the preview is 2x the panel


def font(px, mono=False):
    name = 'DejaVuSansMono.ttf' if mono else 'DejaVuSans.ttf'
    return ImageFont.truetype(os.path.join('/usr/share/fonts/truetype/dejavu', name), px * S)


def box(r):
    x, y, w, h = r
    return [x * S, y * S, (x + w) * S - 1, (y + h) * S - 1]


def draw_map(img, R, C):
    """The picker's 320x214 canvas: the real reference tile's centre, or a plain kMapBg."""
    map_rect = R['map']
    w, h = map_rect[2], map_rect[3]
    if os.path.exists(MAP_SRC):
        src = Image.open(MAP_SRC).convert('RGB')
        # The reference image's own centre at (384,384); crop the canvas around it.
        cx, cy = src.width // 2, src.height // 2
        left = max(0, min(src.width - w, cx - w // 2))
        top = max(0, min(src.height - h, cy - h // 2))
        crop = src.crop((left, top, left + w, top + h))
        img.paste(crop.resize((w * S, h * S), Image.LANCZOS),
                  (map_rect[0] * S, map_rect[1] * S))
    else:
        ImageDraw.Draw(img).rectangle(box(map_rect), fill=rgb565(C['kMapBg']))


def draw_status(img, R, C):
    d = ImageDraw.Draw(img)
    s = R['status']
    d.rectangle(box(s), fill=rgb565(C['kBar']))
    d.line([0, s[3] * S - 1, 320 * S, s[3] * S - 1], fill=rgb565(C['kLine']))
    d.text((6 * S, 13 * S), '11:24', font=font(9, True), fill=rgb565(C['kInk']), anchor='lm')
    d.text((300 * S, 13 * S), 'GPS 9', font=font(9, True), fill=rgb565(C['kGreen']),
           anchor='rm')
    d.text((268 * S, 13 * S), '82%', font=font(9, True), fill=rgb565(C['kDim']), anchor='rm')


def draw_picker(R, C):
    img = Image.new('RGB', (320 * S, 240 * S), rgb565(C['kScreen']))
    draw_map(img, R, C)
    draw_status(img, R, C)
    d = ImageDraw.Draw(img)

    # Cancel (the picker's back key), the two zoom keys and the z15 hint, exactly where the layout
    # puts them.
    b = R['back']
    d.rounded_rectangle(box(b), radius=C['radius'] * S, fill=rgb565(C['kPanel2']),
                        outline=rgb565(C['kLine']))
    d.text(((b[0] + 8) * S, (b[1] + b[3] / 2) * S), 'Cancel', font=font(9, True),
           fill=rgb565(C['kInk']), anchor='lm')
    for key, glyph in ((R['zoomIn'], '+'), (R['zoomOut'], '-')):
        d.rounded_rectangle(box(key), radius=C['radius'] * S, fill=rgb565(C['kPanel2']),
                            outline=rgb565(C['kLine']))
        d.text(((key[0] + key[2] / 2) * S, (key[1] + key[3] / 2) * S), glyph,
               font=font(10, True), fill=rgb565(C['kInk']), anchor='mm')
    hint_x = (b[0] + b[2] + 8) * S
    hint_y = (b[1] + b[3] / 2) * S
    d.text((hint_x, hint_y), 'z15', font=font(9, True), fill=rgb565(C['kDim']), anchor='lm')

    # EST-393: the Map's own centre cross, two 1 px lines in kMapCrossInk, no outline and no dot.
    cross = R['cross']
    cx = cross[0] + cross[2] // 2
    cy = cross[1] + cross[3] // 2
    arm = cross[2] // 2
    d.rectangle([cx * S, (cy - arm) * S, cx * S + S - 1, (cy + arm) * S + S - 1],
                fill=rgb565(C['kMapCrossInk']))
    d.rectangle([(cx - arm) * S, cy * S, (cx + arm) * S + S - 1, cy * S + S - 1],
                fill=rgb565(C['kMapCrossInk']))

    # The banner: full width, flush with the bottom edge (its bottom is 240).
    p = R['panel']
    d.rectangle(box(p), fill=rgb565(C['kPanel']))
    d.line([p[0] * S, p[1] * S, (p[0] + p[2]) * S - 1, p[1] * S], fill=rgb565(C['kLine']))

    # PLACEHOLDER digits only: the preview never carries a real coordinate.
    c = R['coords']
    d.text((c[0] * S, (c[1] + c[3] / 2) * S), '00.00000, 00.00000', font=font(10, True),
           fill=rgb565(C['kInk']), anchor='lm')

    g = R['useGps']
    d.rounded_rectangle(box(g), radius=C['radius'] * S, fill=rgb565(C['kPanel2']),
                        outline=rgb565(C['kLine']))
    d.text(((g[0] + g[2] / 2) * S, (g[1] + g[3] / 2) * S), 'Use my GPS position',
           font=font(10, True), fill=rgb565(C['kDimmer']), anchor='mm')

    a = R['apply']
    d.rounded_rectangle(box(a), radius=C['radius'] * S, fill=rgb565(C['kSelBg2']),
                        outline=rgb565(C['kAcc']))
    d.text(((a[0] + a[2] / 2) * S, (a[1] + a[3] / 2) * S), 'Save', font=font(11),
           fill=rgb565(C['kAccInk']), anchor='mm')
    return img


def main():
    R, C = geometry()
    img = draw_picker(R, C)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT)
    print('wrote', OUT)
    print('banner', R['panel'], 'bottom', R['panel'][1] + R['panel'][3])
    print('cross', R['cross'], 'centre', (R['cross'][0] + R['cross'][2] // 2,
                                          R['cross'][1] + R['cross'][3] // 2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
