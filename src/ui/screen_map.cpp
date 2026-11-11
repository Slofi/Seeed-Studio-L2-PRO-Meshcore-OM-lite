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
// EST-373 (Filip 2026-11-11 ~00:38): the cross is 7 px in ONE dark ink with no outline, the tabs are
// 25 px (their widest icon plus 4 px each side), and the arrow beside the zN line hides the column
// down to a sliver and brings it back.
// EST-374 (Filip 2026-11-11, the rest of the "hide" idea): the canvas is the full map area in both
// states (the bars are drawn OVER it, so hiding them costs a repaint of the chrome and no tile), the
// hide key is a 22 px chip with a chevron, the BOTTOM bar tucks with the tabs (both down to a 3 px
// strip), the zoom + / - stay behind as grey ghosts while hidden, an unread dot rides on the chip,
// and the two groups slide together in a few frames of chrome-only repaint.
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

// EST-373: hidden, the same tab is only the 3 px (kMapTabSliverW) that still fits at the screen
// edge. It is drawn square: it is the middle of a tab, not a rounded left end, so a 3 px sliver does
// not turn into a 3 px smear of the corner radius. One line on its left edge says "a tab is here".
void edgeTabSliver(lgfx::LovyanGFX* d, const Rect& r, uint16_t fill, uint16_t line) {
  d->fillRect(r.x, r.y, r.w, r.h, fill);
  d->drawFastVLine(r.x, r.y, r.h, line);
}

