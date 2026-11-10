// SPDX-License-Identifier: MIT
// Map chrome (EST-351, reworked EST-368, EST-370, EST-372 and EST-373): the rectangles of the zoom /
// locate / traces / names tabs, the plain zoom label with its hide/show arrow, the tile attribution,
// the centre cross and the tapped-marker callout, and the hit test that uses those same rectangles.
// The distance scale is gone (Filip, EST-368: "remove the distance markers, they are quite useless").
// Plain C++ (no LovyanGFX, no Arduino) so test/host can prove it with the system g++.
//
// The tiles are still map.cpp's job. This header only knows the 320x214 map area above the tab bar.
#pragma once

#include <stdint.h>
#include <stdio.h>

#include "layout.h"
#include "theme.h"

namespace om {
namespace ui {

constexpr int kMapMaxMarkers = 32;
constexpr int16_t kMapMargin = 6;
constexpr int16_t kMapCalloutH = 40;
constexpr int16_t kMapMarkerR = 4;
constexpr int16_t kMapMarkerHitR = 11;

// EST-372 (Filip ~00:00): the right-hand keys are tabs out of the screen edge. They run to x == the
// screen width (no margin, no right border; the drawing rounds the left corners only).
// EST-373 (Filip 2026-11-11 ~00:38: "the point of making them tabs is so they could be shortened"):
// the width is no longer a guess. It is the WIDEST thing drawn on a tab plus 4 px on each side. The
// widest is the Follow lock: a 5 px radius circle with a 5 px arm on each side, 17 px across (the
// Traces and Names masks are 13 x 13, the + and - glyphs measure 12 and 5 px of ink in SansTitle).
// 17 + 4 + 4 = 25 px, so the tabs start at x 295 and the map gains the 19 px the old 44 px column
// took. The height is still computed from the layout and never baked in; it drops 34 -> 32 px only
// because the arrow's 22 px tap target above the top tab has to fit (see kMapZoomTapH).
constexpr int16_t kMapTabIconPad = 4;   // 4 px each side of the widest icon (the ticket's number)
constexpr int16_t kMapTabGap = 1;

// The Follow icon (screen_map.cpp draws it with primitives), the widest thing on the column: the
// circle's radius, the length of the four arms and where an arm starts, in px from the centre.
constexpr int16_t kMapLockR = 5;
constexpr int16_t kMapLockArmLen = 5;
constexpr int16_t kMapLockArmOut = 8;                    // arm's outer end, px from the centre
constexpr int16_t kMapLockW = 2 * kMapLockArmOut + 1;    // 17 px across
constexpr int16_t kMapTabIconW = kMapLockW;              // the widest icon drawn on the column
constexpr int16_t kMapTabW = kMapTabIconW + 2 * kMapTabIconPad;   // 25

// EST-373: hidden, each tab keeps this much of itself at the right edge, and nothing else.
constexpr int16_t kMapTabSliverW = 4;

constexpr int16_t kMapZoomLabelH = 11;   // "z13" in mono 10 px
constexpr int16_t kMapZoomLabelW = 21;   // three 6 px glyphs plus 3 px of air
constexpr int16_t kMapZoomLabelGap = 2;  // status bar -> label, label -> arrow, label line -> tabs

// EST-373: the hide/show arrow. A small filled triangle next to the z13 label (same line), in the
// label's ink. Its tap target is 28 x 22 px even though the glyph is 5 x 7: it is the whole label
// line above the top tab, so the finger does not have to find 35 pixels. The 22 px is also what
// fixes the label band's height: five 34 px tabs left 15 px here, so the tabs give 2 px each (34 ->
// 32) and the target sits entirely above the top tab, never on it.
constexpr int16_t kMapArrowW = 5;
constexpr int16_t kMapArrowH = 7;
constexpr int16_t kMapArrowGap = 2;      // label -> arrow
constexpr int16_t kMapZoomTapW = 28;     // the arrow's tap target, at least 28 x 22
constexpr int16_t kMapZoomTapH = 22;

// EST-373: the centre cross. TWO 1 px lines, 7 px long each (arm 3), in ONE dark ink (kMapCrossInk)
// and no outline under them: on a light topo tile the dark line reads on its own, and the old white
// kInk without its 1 px kMapBg plate would be invisible. It is still a cross, never a crosshair: the
// Position picker keeps its own crosshair and does not draw this.
constexpr int16_t kMapCentreCrossArm = 3;   // 2 * 3 + 1 = 7 px

// EST-370: the tile canvas map.cpp renders into, and the point at its centre. mapRender() puts the
// view's geographic centre at this pixel, mapProject() and mapScreenToLatLon() use the same point,
// and the chrome draws its cross there. One source for the zoom focus, the cross and the picker.
constexpr int16_t kMapCanvasW = 320;
constexpr int16_t kMapCanvasH = 214;
constexpr int16_t kMapCentreX = kMapCanvasW / 2;   // 160
constexpr int16_t kMapCentreY = kMapCanvasH / 2;   // 107

struct MapPoint {
  int16_t x = 0;
  int16_t y = 0;
};

// One projected contact marker. `type` is MeshCore's ADV_TYPE_* (1 client, 2 repeater, 3 room).
struct MapMarker {
  int16_t x = 0;
  int16_t y = 0;
  uint8_t type = 0;
  const char* name = "";
  bool selected = false;
  bool self = false;   // our own position: the white dot with the gold halo
};

struct MapChrome {
  Rect status;      // the status bar sits over the map and is never a map tap
  Rect view;        // 0,0,320,214: the tiles and the touch area that pans
  Rect zoomLabel;   // EST-372: plain "z13", right-aligned above the tab column (not a chip)
  Rect zoomArrow;   // EST-373: the 5x7 filled triangle next to it (">" out, "<" hidden)
  Rect zoomTap;     // EST-373: the arrow's 28x22 target, the label line above the top tab
  Rect zoomIn;
  Rect zoomOut;
  Rect lock;
  Rect traces;      // EST-368: the Traces toggle, above the locate tab
  Rect names;       // EST-370: the Names toggle, the top tab of the right column
  Rect callout;     // the tapped marker's panel; empty when nothing is selected
  bool tabsHidden = false;   // EST-373: the saved hide/show state the rects above were built for
};

// All rectangles from the screen size. `selected` adds the callout; it sits over the bottom of the
// map like the mock's .callout. The tabs stack upward from the bottom right: zoom out, zoom in,
// follow, Traces, Names; every tab's right edge IS the screen edge. `tabsHidden` gives each tab its
// 4 px sliver at that same edge: the tab rectangle and the tap zone move together, never one alone.
MapChrome mapChromeFor(const Layout& L, bool selected, bool tabsHidden = false);

// EST-370: the map's centre point, derived from the same view rectangle every other piece of chrome
// uses. For the L2 it is (160, 107), exactly map.cpp's projected view centre.
constexpr MapPoint mapCentreOf(const MapChrome& c) {
  return MapPoint{(int16_t)(c.view.x + c.view.w / 2), (int16_t)(c.view.y + c.view.h / 2)};
}

// The cross's 7x7 box (the screen draws the two 1 px dark lines inside it, nothing else).
constexpr Rect mapCentreCrossRect(const MapChrome& c) {
  const MapPoint p = mapCentreOf(c);
  return Rect{(int16_t)(p.x - kMapCentreCrossArm), (int16_t)(p.y - kMapCentreCrossArm),
              (int16_t)(2 * kMapCentreCrossArm + 1), (int16_t)(2 * kMapCentreCrossArm + 1)};
}

// EST-368 (Filip ~21:35), EST-370 (~23:40: "no outline on the credits!"): the tile attribution is
// plain small dim text drawn straight on the map, no plate and no outline, on the bottom LEFT where
// the removed scale used to sit. `width` is the font's, measured by the caller; the screen draws it
// inside the returned box.
constexpr int16_t kMapCreditH = 10;
constexpr const char* kMapCreditText = "Esri, HERE, Garmin, OSM";
Rect mapCreditRect(const MapChrome& c, int16_t width);

enum class MapHitKind : uint8_t {
  None = 0,
  ZoomIn,
  ZoomOut,
  Lock,
  Traces,      // EST-368: the Traces toggle
  Names,       // EST-370: the Names toggle
  TabsToggle,  // EST-373: the hide/show arrow, and a hidden tab's 4 px sliver (brings them back)
  Marker,
  Map,
  Callout
};

// One tab: its rectangle and the hit kind that rectangle answers. The screen draws THIS rect and the
// hit test reads THIS rect, so the two can never drift apart (test_est373 proves it).
struct MapTab {
  MapHitKind kind = MapHitKind::None;
  Rect rect;
};

constexpr int kMapTabCount = 5;

// The five tabs, bottom up: zoom out, zoom in, follow, Traces, Names. Every one is out of the same
// MapChrome the screen draws, including the 4 px slivers while the tabs are hidden.
void mapTabRects(const MapChrome& c, MapTab out[kMapTabCount]);

struct MapHit {
  MapHitKind kind = MapHitKind::None;
  int marker = -1;   // valid when kind == Marker
};

// The tab or marker under a point. A marker wins over the map; the callout's own box is not a map
// tap; the label line above the column is the hide/show key; every toggle is a tab like the zoom
// ones. `count` markers in `markers`. While the tabs are hidden their slivers are the only tab
// pixels, and a tap on one brings the whole column back.
MapHit mapHitAt(const MapChrome& c, const MapMarker* markers, int count, int x, int y);

}  // namespace ui
}  // namespace om
