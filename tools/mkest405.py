#!/usr/bin/env python3
"""EST-405 preview: the Settings > General DISPLAY rows with the new "Screen auto-off" switch.

Nothing here invents a rectangle or a colour. A tiny C++ dumper (compiled with the system g++, over
the firmware's own headers) prints:
  * src/ui/settings_layout.h's real layout for a 320 x 240 page (status bar, tab row, title row, back
    key, body) and settingsRowRect() for the rows the firmware puts on this page, with the same
    SettingRow list the shell builds for it (Brightness stepper, the Screen auto-off toggle, and the
    Auto-off after row only while the switch is on);
  * the stepper's minus/plus/value rectangles, so the +/- keys sit exactly where the tap does;
  * src/ui/theme.h's real RGB565 colours and the row constants (kSetPadX, kRowH, ...).

Python draws those rectangles with the same colours and the same row shapes screen_settings.cpp's
drawRow() uses, at 2x so the 11 px text is readable. It is NOT a screenshot: no frame was taken from
the device, and the fonts are desktop DejaVu, not the firmware's VLW faces.

Output: docs/shots/2026-11-11-est405-auto-off-rows.png, two panels:
  left  the switch OFF (default): no time row, exactly the page as it was before this ticket;
  right the switch ON: the green ON and the "Auto-off after   1 min" row under it.
"""
import os
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'docs', 'shots', '2026-11-11-est405-auto-off-rows.png')

