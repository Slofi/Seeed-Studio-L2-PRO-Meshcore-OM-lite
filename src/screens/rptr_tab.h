// SPDX-License-Identifier: MIT
// EST-410: the RPTR tab's own module, moved out of main.cpp the way map_tab.{h,cpp} was in EST-371.
// It owns the tab's state (session, list/page scroll, the console queue, the confirmation, the map
// picker and the keyboard sheet), drawRptrTab()/handleRptrTap(), the RPTR view builder and the
// helpers only RPTR uses. rptr_link.cpp (the session), rptr_page.cpp (the rows) and rptr_model.cpp
// (the parsers) are unchanged and stay where they are.
//
// The shell services it needs come in through the small Host below, set once in setup(): the status
// bar, the tab bar model, the raw GPS fix, the toast, whether the RPTR screen is the one on the
// glass, and the shell's redraw. The frame buffer itself is not a service: ui/screen_rptr.cpp draws
// through omFrameBegin()/omFrameEnd() exactly as before.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "gps_sky.h"
#include "ui/layout.h"
#include "ui/press.h"
#include "ui/widgets.h"
#include "ui/tab_bar.h"

namespace om {
namespace rptr_tab {

// The shell services RPTR needs. Set once at boot (begin()).
struct Host {
  void (*buildStatus)(ui::StatusModel&) = nullptr;
  ui::TabBarModel (*tabBar)(ui::Tab) = nullptr;
  // EST-410: the RAW GPS parser state, not the effective own position. The picker's "Use my GPS"
  // and its GPS-available line read g_gps before the move and must keep reading the same source;
  // map_tab's Host::gps is posOwn() (EST-384) and is a different thing.
  const GpsState* (*gps)() = nullptr;
  void (*toast)(const char*) = nullptr;
  // g_tab == 3 && !g_settings: the notification door and the once-a-pass redraw need to know
  // whether this screen is the one on the glass.
  bool (*screenVisible)() = nullptr;
  // The shell's redrawCurrentScreen(); its bool answer (a running screen change refused the frame)
  // was never read on the RPTR paths, and it is not read here either.
  bool (*redraw)() = nullptr;
};

void begin(const Host& host);

// The whole tab: the normal frame, one tap, the drag and its release, and the asynchronous pump.
void draw();
bool tap(int x, int y);
// EST-390: the rectangle the pressed look goes on. Same hit test as tap(), so the tint and the tap
// can never cover different pixels.
ui::PressTarget pressTarget(int x, int y);
void pump();
void beginDrag();
// A move of a finger that began on the RPTR tab. `drag` is the shell's drag latch (the picker's pan
// sets it); `lastX`/`lastY` are the shell's pan base for the picker; `lastRender` is the shell's
// 60 ms frame clock, stamped before a frame goes out exactly as the loop did before the move.
void dragMove(int x, int y, int dy, bool& drag, int& lastX, int& lastY, uint32_t& lastRender);
void endPickerDrag();
// The release clamp for the list and the page rows (the picker has no scroll of its own).
void releaseSnap(bool dragging);

// State the shell reads. Kept behind accessors because it is the tab's own.
bool keyboardOpen();      // the keyboard sheet covers the tab bar and the gear
// EST-412: the CardKB drives the open sheet through the same functions its own on-screen keys do.
// Each returns false when the sheet is not open (or the field refuses the character), so a key
// pressed at the wrong moment changes nothing. Confirm is the sheet's Send/OK.
bool keyboardText(char ch);
bool keyboardBackspace();
bool keyboardConfirm();
bool keyboardClose();     // Esc and the tap above the sheet both just close it
bool modalOpen();         // a confirmation is up
bool mapPickerOpen();     // the Position page's map picker is the whole frame
bool mapPickerDragging();
// EST-414: the tab no longer scans when it opens (the Scan chip is the only door), so this getter
// has no caller left; it stays because it is the tab's own answer to "is the list showing" and a
// later ticket (a USB status line, a test) may ask it.
bool listShowing();
const char* openNodeName();   // the repeater the open session is about; Settings shows it too

// Actions the shell and Settings take on the session.
void open(const char* name);  // Nodes > Login: that repeater's page
void clearSession();          // Settings > Forget: the open session is dropped

// A coordinate in the display form Settings' own-position picker uses (EST-384, the same helper the
// RPTR Position page's editor uses).
void coordText(char* out, size_t cap, int32_t e6);

// EST-417: one arrow press scrolls this tab one row (the list's 36 px rows or the page's 30 px ones),
// clamped by the same rule a finger's release uses. `dir` is +1 for the row below, -1 for the row
// above. Nothing while the sheet, a confirmation or the map picker owns the frame.
void scrollByRow(int dir);

}  // namespace rptr_tab
}  // namespace om
