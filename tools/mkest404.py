#!/usr/bin/env python3
"""EST-404 preview: the charging bolt drawn OVER the battery icon, tips clear of the frame.

Nothing here invents a rectangle, a colour or a bolt pixel. A tiny C++ dumper (compiled with the
system g++, over the firmware's own headers) prints:
  * the status bar's real rectangles from ui/status_cells.h for a 320 x 240 Layout, for each state
    below and with the text widths the mono status face really measures (5 x 6 for "19:26" and
    "GPS 7", 3 x 6 for "78%"/"12%" and 4 x 6 for "100%");
  * the theme's real RGB565 colours;
  * the bolt's real 7 x 11 runs from ui/battery_bolt.h and the battery/bolt sizes from theme.h.
Python fills those rectangles and draws the same runs with the same 1 px kBar edge and the same fill
arithmetic widgets.cpp uses. The icon is drawn straight onto the bar (no paste tile), so a tip that
stands above or below the frame is visible exactly as on the panel.

Output: docs/shots/2026-11-11-est404-bolt-tips.png, at 1x and 8x, for four states:
plain, bolt at 78 %, bolt at 12 % (red low-battery fill) and bolt at 100 % (full green fill).
"""
import math
import os
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'docs', 'shots', '2026-11-11-est404-bolt-tips.png')

