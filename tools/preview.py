#!/usr/bin/env python3
"""ONE preview tool for the OM Lite firmware screens (EST-415 de-clutter).

The per-ticket generators tools/mkest400/402/404/405/406/414.py are folded into this file; git keeps
their history. Each renderer still does what its ticket's script did: a small C++ dumper compiled with
the system g++ over the firmware's own headers prints the real rectangles and the theme's real RGB565
colours, then Python fills them with PIL. These are host renders, NOT screenshots: no frame comes off
the device and the text is desktop DejaVu, not the firmware's VLW faces. All names and texts shown are
invented (public-repo rule 9).

Call it as:
    python3 tools/preview.py                 # render every preview
    python3 tools/preview.py est414          # one preview (names below)
    python3 tools/preview.py est400 est405   # any set
    PREVIEW_OUT=/tmp/png python3 tools/preview.py   # write elsewhere (default docs/shots)

Renderers and their output:
    est400  docs/shots/2026-11-11-est400-polish.png             toast / warn / Info page clip
    est402  docs/shots/2026-11-11-est402-charging-bolt.png      status bar, battery bolt
    est404  docs/shots/2026-11-11-est404-bolt-tips.png          bolt over the icon, 4 states
    est405  docs/shots/2026-11-11-est405-auto-off-rows.png      Settings > General display rows
    est406  docs/shots/2026-11-11-est406-send-button.png        send key on the composer line
    est414  docs/shots/2026-11-12-est414-scroll-indicator.png   indicator on the three lists
"""
import os
import sys


def _out_dir():
    """docs/shots, or PREVIEW_OUT when a dry run must not touch the committed previews."""
    return os.environ.get('PREVIEW_OUT') or os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                         '..', 'docs', 'shots')


def _render_est400():
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
    OUT = os.path.join(_out_dir(), '2026-11-11-est400-polish.png')
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
    main()

def _render_est402():
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
    OUT = os.path.join(_out_dir(), '2026-11-11-est402-charging-bolt.png')
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
    main()

def _render_est404():
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
    OUT = os.path.join(_out_dir(), '2026-11-11-est404-bolt-tips.png')

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
    main()

def _render_est405():
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
    OUT = os.path.join(_out_dir(), '2026-11-11-est405-auto-off-rows.png')

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
    main()

def _render_est406():
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
    OUT = os.path.join(_out_dir(), '2026-11-11-est406-send-button.png')

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
    main()