void drawLockIcon(lgfx::LovyanGFX* d, const Rect& r, bool on) {
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  const uint16_t fg = on ? kAcc : kInk;
  d->drawCircle(cx, cy, kMapLockR, fg);
  d->drawFastHLine(cx - kMapLockArmOut, cy, kMapLockArmLen, fg);
  d->drawFastHLine(cx + kMapLockArmOut - kMapLockArmLen + 1, cy, kMapLockArmLen, fg);
  d->drawFastVLine(cx, cy - kMapLockArmOut, kMapLockArmLen, fg);
  d->drawFastVLine(cx, cy + kMapLockArmOut - kMapLockArmLen + 1, kMapLockArmLen, fg);
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

// EST-373 (Filip: "just a cross, small, thin, no outlines"): the centre cross, above the tiles (and
// the route) but below the markers and the tabs. TWO 1 px dark lines, 7 px long (arm 3), one ink
// (kMapCrossInk) and nothing behind them. It is not a crosshair: the Position picker keeps its own
// crosshair and never calls this screen.
void drawCentreCross(lgfx::LovyanGFX* d, const MapChrome& c) {
  const Rect r = mapCentreCrossRect(c);
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  const int arm = kMapCentreCrossArm;
  const int span = 2 * arm + 1;
  d->fillRect((int16_t)(cx - arm), (int16_t)cy, (int16_t)span, 1, kMapCrossInk);
  d->fillRect((int16_t)cx, (int16_t)(cy - arm), 1, (int16_t)span, kMapCrossInk);
}

// EST-374: the chevron inside the hide/show chip. ">" while the bars are out, "<" while they are
// tucked. A 4 x 7 glyph with a 2 px stroke, drawn from primitives: two diagonals of three 1 x 2 px
// blocks each, meeting at the point. The columns narrow from the outside in, so the stroke stays
// 2 px on the diagonal instead of the 1 px a 45 degree line of single pixels would give.
void drawChevron(lgfx::LovyanGFX* d, const Rect& r, bool left, uint16_t ink) {
  const int n = (r.w < (r.h + 1) / 2 ? r.w : (r.h + 1) / 2);   // number of 1 px columns
  for (int i = 0; i < n; ++i) {
    const int x = left ? (r.x + r.w - 1 - i) : (r.x + i);
    const int top = r.y + i;
    const int bot = r.y + r.h - 2 - i;   // the lower arm's top row, 2 px tall
    d->fillRect((int16_t)x, (int16_t)top, 1, kMapChevronStroke, ink);
    d->fillRect((int16_t)x, (int16_t)bot, 1, kMapChevronStroke, ink);
  }
}

// One zoom glyph, drawn exactly once (EST-374): the shown tab paints it on its kPanel body in kInk,
// and the hidden state paints the SAME character at the SAME centre in kMapGhostInk with no body
// ("same shape, size, stroke, only the colours inverted"). `plate` is the tab's background; without
// it LovyanGFX draws only the glyph's own pixels, so the topo shows through.
void drawZoomGlyph(lgfx::LovyanGFX* d, const Rect& box, char glyph, uint16_t ink, bool plate) {
  setRole(d, FontRole::SansTitle);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  if (plate) d->setTextColor(ink, kPanel);
  else d->setTextColor(ink);
  char s[2] = {glyph, 0};
  d->drawString(s, box.x + box.w / 2, box.y + box.h / 2);
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

// EST-374: the hide/show chip. kPanel fill, kLine edge, rounded; inside it the chevron in kInk. On
// the chip's top-left corner a 6 px gold dot while a chat has something unread AND the bars are
// hidden (shown, the bottom bar's own badge says it). The dot's first row is the map area's first
// row, so the status bar, which is drawn last, never covers it.
void drawChip(lgfx::LovyanGFX* d, const MapChrome& c, int unread) {
  d->fillRoundRect(c.chip.x, c.chip.y, c.chip.w, c.chip.h, kRadius, kPanel);
  d->drawRoundRect(c.chip.x, c.chip.y, c.chip.w, c.chip.h, kRadius, kLine);
  drawChevron(d, c.chevron, c.tabsHidden, kInk);
  if (mapChipShowsUnread(c.tabsHidden, unread)) {
    const Rect u = mapChipUnreadDotRect(c);
    d->fillSmoothCircle((int16_t)(u.x + u.w / 2), (int16_t)(u.y + u.h / 2),
                        (int16_t)(kMapUnreadDotD / 2), kAcc);
  }
}

// EST-374: the tucked bottom bar. Three rows on the screen's floor in kPanel, with the active
// (Map) tab's gold segment at their top: the same x range and the same 2 px (kTabTopLine) the bar
// itself gives that tab, which is what a real bar slid down by kMapBarSlideDy leaves visible.
// It takes no taps: mapHitAt has no rectangle of this strip, so the map gets those rows.
void drawBarStrip(lgfx::LovyanGFX* d, const MapChrome& c, const TabBarModel& tabs) {
  const Rect s = c.barStrip;
  if (s.empty()) return;
  d->fillRect(s.x, s.y, s.w, s.h, kPanel);
  const Rect t = l2().tab(tabs.active);
  d->fillRect(t.x, s.y, t.w, kTabTopLine, kAcc);
}

// The five tabs in their SHOWN geometry, shifted right by `dx` (0 = the steady shown state). Used
// by the steady state and by the hide/show slide: the tab rides out of the screen edge with its
// icon, and the frame clips what is past x 320. Est-373's rule stands: the tap list and this list
// are built from the same mapChromeFor() call, so a drawn tab is a tappable tab.
void drawTabFace(lgfx::LovyanGFX* d, const MapChrome& shown, const MapView& v, int16_t dx) {
  MapTab tabs[kMapTabCount];
  mapTabRects(shown, tabs);
  for (int i = 0; i < kMapTabCount; ++i) {
    Rect r = tabs[i].rect;
    r.x = (int16_t)(r.x + dx);
    bool on = false;
    switch (tabs[i].kind) {
      case MapHitKind::Lock: on = v.locked; break;
      case MapHitKind::Traces: on = v.tracesOn; break;
      case MapHitKind::Names: on = v.namesOn; break;
      default: break;
    }
    edgeTabBox(d, r, on ? kSelBg2 : kPanel, on ? kAcc : kLine);
  }
  for (int i = 0; i < kMapTabCount; ++i) {
    Rect r = tabs[i].rect;
    r.x = (int16_t)(r.x + dx);
    switch (tabs[i].kind) {
      case MapHitKind::ZoomIn:
        drawZoomGlyph(d, r, '+', kInk, true);
        break;
      case MapHitKind::ZoomOut:
        drawZoomGlyph(d, r, '-', kInk, true);
        break;
      case MapHitKind::Lock:
        drawLockIcon(d, r, v.locked);
        break;
      case MapHitKind::Traces: {
        // The Traces icon (tools/mkicons.py): a route with its hop dots, gold while traces are on.
        const int iconX = r.x + (r.w - (int)kIconTrace.w) / 2;
        const int iconY = r.y + (r.h - (int)kIconTrace.h) / 2;
        drawIcon(d, kIconTrace, iconX, iconY, v.tracesOn ? kAcc : kInk,
                 v.tracesOn ? kSelBg2 : kPanel);
        break;
      }
      case MapHitKind::Names: {
        // The Names icon (tools/mkicons.py): a label tag, gold while names are on.
        const int namesX = r.x + (r.w - (int)kIconNames.w) / 2;
        const int namesY = r.y + (r.h - (int)kIconNames.h) / 2;
        drawIcon(d, kIconNames, namesX, namesY, v.namesOn ? kAcc : kInk,
                 v.namesOn ? kSelBg2 : kPanel);
        break;
      }
      default:
        break;
    }
  }
}

// The steady hidden column: five 3 px slivers (a tap on one brings both groups back), and the two
// zoom glyphs left behind at the SHOWN tab centres, in the grey ghost ink, with no tab body.
void drawTabsHidden(lgfx::LovyanGFX* d, const MapChrome& c, const MapView& v) {
  MapTab tabs[kMapTabCount];
  mapTabRects(c, tabs);
  for (int i = 0; i < kMapTabCount; ++i) {
    bool on = false;
    switch (tabs[i].kind) {
      case MapHitKind::Lock: on = v.locked; break;
      case MapHitKind::Traces: on = v.tracesOn; break;
      case MapHitKind::Names: on = v.namesOn; break;
      default: break;
    }
    edgeTabSliver(d, tabs[i].rect, on ? kSelBg2 : kPanel, on ? kAcc : kLine);
  }
  drawZoomGlyph(d, c.zoomInShown, '+', kMapGhostInk, false);
  drawZoomGlyph(d, c.zoomOutShown, '-', kMapGhostInk, false);
}

// One frame. `freshTiles` picks mapRender() (decode + composite) or mapRepush() (the canvas that is
// already in PSRAM). `slide` is null for the steady states; otherwise the tabs ride out by tabDx and
// the whole bottom bar sits barDy rows lower (both from kMapSlideSteps, chrome only).
struct MapSlideFrame {
  int16_t tabDx = 0;
  int16_t barDy = 0;
};

void drawMapFrame(const MapView& v, bool freshTiles, const MapSlideFrame* slide) {
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
    om::mapRepush();   // EST-368/374: an animation frame, the full-height canvas already composited
  }

  if (!om::mapReady()) {
    drawEmptyState(d, Rect{0, kMapCanvasTop, l2().width, kMapCanvasH}, "Map pack not open.",
                   "The card needs /maps/slo/tiles.pack.");
  }

  const MapChrome c = mapScreenChrome(v);

  // EST-370 (Filip ~23:40): the tile attribution is PLAIN small dim text on the map: no outline, no
  // plate, nothing around it. EST-374: its floor follows the bar (shown) or the tucked strip, so the
  // licence line stays on the glass in both states.
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
  // EST-374: it is the centre of the FULL canvas in both states, so it never jumps on a toggle.
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

  // EST-372/373: the zoom is plain small text on the label line above the top tab. EST-374: it sits
  // immediately LEFT of the chip, on the chip's centre line, in the dark map ink (the cross's ink)
  // so it reads on a light topo tile. It is drawn in both states and never moves.
  char zoom[8];
  snprintf(zoom, sizeof(zoom), "z%d", v.zoom);
  setRole(d, FontRole::MonoMeta);
  d->setTextDatum(lgfx::textdatum_t::middle_right);
  d->setTextColor(kMapCrossInk);
  d->drawString(zoom, c.zoomLabel.right(), c.zoomLabel.y + c.zoomLabel.h / 2);

  // EST-374: the chip (always drawn: it is the one way back), with its chevron and the unread dot.
  drawChip(d, c, v.tabs.unread);

  // EST-373/374: drawn from the SAME list the hit test reads (mapTabRects), so a tab's tap zone and
  // its rectangle cannot drift apart. Shown: flush with the screen edge, rounded on the left only.
  // Hidden: the 3 px sliver and, at the shown centres, the grey ghost + / -. Sliding: the shown
  // tabs shifted right by tabDx.
  if (slide != nullptr) {
    const MapChrome shown = mapChromeFor(l2(), v.callout, false);
    drawTabFace(d, shown, v, slide->tabDx);
  } else if (c.tabsHidden) {
    drawTabsHidden(d, c, v);
  } else {
    drawTabFace(d, c, v, 0);
  }

  // EST-374: the bottom page bar. Shown it is the real bar; hidden it is the 3 px strip; mid-slide
  // it is the real bar `barDy` rows lower, clipped by the screen floor.
  if (slide != nullptr) {
    drawTabBarAt(d, l2(), v.tabs, slide->barDy);
  } else if (c.tabsHidden) {
    drawBarStrip(d, c, v.tabs);
  } else {
    drawTabBar(d, l2(), v.tabs);
  }

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
  om::useAppFont(d);
  omFrameEnd();
}

}  // namespace

MapChrome mapScreenChrome(const MapView& v) {
  return mapChromeFor(l2(), v.callout, v.tabsHidden);
}

void mapScreenDraw(const MapView& v) { drawMapFrame(v, true, nullptr); }
void mapScreenAnimate(const MapView& v) { drawMapFrame(v, false, nullptr); }

void mapScreenSlide(const MapView& v, int16_t tabDx, int16_t barDy) {
  MapSlideFrame slide;
  slide.tabDx = tabDx;
  slide.barDy = barDy;
  drawMapFrame(v, false, &slide);
}

}  // namespace ui
}  // namespace om