DUMPER = r'''
#include <stdio.h>
#include <string.h>
#include "ui/layout.h"
#include "ui/settings_layout.h"
#include "ui/theme.h"

using namespace om::ui;

static void addRow(SettingRow* rows, int& n, SetRowKind kind, const char* label, const char* value,
                   bool on) {
  rows[n].kind = kind;
  snprintf(rows[n].label, sizeof(rows[n].label), "%s", label);
  snprintf(rows[n].value, sizeof(rows[n].value), "%s", value);
  rows[n].on = on;
  ++n;
}

int main() {
  const char* names[] = {"kScreen", "kPanel", "kPanel2", "kLine", "kInk", "kDim", "kDimmer", "kAcc",
                         "kAccInk", "kGreen", "kBar", "kRowLine", "kSelBg", "kKeyBg", "kKeyLine"};
  const uint16_t cs[] = {kScreen, kPanel, kPanel2, kLine, kInk, kDim, kDimmer, kAcc,
                         kAccInk, kGreen, kBar, kRowLine, kSelBg, kKeyBg, kKeyLine};
  for (int i = 0; i < (int)(sizeof(cs) / sizeof(cs[0])); ++i) {
    printf("col %s %u\n", names[i], (unsigned)cs[i]);
  }
  printf("num kSetPadX %d\n", (int)kSetPadX);
  printf("num kRowH %d\n", (int)kRowH);
  printf("num kSetStepW %d\n", (int)kSetStepW);
  printf("num kSetStepGap %d\n", (int)kSetStepGap);
  printf("num kSetStepValueW %d\n", (int)kSetStepValueW);
  printf("num kSetBackW %d\n", (int)kSetBackW);
  printf("num kSetBackH %d\n", (int)kSetBackH);

  const SettingsLayout L = settingsLayoutFor(320, 240, false, false, false);
  printf("layout status %d %d %d %d\n", L.status.x, L.status.y, L.status.w, L.status.h);
  printf("layout tabrow %d %d %d %d\n", L.tabrow.x, L.tabrow.y, L.tabrow.w, L.tabrow.h);
  for (int i = 0; i < kSettingsTabCount; ++i) {
    printf("tab %d %d %d %d %d\n", i, L.tabs[i].x, L.tabs[i].y, L.tabs[i].w, L.tabs[i].h);
  }
  printf("layout title %d %d %d %d\n", L.title.x, L.title.y, L.title.w, L.title.h);
  printf("layout back %d %d %d %d\n", L.backButton.x, L.backButton.y, L.backButton.w,
         L.backButton.h);
  printf("layout body %d %d %d %d\n", L.body.x, L.body.y, L.body.w, L.body.h);

  // The two states the ticket asks for. The list is the DISPLAY part of Settings > General, in the
  // order buildSettingsRows() adds it (the rest of the page is above and scrolls away).
  for (int state = 0; state < 2; ++state) {
    const bool on = state == 1;
    SettingRow rows[8];
    int n = 0;
    addRow(rows, n, SetRowKind::Section, "DISPLAY", "", false);
    addRow(rows, n, SetRowKind::Stepper, "Brightness", "160", false);
    addRow(rows, n, SetRowKind::Toggle, "Screen auto-off", "ON", on);
    if (on) addRow(rows, n, SetRowKind::Chevron, "Auto-off after", "1 min", false);
    addRow(rows, n, SetRowKind::Section, "TIME", "", false);
    addRow(rows, n, SetRowKind::Chevron, "Time", "19:26", false);
    for (int i = 0; i < n; ++i) {
      const Rect r = settingsRowRect(L, rows, n, i, 0);
      // '|' between every field: the labels and values contain spaces.
      printf("row|%d|%d|%d|%d|%d|%d|%d|%s|%s|%d\n", state, i, r.x, r.y, r.w, r.h,
             (int)rows[i].kind, rows[i].label, rows[i].value, rows[i].on ? 1 : 0);
      if (rows[i].kind == SetRowKind::Stepper) {
        const Rect m = settingsStepMinusRect(r);
        const Rect p = settingsStepPlusRect(r);
        const Rect v = settingsStepValueRect(r);
        printf("step|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d\n", state, m.x, m.y, m.w, m.h, p.x,
               p.y, p.w, p.h, v.x, v.y, v.w, v.h);
      }
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


def sans(px):
    return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', px)


def mono(px):
    return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', px)


def draw_page(d, ox, oy, s, area, rows, step, C, N):
    """One Settings page at 1 firmware px = s canvas px, offset (ox, oy)."""
    def R(rect):
        return [ox + rect[0] * s, oy + rect[1] * s, ox + (rect[0] + rect[2]) * s - 1,
                oy + (rect[1] + rect[3]) * s - 1]

    # The screen, the status bar, the tab row, the title row and the body: the real rectangles.
    d.rectangle(R(area['status']), fill=C['kBar'])
    d.rectangle([ox, oy + area['status'][3] * s - s, ox + 320 * s - 1,
                 oy + area['status'][3] * s - 1], fill=C['kLine'])
    st = area['status']
    cy = oy + (st[1] + st[3] // 2) * s
    d.text((ox + 4 * s, cy), '19:26', font=mono(10 * s), fill=C['kInk'], anchor='lm')
    d.text((ox + (320 - 6) * s, cy), 'GPS 7   78%', font=mono(10 * s), fill=C['kDim'], anchor='rm')

    tr = area['tabrow']
    d.rectangle(R(tr), fill=C['kBar'])
    d.rectangle([ox, oy + (tr[1] + tr[3]) * s - s, ox + 320 * s - 1,
                 oy + (tr[1] + tr[3]) * s - 1], fill=C['kLine'])
    for i in range(5):
        tab = area['tabs'][i]
        on = i == 0
        if on:
            d.rectangle(R(tab), fill=C['kSelBg'])
        label = ['General', 'Nodes', 'Chat', 'Map', 'RPTR'][i]
        cx = ox + (tab[0] + tab[2] // 2) * s
        ty = oy + (tab[1] + tab[3] // 2 - 1) * s
        d.text((cx, ty), label, font=mono(10 * s), fill=C['kAcc'] if on else C['kDimmer'],
               anchor='mm')
        if on:
            d.rectangle([ox + tab[0] * s, oy + (tab[1] + tab[3] - 2) * s,
                         ox + (tab[0] + tab[2]) * s - 1, oy + (tab[1] + tab[3]) * s - 1],
                        fill=C['kAcc'])

    ti = area['title']
    bk = area['back']
    d.rounded_rectangle(R(bk), radius=3 * s, fill=C['kPanel2'], outline=C['kLine'], width=s)
    bcy = oy + (bk[1] + bk[3] // 2) * s
    x0 = ox + (bk[0] + 7) * s
    d.line([x0, bcy - 3 * s, x0 - 3 * s, bcy], fill=C['kAcc'], width=s)
    d.line([x0 - 3 * s, bcy, x0, bcy + 3 * s], fill=C['kAcc'], width=s)
    d.text((ox + (bk[0] + 12) * s, bcy), 'Back', font=sans(11 * s), fill=C['kDim'], anchor='lm')
    d.text((ox + (bk[0] + bk[2] + 8) * s, oy + (ti[1] + ti[3] // 2) * s), 'Settings',
           font=sans(14 * s), fill=C['kInk'], anchor='lm')
    d.rectangle([ox, oy + (ti[1] + ti[3]) * s - s, ox + 320 * s - 1,
                 oy + (ti[1] + ti[3]) * s - 1], fill=C['kLine'])

    for i, row in enumerate(rows):
        kind, label, value, on = row['kind'], row['label'], row['value'], row['on']
        r = row['rect']
        x, y, w, h = r
        cy = oy + (y + h // 2) * s
        label_x = ox + (x + N['kSetPadX']) * s
        if kind == 0:   # Section
            d.text((label_x, oy + (y + h - 4) * s), label, font=mono(9 * s), fill=C['kDimmer'],
                   anchor='lm')
            continue
        if kind == 4:   # Toggle
            v = value if on else 'OFF'
            d.text((ox + (x + w - N['kSetPadX']) * s, cy), v, font=mono(10 * s),
                   fill=C['kGreen'] if on else C['kDimmer'], anchor='rm')
            d.text((label_x, cy), label, font=sans(11 * s), fill=C['kInk'], anchor='lm')
        elif kind == 3:   # Check (not on this page, drawn for completeness)
            d.text((ox + (x + w - N['kSetPadX'] - 14) * s, cy), value, font=mono(10 * s),
                   fill=C['kDim'], anchor='rm')
            d.text((label_x, cy), label, font=sans(11 * s), fill=C['kInk'], anchor='lm')
            if on:
                d.ellipse([ox + (x + w - N['kSetPadX'] - 6) * s, cy - 3 * s,
                           ox + (x + w - N['kSetPadX']) * s, cy + 3 * s], fill=C['kGreen'])
        elif kind == 2:   # Chevron
            chev = ox + (x + w - N['kSetPadX']) * s
            d.line([chev - 4 * s, cy - 3 * s, chev, cy], fill=C['kDimmer'], width=s)
            d.line([chev, cy, chev - 4 * s, cy + 3 * s], fill=C['kDimmer'], width=s)
            d.text((ox + (x + w - N['kSetPadX'] - 12) * s, cy), value, font=mono(10 * s),
                   fill=C['kDim'], anchor='rm')
            d.text((label_x, cy), label, font=sans(11 * s), fill=C['kInk'], anchor='lm')
        elif kind == 5:   # Stepper
            sp = step
            for key, glyph in ((sp['minus'], '-'), (sp['plus'], '+')):
                kx, ky, kw, kh = key
                d.rounded_rectangle([ox + kx * s, oy + ky * s, ox + (kx + kw) * s - 1,
                                     oy + (ky + kh) * s - 1],
                                    radius=2 * s, fill=C['kKeyBg'], outline=C['kKeyLine'], width=s)
                kcy = oy + (ky + kh // 2) * s
                d.line([ox + (kx + 5) * s, kcy, ox + (kx + kw - 5) * s, kcy], fill=C['kInk'],
                       width=s)
                if glyph == '+':
                    d.line([ox + (kx + kw // 2) * s, kcy - (kh // 2 - 5) * s,
                            ox + (kx + kw // 2) * s, kcy + (kh // 2 - 5) * s], fill=C['kInk'],
                           width=s)
            v = sp['value']
            d.text((ox + (v[0] + v[2]) * s, cy), value, font=mono(10 * s), fill=C['kInk'],
                   anchor='rm')
            d.text((label_x, cy), label, font=sans(11 * s), fill=C['kInk'], anchor='lm')
        # The 1 px row line under everything but a Section.
        d.rectangle([ox + x * s, oy + (y + h) * s - s, ox + (x + w) * s - 1,
                     oy + (y + h) * s - 1], fill=C['kRowLine'])


def main():
    tmp = tempfile.mkdtemp(prefix='est405-')
    src = os.path.join(tmp, 'est405_preview.cpp')
    exe = os.path.join(tmp, 'est405_preview')
    with open(src, 'w', encoding='utf-8') as fh:
        fh.write(DUMPER)
    subprocess.run(['g++', '-std=c++17', '-I', os.path.join(ROOT, 'src'), src, '-o', exe],
                   check=True)
    out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout

    colours, nums, area, states, steps = {}, {}, {}, {}, {}
    tabs = []
    for line in out.splitlines():
        if not line:
            continue
        if line.startswith('row|'):
            f = line.split('|')
            state = int(f[1])
            states.setdefault(state, []).append({
                'rect': (int(f[3]), int(f[4]), int(f[5]), int(f[6])),
                'kind': int(f[7]), 'label': f[8], 'value': f[9], 'on': f[10] == '1'})
            continue
        if line.startswith('step|'):
            f = line.split('|')
            steps[int(f[1])] = {
                'minus': tuple(int(v) for v in f[2:6]),
                'plus': tuple(int(v) for v in f[6:10]),
                'value': tuple(int(v) for v in f[10:14])}
            continue
        p = line.split()
        if p[0] == 'col':
            colours[p[1]] = rgb565(int(p[2]))
        elif p[0] == 'num':
            nums[p[1]] = int(p[2])
        elif p[0] == 'layout':
            area[p[1]] = tuple(int(v) for v in p[2:6])
        elif p[0] == 'tab':
            # "tab <i> <x> <y> <w> <h>": p[2:6] is the rectangle, p[1] the index.
            tabs.append(tuple(int(v) for v in p[2:6]))
        else:
            raise SystemExit('unexpected dumper line: %r' % line)
    area['tabs'] = tabs
    C, N = colours, nums

    s = 2
    pad = 14
    page_w, page_h = 320 * s, 240 * s
    canvas_w = pad * 3 + page_w * 2
    canvas_h = pad + 26 + page_h + 18 + 22 + pad
    img = Image.new('RGB', (canvas_w, canvas_h), (0x13, 0x12, 0x10))
    d = ImageDraw.Draw(img)
    d.text((pad, pad), 'EST-405: Settings > General, DISPLAY, "Screen auto-off" (2x)',
           font=sans(15), fill=C['kInk'])
    labels = ['switch OFF (default): no time row', 'switch ON: the "Auto-off after" row appears']
    for state in (0, 1):
        ox = pad + state * (page_w + pad)
        oy = pad + 26
        # The page's own background first (the whole screen).
        d.rectangle([ox, oy, ox + page_w - 1, oy + page_h - 1], fill=C['kScreen'])
        draw_page(d, ox, oy, s, area, states[state], steps.get(state, {}), C, N)
        d.text((ox, oy + page_h + 6), labels[state], font=sans(12), fill=C['kDim'])
    d.text((pad, canvas_h - pad - 12),
           'host render: real rectangles from ui/settings_layout.h and colours from ui/theme.h. '
           'DejaVu text, not the VLW faces; the status and tab bars are context only.',
           font=sans(11), fill=(0x5f, 0x6b, 0x7d))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT)
    print(OUT)


if __name__ == '__main__':
    main()
