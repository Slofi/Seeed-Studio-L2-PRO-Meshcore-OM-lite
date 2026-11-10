// SPDX-License-Identifier: MIT
// The Info page a Nodes long-press opens (EST-342): a full page with the public key (short and
// whole), the type, the last advert as a time, the position when there is one, and the path. No tab
// bar: it is a detail page with one way back, the header row. Draws only into the off-screen frame.
#include "ui/screen_node_info.h"

#include <stdio.h>

#include "../fonts.h"
#include "board.h"
#include "ui/layout.h"
#include "ui/theme.h"

namespace om {
namespace ui {

Rect nodeInfoBackRect() {
  const Layout& L = l2();
  return Rect{L.content.x, L.content.y, L.content.w, kHeadH};
}

void nodeInfoDraw(const StatusModel& status, const NodeInfoModel& m) {
  if (!omFrameBegin()) {
    omFrameEnd();
    return;
  }
  lgfx::LovyanGFX* d = om_display();
  if (!d) {
    omFrameEnd();
    return;
  }
  const Layout& L = l2();
  d->fillScreen(kScreen);
  drawStatusBar(d, L, status);

  const Rect back = nodeInfoBackRect();
  d->fillRect(back.x, back.y, back.w, back.h, kScreen);
  d->drawFastHLine(back.x, back.bottom() - 1, back.w, kLine);
  const int cy = back.y + back.h / 2;

  // A left chevron drawn as two lines (the faces are ASCII only), then the node's name.
  const int ax = back.x + kHeadPadX + 2;
  d->drawLine(ax + 3, cy - 4, ax, cy, kAcc);
  d->drawLine(ax, cy, ax + 3, cy + 4, kAcc);
  drawTextLeft(d, FontRole::SansTitle, kInk, kScreen, ax + 9, cy, m.name[0] ? m.name : "Info");

  NodeInfoLine lines[kMaxNodeInfoLines];
  const int n = nodeInfoLines(m, lines, kMaxNodeInfoLines);
  const int16_t rowH = 20;
  int16_t y = (int16_t)(back.bottom() + 5);
  for (int i = 0; i < n; ++i) {
    const int lineCy = y + rowH / 2;
    if (lines[i].label[0]) {
      drawTextLeft(d, FontRole::MonoMeta, kDim, kScreen, kRowPadX, lineCy, lines[i].label);
    }
    drawTextEllipsis(d, FontRole::MonoMeta, kInk, kScreen, kInfoColX, lineCy,
                     L.width - kInfoColX - kRowPadX, lines[i].value);
    y = (int16_t)(y + rowH);
  }

  // The hint at the floor: how to leave, for the page that has no tab bar.
  drawTextEllipsis(d, FontRole::MonoMeta, kDimmer, kScreen, kRowPadX, L.height - 10,
                   L.width - 2 * kRowPadX, "tap the header to go back");

  om::useAppFont(d);
  omFrameEnd();
}

}  // namespace ui
}  // namespace om
