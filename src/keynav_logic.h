// SPDX-License-Identifier: MIT
// EST-417: what the CardKB's arrows DO. cardkb_logic.h says what byte is what key; this header says
// what an arrow means on the screen that is up right now, plus the two bits of arithmetic the shell
// needs: one step along a tab row (never wrapping) and one row of scroll, clamped by the SAME rule a
// finger's release uses. Plain C++ (no Arduino, no Wire, no display), so test/host proves the whole
// table with the system g++.
#pragma once

#include <stdint.h>

#include "cardkb_logic.h"
#include "ui/layout.h"

namespace om {
namespace keynav {

// The four contexts the ticket names. Exactly one wins, in this order: a text field (its composer
// still scrolls the chat behind it), then a sheet or pop-up (the arrows do nothing there), then the
// Map (plain arrows pan), then a list screen (plain arrows switch tabs and scroll the list).
enum class Context : uint8_t { List = 0, Map, TextField, Sheet };

enum class Act : uint8_t {
  None = 0,
  TabPrev,     // one tab to the left, never wrapping
  TabNext,     // one tab to the right, never wrapping
  ScrollUp,    // one row towards the top of the current list
  ScrollDown,  // one row towards its end
  PanLeft,     // the Map: the view moves one fixed step west
  PanRight,
  PanUp,       // north
  PanDown,
  ZoomIn,      // Fn+Up on the Map (guarded by kFnZoomOnMap)
  ZoomOut,     // Fn+Down on the Map
};

// EST-417 rule 4 (Claude's suggestion, Filip may drop it: "keep it a separate, easily removed
// mapping"). Fn+Up / Fn+Down zoom the Map; this is one constant and the two table entries below. Set
// it false and those two keys do nothing, nothing else changes.
constexpr bool kFnZoomOnMap = true;

// One arrow, one context, one action. Every arrow byte in cardkb_logic.h has a row here, for every
// context, so "what does this key do here" is one table and not four if-chains in the shell.
constexpr Act actFor(cardkb::Arrow a, Context c) {
  if (a == cardkb::Arrow::None) return Act::None;
  // A sheet or pop-up owns the keys: the arrows do nothing, Esc closes it exactly as before.
  if (c == Context::Sheet) return Act::None;
  if (c == Context::TextField) {
    // Item 5: only up/down act, and only to scroll the chat behind the composer; left/right and
    // both Fn pairs do nothing.
    if (a == cardkb::Arrow::Up) return Act::ScrollUp;
    if (a == cardkb::Arrow::Down) return Act::ScrollDown;
    return Act::None;
  }
  switch (a) {
    // On the Map the four plain arrows pan; everywhere else left/right are the tab step.
    case cardkb::Arrow::Left:    return c == Context::Map ? Act::PanLeft : Act::TabPrev;
    case cardkb::Arrow::Right:   return c == Context::Map ? Act::PanRight : Act::TabNext;
    case cardkb::Arrow::Up:      return c == Context::Map ? Act::PanUp : Act::ScrollUp;
    case cardkb::Arrow::Down:    return c == Context::Map ? Act::PanDown : Act::ScrollDown;
    // Fn+left / Fn+right are the one tab rule that works on EVERY screen (Map included).
    case cardkb::Arrow::FnLeft:  return Act::TabPrev;
    case cardkb::Arrow::FnRight: return Act::TabNext;
    case cardkb::Arrow::FnUp:
      return (c == Context::Map && kFnZoomOnMap) ? Act::ZoomIn : Act::None;
    case cardkb::Arrow::FnDown:
      return (c == Context::Map && kFnZoomOnMap) ? Act::ZoomOut : Act::None;
    default:                     return Act::None;
  }
}

// One step along a tab row, never wrapping: the answer is -1 when the step would leave the row, and
// the caller then does nothing at all (the ends of Nodes and RPTR are stops, not a carousel).
constexpr int tabStep(int current, int delta, int count) {
  if (count <= 0 || delta == 0) return -1;
  const int next = current + delta;
  if (next < 0 || next >= count) return -1;
  return next;
}

// One key press moves the scroll by exactly one row (`rowPx`, the target's own row height), then the
// SAME clamp a finger's release applies (ui/layout.h listSnapScroll). A key has no fingertip, so
// there is no give and no stretch: the list stops dead at either end, where a drag would give 10 px.
constexpr int keyScroll(int base, int dir, int rowPx, int maxScroll) {
  return ui::listSnapScroll(base + dir * rowPx, maxScroll);
}

}  // namespace keynav
}  // namespace om
