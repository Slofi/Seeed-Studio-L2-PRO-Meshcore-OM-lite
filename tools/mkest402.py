#!/usr/bin/env python3
"""EST-402 preview: the status bar with and without the charging bolt.

Nothing here invents a rectangle, a colour or a bolt pixel. A tiny C++ dumper (compiled with the
system g++, over the firmware's own headers) prints:
  * the status bar's real rectangles from ui/status_cells.h for a 320 x 240 Layout, with the text
    widths the mono status face really measures (5 x 6, 3 x 6 and 5 x 6 px);
  * the theme's real RGB565 colours;
  * the bolt's real 5 x 7 runs from ui/battery_bolt.h and the battery/bolt sizes from theme.h.
Python fills those rectangles and draws the same runs, the same 1 px kBar edge and the same fill
arithmetic widgets.cpp uses. The text is a status-line example (19:26, 78%, GPS 7): no real name,
position or message.

Output: docs/shots/2026-11-11-est402-charging-bolt.png (2x), two full bars plus an 8x crop of the
battery icon in both states, so the bolt can be judged at the size it is drawn.
"""
import math
import os
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'docs', 'shots', '2026-11-11-est402-charging-bolt.png')
S = 2

DUMPER = r'''
#include <stdio.h>
#include "ui/layout.h"
#include "ui/status_cells.h"
#include "ui/theme.h"
#include "ui/battery_bolt.h"

using namespace om::ui;

int main() {
  const Layout& L = l2();
  const char* names[] = {"kBar", "kLine", "kInk", "kDim", "kGreen", "kAcc", "kBad", "kWarn",
                         "kDimmer", "kPanel"};
  const uint16_t cs[] = {kBar, kLine, kInk, kDim, kGreen, kAcc, kBad, kWarn, kDimmer, kPanel};
  for (int i = 0; i < (int)(sizeof(cs) / sizeof(cs[0])); ++i) {
    printf("col %s %u\n", names[i], (unsigned)cs[i]);
  }

  StatusCellWidths w;
  w.clock = 5 * kMonoStatusAdvancePx;   // "19:26"
  w.pct = 3 * kMonoStatusAdvancePx;     // "78%"
  w.gps = 5 * kMonoStatusAdvancePx;     // "GPS 7"
  const StatusCellFlags f;
  const StatusBarRects r = statusBarRects(L, w, f);

  const Rect* rs[] = {&L.status, &r.time, &r.gear, &r.battery, &r.battIcon, &r.battText, &r.gps};
  const char* rn[] = {"status", "time", "gear", "battery", "battIcon", "battText", "gps"};
  for (int i = 0; i < 7; ++i) {
    printf("rect %s %d %d %d %d\n", rn[i], rs[i]->x, rs[i]->y, rs[i]->w, rs[i]->h);
  }
  printf("batt %d %d\n", kBattW, kBattH);
  printf("boltbox %d %d\n", kBoltW, kBoltH);
  for (int i = 0; i < kBoltH; ++i) {
    printf("bolt %d %d %d\n", i, kBoltRuns[i].x, kBoltRuns[i].w);
  }
  return 0;
}
'''


def rgb565(v):
    r = ((v >> 11) & 0x1F) * 255 // 31
    g = ((v >> 5) & 0x3F) * 255 // 63
    b = (v & 0x1F) * 255 // 31
    return (r, g, b)


def font(px):
    return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', px * S)


def gear(d, r, col, bg, s):
    """A 13 px cog approximation (8 teeth, a hub hole), enough to read the bar as itself."""
    ox, oy = r[0] * s, r[1] * s
    cx, cy = ox + r[2] * s // 2, oy + r[3] * s // 2
    rad = r[2] * s // 2
    for i in range(8):
        a = i * math.pi / 4
        x = cx + int(rad * 0.82 * math.cos(a))
        y = cy + int(rad * 0.82 * math.sin(a))
        d.rectangle([x - s, y - s, x + s, y + s], fill=col)
    d.ellipse([cx - rad * 0.62, cy - rad * 0.62, cx + rad * 0.62, cy + rad * 0.62], fill=col)
    d.ellipse([cx - rad * 0.22, cy - rad * 0.22, cx + rad * 0.22, cy + rad * 0.22], fill=bg)


def battery(d, x, y, pct, runs, battw, batth, boltw, bolth, C, s, with_bolt):
    """widgets.cpp drawBattery()/drawBolt() at 1 px = s canvas px."""
    X, Y = x * s, y * s
    W, H = battw * s, batth * s
    d.rectangle([X, Y, X + W - s, Y + H - s], outline=C['kDim'], width=s)
    d.rectangle([X + W, Y + 2 * s, X + W + 2 * s - 1, Y + (batth - 2) * s - 1], fill=C['kDim'])
    if pct >= 0:
        fill = (battw - 2) * (100 if pct > 100 else pct) // 100
        if fill > 0:
            col = C['kBad'] if pct <= 15 else (C['kWarn'] if pct <= 30 else C['kGreen'])
            d.rectangle([X + s, Y + s, X + fill * s + s - 1, Y + (batth - 1) * s - 1], fill=col)
    if not with_bolt:
        return
    bx = x + (battw - boltw) // 2
    # The 1 px dilation: one row of the gold run expanded by 1 px each side and 1 row above/below,
    # clipped to the icon's interior exactly as the C++ loop does.
    for r, rx, rw in runs:
        if r == 0 or r == bolth - 1:
            continue
        hy = 1 if r == 1 else (r - 1)
        hh = 2 if (r == 1 or r == bolth - 2) else 3
        d.rectangle([(bx + rx - 1) * s, (y + hy) * s,
                     (bx + rx + rw + 1) * s - 1, (y + hy + hh) * s - 1], fill=C['kBar'])
    for r, rx, rw in runs:
        d.rectangle([(bx + rx) * s, (y + r) * s,
                     (bx + rx + rw) * s - 1, (y + r + 1) * s - 1], fill=C['kAcc'])


