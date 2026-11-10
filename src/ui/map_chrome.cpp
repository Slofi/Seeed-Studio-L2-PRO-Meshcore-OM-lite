// SPDX-License-Identifier: MIT
// Map chrome arithmetic: the edge-tab rectangles, the plain zoom label and its hide/show arrow, the
// attribution's box, the centre point and the one hit test. No LovyanGFX. EST-368 removed the
// distance scale and its 1-2-5 ladder; EST-370 added the Names toggle and the centre marker; EST-372
// turned the right-hand keys into tabs flush with the screen edge, replaced the centre dot with a thin
// cross and dropped the "offline" chip for a plain zN label. EST-373 narrows the tabs to their widest
// icon plus 4 px each side, gives the cross one dark ink and adds the arrow that hides the column.
#include "ui/map_chrome.h"

namespace om {
namespace ui {

MapChrome mapChromeFor(const Layout& L, bool selected, bool tabsHidden) {
  MapChrome c;
  const int16_t w = L.width;
  const int16_t h = (int16_t)(L.tabbar.y);   // the map runs above the tab bar
  c.status = L.status;
  c.view = Rect{0, 0, w, h};
  c.tabsHidden = tabsHidden;

  // EST-373: the label line is also the hide/show key: one 28 x 22 px target at the top right, from
  // the status bar's bottom to just above the top tab. The label and the 5 x 7 arrow sit inside it,
  // right-aligned, so the finger has one big target instead of two small ones.
  c.zoomTap = Rect{(int16_t)(w - kMapZoomTapW), L.status.bottom(), kMapZoomTapW, kMapZoomTapH};
  c.zoomLabel = Rect{c.zoomTap.x, (int16_t)(L.status.bottom() + kMapZoomLabelGap), kMapZoomLabelW,
                     kMapZoomLabelH};
  c.zoomArrow = Rect{(int16_t)(c.zoomLabel.right() + kMapArrowGap),
                     (int16_t)(c.zoomLabel.y + (kMapZoomLabelH - kMapArrowH) / 2), kMapArrowW,
                     kMapArrowH};
  const int16_t top = (int16_t)(c.zoomTap.bottom() + kMapZoomLabelGap);

  // Five tabs, bottom up: zoom out, zoom in, follow, Traces, Names. The right edge is the screen
  // edge (drawing rounds the left corners only and leaves the right square). The height is what is
  // left after the label line and the four 1 px gaps; every tab is the same height. EST-373: hidden,
  // each tab is only its 4 px sliver at the same edge, at the same height, so the rectangle and the
  // tap zone move together.
  const int16_t available = (int16_t)(h - top - 4 * kMapTabGap);
  int16_t tabH = (int16_t)(available / 5);
  if (tabH < 1) tabH = 1;
  const int16_t tabW = tabsHidden ? kMapTabSliverW : kMapTabW;
  const int16_t bx = (int16_t)(w - tabW);
  c.zoomOut = Rect{bx, (int16_t)(h - tabH), tabW, tabH};
  c.zoomIn = Rect{bx, (int16_t)(c.zoomOut.y - tabH - kMapTabGap), tabW, tabH};
  c.lock = Rect{bx, (int16_t)(c.zoomIn.y - tabH - kMapTabGap), tabW, tabH};
  c.traces = Rect{bx, (int16_t)(c.lock.y - tabH - kMapTabGap), tabW, tabH};
  c.names = Rect{bx, (int16_t)(c.traces.y - tabH - kMapTabGap), tabW, tabH};

  if (selected) {
    c.callout = Rect{8, (int16_t)(h - 8 - kMapCalloutH), (int16_t)(w - 16), kMapCalloutH};
  }
  return c;
}

void mapTabRects(const MapChrome& c, MapTab out[kMapTabCount]) {
  if (out == nullptr) return;
  out[0] = MapTab{MapHitKind::ZoomOut, c.zoomOut};
  out[1] = MapTab{MapHitKind::ZoomIn, c.zoomIn};
  out[2] = MapTab{MapHitKind::Lock, c.lock};
  out[3] = MapTab{MapHitKind::Traces, c.traces};
  out[4] = MapTab{MapHitKind::Names, c.names};
}

Rect mapCreditRect(const MapChrome& c, int16_t width) {
  Rect r;
  if (width <= 0) return r;   // nothing to draw, nothing to place
  // EST-370: plain dim text, no plate and no outline. The box is clamped to the map area and to
  // the tab column's left edge, the same rule that keeps every other piece of chrome off the tabs.
  const int16_t cy = (int16_t)(c.view.h - kMapMargin - kMapCreditH / 2);
  int16_t x = (int16_t)(c.view.x + kMapMargin);
  r = Rect{x, (int16_t)(cy - kMapCreditH / 2), width, kMapCreditH};
  // A very wide face (a narrower screen) must not run into the tab column: the box ends at the
  // tabs' left edge, the same rule that keeps every other piece of chrome off them.
  const int16_t limit = (int16_t)(c.zoomOut.x - kMapMargin);
  if (r.right() > limit) r.w = (int16_t)(limit - r.x);
  if (r.w < 0) r.w = 0;
  return r;
}

MapHit mapHitAt(const MapChrome& c, const MapMarker* markers, int count, int x, int y) {
  MapHit hit;
  if (c.status.contains(x, y)) return hit;   // the bar's own keys are handled before this
  // Markers first: a finger on a dot is a node, even if the dot is under a tab.
  int best = -1;
  int bestD2 = kMapMarkerHitR * kMapMarkerHitR;
  for (int i = 0; i < count; ++i) {
    if (!markers) break;
    const int dx = (int)markers[i].x - x;
    const int dy = (int)markers[i].y - y;
    const int d2 = dx * dx + dy * dy;
    if (d2 <= bestD2) {
      bestD2 = d2;
      best = i;
    }
  }
  if (best >= 0) {
    hit.kind = MapHitKind::Marker;
    hit.marker = best;
    return hit;
  }
  // EST-373: the label line above the column is the hide/show key. It ends above the top tab's
  // top edge, so the two targets can never fight over a pixel (the 22 px target is why).
  if (c.zoomTap.contains(x, y)) { hit.kind = MapHitKind::TabsToggle; return hit; }
  // EST-373: the tabs and their tap zones are ONE list, built from the rectangles the screen draws.
  MapTab tabs[kMapTabCount];
  mapTabRects(c, tabs);
  for (int i = 0; i < kMapTabCount; ++i) {
    if (!tabs[i].rect.contains(x, y)) continue;
    hit.kind = tabs[i].kind;
    // While the column is hidden only a 4 px sliver is visible, and its tap brings the tabs back
    // instead of firing a switch the finger cannot see.
    if (c.tabsHidden) hit.kind = MapHitKind::TabsToggle;
    return hit;
  }
  if (!c.callout.empty() && c.callout.contains(x, y)) { hit.kind = MapHitKind::Callout; return hit; }
  if (c.view.contains(x, y)) { hit.kind = MapHitKind::Map; return hit; }
  return hit;
}

}  // namespace ui
}  // namespace om
