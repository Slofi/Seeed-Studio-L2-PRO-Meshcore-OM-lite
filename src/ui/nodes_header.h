// SPDX-License-Identifier: MIT
// The Nodes screen's one-line header, above the list (EST-337).
//
// Filip: the node count does not belong in the top bar ("4 nodes, it should be seen in the Nodes
// screen, not in the bar"). So it is the first line of the Nodes screen, mono and dim:
//
//   4 nodes heard
//   4 nodes heard · 2 in the last hour
//
// The count is the contact table: every node whose advert we heard and kept, including the ones
// restored from flash. The second clause appears only when the timestamps are real (a real clock
// source has set the time since boot), because a count from a build-time floor would be a guess.
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host compiles it with the system g++.
#pragma once

#include <stddef.h>
#include <stdio.h>

namespace om {
namespace ui {

// nodes is clamped to three digits (a header line, not a table). lastHour is drawn only when
// lastHourKnown; a negative lastHour is treated as 0 when it is shown.
inline void nodesHeaderText(char* out, size_t cap, int nodes, int lastHour, bool lastHourKnown) {
  if (!out || cap == 0) return;
  const int n = nodes < 0 ? 0 : (nodes > 999 ? 999 : nodes);
  const char* noun = (n == 1) ? "node" : "nodes";
  if (!lastHourKnown) {
    snprintf(out, cap, "%d %s heard", n, noun);
    return;
  }
  const int m = lastHour < 0 ? 0 : (lastHour > 999 ? 999 : lastHour);
  // U+00B7 MIDDLE DOT, the mock's separator; the mono face carries it (tools/mkvlw.py --extra 0xb7).
  snprintf(out, cap, "%d %s heard \xC2\xB7 %d in the last hour", n, noun, m);
}

}  // namespace ui
}  // namespace om
