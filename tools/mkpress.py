#!/usr/bin/env python3
"""EST-390 preview: the pressed look and the key preview bubble, 1:1 at 320x240.

Nothing here invents a coordinate or a colour. A tiny C++ dumper (compiled with the system g++ over
the firmware's own headers) prints the REAL rectangles from ui/chat_layout.h / ui/layout.h and the
REAL theme values from ui/theme.h, and the pressed fill is the firmware's own pressBlend() arithmetic
(theme.h kPressedTint / kPressedAlpha). Python only fills those rectangles and puts text in them with
the same DejaVu family the panel's faces were generated from.

Output: docs/shots/2026-11-11-est390-touch-feedback.png
  left  panel: the chat keyboard sheet with "k" held: pressed key + the character bubble above it,
               clear of the draft line that shows what is being typed;
  right panel: the Nodes list with the second row held: the one accent tint on the row's own rect.
"""
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'docs', 'shots', '2026-11-11-est390-touch-feedback.png')

DUMPER = r'''
#include <stdio.h>
#include <string.h>
#include "ui/chat_layout.h"
#include "ui/layout.h"
#include "ui/theme.h"
using namespace om::ui;

static void row(const char* name, const Rect& r) {
  printf("%s %d %d %d %d\n", name, r.x, r.y, r.w, r.h);
}
static void col(const char* name, uint16_t c) { printf("%s %u\n", name, (unsigned)c); }

int main() {
  const Layout& L = l2();
  row("status", L.status);
  row("content", L.content);
  row("tabbar", L.tabbar);
  for (int i = 0; i < kTabCount; ++i) {
    char n[16];
    snprintf(n, sizeof(n), "tab%d", i);
    row(n, L.tab((Tab)i));
  }
  row("gear", L.gear());
  row("head", L.listHeader());
  row("listbody", L.listBody());
  for (int i = 0; i < 4; ++i) {
    char n[16];
    snprintf(n, sizeof(n), "row%d", i);
    row(n, L.rowVisible(i, 0));
  }
  row("pressedrow", L.rowVisible(1, 0));

  const ChatLayout g = chatLayoutFor(L, true, false, false, false);
  row("sheet", g.sheet);
  row("draft", g.draft);
  row("msgs", g.msgs);
  // The held key: the one whose label is 'k' on the letters page.
  for (int r = 0; r < 4; ++r) {
    KeyDef defs[10];
    const int n = keyRow(false, r, defs);
    for (int c = 0; c < n; ++c) {
      if (defs[c].kind == KeyKind::Char && defs[c].label[0] == 'k') {
        row("pressedkey", g.keys[r][c]);        // the drawn cap
        row("pressedkeyhit", g.keyHit[r][c]);   // the tap target the tint covers
        printf("pressedcp %u\n", (unsigned)(unsigned char)defs[c].label[0]);
        row("bubble", pressBubbleRect(g.keys[r][c], g.draft.y));
      }
    }
  }
  // Every key of the sheet, for the drawing.
  for (int r = 0; r < 4; ++r) {
    KeyDef defs[10];
    const int n = keyRow(false, r, defs);
    for (int c = 0; c < n; ++c) {
      char name[24];
      snprintf(name, sizeof(name), "key.%d.%d", r, c);
      row(name, g.keys[r][c]);
      printf("keydef.%d.%d %d %s\n", r, c, (int)defs[c].kind, defs[c].label);
    }
  }

  col("kBg", kBg);
  col("kScreen", kScreen);
  col("kPanel2", kPanel2);
  col("kBar", kBar);
  col("kLine", kLine);
  col("kRowLine", kRowLine);
  col("kInk", kInk);
  col("kDim", kDim);
  col("kDimmer", kDimmer);
  col("kAcc", kAcc);
  col("kSelBg", kSelBg);
  col("kGreen", kGreen);
  col("kAccInk", kAccInk);
  col("kKeyBg", kKeyBg);
  col("kKeyLine", kKeyLine);
  col("kSheet", kSheet);
  col("kFieldBg", kFieldBg);
  col("pressedKey", pressBlend(kPressedTint, kKeyBg, kPressedAlpha));
  col("pressedRow", pressBlend(kPressedTint, kScreen, kPressedAlpha));
  col("pressedTab", pressBlend(kPressedTint, kSelBg, kPressedAlpha));
  return 0;
}
'''

SOURCES = ['src/ui/chat_layout.cpp', 'src/ui/text_wrap.cpp', 'src/rptr_model.cpp',
           'src/timekeeper.cpp']


