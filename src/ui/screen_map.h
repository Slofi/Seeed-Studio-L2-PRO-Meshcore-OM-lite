// SPDX-License-Identifier: MIT
// Map screen (EST-351): the new layer's chrome over map.cpp's tiles. The tiles, the SD reads and the
// pan stay exactly where they were; this file owns the pixels and the rectangles of everything
// around them, and draws the whole view into the off-screen frame.
#pragma once

#include <LovyanGFX.hpp>

#include "ui/layout.h"
#include "ui/map_chrome.h"
#include "ui/widgets.h"

namespace om {
namespace ui {

struct MapView {
  StatusModel status;
  int zoom = 13;
  bool locked = false;       // follow our own position
  bool hasFix = false;
  MapMarker markers[kMapMaxMarkers];
  int markerCount = 0;
  bool callout = false;      // a marker is selected
  char calloutTitle[32] = "";
  char calloutLine[64] = "";
};

// The chrome for this view. Drawing and the tap test both come from this one call.
MapChrome mapScreenChrome(const MapView& v);

// One frame: tiles (mapRender), markers, chip, keys, scale, callout, status bar and tab bar.
void mapScreenDraw(const MapView& v);

}  // namespace ui
}  // namespace om
