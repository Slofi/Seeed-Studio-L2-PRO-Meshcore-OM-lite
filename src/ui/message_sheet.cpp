// SPDX-License-Identifier: MIT
#include "ui/message_sheet.h"

#include <stdio.h>
#include <string.h>

namespace om {
namespace ui {
namespace {

void add(SheetRow* out, int& n, int max, SheetAction action, const char* label,
         const char* value = "") {
  if (n >= max) return;
  out[n].action = action;
  out[n].label = label;
  out[n].value = value ? value : "";
  ++n;
}

}  // namespace

int messageSheetRows(const SheetContext& c, SheetRow* out, int max) {
  if (out == nullptr || max <= 0) return 0;
  int n = 0;
  if (!c.mine) {
    add(out, n, max, SheetAction::Reply, "Reply");
    // The DM store arrives with upstream's companion code. Until then there is no DM to open, so the
    // row is left out rather than shown greyed (EST-336).
    if (c.dmAvailable) add(out, n, max, SheetAction::DirectMessage, "Direct message");
    add(out, n, max, SheetAction::CopyText, "Copy text");
    add(out, n, max, SheetAction::Mention, "Mention");
    if (c.senderKnown) add(out, n, max, SheetAction::NodeDetails, "Node details");
    if (c.senderHasPosition) add(out, n, max, SheetAction::ShowOnMap, "Show on map");
    if (c.pathKnown) add(out, n, max, SheetAction::Path, "Path", c.pathText);
  } else {
    add(out, n, max, SheetAction::CopyText, "Copy text");
    add(out, n, max, SheetAction::Resend, "Resend");
    // The count is drawn by the screen from the held message (screen_chat.cpp), because it is a live
    // number: the row itself only says what the tap means.
    add(out, n, max, SheetAction::HeardRepeats, "Heard repeats");
  }
  return n;
}

MessageSheetLayout messageSheetLayout(const Layout& L, int rowCount) {
  MessageSheetLayout g;
  if (rowCount < 0) rowCount = 0;
  if (rowCount > kMaxSheetRows) rowCount = kMaxSheetRows;
  g.rowCount = rowCount;
  const int16_t h = (int16_t)(2 * kSheetPadY + kSheetHeadH + rowCount * kSheetRowH);
  g.panel = Rect{0, (int16_t)(L.height - h), L.width, h};
  g.head = Rect{kSheetPadX, (int16_t)(g.panel.y + kSheetPadY),
                (int16_t)(L.width - 2 * kSheetPadX), kSheetHeadH};
  int16_t y = g.head.bottom();
  for (int i = 0; i < rowCount; ++i) {
    g.rows[i] = Rect{kSheetPadX, y, (int16_t)(L.width - 2 * kSheetPadX), kSheetRowH};
    y = (int16_t)(y + kSheetRowH);
  }
  return g;
}

int messageSheetRowAt(const MessageSheetLayout& g, int px, int py) {
  for (int i = 0; i < g.rowCount; ++i) {
    if (g.rows[i].contains(px, py)) return i;
  }
  return -1;
}

int formatMessagePath(char* out, size_t cap, const uint8_t* path, uint8_t hashSize, uint8_t count,
                      PathNameFn resolve, void* ctx) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = 0;
  if (path == nullptr || hashSize < 1 || hashSize > 3 || count == 0) return 0;
  if ((int)count * (int)hashSize > 64) return 0;   // MAX_PATH_SIZE
  size_t n = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t* hash = path + (size_t)i * hashSize;
    if (i > 0) {
      if (n + 3 >= cap) break;
      memcpy(out + n, " > ", 3);
      n += 3;
    }
    const char* name = resolve ? resolve(ctx, hash, hashSize) : nullptr;
    if (name != nullptr && name[0] != 0) {
      const size_t len = strlen(name);
      if (n + len >= cap) break;
      memcpy(out + n, name, len);
      n += len;
    } else {
      char hex[7];
      for (uint8_t b = 0; b < hashSize; ++b) snprintf(hex + b * 2, 3, "%02x", hash[b]);
      hex[hashSize * 2] = 0;
      const size_t len = (size_t)hashSize * 2;
      if (n + len >= cap) break;
      memcpy(out + n, hex, len);
      n += len;
    }
  }
  out[n] = 0;
  return (int)n;
}

}  // namespace ui
}  // namespace om
