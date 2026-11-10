// SPDX-License-Identifier: MIT
// Map screen: the new layer's chrome over map.cpp's tiles. Everything is drawn into the off-screen
// frame, once, then pushed by omFrameEnd(); the toast rides on top as always.
#include "ui/screen_map.h"

#include <Arduino.h>

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

}  // namespace

MapChrome mapScreenChrome(const MapView& v) {
  return mapChromeFor(l2(), v.zoom, 0.0, v.callout);
}

void mapScreenDraw(const MapView& v) {
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
  om::mapRender();   // the tiles only: the attribution, the chrome and the labels are drawn here

  if (!om::mapReady()) {
    drawEmptyState(d, Rect{0, 0, l2().width, (int16_t)l2().tabbar.y}, "Map pack not open.",
                   "The card needs /maps/slo/tiles.pack.");
  }

  const MapChrome c = mapScreenChrome(v);

  // EST-353: the tile attribution is the map's licence line, not part of the canvas: one dim line on
  // the bottom RIGHT, right-aligned to the key column and measured, so the scale's two labels
  // (bottom left) can never run into it again. drawTextEllipsis keeps it inside the rectangle the
  // same arithmetic returned, and its own background keeps it readable over a bright tile.
  if (om::mapReady()) {
    const int creditW =
        measureText(d, FontRole::MonoMeta, kMapCreditText, (int)strlen(kMapCreditText));
    const Rect cr = mapCreditRect(c, (int16_t)creditW);
    if (!cr.empty()) {
      drawTextEllipsis(d, FontRole::MonoMeta, kMapCredit, kMapBg, cr.x, cr.y + cr.h / 2, cr.w,
                       kMapCreditText);
    }
  }

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

  // The info chip (mock .mapinfo): what this map is and at which zoom.
  char chip[24];
  snprintf(chip, sizeof(chip), "offline \xC2\xB7 z%d", v.zoom);
  d->fillRoundRect(c.chip.x, c.chip.y, c.chip.w, c.chip.h, kRadiusSm, kPanel);
  d->drawRoundRect(c.chip.x, c.chip.y, c.chip.w, c.chip.h, kRadiusSm, kLine);
  drawTextLeft(d, FontRole::MonoMeta, kDim, kPanel, c.chip.x + kMapChipPadX,
               c.chip.y + c.chip.h / 2, chip);

  // Zoom and lock keys, bottom right, as in the mock.
  keyBox(d, c.zoomIn, kPanel, kLine);
  keyBox(d, c.zoomOut, kPanel, kLine);
  keyBox(d, c.lock, v.locked ? kSelBg2 : kPanel, v.locked ? kAcc : kLine);
  setRole(d, FontRole::SansTitle);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(kInk, kPanel);
  d->drawString("+", c.zoomIn.x + c.zoomIn.w / 2, c.zoomIn.y + c.zoomIn.h / 2);
  d->drawString("-", c.zoomOut.x + c.zoomOut.w / 2, c.zoomOut.y + c.zoomOut.h / 2);
  drawLockIcon(d, c.lock, v.locked);

  // The scale line, bottom left: a rail, three caps, the half and the full distance.
  d->drawFastHLine(c.scale.x, c.scale.bottom() - 7, c.scalePx, kInk);
  for (int i = 0; i < 3; ++i) {
    const int x = c.scale.x + (c.scalePx * i) / 2;
    d->drawFastVLine(x, c.scale.bottom() - 10, 4, kInk);
  }
  const int midW = measureText(d, FontRole::MonoMeta, c.scaleMid, (int)strlen(c.scaleMid));
  const int rightW = measureText(d, FontRole::MonoMeta, c.scaleRight, (int)strlen(c.scaleRight));
  // EST-352: one measured space between the two labels. If the rail cannot hold both, the half
  // label is dropped and the end label (right-aligned to the rail) stays readable.
  const int spaceW = measureText(d, FontRole::MonoMeta, " ", 1);
  const MapScaleLabelLayout sl =
      mapScaleLabelLayout(c.scale, midW, rightW, spaceW > 0 ? spaceW : 1);
  if (sl.showMid) {
    drawTextLeft(d, FontRole::MonoMeta, kInk, kMapBg, sl.midX, c.scale.bottom() - 1, c.scaleMid);
  }
  drawTextLeft(d, FontRole::MonoMeta, kInk, kMapBg, sl.rightX, c.scale.bottom() - 1,
               c.scaleRight);

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
  drawTabBar(d, l2(), Tab::Map, 0, false);
  om::useAppFont(d);
  omFrameEnd();
}

}  // namespace ui
}  // namespace om
