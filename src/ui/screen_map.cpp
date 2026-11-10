// SPDX-License-Identifier: MIT
// Map screen: the new layer's chrome over map.cpp's tiles. Everything is drawn into the off-screen
// frame, once, then pushed by omFrameEnd(); the toast rides on top as always.
//
// EST-368: no distance scale; the attribution is written straight on the map with a 1 px outline;
// the Traces key toggles OM's animated routes, drawn over the tiles at the map's own frame rate.
#include "ui/screen_map.h"

#include <Arduino.h>
#include <math.h>

#include "board.h"
#include "map.h"
#include "../fonts.h"

namespace om {
namespace ui {
namespace {

void keyBox(lgfx::LovyanGFX* d, const Rect& r, uint16_t fill, uint16_t line) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, fill);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, line);
}

void drawLockIcon(lgfx::LovyanGFX* d, const Rect& r, bool on) {
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  const uint16_t fg = on ? kAcc : kInk;
  d->drawCircle(cx, cy, 5, fg);
  d->drawFastHLine(cx - 8, cy, 5, fg);
  d->drawFastHLine(cx + 4, cy, 5, fg);
  d->drawFastVLine(cx, cy - 8, 5, fg);
  d->drawFastVLine(cx, cy + 4, 5, fg);
  d->fillCircle(cx, cy, 1, fg);
}

void drawMarker(lgfx::LovyanGFX* d, const MapMarker& m) {
  const int r = m.self ? 5 : kMapMarkerR;
  if (m.self) {
    d->fillCircle(m.x, m.y, r + 4, kSelBg2);   // the gold halo the mock gives .mk.self
    d->fillCircle(m.x, m.y, r + 2, kAcc);
    d->fillCircle(m.x, m.y, r, kWhite);
    d->drawCircle(m.x, m.y, r, kScreen);
  } else {
    const NodeType t = m.type == 2 ? NodeType::Repeater
                     : (m.type == 3 ? NodeType::Room
                     : (m.type == 4 ? NodeType::Sensor : NodeType::Client));
    d->fillCircle(m.x, m.y, r, typeColour(t));
    d->drawCircle(m.x, m.y, r, kScreen);
    if (m.selected) {
      d->drawCircle(m.x, m.y, r + 3, kWhite);
      d->drawCircle(m.x, m.y, r + 4, kSelBg2);
    }
  }
}

// EST-368: text straight on the tiles. LovyanGFX fills a character cell only when the background
// colour differs from the ink, so setTextColor(fg) alone is a transparent draw; the outline is the
// same string at the four 1 px neighbours in a near-black, which is what keeps a small dim line
// legible on a bright topo tile without a box behind it.
void drawTextOnMap(lgfx::LovyanGFX* d, FontRole role, uint16_t ink, const Rect& box, const char* s) {
  if (d == nullptr || s == nullptr || s[0] == 0 || box.w <= 0) return;
  setRole(d, role);
  d->setTextDatum(lgfx::textdatum_t::middle_left);
  const int x = box.x;
  const int cy = box.y + box.h / 2;
  d->setTextColor(kMapCreditEdge);
  d->drawString(s, x - 1, cy);
  d->drawString(s, x + 1, cy);
  d->drawString(s, x, cy - 1);
  d->drawString(s, x, cy + 1);
  d->setTextColor(ink);
  d->drawString(s, x, cy);
}