def _render_est414():
    """EST-414 preview: the scroll indicator on the three lists Filip named, from the firmware's own geometry.

    Nothing here invents a rectangle or a colour. A tiny C++ dumper (system g++, over the firmware's own
    headers) prints:
      * src/ui/theme.h's real RGB565 colours and the row/indicator constants;
      * src/ui/layout.h's 320 x 240 split (status bar, content, tab bar, the Nodes header and list body);
        one Nodes row per visible index at scroll 0;
      * src/ui/chat_layout.h's ChatLayout (header, ADVERT key, message band, compose line) and the open
        selector (panel, list, pinned Manage row) plus one selector row's own layout per entry;
      * the message spans of the open chat (msgSpans at scroll 0);
      * the three ScrollIndicator results scrollIndicatorFor() gives for those rectangles, so the track
        and thumb drawn here are the ones the firmware draws.

    Python draws them with screen_settings/chat/widgets' shapes and the same colours, at 2x so the 11 px
    text is readable. It is NOT a screenshot: no frame came off the device, and the fonts are desktop
    DejaVu, not the firmware's VLW faces. The names and texts are invented (rule 9).

    Output: docs/shots/2026-11-12-est414-scroll-indicator.png, three panels:
      left   the Nodes list with the indicator (20 nodes, so it scrolls);
      middle the Chat tab's channel list (6 channels, 2 DMs) with the indicator;
      right  an open chat at rest, where the bottom-anchored thumb sits at the bottom.
    """
    import os
    import subprocess
    import tempfile

    from PIL import Image, ImageDraw, ImageFont

    ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
    OUT = os.path.join(_out_dir(), '2026-11-12-est414-scroll-indicator.png')

    DUMPER = r'''
    #include <stdio.h>
    #include <string.h>

    #include "ui/chat_layout.h"
    #include "ui/layout.h"
    #include "ui/scroll_indicator.h"
    #include "ui/theme.h"

    using namespace om::ui;

    static void col(const char* name, uint16_t v) { printf("col %s %u\n", name, (unsigned)v); }
    static void num(const char* name, int v) { printf("num %s %d\n", name, v); }
    static void rect(const char* name, const Rect& r) {
      printf("rect %s %d %d %d %d\n", name, r.x, r.y, r.w, r.h);
    }

    int main() {
      col("kScreen", kScreen);
      col("kPanel", kPanel);
      col("kPanel2", kPanel2);
      col("kLine", kLine);
      col("kInk", kInk);
      col("kDim", kDim);
      col("kDimmer", kDimmer);
      col("kAcc", kAcc);
      col("kAccInk", kAccInk);
      col("kBar", kBar);
      col("kRowLine", kRowLine);
      col("kSelBg", kSelBg);
      col("kFieldBg", kFieldBg);
      col("kClient", kClient);
      col("kGreen", kGreen);
      col("kWarn", kWarn);
      col("kScrollTrack", kScrollTrack);
      col("kScrollThumb", kScrollThumb);
      num("kRowH", kRowH);
      num("kRowPadX", kRowPadX);
      num("kTypeDot", kTypeDot);
      num("kRowGap", kRowGap);
      num("kStatusH", kStatusH);
      num("kTabH", kTabH);
      num("kSelPadX", kSelPadX);
      num("kSelSectionH", kSelSectionH);
      num("kSelRowH", kSelRowH);
      num("kSelEmptyH", kSelEmptyH);
      num("kSelManageH", kSelManageH);
      num("kSelDot", kSelDot);
      num("kSelLine1Cy", kSelLine1Cy);
      num("kSelLine2Cy", kSelLine2Cy);
      num("kMsgPadX", kMsgPadX);
      num("kMsgPadY", kMsgPadY);
      num("kMsgGap", kMsgGap);
      num("kMsgLineH", kMsgLineH);
      num("kMsgHdrGap", kMsgHdrGap);
      num("kMsgStampW", kMsgStampW);
      num("kChatHeadH", kChatHeadH);
      num("kHeadPadX", kHeadPadX);
      num("kComposeH", kComposeH);
      num("kComposePadX", kComposePadX);
      num("kFieldH", kFieldH);
      num("kFieldPadX", kFieldPadX);
      num("kScrollTrackW", kScrollTrackW);

      const Layout& L = l2();
      rect("status", L.status);
      rect("content", L.content);
      rect("tabbar", L.tabbar);
      for (int i = 0; i < kTabCount; ++i) rect("tab", L.tab((Tab)i));
      rect("nodesHead", L.listHeader());
      rect("nodesBody", L.listBody());

      // --- 1. the Nodes list: 20 nodes, 7 visible rows at scroll 0 --------------------------------
      const int nodes = 20;
      for (int i = 0; i < 7; ++i) rect("nrow", L.row(i, 0));
      const Rect body = L.listBody();
      ScrollIndicator si = scrollIndicatorFor(body, nodes * kRowH, body.h, 0, L.maxScroll(nodes), false);
      printf("ind nodes %d %d %d %d %d %d %d %d %d\n", si.visible ? 1 : 0, si.track.x, si.track.y,
             si.track.w, si.track.h, si.thumb.x, si.thumb.y, si.thumb.w, si.thumb.h);

      // --- 2. the Chat tab's channel list (the header dropdown) ---------------------------------
      const ChatLayout sel = chatLayoutFor(L, false, false, false, true);
      rect("head", sel.head);
      rect("headTitle", sel.headTitle);
      rect("advert", sel.advert);
      rect("msgs", sel.msgs);
      rect("compose", sel.compose);
      rect("field", sel.field);
      rect("selPanel", sel.selPanel);
      rect("selList", sel.selList);
      rect("selManage", sel.selManage);
      const int chans = 6;
      const int dms = 2;
      for (int e = 0; e < chans + dms; ++e) {
        // Entry 0 carries an unread count, entry 2 is the open chat: the Chat tab's own unread, kept.
        const uint16_t unread = (e == 0) ? 2 : 0;
        const SelRowLayout o = selRowLayoutFor(sel.selList, chans, dms, e, 0, unread, e == 2);
        printf("selrow %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", e, o.row.x, o.row.y, o.row.w,
               o.row.h, o.nameX, o.line1Cy, o.line2Cy, o.dot.x, o.dot.y, o.dot.w, o.count.x, o.count.y,
               o.count.w, o.hasDot ? 1 : 0, o.hasCount ? 1 : 0);
      }
      const ScrollIndicator ci =
          scrollIndicatorFor(sel.selList, selContentHeight(chans, dms), sel.selList.h, 0,
                             selMaxScroll(sel.selList, chans, dms), false);
      printf("ind chans %d %d %d %d %d %d %d %d %d\n", ci.visible ? 1 : 0, ci.track.x, ci.track.y,
             ci.track.w, ci.track.h, ci.thumb.x, ci.thumb.y, ci.thumb.w, ci.thumb.h);

      // --- 3. an open chat at rest (scroll 0 = the newest message) -------------------------------
      const ChatLayout chat = chatLayoutFor(L, false, false, false, false);
      rect("chatHead", chat.head);
      rect("chatAdsvert", chat.advert);
      rect("msgs2", chat.msgs);
      rect("compose2", chat.compose);
      rect("field2", chat.field);
      // Six blocks: the last one is long enough to need three lines, so the band scrolls.
      const int16_t heights[6] = {28, 42, 14, 28, 56, 42};
      int16_t tops[6];
      msgSpans(chat.msgs, heights, 6, 0, tops);
      for (int i = 0; i < 6; ++i) printf("msg %d %d %d\n", i, tops[i], heights[i]);
      const ScrollIndicator mi = scrollIndicatorFor(chat.msgs, msgContentHeight(heights, 6), chat.msgs.h,
                                                    0, msgMaxScroll(chat.msgs, heights, 6), true);
      printf("ind msgs %d %d %d %d %d %d %d %d %d\n", mi.visible ? 1 : 0, mi.track.x, mi.track.y,
             mi.track.w, mi.track.h, mi.thumb.x, mi.thumb.y, mi.thumb.w, mi.thumb.h);
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


    def trim(d, text, font, maxw):
        """The longest prefix of `text` that fits `maxw`, with '...' when it was cut (the firmware's own
        ellipsis rule, applied with the desktop font because that is what this preview draws)."""
        if d.textlength(text, font=font) <= maxw:
            return text
        out = text
        while out and d.textlength(out + '...', font=font) > maxw:
            out = out[:-1]
        return out + '...'


    class Canvas:
        """One 320 x 240 screen at scale s, offset (ox, oy), with the theme's colours and constants."""

        def __init__(self, d, ox, oy, s, C, N):
            self.d, self.ox, self.oy, self.s, self.C, self.N = d, ox, oy, s, C, N

        def R(self, r):
            ox, oy, s = self.ox, self.oy, self.s
            return [ox + r[0] * s, oy + r[1] * s, ox + (r[0] + r[2]) * s - 1,
                    oy + (r[1] + r[3]) * s - 1]

        def x(self, v):
            return self.ox + v * self.s

        def y(self, v):
            return self.oy + v * self.s

        def text(self, xy, s, font, fill, anchor='lm'):
            self.d.text((self.x(xy[0]), self.y(xy[1])), s, font=font, fill=fill, anchor=anchor)

        def line(self, x0, y0, x1, y1, fill, w=None):
            w = self.s if w is None else w
            self.d.line([self.x(x0), self.y(y0), self.x(x1), self.y(y1)], fill=fill, width=w)

        def status_bar(self, clock='14:02', right='GPS 7   78%'):
            C, N = self.C, self.N
            self.d.rectangle([self.x(0), self.y(0), self.x(320) - 1, self.y(N['kStatusH']) - 1],
                             fill=C['kBar'])
            self.line(0, N['kStatusH'] - 1, 320, N['kStatusH'] - 1, C['kLine'])
            self.text((4, N['kStatusH'] // 2), clock, mono(10 * self.s), C['kInk'])
            self.text((320 - 6, N['kStatusH'] // 2), right, mono(10 * self.s), C['kDim'],
                      anchor='rm')

        def tab_bar(self, tabs, active):
            C, N = self.C, self.N
            labels = ['Nodes', 'Chat', 'Map', 'RPTR']
            bar = tabs[0]
            top = bar[1]
            self.d.rectangle([self.x(0), self.y(top), self.x(320) - 1, self.y(240) - 1],
                             fill=C['kBar'])
            self.line(0, top, 320, top, C['kLine'])
            for t in range(4):
                r = tabs[t]
                if t == active:
                    self.d.rectangle(self.R(r), fill=C['kSelBg'])
                    self.d.rectangle([self.x(r[0]), self.y(r[1] + 1), self.x(r[0] + r[2]) - 1,
                                      self.y(r[1] + 2) - 1], fill=C['kAcc'])
                cx = r[0] + r[2] // 2
                self.text((cx, r[1] + r[3] // 2 - 1), labels[t], mono(9 * self.s),
                          C['kAcc'] if t == active else C['kDimmer'], anchor='mm')

        def indicator(self, ind):
            """The firmware's drawScrollIndicator(): track in kScrollTrack, thumb in kScrollThumb."""
            C = self.C
            visible, tx, ty, tw, th, hx, hy, hw, hh = ind
            if not visible:
                return
            self.d.rectangle([self.x(tx), self.y(ty), self.x(tx + tw) - 1, self.y(ty + th) - 1],
                             fill=C['kScrollTrack'])
            rad = (tw // 2) * self.s
            self.d.rounded_rectangle([self.x(hx), self.y(hy), self.x(hx + hw) - 1,
                                      self.y(hy + hh) - 1], radius=rad, fill=C['kScrollThumb'])


    def draw_nodes(c, A, C, N):
        c.status_bar()
        # The one-line header above the list ("20 nodes heard, 7 in the last hour"), mono dim.
        h = A['nodesHead']
        c.text((h[0] + N['kRowPadX'], h[1] + h[3] // 2), '20 nodes heard \u00b7 7 in the last hour',
               mono(10 * c.s), C['kDim'])
        names = ['Alice', 'Bob', 'Repeater 1', 'Room 2', 'Sensor A', 'Carol', 'Dave']
        notes = ['direct', 'path 1 hop', 'path 2 hops', 'direct', 'path 3 hops', 'direct', 'path 1 hop']
        ages = ['14:02', '14:01', '13:58', '13:55', '13:52', '13:47', '13:40']
        kinds = [C['kClient'], C['kClient'], C['kGreen'], C['kWarn'], C['kClient'], C['kClient'],
                 C['kClient']]
        for i, r in enumerate(A['nrow']):
            x, y, w, hh = r
            if i == 0:
                c.d.rectangle(c.R(r), fill=C['kSelBg'])
            bg = C['kSelBg'] if i == 0 else C['kScreen']
            cy = y + hh // 2
            dot_x = x + N['kRowPadX'] + N['kTypeDot'] // 2
            c.d.ellipse([c.x(dot_x - N['kTypeDot'] // 2), c.y(cy - N['kTypeDot'] // 2),
                         c.x(dot_x + N['kTypeDot'] // 2), c.y(cy + N['kTypeDot'] // 2)], fill=kinds[i])
            # name in sans, note and age in mono, right-aligned a row pad in from the track.
            right = x + w - N['kRowPadX']
            age_font = mono(10 * c.s)
            aw = c.d.textlength(ages[i], font=age_font) / c.s
            c.text((right, cy), ages[i], age_font, C['kDim'], anchor='rm')
            note_font = mono(10 * c.s)
            nw = c.d.textlength(notes[i], font=note_font) / c.s
            c.text((right - aw - N['kRowGap'], cy), notes[i], note_font, C['kDimmer'], anchor='rm')
            name_x = x + N['kRowPadX'] + N['kTypeDot'] + N['kRowGap']
            room = right - aw - N['kRowGap'] - nw - N['kRowGap'] - name_x
            c.text((name_x, cy), trim(c.d, names[i], sans(11 * c.s), room * c.s), sans(11 * c.s),
                   C['kAccInk'] if i == 0 else C['kInk'])
            c.line(x, y + hh - 1, x + w, y + hh - 1, C['kRowLine'])
        c.indicator(A['ind_nodes'])


    def draw_chans(c, A, C, N):
        c.status_bar()
        head = A['head']
        c.d.rectangle(c.R(head), fill=C['kScreen'])
        c.line(head[0], head[1] + head[3] - 1, head[0] + head[2], head[1] + head[3] - 1, C['kLine'])
        cy = head[1] + head[3] // 2
        c.text((head[0] + N['kHeadPadX'], cy), 'SLO', sans(11 * c.s), C['kInk'])
        c.line(head[0] + N['kHeadPadX'] + 20, cy - 2, head[0] + N['kHeadPadX'] + 23, cy + 2,
               C['kDim'])
        c.line(head[0] + N['kHeadPadX'] + 23, cy + 2, head[0] + N['kHeadPadX'] + 26, cy - 2,
               C['kDim'])
        ad = A['advert']
        c.d.rounded_rectangle(c.R(ad), radius=3 * c.s, fill=C['kPanel2'], outline=C['kLine'],
                              width=c.s)
        c.text((ad[0] + ad[2] // 2, ad[1] + ad[3] // 2 + 1), 'ADVERT', mono(10 * c.s), C['kAcc'],
               anchor='mm')

        panel = A['selPanel']
        c.d.rectangle(c.R(panel), fill=C['kPanel'])
        c.d.rectangle(c.R(panel), outline=C['kLine'], width=c.s)
        lst = A['selList']
        rows = A['selrow']
        # The "Channels" label, at the list's top minus the scroll (0 here).
        c.text((lst[0] + N['kSelPadX'], lst[1] + N['kSelSectionH'] // 2), 'Channels', mono(10 * c.s),
               C['kDim'])
        chan_names = ['ch-longname', 'SLO', 'local', 'test 2', 'EU', 'friends']
        chan_prev = ['Bob: hello there', 'Alice: see you at 6', 'no messages yet', 'Dave: ping',
                     'no messages yet', 'Eve: ok']
        dm_names = ['Alice', 'Bob']
        dm_prev = ['Alice: hi', 'Bob: got it']
        for r in rows:
            e = r[0]
            x, y, w, hh = r[1], r[2], r[3], r[4]
            name_x, l1, l2 = r[5], r[6], r[7]
            selected = e == 2
            bg = C['kSelBg'] if selected else C['kPanel']
            if selected:
                c.d.rectangle(c.R((x, y, w, hh)), fill=C['kSelBg'])
            if e >= 6:
                continue   # the DM rows are drawn by the same loop below (entry order)
            if r[14]:   # hasDot: the Chat tab's own unread marker
                c.d.ellipse([c.x(r[8]), c.y(r[9]), c.x(r[8] + r[10]), c.y(r[9] + r[10])],
                            fill=C['kAcc'])
            age = '14:0%d' % (2 - e) if e < 3 else '13:%02d' % (58 - e)
            c.text((x + w - N['kSelPadX'], l1), age, mono(10 * c.s), C['kDimmer'], anchor='rm')
            c.text((name_x, l1), trim(c.d, chan_names[e], sans(11 * c.s),
                                      (x + w - N['kSelPadX'] - 26 - name_x) * c.s), sans(11 * c.s),
                   C['kAccInk'] if selected else C['kInk'])
            right2 = x + w - N['kSelPadX']
            if r[15]:   # hasCount: an unread count next to the preview, unchanged by this ticket
                c.text((right2, l2), '2', mono(10 * c.s), C['kAcc'], anchor='rm')
                right2 -= N['kSelPadX'] + 2
            c.text((name_x, l2), trim(c.d, chan_prev[e], mono(10 * c.s),
                                      (right2 - name_x) * c.s), mono(10 * c.s), C['kDim'])
            c.line(x, y + hh - 1, x + w, y + hh - 1, C['kRowLine'])
        # "Direct messages" and its two rows, at the same arithmetic selRowTop() uses.
        y_dm = lst[1] + N['kSelSectionH'] + 6 * N['kSelRowH']
        c.text((lst[0] + N['kSelPadX'], y_dm + N['kSelSectionH'] // 2), 'Direct messages',
               mono(10 * c.s), C['kDim'])
        for j in range(2):
            e = 6 + j
            r = rows[e]
            x, y, w, hh = r[1], r[2], r[3], r[4]
            c.text((x + w - N['kSelPadX'], r[6]), '13:3%d' % (2 - j), mono(10 * c.s), C['kDimmer'],
                   anchor='rm')
            c.text((r[5], r[6]), dm_names[j], sans(11 * c.s), C['kInk'])
            c.text((r[5], r[7]), dm_prev[j], mono(10 * c.s), C['kDim'])
            c.line(x, y + hh - 1, x + w, y + hh - 1, C['kRowLine'])
        mg = A['selManage']
        # The list is clipped at the Manage row in the firmware (drawChatSelector sets the clip to `list`),
        # so the pinned row is painted here as an opaque band over whatever the list ran under it.
        c.d.rectangle(c.R((mg[0], mg[1], mg[2], mg[3])), fill=C['kPanel'])
        c.line(mg[0], mg[1], mg[0] + mg[2], mg[1], C['kLine'])
        c.text((mg[0] + N['kSelPadX'], mg[1] + mg[3] // 2), 'Manage channels', sans(11 * c.s),
               C['kAcc'])
        cx = mg[0] + mg[2] - N['kSelPadX'] - 5
        cy2 = mg[1] + mg[3] // 2
        c.line(cx, cy2 - 3, cx + 3, cy2, C['kAcc'])
        c.line(cx + 3, cy2, cx, cy2 + 3, C['kAcc'])
        # The compose line below the dropdown (the selector covers the message band only).
        comp = A['compose']
        c.d.rectangle(c.R(comp), fill=C['kScreen'])
        c.line(comp[0], comp[1], comp[0] + comp[2], comp[1], C['kLine'])
        f = A['field']
        c.d.rounded_rectangle(c.R(f), radius=4 * c.s, fill=C['kFieldBg'], outline=C['kLine'],
                              width=c.s)
        c.text((f[0] + N['kFieldPadX'], f[1] + f[3] // 2), 'message', mono(10 * c.s), C['kDimmer'])
        c.indicator(A['ind_chans'])


    def draw_chat(c, A, C, N):
        c.status_bar()
        head = A['chatHead']
        c.d.rectangle(c.R(head), fill=C['kScreen'])
        c.line(head[0], head[1] + head[3] - 1, head[0] + head[2], head[1] + head[3] - 1, C['kLine'])
        cy = head[1] + head[3] // 2
        c.text((head[0] + N['kHeadPadX'], cy), 'SLO', sans(11 * c.s), C['kInk'])
        c.line(head[0] + N['kHeadPadX'] + 20, cy - 2, head[0] + N['kHeadPadX'] + 23, cy + 2,
               C['kDim'])
        c.line(head[0] + N['kHeadPadX'] + 23, cy + 2, head[0] + N['kHeadPadX'] + 26, cy - 2,
               C['kDim'])
        ad = A['chatAdsvert']
        c.d.rounded_rectangle(c.R(ad), radius=3 * c.s, fill=C['kPanel2'], outline=C['kLine'],
                              width=c.s)
        c.text((ad[0] + ad[2] // 2, ad[1] + ad[3] // 2 + 1), 'ADVERT', mono(10 * c.s), C['kAcc'],
               anchor='mm')

        band = A['msgs2']
        c.d.rectangle(c.R(band), fill=C['kScreen'])
        msgs = [
            ('13:58', 'Alice', ['hello everyone'], C['kAcc']),
            ('14:00', 'Bob', ['the repeater on the hill is', 'back up, two hops from here'], C['kAcc']),
            ('14:01', 'me', ['good'], C['kGreen']),
            ('14:02', 'Carol', ['anyone near the river today?'], C['kAcc']),
            ('14:03', 'Dave', ['I am, the water is high', 'and the path is muddy', 'take the upper road'],
             C['kAcc']),
            ('14:04', 'Eve', ['thanks Dave, upper road it is'], C['kAcc']),
        ]
        for (i, (time, name, lines, name_col)) in enumerate(msgs):
            top = A['msg'][i][1]
            height = A['msg'][i][2]
            if top >= band[1] + band[3] or top + height <= band[1]:
                continue
            padx = band[0] + N['kMsgPadX']
            cy = top + N['kMsgLineH'] // 2
            if band[1] <= cy - N['kMsgLineH'] // 2 and cy + N['kMsgLineH'] // 2 <= band[1] + band[3]:
                c.text((padx, cy), time, mono(10 * c.s), C['kDimmer'])
                c.text((padx + N['kMsgStampW'] + N['kMsgHdrGap'], cy), name, mono(10 * c.s),
                       name_col)
            prefix = N['kMsgPadX'] + N['kMsgStampW'] + N['kMsgHdrGap'] + \
                int(c.d.textlength(name, font=mono(10 * c.s)) / c.s) + N['kMsgHdrGap']
            # The band's real text limit: the right pad, which is where the track starts.
            right = band[0] + band[2] - N['kMsgPadX']
            for r, text in enumerate(lines):
                ly = cy + r * N['kMsgLineH']
                if ly - N['kMsgLineH'] // 2 < band[1] or ly + N['kMsgLineH'] // 2 > band[1] + band[3]:
                    continue   # outside the band: the firmware's clip cuts it
                lx = band[0] + (prefix if r == 0 else N['kMsgPadX'])
                # The long line is measured against the limit, so the preview shows that text stops
                # 3 px before the 4 px track and never runs under it.
                c.text((lx, cy + r * N['kMsgLineH']),
                       trim(c.d, text, sans(11 * c.s), (right - lx) * c.s), sans(11 * c.s), C['kInk'])

        comp = A['compose2']
        c.d.rectangle(c.R(comp), fill=C['kScreen'])
        c.line(comp[0], comp[1], comp[0] + comp[2], comp[1], C['kLine'])
        f = A['field2']
        c.d.rounded_rectangle(c.R(f), radius=4 * c.s, fill=C['kFieldBg'], outline=C['kLine'],
                              width=c.s)
        c.text((f[0] + N['kFieldPadX'], f[1] + f[3] // 2), 'message', mono(10 * c.s), C['kDimmer'])
        c.indicator(A['ind_msgs'])


    def main():
        tmp = tempfile.mkdtemp(prefix='est414-')
        src = os.path.join(tmp, 'est414_preview.cpp')
        exe = os.path.join(tmp, 'est414_preview')
        with open(src, 'w', encoding='utf-8') as fh:
            fh.write(DUMPER)
        subprocess.run(['g++', '-std=c++17', '-I', os.path.join(ROOT, 'src'), src,
                        os.path.join(ROOT, 'src', 'ui', 'chat_layout.cpp'), '-o', exe],
                       check=True)
        out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout

        C, N, A = {}, {}, {}
        A['nrow'], A['selrow'], A['msg'] = [], [], []
        tabs = []
        for line in out.splitlines():
            if not line:
                continue
            f = line.split()
            if f[0] == 'col':
                C[f[1]] = rgb565(int(f[2]))
            elif f[0] == 'num':
                N[f[1]] = int(f[2])
            elif f[0] == 'rect':
                if f[1] == 'tab':
                    tabs.append(tuple(int(v) for v in f[2:6]))
                elif f[1] == 'nrow':
                    A['nrow'].append(tuple(int(v) for v in f[2:6]))
                else:
                    A[f[1]] = tuple(int(v) for v in f[2:6])
            elif f[0] == 'ind':
                A['ind_' + f[1]] = tuple(int(v) for v in f[2:11])
            elif f[0] == 'selrow':
                A['selrow'].append(tuple(int(v) for v in f[1:]))
            elif f[0] == 'msg':
                A['msg'].append((int(f[1]), int(f[2]), int(f[3])))
            else:
                raise SystemExit('unexpected dumper line: %r' % line)

        s = 2
        pad = 14
        pw, ph = 320 * s, 240 * s
        canvas_w = pad * 4 + pw * 3
        canvas_h = pad + 26 + ph + 20 + 34 + pad
        img = Image.new('RGB', (canvas_w, canvas_h), (0x13, 0x12, 0x10))
        d = ImageDraw.Draw(img)
        d.text((pad, pad), 'EST-414: the scroll indicator on the three lists (2x)', font=sans(15),
               fill=C['kInk'])
        captions = [
            'Nodes list: 20 nodes, thumb = visible / total, at the top (scroll 0)',
            'Chat channel list (the header dropdown): 6 channels, 2 DMs, at scroll 0',
            'Open chat at rest: bottom-anchored, so the thumb sits at the bottom',
        ]
        # Each panel is its own 320 x 240 image, so nothing drawn past the screen edge can bleed into
        # the next panel or the caption: the panel's own clip, like the device's.
        for i, draw in enumerate((draw_nodes, draw_chans, draw_chat)):
            panel_img = Image.new('RGB', (pw, ph), C['kScreen'])
            c = Canvas(ImageDraw.Draw(panel_img), 0, 0, s, C, N)
            draw(c, A, C, N)
            c.tab_bar(tabs, 0 if draw is draw_nodes else 1)
            ox = pad + i * (pw + pad)
            oy = pad + 26
            img.paste(panel_img, (ox, oy))
            d.text((ox, oy + ph + 6), captions[i], font=sans(12), fill=C['kDim'])
        d.text((pad, canvas_h - pad - 12),
               'host render: real rectangles from ui/layout.h, ui/chat_layout.h and ui/scroll_indicator.h, '
               'colours from ui/theme.h. DejaVu text, not the VLW faces; invented names and messages.',
               font=sans(11), fill=(0x5f, 0x6b, 0x7d))

        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        img.save(OUT)
        print(OUT)
    main()

RENDERERS = {
    'est400': _render_est400,
    'est402': _render_est402,
    'est404': _render_est404,
    'est405': _render_est405,
    'est406': _render_est406,
    'est414': _render_est414,
}


def main(argv):
    names = argv[1:] or ['all']
    if names == ['all']:
        names = list(RENDERERS)
    unknown = [n for n in names if n not in RENDERERS]
    if unknown:
        sys.exit('unknown preview(s): %s (choose from %s)' %
                 (', '.join(unknown), ', '.join(RENDERERS)))
    for name in names:
        RENDERERS[name]()


if __name__ == '__main__':
    main(sys.argv)
