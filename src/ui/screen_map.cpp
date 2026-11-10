// SPDX-License-Identifier: MIT
// Map screen: the new layer's chrome over map.cpp's tiles. Everything is drawn into the off-screen
// frame, once, then pushed by omFrameEnd(); the toast rides on top as always.
//
// EST-368: no distance scale; the Traces tab toggles OM's animated routes, drawn over the tiles at
// the map's own frame rate. EST-370 (Filip ~23:40): the attribution is plain dim text, the Names tab
// hides every label, and a small marker shows the map's centre.
// EST-372 (Filip ~23:55 and ~00:00): the centre marker is a thin 9 px cross, the traces are a bolder
// fuchsia-violet at 2 px with a 5 px dot and a dark outline, the right-hand keys are tabs flush with
// the screen edge, and the "offline · z13" chip is a plain zN line above them.
#include "ui/screen_map.h"

#include <Arduino.h>
#include <math.h>

#include "board.h"
#include "map.h"
#include "../fonts.h"

namespace om {
namespace ui {
namespace {

// EST-372: a tab coming out of the right edge. The rounded left corners are the visible ones; the
// rectangle is extended past the screen by the radius, so the right corners are clipped away and
// the visible right side is square with no border and no gap. One call for the fill and the edge.
void edgeTabBox(lgfx::LovyanGFX* d, const Rect& r, uint16_t fill, uint16_t line) {
  d->fillRoundRect(r.x, r.y, (int16_t)(r.w + kRadius), r.h, kRadius, fill);
  d->drawRoundRect(r.x, r.y, (int16_t)(r.w + kRadius), r.h, kRadius, line);
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

// EST-370 (Filip ~23:40: "no outline on the credits!"): the attribution is plain small dim text
// straight on the tiles. setTextColor(fg) with no background makes LovyanGFX draw only the glyph
// pixels (the tile shows through the anti-aliased edges), so nothing is drawn around or behind it.
void drawPlainTextOnMap(lgfx::LovyanGFX* d, FontRole role, uint16_t ink, const Rect& box,
                        const char* s) {
  if (d == nullptr || s == nullptr || s[0] == 0 || box.w <= 0) return;
  setRole(d, role);
  d->setTextDatum(lgfx::textdatum_t::middle_left);
  d->setTextColor(ink);
  d->drawString(s, box.x, box.y + box.h / 2);
}

// EST-372: the centre cross, above the tiles (and the route) but below the markers and the tabs.
// Two 1 px kInk lines, 9 px long; a 1 px kMapBg outline under them keeps the white line visible on
// a bright topo tile as well as on a dark one. It is not a crosshair: the Position picker keeps its
// own crosshair and never calls this screen.
void drawCentreCross(lgfx::LovyanGFX* d, const MapChrome& c) {
  const Rect r = mapCentreCrossRect(c);
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  const int arm = kMapCentreCrossArm;
  const int span = 2 * arm + 1;
  d->fillRect((int16_t)(cx - arm), (int16_t)(cy - 1), (int16_t)span, 3, kMapBg);  // dark outline
  d->fillRect((int16_t)(cx - 1), (int16_t)(cy - arm), 3, (int16_t)span, kMapBg);
  d->fillRect((int16_t)(cx - arm), (int16_t)cy, (int16_t)span, 1, kInk);           // the cross
  d->fillRect((int16_t)cx, (int16_t)(cy - arm), 1, (int16_t)span, kInk);
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

// One blended line from a to b, pixel by pixel. `w` makes it a band of w px (EST-372: the tail is
// 2 px). The colour is mixed with the tile under each pixel, so the line reads on a bright map as
// well as on a dark one.
void blendLine(lgfx::LovyanGFX* d, int x0, int y0, int x1, int y1, uint16_t col, uint8_t alpha,
               int w) {
  const int dx = x1 - x0;
  const int dy = y1 - y0;
  const int n = (abs(dx) > abs(dy) ? abs(dx) : abs(dy));
  if (n <= 0) {
    for (int o = 0; o < w; ++o) {
      const int x = x0 + (w > 1 ? o : 0);
      const int y = y0;
      d->drawPixel((int16_t)x, (int16_t)y, mixOverFrame(x, y, col, alpha));
    }
    return;
  }
  const bool horizontal = abs(dx) >= abs(dy);
  for (int i = 0; i <= n; ++i) {
    const int x = x0 + (dx * i) / n;
    const int y = y0 + (dy * i) / n;
    for (int o = 0; o < w; ++o) {
      const int xx = horizontal ? x + o : x;
      const int yy = horizontal ? y : y + o;
      d->drawPixel((int16_t)xx, (int16_t)yy, mixOverFrame(xx, yy, col, alpha));
    }
  }
}

// The whole route: 2 px wide, the mock's 2 on / 4 off dash kept for the unknown hops. EST-372 draws
// it at full opacity; the old 1 px line at 34 % vanished into the light topo tiles.
void drawTraceRoute(lgfx::LovyanGFX* d, const TraceFrame& f) {
  for (int i = 0; i + 1 < f.routeCount; ++i) {
    const int x0 = f.route[i].x, y0 = f.route[i].y;
    const int x1 = f.route[i + 1].x, y1 = f.route[i + 1].y;
    const int dx = x1 - x0, dy = y1 - y0;
    const int n = (abs(dx) > abs(dy) ? abs(dx) : abs(dy));
    if (n <= 0) continue;
    const bool horizontal = abs(dx) >= abs(dy);
    for (int k = 0; k <= n; ++k) {
      if ((k % 6) >= 2) continue;   // 2 on, 4 off
      const int x = x0 + (dx * k) / n;
      const int y = y0 + (dy * k) / n;
      for (int o = 0; o < kTraceRouteW; ++o) {
        const int xx = horizontal ? x + o : x;
        const int yy = horizontal ? y : y + o;
        d->drawPixel((int16_t)xx, (int16_t)yy, kTraceLine);
      }
    }
  }
}

// The dot and its tail, above the markers: a 5 px dot with a 1 px dark outline (a 2 px dark ring
// would swallow it, one dark pixel is enough to read on a bright tile) and a 2 px fading tail.
void drawTraceMoving(lgfx::LovyanGFX* d, const TraceFrame& f) {
  for (int i = 0; i + 1 < f.tailCount; ++i) {
    blendLine(d, f.tail[i].x, f.tail[i].y, f.tail[i + 1].x, f.tail[i + 1].y, kTraceTail,
              f.tailAlpha, kTraceTailW);
  }
  if (f.hasDot) {
    const uint16_t ring = mixOverFrame(f.dot.x, f.dot.y, kTraceDotOutline, f.dotAlpha);
    d->fillCircle(f.dot.x, f.dot.y, (int16_t)(kTraceDotR + kTraceDotOutlineW), ring);
    d->fillCircle(f.dot.x, f.dot.y, kTraceDotR,
                  mixOverFrame(f.dot.x, f.dot.y, kTraceDot, f.dotAlpha));
  }
}

void drawTrace(lgfx::LovyanGFX* d, const TraceFrame& f, bool movingOnly) {
  if (!f.live) return;
  if (!movingOnly) drawTraceRoute(d, f);
  if (f.direct) {
    if (f.ringAlpha > 0 && f.ringR > 0) {
      const uint16_t col = mixOverFrame(f.ringX + f.ringR, f.ringY, kTraceRing, f.ringAlpha);
      // EST-372: the same colour, 2 px instead of 1.
      for (int w = 0; w < kTraceRingW; ++w) {
        const int r = f.ringR - w;
        if (r > 0) d->drawCircle(f.ringX, f.ringY, (int16_t)r, col);
      }
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

  // EST-370 (Filip ~23:40): the tile attribution is PLAIN small dim text on the map: no outline, no
  // plate, nothing around it.
  if (om::mapReady()) {
    const int creditW =
        measureText(d, FontRole::MonoMeta, kMapCreditText, (int)strlen(kMapCreditText));
    const Rect cr = mapCreditRect(c, (int16_t)creditW);
    if (!cr.empty()) {
      if (creditW <= cr.w) {
        drawPlainTextOnMap(d, FontRole::MonoMeta, kMapCredit, cr, kMapCreditText);
      } else {
        // A box too narrow for the whole line (no real screen is): shorten it so it never runs
        // under the tab column.
        drawTextEllipsis(d, FontRole::MonoMeta, kMapCredit, kMapBg, cr.x, cr.y + cr.h / 2, cr.w,
                         kMapCreditText);
      }
    }
  }

  // The route belongs under the markers; the moving dot goes on top of them (below).
  if (!v.trace.direct) drawTrace(d, v.trace, false);

  // EST-372: the centre cross, above the tiles and the route, below the markers and the tabs.
  drawCentreCross(d, c);

  // Node markers over the tiles, our own position last so it is never hidden by a contact.
  for (int i = 0; i < v.markerCount; ++i) {
    if (v.markers[i].self) continue;
    drawMarker(d, v.markers[i]);
  }
  for (int i = 0; i < v.markerCount; ++i) {
    if (v.markers[i].self) drawMarker(d, v.markers[i]);
  }

  // EST-370: the Names tab. Off hides every marker label; the markers themselves stay. When on,
  // every contact is named (clients included), our own marker never (the tab's "Your position").
  if (v.namesOn) {
    for (int i = 0; i < v.markerCount; ++i) {
      const MapMarker& m = v.markers[i];
      if (m.self) continue;
      setRole(d, FontRole::MonoMeta);
      const int w = measureText(d, FontRole::MonoMeta, m.name, (int)strlen(m.name));
      const int lx = m.x - w / 2;
      const int ly = m.y + 11;
      d->fillRect(lx - 2, ly - 5, w + 4, 10, kMapBg);
      drawTextLeft(d, FontRole::MonoMeta, m.selected ? kAccInk : kInk, kMapBg, lx, ly, m.name);
    }
  }

  // The moving dot, its tail and the direct-arrival ring ride above markers and labels.
  if (v.trace.direct) drawTrace(d, v.trace, false);
  else drawTrace(d, v.trace, true);

  // EST-372: the zoom is plain small text, right-aligned with the tabs' right edge, just above the
  // top tab. No plate, no border, no "offline": the map is always offline.
  char zoom[8];
  snprintf(zoom, sizeof(zoom), "z%d", v.zoom);
  setRole(d, FontRole::MonoMeta);
  d->setTextDatum(lgfx::textdatum_t::middle_right);
  d->setTextColor(kDim);
  d->drawString(zoom, c.zoomLabel.right(), c.zoomLabel.y + c.zoomLabel.h / 2);

  // EST-372: the right-hand tabs, flush with the screen edge, rounded on the left only. Zoom,
  // follow, Traces (EST-368) and Names (EST-370), bottom up.
  edgeTabBox(d, c.zoomIn, kPanel, kLine);
  edgeTabBox(d, c.zoomOut, kPanel, kLine);
  edgeTabBox(d, c.lock, v.locked ? kSelBg2 : kPanel, v.locked ? kAcc : kLine);
  edgeTabBox(d, c.traces, v.tracesOn ? kSelBg2 : kPanel, v.tracesOn ? kAcc : kLine);
  edgeTabBox(d, c.names, v.namesOn ? kSelBg2 : kPanel, v.namesOn ? kAcc : kLine);
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
  // The Names icon (tools/mkicons.py): a label tag, gold while names are on.
  const int namesX = c.names.x + (c.names.w - (int)kIconNames.w) / 2;
  const int namesY = c.names.y + (c.names.h - (int)kIconNames.h) / 2;
  drawIcon(d, kIconNames, namesX, namesY, v.namesOn ? kAcc : kInk,
           v.namesOn ? kSelBg2 : kPanel);

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
