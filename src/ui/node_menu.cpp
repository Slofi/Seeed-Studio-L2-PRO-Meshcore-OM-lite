// SPDX-License-Identifier: MIT
#include "ui/node_menu.h"

#include <stdio.h>
#include <string.h>

namespace om {
namespace ui {
namespace {

void add(NodeMenuRow* out, int& n, int max, NodeAction action, const char* label,
         const char* value = "") {
  if (n >= max) return;
  out[n].action = action;
  out[n].label = label;
  out[n].value = value ? value : "";
  ++n;
}

}  // namespace

int nodeMenuRows(const NodeMenuContext& c, NodeMenuRow* out, int max) {
  if (out == nullptr || max <= 0) return 0;
  int n = 0;
  // A client can be written to; a repeater or a room cannot take a text DM, so the row is left out
  // rather than offered and failed.
  if (c.hasKey && c.advType == kAdvChat) {
    add(out, n, max, NodeAction::DirectMessage, "Direct message");
  }
  add(out, n, max, NodeAction::Info, "Info");
  if (c.hasPosition) add(out, n, max, NodeAction::ShowOnMap, "Show on map");
  // A status request works over the known path or as a flood (BaseChatMesh::sendRequest), so it is
  // offered for every contact whose key we hold.
  if (c.hasKey) add(out, n, max, NodeAction::Ping, "Ping");
  // A trace is a directed packet along the contact's path; with no path known there is nowhere to
  // aim it (upstream's CMD_SEND_TRACE_PATH also takes an explicit path from the app).
  if (c.hasKey && c.pathKnown) add(out, n, max, NodeAction::Trace, "Trace");
  if (c.hasKey && c.pathKnown) add(out, n, max, NodeAction::ResetPath, "Reset path");
  if (c.hasKey) {
    add(out, n, max, NodeAction::Favourite, "Favourite", c.favourite ? "on" : "off");
  }
  if (c.hasKey) {
    // Two taps: the first only renames the row, so a long press followed by a stray tap cannot
    // delete a contact.
    add(out, n, max, NodeAction::RemoveContact,
        c.removeArmed ? "Remove: tap again" : "Remove contact");
  }
  if (c.advType == kAdvRepeater && c.rptrTabExists) {
    add(out, n, max, NodeAction::LoginRptr, "Log in (RPTR)");
  }
  return n;
}

NodeSheetLayout nodeSheetLayout(const Layout& L, int rowCount) {
  NodeSheetLayout g;
  if (rowCount < 0) rowCount = 0;
  if (rowCount > kMaxNodeMenuRows) rowCount = kMaxNodeMenuRows;
  g.rowCount = rowCount;
  const int16_t h = (int16_t)(2 * kSheetPadY + kSheetHeadH + rowCount * kNodeSheetRowH);
  g.panel = Rect{0, (int16_t)(L.height - h), L.width, h};
  g.head = Rect{kSheetPadX, (int16_t)(g.panel.y + kSheetPadY),
                (int16_t)(L.width - 2 * kSheetPadX), kSheetHeadH};
  int16_t y = g.head.bottom();
  for (int i = 0; i < rowCount; ++i) {
    g.rows[i] = Rect{kSheetPadX, y, (int16_t)(L.width - 2 * kSheetPadX), kNodeSheetRowH};
    y = (int16_t)(y + kNodeSheetRowH);
  }
  return g;
}

int nodeSheetRowAt(const NodeSheetLayout& g, int px, int py) {
  for (int i = 0; i < g.rowCount; ++i) {
    if (g.rows[i].contains(px, py)) return i;
  }
  return -1;
}

int traceRowText(char* out, size_t cap, const TraceHop& hop) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "%s  SNR %.1f", hop.name[0] ? hop.name : "?",
                         (double)hop.snr4 / 4.0);
  return n < 0 ? 0 : n;
}

}  // namespace ui
}  // namespace om
