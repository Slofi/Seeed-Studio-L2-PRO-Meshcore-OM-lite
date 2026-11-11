#!/usr/bin/env python3
"""EST-400 preview: the three visible polish fixes, drawn from the firmware's OWN rectangles.

Nothing here invents a rectangle or a colour. A tiny C++ dumper (compiled with the system g++, over
the firmware's headers) prints:
  * the one-line toast's plate (the same kToastMargin/kToastH/l2() arithmetic widgets.cpp uses) and
    the room its text has;
  * the Settings page's Warn row and the plate drawWarn() now draws inside it;
  * the Info page's header, clipped body and floor hint from ui/node_info_layout.h, and the NINE real
    row labels a contact with a key, an advert, a position, a path and an SNR produces.
Python only fills those rectangles with the theme's real RGB565 colours and the same DejaVu family
the panel's faces were generated from. The text is the real UI copy or invented placeholders: no
real name, no coordinate, no message text.

Output: docs/shots/2026-11-11-est400-polish.png (2x), four panels:
  toast        the long line that used to run off the plate now ends with "..." inside it;
  warn         the plate is inset like every other row and its text ends inside it;
  info top     the ninth row is cut by the body's clip (it no longer lands on the hint);
  info scrolled the same page at its maximum scroll, the ninth row on the body's floor.
"""
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'docs', 'shots', '2026-11-11-est400-polish.png')
S = 2

DUMPER = r'''
#include <stdio.h>
#include <string.h>
#include "ui/layout.h"
#include "ui/node_info.h"
#include "ui/node_info_layout.h"
#include "ui/settings_layout.h"
#include "ui/theme.h"
using namespace om::ui;

static void row(const char* name, const Rect& r) {
  printf("%s %d %d %d %d\n", name, r.x, r.y, r.w, r.h);
}

int main() {
  const Layout& L = l2();
  row("status", L.status);
  row("screen", Rect{0, 0, L.width, L.height});
  // The one-line toast: ui/widgets.cpp drawToast()'s own arithmetic.
  const Rect toast{kToastMargin, (int16_t)(L.tabbar.y - kToastMargin - kToastH),
                   (int16_t)(L.width - 2 * kToastMargin), kToastH};
  row("toast", toast);
  printf("toastroom %d\n", toast.w - 2 * kToastMargin);

  // Settings > Preset confirmation: the real Warn row and the plate drawWarn() now insets.
  SettingRow rows[3];
  rows[0].kind = SetRowKind::Value;
  snprintf(rows[0].label, sizeof(rows[0].label), "%s", "Preset");
  snprintf(rows[0].value, sizeof(rows[0].value), "%s", "Europe");
  rows[1].kind = SetRowKind::Warn;
  snprintf(rows[1].label, sizeof(rows[1].label), "%s",
           "A node with the wrong preset is deaf: it hears nothing and reports no error.");
  rows[2].kind = SetRowKind::Button;
  snprintf(rows[2].label, sizeof(rows[2].label), "%s", "Apply and save");
  const SettingsLayout SL = settingsLayoutFor(L.width, L.height, true);
  row("settingsbody", SL.body);
  row("setrow0", settingsRowRect(SL, rows, 3, 0, 0));
  const Rect warnRow = settingsRowRect(SL, rows, 3, 1, 0);
  row("warn", warnRow);
  row("warnplate", Rect{(int16_t)(warnRow.x + kSetPadX), (int16_t)(warnRow.y + 3),
                        (int16_t)(warnRow.w - 2 * kSetPadX), (int16_t)(warnRow.h - 6)});
  printf("warnroom %d\n", warnRow.w - 2 * kSetPadX - 12);
  row("setrow2", settingsRowRect(SL, rows, 3, 2, 0));

  // The Info page at its largest: name, type, 3 key rows, advert, position, path, SNR = 9 rows.
  NodeInfoModel m;
  snprintf(m.name, sizeof(m.name), "%s", "Alice");
  m.hasKey = m.hasPosition = m.hasSnr = true;
  snprintf(m.lastSeen, sizeof(m.lastSeen), "%s", "12:30");
  snprintf(m.path, sizeof(m.path), "%s", "direct");
  m.snr4 = 30;
  const NodeInfoLayout NI = nodeInfoLayoutFor(L);
  row("nodeback", NI.back);
  row("nodebody", NI.body);
  row("nodehint", NI.hint);
  NodeInfoLine lines[kMaxNodeInfoLines];
  const int n = nodeInfoLines(m, lines, kMaxNodeInfoLines);
  printf("nodecount %d\n", n);
  for (int i = 0; i < n; ++i) {
    printf("line %d %d %s\n", i, NI.body.y + i * kNodeInfoRowH + kNodeInfoRowH / 2, lines[i].label);
  }
  printf("nodemaxscroll %d\n", nodeInfoMaxScroll(n, NI));
  printf("rowh %d\n", kNodeInfoRowH);

  const uint16_t cs[] = {kScreen, kBar, kLine, kRowLine, kInk, kDim, kDimmer, kAcc, kAccInk,
                         kPanel, kPanel2, kWarnBg, kWarnLine, kWarnInk, kSelBg2};
  const char* names[] = {"kScreen", "kBar", "kLine", "kRowLine", "kInk", "kDim", "kDimmer", "kAcc",
                         "kAccInk", "kPanel", "kPanel2", "kWarnBg", "kWarnLine", "kWarnInk",
                         "kSelBg2"};
  for (int i = 0; i < (int)(sizeof(cs) / sizeof(cs[0])); ++i) {
    printf("col %s %u\n", names[i], (unsigned)cs[i]);
  }
  return 0;
}
'''


