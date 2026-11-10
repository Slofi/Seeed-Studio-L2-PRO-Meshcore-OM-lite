// SPDX-License-Identifier: MIT
// Frame-only widgets. All drawing goes through the LovyanGFX surface passed in; the caller owns the
// omFrameBegin/omFrameEnd pair (screen_nodes.cpp opens it).
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>

#include "ui/fonts.h"
#include "ui/icons.h"

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

void drawBattery(lgfx::LovyanGFX* d, int x, int cy, int pct) {
  const int y = cy - kBattH / 2;
  d->drawRect(x, y, kBattW, kBattH, kDim);
  d->fillRect(x + kBattW, y + 2, 2, kBattH - 4, kDim);
  if (pct < 0) return;
  int fill = (kBattW - 2) * (pct > 100 ? 100 : pct) / 100;
  if (fill < 0) fill = 0;
  if (fill == 0) return;
  const uint16_t c = (pct <= 15) ? kBad : (pct <= 30 ? kWarn : kGreen);
  d->fillRect(x + 1, y + 1, fill, kBattH - 2, c);
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

// Text helpers, public because the screens use the same role fonts and colour pairs. They draw into
// the surface passed in, exactly like everything else here.
void setRole(lgfx::LovyanGFX* d, FontRole role) {
  const lgfx::IFont* f = fontFor(role);
  d->setFont(f ? f : &lgfx::fonts::Font0);
}

void drawTextLeft(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int x, int y,
                  const char* s) {
  setRole(d, role);
  d->setTextDatum(lgfx::textdatum_t::middle_left);
  d->setTextColor(fg, bg);
  d->drawString(s, x, y);
}

int drawTextRight(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int right, int y,
                  const char* s) {
  setRole(d, role);
  const int w = d->textWidth(s);
  d->setTextDatum(lgfx::textdatum_t::middle_right);
  d->setTextColor(fg, bg);
  d->drawString(s, right, y);
  return w;
}

// ASCII only by design (the fonts cover 0x20-0x7E); never draws outside maxW.
void drawTextEllipsis(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int x, int y,
                      int maxW, const char* s) {
  setRole(d, role);
  d->setTextDatum(lgfx::textdatum_t::middle_left);
  d->setTextColor(fg, bg);
  if (maxW <= 0) return;
  if (d->textWidth(s) <= maxW) {
    d->drawString(s, x, y);
    return;
  }
  const int ellW = d->textWidth("...");
  char buf[48];
  int n = 0;
  int w = 0;
  for (; s[n] && n < (int)sizeof(buf) - 4; ++n) {
    buf[n] = s[n];
    buf[n + 1] = 0;
    w = d->textWidth(buf);
    if (w + ellW > maxW) break;
  }
  if (n == 0) return;
  buf[n] = 0;
  strncat(buf, "...", sizeof(buf) - strlen(buf) - 1);
  d->drawString(buf, x, y);
}

int measureText(lgfx::LovyanGFX* d, FontRole role, const char* s, int len) {
  if (d == nullptr || s == nullptr || len <= 0) return 0;
  char buf[128];
  const int n = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
  memcpy(buf, s, (size_t)n);
  buf[n] = 0;
  setRole(d, role);
  return d->textWidth(buf);
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
  setRole(d, role);
  return d->textWidth(text);
}

void drawStatusBar(lgfx::LovyanGFX* d, const Layout& L, const StatusModel& m) {
  const Rect& r = L.status;
  d->fillRect(r.x, r.y, r.w, r.h, kBar);
  d->drawFastHLine(r.x, r.bottom() - 1, r.w, kLine);

  const int cy = r.y + r.h / 2;
  const int dotCx = r.x + kStatusPadX + kStatusDot / 2;
  d->fillCircle(dotCx, cy, kStatusDot / 2, m.meshReady ? kGreen : kDimmer);

  char left[40];
  // The mock separates with U+00B7 MIDDLE DOT, not an ASCII period on the baseline. The mono blob
  // carries that glyph (tools/mkvlw.py --extra 0xb7), so the UTF-8 bytes below draw at x-height.
  snprintf(left, sizeof(left), "%s \xC2\xB7 %s \xC2\xB7 %s", m.protocol, m.region, m.hash);
  const int leftX = r.x + kStatusPadX + kStatusDot + kStatusGap;
  drawTextLeft(d, FontRole::MonoStatus, kDim, kBar, leftX, cy, left);

  // Right-hand group, laid out from the gear leftwards with the same measurements that draw it.
  const Rect gear = L.gear();
  drawGear(d, gear, kDim, kBar);
  int rx = gear.x - kStatusGap;

  rx -= drawTextRight(d, FontRole::MonoStatus, kDim, kBar, rx, cy, m.clock) + kStatusGap;

  char pct[8];
  if (m.batteryPct < 0) snprintf(pct, sizeof(pct), "--");
  else snprintf(pct, sizeof(pct), "%d%%", m.batteryPct);
  const int pctW = drawTextRight(d, FontRole::MonoStatus, kDim, kBar, rx, cy, pct);
  rx -= pctW + kBattGap;   // mock .bits gap: the icon no longer touches "97%"
  drawBattery(d, rx - kBattW, cy, m.batteryPct);
  rx -= kBattW + kStatusGap;

  char gps[12];
  if (m.gpsValid) snprintf(gps, sizeof(gps), "GPS %d", m.gpsSats);
  else snprintf(gps, sizeof(gps), "GPS --");
  rx -= drawTextRight(d, FontRole::MonoStatus, kDim, kBar, rx, cy, gps) + kStatusGap;

  char nodes[16];
  snprintf(nodes, sizeof(nodes), "%d %s", m.nodes, m.nodes == 1 ? "node" : "nodes");
  drawTextRight(d, FontRole::MonoStatus, kDim, kBar, rx, cy, nodes);
}

void drawTabBar(lgfx::LovyanGFX* d, const Layout& L, Tab active, int unread) {
  const Rect& bar = L.tabbar;
  d->fillRect(bar.x, bar.y, bar.w, bar.h, kBar);
  d->drawFastHLine(bar.x, bar.y, bar.w, kLine);

  for (int i = 0; i < kTabCount; ++i) {
    const Tab t = (Tab)i;
    const Rect r = L.tab(t);
    const bool on = (t == active);
    if (on) {
      d->fillRect(r.x, r.y, r.w, r.h, kSelBg);
      d->fillRect(r.x, r.y, r.w, kTabTopLine, kAcc);
    }
    const int cx = r.x + r.w / 2;
    const int iconCy = r.y + kTabPadY + kTabIcon / 2;
    const int labelCy = r.bottom() - 5;
    const uint16_t col = on ? kAcc : kDimmer;
    drawTabIcon(d, t, cx, iconCy, col);

    setRole(d, FontRole::SansTab);
    d->setTextDatum(lgfx::textdatum_t::middle_center);
    d->setTextColor(col, on ? kSelBg : kBar);
    d->drawString(tabLabel(t), cx, labelCy);
  }

  if (unread > 0) {
    const Rect chat = L.tab(Tab::Chat);
    char b[4];
    if (unread > 9) snprintf(b, sizeof(b), "9");
    else snprintf(b, sizeof(b), "%d", unread);
    const int bw = 11;
    const int bx = chat.x + chat.w / 2 + 7;
    const int by = chat.y + 2;
    d->fillRoundRect(bx, by, bw, kBadgeH, kRadius, kBad);
    setRole(d, FontRole::SansTab);
    d->setTextDatum(lgfx::textdatum_t::middle_center);
    d->setTextColor(kWhite, kBad);
    d->drawString(b, bx + bw / 2, by + kBadgeH / 2);
  }
}

void drawListRow(lgfx::LovyanGFX* d, const Rect& r, const RowModel& row) {
  if (r.empty()) return;
  if (row.selected) d->fillRect(r.x, r.y, r.w, r.h, kSelBg);

  const int cy = r.y + r.h / 2;
  const int dotCx = r.x + kRowPadX + kTypeDot / 2;
  d->fillCircle(dotCx, cy, kTypeDot / 2, typeColour(row.type));

  const int right = r.right() - kRowPadX;
  const int ageW = textWidth(d, FontRole::MonoMeta, row.age);
  const int ageX = right - ageW;
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
  setRole(d, FontRole::SansBody);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(kInk, kScreen);
  d->drawString(title, cx, cy - 9);
  d->setTextColor(kDim, kScreen);
  d->drawString(sub, cx, cy + 9);
}

}  // namespace ui
}  // namespace om
