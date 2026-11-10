// SPDX-License-Identifier: MIT
// Nodes tab on the new UI layer. Draws only into the off-screen frame.
#pragma once

#include <stdint.h>

#include "ui/node_menu.h"
#include "ui/widgets.h"

namespace om {
namespace ui {

struct NodeItem {
  char name[32] = "";
  NodeType type = NodeType::Unknown;
  int16_t rssi = kNoRssi;
  char note[16] = "";   // "direct", "path 4B", ...
  char age[16] = "";    // "14:02", "Mon 14:02", "10.11. 14:02", or an age when the clock was not real
  bool favourite = false;   // a star, kept in our own NVS
};

struct NodesView {
  StatusModel status;
  const NodeItem* items = nullptr;
  int count = 0;
  int selected = -1;
  int scroll = 0;
  int unread = 0;
  // The one-line header above the list: "<N> nodes heard", plus "<M> in the last hour" only when
  // the timestamps are real (main computes both from the contact table).
  int heardLastHour = 0;
  bool heardLastHourKnown = false;
  bool newNodeDot = false;   // a contact was added since this screen was last opened
  // EST-342: the long-press sheet. A menu (rows built by nodeMenuRows) or a trace result; nullptr
  // and no trace means the sheet is closed. The shell fills these from the contact table.
  const NodeMenuRow* menu = nullptr;
  int menuCount = 0;
  const char* menuTitle = "";
  const char* menuSub = "";
  const TraceView* trace = nullptr;
};

// Opens the frame, draws the screen, closes (and pushes) the frame. The caller keeps the state.
void nodesScreenDraw(const NodesView& view);

// The open sheet's panel, or an empty rect when it is closed. The shell needs it on the PRESS so a
// touch outside closes the sheet instead of acting on the list behind it.
Rect nodeSheetPanel(const NodesView& view);
// The row under (x, y) in a node MENU; -1 for the header, the pads, a trace sheet or anything
// outside. The row index is the one nodeMenuRows() returned.
int nodeSheetRowAt(const NodesView& view, int x, int y);

}  // namespace ui
}  // namespace om
