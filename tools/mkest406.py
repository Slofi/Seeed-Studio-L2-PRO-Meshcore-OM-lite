#!/usr/bin/env python3
"""EST-406 preview: Send moved from the keyboard onto the message input line.

Nothing here invents a rectangle or a colour. A tiny C++ dumper (compiled with the system g++, over
the firmware's own headers) prints:
  * src/ui/chat_layout.h's real `chatLayoutFor()` for the open chat sheet: the header, the message
    band, the sheet, the draft plate, the new 30 x 26 send button and every key rect of the letters
    page, together with `keyRow()`'s labels and kinds (so the key the shell hits is the key drawn);
  * src/ui/theme.h's real RGB565 colours and the sizes the drawing uses (key radius, status bar).

Python draws those rectangles with the same shapes screen_chat.cpp's drawSheet()/drawSendButton()/
drawDraft() and ui/widgets.cpp's drawKeyCap() use, at 2x so the 10 px text is readable. The status
bar, header and message band are context only; the focus is the composer line and the keyboard.

It is NOT a screenshot: no frame was taken from the device, and the fonts are desktop DejaVu, not
the firmware's VLW faces.

Output: docs/shots/2026-11-11-est406-send-button.png, two panels:
  left  empty draft: the send arrow is DIM (no action; a tap does nothing)
  right "hello": the send arrow is BRIGHT (a tap calls the one chatSendDraft())
"""
import os
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'docs', 'shots', '2026-11-11-est406-send-button.png')

DUMPER = r'''
#include <stdio.h>
#include "ui/layout.h"
#include "ui/chat_layout.h"
#include "ui/theme.h"

using namespace om::ui;

static void colour(const char* name, uint16_t v) { printf("col %s %u\n", name, (unsigned)v); }

int main() {
  colour("kScreen", kScreen);
  colour("kBar", kBar);
  colour("kLine", kLine);
  colour("kSheet", kSheet);
  colour("kKeyBg", kKeyBg);
  colour("kKeyLine", kKeyLine);
  colour("kInk", kInk);
  colour("kDim", kDim);
  colour("kDimmer", kDimmer);
  colour("kAcc", kAcc);
  colour("kAccInk", kAccInk);
  colour("kSelBg2", kSelBg2);
  colour("kPanel2", kPanel2);
  colour("kCodeBg", kCodeBg);
  colour("kRowLine", kRowLine);
  colour("kGreen", kGreen);

  printf("num kStatusH %d\n", (int)kStatusH);
  printf("num kChatHeadH %d\n", (int)kChatHeadH);
  printf("num kKeyRadius %d\n", (int)kKeyRadius);
  printf("num kOskRadius %d\n", (int)kOskRadius);
  printf("num kFieldPadX %d\n", (int)kFieldPadX);
  printf("num kCaretW %d\n", (int)kCaretW);
  printf("num kCaretH %d\n", (int)kCaretH);
  printf("num kSendBtnW %d\n", (int)kSendBtnW);
  printf("num kSendBtnH %d\n", (int)kSendBtnH);

  const Layout& L = l2();
  printf("rect status %d %d %d %d\n", L.status.x, L.status.y, L.status.w, L.status.h);
  printf("rect tabbar %d %d %d %d\n", L.tabbar.x, L.tabbar.y, L.tabbar.w, L.tabbar.h);
  const ChatLayout g = chatLayoutFor(L, true, false, false, false);
  printf("rect head %d %d %d %d\n", g.head.x, g.head.y, g.head.w, g.head.h);
  printf("rect msgs %d %d %d %d\n", g.msgs.x, g.msgs.y, g.msgs.w, g.msgs.h);
  printf("rect sheet %d %d %d %d\n", g.sheet.x, g.sheet.y, g.sheet.w, g.sheet.h);
  printf("rect draft %d %d %d %d\n", g.draft.x, g.draft.y, g.draft.w, g.draft.h);
  printf("rect send %d %d %d %d\n", g.sendBtn.x, g.sendBtn.y, g.sendBtn.w, g.sendBtn.h);

  for (int r = 0; r < 4; ++r) {
    KeyDef defs[10];
    const int n = keyRow(false, r, defs);
    for (int c = 0; c < n && c < g.keyCount[r]; ++c) {
      const Rect& k = g.keys[r][c];
      printf("key|%d|%d|%d|%d|%d|%d|%d|%s\n", r, c, k.x, k.y, k.w, k.h, (int)defs[c].kind,
             defs[c].label);
    }
  }
  return 0;
}
'''

