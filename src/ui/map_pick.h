// SPDX-License-Identifier: MIT
// EST-366 / EST-384: ONE map picker, used by the RPTR Position page and by the node's own Fixed
// position. The geometry is ui/rptr_layout.h's rptrMapPickLayoutFor()/rptrMapPickHitAt(); this file
// owns only the pixels, so the two screens cannot drift into two pickers.
// EST-393: the picker's mark is the Map's own centre cross, drawn by ui/map_chrome.h's one helper.
#pragma once

#include <LovyanGFX.hpp>

#include "ui/widgets.h"

namespace om {
namespace ui {

struct MapPickView {
  char coords[40] = "";       // the candidate point, mono under the map
  char hint[40] = "";         // "z15"
  bool gps = false;           // "Use my GPS position" is live
  bool action = false;        // the Save/Apply key is live
  const char* actionLabel = "Apply";
  const char* backLabel = "< back";
};

// Draws the picker INSIDE an open omFrame (screen_rptr does its own frame). mapRender() is called
// here because both users show the same offline map.
void drawMapPick(lgfx::LovyanGFX* d, const MapPickView& v);

// A complete frame for a caller that is not the RPTR screen (Settings' own position picker).
void drawMapPickScreen(const MapPickView& v, const StatusModel& status);

}  // namespace ui
}  // namespace om
