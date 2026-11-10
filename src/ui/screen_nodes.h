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
  char age[16] = "";    // "14:02", "Mon 14:02", "10.11. 14:02", or an age when the clock was not real
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
};

// Opens the frame, draws the screen, closes (and pushes) the frame. The caller keeps the state.
void nodesScreenDraw(const NodesView& view);

}  // namespace ui
}  // namespace om
