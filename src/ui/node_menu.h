// SPDX-License-Identifier: MIT
// The Nodes long-press menu: one bottom sheet in the message sheet's style, and the rows each node
// type can actually use. A row is present only when its action can work; it is never shown greyed
// (the same rule the message sheet follows). Plain C++ (no LovyanGFX, no Arduino), so test/host
// proves the rows per node type and the sheet's hit rectangles with the system g++.
//
// MeshCore's advert types (lib/meshcore/helpers/AdvertDataHelpers.h): 1 client/chat, 2 repeater,
// 3 room, 4 sensor. The screen keeps the same numbers, so this header needs no MeshCore include.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "layout.h"
#include "theme.h"

namespace om {
namespace ui {

constexpr uint8_t kAdvChat = 1;
constexpr uint8_t kAdvRepeater = 2;
constexpr uint8_t kAdvRoom = 3;
constexpr uint8_t kAdvSensor = 4;

// "client" / "repeater" / "room" / "sensor"; "node" for anything else. The new-node toast and the
// sheet header both use these words, so the two can never disagree.
inline const char* advTypeName(uint8_t t) {
  switch (t) {
    case kAdvChat: return "client";
    case kAdvRepeater: return "repeater";
    case kAdvRoom: return "room";
    case kAdvSensor: return "sensor";
    default: return "node";
  }
}

enum class NodeAction : uint8_t {
  None = 0,
  DirectMessage,
  Info,
  ShowOnMap,
  Ping,
  Trace,
  ResetPath,
  Favourite,
  RemoveContact,
  LoginRptr,
};

// Eight is the real maximum: a client offers DM, Info, map, Ping, Trace, Reset, Favourite, Remove;
// a repeater swaps DM for the RPTR login. Eight rows of 20 px keep the panel above the status bar.
constexpr int kMaxNodeMenuRows = 8;

struct NodeMenuRow {
  NodeAction action = NodeAction::None;
  const char* label = "";
  const char* value = "";   // right-hand mono text (favourite state), "" = none
  // EST-400: the value is gold (a state that is ON). An explicit flag, because the drawing used to
  // test `value[0] == 'o' && value[1] == 'n'`, so any value beginning "on" was drawn as a state.
  bool valueAccent = false;
};

// One node's facts, filled by the shell from the contact table.
struct NodeMenuContext {
  uint8_t advType = 0;
  bool hasKey = false;        // a real contact, so its public key is known
  bool hasPosition = false;
  bool pathKnown = false;     // ContactInfo::out_path_len != OUT_PATH_UNKNOWN
  bool favourite = false;
  bool rptrTabExists = false; // this build has an RPTR tab
  bool removeArmed = false;   // the two-step remove: the first tap renames the row
};

// The rows in the mock's order. Returns 0..kMaxNodeMenuRows.
int nodeMenuRows(const NodeMenuContext& c, NodeMenuRow* out, int max);

// The sheet's geometry: as tall as its contents, anchored to the bottom, rounded at the top. The
// header is two lines (name, then type, last heard and path); rows are kNodeSheetRowH.
struct NodeSheetLayout {
  Rect panel;
  Rect head;
  Rect rows[kMaxNodeMenuRows];
  int rowCount = 0;
};

NodeSheetLayout nodeSheetLayout(const Layout& L, int rowCount);
// The row under (px, py), or -1 (the header and the pads answer -1, which closes the sheet).
int nodeSheetRowAt(const NodeSheetLayout& g, int px, int py);

// --- Trace result --------------------------------------------------------------------------------
// onTraceRecv() hands over the path hashes and one SNR byte per hop; this is the small list the
// screen draws. `name` is the repeater's name when a known repeater matches, otherwise its hex.
struct TraceHop {
  char name[32] = "";
  int8_t snr4 = 0;   // SNR x 4, MeshCore's own unit
  bool known = false;
};

constexpr int kMaxTraceHops = 8;

struct TraceView {
  TraceHop hops[kMaxTraceHops];
  int count = 0;
  int8_t finalSnr4 = 0;   // the trace's own SNR at our receiver
  uint32_t rttMs = 0;     // send -> trace data back
  bool haveRtt = false;
};

// "OM-RPTR  SNR 7.2" into out. Returns the number of bytes written.
int traceRowText(char* out, size_t cap, const TraceHop& hop);

// Favourites first, then the newest advert first. The contact table is sorted with this rule, so
// the star a person set is where they expect it (EST-342).
inline bool nodeComesBefore(bool favA, uint32_t lastA, bool favB, uint32_t lastB) {
  if (favA != favB) return favA;
  return lastA > lastB;
}

}  // namespace ui
}  // namespace om
