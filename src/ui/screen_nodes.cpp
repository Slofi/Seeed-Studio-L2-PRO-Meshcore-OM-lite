// SPDX-License-Identifier: MIT
// Nodes screen: real MeshCore contacts only. If the contact table is empty the honest empty state is
// shown, never sample nodes.
#include "ui/screen_nodes.h"

#include <stdio.h>

#include "../fonts.h"   // om::useAppFont: restore the old screens' 12 px default before the frame closes
#include "board.h"
#include "ui/layout.h"
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

  if (view.count <= 0 || view.items == nullptr) {
    drawEmptyState(d, L.content, "No nodes yet", "Waiting for adverts. Heard nodes appear here.");
  } else {
    // One clip for the whole list: a row scrolled half under the status bar is cut there. The full
    // row rect keeps the text at its true centre; the clip is what trims it, exactly like the
    // visible rect the tap test uses.
    d->setClipRect(L.content.x, L.content.y, L.content.w, L.content.h);
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
