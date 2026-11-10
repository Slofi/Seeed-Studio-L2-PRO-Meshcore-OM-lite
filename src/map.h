// SPDX-License-Identifier: MIT
// OM Lite map — the on-device reader for the tile pack built by tools/tiles_export.py --pack.
//
// Pack layout (little endian): tiles.idx is a sorted array of 24-byte records
//   <B3xIIQI>: z u8, 3 pad, x u32, y u32, offset u64, length u32
// sorted by (z, x, y); tiles.pack holds the JPEG bytes. The index is loaded once into PSRAM
// (1.8 MB for Slovenia); tiles are decoded into a small PSRAM cache so panning composites from
// memory instead of re-decoding a JPEG on every drag.
#pragma once

#include <Arduino.h>

namespace om {

bool mapBegin();                 // opens the index + pack (card must be mounted)
bool mapReady();
uint32_t mapIndexBytes();

void mapSetView(double lat, double lon, int z);   // recentre/zoom
void mapRender();                                 // draw the 320x200 map area
// EST-368: push the tile canvas that is already composited, with no decode, no SD read and no
// recomposite. The Map's animation frames use this so a moving trace dot costs a frame push, not a
// tile render. Does nothing until mapRender() has run once.
void mapRepush();
// EST-371: the Map's fast open. True once a canvas has been composited and can be repushed without
// decoding tiles; mapViewStale() is true when a pan/zoom/recentre since then means those tiles no
// longer show the current view (the shell then refreshes them after the cached frame is visible).
bool mapCanvasReady();
bool mapViewStale();
void mapPan(int dx, int dy);                      // move the view by screen pixels
void mapZoomBy(int delta);                        // clamp inside the pack's range
int mapZoom();

void mapScreenToLatLon(int x, int y, double& lat, double& lon);
// Project a geographic point into the 320x214 view the tiles use (EST-351). False when the pack
// is not open; a point outside the view is still projected, the caller filters it.
bool mapProject(double lat, double lon, int& x, int& y);
void mapSetMarker(double lat, double lon);
void mapClearMarker();
bool mapHasMarker();

}  // namespace om