# KeyKind from ui/chat_layout.h
KIND_SHIFT, KIND_MODE, KIND_SPACE, KIND_DEL, KIND_EMOJI = 1, 2, 3, 4, 6


def rgb565(v):
    r = ((v >> 11) & 0x1F) * 255 // 31
    g = ((v >> 5) & 0x3F) * 255 // 63
    b = (v & 0x1F) * 255 // 31
    return (r, g, b)


def sans(px):
    return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', px)


def mono(px):
    return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', px)


def draw_key(d, ox, oy, s, k, kind, label, C, N):
    """One key cap, the shapes ui/widgets.cpp drawKeyCap() uses."""
    x, y, w, h = k
    x0, y0 = ox + x * s, oy + y * s
    x1, y1 = ox + (x + w) * s - 1, oy + (y + h) * s - 1
    d.rounded_rectangle([x0, y0, x1, y1], radius=N['kKeyRadius'] * s, fill=C['kKeyBg'],
                        outline=C['kKeyLine'], width=s)
    cx = (x0 + x1) / 2
    cy = (y0 + y1) / 2
    if kind == KIND_DEL:
        # The vector backspace: an outlined pentagon and its X, as drawKeyCap draws it.
        hw, hh = 4 * s, 4 * s
        lw = max(1, s)
        d.line([cx + hw, cy - hh, cx - hw, cy - hh], fill=C['kInk'], width=lw)
        d.line([cx - hw, cy - hh, cx - hw - 3 * s, cy], fill=C['kInk'], width=lw)
        d.line([cx - hw - 3 * s, cy, cx - hw, cy + hh], fill=C['kInk'], width=lw)
        d.line([cx - hw, cy + hh, cx + hw, cy + hh], fill=C['kInk'], width=lw)
        d.line([cx + hw, cy + hh, cx + hw, cy - hh], fill=C['kInk'], width=lw)
        d.line([cx - 3 * s, cy - 2 * s, cx + s, cy + 2 * s], fill=C['kInk'], width=lw)
        d.line([cx + s, cy - 2 * s, cx - 3 * s, cy + 2 * s], fill=C['kInk'], width=lw)
        return
    if kind == KIND_EMOJI:
        # The vector smiley: a ring, two eyes, the mouth line.
        rad = 5 * s
        for yy in range(-rad, rad + 1):
            for xx in range(-rad, rad + 1):
                dd = xx * xx + yy * yy
                if (rad - s) * (rad - s) <= dd <= rad * rad:
                    d.point((cx + xx, cy + yy), fill=C['kInk'])
        d.ellipse([cx - 2 * s - s, cy - s - s, cx - 2 * s + s, cy - s + s], fill=C['kInk'])
        d.ellipse([cx + 2 * s - s, cy - s - s, cx + 2 * s + s, cy - s + s], fill=C['kInk'])
        d.line([cx - 2 * s, cy + 2 * s, cx, cy + 3 * s], fill=C['kInk'], width=s)
        d.line([cx, cy + 3 * s, cx + 2 * s, cy + 2 * s], fill=C['kInk'], width=s)
        return
    d.text((cx, cy), label, font=mono(10 * s), fill=C['kInk'], anchor='mm')


def draw_send_button(d, ox, oy, s, r, enabled, C, N):
    """screen_chat.cpp drawSendButton(): plate, edge and the send arrow, from primitives."""
    x, y, w, h = r
    x0, y0 = ox + x * s, oy + y * s
    x1, y1 = ox + (x + w) * s - 1, oy + (y + h) * s - 1
    bg = C['kSelBg2'] if enabled else C['kKeyBg']
    edge = C['kAcc'] if enabled else C['kKeyLine']
    ink = C['kAccInk'] if enabled else C['kDimmer']
    d.rounded_rectangle([x0, y0, x1, y1], radius=N['kKeyRadius'] * s, fill=bg, outline=edge,
                        width=s)
    cx = (x0 + x1) // 2
    cy = (y0 + y1) // 2
    # a 2 px shaft and a solid triangular head, exactly the two fill calls in drawSendButton()
    d.rectangle([cx - 6 * s, cy - s, cx + 2 * s - 1, cy + s - 1], fill=ink)
    d.polygon([(cx + 6 * s, cy), (cx - s, cy - 5 * s), (cx - s, cy + 5 * s)], fill=ink)