def geometry():
    src = '/tmp/est390_preview_geo.cpp'
    with open(src, 'w') as f:
        f.write('#include "ui/press.h"\n' + DUMPER)
    cmd = ['g++', '-std=c++17', '-O1', '-I', os.path.join(ROOT, 'src'), src]
    cmd += [os.path.join(ROOT, s) for s in SOURCES]
    cmd += ['-o', '/tmp/est390_preview_geo']
    subprocess.run(cmd, check=True)
    out = subprocess.run(['/tmp/est390_preview_geo'], check=True, capture_output=True, text=True)
    rects, cols, defs = {}, {}, {}
    for line in out.stdout.splitlines():
        p = line.split()
        if not p:
            continue
        if len(p) == 5:
            rects[p[0]] = tuple(int(v) for v in p[1:])
        elif len(p) == 2 and not p[0].startswith('keydef'):
            cols[p[0]] = int(p[1])
        elif p[0].startswith('keydef.'):
            _, r, c = p[0].split('.')
            defs[(int(r), int(c))] = (int(p[1]), p[2] if len(p) > 2 else '')
        elif p[0] == 'pressedcp':
            cols['pressedcp'] = int(p[1])
    return rects, cols, defs


def rgb565(v):
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def font(px, mono=False):
    name = 'DejaVuSansMono.ttf' if mono else 'DejaVuSans.ttf'
    return ImageFont.truetype(os.path.join('/usr/share/fonts/truetype/dejavu', name), px)


def centred(d, box, text, f, fill):
    x, y, w, h = box
    if not text:
        return
    tw = d.textlength(text, font=f)
    d.text((x + (w - tw) / 2, y + h / 2), text, font=f, fill=fill, anchor='lm')


