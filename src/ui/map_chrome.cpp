// SPDX-License-Identifier: MIT
// Map chrome arithmetic: the scale, the key rectangles and the one hit test. No LovyanGFX.
#include "ui/map_chrome.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string.h>

namespace om {
namespace ui {

double mapMetresPerPixel(int zoom, double lat) {
  // Web-Mercator at the equator, shrunk by cos(latitude). Clamp the latitude to the usable band:
  // beyond 85 degrees the projection explodes and the scale is meaningless.
  if (zoom < 0) zoom = 0;
  if (zoom > 22) zoom = 22;
  if (lat > 85.0) lat = 85.0;
  if (lat < -85.0) lat = -85.0;
  return 156543.03392 * cos(lat * M_PI / 180.0) / (double)(1UL << zoom);
}

int32_t mapScaleDistance(int zoom, double lat, int16_t* pxOut) {
  const double mpp = mapMetresPerPixel(zoom, lat);
  // The 1-2-5 ladder. Take the largest distance that still fits the mock's short bar; never go
  // below 10 m (a bar narrower than a few px stops reading as a scale).
  static const int32_t kNice[] = {10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000,
                                  20000, 50000, 100000, 200000, 500000, 1000000};
  int32_t best = kNice[0];
  if (pxOut) *pxOut = 0;
  for (size_t i = 0; i < sizeof(kNice) / sizeof(kNice[0]); ++i) {
    const double px = (double)kNice[i] / mpp;
    if (px <= (double)kMapScaleMaxW) {
      best = kNice[i];
      if (pxOut) *pxOut = (int16_t)lround(px);
    }
  }
  return best;
}

void mapScaleLabels(int32_t metres, char* mid, size_t midCap, char* right, size_t rightCap) {
  if (mid && midCap) mid[0] = 0;
  if (right && rightCap) right[0] = 0;
  auto fmt = [](char* out, size_t cap, int32_t m) {
    if (!out || cap == 0) return;
    char tmp[24];
    if (m >= 1000) {
      if (m % 1000 == 0) snprintf(tmp, sizeof(tmp), "%ldkm", (long)(m / 1000));
      else snprintf(tmp, sizeof(tmp), "%.1fkm", (double)m / 1000.0);
    } else {
      snprintf(tmp, sizeof(tmp), "%ldm", (long)m);
    }
    size_t n = strlen(tmp);
    if (n >= cap) n = cap - 1;
    memcpy(out, tmp, n);
    out[n] = 0;
  };
  fmt(mid, midCap, metres / 2);
  fmt(right, rightCap, metres);
}

MapScaleLabelLayout mapScaleLabelLayout(const Rect& bar, int midW, int rightW, int gap) {
  MapScaleLabelLayout out;
  if (midW < 0) midW = 0;
  if (rightW < 0) rightW = 0;
  if (gap < 0) gap = 0;
  // The end label is the one that must always be readable: it is right-aligned to the bar's right
  // end and clamped to the bar's left when it is wider than the rail.
  int16_t rightX = (int16_t)(bar.right() - rightW);
  if (rightX < bar.x) rightX = bar.x;
  out.rightX = rightX;
  out.midX = bar.x;
  // One space of air between the two labels, measured by the caller with the same font. If the half
  // label and that space cannot fit before the end label, the half label is dropped.
  out.showMid = midW > 0 && (int)bar.x + midW + gap <= (int)rightX;
  return out;
}

MapChrome mapChromeFor(const Layout& L, int zoom, double lat, bool selected) {
  MapChrome c;
  const int16_t w = L.width;
  const int16_t h = (int16_t)(L.tabbar.y);   // the map runs above the tab bar
  c.status = L.status;
  c.view = Rect{0, 0, w, h};

  const int16_t right = (int16_t)(w - kMapMargin);
  c.chip = Rect{(int16_t)(right - 92), (int16_t)(L.status.bottom() + 6), 92, kMapChipH};

  // The mock's stack: lock, +, - from the bottom right corner up.
  const int16_t bx = (int16_t)(right - kMapKey);
  const int16_t by = (int16_t)(h - kMapMargin - kMapKey);
  c.zoomOut = Rect{bx, by, kMapKey, kMapKey};
  c.zoomIn = Rect{bx, (int16_t)(by - kMapKey - kMapKeyGap), kMapKey, kMapKey};
  c.lock = Rect{bx, (int16_t)(c.zoomIn.y - kMapKey - kMapKeyGap), kMapKey, kMapKey};

  const int32_t metres = mapScaleDistance(zoom, lat, &c.scalePx);
  c.scale = Rect{kMapMargin, (int16_t)(h - kMapMargin - kMapScaleH), c.scalePx,
                 kMapScaleH};
  mapScaleLabels(metres, c.scaleMid, sizeof(c.scaleMid), c.scaleRight, sizeof(c.scaleRight));

  if (selected) {
    c.callout = Rect{8, (int16_t)(h - 8 - kMapCalloutH), (int16_t)(w - 16), kMapCalloutH};
  }
  return c;
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
  if (!c.callout.empty() && c.callout.contains(x, y)) { hit.kind = MapHitKind::Callout; return hit; }
  if (c.view.contains(x, y)) { hit.kind = MapHitKind::Map; return hit; }
  return hit;
}

}  // namespace ui
}  // namespace om