def main():
    tmp = tempfile.mkdtemp(prefix='est402-')
    src = os.path.join(tmp, 'est402_preview.cpp')
    exe = os.path.join(tmp, 'est402_preview')
    with open(src, 'w', encoding='utf-8') as fh:
        fh.write(DUMPER)
    subprocess.run(['g++', '-std=c++17', '-I', os.path.join(ROOT, 'src'), src, '-o', exe],
                   check=True)
    out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout

    rects, colours, runs = {}, {}, []
    batt = boltbox = None
    for line in out.splitlines():
        p = line.split()
        if not p:
            continue
        if p[0] == 'col':
            colours[p[1]] = rgb565(int(p[2]))
        elif p[0] == 'rect':
            rects[p[1]] = tuple(int(v) for v in p[2:6])
        elif p[0] == 'batt':
            batt = (int(p[1]), int(p[2]))
        elif p[0] == 'boltbox':
            boltbox = (int(p[1]), int(p[2]))
        elif p[0] == 'bolt':
            runs.append((int(p[1]), int(p[2]), int(p[3])))
        else:
            raise SystemExit('unexpected dumper line: %r' % line)

    C = colours
    battw, batth = batt
    boltw, bolth = boltbox
    W = 320
    pad = 14
    zoom = 8
    canvas_w = (W + 2 * pad) * S
    canvas_h = 370
    img = Image.new('RGB', (canvas_w, canvas_h), (0x13, 0x12, 0x10))
    d = ImageDraw.Draw(img)

    def title(y, text, col):
        d.text((pad * S, y), text, font=font(9), fill=col)

    def bar(oy, with_bolt, pct):
        x0 = pad * S
        oy_log = oy // S
        r = rects['status']
        d.rectangle([x0, oy, x0 + r[2] * S - 1, oy + r[3] * S - 1], fill=C['kBar'])
        d.line([x0, oy + (r[3] - 1) * S, x0 + r[2] * S, oy + (r[3] - 1) * S], fill=C['kLine'])
        cy = oy + r[3] * S // 2
        f = font(10)
        t = rects['time']
        d.text((x0 + t[0] * S, cy), '19:26', font=f, fill=C['kInk'], anchor='lm')
        g = rects['gps']
        d.text((x0 + g[0] * S + g[2] * S, cy), 'GPS 7', font=f, fill=C['kGreen'], anchor='rm')
        bt = rects['battText']
        pct_text = '--' if pct < 0 else '%d%%' % pct
        d.text((x0 + bt[0] * S + bt[2] * S, cy), pct_text, font=f, fill=C['kDim'], anchor='rm')
        gr = rects['gear']
        gear(d, (gr[0], oy_log + gr[1], gr[2], gr[3]), C['kDim'], C['kBar'], S)
        bi = rects['battIcon']
        # The icon is drawn on its own tile with one 1 px margin, so the paste puts it exactly on
        # rects['battIcon']: the tile's origin is the icon's top-left minus the margin.
        bw = (battw + 2) * S
        bh = (batth + 2) * S
        tile = Image.new('RGB', (bw, bh), C['kBar'])
        td = ImageDraw.Draw(tile)
        battery(td, 1, 1, pct, runs, battw, batth, boltw, bolth, C, S, with_bolt)
        img.paste(tile, (x0 + (bi[0] - 1) * S, oy + (bi[1] - 1) * S))

    # 1. no bolt
    title(8 * S, 'no external power: the battery icon as it is today', C['kDim'])
    bar(24 * S, False, 78)
    # 2. with the bolt
    title(62 * S, 'external power present: the gold bolt (VBUSOK, debounced)', C['kDim'])
    bar(78 * S, True, 78)
    # 3. the icon at 8x, both states, and a low level to show the bolt over an empty fill
    title(116 * S, 'the icon at 8x: plain, bolt at 78 %, bolt at 12 %', C['kDim'])
    zy = 130 * S
    items = [(False, 78), (True, 78), (True, 12)]
    zx = pad * S
    for i, (with_bolt, pct) in enumerate(items):
        bw = (battw + 2) * zoom + 16
        bh = (batth + 2) * zoom + 16
        tile = Image.new('RGB', (bw, bh), C['kBar'])
        td = ImageDraw.Draw(tile)
        battery(td, 1, 1, pct, runs, battw, batth, boltw, bolth, C, zoom, with_bolt)
        img.paste(tile, (zx, zy))
        d.text((zx, zy + bh + 2), ('bolt' if with_bolt else 'plain') + ' %d%%' % pct,
               font=font(9), fill=C['kDim'])
        zx += bw + 14

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT)
    print(OUT)


if __name__ == '__main__':
    main()
