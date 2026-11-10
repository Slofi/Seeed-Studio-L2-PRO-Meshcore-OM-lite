// SPDX-License-Identifier: MIT
// Frame-only widgets. All drawing goes through the LovyanGFX surface passed in; the caller owns the
// omFrameBegin/omFrameEnd pair (screen_nodes.cpp opens it).
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>

#include "ui/badge_font.h"
#include "ui/fonts.h"
#include "ui/icons.h"
#include "ui/status_cells.h"
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

bool fontHasGlyph(FontRole role, uint32_t cp) {
  if (cp >= 0x20 && cp <= 0x7E) return true;
  // Only the mono blob was generated with the mock's U+00B7 MIDDLE DOT (tools/mkvlw.py --extra 0xb7).
  return cp == 0xB7u && (role == FontRole::MonoMeta || role == FontRole::MonoStatus);
}

namespace {

struct Utf8Ctx {
  lgfx::LovyanGFX* d;
  FontRole role;
};

// Width of one code point, or 0 when the face has no glyph for it (measureUtf8 then uses the
// placeholder width, a space plus 2 px).
int glyphWidthCb(void* ctx, uint32_t cp) {
  Utf8Ctx* c = (Utf8Ctx*)ctx;
  if (c->d == nullptr || !fontHasGlyph(c->role, cp)) return 0;
  char b[5] = {0};
  const int n = utf8Encode(cp, b);
  if (n <= 0) return 0;
  b[n] = 0;
  setRole(c->d, c->role);
  return c->d->textWidth(b);
}

int spaceWidth(lgfx::LovyanGFX* d, FontRole role) {
  setRole(d, role);
  return d->textWidth(" ");
}

// The fallback glyph: one small dim box, never LovyanGFX's full-height missing-glyph rectangle.
void drawMissingGlyph(lgfx::LovyanGFX* d, int x, int cy, int w) {
  d->drawRect(x, cy - kMissingBoxH / 2, w, kMissingBoxH, kDimmer);
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
  int runStart = -1;
  int xCursor = x;
  for (int i = 0; s[i] != 0;) {
    const int at = i;
    const uint32_t cp = utf8Decode(s, &i);
    if (fontHasGlyph(role, cp)) {
      if (runStart < 0) runStart = at;
      continue;
    }
    if (runStart >= 0) xCursor = drawRun(d, xCursor, y, s, runStart, at);
    runStart = -1;
    const int w = spaceWidth(d, role) + kMissingExtra;
    drawMissingGlyph(d, xCursor, y, w);
    xCursor += w;
  }
  if (runStart >= 0) drawRun(d, xCursor, y, s, runStart, (int)strlen(s));
}

int drawTextRight(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int right, int y,
                  const char* s) {
  if (d == nullptr || s == nullptr) return 0;
  const int w = measureText(d, role, s, (int)strlen(s));
  drawTextLeft(d, role, fg, bg, right - w, y, s);
  return w;
}

// Never draws outside maxW; the cut is on a code point boundary and ends with "...".
void drawTextEllipsis(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int x, int y,
                      int maxW, const char* s) {
  if (d == nullptr || s == nullptr || maxW <= 0) return;
  const int len = (int)strlen(s);
  if (measureText(d, role, s, len) <= maxW) {
    drawTextLeft(d, role, fg, bg, x, y, s);
    return;
  }
  const int ellW = measureText(d, role, "...", 3);
  char buf[80];
  int cut = 0;
  int w = 0;
  for (int i = 0; s[i] != 0 && cut < (int)sizeof(buf) - 4;) {
    int at = i;
    utf8Decode(s, &at);
    const int cw = measureText(d, role, s + i, at - i);
    if (w + cw + ellW > maxW) break;
    w += cw;
    cut = at;
    i = at;
  }
  if (cut == 0) return;
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

  // "GPS 7" with a fix, "GPS 0/4" while searching (used/heard), "GPS --" when the module has never
  // talked. The row is measured before anything on the right is drawn: the fitting order is the
  // ticket's (drop the node count's "nodes" word first, then shorten the GPS cell, then drop the
  // node cell), so the left group and the right group are always at least kStatusGap apart.
  GpsCell cell;
  cell.valid = m.gpsValid;
  cell.talked = m.gpsTalked;
  cell.used = m.gpsSats;
  cell.heard = m.gpsHeard;

  char nodesFull[kNodeCellMax];
  char nodesNumber[kNodeCellMax];
  nodeCellText(nodesFull, sizeof(nodesFull), m.nodes, true);
  nodeCellText(nodesNumber, sizeof(nodesNumber), m.nodes, false);

  char gpsFull[kGpsCellMax];
  char gpsShort[kGpsCellMax];
  gpsCellText(gpsFull, sizeof(gpsFull), cell);
  gpsCellTextShort(gpsShort, sizeof(gpsShort), cell);

  StatusRightWidths widths;
  widths.nodes = textWidth(d, FontRole::MonoStatus, nodesFull);
  widths.nodesNumber = textWidth(d, FontRole::MonoStatus, nodesNumber);
  widths.gps = textWidth(d, FontRole::MonoStatus, gpsFull);
  widths.gpsShort = textWidth(d, FontRole::MonoStatus, gpsShort);
  widths.pct = textWidth(d, FontRole::MonoStatus, pct);
  widths.clock = textWidth(d, FontRole::MonoStatus, m.clock);

  const int leftEnd = leftX + textWidth(d, FontRole::MonoStatus, left);
  const StatusRowForm form = chooseStatusRow(r.w, leftEnd, widths);
  const char* nodes = nodesFull;
  const char* gps = gpsFull;
  switch (form) {
    case StatusRowForm::Full: break;
    case StatusRowForm::NodeNumber: nodes = nodesNumber; break;
    case StatusRowForm::NodeNumberGpsShort: nodes = nodesNumber; gps = gpsShort; break;
    case StatusRowForm::NodesDropped: nodes = nullptr; gps = gpsShort; break;
  }

  rx -= drawTextRight(d, FontRole::MonoStatus, kDim, kBar, rx, cy, gps) + kStatusGap;
  if (nodes != nullptr) drawTextRight(d, FontRole::MonoStatus, kDim, kBar, rx, cy, nodes);
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
    const int labelCy = r.bottom() - kTabLabelPadB;
    const uint16_t col = on ? kAcc : kDimmer;
    drawTabIcon(d, t, cx, iconCy, col);

    setRole(d, FontRole::SansTab);
    d->setTextDatum(lgfx::textdatum_t::middle_center);
    d->setTextColor(col, on ? kSelBg : kBar);
    d->drawString(tabLabel(t), cx, labelCy);
  }

  // The unread count sits on the Chat icon, top-right, on the badge widget (theme.h holds its
  // geometry). The ring is the bar colour, so the circle separates from the icon under it.
  if (unread > 0) {
    const Rect box = countBadgeRect(L.tab(Tab::Chat), unread > 9);
    drawCountBadge(d, box, unread, kBar);
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