DUMPER = r'''
#include <stdio.h>
#include <string.h>
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
  printf("status %d %d %d %d\n", L.status.x, L.status.y, L.status.w, L.status.h);
  printf("batt %d %d\n", kBattW, kBattH);
  printf("boltbox %d %d\n", kBoltW, kBoltH);
  for (int i = 0; i < kBoltH; ++i) {
    printf("bolt %d %d %d\n", i, kBoltRuns[i].x, kBoltRuns[i].w);
  }

  const int pcts[] = {78, 12, 100};
  for (int p = 0; p < 3; ++p) {
    const int pct = pcts[p];
    char pctText[8];
    snprintf(pctText, sizeof(pctText), "%d%%", pct);
    StatusCellWidths w;
    w.clock = 5 * kMonoStatusAdvancePx;                  // "19:26"
    w.pct = (int)strlen(pctText) * kMonoStatusAdvancePx;
    w.gps = 5 * kMonoStatusAdvancePx;                    // "GPS 7"
    const StatusCellFlags f;
    const StatusBarRects r = statusBarRects(L, w, f);
    const Rect* rs[] = {&r.time, &r.gear, &r.battery, &r.battIcon, &r.battText, &r.gps};
    const char* rn[] = {"time", "gear", "battery", "battIcon", "battText", "gps"};
    for (int i = 0; i < 6; ++i) {
      printf("rect %d %s %d %d %d %d\n", pct, rn[i], rs[i]->x, rs[i]->y, rs[i]->w, rs[i]->h);
    }
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
    return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', px)


def gear(d, r, col, bg, s, base_y):
    """A 13 px cog approximation (8 teeth, a hub hole), enough to read the bar as itself."""
    ox, oy = r[0] * s, base_y + r[1] * s
    cx, cy = ox + r[2] * s // 2, oy + r[3] * s // 2
    rad = r[2] * s // 2
    for i in range(8):
        a = i * math.pi / 4
        x = cx + int(rad * 0.82 * math.cos(a))
        y = cy + int(rad * 0.82 * math.sin(a))
        d.rectangle([x - s, y - s, x + s, y + s], fill=col)
    d.ellipse([cx - rad * 0.62, cy - rad * 0.62, cx + rad * 0.62, cy + rad * 0.62], fill=col)
    d.ellipse([cx - rad * 0.22, cy - rad * 0.22, cx + rad * 0.22, cy + rad * 0.22], fill=bg)


def battery(d, rect, pct, runs, battw, batth, boltw, bolth, C, s, with_bolt, base_y):
    """widgets.cpp drawBattery()/drawBolt() at 1 px = s canvas px, drawn on the bar itself."""
    x, y = rect[0], rect[1]
    X, Y = x * s, base_y + y * s
    W, H = battw * s, batth * s
    d.rectangle([X, Y, X + W - s, Y + H - s], outline=C['kDim'], width=s)
    d.rectangle([X + W, Y + 2 * s, X + W + 2 * s - 1, Y + (batth - 2) * s - 1], fill=C['kDim'])
    if pct >= 0:
        fill = (battw - 2) * (100 if pct > 100 else pct) // 100
        if fill > 0:
            col = C['kBad'] if pct <= 15 else (C['kWarn'] if pct <= 30 else C['kGreen'])
            d.rectangle([X + s, Y + s, X + (1 + fill) * s - 1, Y + (batth - 1) * s - 1], fill=col)
    if not with_bolt:
        return
    bx = x + (battw - boltw) // 2
    by = base_y + (y - (bolth - batth) // 2) * s   # EST-404: tips stand 2 px clear of the frame
    # The 1 px kBar edge, drawn for every row (tips included), then the gold over its middle.
    for r, rx, rw in runs:
        d.rectangle([(bx + rx - 1) * s, by + (r - 1) * s,
                     (bx + rx + rw + 1) * s - 1, by + (r + 1) * s - 1], fill=C['kBar'])
    for r, rx, rw in runs:
        d.rectangle([(bx + rx) * s, by + r * s,
                     (bx + rx + rw) * s - 1, by + (r + 1) * s - 1], fill=C['kAcc'])


def draw_bar(img, d, oy, s, label, pct, with_bolt, rects, batt, boltbox, runs, C):
    """One full status bar at `oy` (canvas px), 1 firmware px = s canvas px."""
    W, H = 320, 26
    d.rectangle([0, oy, W * s - 1, oy + H * s - 1], fill=C['kBar'])
    d.rectangle([0, oy + (H - 1) * s, W * s - 1, oy + (H - 1) * s + s - 1], fill=C['kLine'])
    cy = oy + H * s // 2
    f = font(10 * s)
    t = rects['time']
    d.text((t[0] * s, cy), '19:26', font=f, fill=C['kInk'], anchor='lm')
    g = rects['gps']
    d.text(((g[0] + g[2]) * s, cy), 'GPS 7', font=f, fill=C['kGreen'], anchor='rm')
    bt = rects['battText']
    pct_text = '%d%%' % pct
    d.text(((bt[0] + bt[2]) * s, cy), pct_text, font=f, fill=C['kDim'], anchor='rm')
    gr = rects['gear']
    gear(d, gr, C['kDim'], C['kBar'], s, oy)
    battery(d, rects['battIcon'], pct, runs, batt[0], batt[1], boltbox[0], boltbox[1], C, s,
            with_bolt, oy)
    d.text((4 * s, oy + H * s + 2), label, font=font(11), fill=(0x93, 0xa0, 0xb4))


def main():
    tmp = tempfile.mkdtemp(prefix='est404-')
    src = os.path.join(tmp, 'est404_preview.cpp')
    exe = os.path.join(tmp, 'est404_preview')
    with open(src, 'w', encoding='utf-8') as fh:
        fh.write(DUMPER)
    subprocess.run(['g++', '-std=c++17', '-I', os.path.join(ROOT, 'src'), src, '-o', exe],
                   check=True)
    out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout

    colours, states, runs = {}, {}, []
    status = batt = boltbox = None
    for line in out.splitlines():
        p = line.split()
        if not p:
            continue
        if p[0] == 'col':
            colours[p[1]] = rgb565(int(p[2]))
        elif p[0] == 'status':
            status = tuple(int(v) for v in p[1:5])
        elif p[0] == 'batt':
            batt = (int(p[1]), int(p[2]))
        elif p[0] == 'boltbox':
            boltbox = (int(p[1]), int(p[2]))
        elif p[0] == 'bolt':
            runs.append((int(p[1]), int(p[2]), int(p[3])))
        elif p[0] == 'rect':
            states.setdefault(int(p[1]), {})[p[2]] = tuple(int(v) for v in p[3:7])
        else:
            raise SystemExit('unexpected dumper line: %r' % line)

    C = colours
    pad = 12
    scale_z = 8
    canvas_w = 320 * scale_z + 2 * pad
    one_h = 4 * (26 + 18)
    zoom_h = 4 * (26 * scale_z + 20)
    canvas_h = pad + one_h + 22 + zoom_h + pad
    img = Image.new('RGB', (canvas_w, canvas_h), (0x13, 0x12, 0x10))
    d = ImageDraw.Draw(img)

    d.text((pad, pad), 'EST-404: the charging bolt drawn OVER the battery icon, tips 2 px clear',
           font=font(13), fill=C['kInk'])
    d.text((pad, pad + 18), '1x (actual panel pixels): plain, 78 %, 12 %, 100 %',
           font=font(11), fill=(0x93, 0xa0, 0xb4))
    y = pad + 36
    for pct, with_bolt in ((78, False), (78, True), (12, True), (100, True)):
        lab = '%d %% %s' % (pct, 'with bolt' if with_bolt else 'plain')
        draw_bar(img, d, y, 1, lab, pct, with_bolt, states[pct], batt, boltbox, runs, C)
        y += 26 + 18

    y = pad + one_h + 22
    d.text((pad, y), '8x (each firmware pixel is 8 x 8): the same four states',
           font=font(13), fill=C['kInk'])
    y += 24
    for pct, with_bolt in ((78, False), (78, True), (12, True), (100, True)):
        lab = '%d %% %s' % (pct, 'with bolt' if with_bolt else 'plain')
        draw_bar(img, d, y, scale_z, lab, pct, with_bolt, states[pct], batt, boltbox, runs, C)
        y += 26 * scale_z + 20

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT)
    print(OUT)


if __name__ == '__main__':
    main()
