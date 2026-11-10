// SPDX-License-Identifier: MIT
// Map chrome arithmetic: the edge-tab rectangles, the plain zoom label, the attribution's box, the
// centre point and the one hit test. No LovyanGFX. EST-368 removed the distance scale and its 1-2-5
// ladder; EST-370 added the Names toggle and the centre marker; EST-372 turned the right-hand keys
// into tabs flush with the screen edge, replaced the centre dot with a thin cross and dropped the
// "offline" chip for a plain zN label.
#include "ui/map_chrome.h"

#include <string.h>

namespace om {
namespace ui {

MapChrome mapChromeFor(const Layout& L, bool selected) {
  MapChrome c;
  const int16_t w = L.width;
  const int16_t h = (int16_t)(L.tabbar.y);   // the map runs above the tab bar
  c.status = L.status;
  c.view = Rect{0, 0, w, h};

  // EST-372: the zoom is plain text, right-aligned to the same edge as the tabs, just above the
  // top one. It is not a chip: no plate, no border.
  c.zoomLabel = Rect{(int16_t)(w - kMapZoomLabelW),
                     (int16_t)(L.status.bottom() + kMapZoomLabelGap), kMapZoomLabelW,
                     kMapZoomLabelH};
  const int16_t top = (int16_t)(c.zoomLabel.bottom() + kMapZoomLabelGap);

  // Five tabs, bottom up: zoom out, zoom in, follow, Traces, Names. The right edge is the screen
  // edge (drawing rounds the left corners only and leaves the right square). The height is what is
  // left after the label and the four 1 px gaps; every tab is the same height.
  const int16_t available = (int16_t)(h - top - 4 * kMapTabGap);
  int16_t tabH = (int16_t)(available / 5);
  if (tabH < 1) tabH = 1;
  const int16_t bx = (int16_t)(w - kMapTabW);
  c.zoomOut = Rect{bx, (int16_t)(h - tabH), kMapTabW, tabH};
  c.zoomIn = Rect{bx, (int16_t)(c.zoomOut.y - tabH - kMapTabGap), kMapTabW, tabH};
  c.lock = Rect{bx, (int16_t)(c.zoomIn.y - tabH - kMapTabGap), kMapTabW, tabH};
  c.traces = Rect{bx, (int16_t)(c.lock.y - tabH - kMapTabGap), kMapTabW, tabH};
  c.names = Rect{bx, (int16_t)(c.traces.y - tabH - kMapTabGap), kMapTabW, tabH};

  if (selected) {
    c.callout = Rect{8, (int16_t)(h - 8 - kMapCalloutH), (int16_t)(w - 16), kMapCalloutH};
  }
  return c;
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
  if (c.zoomIn.contains(x, y)) { hit.kind = MapHitKind::ZoomIn; return hit; }
  if (c.zoomOut.contains(x, y)) { hit.kind = MapHitKind::ZoomOut; return hit; }
  if (c.lock.contains(x, y)) { hit.kind = MapHitKind::Lock; return hit; }
  if (c.traces.contains(x, y)) { hit.kind = MapHitKind::Traces; return hit; }
  if (c.names.contains(x, y)) { hit.kind = MapHitKind::Names; return hit; }
  if (!c.callout.empty() && c.callout.contains(x, y)) { hit.kind = MapHitKind::Callout; return hit; }
  if (c.view.contains(x, y)) { hit.kind = MapHitKind::Map; return hit; }
  return hit;
}

}  // namespace ui
}  // namespace om
