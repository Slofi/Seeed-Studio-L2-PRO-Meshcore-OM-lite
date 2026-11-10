// SPDX-License-Identifier: MIT
// OM Lite UI widgets. Every function draws into the surface it is given; the Nodes screen passes
// om_display() while a frame is open, so nothing here can bypass the off-screen frame.
#pragma once

#include <LovyanGFX.hpp>

#include "layout.h"
#include "theme.h"

namespace om {
namespace ui {

constexpr int kNoRssi = -127;

enum class NodeType : uint8_t { Client, Repeater, Room, Sensor, Unknown };

struct StatusModel {
  bool meshReady = false;
  const char* protocol = "MC";
  const char* region = "EU868";
  const char* hash = "2B";
  int nodes = 0;
  bool gpsValid = false;
  int gpsSats = 0;
  int batteryPct = -1;      // -1 = unknown
  const char* clock = "--:--";
};

struct RowModel {
  const char* name = "";
  NodeType type = NodeType::Unknown;
  int rssi = kNoRssi;       // dBm, kNoRssi = not known (no bar is drawn)
  const char* note = "";    // optional mono text before the age, e.g. "direct" or "path 4B"
  const char* age = "";     // mono, right aligned
  bool selected = false;
};

// Status bar and tab bar.
void drawStatusBar(lgfx::LovyanGFX* d, const Layout& L, const StatusModel& m);
void drawTabBar(lgfx::LovyanGFX* d, const Layout& L, Tab active, int unread);

// One list row (type dot, name in sans, optional note, signal bars, age in mono, bottom line).
void drawListRow(lgfx::LovyanGFX* d, const Rect& r, const RowModel& row);

// Centred two-line empty state (sans title, dim text).
void drawEmptyState(lgfx::LovyanGFX* d, const Rect& area, const char* title, const char* sub);

// Exposed for tests / later screens; they use the same colours as the widgets.
uint16_t typeColour(NodeType t);
int textWidth(lgfx::LovyanGFX* d, FontRole role, const char* text);

// --- text helpers -----------------------------------------------------------------------------
// The chat screen needs the same three primitives the widgets use (a role font, a colour pair, and a
// measure that works on a byte range for the wrapper). Public so no screen grows a second copy.
void setRole(lgfx::LovyanGFX* d, FontRole role);
void drawTextLeft(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int x, int y,
                  const char* s);
int drawTextRight(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int right, int y,
                  const char* s);
void drawTextEllipsis(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int x, int y,
                      int maxW, const char* s);
// Width of the first `len` bytes of s (copied into a small buffer: the VLW face measures a C string).
int measureText(lgfx::LovyanGFX* d, FontRole role, const char* s, int len);

}  // namespace ui
}  // namespace om
