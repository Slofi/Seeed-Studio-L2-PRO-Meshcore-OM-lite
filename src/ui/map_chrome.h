// SPDX-License-Identifier: MIT
// Map chrome (EST-351, reworked EST-368 and EST-370): the rectangles of the zoom / locate / traces /
// names keys, the info chip, the tile attribution, the centre dot and the tapped-marker callout,
// and the hit test that uses those same rectangles. The distance scale is gone (Filip, EST-368:
// "remove the distance markers, they are quite useless").
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
constexpr int16_t kMapChipH = 18;
constexpr int16_t kMapChipPadX = 6;
constexpr int16_t kMapKey = 24;          // a finger-sized key, the mock's 19 px plus the pad
constexpr int16_t kMapKeyGap = 4;
constexpr int16_t kMapMargin = 6;
constexpr int16_t kMapCalloutH = 40;
constexpr int16_t kMapMarkerR = 4;
constexpr int16_t kMapMarkerHitR = 11;
// EST-370: the centre dot. A 3 px core (radius 1) with a 1 px dark ring drawn around it, so it
// reads on a bright topo tile and on a dark one. It is a dot, never a crosshair: the Position
// picker keeps its own crosshair and does not draw this.
constexpr int16_t kMapCentreDotR = 1;

// EST-370: the tile canvas map.cpp renders into, and the point at its centre. mapRender() puts the
// view's geographic centre at this pixel, mapProject() and mapScreenToLatLon() use the same point,
// and the chrome draws its dot there. One source for the zoom focus, the dot and the picker.
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
  Rect status;     // the status bar sits over the map and is never a map tap
  Rect view;       // 0,0,320,214: the tiles and the touch area that pans
  Rect chip;       // "offline · z13", top right inside the content
  Rect zoomIn;
  Rect zoomOut;
  Rect lock;
  Rect traces;     // EST-368: the Traces toggle, above the locate key
  Rect names;      // EST-370: the Names toggle, the top key of the right column
  Rect callout;    // the tapped marker's panel; empty when nothing is selected
};

// All rectangles from the screen size. `selected` adds the callout; it sits over the bottom of the
// map like the mock's .callout. The keys stack upward from the bottom right: zoom out, zoom in,
// follow, Traces, Names.
MapChrome mapChromeFor(const Layout& L, bool selected);

// EST-370: the map's centre point, derived from the same view rectangle every other piece of chrome
// uses. For the L2 it is (160, 107), exactly map.cpp's projected view centre.
constexpr MapPoint mapCentreOf(const MapChrome& c) {
  return MapPoint{(int16_t)(c.view.x + c.view.w / 2), (int16_t)(c.view.y + c.view.h / 2)};
}

// The dot's 3x3 core rectangle (the screen adds the 1 px ring around it).
constexpr Rect mapCentreDotRect(const MapChrome& c) {
  const MapPoint p = mapCentreOf(c);
  return Rect{(int16_t)(p.x - kMapCentreDotR), (int16_t)(p.y - kMapCentreDotR),
              (int16_t)(2 * kMapCentreDotR + 1), (int16_t)(2 * kMapCentreDotR + 1)};
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

// The key or marker under a point. A marker wins over the map; the callout's own box is not a map
// tap; every toggle is a key like the zoom ones. `count` markers in `markers`.
MapHit mapHitAt(const MapChrome& c, const MapMarker* markers, int count, int x, int y);

}  // namespace ui
}  // namespace om
