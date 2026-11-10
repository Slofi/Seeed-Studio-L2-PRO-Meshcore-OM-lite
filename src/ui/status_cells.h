// SPDX-License-Identifier: MIT
// The status bar's GPS cell, the node count cell, and the arithmetic that proves the row fits.
//
// Why the GPS cell changed: GGA's satellite count only counts the satellites USED in the fix, and
// GSV's "in view" number is what the module's ALMANAC expects, not what it hears. Filip asked for
// ONE number, so the cell shows the satellites heard right now (GSV with a C/N0 value). Searching:
// "GPS 3" in dim. With a fix: the same heard number in the fix colour (kGreen), so a fix is visible
// without a second number. The module silent: "GPS --". The used count and the expected count stay
// in the heartbeat, not on the glass (EST-336).
//
// The row is 320 px and the mono face advances 6 px per glyph, so the text is short on purpose and
// every width below is measured, not guessed: widgets.cpp measures with the real font and asks
// chooseStatusRow() before it commits, and test/host repeats the measurement.
//
// Fitting order (the ticket's rule): the full row first; if it does not fit, drop the node count's
// word ("12 nodes" -> "12"); only then shorten the GPS cell ("GPS 13" -> "13"); as the last
// resort drop the node cell. Adjacent cells are always at least kStatusGap apart, including the gap
// between the left group and the right one, so the two groups can never run into each other.
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

constexpr int kGpsCellMax = 12;    // "GPS 99" is 6 chars plus room
constexpr int kNodeCellMax = 16;   // "255 nodes" is 9 chars plus room

struct GpsCell {
  bool valid = false;    // a fix: the heard number changes colour, nothing else
  bool talked = false;   // at least one complete NMEA sentence from the module
  int heard = 0;         // GSV satellites with a C/N0 value above zero, all constellations
};

// Clamped to two digits: the cell may never outgrow the row.
inline int gpsCountClamp(int v) { return v < 0 ? 0 : (v > 99 ? 99 : v); }

// "GPS 4" (the satellites heard right now) while searching and with a fix alike; "GPS --" when the
// module has never talked. The fix is the colour, not a second number (EST-336).
inline void gpsCellText(char* out, size_t cap, const GpsCell& g) {
  if (!out || cap == 0) return;
  if (!g.talked) snprintf(out, cap, "GPS --");
  else snprintf(out, cap, "GPS %d", gpsCountClamp(g.heard));
}

// The same cell without the label, for a row that would otherwise collide with the left group.
inline void gpsCellTextShort(char* out, size_t cap, const GpsCell& g) {
  if (!out || cap == 0) return;
  if (!g.talked) snprintf(out, cap, "--");
  else snprintf(out, cap, "%d", gpsCountClamp(g.heard));
}

// The node count cell: "2 nodes" normally, just "2" when the row is tight (the ticket's first thing
// to drop). A count of 1 uses "node". The number is clamped to three digits: a status cell, not a
// table, and the clamp keeps the text provably inside kNodeCellMax for the snprintf.
inline void nodeCellText(char* out, size_t cap, int nodes, bool withWord) {
  if (!out || cap == 0) return;
  const int n = nodes < 0 ? 0 : (nodes > 999 ? 999 : nodes);
  if (withWord) snprintf(out, cap, "%d %s", n, n == 1 ? "node" : "nodes");
  else snprintf(out, cap, "%d", n);
}

// Widths of the right-hand cells in px, as measured by the caller with the real mono face.
struct StatusRightWidths {
  int nodes = 0;        // "2 nodes"
  int nodesNumber = 0;  // "2"
  int gps = 0;          // "GPS 0/13"
  int gpsShort = 0;     // "0/13"
  int pct = 0;          // "97%"
  int clock = 0;        // "08:53"
};

// Which form of the right-hand group is drawn. The order is the fitting order above.
enum class StatusRowForm : uint8_t {
  Full = 0,             // "2 nodes" + "GPS 0/13"
  NodeNumber,           // "2" + "GPS 0/13"
  NodeNumberGpsShort,   // "2" + "0/13"
  NodesDropped,         // no node cell + "0/13" (last resort; nothing to shorten any more)
};

// The whole right-hand group, from its leftmost pixel to the screen edge: the gear and its padding
// included, and every adjacent pair separated by exactly kStatusGap, as drawStatusBar lays it out.
inline int statusRightWidth(const StatusRightWidths& w, StatusRowForm form) {
  int nodes = 0;
  switch (form) {
    case StatusRowForm::Full: nodes = w.nodes; break;
    case StatusRowForm::NodeNumber:
    case StatusRowForm::NodeNumberGpsShort: nodes = w.nodesNumber; break;
    case StatusRowForm::NodesDropped: nodes = 0; break;
  }
  const bool shortGps = (form == StatusRowForm::NodeNumberGpsShort ||
                         form == StatusRowForm::NodesDropped);
  const int gps = shortGps ? w.gpsShort : w.gps;
  int width = gps + kStatusGap + kBattW + kBattGap + w.pct + kStatusGap + w.clock + kStatusGap +
              kGearSize + kStatusPadX;
  if (form != StatusRowForm::NodesDropped) width += nodes + kStatusGap;
  return width;
}

// leftEnd = x one past the left group's last pixel. True when the right group starts at least
// kStatusGap to the right of it.
inline bool statusRowFits(int screenW, int leftEnd, const StatusRightWidths& w, StatusRowForm form) {
  return leftEnd + kStatusGap <= screenW - statusRightWidth(w, form);
}

// The widest form that fits, tried in the ticket's order. NodesDropped is the narrowest, so it is
// also the answer when even that is too wide (the caller then draws it; with the current left group
// that cannot happen, and test/host sweeps the realistic widths to say so).
inline StatusRowForm chooseStatusRow(int screenW, int leftEnd, const StatusRightWidths& w) {
  const StatusRowForm order[] = {StatusRowForm::Full, StatusRowForm::NodeNumber,
                                 StatusRowForm::NodeNumberGpsShort, StatusRowForm::NodesDropped};
  for (StatusRowForm form : order) {
    if (statusRowFits(screenW, leftEnd, w, form)) return form;
  }
  return StatusRowForm::NodesDropped;
}

}  // namespace ui
}  // namespace om
