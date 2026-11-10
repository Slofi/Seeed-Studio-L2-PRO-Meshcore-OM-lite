// SPDX-License-Identifier: MIT
// Map chrome (EST-351): the scale bar's arithmetic, the rectangles of the zoom / lock keys, the
// info chip and the tapped-marker callout, and the hit test that uses those same rectangles.
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
constexpr int16_t kMapScaleH = 15;
constexpr int16_t kMapScaleMaxW = 92;
constexpr int16_t kMapMarkerR = 4;
constexpr int16_t kMapMarkerHitR = 11;

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
  Rect scale;      // the rail's box, bottom left
  Rect callout;    // the tapped marker's panel; empty when nothing is selected
  int16_t scalePx = 0;
  char scaleMid[12] = "";
  char scaleRight[12] = "";
};

// Web-Mercator metres per pixel at a latitude. One number, used by the scale bar and by tests.
double mapMetresPerPixel(int zoom, double lat);

// The largest 1-2-5 "nice" distance whose bar fits kMapScaleMaxW, and the bar's width. The labels
// are the half and the full distance, as the mock's 500 / 1km pair.
int32_t mapScaleDistance(int zoom, double lat, int16_t* pxOut);
void mapScaleLabels(int32_t metres, char* mid, size_t midCap, char* right, size_t rightCap);

// All rectangles from the screen size and the zoom/latitude. `selected` adds the callout; it sits
// over the bottom of the map like the mock's .callout.
MapChrome mapChromeFor(const Layout& L, int zoom, double lat, bool selected);

enum class MapHitKind : uint8_t { None = 0, ZoomIn, ZoomOut, Lock, Marker, Map, Callout };

struct MapHit {
  MapHitKind kind = MapHitKind::None;
  int marker = -1;   // valid when kind == Marker
};

// The key or marker under a point. A marker wins over the map; the callout's own box is not a map
// tap. `count` markers in `markers`.
MapHit mapHitAt(const MapChrome& c, const MapMarker* markers, int count, int x, int y);

}  // namespace ui
}  // namespace om
