// SPDX-License-Identifier: MIT
// Map chrome arithmetic: the edge-tab rectangles, the hide/show chip and its chevron, the plain zoom
// label, the attribution's box, the tucked bottom bar, the centre point and the one hit test. No
// LovyanGFX. EST-368 removed the distance scale and its 1-2-5 ladder; EST-370 added the Names toggle
// and the centre marker; EST-372 turned the right-hand keys into tabs flush with the screen edge,
// replaced the centre dot with a thin cross and dropped the "offline" chip for a plain zN label.
// EST-373 narrows the tabs to their widest icon plus 4 px each side, gives the cross one dark ink and
// adds the arrow that hides the column. EST-374 makes the arrow a 22 px chip, hides the bottom bar
// with the tabs (both down to a 3 px strip), runs the canvas to the screen floor in both states and
// leaves the zoom + / - behind as grey ghosts so zooming still works while hidden.
#include "ui/map_chrome.h"

namespace om {
namespace ui {

MapChrome mapChromeFor(const Layout& L, bool selected, bool tabsHidden) {
  MapChrome c;
  const int16_t w = L.width;
  // EST-374: the tile canvas now runs from the status bar's bottom row to the screen's floor. The
  // view IS that canvas: it is what map.cpp renders, what the cross is centred on, and what pans.
  c.status = L.status;
  c.view = Rect{0, kMapCanvasTop, w, kMapCanvasH};
  c.tabsHidden = tabsHidden;
  // Where the bottom bar starts while it is shown. The tab column still ends here in both states, so
  // the five tabs keep their vertical band and the hidden slivers sit at exactly their shown rows.
  const int16_t barTop = L.tabbar.y;

  // EST-374: the chip at the top right, flush with the tabs' right edge, starting on the status
  // bar's bottom row. Its label sits immediately to its left on the same centre line.
  c.chip = Rect{(int16_t)(w - kMapChipSize), L.status.bottom(), kMapChipSize, kMapChipSize};
  c.chipTap = Rect{(int16_t)(c.chip.right() - kMapChipTapW), c.chip.y, kMapChipTapW, kMapChipTapH};
  c.chevron = Rect{(int16_t)(c.chip.x + (kMapChipSize - kMapChevronW) / 2),
                   (int16_t)(c.chip.y + (kMapChipSize - kMapChevronH) / 2), kMapChevronW,
                   kMapChevronH};
  c.zoomLabel = Rect{(int16_t)(c.chip.x - kMapChipGap - kMapZoomLabelW),
                     (int16_t)(c.chip.y + (kMapChipSize - kMapZoomLabelH) / 2), kMapZoomLabelW,
                     kMapZoomLabelH};

  // Five tabs, bottom up: zoom out, zoom in, follow, Traces, Names. The right edge is the screen
  // edge (drawing rounds the left corners only and leaves the right square). The height is what is
  // left after the chip's target and the four 1 px gaps; every tab is the same height. EST-374:
  // hidden, each tab is only its 3 px sliver at the same edge, at the same height, so the rectangle
  // and the tap zone move together. The chip target's 26 px is what makes the tabs 31 px.
  const int16_t top = (int16_t)(c.chipTap.bottom() + kMapChipGap);
  const int16_t available = (int16_t)(barTop - top - 4 * kMapTabGap);
  int16_t tabH = (int16_t)(available / 5);
  if (tabH < 1) tabH = 1;
  const int16_t tabW = tabsHidden ? kMapTabSliverW : kMapTabW;
  const int16_t bx = (int16_t)(w - tabW);
  c.zoomOut = Rect{bx, (int16_t)(barTop - tabH), tabW, tabH};
  c.zoomIn = Rect{bx, (int16_t)(c.zoomOut.y - tabH - kMapTabGap), tabW, tabH};
  c.lock = Rect{bx, (int16_t)(c.zoomIn.y - tabH - kMapTabGap), tabW, tabH};
  c.traces = Rect{bx, (int16_t)(c.lock.y - tabH - kMapTabGap), tabW, tabH};
  c.names = Rect{bx, (int16_t)(c.traces.y - tabH - kMapTabGap), tabW, tabH};

  // EST-374: the SHOWN-state zoom tabs, always. While the bars are hidden these are the ghost
  // glyphs' boxes (and their tap zones), so a hidden zoom tap lands exactly where the shown tab's
  // glyph sits: same centre, same glyph, only the colours inverted.
  const int16_t shownX = (int16_t)(w - kMapTabW);
  c.zoomOutShown = Rect{shownX, c.zoomOut.y, kMapTabW, tabH};
  c.zoomInShown = Rect{shownX, c.zoomIn.y, kMapTabW, tabH};

  // EST-374: the tucked bottom bar's strip: the bar's own top rows, on the screen's floor.
  c.barStrip = Rect{0, (int16_t)(L.height - kMapBarStripH), w, kMapBarStripH};
  // The attribution's floor: above the shown bar, or above the strip while it is tucked.
  c.creditBottom = tabsHidden ? c.barStrip.y : barTop;

  if (selected) {
    c.callout = Rect{8, (int16_t)(barTop - 8 - kMapCalloutH), (int16_t)(w - 16), kMapCalloutH};
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
  // EST-374: its floor is the lowest clear row (the bar, or the tucked strip), so the licence line
  // stays on the glass in both states.
  const int16_t floor = c.creditBottom > 0 ? c.creditBottom : c.view.bottom();
  const int16_t cy = (int16_t)(floor - kMapMargin - kMapCreditH / 2);
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
  // EST-374: the chip is the hide/show key, in both states. It ends above the top tab's top edge,
  // so the two targets can never fight over a pixel (the 26 px target is why the tabs are 31 px).
  if (c.chipTap.contains(x, y)) { hit.kind = MapHitKind::TabsToggle; return hit; }
  // EST-373/374: the tabs and their tap zones are ONE list, built from the rectangles the screen
  // draws. Hidden, the only tab pixels are the 3 px slivers, and a tap on one brings both groups
  // back instead of firing a switch the finger cannot see.
  MapTab tabs[kMapTabCount];
  mapTabRects(c, tabs);
  for (int i = 0; i < kMapTabCount; ++i) {
    if (!tabs[i].rect.contains(x, y)) continue;
    hit.kind = c.tabsHidden ? MapHitKind::TabsToggle : tabs[i].kind;
    return hit;
  }
  // EST-374: hidden, the zoom tabs leave their + and - behind at the SHOWN rectangles, and those
  // are the tap zones while hidden. The sliver check above wins on its 3 px, so the visible remnant
  // is still the hide/show key; everything else in the shown rectangle zooms.
  if (c.tabsHidden) {
    if (c.zoomInShown.contains(x, y)) { hit.kind = MapHitKind::ZoomIn; return hit; }
    if (c.zoomOutShown.contains(x, y)) { hit.kind = MapHitKind::ZoomOut; return hit; }
  }
  if (!c.callout.empty() && c.callout.contains(x, y)) { hit.kind = MapHitKind::Callout; return hit; }
  if (c.view.contains(x, y)) { hit.kind = MapHitKind::Map; return hit; }
  return hit;
}

}  // namespace ui
}  // namespace om
