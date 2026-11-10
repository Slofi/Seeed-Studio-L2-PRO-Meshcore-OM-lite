// SPDX-License-Identifier: MIT
// The status bar's GPS cell and the arithmetic that proves the row fits.
//
// Why: GGA's satellite count only counts the satellites USED in the fix, so a receiver that hears
// nothing and one that hears plenty but too weakly both report 0. GSV's "in view" separates them,
// so while searching the cell reads "GPS 0/5" (used/in view) instead of "GPS --". With a fix the
// count that matters is the used one: "GPS 7".
//
// The row is 320 px and the mono face advances 6 px per glyph, so the text is short on purpose and
// the widths below are measured, not guessed: widgets.cpp measures with the real font and asks
// statusRowFits() before it commits, and test/host repeats the measurement. When the full cell
// would push the node count into the left group, the short form ("0/5") is drawn instead.
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host compiles it with the system g++.
#pragma once

#include <stddef.h>
#include <stdio.h>

#include "ui/theme.h"

namespace om {
namespace ui {

// Every glyph of the 10 px mono blob advances 6 px (it is monospaced; tools/mkvlw.py records one
// xAdvance for all 96 glyphs). Used by the host test to repeat the status-row measurement.
constexpr int kMonoStatusAdvancePx = 6;

constexpr int kGpsCellMax = 12;   // "GPS 0/99" is 8 chars plus room

struct GpsCell {
  bool valid = false;    // a fix: show the satellites used
  bool talked = false;   // at least one complete NMEA sentence from the module
  int used = 0;          // GGA satellite count (used in the fix)
  int inView = 0;        // GSV satellites in view, all constellations
};

// Clamped to two digits: the cell may never outgrow the row.
inline int gpsCountClamp(int v) { return v < 0 ? 0 : (v > 99 ? 99 : v); }

// "GPS 7" with a fix, "GPS 0/5" while searching, "GPS --" when the module has said nothing at all.
inline void gpsCellText(char* out, size_t cap, const GpsCell& g) {
  if (!out || cap == 0) return;
  if (g.valid) snprintf(out, cap, "GPS %d", gpsCountClamp(g.used));
  else if (!g.talked) snprintf(out, cap, "GPS --");
  else snprintf(out, cap, "GPS %d/%d", gpsCountClamp(g.used), gpsCountClamp(g.inView));
}

// The same cell without the label, for a row that would otherwise collide with the left group.
inline void gpsCellTextShort(char* out, size_t cap, const GpsCell& g) {
  if (!out || cap == 0) return;
  if (g.valid) snprintf(out, cap, "%d", gpsCountClamp(g.used));
  else if (!g.talked) snprintf(out, cap, "--");
  else snprintf(out, cap, "%d/%d", gpsCountClamp(g.used), gpsCountClamp(g.inView));
}

// Widths of the right-hand cells in px, as measured by the caller with the mono face.
struct StatusRightWidths {
  int nodes = 0;
  int gps = 0;
  int pct = 0;
  int clock = 0;
};

// The whole right-hand group, from the outer edge of the node count to the screen edge: it includes
// the gear and its padding, exactly as drawStatusBar lays them out.
inline int statusRightWidth(const StatusRightWidths& w) {
  return w.nodes + kStatusGap + w.gps + kStatusGap + kBattW + kBattGap + w.pct + kStatusGap +
         w.clock + kStatusGap + kGearSize + kStatusPadX;
}

// leftEnd = x one past the left group's last pixel. True when the right group starts at least
// kStatusGap to the right of it.
inline bool statusRowFits(int screenW, int leftEnd, const StatusRightWidths& w) {
  return leftEnd + kStatusGap <= screenW - statusRightWidth(w);
}

}  // namespace ui
}  // namespace om