def rgb565(v):
    r = ((v >> 11) & 0x1F) * 255 // 31
    g = ((v >> 5) & 0x3F) * 255 // 63
    b = (v & 0x1F) * 255 // 31
    return (r, g, b)


def font(px, mono=False):
    name = 'DejaVuSansMono.ttf' if mono else 'DejaVuSans.ttf'
    return ImageFont.truetype(os.path.join('/usr/share/fonts/truetype/dejavu', name), px * S)


def ellipsise(d, text, f, room):
    if d.textlength(text, font=f) <= room * S:
        return text
    dots = '...'
    dots_w = d.textlength(dots, font=f)
    lo = 0
    for i in range(1, len(text) + 1):
        if d.textlength(text[:i], font=f) + dots_w > room * S:
            break
        lo = i
    return text[:lo] + dots


def wrapper(text, f, d, room):
    lines, cur = [], ''
    for word in text.split(' '):
        trial = (cur + ' ' + word).strip()
        if not cur or d.textlength(trial, font=f) <= room * S:
            cur = trial
        else:
            lines.append(cur)
            cur = word
    if cur:
        lines.append(cur)
    return lines


def main():
    src = '/tmp/est400_preview.cpp'
    exe = '/tmp/est400_preview'
    with open(src, 'w', encoding='utf-8') as fh:
        fh.write(DUMPER)
    subprocess.run(['g++', '-std=c++17', '-I', os.path.join(ROOT, 'src'), src, '-o', exe],
                   check=True)
    out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout

    rects, colours, lines, misc = {}, {}, [], {}
    for line in out.splitlines():
        p = line.split()
        if not p:
            continue
        if p[0] == 'col':
            colours[p[1]] = rgb565(int(p[2]))
        elif len(p) == 5 and all(v.lstrip('-').isdigit() for v in p[1:]):
            rects[p[0]] = tuple(int(v) for v in p[1:])
        elif p[0] == 'line':
            lines.append((int(p[1]), int(p[2]), ' '.join(p[3:])))
        elif p[0] == 'toastroom':
            misc['toastroom'] = int(p[1])
        elif p[0] == 'warnroom':
            misc['warnroom'] = int(p[1])
        elif p[0] == 'nodecount':
            misc['nodecount'] = int(p[1])
        elif p[0] == 'nodemaxscroll':
            misc['nodemaxscroll'] = int(p[1])
        elif p[0] == 'rowh':
            misc['rowh'] = int(p[1])
        else:
            raise SystemExit(f'unexpected dumper line: {line!r}')

    C = colours
    W, H = 320, 240
    pad = 14
    panels = 4
    img = Image.new('RGB', ((W + pad) * panels * S + pad * S, (H + 40) * S),
                    (0x13, 0x12, 0x10))
    d = ImageDraw.Draw(img)

    def panel(i, title):
        ox = (pad + i * (W + pad)) * S
        oy = 30 * S
        box = rects['screen']
        d.rectangle([ox, oy, ox + box[2] * S, oy + box[3] * S], fill=C['kScreen'])
        d.text((ox, 8 * S), title, font=font(9), fill=C['kDim'])
        return ox, oy

    def status(ox, oy):
        r = rects['status']
        d.rectangle([ox, oy, ox + r[2] * S, oy + r[3] * S], fill=C['kBar'])
        d.line([ox, oy + r[3] * S, ox + r[2] * S, oy + r[3] * S], fill=C['kLine'])
        d.text((ox + 6 * S, oy + 13 * S), '11:24', font=font(9, True), fill=C['kInk'],
               anchor='lm')
        d.text((ox + (W - 6) * S, oy + 13 * S), 'GPS 9', font=font(9, True), fill=C['kGreen']
               if 'kGreen' in C else C['kDim'], anchor='rm')
        d.text((ox + (W - 46) * S, oy + 13 * S), '82%', font=font(9, True), fill=C['kDim'],
               anchor='rm')

    # ---- 1. the toast -----------------------------------------------------------------------
    ox, oy = panel(0, 'toast: ellipsised to its plate')
    status(ox, oy)
    tb = rects['toast']
    d.rounded_rectangle([ox + tb[0] * S, oy + tb[1] * S, ox + (tb[0] + tb[2]) * S,
                         oy + (tb[1] + tb[3]) * S], radius=4 * S, fill=C['kPanel2'],
                        outline=C['kLine'], width=S)
    f = font(11)
    text = 'History not saved: card error, 4294967295 entries dropped from the log'
    cut = ellipsise(d, text, f, misc['toastroom'])
    tw = d.textlength(cut, font=f)
    cx = ox + (tb[0] + (tb[2] - tw / S) / 2) * S
    d.text((cx, oy + (tb[1] + tb[3] / 2) * S), cut, font=f, fill=C['kInk'], anchor='lm')

    # ---- 2. the Warn box --------------------------------------------------------------------
    ox, oy = panel(1, 'Warn box: plate and text inside the row')
    status(ox, oy)
    d.line([ox, oy + (rects['warn'][1]) * S, ox + W * S, oy + rects['warn'][1] * S],
           fill=C['kLine'])
    r = rects['setrow0']
    d.text((ox + (r[0] + 7) * S, oy + (r[1] + r[3] / 2) * S), 'Preset', font=font(11),
           fill=C['kInk'], anchor='lm')
    d.text((ox + (r[0] + r[2] - 7) * S, oy + (r[1] + r[3] / 2) * S), 'Europe', font=font(9, True),
           fill=C['kDim'], anchor='rm')
    p = rects['warnplate']
    d.rounded_rectangle([ox + p[0] * S, oy + p[1] * S, ox + (p[0] + p[2]) * S,
                         oy + (p[1] + p[3]) * S], radius=4 * S, fill=C['kWarnBg'],
                        outline=C['kWarnLine'], width=S)
    f = font(11)
    y = oy + (p[1] + 6) * S
    for i, ln in enumerate(wrapper('A node with the wrong preset is deaf: it hears nothing '
                                   'and reports no error.', f, d, misc['warnroom'])[:3]):
        d.text((ox + (p[0] + 6) * S, y + i * 11 * S + 5 * S), ln, font=f, fill=C['kWarnInk'],
               anchor='lm')
    b = rects['setrow2']
    d.rounded_rectangle([ox + (b[0] + 7) * S, oy + (b[1] + 3) * S, ox + (b[0] + b[2] - 7) * S,
                         oy + (b[1] + b[3] - 3) * S], radius=4 * S, fill=C['kPanel2'],
                        outline=C['kLine'], width=S)
    d.text((ox + (b[0] + b[2] / 2) * S, oy + (b[1] + b[3] / 2) * S), 'Apply and save',
           font=f, fill=C['kInk'], anchor='mm')

    # ---- 3 and 4. the Info page, at the top and at its maximum scroll -----------------------
    values = ['Alice', 'client', '3f2a91c4', '8d7e6f50 11223344', '55667788 99aabbcc',
              '12:30', '<position>', 'direct', '7.5 dB']
    for i, (title, scroll) in enumerate((('Info: nine rows, the last clipped', 0),
                                         ('Info: scrolled, the last row on the floor',
                                          misc['nodemaxscroll']))):
        ox, oy = panel(2 + i, title)
        status(ox, oy)
        hb = rects['nodeback']
        d.rectangle([ox + hb[0] * S, oy + hb[1] * S, ox + (hb[0] + hb[2]) * S,
                     oy + (hb[1] + hb[3]) * S], fill=C['kScreen'])
        d.line([ox + hb[0] * S, oy + (hb[1] + hb[3] - 1) * S, ox + (hb[0] + hb[2]) * S,
                oy + (hb[1] + hb[3] - 1) * S], fill=C['kLine'])
        cy = hb[1] + hb[3] // 2
        ax = hb[0] + 10
        d.line([ox + (ax + 3) * S, oy + (cy - 4) * S, ox + ax * S, oy + cy * S], fill=C['kAcc'],
               width=S)
        d.line([ox + ax * S, oy + cy * S, ox + (ax + 3) * S, oy + (cy + 4) * S], fill=C['kAcc'],
               width=S)
        d.text((ox + (ax + 9) * S, oy + cy * S), 'Alice', font=font(12), fill=C['kInk'],
               anchor='lm')
        # The body's clip: a crop box around the rows only.
        bb = rects['nodebody']
        body_img = img.crop((ox, oy + bb[1] * S, ox + W * S, oy + (bb[1] + bb[3]) * S))
        bd = ImageDraw.Draw(body_img)
        for idx, cy_row, label in lines:
            y = (cy_row - bb[1] - scroll) * S
            if label:
                bd.text((7 * S, y), label, font=font(9, True), fill=C['kDim'], anchor='lm')
            bd.text((86 * S, y), values[idx] if idx < len(values) else '', font=font(9, True),
                    fill=C['kInk'], anchor='lm')
        img.paste(body_img, (ox, oy + bb[1] * S))
        hint = rects['nodehint']
        d.text((ox + hint[0] * S, oy + (hint[1] + hint[3] / 2) * S), 'tap the header to go back',
               font=font(9, True), fill=C['kDimmer'], anchor='lm')

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT)
    print(OUT)


if __name__ == '__main__':
    sys.exit(main())
