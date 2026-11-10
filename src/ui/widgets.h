// SPDX-License-Identifier: MIT
// OM Lite UI widgets. Every function draws into the surface it is given; the Nodes screen passes
// om_display() while a frame is open, so nothing here can bypass the off-screen frame.
#pragma once

#include <LovyanGFX.hpp>

#include "layout.h"
#include "radio_fault.h"
#include "theme.h"
#include "ui/icons.h"

namespace om {
namespace ui {

constexpr int kNoRssi = -127;

enum class NodeType : uint8_t { Client, Repeater, Room, Sensor, Unknown };

struct StatusModel {
  // The new top bar (EST-337). The left is only the clock; the right is the gear, the battery, the
  // GPS cell and the status icons that are on. "MC", "EU868", "2B", the green dot and the node count
  // are gone: the node count is the Nodes screen's header, the rest belongs in Settings > Radio.
  bool gpsValid = false;
  int gpsHeard = 0;         // satellites HEARD right now (GSV with a C/N0 value, all constellations)
  bool gpsTalked = false;   // the module has sent at least one NMEA sentence
  int batteryPct = -1;      // -1 = unknown
  const char* clock = "--:--";
  bool dnd = false;         // the existing do-not-disturb setting, a moon in the bar
  bool bt = false;          // reserved: Bluetooth settings come in a later ticket
  bool wifi = false;        // reserved: Wi-Fi settings come in a later ticket
  RadioFault fault = RadioFault::None;   // the red mark; None draws nothing
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

// One-line toast above the tab bar (theme.h kToastH / kToastMargin). Used by a tap on the radio fault
// mark to say which of the three things is wrong. The caller keeps the text and the time.
void drawToast(lgfx::LovyanGFX* d, const Layout& L, const char* text);

// The unread badge: one widget for every count that sits on red. `box` comes from countBadgeRect()
// (13 px circle for one digit, 19x13 pill for "9+"); `ring` is the surface colour behind it, drawn
// as a 1 px separator. The digits are the pixel face of ui/badge_font.h, never a smooth font.
void drawCountBadge(lgfx::LovyanGFX* d, const Rect& box, int count, uint16_t ring);

// One list row (type dot, name in sans, optional note, signal bars, age in mono, bottom line).
void drawListRow(lgfx::LovyanGFX* d, const Rect& r, const RowModel& row);

// Centred two-line empty state (sans title, dim text). A null or empty `sub` draws the title alone,
// on the band's centre, which is the chat's "No messages yet" (EST-336).
void drawEmptyState(lgfx::LovyanGFX* d, const Rect& area, const char* title, const char* sub);

// Blend an 8-bit alpha icon (tools/mkicons.py) in fg over bg, pixel by pixel. The caller knows the
// surface colour, so no read from the frame is needed.
void drawIcon(lgfx::LovyanGFX* d, const IconMask& icon, int x, int y, uint16_t fg, uint16_t bg);

// Exposed for tests / later screens; they use the same colours as the widgets.
uint16_t typeColour(NodeType t);
int textWidth(lgfx::LovyanGFX* d, FontRole role, const char* text);

// --- text helpers -----------------------------------------------------------------------------
// The chat screen needs the same three primitives the widgets use (a role font, a colour pair, and a
// measure that works on a byte range for the wrapper). Public so no screen grows a second copy.
//
// All of them decode UTF-8. A code point the face cannot draw is never handed to LovyanGFX (its
// missing-glyph rectangle is line-high and reads as a caret); it measures, and draws, as one small
// dim box a space plus 2 px wide (theme.h kMissingExtra / kMissingBoxH).
void setRole(lgfx::LovyanGFX* d, FontRole role);
void drawTextLeft(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int x, int y,
                  const char* s);
int drawTextRight(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int right, int y,
                  const char* s);
void drawTextEllipsis(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int x, int y,
                      int maxW, const char* s);
// Width of the first `len` bytes of s, code point by code point (a range may start or end anywhere;
// a byte that is not valid UTF-8 counts as one missing glyph).
int measureText(lgfx::LovyanGFX* d, FontRole role, const char* s, int len);

// Whether the face for a role carries a glyph. The four blobs hold ASCII 0x20-0x7E and the mono face
// U+00B7 (theme.h), so anything else is the placeholder box.
bool fontHasGlyph(FontRole role, uint32_t cp);

}  // namespace ui
}  // namespace om