def draw_status(d, R, C):
    x, y, w, h = R['status']
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=rgb565(C['kBar']))
    d.line([x, y + h - 1, x + w - 1, y + h - 1], fill=rgb565(C['kLine']))
    d.text((x + 6, y + h // 2), '11:24', font=font(10, True), fill=rgb565(C['kInk']), anchor='lm')
    # battery + gear, plainly
    bx = x + w - 6 - 13
    d.rectangle([bx, y + 9, bx + 12, y + 16], outline=rgb565(C['kDim']))
    d.rectangle([bx + 1, y + 10, bx + 9, y + 15], fill=rgb565(C['kGreen']))
    gx, gy, gw, gh = R['gear']
    d.rectangle([gx, gy, gx + gw - 1, gy + gh - 1], outline=rgb565(C['kDim']))


def draw_tabbar(d, R, C, active):
    x, y, w, h = R['tabbar']
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=rgb565(C['kBar']))
    d.line([x, y, x + w - 1, y], fill=rgb565(C['kLine']))
    labels = ['Nodes', 'Chat', 'Map', 'RPTR']
    for i, name in enumerate(labels):
        tx, ty, tw_, th = R[f'tab{i}']
        if i == active:
            d.rectangle([tx, ty, tx + tw_ - 1, ty + th - 1], fill=rgb565(C['kSelBg']))
            d.rectangle([tx, ty, tx + tw_ - 1, ty + 2], fill=rgb565(C['kAcc']))
        fg = rgb565(C['kAcc']) if i == active else rgb565(C['kDim'])
        centred(d, (tx, ty + 4, tw_, th - 4), name, font(9), fg)


def panel_keyboard(R, C, defs):
    img = Image.new('RGB', (320, 240), rgb565(C['kScreen']))
    d = ImageDraw.Draw(img)
    draw_status(d, R, C)
    # the message band is not the point of this panel: leave it dark, one dim line of context
    mx, my, mw, mh = R['msgs']
    d.text((mx + 7, my + 8), 'Alice   hello', font=font(11), fill=rgb565(C['kDim']))
    # the sheet
    x, y, w, h = R['sheet']
    d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=6, fill=rgb565(C['kSheet']))
    # the draft line (what is being typed): the bubble must never cover it
    dx, dy, dw, dh = R['draft']
    d.rounded_rectangle([dx, dy, dx + dw - 1, dy + dh - 1], radius=3,
                        fill=rgb565(C['kFieldBg']), outline=rgb565(C['kLine']))
    d.text((dx + 5, dy + dh // 2), 'helo|', font=font(11, True), fill=rgb565(C['kInk']), anchor='lm')
    # the keys
    for (r, c), (kind, label) in defs.items():
        kx, ky, kw, kh = R[f'key.{r}.{c}']
        d.rounded_rectangle([kx, ky, kx + kw - 1, ky + kh - 1], radius=3,
                            fill=rgb565(C['kKeyBg']), outline=rgb565(C['kKeyLine']))
        if kind == 0:
            centred(d, (kx, ky, kw, kh), label, font(10, True), rgb565(C['kInk']))
        elif kind == 2:
            centred(d, (kx, ky, kw, kh), '123', font(10, True), rgb565(C['kInk']))
        elif kind == 3:
            centred(d, (kx, ky, kw, kh), 'space', font(10, True), rgb565(C['kInk']))
        elif kind == 5:
            d.rounded_rectangle([kx, ky, kx + kw - 1, ky + kh - 1], radius=3,
                                fill=rgb565(C['kSelBg']))
            centred(d, (kx, ky, kw, kh), 'Send', font(10, True), rgb565(C['kAccInk'])
                    if 'kAccInk' in C else rgb565(C['kAcc']))
        elif kind == 4:   # backspace: a vector, drawn here as the word
            centred(d, (kx, ky, kw, kh), 'del', font(9, True), rgb565(C['kInk']))
        elif kind == 6:
            centred(d, (kx, ky, kw, kh), ':)', font(10), rgb565(C['kInk']))
        elif kind == 1:
            centred(d, (kx, ky, kw, kh), 'shift', font(9, True), rgb565(C['kInk']))
    # the pressed key: the tint on the TAP TARGET (keyHit), exactly what the overlay paints
    hx, hy, hw, hh = R['pressedkeyhit']
    d.rectangle([hx, hy, hx + hw - 1, hy + hh - 1], fill=rgb565(C['pressedKey']))
    kx, ky, kw, kh = R['pressedkey']
    d.rounded_rectangle([kx, ky, kx + kw - 1, ky + kh - 1], radius=3,
                        fill=rgb565(C['pressedKey']), outline=rgb565(C['kAcc']))
    centred(d, (kx, ky, kw, kh), 'k', font(10, True), rgb565(C['kInk']))
    # the preview bubble (drawPreviewBubble: panel2 plate, gold rim, title face)
    bx, by, bw, bh = R['bubble']
    d.rounded_rectangle([bx, by, bx + bw - 1, by + bh - 1], radius=6,
                        fill=rgb565(C['kPanel2']), outline=rgb565(C['kAcc']))
    centred(d, (bx, by, bw, bh), chr(C['pressedcp']), font(14), rgb565(C['kInk']))
    return img


def panel_nodes(R, C):
    img = Image.new('RGB', (320, 240), rgb565(C['kScreen']))
    d = ImageDraw.Draw(img)
    draw_status(d, R, C)
    hx, hy, hw, hh = R['head']
    d.text((hx + 7, hy + hh // 2), '4 nodes heard', font=font(10, True),
           fill=rgb565(C['kDim']), anchor='lm')
    names = ['Alice', 'Bob', 'Rptr Hill', 'Sensor 4']
    ages = ['11:20', '11:12', '10:58', '10:41']
    types = [rgb565(C['kDim']), rgb565(C['kGreen']), rgb565(C['kGreen']), rgb565(C['kDimmer'])]
    for i in range(4):
        rx, ry, rw, rh = R[f'row{i}']
        if rh <= 0:
            continue
        if i == 1:
            d.rectangle([rx, ry, rx + rw - 1, ry + rh - 1], fill=rgb565(C['pressedRow']))
        d.ellipse([rx + 7, ry + rh // 2 - 3, rx + 13, ry + rh // 2 + 3], fill=types[i])
        d.text((rx + 22, ry + rh // 2), names[i], font=font(11),
               fill=rgb565(C['kInk']), anchor='lm')
        tw = d.textlength(ages[i], font=font(10, True))
        d.text((rx + rw - 7 - tw, ry + rh // 2), ages[i], font=font(10, True),
               fill=rgb565(C['kDim']), anchor='lm')
        d.line([rx, ry + rh - 1, rx + rw - 1, ry + rh - 1], fill=rgb565(C['kRowLine']))
    draw_tabbar(d, R, C, 0)
    return img


def main():
    R, C, defs = geometry()
    left = panel_keyboard(R, C, defs)
    right = panel_nodes(R, C)
    out = Image.new('RGB', (320 * 2 + 8, 240), (12, 11, 9))
    out.paste(left, (0, 0))
    out.paste(right, (328, 0))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    out.save(OUT)
    print('wrote', OUT)


if __name__ == '__main__':
    sys.exit(main())
