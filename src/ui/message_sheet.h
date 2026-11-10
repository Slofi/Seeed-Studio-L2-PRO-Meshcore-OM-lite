// SPDX-License-Identifier: MIT
// The message sheet: what a tap on a message opens (EST-336). A bottom sheet in the mock's style,
// like the keyboard sheet, drawn over the tab bar; a tap outside closes it. Its header carries what
// the old INFO panel showed (sender, channel, age, hops, SNR), so INFO is no longer a separate step.
//
// Rows appear only when their action can actually work: a "Direct message" row without the sender's
// public key (nothing to encrypt to), a "Node details" row for a sender who is not a known contact,
// a "Show on map" row for a contact with no position and a "Path" row for a packet that carried no
// repeater are all left OUT, never greyed.
//
// Plain C++ (no LovyanGFX, no Arduino), so test/host proves the rows, the rectangles and the path
// naming with the system g++.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "layout.h"
#include "theme.h"

namespace om {
namespace ui {

enum class SheetAction : uint8_t {
  None = 0,
  Reply,          // the "@Name text" draft, as REPLY did
  DirectMessage,  // the sender's thread, opened or created (needs the contact's key)
  CopyText,       // the whole text into the draft
  Mention,        // "@Name " into the draft, without the reply bar
  NodeDetails,    // jump to the sender in Nodes
  ShowOnMap,      // the sender's advertised position on the map
  Path,           // the repeaters the packet came over
  Resend,         // own message: the same text on the same channel, a new packet
  HeardRepeats,   // own message: how often we heard our packet repeated by others
};

constexpr int kMaxSheetRows = 7;   // a channel message with DMs can offer all seven

struct SheetRow {
  SheetAction action = SheetAction::None;
  const char* label = "";
  const char* value = "";   // right-hand mono text (the path), "" = none
};

// One message's facts, filled by the shell from the mesh facade.
struct SheetContext {
  bool mine = false;
  // EST-342: "Direct message" is offered only when the sender is a known contact whose public key
  // we hold (that key is what encrypts the DM), and never while the sheet is already open inside
  // that same thread.
  bool senderHasKey = false;
  bool alreadyDm = false;
  bool dm = false;                 // a direct message thread (Resend/Heard differ there)
  bool senderKnown = false;
  bool senderHasPosition = false;
  bool pathKnown = false;
  const char* pathText = "";   // formatMessagePath()'s output, "" when there is no path
};

// The rows for one message, in the mock's order: the things that write first, then what the message
// is, then where it came from. Returns the count (0..kMaxSheetRows).
int messageSheetRows(const SheetContext& c, SheetRow* out, int max);

// The sheet's geometry: full width, rounded top corners, anchored to the screen's bottom edge, as
// tall as its contents. The header is two lines; each row is kSheetRowH and its bottom edge is the
// 1 px row line.
struct MessageSheetLayout {
  Rect panel;
  Rect head;
  Rect rows[kMaxSheetRows];
  int rowCount = 0;
};

MessageSheetLayout messageSheetLayout(const Layout& L, int rowCount);

// The row under (px, py), or -1 (the header, the pads and anything outside the panel answer -1, so
// the shell closes the sheet).
int messageSheetRowAt(const MessageSheetLayout& g, int px, int py);

// "OM-RPTR > 3fa2": one entry per hash in the packet's path, in the order the packet picked them up.
// `resolve` returns the matching repeater's name or ""; an unmatched hash stays hex, `hashSize`
// bytes (1..3 bytes = 2..6 hex digits). Returns the number of bytes written.
using PathNameFn = const char* (*)(void* ctx, const uint8_t* hash, uint8_t len);
int formatMessagePath(char* out, size_t cap, const uint8_t* path, uint8_t hashSize, uint8_t count,
                      PathNameFn resolve, void* ctx);

}  // namespace ui
}  // namespace om
