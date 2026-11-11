// SPDX-License-Identifier: MIT
// Frame-only widgets. All drawing goes through the LovyanGFX surface passed in; the caller owns the
// omFrameBegin/omFrameEnd pair (screen_nodes.cpp opens it).
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>

#include "ui/badge_font.h"
#include "ui/battery_bolt.h"   // EST-402: the bolt's pixel rows, shared with the preview
#include "ui/emoji.h"
#include "ui/emoji_table.h"
#include "ui/fonts.h"
#include "ui/icons.h"
#include "ui/keyboard_draw.h"   // one key-cap / accent-bubble drawing for the shared sheet (EST-352)
#include "ui/status_cells.h"
#include "ui/toast.h"
#include "ui/text_wrap.h"

namespace om {
namespace ui {
namespace {

uint16_t barsOn(int rssi) {
  if (rssi > -60) return 5;
  if (rssi > -80) return 4;
  if (rssi > -95) return 3;
  if (rssi > -105) return 2;
  return 1;
}

void drawSignalBars(lgfx::LovyanGFX* d, int x, int bottom, int rssi) {
  const int on = (int)barsOn(rssi);
  for (int i = 0; i < kBarsCount; ++i) {
    const int h = 3 + i;
    const int bx = x + i * (kBarsW + kBarsGap);
    d->fillRect(bx, bottom - h, kBarsW, h, (i < on) ? kGreen : kDimmer);
  }
}

// EST-402/404: the charging bolt, drawn LAST over the battery icon (fill and frame). Gold is the
// theme's one accent, but gold on the green fill measures about 1.1:1, so every row gets a 1 px kBar
// edge: expanding the run one pixel left and right and one row above and below is exactly a 1 px
// dilation, and the gold rows are drawn afterwards over its middle. EST-404: the edge is drawn for
// ALL rows, including the tips, because the bolt is taller than the body and its tips now cross the
// frame. `y` is the icon's top row; the bolt is centred on it, `(kBoltH - kBattH) / 2` (2 px) clear.
//
// It means "external power is present" and nothing more. The cell voltage stays at about 4.19 V both
// while charging and once full, and BC_LVL is the port's claim, not proof, so this build makes NO
// "charging" vs "full" guess (EST-402 item 3; the rule is charge_state.h).
void drawBolt(lgfx::LovyanGFX* d, int x, int y) {
  const int bx = x + (kBattW - kBoltW) / 2;
  const int by = y - (kBoltH - kBattH) / 2;   // the tips stand clear above and below the frame
  for (int r = 0; r < kBoltH; ++r) {
    const int hx = bx + kBoltRuns[r].x - 1;
    const int hw = kBoltRuns[r].w + 2;
    d->fillRect(hx, by + r - 1, hw, 3, kBar);
  }
  for (int r = 0; r < kBoltH; ++r) {
    d->fillRect(bx + kBoltRuns[r].x, by + r, kBoltRuns[r].w, 1, kAcc);
  }
}

void drawBattery(lgfx::LovyanGFX* d, int x, int cy, int pct, bool externalPower) {
  const int y = cy - kBattH / 2;
  d->drawRect(x, y, kBattW, kBattH, kDim);
  d->fillRect(x + kBattW, y + 2, 2, kBattH - 4, kDim);
  if (pct >= 0) {
    int fill = (kBattW - 2) * (pct > 100 ? 100 : pct) / 100;
    if (fill < 0) fill = 0;
    if (fill > 0) {
      const uint16_t c = (pct <= 15) ? kBad : (pct <= 30 ? kWarn : kGreen);
      d->fillRect(x + 1, y + 1, fill, kBattH - 2, c);
    }
  }
  // The bolt is drawn even when the percentage is unknown: the port controller can answer before the
  // ADC has a reading, and "external power present" does not depend on the level.
  if (externalPower) drawBolt(d, x, y);
}

// Blend a foreground RGB565 colour over a known background by a 0..255 coverage value. The widget
// always knows the surface it draws on (the status bar's kBar, the tab's kSelBg), so no pixel read
// from the frame is needed.
uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a) {
  if (a == 0) return bg;
  if (a == 255) return fg;
  const uint32_t fr = (fg >> 11) & 0x1F, fg6 = (fg >> 5) & 0x3F, fb = fg & 0x1F;
  const uint32_t br = (bg >> 11) & 0x1F, bg6 = (bg >> 5) & 0x3F, bb = bg & 0x1F;
  const uint32_t inv = 255U - a;
  const uint32_t r = (fr * a + br * inv + 127U) / 255U;
  const uint32_t g = (fg6 * a + bg6 * inv + 127U) / 255U;
  const uint32_t b = (fb * a + bb * inv + 127U) / 255U;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// A cog that is the mock's U+2699 glyph (tools/mkicons.py), not a hand-built crosshair. The mask is
// blended over the bar colour, so its antialiased edge matches the surrounding text.
void drawGear(lgfx::LovyanGFX* d, const Rect& r, uint16_t col, uint16_t bg) {
  drawIcon(d, kIconGear, r.x, r.y, col, bg);
}

// Four 11x11 icons, drawn with primitives so there is no missing glyph and no icon font in flash.
void drawTabIcon(lgfx::LovyanGFX* d, Tab t, int cx, int cy, uint16_t col) {
  switch (t) {
    case Tab::Nodes:
      d->drawLine(cx, cy - 5, cx + 5, cy, col);
      d->drawLine(cx + 5, cy, cx, cy + 5, col);
      d->drawLine(cx, cy + 5, cx - 5, cy, col);
      d->drawLine(cx - 5, cy, cx, cy - 5, col);
      d->fillCircle(cx, cy, 1, col);
      break;
    case Tab::Chat:
      d->drawRect(cx - 5, cy - 4, 10, 8, col);
      d->drawLine(cx - 5, cy - 4, cx, cy + 1, col);
      d->drawLine(cx + 5, cy - 4, cx, cy + 1, col);
      break;
    case Tab::Map:
      d->drawCircle(cx, cy - 2, 3, col);
      d->drawLine(cx - 2, cy, cx, cy + 5, col);
      d->drawLine(cx + 2, cy, cx, cy + 5, col);
      d->fillCircle(cx, cy - 2, 1, col);
      break;
    case Tab::Rptr:
      d->drawLine(cx, cy + 5, cx, cy - 2, col);
      d->drawLine(cx - 4, cy + 5, cx, cy - 4, col);
      d->drawLine(cx + 4, cy + 5, cx, cy - 4, col);
      d->drawLine(cx - 4, cy + 5, cx + 4, cy + 5, col);
      d->fillCircle(cx, cy - 4, 1, col);
      break;
    default:
      break;
  }
}

}  // namespace

namespace {

// Defined with the text helpers below; declared here so drawEmoji() can use it.
void drawEmojiGlyph(lgfx::LovyanGFX* d, int x, int cy, const EmojiGlyph& e, uint16_t bg);

}  // namespace

void drawIcon(lgfx::LovyanGFX* d, const IconMask& icon, int x, int y, uint16_t fg, uint16_t bg) {
  if (!d || !icon.alpha || icon.w == 0 || icon.h == 0) return;
  for (int yy = 0; yy < icon.h; ++yy) {
    for (int xx = 0; xx < icon.w; ++xx) {
      const uint8_t a = icon.alpha[(size_t)yy * icon.w + xx];
      if (a == 0) continue;
      d->drawPixel((int16_t)(x + xx), (int16_t)(y + yy), blend565(fg, bg, a));
    }
  }
}

bool drawEmoji(lgfx::LovyanGFX* d, uint32_t cp, int x, int cy, uint16_t bg) {
  const EmojiGlyph* e = emojiFind(cp);
  if (e == nullptr) return false;
  drawEmojiGlyph(d, x, cy, *e, bg);
  return true;
}

// Text helpers, public because the screens use the same role fonts and colour pairs. They draw into
// the surface passed in, exactly like everything else here.
void setRole(lgfx::LovyanGFX* d, FontRole role) {
  const lgfx::IFont* f = fontFor(role);
  d->setFont(f ? f : &lgfx::fonts::Font0);
}

bool fontHasGlyph(FontRole role, uint32_t cp) {
  (void)role;   // every face carries the same ranges (tools/mkvlw.py --extra, EST-346)
  return fontCoversCodePoint(cp);
}

namespace {

struct Utf8Ctx {
  lgfx::LovyanGFX* d;
  FontRole role;
};

// Width of one code point, or 0 when neither the emoji table nor the face has it (measureUtf8
// then uses the placeholder width, a space plus 2 px).
int glyphWidthCb(void* ctx, uint32_t cp) {
  Utf8Ctx* c = (Utf8Ctx*)ctx;
  if (c->d == nullptr) return 0;
  if (const EmojiGlyph* e = emojiFind(cp)) return (int)e->w + 1;   // the cell and one px of air
  if (!fontHasGlyph(c->role, cp)) return 0;
  char b[5] = {0};
  const int n = utf8Encode(cp, b);
  if (n <= 0) return 0;
  b[n] = 0;
  setRole(c->d, c->role);
  return c->d->textWidth(b);
}

int measureCb(void* ctx, const char* text, int len) {
  Utf8Ctx* c = (Utf8Ctx*)ctx;
  if (c == nullptr) return 0;
  return measureText(c->d, c->role, text, len);
}

int spaceWidth(lgfx::LovyanGFX* d, FontRole role) {
  setRole(d, role);
  return d->textWidth(" ");
}

// The fallback glyph: one small dim box, never LovyanGFX's full-height missing-glyph rectangle.
void drawMissingGlyph(lgfx::LovyanGFX* d, int x, int cy, int w) {
  d->drawRect(x, cy - kMissingBoxH / 2, w, kMissingBoxH, kDimmer);
}

// One emoji: the RGB565 cell blended by its 4-bit coverage over the colour the caller knows it is
// drawing on (the same bargain drawIcon makes, so no pixel read from the frame is needed).
void drawEmojiGlyph(lgfx::LovyanGFX* d, int x, int cy, const EmojiGlyph& e, uint16_t bg) {
  if (d == nullptr || e.rgb == nullptr || e.alpha == nullptr || e.w == 0 || e.h == 0) return;
  const int top = cy - (int)e.h / 2;
  const int stride = (e.w + 1) / 2;
  for (int yy = 0; yy < (int)e.h; ++yy) {
    for (int xx = 0; xx < (int)e.w; ++xx) {
      const uint8_t packed = e.alpha[(size_t)yy * stride + xx / 2];
      const uint8_t nibble = (xx & 1) ? (uint8_t)(packed & 0x0F) : (uint8_t)(packed >> 4);
      if (nibble == 0) continue;
      const uint16_t rgb = emojiReadU16(e.rgb + ((size_t)yy * e.w + xx) * 2);
      d->drawPixel((int16_t)(x + xx), (int16_t)(top + yy),
                   blend565(rgb, bg, (uint8_t)(nibble * 17)));
    }
  }
}

// Draw the drawable run [a, b) at (x, cy), advancing x. Long runs are cut at a code point boundary
// into chunks, because a VLW face measures a C string.
int drawRun(lgfx::LovyanGFX* d, int x, int cy, const char* s, int a, int b) {
  char chunk[112];
  while (a < b) {
    int end = a;
    while (end < b) {
      int at = end;
      utf8Decode(s, &at);
      if (at > b) at = b;
      if (at - a > (int)sizeof(chunk) - 5) break;
      end = at;
    }
    if (end == a) {   // a single sequence longer than the chunk: draw it alone
      int at = a;
      utf8Decode(s, &at);
      end = at > b ? b : at;
    }
    const int n = end - a;
    memcpy(chunk, s + a, (size_t)n);
    chunk[n] = 0;
    d->drawString(chunk, x, cy);
    x += d->textWidth(chunk);
    a = end;
  }
  return x;
}

}  // namespace

void drawTextLeft(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int x, int y,
                  const char* s) {
  if (d == nullptr || s == nullptr) return;
  setRole(d, role);
  d->setTextDatum(lgfx::textdatum_t::middle_left);
  d->setTextColor(fg, bg);
  if (*s == 0) return;
  const int len = (int)strlen(s);
  int runStart = -1;
  int runEnd = -1;
  int xCursor = x;
  for (int i = 0; i < len;) {
    TextUnit u;
    const int next = textUnitAt(s, len, i, &u);
    if (next <= i) break;
    i = next;
    if (u.zero) continue;   // a stray selector has no base and no width

    if (const EmojiGlyph* e = emojiFind(u.cp)) {
      if (runStart >= 0) {
        xCursor = drawRun(d, xCursor, y, s, runStart, runEnd);
        runStart = -1;
        runEnd = -1;
      }
      drawEmojiGlyph(d, xCursor, y, *e, bg);
      xCursor += (int)e->w + 1;
      continue;
    }
    if (fontHasGlyph(role, u.cp)) {
      if (runStart < 0) runStart = u.start;
      runEnd = u.coreEnd;   // a consumed selector is never handed to the face
      continue;
    }

    if (runStart >= 0) {
      xCursor = drawRun(d, xCursor, y, s, runStart, runEnd);
      runStart = -1;
      runEnd = -1;
    }
    const int w = spaceWidth(d, role) + kMissingExtra;
    drawMissingGlyph(d, xCursor, y, w);
    xCursor += w;
  }
  if (runStart >= 0) drawRun(d, xCursor, y, s, runStart, runEnd);
}

int drawTextRight(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int right, int y,
                  const char* s) {
  if (d == nullptr || s == nullptr) return 0;
  const int w = measureText(d, role, s, (int)strlen(s));
  drawTextLeft(d, role, fg, bg, right - w, y, s);
  return w;
}

// Never draws outside maxW; the cut is on a drawable-unit boundary and ends with "...". EST-400: the
// cut itself is ui/text_wrap.h's ellipsisFit(), the same arithmetic the host test proves.
void drawTextEllipsis(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int x, int y,
                      int maxW, const char* s) {
  if (d == nullptr || s == nullptr || maxW <= 0) return;
  Utf8Ctx ctx{d, role};
  int cut = ellipsisFit(s, measureCb, &ctx, maxW);
  if (cut < 0) {   // the whole string fits: draw it unchanged
    drawTextLeft(d, role, fg, bg, x, y, s);
    return;
  }
  if (cut == 0) return;
  char buf[80];
  if (cut > (int)sizeof(buf) - 4) {
    cut = (int)sizeof(buf) - 4;
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) --cut;   // never mid-sequence
    if (cut == 0) return;
  }
  memcpy(buf, s, (size_t)cut);
  buf[cut] = 0;
  strncat(buf, "...", sizeof(buf) - strlen(buf) - 1);
  drawTextLeft(d, role, fg, bg, x, y, buf);
}

int measureText(lgfx::LovyanGFX* d, FontRole role, const char* s, int len) {
  if (d == nullptr || s == nullptr || len <= 0) return 0;
  Utf8Ctx ctx{d, role};
  return measureUtf8(s, len, glyphWidthCb, &ctx, spaceWidth(d, role));
}

uint16_t typeColour(NodeType t) {
  switch (t) {
    case NodeType::Client:   return kClient;
    case NodeType::Repeater: return kGreen;
    case NodeType::Room:     return kWarn;
    case NodeType::Sensor:   return kDim;
    default:                 return kDimmer;
  }
}

int textWidth(lgfx::LovyanGFX* d, FontRole role, const char* text) {
  if (d == nullptr || text == nullptr) return 0;
  return measureText(d, role, text, (int)strlen(text));
}

void drawStatusBar(lgfx::LovyanGFX* d, const Layout& L, const StatusModel& m) {
  const Rect& r = L.status;
  d->fillRect(r.x, r.y, r.w, r.h, kBar);
  d->drawFastHLine(r.x, r.bottom() - 1, r.w, kLine);

  const int cy = r.y + r.h / 2;

  // Everything is measured first, then the same rectangles draw and answer the taps (EST-337).
  GpsCell cell;
  cell.valid = m.gpsValid;
  cell.talked = m.gpsTalked;
  cell.heard = m.gpsHeard;
  char gps[kGpsCellMax];
  gpsCellText(gps, sizeof(gps), cell);

  char pct[8];
  if (m.batteryPct < 0) snprintf(pct, sizeof(pct), "--");
  else snprintf(pct, sizeof(pct), "%d%%", m.batteryPct);

  const char* clock = (m.clock && m.clock[0]) ? m.clock : "--:--";
  StatusCellWidths widths;
  widths.clock = textWidth(d, FontRole::MonoStatus, clock);
  widths.pct = textWidth(d, FontRole::MonoStatus, pct);
  widths.gps = textWidth(d, FontRole::MonoStatus, gps);

  StatusCellFlags flags;
  flags.fault = (m.fault != RadioFault::None);
  flags.dnd = m.dnd;
  flags.bt = m.bt;
  flags.wifi = m.wifi;
  const StatusBarRects rects = statusBarRects(L, widths, flags);

  // LEFT: the time in ink, nothing else unless the radio fault mark is up.
  drawTextLeft(d, FontRole::MonoStatus, kInk, kBar, rects.time.x, cy, clock);
  if (flags.fault) drawIcon(d, kIconFault, rects.fault.x, rects.fault.y, kBad, kBar);

  // RIGHT, from the right edge: gear, battery (percent then icon), GPS, then the optional icons.
  drawGear(d, rects.gear, kDim, kBar);
  drawTextRight(d, FontRole::MonoStatus, kDim, kBar, rects.battText.right(), cy, pct);
  // EST-402: the bolt is inside the icon; the percentage is unchanged and stays where it is.
  drawBattery(d, rects.battIcon.x, cy, m.batteryPct, m.externalPower);
  // ONE GPS number: the satellites heard right now, dim while searching and in kGreen with a fix
  // (the fix is the colour, not a second number), "GPS --" when the module never talked.
  const uint16_t gpsColour = cell.valid ? kGreen : kDim;
  drawTextRight(d, FontRole::MonoStatus, gpsColour, kBar, rects.gps.right(), cy, gps);
  if (flags.dnd) drawIcon(d, kIconDnd, rects.dnd.x, rects.dnd.y, kDim, kBar);
  // EST-354: present when Wi-Fi is on, dim until associated, ink once it has an IP.
  if (flags.wifi) {
    drawIcon(d, kIconWifi, rects.wifi.x, rects.wifi.y, m.wifiConnected ? kInk : kDim, kBar);
  }
  // EST-356: present while the switch is on, dim while advertising, ink once the phone is connected.
  if (flags.bt) drawIcon(d, kIconBluetooth, rects.bt.x, rects.bt.y, m.btConnected ? kInk : kDim, kBar);
}

// One line on a 304 px plate. EST-400: it used to be the only text path in the UI that neither
// ellipsised nor clipped: a long line ran past both rounded ends. The text stays centred while it
// fits the plate (the old middle_center datum's result) and is cut with "..." to the plate's inner
// width when it does not.
void drawToast(lgfx::LovyanGFX* d, const Layout& L, const char* text) {
  if (d == nullptr || text == nullptr || text[0] == 0) return;
  const Rect box{(int16_t)kToastMargin, (int16_t)(L.tabbar.y - kToastMargin - kToastH),
                 (int16_t)(L.width - 2 * kToastMargin), kToastH};
  d->fillRoundRect(box.x, box.y, box.w, box.h, kRadius, kPanel2);
  d->drawRoundRect(box.x, box.y, box.w, box.h, kRadius, kLine);
  const int room = box.w - 2 * kToastMargin;
  const int tw = measureText(d, FontRole::SansBody, text, (int)strlen(text));
  const int drawn = tw > room ? room : tw;
  drawTextEllipsis(d, FontRole::SansBody, kInk, kPanel2, box.x + (box.w - drawn) / 2,
                   box.y + box.h / 2, room, text);
}

// The frame overlay (EST-344): the toast state lives in ui/toast.h; this only decides whether the
// current time is inside the toast and draws it. Every framed redraw reaches it through
// omFrameEnd(), which is what keeps it on the screen for its whole life instead of one frame.
//
// EST-368: a message notice carries its text, so it is drawn as a panel that grows with the text
// (the title in mono dim, then the message wrapped by the chat's own wrapper) instead of the single
// centred line. The one-line forms are unchanged.
namespace {

int measureNoticeBody(void* ctx, const char* text, int len) {
  return measureText((lgfx::LovyanGFX*)ctx, FontRole::SansBody, text, len);
}

void drawNoticePanel(lgfx::LovyanGFX* d, const Layout& L, const ToastState& t) {
  if (t.body[0] == 0) {
    drawToast(d, L, t.text);
    return;
  }
  const NoticeLayout lo =
      noticeLayout(t.body, measureNoticeBody, d, L.width, L.tabbar.y, L.status.bottom());
  d->fillRoundRect(lo.boxX, lo.boxY, lo.boxW, lo.boxH, kRadius, kPanel2);
  d->drawRoundRect(lo.boxX, lo.boxY, lo.boxW, lo.boxH, kRadius, kLine);
  drawTextEllipsis(d, FontRole::MonoMeta, kDim, kPanel2, lo.boxX + kNoticePad, lo.titleCy,
                   lo.boxW - 2 * kNoticePad, t.text);
  char buf[kNoticeBodyCap + 8];
  for (int i = 0; i < lo.lineCount; ++i) {
    int len = (i == lo.lineCount - 1) ? lo.lastLen : lo.lines[i].len;
    if (len < 0) len = 0;
    if (lo.lines[i].start + len > (int)sizeof(buf) - 4) len = (int)sizeof(buf) - 4 - lo.lines[i].start;
    if (len < 0) len = 0;
    if (len > 0) memcpy(buf, t.body + lo.lines[i].start, (size_t)len);
    buf[len] = 0;
    if (i == lo.lineCount - 1 && lo.truncated) memcpy(buf + len, "...", 4);
    drawTextLeft(d, FontRole::SansBody, kInk, kPanel2, lo.boxX + kNoticePad,
                 lo.firstLineCy + i * kNoticeLineH, buf);
  }
}

}  // namespace

void drawToastOverlay(lgfx::LovyanGFX* d, uint32_t nowMs) {
  ToastState& t = toastState();
  if (!t.visible(nowMs)) return;
  drawNoticePanel(d, l2(), t);
  // EST-381: the count the notification module logs, one line per toast. It only ever goes up when
  // the toast was really painted into a frame, which is what tells a Map pop-up that was delivered
  // and drawn apart from one that never got a frame at all.
  ++t.paints;
}

void drawTabBar(lgfx::LovyanGFX* d, const Layout& L, const TabBarModel& m) {
  drawTabBarAt(d, L, m, 0);
}

// EST-374: the same bar, `dy` rows lower. The Map's hide animation moves the whole page bar down
// (kMapBarSlideDy) and the frame clips what falls off the screen floor; the bar's own drawing is
// untouched, so the last rows of the slide are the real bar's top rows (the kLine edge and the
// active tab's gold segment), which is exactly what "tucked under" should leave behind.
void drawTabBarAt(lgfx::LovyanGFX* d, const Layout& L, const TabBarModel& m, int16_t dy) {
  Rect bar = L.tabbar;
  bar.y = (int16_t)(bar.y + dy);
  d->fillRect(bar.x, bar.y, bar.w, bar.h, kBar);
  d->drawFastHLine(bar.x, bar.y, bar.w, kLine);

  for (int i = 0; i < kTabCount; ++i) {
    const Tab t = (Tab)i;
    Rect r = L.tab(t);
    r.y = (int16_t)(r.y + dy);
    const bool on = (t == m.active);
    if (on) {
      d->fillRect(r.x, r.y, r.w, r.h, kSelBg);
      d->fillRect(r.x, r.y, r.w, kTabTopLine, kAcc);
    }
    const int cx = r.x + r.w / 2;
    const int iconCy = r.y + kTabPadY + kTabIcon / 2;
    const int labelCy = r.bottom() - kTabLabelPadB;
    const uint16_t col = on ? kAcc : kDimmer;
    drawTabIcon(d, t, cx, iconCy, col);

    setRole(d, FontRole::SansTab);
    d->setTextDatum(lgfx::textdatum_t::middle_center);
    d->setTextColor(col, on ? kSelBg : kBar);
    d->drawString(tabLabel(t), cx, labelCy);
  }

  // The unread count sits on the Chat icon, top-right, on the badge widget (theme.h holds its
  // geometry). The ring is the bar colour, so the circle separates from the icon under it. EST-364:
  // the box is as wide as the text the face draws (one digit, two, or "99+").
  if (m.unread > 0) {
    char badgeChars[4];
    const int chars = badgeText(m.unread, badgeChars, (int)sizeof(badgeChars));
    if (chars > 0) {
      Rect b = countBadgeRect(L.tab(Tab::Chat), chars);
      b.y = (int16_t)(b.y + dy);
      drawCountBadge(d, b, m.unread, kBar);
    }
  }

  // EST-342: a small gold dot on the Nodes tab while a contact added since the screen was last
  // opened is still unseen. It is not a count, so it is a dot, and it sits in the same corner the
  // Chat badge uses.
  if (m.newNodeDot) {
    const Rect tab = L.tab(Tab::Nodes);
    const int cx = tab.x + tab.w / 2 + kBadgeCxOfs;
    const int cy = tab.y + kBadgeTop + kBadgeD / 2 + dy;
    d->fillSmoothCircle(cx, cy, kNewDotR + kBadgeRing, kBar);
    d->fillSmoothCircle(cx, cy, kNewDotR, kAcc);
  }
}

// One badge for every count drawn on red. A smooth circle (13 px) or pill (19x13, two digits), a 1 px
// ring in `ring`, and the pixel face for the digits: white on the solid fill, no anti-aliasing over
// the glyph itself.
void drawCountBadge(lgfx::LovyanGFX* d, const Rect& box, int count, uint16_t ring) {
  if (d == nullptr || count <= 0 || box.w < kBadgeD || box.h < kBadgeD) return;
  char text[4];
  const int chars = badgeText(count, text, (int)sizeof(text));
  if (chars <= 0) return;

  const int cx = box.x + box.w / 2;
  const int cy = box.y + box.h / 2;
  const int r = kBadgeD / 2;   // 6: fillSmoothCircle(cx, cy, 6) spans 13 px
  if (box.w > box.h) {
    d->fillSmoothRoundRect(box.x - kBadgeRing, box.y - kBadgeRing, box.w + 2 * kBadgeRing,
                           box.h + 2 * kBadgeRing, r + kBadgeRing, ring);
    d->fillSmoothRoundRect(box.x, box.y, box.w, box.h, r, kBad);
  } else {
    d->fillSmoothCircle(cx, cy, r + kBadgeRing, ring);
    d->fillSmoothCircle(cx, cy, r, kBad);
  }

  int x = cx - badgeTextWidth(chars) / 2;
  const int y = cy - kBadgeGlyphH / 2;
  for (int i = 0; i < chars; ++i) {
    const uint8_t* rows = badgeGlyphRows(text[i]);
    if (rows != nullptr) {
      for (int ry = 0; ry < kBadgeGlyphH; ++ry) {
        for (int rx = 0; rx < kBadgeGlyphW; ++rx) {
          if (rows[ry] & (uint8_t)(1u << (kBadgeGlyphW - 1 - rx))) {
            d->drawPixel((int16_t)(x + rx), (int16_t)(y + ry), kWhite);
          }
        }
      }
    }
    x += kBadgeGlyphW + kBadgeGlyphGap;
  }
}

void drawListRow(lgfx::LovyanGFX* d, const Rect& r, const RowModel& row) {
  if (r.empty()) return;
  if (row.selected) d->fillRect(r.x, r.y, r.w, r.h, kSelBg);

  const int cy = r.y + r.h / 2;
  const int dotCx = r.x + kRowPadX + kTypeDot / 2;
  d->fillCircle(dotCx, cy, kTypeDot / 2, typeColour(row.type));

  const int right = r.right() - kRowPadX;
  // EST-342: the favourite star sits at the far right, so it is seen before the age; the age and the
  // note shift left by its width, never under it.
  int ageRight = right;
  if (row.favourite) {
    drawTextRight(d, FontRole::SansBody, kAcc, row.selected ? kSelBg : kScreen, right, cy, "*");
    ageRight -= textWidth(d, FontRole::SansBody, "*") + kRowGap;
  }
  const int ageW = textWidth(d, FontRole::MonoMeta, row.age);
  const int ageX = ageRight - ageW;
  int cursor = ageX - kRowGap;
  int noteRight = cursor;
  int noteW = 0;
  if (row.note && row.note[0]) {
    noteW = textWidth(d, FontRole::MonoMeta, row.note);
    cursor -= noteW + kRowGap;
  }
  int barsX = cursor - (kBarsCount * (kBarsW + kBarsGap) - kBarsGap);
  if (row.rssi == kNoRssi) barsX = cursor;
  else cursor = barsX - kRowGap;

  const int nameX = r.x + kRowPadX + kTypeDot + kRowGap;
  const int nameMaxW = cursor - nameX;
  drawTextEllipsis(d, FontRole::SansBody, kInk, row.selected ? kSelBg : kScreen, nameX, cy,
               nameMaxW, row.name);

  if (row.note && row.note[0]) {
    drawTextRight(d, FontRole::MonoMeta, kDimmer, row.selected ? kSelBg : kScreen, noteRight, cy,
              row.note);
  }
  if (row.rssi != kNoRssi) {
    drawSignalBars(d, barsX, r.y + r.h - kRowPadY, row.rssi);
  }
  drawTextRight(d, FontRole::MonoMeta, kDim, row.selected ? kSelBg : kScreen, ageX + ageW, cy,
            row.age);

  const int lineY = r.bottom() - 1;
  if (lineY >= r.y) d->drawFastHLine(r.x, lineY, r.w, kRowLine);
}

void drawEmptyState(lgfx::LovyanGFX* d, const Rect& area, const char* title, const char* sub) {
  const int cx = area.x + area.w / 2;
  const int cy = area.y + area.h / 2;
  // An empty state may be one line only (the chat's "No messages yet", EST-336): no orphan second
  // line, and the title sits on the band's centre instead of 9 px above it.
  const bool hasSub = sub != nullptr && sub[0] != 0;
  setRole(d, FontRole::SansBody);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(kInk, kScreen);
  d->drawString(title, cx, hasSub ? cy - 9 : cy);
  if (!hasSub) return;
  d->setTextColor(kDim, kScreen);
  d->drawString(sub, cx, cy + 9);
}

// --- the shared keyboard sheet's drawing (EST-352) ---------------------------------------------

void drawKeyCap(lgfx::LovyanGFX* d, const Rect& r, const KeyDef& k, bool shift) {
  const bool on = (k.kind == KeyKind::Send) || (k.kind == KeyKind::Shift && shift);
  const uint16_t bg = on ? kSelBg2 : kKeyBg;
  d->fillRoundRect(r.x, r.y, r.w, r.h, kKeyRadius, bg);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kKeyRadius, on ? kAcc : kKeyLine);
  const uint16_t fg = on ? kAccInk : kInk;
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  if (k.kind == KeyKind::Del) {   // a vector: the ASCII faces carry no backspace glyph
    const int hw = 4;
    const int hh = 4;
    d->drawLine(cx + hw, cy - hh, cx - hw, cy - hh, fg);
    d->drawLine(cx - hw, cy - hh, cx - hw - 3, cy, fg);
    d->drawLine(cx - hw - 3, cy, cx - hw, cy + hh, fg);
    d->drawLine(cx - hw, cy + hh, cx + hw, cy + hh, fg);
    d->drawLine(cx + hw, cy + hh, cx + hw, cy - hh, fg);
    d->drawLine(cx - 3, cy - 2, cx + 1, cy + 2, fg);
    d->drawLine(cx + 1, cy - 2, cx - 3, cy + 2, fg);
    return;
  }
  if (k.kind == KeyKind::Emoji) {   // a vector smiley, for the same reason
    const int rad = 5;
    for (int y = -rad; y <= rad; ++y) {
      for (int x = -rad; x <= rad; ++x) {
        const int dd = x * x + y * y;
        if (dd <= rad * rad && dd >= (rad - 1) * (rad - 1)) {
          d->drawPixel((int16_t)(cx + x), (int16_t)(cy + y), fg);
        }
      }
    }
    d->drawPixel((int16_t)(cx - 2), (int16_t)(cy - 1), fg);
    d->drawPixel((int16_t)(cx + 2), (int16_t)(cy - 1), fg);
    d->drawLine(cx - 2, cy + 2, cx, cy + 3, fg);
    d->drawLine(cx, cy + 3, cx + 2, cy + 2, fg);
    return;
  }
  char label[8];
  snprintf(label, sizeof(label), "%s",
           k.kind == KeyKind::Shift ? (shift ? "CAPS" : "shift") : k.label);
  if (shift && label[0] >= 'a' && label[0] <= 'z') label[0] = (char)(label[0] - 32);
  setRole(d, FontRole::MonoMeta);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(fg, bg);
  d->drawString(label, cx, cy);
}

