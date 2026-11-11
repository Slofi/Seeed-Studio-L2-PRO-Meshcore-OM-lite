#!/usr/bin/env python3
"""EST-392 preview: Settings > General > Position, closed and with the choice open.

Nothing here invents a coordinate or a colour. A tiny C++ dumper (compiled with the system g++ over
the firmware's own headers) builds the General page's real row list, asks ui/settings_layout.h for
the real rectangles (the tab row, the title/back key, every visible row, and the two-row choice) and
prints the real theme values from ui/theme.h. Python only fills those rectangles and puts the printed
labels in them with the same DejaVu family the panel's faces were generated from.

Output: docs/shots/2026-11-11-est392-position-choice.png (2x)
  left  panel: the page scrolled to POSITION, the choice closed: `Position  GPS >` with Satellites,
               Fix and Share position under it (the GPS details stay);
  right panel: the same page with the choice open: GPS (the source in use) and Fixed, whose row leads
               to EST-384's map picker.

No real coordinates anywhere: the satellites/fix numbers are invented counts, and the page has never
shown a coordinate since EST-384.
"""
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'docs', 'shots', '2026-11-11-est392-position-choice.png')

DUMPER = r'''
#include <stdio.h>
#include <string.h>
#include "ui/settings_layout.h"
#include "ui/theme.h"
using namespace om::ui;

// The General page as the shell builds it (the rows that matter here are the real ones: the
// POSITION section, its one Position row, the GPS details, Share position, and the rows above).
static SettingRow rows[21];
static int n = 0;
static int posRow = -1;

static void add(SetRowKind kind, const char* label, const char* value = "", bool on = false) {
  SettingRow& r = rows[n++];
  r = SettingRow();
  r.kind = kind;
  snprintf(r.label, sizeof(r.label), "%s", label);
  snprintf(r.value, sizeof(r.value), "%s", value);
  r.on = on;
}

static void row(const char* name, const Rect& r) {
  printf("%s %d %d %d %d\n", name, r.x, r.y, r.w, r.h);
}
static void col(const char* name, uint16_t c) { printf("%s %u\n", name, (unsigned)c); }

int main() {
  add(SetRowKind::Section, "RADIO & MESH");
  add(SetRowKind::Chevron, "Region / preset", "Europe");
  add(SetRowKind::Stepper, "TX power", "22 dBm");
  add(SetRowKind::Chevron, "Path hash", "2B");
  add(SetRowKind::Chevron, "Node", "OM Lite");
  add(SetRowKind::Chevron, "Channels", "2 channels");
  add(SetRowKind::Section, "CONNECTIONS");
  add(SetRowKind::Chevron, "Wi-Fi", "off");
  add(SetRowKind::Chevron, "Bluetooth", "off");
  add(SetRowKind::Section, "SOUND");
  add(SetRowKind::Chevron, "Sound", "on");
  add(SetRowKind::Section, "DISPLAY");
  add(SetRowKind::Stepper, "Brightness", "180");
  add(SetRowKind::Section, "TIME");
  add(SetRowKind::Chevron, "Time", "11:24");
  add(SetRowKind::Section, kPosSectionLabel);
  posRow = n;
  add(SetRowKind::Chevron, kPosRowLabel, "GPS");
  add(SetRowKind::Value, "Satellites", "9 used, 14 heard");
  add(SetRowKind::Value, "Fix", "3D");
  add(SetRowKind::Toggle, "Share position", "ON", true);
  add(SetRowKind::Section, "ABOUT");

  const SettingsLayout L = settingsLayoutFor(kScreenW, kScreenH, false, false, false);
  row("status", L.status);
  row("tabrow", L.tabrow);
  for (int i = 0; i < kSettingsTabCount; ++i) {
    char nm[16];
    snprintf(nm, sizeof(nm), "tab%d", i);
    row(nm, L.tabs[i]);
  }
  row("title", L.title);
  row("back", L.backButton);
  row("body", L.body);

  // The scroll the shell lifts the page to when the row is tapped (EST-392).
  const int opened = settingsChoiceScrollFor(L, rows, n, posRow, 0);
  printf("opened %d\n", opened);
  printf("maxscroll %d\n", settingsMaxScroll(L, rows, n));
  for (int i = 0; i < n; ++i) {
    const Rect vis = settingsRowVisible(L, rows, n, i, opened);
    if (vis.empty()) continue;
    // The FULL rect is what drawRow is given (the body clip trims it); the visible slice is what
    // the hit test can land on. Both are printed so the preview draws like the panel and a reader
    // can see the two are the same rectangle.
    const Rect full = settingsRowRect(L, rows, n, i, opened);
    printf("row.%d %d %d %d %d\n", i, full.x, full.y, full.w, full.h);
    printf("vis.%d %d %d %d %d\n", i, vis.x, vis.y, vis.w, vis.h);
    printf("kind.%d %d\n", i, (int)rows[i].kind);
    const char* l = rows[i].label;
    const char* v = rows[i].value;
    printf("label.%d %s\n", i, l);
    printf("value.%d %s\n", i, v);
    printf("on.%d %d\n", i, rows[i].on ? 1 : 0);
  }
  const SettingsChoiceLayout c = settingsPositionChoiceFor(L, rows, n, posRow, opened);
  row("panel", c.panel);
  for (int i = 0; i < kPosChoiceCount; ++i) {
    char nm[16];
    snprintf(nm, sizeof(nm), "option%d", i);
    row(nm, c.option[i]);
    printf("optlabel.%d %s\n", i, posOptionLabel(i));
    printf("optdesc.%d %s\n", i, posOptionDesc(i));
  }
  printf("posrow %d\n", posRow);

  col("kScreen", kScreen);
  col("kBar", kBar);
  col("kLine", kLine);
  col("kRowLine", kRowLine);
  col("kInk", kInk);
  col("kDim", kDim);
  col("kDimmer", kDimmer);
  col("kAcc", kAcc);
  col("kSelBg", kSelBg);
  col("kAccInk", kAccInk);
  col("kGreen", kGreen);
  col("kPanel", kPanel);
  col("kPanel2", kPanel2);
  return 0;
}
'''


