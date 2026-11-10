// SPDX-License-Identifier: MIT
// Frame-only widgets. All drawing goes through the LovyanGFX surface passed in; the caller owns the
// omFrameBegin/omFrameEnd pair (screen_nodes.cpp opens it).
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>

#include "ui/fonts.h"

namespace om {
namespace ui {
namespace {

void applyRole(lgfx::LovyanGFX* d, FontRole role) {
  const lgfx::IFont* f = fontFor(role);
  d->setFont(f ? f : &lgfx::fonts::Font0);
}

void textLeft(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int x, int y,
              const char* s) {
  applyRole(d, role);
  d->setTextDatum(lgfx::textdatum_t::middle_left);
  d->setTextColor(fg, bg);
  d->drawString(s, x, y);
}

int textRight(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int right, int y,
              const char* s) {
  applyRole(d, role);
  const int w = d->textWidth(s);
  d->setTextDatum(lgfx::textdatum_t::middle_right);
  d->setTextColor(fg, bg);
  d->drawString(s, right, y);
  return w;
}

// ASCII only by design (the fonts cover 0x20-0x7E); never draws outside maxW.
void textEllipsis(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int x, int y,
                  int maxW, const char* s) {
  applyRole(d, role);
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

void drawGear(lgfx::LovyanGFX* d, const Rect& r, uint16_t col) {
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  d->drawCircle(cx, cy, 3, col);
  d->fillCircle(cx, cy, 1, col);
  static const int8_t dx[8] = {3, 2, 0, -2, -3, -2, 0, 2};
  static const int8_t dy[8] = {0, 2, 3, 2, 0, -2, -3, -2};
  for (int i = 0; i < 8; ++i) {
    d->drawLine(cx + dx[i], cy + dy[i], cx + dx[i] * 5 / 3, cy + dy[i] * 5 / 3, col);
  }
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
  applyRole(d, role);
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
  snprintf(left, sizeof(left), "%s . %s . %s", m.protocol, m.region, m.hash);
  const int leftX = r.x + kStatusPadX + kStatusDot + kStatusGap;
  textLeft(d, FontRole::MonoStatus, kDim, kBar, leftX, cy, left);

  // Right-hand group, laid out from the gear leftwards with the same measurements that draw it.
  const Rect gear = L.gear();
  drawGear(d, gear, kDim);
  int rx = gear.x - kStatusGap;

  rx -= textRight(d, FontRole::MonoStatus, kDim, kBar, rx, cy, m.clock) + kStatusGap;

  char pct[8];
  if (m.batteryPct < 0) snprintf(pct, sizeof(pct), "--");
  else snprintf(pct, sizeof(pct), "%d%%", m.batteryPct);
  const int pctW = textRight(d, FontRole::MonoStatus, kDim, kBar, rx, cy, pct);
  rx -= pctW + 2;
  drawBattery(d, rx - kBattW, cy, m.batteryPct);
  rx -= kBattW + kStatusGap;

  char gps[12];
  if (m.gpsValid) snprintf(gps, sizeof(gps), "GPS %d", m.gpsSats);
  else snprintf(gps, sizeof(gps), "GPS --");
  rx -= textRight(d, FontRole::MonoStatus, kDim, kBar, rx, cy, gps) + kStatusGap;

  char nodes[16];
  snprintf(nodes, sizeof(nodes), "%d %s", m.nodes, m.nodes == 1 ? "node" : "nodes");
  textRight(d, FontRole::MonoStatus, kDim, kBar, rx, cy, nodes);
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

    applyRole(d, FontRole::SansTab);
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
    applyRole(d, FontRole::SansTab);
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
  textEllipsis(d, FontRole::SansBody, kInk, row.selected ? kSelBg : kScreen, nameX, cy,
               nameMaxW, row.name);

  if (row.note && row.note[0]) {
    textRight(d, FontRole::MonoMeta, kDimmer, row.selected ? kSelBg : kScreen, noteRight, cy,
              row.note);
  }
  if (row.rssi != kNoRssi) {
    drawSignalBars(d, barsX, r.y + r.h - kRowPadY, row.rssi);
  }
  textRight(d, FontRole::MonoMeta, kDim, row.selected ? kSelBg : kScreen, ageX + ageW, cy,
            row.age);

  const int lineY = r.bottom() - 1;
  if (lineY >= r.y) d->drawFastHLine(r.x, lineY, r.w, kRowLine);
}

void drawEmptyState(lgfx::LovyanGFX* d, const Rect& area, const char* title, const char* sub) {
  const int cx = area.x + area.w / 2;
  const int cy = area.y + area.h / 2;
  applyRole(d, FontRole::SansBody);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(kInk, kScreen);
  d->drawString(title, cx, cy - 9);
  d->setTextColor(kDim, kScreen);
  d->drawString(sub, cx, cy + 9);
}

}  // namespace ui
}  // namespace om
