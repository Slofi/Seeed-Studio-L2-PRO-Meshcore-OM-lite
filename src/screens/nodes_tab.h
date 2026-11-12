// SPDX-License-Identifier: MIT
// EST-411: the Nodes tab's own module, moved out of main.cpp the way rptr_tab.{h,cpp} was in
// EST-410. It owns the tab's state (the list copy, the scroll bases, the node sheet, the Info page,
// the trace sheet, the new-node dot and the long-press timing), drawHome(), the Nodes tap handling,
// handleNodeAction() and the helpers only Nodes uses. mesh_link.cpp (the contact table) and the
// favourites store are unchanged and stay where they are.
//
// The shell services it needs come in through the small Host below, set once in setup(): the status
// bar, the tab bar model, the toast, whether the Nodes screen is the one on the glass, and the two
// shell actions a sheet row can reach (open a DM, switch page). The frame buffer itself is not a
// service: ui/screen_nodes.cpp draws through omFrameBegin()/omFrameEnd() exactly as before.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mesh_link.h"
#include "ui/press.h"
#include "ui/screen_nodes.h"
#include "ui/tab_bar.h"
#include "ui/widgets.h"

namespace om {
namespace nodes_tab {

// The rows are copied out of mesh_link's contact table, never sample data.
constexpr int kNodeCap = 64;

// The shell services Nodes needs. Set once at boot (begin()).
struct Host {
  void (*buildStatus)(ui::StatusModel&) = nullptr;
  ui::TabBarModel (*tabBar)(ui::Tab) = nullptr;
  void (*toast)(const char*) = nullptr;
  // g_tab == 0 && !g_settings: the long-press arm and the sheet's press-away rule only run while
  // this screen is the one on the glass.
  bool (*screenVisible)() = nullptr;
  // The sheet's Direct message row and its Login/Show on map rows: the shell's chatOpenDmFor() and
  // its one page switch (showTab()) stay in main.cpp and are reached through these two.
  bool (*openDm)(const char*) = nullptr;
  void (*showTab)(int) = nullptr;
};

// `items` is the PSRAM row buffer and `scratch` the one contact-table copy: both are allocated by
// main.cpp's allocUiBuffers() (EST-357's one PSRAM point) and handed over here once.
void begin(const Host& host, ui::NodeItem* items, MeshContact* scratch);

// The whole tab: the normal frame, one tap, and the pressed look's own rectangle.
void draw();
bool tap(int x, int y, bool longPress);
ui::PressTarget pressTarget(int x, int y);

// The Info page or a sheet owns every tap and the whole frame while it is up (the shell must not
// reach the tab bar behind it).
bool overlayOpen();
// A sheet (not the Info page) is up: the auto-off timer waits.
bool modalOpen();

// State the shell reads.
int count();
const ui::NodeItem& item(int i);
uint32_t seenSignature();      // the contact-table signature of the last drawn frame
bool newNodeDot();             // the dot on the tab bar
void noteNewNode();            // a contact was added by an advert
void refresh();                // refreshNodes(), for the USB commands
void selectByName(const char* name);              // Chat > Node details
void setTrace(const ui::TraceView& tv, const char* title);   // a trace's hops came back

// The loop's gesture hooks. The shell keeps the press/release latches and the 60 ms frame clock;
// these touch only the tab's own state.
void beginDrag();              // the finger went down: the two scroll bases
void holdPress(int x, int y, uint32_t now);       // arm the long-press rule on a row
void holdDisarm();             // a new press: the old arm is dropped (no reset)
bool holdArmed();
void holdReset();
bool holdUpdate(int x, int y, uint32_t now);      // true when the hold fired
void holdRelease();
bool closeSheetAwayFrom(int x, int y);            // true when this press closed a sheet
void dragMove(int dy, uint32_t& lastRender);
void releaseSnap(bool dragging);

}  // namespace nodes_tab
}  // namespace om
