// SPDX-License-Identifier: MIT
// The Info page a Nodes long-press opens (EST-342): a full page with the public key (short and
// whole), the type, the last advert as a time, the position when there is one, and the path. No tab
// bar: it is a detail page with one way back, the header row. Draws only into the off-screen frame.
// EST-400: the rows are clipped to their body and the body scrolls; the hint has its own rectangle
// on the floor instead of a literal baseline the last row could reach.
#include "ui/screen_node_info.h"

#include <stdio.h>

#include "../fonts.h"
#include "board.h"
#include "ui/layout.h"
#include "ui/theme.h"

namespace om {
namespace ui {

Rect nodeInfoBackRect() { return nodeInfoLayoutFor(l2()).back; }

void nodeInfoDraw(const StatusModel& status, const NodeInfoModel& m, int scroll) {
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

  const NodeInfoLayout g = nodeInfoLayoutFor(L);
  const Rect back = g.back;
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
  const int16_t y0 = (int16_t)(g.body.y - scroll);
  d->setClipRect(g.body.x, g.body.y, g.body.w, g.body.h);
  for (int i = 0; i < n; ++i) {
    const int lineCy = (int16_t)(y0 + i * kNodeInfoRowH + kNodeInfoRowH / 2);
    if (lines[i].label[0]) {
      drawTextLeft(d, FontRole::MonoMeta, kDim, kScreen, kRowPadX, lineCy, lines[i].label);
    }
    drawTextEllipsis(d, FontRole::MonoMeta, kInk, kScreen, kInfoColX, lineCy,
                     L.width - kInfoColX - kRowPadX, lines[i].value);
  }
  d->clearClipRect();

  // The hint at the floor: how to leave, for the page that has no tab bar. Its own rectangle, above
  // the screen's last rows and clear of the body (kNodeInfoHintGap).
  drawTextEllipsis(d, FontRole::MonoMeta, kDimmer, kScreen, g.hint.x,
                   g.hint.y + g.hint.h / 2, g.hint.w, "tap the header to go back");

  om::useAppFont(d);
  omFrameEnd();
}

}  // namespace ui
}  // namespace om
