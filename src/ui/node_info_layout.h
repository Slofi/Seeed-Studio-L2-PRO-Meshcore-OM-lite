// SPDX-License-Identifier: MIT
// EST-400: the Info page's geometry, plain C++ so test/host can prove it. The page is a header row
// (the one way back), a scrolling body of kNodeInfoRowH rows, and a hint on the screen floor. The old
// page drew the rows from the header with no clip and put the hint at L.height - 10, so a ninth row
// (one field away: hasSnr is false today) ended at 234 on top of the hint's line at 230.
#pragma once

#include <stdint.h>

#include "layout.h"
#include "theme.h"

namespace om {
namespace ui {

constexpr int16_t kNodeInfoRowH = 20;
constexpr int16_t kNodeInfoHintH = 16;   // one mono line
constexpr int16_t kNodeInfoHintGap = 2;  // body -> hint, and hint -> screen floor
constexpr int16_t kNodeInfoBodyGap = 5;  // header -> first row

struct NodeInfoLayout {
  Rect back;   // the header row: the chevron and the name, and the tap that leaves
  Rect body;   // the rows' viewport: the clip rect and the scroll's limit
  Rect hint;   // "tap the header to go back", on the floor
};

constexpr NodeInfoLayout nodeInfoLayoutFor(const Layout& L) {
  NodeInfoLayout g;
  g.back = Rect{L.content.x, L.content.y, L.content.w, kHeadH};
  g.hint = Rect{kRowPadX, (int16_t)(L.height - kNodeInfoHintH - kNodeInfoHintGap),
                (int16_t)(L.width - 2 * kRowPadX), kNodeInfoHintH};
  const int16_t top = (int16_t)(g.back.bottom() + kNodeInfoBodyGap);
  const int16_t room = (int16_t)(g.hint.y - kNodeInfoHintGap - top);
  const int16_t bodyH = room > 0 ? room : 0;
  g.body = Rect{0, top, L.width, bodyH};
  return g;
}

// How far a list of `lineCount` rows can scroll inside this body. Zero when they all fit.
constexpr int nodeInfoMaxScroll(int lineCount, const NodeInfoLayout& g) {
  const int total = lineCount * kNodeInfoRowH;
  return total > g.body.h ? total - (int)g.body.h : 0;
}

// The same fingertip rule as the Nodes list and the Settings pages (kSelGive px of stretch, snapped
// back on release), so the Info page feels like every other list.
constexpr int nodeInfoDragScroll(int base, int dyUp, int maxScroll) {
  int next = base + dyUp;
  if (next < -kSelGive) next = -kSelGive;
  if (next > maxScroll + kSelGive) next = maxScroll + kSelGive;
  return next;
}

constexpr int nodeInfoSnapScroll(int v, int maxScroll) {
  if (v < 0) return 0;
  if (v > maxScroll) return maxScroll;
  return v;
}

}  // namespace ui
}  // namespace om
