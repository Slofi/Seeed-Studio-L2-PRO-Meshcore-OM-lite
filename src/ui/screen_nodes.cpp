// SPDX-License-Identifier: MIT
// Nodes screen: real MeshCore contacts only. If the contact table is empty the honest empty state is
// shown, never sample nodes.
#include "ui/screen_nodes.h"

#include <stdio.h>

#include "../fonts.h"   // om::useAppFont: restore the old screens' 12 px default before the frame closes
#include "board.h"
#include "ui/layout.h"
#include "ui/nodes_header.h"
#include "ui/theme.h"

namespace om {
namespace ui {

void nodesScreenDraw(const NodesView& view) {
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

  // The one-line header above the list: the node count lives here now, not in the top bar. It shows
  // even when the table is empty ("0 nodes heard"), so the number is never a mystery.
  char head[64];
  nodesHeaderText(head, sizeof(head), view.count, view.heardLastHour, view.heardLastHourKnown);
  const Rect h = L.listHeader();
  drawTextLeft(d, FontRole::MonoMeta, kDim, kScreen, h.x + kRowPadX, h.y + h.h / 2, head);

  if (view.count <= 0 || view.items == nullptr) {
    drawEmptyState(d, L.listBody(), "No nodes yet", "Waiting for adverts. Heard nodes appear here.");
  } else {
    // One clip for the whole list: a row scrolled half under the header is cut there. The full row
    // rect keeps the text at its true centre; the clip is what trims it, exactly like the visible
    // rect the tap test uses.
    const Rect body = L.listBody();
    d->setClipRect(body.x, body.y, body.w, body.h);
    for (int i = 0; i < view.count; ++i) {
      if (L.rowVisible(i, view.scroll).empty()) continue;
      RowModel row;
      row.name = view.items[i].name;
      row.type = view.items[i].type;
      row.rssi = view.items[i].rssi;
      row.note = view.items[i].note;
      row.age = view.items[i].age;
      row.selected = (i == view.selected);
      drawListRow(d, L.row(i, view.scroll), row);
    }
    d->clearClipRect();
  }

  drawStatusBar(d, L, view.status);
  drawTabBar(d, L, Tab::Nodes, view.unread);

  // The old tabs share this frame and never set a font of their own; leave them the 12 px default.
  om::useAppFont(d);
  omFrameEnd();
}

}  // namespace ui
}  // namespace om