// A colour laid over whatever the frame already holds (the tiles) by a 0..255 alpha. omFramePixel
// returns the colour as drawn, so this is the tile colour under the pixel, not an assumption.
uint16_t mixOverFrame(int x, int y, uint16_t fg, uint8_t a) {
  if (a == 0) return omFramePixel(x, y);
  if (a == 255) return fg;
  const uint16_t bg = omFramePixel(x, y);
  const uint32_t fr = (fg >> 11) & 0x1F, fg6 = (fg >> 5) & 0x3F, fb = fg & 0x1F;
  const uint32_t br = (bg >> 11) & 0x1F, bg6 = (bg >> 5) & 0x3F, bb = bg & 0x1F;
  const uint32_t inv = 255U - a;
  const uint32_t r = (fr * a + br * inv + 127U) / 255U;
  const uint32_t g = (fg6 * a + bg6 * inv + 127U) / 255U;
  const uint32_t b = (fb * a + bb * inv + 127U) / 255U;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// One blended line from a to b, pixel by pixel. The colour is mixed with the tile under each pixel,
// so the line reads on a bright map as well as on a dark one.
void blendLine(lgfx::LovyanGFX* d, int x0, int y0, int x1, int y1, uint16_t col, uint8_t alpha) {
  const int dx = x1 - x0;
  const int dy = y1 - y0;
  const int n = (abs(dx) > abs(dy) ? abs(dx) : abs(dy));
  if (n <= 0) {
    d->drawPixel((int16_t)x0, (int16_t)y0, mixOverFrame(x0, y0, col, alpha));
    return;
  }
  for (int i = 0; i <= n; ++i) {
    const int x = x0 + (dx * i) / n;
    const int y = y0 + (dy * i) / n;
    d->drawPixel((int16_t)x, (int16_t)y, mixOverFrame(x, y, col, alpha));
  }
}

// The whole route, faint and dashed: 2 px on, 4 px off, as the mock's .tr stroke-dasharray.
void drawTraceRoute(lgfx::LovyanGFX* d, const TraceFrame& f) {
  for (int i = 0; i + 1 < f.routeCount; ++i) {
    const int x0 = f.route[i].x, y0 = f.route[i].y;
    const int x1 = f.route[i + 1].x, y1 = f.route[i + 1].y;
    const int dx = x1 - x0, dy = y1 - y0;
    const int n = (abs(dx) > abs(dy) ? abs(dx) : abs(dy));
    if (n <= 0) continue;
    for (int k = 0; k <= n; ++k) {
      if ((k % 6) >= 2) continue;   // 2 on, 4 off
      const int x = x0 + (dx * k) / n;
      const int y = y0 + (dy * k) / n;
      d->drawPixel((int16_t)x, (int16_t)y, mixOverFrame(x, y, kTraceLine, 86));   // about 34 %
    }
  }
}

// The dot and its tail, above the markers: the dot has a violet ring so it reads on any tile.
void drawTraceMoving(lgfx::LovyanGFX* d, const TraceFrame& f) {
  for (int i = 0; i + 1 < f.tailCount; ++i) {
    blendLine(d, f.tail[i].x, f.tail[i].y, f.tail[i + 1].x, f.tail[i + 1].y, kTraceTail,
              f.tailAlpha);
  }
  if (f.hasDot) {
    d->fillCircle(f.dot.x, f.dot.y, 3, mixOverFrame(f.dot.x, f.dot.y, kTraceDot, f.dotAlpha));
    d->drawCircle(f.dot.x, f.dot.y, 4, mixOverFrame(f.dot.x, f.dot.y, kTraceLine, 200));
  }
}

void drawTrace(lgfx::LovyanGFX* d, const TraceFrame& f, bool movingOnly) {
  if (!f.live) return;
  if (!movingOnly) drawTraceRoute(d, f);
  if (f.direct) {
    if (f.ringAlpha > 0 && f.ringR > 0) {
      const uint16_t col = mixOverFrame(f.ringX + f.ringR, f.ringY, kTraceRing, f.ringAlpha);
      d->drawCircle(f.ringX, f.ringY, f.ringR, col);
      if (f.ringR > 5) d->drawCircle(f.ringX, f.ringY, f.ringR - 1, col);
    }
  } else {
    drawTraceMoving(d, f);
  }
}

void drawMapFrame(const MapView& v, bool freshTiles) {
  if (!omFrameBegin()) {
    omFrameEnd();
    return;
  }
  lgfx::LovyanGFX* d = om_display();
  if (!d) {
    omFrameEnd();
    return;
  }

  d->fillScreen(kMapBg);
  if (freshTiles) {
    om::mapRender();   // the tiles only: this file draws the attribution, chrome and traces
  } else {
    om::mapRepush();   // EST-368: an animation frame, the canvas already composited
  }

  if (!om::mapReady()) {
    drawEmptyState(d, Rect{0, 0, l2().width, (int16_t)l2().tabbar.y}, "Map pack not open.",
                   "The card needs /maps/slo/tiles.pack.");
  }

  const MapChrome c = mapScreenChrome(v);

  // EST-368 (Filip ~21:35): the tile attribution, drawn straight on the map in a small dim face
  // with a 1 px dark outline. No plate behind it, on the bottom left where the scale used to be.
  if (om::mapReady()) {
    const int creditW =
        measureText(d, FontRole::MonoMeta, kMapCreditText, (int)strlen(kMapCreditText));
    const Rect cr = mapCreditRect(c, (int16_t)creditW);
    if (!cr.empty()) {
      if (creditW <= cr.w) {
        drawTextOnMap(d, FontRole::MonoMeta, kMapCredit, cr, kMapCreditText);
      } else {
        // A box too narrow for the whole line: the ellipsis form, on the panel colour, so the cut
        // is visible instead of running under the keys.
        drawTextEllipsis(d, FontRole::MonoMeta, kMapCredit, kMapBg, cr.x, cr.y + cr.h / 2, cr.w,
                         kMapCreditText);
      }
    }
  }

  // The route belongs under the markers; the moving dot goes on top of them (below).
  if (!v.trace.direct) drawTrace(d, v.trace, false);

  // Node markers over the tiles, our own position last so it is never hidden by a contact.
  for (int i = 0; i < v.markerCount; ++i) {
    if (v.markers[i].self) continue;
    drawMarker(d, v.markers[i]);
  }
  for (int i = 0; i < v.markerCount; ++i) {
    if (v.markers[i].self) drawMarker(d, v.markers[i]);
  }

  // Labels: the mock shows every repeater and room, a client only when selected or on a trace.
  for (int i = 0; i < v.markerCount; ++i) {
    const MapMarker& m = v.markers[i];
    if (m.self || (m.type == 1 && !m.selected)) continue;
    setRole(d, FontRole::MonoMeta);
    const int w = measureText(d, FontRole::MonoMeta, m.name, (int)strlen(m.name));
    const int lx = m.x - w / 2;
    const int ly = m.y + 11;
    d->fillRect(lx - 2, ly - 5, w + 4, 10, kMapBg);
    drawTextLeft(d, FontRole::MonoMeta, m.selected ? kAccInk : kInk, kMapBg, lx, ly, m.name);
  }

  // The moving dot, its tail and the direct-arrival ring ride above markers and labels.
  if (v.trace.direct) drawTrace(d, v.trace, false);
  else drawTrace(d, v.trace, true);

  // The info chip (mock .mapinfo): what this map is and at which zoom.
  char chip[24];
  snprintf(chip, sizeof(chip), "offline \xC2\xB7 z%d", v.zoom);
  d->fillRoundRect(c.chip.x, c.chip.y, c.chip.w, c.chip.h, kRadiusSm, kPanel);
  d->drawRoundRect(c.chip.x, c.chip.y, c.chip.w, c.chip.h, kRadiusSm, kLine);
  drawTextLeft(d, FontRole::MonoMeta, kDim, kPanel, c.chip.x + kMapChipPadX,
               c.chip.y + c.chip.h / 2, chip);

  // Zoom, follow and the Traces toggle (EST-368), bottom right, as in the mock.
  keyBox(d, c.zoomIn, kPanel, kLine);
  keyBox(d, c.zoomOut, kPanel, kLine);
  keyBox(d, c.lock, v.locked ? kSelBg2 : kPanel, v.locked ? kAcc : kLine);
  keyBox(d, c.traces, v.tracesOn ? kSelBg2 : kPanel, v.tracesOn ? kAcc : kLine);
  setRole(d, FontRole::SansTitle);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(kInk, kPanel);
  d->drawString("+", c.zoomIn.x + c.zoomIn.w / 2, c.zoomIn.y + c.zoomIn.h / 2);
  d->drawString("-", c.zoomOut.x + c.zoomOut.w / 2, c.zoomOut.y + c.zoomOut.h / 2);
  drawLockIcon(d, c.lock, v.locked);
  // The Traces icon (tools/mkicons.py): a route with its hop dots, gold while traces are on.
  const int iconX = c.traces.x + (c.traces.w - (int)kIconTrace.w) / 2;
  const int iconY = c.traces.y + (c.traces.h - (int)kIconTrace.h) / 2;
  drawIcon(d, kIconTrace, iconX, iconY, v.tracesOn ? kAcc : kInk,
           v.tracesOn ? kSelBg2 : kPanel);

  // The tapped marker's panel (mock .callout): dark gold, gold edge, the name then one mono line.
  if (v.callout && !c.callout.empty()) {
    d->fillRoundRect(c.callout.x, c.callout.y, c.callout.w, c.callout.h, kRadius, kSelBg);
    d->drawRoundRect(c.callout.x, c.callout.y, c.callout.w, c.callout.h, kRadius, kAcc);
    drawTextEllipsis(d, FontRole::SansBody, kAccInk, kSelBg, c.callout.x + 7,
                     c.callout.y + 12, c.callout.w - 14, v.calloutTitle);
    drawTextEllipsis(d, FontRole::MonoMeta, kDim, kSelBg, c.callout.x + 7,
                     c.callout.y + 28, c.callout.w - 14, v.calloutLine);
  }

  drawStatusBar(d, l2(), v.status);
  drawTabBar(d, l2(), v.tabs);
  om::useAppFont(d);
  omFrameEnd();
}

}  // namespace

MapChrome mapScreenChrome(const MapView& v) {
  return mapChromeFor(l2(), v.callout);
}

void mapScreenDraw(const MapView& v) { drawMapFrame(v, true); }
void mapScreenAnimate(const MapView& v) { drawMapFrame(v, false); }

}  // namespace ui
}  // namespace om