def draw_panel(d, ox, oy, s, R, keys, C, N, draft_text):
    def P(rect):
        x, y, w, h = rect
        return [ox + x * s, oy + y * s, ox + (x + w) * s - 1, oy + (y + h) * s - 1]

    # The screen, then the status bar (context only).
    d.rectangle(P((0, 0, 320, 240)), fill=C['kScreen'])
    st = R['status']
    d.rectangle(P(st), fill=C['kBar'])
    st_cy = oy + (st[1] + st[3] // 2) * s
    d.text((ox + 4 * s, st_cy), '19:26', font=mono(10 * s), fill=C['kInk'], anchor='lm')
    d.text((ox + (320 - 6) * s, st_cy), 'GPS 7   78%', font=mono(10 * s), fill=C['kDim'],
           anchor='rm')
    d.rectangle([ox, oy + (st[1] + st[3]) * s - s, ox + 320 * s - 1, oy + (st[1] + st[3]) * s - 1],
                fill=C['kLine'])

    # The chat header: the open chat's name left, the ADVERT chip right (context).
    hd = R['head']
    d.rectangle(P(hd), fill=C['kScreen'])
    hcy = oy + (hd[1] + hd[3] // 2) * s
    d.text((ox + (hd[0] + 8) * s, hcy), 'Camp', font=sans(11 * s), fill=C['kInk'], anchor='lm')
    adv = [ox + (hd[0] + hd[2] - 8 - 62) * s, oy + (hd[1] + 1) * s,
           ox + (hd[0] + hd[2] - 8) * s - 1, oy + (hd[1] + hd[3] - 2) * s - 1]
    d.rounded_rectangle(adv, radius=N['kKeyRadius'] * s, fill=C['kPanel2'], outline=C['kLine'],
                        width=s)
    d.text(((adv[0] + adv[2]) / 2, hcy), 'ADVERT', font=mono(9 * s), fill=C['kAcc'], anchor='mm')
    d.rectangle([ox, oy + (hd[1] + hd[3]) * s - s, ox + 320 * s - 1,
                 oy + (hd[1] + hd[3]) * s - 1], fill=C['kLine'])

    # The message band: invented names and texts (rule 9), context only.
    m = R['msgs']
    d.rectangle(P(m), fill=C['kScreen'])
    line_h = 14 * s
    y = oy + (m[1] + 5) * s
    for stamp, name, text in (('19:24', 'Alice', 'hello'), ('19:25', 'me', 'ok, on my way')):
        d.text((ox + (m[0] + 5) * s, y + line_h // 2), stamp, font=mono(9 * s), fill=C['kDimmer'],
               anchor='lm')
        d.text((ox + (m[0] + 5 + 36) * s, y + line_h // 2), name, font=mono(9 * s),
               fill=C['kGreen'] if name == 'me' else C['kAcc'], anchor='lm')
        d.text((ox + (m[0] + 5 + 36 + 46) * s, y + line_h // 2), text, font=sans(10 * s),
               fill=C['kInk'], anchor='lm')
        y += line_h

    # The sheet: rounded at the top only (the body runs past the screen's floor).
    sh = R['sheet']
    d.rounded_rectangle([ox + sh[0] * s, oy + sh[1] * s, ox + (sh[0] + sh[2]) * s - 1,
                         oy + (sh[1] + sh[3] + N['kOskRadius']) * s - 1],
                        radius=N['kOskRadius'] * s, fill=C['kSheet'])
    d.rectangle([ox + sh[0] * s, oy + (sh[1] + sh[3]) * s - 1, ox + (sh[0] + sh[2]) * s - 1,
                 oy + (sh[1] + sh[3]) * s - 1], fill=C['kSheet'])
    d.line([ox + sh[0] * s, oy + sh[1] * s, ox + (sh[0] + sh[2]) * s - 1, oy + sh[1] * s],
           fill=C['kLine'], width=s)

    # The draft plate and its text (drawSheet + drawDraft with focus true).
    dr = R['draft']
    d.rounded_rectangle(P(dr), radius=N['kKeyRadius'] * s, fill=C['kCodeBg'], outline=C['kAcc'],
                        width=s)
    dcy = oy + (dr[1] + dr[3] // 2) * s
    tx = ox + (dr[0] + N['kFieldPadX']) * s
    if draft_text:
        d.text((tx, dcy), draft_text, font=mono(10 * s), fill=C['kInk'], anchor='lm')
        tw = d.textlength(draft_text, font=mono(10 * s))
        d.rectangle([tx + tw + s, dcy - N['kCaretH'] * s // 2, tx + tw + N['kCaretW'] * s,
                     dcy + N['kCaretH'] * s // 2], fill=C['kAcc'])
    else:
        d.text((tx, dcy), 'message', font=mono(10 * s), fill=C['kDimmer'], anchor='lm')
        d.rectangle([tx, dcy - N['kCaretH'] * s // 2, tx + N['kCaretW'] * s - 1,
                     dcy + N['kCaretH'] * s // 2], fill=C['kAcc'])

    # EST-406: the one send button, dim with no draft, bright with text.
    draw_send_button(d, ox, oy, s, R['send'], bool(draft_text.strip()), C, N)

    for r, c, kx, ky, kw, kh, kind, label in keys:
        draw_key(d, ox, oy, s, (kx, ky, kw, kh), kind, label, C, N)


def main():
    tmp = tempfile.mkdtemp(prefix='est406-')
    src = os.path.join(tmp, 'est406_preview.cpp')
    exe = os.path.join(tmp, 'est406_preview')
    with open(src, 'w', encoding='utf-8') as fh:
        fh.write(DUMPER)
    subprocess.run(['g++', '-std=c++17', '-I', os.path.join(ROOT, 'src'), src,
                    os.path.join(ROOT, 'src', 'ui', 'chat_layout.cpp'), '-o', exe], check=True)
    out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout

    colours, nums, rects, keys = {}, {}, {}, []
    for line in out.splitlines():
        if not line:
            continue
        if line.startswith('key|'):
            f = line.split('|')
            keys.append((int(f[1]), int(f[2]), int(f[3]), int(f[4]), int(f[5]), int(f[6]),
                         int(f[7]), f[8]))
            continue
        p = line.split()
        if p[0] == 'col':
            colours[p[1]] = rgb565(int(p[2]))
        elif p[0] == 'num':
            nums[p[1]] = int(p[2])
        elif p[0] == 'rect':
            rects[p[1]] = tuple(int(v) for v in p[2:6])
        else:
            raise SystemExit('unexpected dumper line: %r' % line)
    C, N, R = colours, nums, rects
    assert R['send'][2] == N['kSendBtnW'] and R['send'][3] == N['kSendBtnH']

    s = 2
    pad = 14
    page_w, page_h = 320 * s, 240 * s
    canvas_w = pad * 3 + page_w * 2
    canvas_h = pad + 26 + page_h + 20 + 22 + pad
    img = Image.new('RGB', (canvas_w, canvas_h), (0x13, 0x12, 0x10))
    d = ImageDraw.Draw(img)
    d.text((pad, pad), 'EST-406: Send sits on the message input line, the keyboard keeps no Send (2x)',
           font=sans(15), fill=C['kInk'])
    labels = ['empty draft: dim arrow, a tap does nothing', 'text "hello": bright arrow, a tap sends']
    for i, text in enumerate(('', 'hello')):
        ox = pad + i * (page_w + pad)
        oy = pad + 26
        draw_panel(d, ox, oy, s, R, keys, C, N, text)
        d.text((ox, oy + page_h + 6), labels[i], font=sans(12),
               fill=C['kDim'] if i == 0 else C['kAcc'])
    d.text((pad, canvas_h - pad - 12),
           'host render: real rects from ui/chat_layout.h (send 30 x 26, the same rect draws and '
           'hits) and colours from ui/theme.h. DejaVu text, not the VLW faces; status bar, header '
           'and messages are context only.',
           font=sans(11), fill=(0x5f, 0x6b, 0x7d))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT)
    print(OUT)


if __name__ == '__main__':
    main()
