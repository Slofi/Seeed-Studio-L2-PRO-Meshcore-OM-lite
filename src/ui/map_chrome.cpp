// SPDX-License-Identifier: MIT
// Map chrome arithmetic: the key rectangles, the attribution's box and the one hit test. No
// LovyanGFX. EST-368 removed the distance scale and its 1-2-5 ladder.
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

  const int16_t right = (int16_t)(w - kMapMargin);
  c.chip = Rect{(int16_t)(right - 92), (int16_t)(L.status.bottom() + 6), 92, kMapChipH};

  // The mock's stack, now four keys from the bottom right corner up: zoom out, zoom in, follow,
  // and the Traces toggle on top (EST-368).
  const int16_t bx = (int16_t)(right - kMapKey);
  const int16_t by = (int16_t)(h - kMapMargin - kMapKey);
  c.zoomOut = Rect{bx, by, kMapKey, kMapKey};
  c.zoomIn = Rect{bx, (int16_t)(by - kMapKey - kMapKeyGap), kMapKey, kMapKey};
  c.lock = Rect{bx, (int16_t)(c.zoomIn.y - kMapKey - kMapKeyGap), kMapKey, kMapKey};
  c.traces = Rect{bx, (int16_t)(c.lock.y - kMapKey - kMapKeyGap), kMapKey, kMapKey};

  if (selected) {
    c.callout = Rect{8, (int16_t)(h - 8 - kMapCalloutH), (int16_t)(w - 16), kMapCalloutH};
  }
  return c;
}

Rect mapCreditRect(const MapChrome& c, int16_t width) {
  Rect r;
  if (width <= 0) return r;   // nothing to draw, nothing to place
  // EST-368: on the bottom LEFT, where the removed scale used to be, clamped to the map area. The
  // text is drawn straight on the tiles with a 1 px dark outline, so no plate has to reserve room.
  const int16_t cy = (int16_t)(c.view.h - kMapMargin - kMapCreditH / 2);
  int16_t x = (int16_t)(c.view.x + kMapMargin);
  r = Rect{x, (int16_t)(cy - kMapCreditH / 2), width, kMapCreditH};
  // A very wide face (a narrower screen) must not run into the key column: the box ends at the
  // keys' left edge, the same rule that keeps every other piece of chrome off them.
  const int16_t limit = (int16_t)(c.zoomOut.x - kMapMargin);
  if (r.right() > limit) r.w = (int16_t)(limit - r.x);
  if (r.w < 0) r.w = 0;
  return r;
}

MapHit mapHitAt(const MapChrome& c, const MapMarker* markers, int count, int x, int y) {
  MapHit hit;
  if (c.status.contains(x, y)) return hit;   // the bar's own keys are handled before this
  // Markers first: a finger on a dot is a node, even if the dot is under a key.
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
  if (!c.callout.empty() && c.callout.contains(x, y)) { hit.kind = MapHitKind::Callout; return hit; }
  if (c.view.contains(x, y)) { hit.kind = MapHitKind::Map; return hit; }
  return hit;
}

}  // namespace ui
}  // namespace om
