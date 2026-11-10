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
void mapPan(int dx, int dy);                      // move the view by screen pixels
void mapZoomBy(int delta);                        // clamp inside the pack's range
int mapZoom();

void mapScreenToLatLon(int x, int y, double& lat, double& lon);
void mapSetMarker(double lat, double lon);
void mapClearMarker();
bool mapHasMarker();

}  // namespace om