// EST-390: the bubble's LOOK, at a rectangle the caller placed. The accent preview (EST-346/352) and
// the new key preview are the same drawing: one panel2 rounded plate, one gold rim, one character in
// the title face. Keeping both on this one function is why the two bubbles cannot drift apart.
void drawPreviewBubble(lgfx::LovyanGFX* d, const Rect& box, uint32_t cp) {
  if (d == nullptr || cp == 0 || box.empty()) return;
  char letter[5] = {0};
  const int n = utf8Encode(cp, letter);
  if (n <= 0) return;
  letter[n] = 0;
  d->fillRoundRect(box.x, box.y, box.w, box.h, kOskRadius, kPanel2);
  d->drawRoundRect(box.x, box.y, box.w, box.h, kOskRadius, kAcc);
  setRole(d, FontRole::SansTitle);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(kInk, kPanel2);
  d->drawString(letter, box.x + box.w / 2, box.y + box.h / 2);
}

void drawAccentBubble(lgfx::LovyanGFX* d, const Rect& key, uint32_t cp) {
  if (d == nullptr || cp == 0) return;
  int16_t x = (int16_t)(key.x + key.w / 2 - kAccentBubbleW / 2);
  int16_t y = (int16_t)(key.y - kAccentBubbleGap - kAccentBubbleH);
  if (x < 2) x = 2;
  if (x > kScreenW - 2 - kAccentBubbleW) x = (int16_t)(kScreenW - 2 - kAccentBubbleW);
  if (y < kStatusH + 2) y = (int16_t)(kStatusH + 2);
  drawPreviewBubble(d, Rect{x, y, kAccentBubbleW, kAccentBubbleH}, cp);
}

}  // namespace ui
}  // namespace om
