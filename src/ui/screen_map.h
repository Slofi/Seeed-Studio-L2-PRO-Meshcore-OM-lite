// SPDX-License-Identifier: MIT
// Map screen (EST-351): the new layer's chrome over map.cpp's tiles. The tiles, the SD reads and the
// pan stay exactly where they were; this file owns the pixels and the rectangles of everything
// around them, and draws the whole view into the off-screen frame.
//
// EST-368: the distance scale is gone and the Map carries OM's Traces: a dashed route, a dot with a
// short tail moving hop by hop, and a ring for a direct arrival (ui/map_trace.h owns the model).
// EST-370: the attribution is plain dim text (no outline, no plate), a Names tab hides every label.
// EST-372: the traces are bolder fuchsia-violet at 2 px, the centre mark is a thin 9 px cross, the
// right-hand keys are edge tabs, and the old "offline · zN" chip is plain zN text.
#pragma once

#include <LovyanGFX.hpp>

#include "ui/layout.h"
#include "ui/map_chrome.h"
#include "ui/map_trace.h"
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
  // EST-365: the tab bar's badge, from the shell's one currentTabBar() call.
  TabBarModel tabs;
  // EST-368: the Traces toggle (highlighted when on) and the one live animation frame, sampled by
  // the shell at the map's own frame rate. `trace.live` false draws nothing.
  bool tracesOn = false;
  // EST-370: the Names toggle. Off hides every marker label and leaves the markers themselves.
  bool namesOn = false;
  TraceFrame trace;
};

// The chrome for this view. Drawing and the tap test both come from this one call.
MapChrome mapScreenChrome(const MapView& v);

// One full frame: tiles (mapRender), attribution, markers, labels, traces, chip, keys, callout, the
// status bar and the tab bar.
void mapScreenDraw(const MapView& v);

// EST-368: one ANIMATION frame. The tiles are the ones map.cpp already composited, pushed again
// (no decode, no SD read), and only the chrome and the trace are drawn. Used while a trace runs, at
// the map's refresh rate and never when idle.
void mapScreenAnimate(const MapView& v);

}  // namespace ui
}  // namespace om
