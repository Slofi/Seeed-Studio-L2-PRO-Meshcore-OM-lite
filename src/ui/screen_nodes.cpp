// SPDX-License-Identifier: MIT
// Nodes screen: real MeshCore contacts only. If the contact table is empty the honest empty state is
// shown, never sample nodes. The long-press sheet (EST-342) is drawn in the same frame, last, so it
// covers the tab bar exactly like the chat's bottom sheets.
#include "ui/screen_nodes.h"

#include <stdio.h>
#include <string.h>

#include "../fonts.h"   // om::useAppFont: restore the old screens' 12 px default before the frame closes
#include "board.h"
#include "ui/layout.h"
#include "ui/nodes_header.h"
#include "ui/theme.h"

namespace om {
namespace ui {
namespace {

int sheetRowCount(const NodesView& v) {
  if (v.trace != nullptr) return v.trace->count > 0 ? v.trace->count + 1 : 1;
  return v.menu != nullptr ? v.menuCount : 0;
}

void drawSheetHead(lgfx::LovyanGFX* d, const NodesView& v, const NodeSheetLayout& g) {
  const int cy1 = g.head.y + kSheetLine1Cy;
  drawTextEllipsis(d, FontRole::SansBody, kInk, kSheet, g.head.x, cy1, g.head.w,
                   v.menuTitle ? v.menuTitle : "");
  if (v.menuSub && v.menuSub[0]) {
    drawTextEllipsis(d, FontRole::MonoMeta, kDim, kSheet, g.head.x, g.head.y + kSheetLine2Cy,
                     g.head.w, v.menuSub);
  }
  d->drawFastHLine(g.head.x, g.head.bottom() - 1, g.head.w, kLine);
}

void drawActionRow(lgfx::LovyanGFX* d, const Rect& r, const char* label, const char* value) {
  const int cy = r.y + r.h / 2;
  drawTextEllipsis(d, FontRole::SansBody, kInk, kSheet, r.x, cy, r.w - 70, label);
  if (value != nullptr && value[0] != 0) {
    drawTextRight(d, FontRole::MonoMeta, value[0] == 'o' && value[1] == 'n' ? kAcc : kDim, kSheet,
                  r.right(), cy, value);
  }
  d->drawFastHLine(r.x, r.bottom() - 1, r.w, kRowLine);
}

void drawNodeSheet(lgfx::LovyanGFX* d, const NodesView& v) {
  const int rows = sheetRowCount(v);
  if (rows <= 0) return;
  const NodeSheetLayout g = nodeSheetLayout(l2(), rows);
  d->fillRoundRect(g.panel.x, g.panel.y, g.panel.w, (int16_t)(g.panel.h + kOskRadius), kOskRadius,
                   kSheet);
  d->drawFastHLine(g.panel.x, g.panel.y, g.panel.w, kLine);
  drawSheetHead(d, v, g);

  if (v.trace != nullptr) {
    const TraceView& t = *v.trace;
    if (t.count <= 0) {
      drawActionRow(d, g.rows[0], "No trace data back", "");
      return;
    }
    for (int i = 0; i < t.count && i < g.rowCount; ++i) {
      char value[24];
      snprintf(value, sizeof(value), "SNR %.1f", (double)t.hops[i].snr4 / 4.0);
      drawActionRow(d, g.rows[i], t.hops[i].name, value);
    }
    if (t.count < g.rowCount) {
      char value[24];
      snprintf(value, sizeof(value), "SNR %.1f", (double)t.finalSnr4 / 4.0);
      drawActionRow(d, g.rows[t.count], "at this node", value);
    }
    return;
  }

  for (int i = 0; i < v.menuCount && i < g.rowCount; ++i) {
    drawActionRow(d, g.rows[i], v.menu[i].label, v.menu[i].value);
  }
}

}  // namespace

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
      row.favourite = view.items[i].favourite;
      drawListRow(d, L.row(i, view.scroll), row);
    }
    d->clearClipRect();
  }

  drawStatusBar(d, L, view.status);
  drawTabBar(d, L, view.tabs);
  drawNodeSheet(d, view);   // over the tab bar, like the chat's message sheet

  // The old tabs share this frame and never set a font of their own; leave them the 12 px default.
  om::useAppFont(d);
  omFrameEnd();
}

Rect nodeSheetPanel(const NodesView& view) {
  const int rows = sheetRowCount(view);
  if (rows <= 0) return Rect{};
  return nodeSheetLayout(l2(), rows).panel;
}

int nodeSheetRowAt(const NodesView& view, int x, int y) {
  if (view.trace != nullptr) return -1;   // the trace sheet is information: any tap closes it
  if (view.menu == nullptr || view.menuCount <= 0) return -1;
  const NodeSheetLayout g = nodeSheetLayout(l2(), view.menuCount);
  return nodeSheetRowAt(g, x, y);
}

}  // namespace ui
}  // namespace om
