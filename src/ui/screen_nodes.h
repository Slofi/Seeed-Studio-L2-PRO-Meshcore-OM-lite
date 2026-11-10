// SPDX-License-Identifier: MIT
// Nodes tab on the new UI layer. Draws only into the off-screen frame.
#pragma once

#include <stdint.h>

#include "ui/widgets.h"

namespace om {
namespace ui {

struct NodeItem {
  char name[32] = "";
  NodeType type = NodeType::Unknown;
  int16_t rssi = kNoRssi;
  char note[16] = "";   // "direct", "path 4B", ...
  char age[8] = "";     // "now", "2m", "3h", ...
};

struct NodesView {
  StatusModel status;
  const NodeItem* items = nullptr;
  int count = 0;
  int selected = -1;
  int scroll = 0;
  int unread = 0;
};

// Opens the frame, draws the screen, closes (and pushes) the frame. The caller keeps the state.
void nodesScreenDraw(const NodesView& view);

}  // namespace ui
}  // namespace om