def geometry():
    src = '/tmp/est392_preview_geo.cpp'
    with open(src, 'w') as f:
        f.write(DUMPER)
    cmd = ['g++', '-std=c++17', '-O1', '-I', os.path.join(ROOT, 'src'), src,
           '-o', '/tmp/est392_preview_geo']
    subprocess.run(cmd, check=True)
    out = subprocess.run(['/tmp/est392_preview_geo'], check=True, capture_output=True, text=True)
    rects, cols, rows = {}, {}, {}
    for line in out.stdout.splitlines():
        p = line.split(' ', 1)
        if len(p) != 2:
            continue
        key, rest = p
        if key in ('opened', 'maxscroll', 'posrow'):
            cols[key] = int(rest)
        elif key.startswith('label.') or key.startswith('value.') or key.startswith('optlabel.') \
                or key.startswith('optdesc.'):
            rows[key] = rest
        elif key.startswith('kind.') or key.startswith('on.'):
            rows[key] = int(rest)
        elif len(rest.split()) == 4:
            rects[key] = tuple(int(v) for v in rest.split())
        else:
            cols[key] = int(rest)
    return rects, cols, rows


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


def draw_page(R, C, rows, open_choice):
    img = Image.new('RGB', (320 * S, 240 * S), rgb565(C['kScreen']))
    d = ImageDraw.Draw(img)

    # status bar: the clock, the battery, the GPS count and the gear (the app's own chrome)
    d.rectangle(box(R['status']), fill=rgb565(C['kBar']))
    d.line([0, R['status'][3] * S - 1, 320 * S, R['status'][3] * S - 1],
           fill=rgb565(C['kLine']))
    d.text((6 * S, 13 * S), '11:24', font=font(9, True), fill=rgb565(C['kInk']), anchor='lm')
    d.text((300 * S, 13 * S), 'GPS 9', font=font(9, True), fill=rgb565(C['kGreen']),
           anchor='rm')
    d.text((268 * S, 13 * S), '82%', font=font(9, True), fill=rgb565(C['kDim']), anchor='rm')

    # the settings tab row: General selected (the gold underline)
    d.rectangle(box(R['tabrow']), fill=rgb565(C['kBar']))
    d.line([0, (R['tabrow'][1] + R['tabrow'][3]) * S - 1, 320 * S,
            (R['tabrow'][1] + R['tabrow'][3]) * S - 1], fill=rgb565(C['kLine']))
    names = ['General', 'Nodes', 'Chat', 'Map', 'RPTR']
    for i, nm in enumerate(names):
        r = R[f'tab{i}']
        on = i == 0
        if on:
            d.rectangle(box(r), fill=rgb565(C['kSelBg']))
            d.rectangle([r[0] * S, (r[1] + r[3] - 2) * S, (r[0] + r[2]) * S - 1,
                         (r[1] + r[3]) * S - 1], fill=rgb565(C['kAcc']))
        d.text(((r[0] + r[2] / 2) * S, (r[1] + r[3] / 2 - 1) * S), nm, font=font(9, True),
               fill=rgb565(C['kAcc'] if on else C['kDimmer']), anchor='mm')

    # the title row: Back on the left, "Settings" after it
    b = R['back']
    d.rounded_rectangle(box(b), radius=3 * S, fill=rgb565(C['kPanel2']),
                        outline=rgb565(C['kLine']))
    d.line([(b[0] + 7) * S, (b[1] + b[3] / 2 - 3) * S, (b[0] + 4) * S, (b[1] + b[3] / 2) * S,
            (b[0] + 7) * S, (b[1] + b[3] / 2 + 3) * S], fill=rgb565(C['kAcc']), width=S)
    d.text(((b[0] + 12) * S, (b[1] + b[3] / 2 + 1) * S), 'Back', font=font(9),
           fill=rgb565(C['kDim']), anchor='lm')
    t = R['title']
    d.text(((b[0] + b[2] + 8) * S, (t[1] + t[3] / 2 + 1) * S), 'Settings', font=font(12),
           fill=rgb565(C['kInk']), anchor='lm')
    d.line([0, (t[1] + t[3] - 1) * S, 320 * S, (t[1] + t[3] - 1) * S], fill=rgb565(C['kLine']))

    # the rows, from the FULL rectangles the firmware hands drawRow, clipped to the body exactly as
    # screen_settings.cpp clips them (rows scroll under the title row and are cut at the floor)
    rows_layer = Image.new('RGB', img.size, rgb565(C['kScreen']))
    rd = ImageDraw.Draw(rows_layer)
    for i in range(21):
        key = f'row.{i}'
        if key not in R:
            continue
        x, y, w, h = R[key]
        kind = rows[f'kind.{i}']
        label = rows.get(f'label.{i}', '')
        value = rows.get(f'value.{i}', '')
        on = rows.get(f'on.{i}', 0)
        cy = (y + h / 2) * S
        if kind == 0:   # Section
            rd.text(((x + 7) * S, (y + h - 4) * S), label, font=font(8, True),
                    fill=rgb565(C['kDimmer']), anchor='lm')
            continue
        if kind == 2:   # Chevron
            rd.text(((x + 7) * S, cy), label, font=font(11), fill=rgb565(C['kInk']), anchor='lm')
            if value:
                rd.text(((x + w - 19) * S, cy), value, font=font(9, True),
                        fill=rgb565(C['kDim']), anchor='rm')
            cx = (x + w - 7) * S
            rd.line([cx - 4 * S, cy - 3 * S, cx, cy, cx - 4 * S, cy + 3 * S],
                    fill=rgb565(C['kDimmer']), width=S)
        elif kind == 6:  # Toggle
            rd.text(((x + 7) * S, cy), label, font=font(11), fill=rgb565(C['kInk']), anchor='lm')
            rd.text(((x + w - 7) * S, cy), 'ON' if on else 'OFF', font=font(9, True),
                    fill=rgb565(C['kGreen'] if on else C['kDimmer']), anchor='rm')
        elif kind == 5:  # Stepper: the two keys and the value between them
            minus = [x + w - 7 - 2 * 18 - 5 - 66 - 5, y + (h - 18) // 2, 18, 18]
            plus = [x + w - 7 - 18, y + (h - 18) // 2, 18, 18]
            for k in (minus, plus):
                rd.rounded_rectangle([k[0] * S, k[1] * S, (k[0] + k[2]) * S - 1,
                                      (k[1] + k[3]) * S - 1], radius=3 * S,
                                     fill=rgb565(C['kPanel2']), outline=rgb565(C['kLine']))
            mcy = (minus[1] + 9) * S
            rd.line([(minus[0] + 5) * S, mcy, (minus[0] + 13) * S, mcy],
                    fill=rgb565(C['kInk']), width=S)
            rd.line([(plus[0] + 5) * S, mcy, (plus[0] + 13) * S, mcy],
                    fill=rgb565(C['kInk']), width=S)
            rd.line([(plus[0] + 9) * S, (plus[1] + 5) * S, (plus[0] + 9) * S, (plus[1] + 13) * S],
                    fill=rgb565(C['kInk']), width=S)
            rd.text(((x + 7) * S, cy), label, font=font(11), fill=rgb565(C['kInk']), anchor='lm')
            rd.text(((minus[0] + 18 + 5 + 66) * S, cy), value, font=font(9, True),
                    fill=rgb565(C['kInk']), anchor='rm')
        else:            # Value
            rd.text(((x + 7) * S, cy), label, font=font(11), fill=rgb565(C['kInk']), anchor='lm')
            if value:
                rd.text(((x + w - 7) * S, cy), value, font=font(9, True),
                        fill=rgb565(C['kDim']), anchor='rm')
        rd.line([x * S, (y + h - 1) * S, (x + w) * S - 1, (y + h - 1) * S],
                fill=rgb565(C['kRowLine']))
    bx, by, bw, bh = R['body']
    body_box = (bx * S, by * S, (bx + bw) * S, (by + bh) * S)
    img.paste(rows_layer.crop(body_box), body_box[:2])

    if open_choice:
        # the choice: the same panel the chat selector drops, one plain row per option, the source
        # in use highlighted and carrying the Check row's dot
        p = R['panel']
        d.rectangle(box(p), fill=rgb565(C['kPanel']), outline=rgb565(C['kLine']), width=S)
        for i in range(2):
            o = R[f'option{i}']
            on = i == 0            # GPS is the source in use in this preview
            ox, oy, ow, oh = o
            if on:
                d.rectangle(box(o), fill=rgb565(C['kSelBg']))
            ocy = (oy + oh / 2) * S
            d.text(((ox + 7) * S, ocy), rows[f'optlabel.{i}'], font=font(11),
                   fill=rgb565(C['kAccInk'] if on else C['kInk']), anchor='lm')
            d.text(((ox + ow - 21) * S, ocy), rows[f'optdesc.{i}'], font=font(9, True),
                   fill=rgb565(C['kDim']), anchor='rm')
            if on:
                cxx = (ox + ow - 10) * S
                d.ellipse([cxx - 3 * S, ocy - 3 * S, cxx + 3 * S, ocy + 3 * S],
                          fill=rgb565(C['kGreen']))
            if i + 1 < 2:
                d.line([ox * S, (oy + oh - 1) * S, (ox + ow) * S - 1, (oy + oh - 1) * S],
                       fill=rgb565(C['kRowLine']))
    return img



def main():
    R, C, rows = geometry()
    left = draw_page(R, C, rows, False)
    right = draw_page(R, C, rows, True)
    out = Image.new('RGB', (320 * S * 2 + 8, 240 * S), (12, 11, 9))
    out.paste(left, (0, 0))
    out.paste(right, (320 * S + 8, 0))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    out.save(OUT)
    print('wrote', OUT, 'at scroll', C['opened'], 'of', C['maxscroll'])
    return 0


if __name__ == '__main__':
    sys.exit(main())
