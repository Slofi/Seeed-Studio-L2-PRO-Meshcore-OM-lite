// SPDX-License-Identifier: MIT
// The top bar's cells and the arithmetic that proves the row fits (EST-337).
//
// The bar is now the phone convention Filip asked for:
//   left:  HH:MM (mono, ink), "--:--" until a real source has set the clock; then, in their place,
//          the RADIO FAULT mark: a small red mark that appears only when the device knows something
//          is wrong with ITSELF (see radio_fault.h). A tap on it opens a one-line toast.
//   right: from the right edge the gear, the battery icon + percent, the GPS cell (one number,
//          satellites heard, dim while searching, fix colour with a fix, "GPS --" when the module is
//          silent), then the status icons that appear only when they are on: do-not-disturb, Wi-Fi
//          and Bluetooth. The Wi-Fi/Bluetooth slots are reserved here (their widths are theme.h
//          constants) and are drawn only when those settings exist in a later ticket.
//
// The green dot, "MC", "EU868", "2B" and the node count are gone. The node count now lives in the
// Nodes screen's header (ui/nodes_header.h); region/frequency/path hash go to Settings > Radio.
//
// There is no shortening order any more: nothing on the row has a short form, so chooseStatusRow()
// and the node cell are removed rather than left as dead branches. The measured-fit check stays:
// widgets.cpp measures the real text and asks statusBarRects() for the rectangles it then draws, and
// test/host repeats the measurement for the worst case (all icons on, 100 %, "GPS 14").
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host compiles it with the system g++.
#pragma once

#include <stddef.h>
#include <stdio.h>

#include "ui/layout.h"
#include "ui/theme.h"

namespace om {
namespace ui {

// Every glyph of the 10 px mono blob advances 6 px (it is monospaced; tools/mkvlw.py records one
// xAdvance for all 96 glyphs). Used by the host test to repeat the status-row measurement.
constexpr int kMonoStatusAdvancePx = 6;

constexpr int kGpsCellMax = 12;    // "GPS 99" is 6 chars plus room
constexpr int kClockCellMax = 8;   // "HH:MM" is 5 chars plus room

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

// Measured widths of the three text cells, in px. Icons have fixed widths from theme.h.
struct StatusCellWidths {
  int clock = kStatusClockW;  // "08:53" or "--:--" (always five mono glyphs)
  int pct = 0;                // "97%" or "--"
  int gps = 0;                // "GPS 14" or "GPS --"
};

// Which optional cells are on. Bluetooth and Wi-Fi are reserved slots: drawn only when true, and no
// firmware path sets them yet (later tickets).
struct StatusCellFlags {
  bool fault = false;
  bool dnd = false;
  bool bt = false;
  bool wifi = false;
};

// Every rectangle the bar draws into, in screen coordinates. An optional cell that is off is empty
// (w == 0), so drawing can ask the same struct the tap test uses and nothing is guessed.
struct StatusBarRects {
  Rect time;
  Rect fault;
  Rect bt;
  Rect wifi;
  Rect dnd;
  Rect gps;
  Rect battery;     // the icon and the percent as one cell (they keep the mock's 8 px inner gap)
  Rect battIcon;
  Rect battText;
  Rect gear;
  int leftEnd = 0;    // one past the left group's last pixel (time, or the fault mark)
  int rightStart = 0; // the right group's leftmost pixel (Bluetooth when it is on, else GPS)
  bool fits = false;  // left group + one gap <= right group
};

// Lay the row out from both edges. The right group is placed from the gear leftwards, every adjacent
// pair exactly kStatusGap apart; the fault mark follows the clock with one kStatusGap. The battery's
// icon and percent keep kBattGap between them ("as now") and count as ONE cell to the row.
inline StatusBarRects statusBarRects(const Layout& L, const StatusCellWidths& w,
                                     const StatusCellFlags& f) {
  StatusBarRects r;
  const Rect& s = L.status;
  const int cy = s.y + s.h / 2;

  // --- right, from the screen edge -------------------------------------------------------------
  r.gear = L.gear();
  int right = r.gear.x - kStatusGap;

  r.battText = Rect{(int16_t)(right - w.pct), s.y, (int16_t)w.pct, s.h};
  right -= w.pct + kBattGap;
  r.battIcon = Rect{(int16_t)(right - kBattW), (int16_t)(cy - kBattH / 2), kBattW, kBattH};
  r.battery = Rect{r.battIcon.x, s.y, (int16_t)(r.battText.right() - r.battIcon.x), s.h};
  right = r.battIcon.x - kStatusGap;

  r.gps = Rect{(int16_t)(right - w.gps), s.y, (int16_t)w.gps, s.h};
  right = r.gps.x - kStatusGap;

  // The optional icons, from the GPS cell leftwards. DND sits closest to the GPS number; Wi-Fi and
  // Bluetooth keep the slots the next tickets draw into.
  if (f.dnd) {
    r.dnd = Rect{(int16_t)(right - kStatusIconDnd), (int16_t)(cy - kStatusIconDnd / 2),
                 kStatusIconDnd, kStatusIconDnd};
    right = r.dnd.x - kStatusGap;
  }
  if (f.wifi) {
    r.wifi = Rect{(int16_t)(right - kStatusIconWifi), (int16_t)(cy - kStatusIconWifi / 2),
                  kStatusIconWifi, kStatusIconWifi};
    right = r.wifi.x - kStatusGap;
  }
  if (f.bt) {
    r.bt = Rect{(int16_t)(right - kStatusIconBtW), (int16_t)(cy - kStatusIconBtH / 2),
                kStatusIconBtW, kStatusIconBtH};
    right = r.bt.x - kStatusGap;
  }
  r.rightStart = right + kStatusGap;   // the left edge of the leftmost right-hand cell

  // --- left: the time, then the fault mark -----------------------------------------------------
  r.time = Rect{(int16_t)(s.x + kStatusPadX), s.y, (int16_t)w.clock, s.h};
  r.leftEnd = r.time.right();
  if (f.fault) {
    r.fault = L.radioFault();
    r.leftEnd = r.fault.right();
  }

  r.fits = r.leftEnd + kStatusGap <= r.rightStart;
  return r;
}

// The measured-fit answer on its own, for a caller that only wants to know.
inline bool statusBarFits(const Layout& L, const StatusCellWidths& w, const StatusCellFlags& f) {
  return statusBarRects(L, w, f).fits;
}

}  // namespace ui
}  // namespace om
