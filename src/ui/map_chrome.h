// SPDX-License-Identifier: MIT
// Map chrome (EST-351, reworked EST-368, EST-370 and EST-372): the rectangles of the zoom / locate /
// traces / names tabs, the plain zoom label, the tile attribution, the centre cross and the
// tapped-marker callout, and the hit test that uses those same rectangles. The distance scale is gone
// (Filip, EST-368: "remove the distance markers, they are quite useless").
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
// screen width (no margin, no right border; the drawing rounds the left corners only) and are as
// tall as the space allows. Five tabs at 40 px need 200 px, but the map band is only 189 px
// (status bar 26, tab bar 25); with the z13 line above the column the fit is 34 px each, 1 px
// apart, still 10 px taller and 20 px wider than the old 24x24 key. The height is computed from
// the layout, never baked in.
constexpr int16_t kMapTabW = 44;
constexpr int16_t kMapTabGap = 1;
constexpr int16_t kMapZoomLabelH = 11;   // "z13" in mono 10 px
constexpr int16_t kMapZoomLabelW = 24;   // three 6 px glyphs plus a little air
constexpr int16_t kMapZoomLabelGap = 2;  // status bar -> label, label -> first tab

// EST-372: the centre cross. Two 1 px lines, 9 px long each (arm 4) in kInk, with a 1 px kMapBg
// outline under them so the white lines read on a bright topo tile as well as on a dark one. It is
// a cross, never a crosshair: the Position picker keeps its own crosshair and does not draw this.
constexpr int16_t kMapCentreCrossArm = 4;   // 2 * 4 + 1 = 9 px

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
  Rect zoomIn;
  Rect zoomOut;
  Rect lock;
  Rect traces;      // EST-368: the Traces toggle, above the locate tab
  Rect names;       // EST-370: the Names toggle, the top tab of the right column
  Rect callout;     // the tapped marker's panel; empty when nothing is selected
};

// All rectangles from the screen size. `selected` adds the callout; it sits over the bottom of the
// map like the mock's .callout. The tabs stack upward from the bottom right: zoom out, zoom in,
// follow, Traces, Names; every tab's right edge IS the screen edge.
MapChrome mapChromeFor(const Layout& L, bool selected);

// EST-370: the map's centre point, derived from the same view rectangle every other piece of chrome
// uses. For the L2 it is (160, 107), exactly map.cpp's projected view centre.
constexpr MapPoint mapCentreOf(const MapChrome& c) {
  return MapPoint{(int16_t)(c.view.x + c.view.w / 2), (int16_t)(c.view.y + c.view.h / 2)};
}

// The cross's 9x9 box (the screen draws the two 1 px lines and their 1 px outline inside it).
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
  Traces,    // EST-368: the Traces toggle
  Names,     // EST-370: the Names toggle
  Marker,
  Map,
  Callout
};

struct MapHit {
  MapHitKind kind = MapHitKind::None;
  int marker = -1;   // valid when kind == Marker
};

// The tab or marker under a point. A marker wins over the map; the callout's own box is not a map
// tap; every toggle is a tab like the zoom ones. `count` markers in `markers`.
MapHit mapHitAt(const MapChrome& c, const MapMarker* markers, int count, int x, int y);

}  // namespace ui
}  // namespace om
