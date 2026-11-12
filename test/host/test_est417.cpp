// SPDX-License-Identifier: MIT
// EST-417, the C++ half: the CardKB's arrows and the unplugged keyboard, as rules.
//  * every measured arrow byte (plain 0xB4/0xB7/0xB5/0xB6, the one-shot Fn pair 0x98/0xA5/0x99/0xA4)
//    becomes its Arrow, and nothing else does;
//  * the key-to-action table: every arrow in all four contexts (list screen, map, text field, sheet);
//  * the tab step never wraps at either end;
//  * one key press scrolls exactly one row of each list, clamped by the same rule a release uses;
//  * the map's key pan is a quarter of the canvas;
//  * the lost-keyboard limit: N-1 failures keep it, the Nth loses it;
//  * the gate: switch off never reads, found alone is not enough either.
// src/keynav_logic.h and src/cardkb_logic.h are plain C++: no Arduino, no Wire, no display.
#include <stdio.h>

#include "cardkb_logic.h"
#include "keynav_logic.h"
#include "ui/map_chrome.h"
#include "ui/node_info_layout.h"
#include "ui/rptr_layout.h"

using namespace om::keynav;
using om::cardkb::Arrow;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// ---------------------------------------------------------------------------------- the arrow bytes
static void testArrowBytes() {
  // The measured table: plain and Fn, one byte each.
  CHECK(om::cardkb::arrowFor(0xB4) == Arrow::Left);
  CHECK(om::cardkb::arrowFor(0xB7) == Arrow::Right);
  CHECK(om::cardkb::arrowFor(0xB5) == Arrow::Up);
  CHECK(om::cardkb::arrowFor(0xB6) == Arrow::Down);
  CHECK(om::cardkb::arrowFor(0x98) == Arrow::FnLeft);
  CHECK(om::cardkb::arrowFor(0xA5) == Arrow::FnRight);
  CHECK(om::cardkb::arrowFor(0x99) == Arrow::FnUp);
  CHECK(om::cardkb::arrowFor(0xA4) == Arrow::FnDown);
  // The named constants and the table are the same bytes.
  CHECK(om::cardkb::kCodeLeft == 0xB4 && om::cardkb::kCodeRight == 0xB7);
  CHECK(om::cardkb::kCodeUp == 0xB5 && om::cardkb::kCodeDown == 0xB6);
  CHECK(om::cardkb::kCodeFnLeft == 0x98 && om::cardkb::kCodeFnRight == 0xA5);
  CHECK(om::cardkb::kCodeFnUp == 0x99 && om::cardkb::kCodeFnDown == 0xA4);
  // Where the plain arrows sit in M5Stack's table, and bytes that only LOOK close: not arrows.
  for (int c = 0; c <= 0xFF; ++c) {
    const bool isArrow = (c == 0xB4 || c == 0xB7 || c == 0xB5 || c == 0xB6 || c == 0x98 ||
                          c == 0xA5 || c == 0x99 || c == 0xA4);
    CHECK((om::cardkb::arrowFor((uint8_t)c) != Arrow::None) == isArrow);
  }
  CHECK(om::cardkb::arrowFor(0x00) == Arrow::None);   // nothing pressed
  CHECK(om::cardkb::arrowFor('a') == Arrow::None);    // a letter
  CHECK(om::cardkb::arrowFor(0xB3) == Arrow::None);   // next to left
  CHECK(om::cardkb::arrowFor(0xB8) == Arrow::None);   // next to right
  CHECK(om::cardkb::arrowFor(0x97) == Arrow::None);   // next to the Fn pair
  CHECK(om::cardkb::arrowFor(0xA6) == Arrow::None);
}

// ------------------------------------------------------------------------------- the action table
struct Row {
  uint8_t code;
  Act list;
  Act map;
  Act text;
  Act sheet;
};

static void testActionTable() {
  // The ticket's table, one row per measured byte, one column per context. Written out rather than
  // derived, so a wrong entry in keynav_logic.h fails here instead of agreeing with itself.
  const Row rows[] = {
      // code, list,            map,            text field,      sheet
      {0xB4, Act::TabPrev,    Act::PanLeft,   Act::None,       Act::None},
      {0xB7, Act::TabNext,    Act::PanRight,  Act::None,       Act::None},
      {0xB5, Act::ScrollUp,   Act::PanUp,     Act::ScrollUp,   Act::None},
      {0xB6, Act::ScrollDown, Act::PanDown,   Act::ScrollDown, Act::None},
      {0x98, Act::TabPrev,    Act::TabPrev,   Act::None,       Act::None},   // Fn+left, every screen
      {0xA5, Act::TabNext,    Act::TabNext,   Act::None,       Act::None},   // Fn+right, every screen
      {0x99, Act::None,       Act::ZoomIn,    Act::None,       Act::None},   // Fn+up: zoom on the Map only
      {0xA4, Act::None,       Act::ZoomOut,   Act::None,       Act::None},   // Fn+down: as above
  };
  for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
    const Arrow a = om::cardkb::arrowFor(rows[i].code);
    CHECK(a != Arrow::None);
    CHECK(actFor(a, Context::List) == rows[i].list);
    CHECK(actFor(a, Context::Map) == rows[i].map);
    CHECK(actFor(a, Context::TextField) == rows[i].text);
    CHECK(actFor(a, Context::Sheet) == rows[i].sheet);
  }
  // No arrow is ever nothing on both list screens and the Map, and every arrow does nothing in a
  // sheet (item 6) and nothing but up/down in a text field (item 5).
  for (int c = 0; c <= 0xFF; ++c) {
    const Arrow a = om::cardkb::arrowFor((uint8_t)c);
    if (a == Arrow::None) continue;
    CHECK(actFor(a, Context::Sheet) == Act::None);
    const Act t = actFor(a, Context::TextField);
    CHECK(t == Act::None || t == Act::ScrollUp || t == Act::ScrollDown);
    if (t == Act::ScrollUp) CHECK(a == Arrow::Up);
    if (t == Act::ScrollDown) CHECK(a == Arrow::Down);
  }
  CHECK(actFor(Arrow::None, Context::List) == Act::None);
  CHECK(actFor(Arrow::None, Context::Map) == Act::None);
  // The zoom mapping is one switch: the constant is on in this build, and dropping it must leave the
  // two Fn keys doing nothing on the Map (armed separately by compiling with it false).
  CHECK(kFnZoomOnMap);
  CHECK(actFor(Arrow::FnUp, Context::Map) == Act::ZoomIn);
  CHECK(actFor(Arrow::FnDown, Context::Map) == Act::ZoomOut);
}

// ----------------------------------------------------------------------------------- no wrap
static void testTabStep() {
  const int n = 4;   // the four main tabs
  CHECK(tabStep(0, -1, n) == -1);   // Nodes is the left end: a stop, not a carousel
  CHECK(tabStep(3, 1, n) == -1);    // RPTR is the right end
  CHECK(tabStep(0, 1, n) == 1);
  CHECK(tabStep(1, -1, n) == 0);
  CHECK(tabStep(1, 1, n) == 2);
  CHECK(tabStep(2, 1, n) == 3);
  CHECK(tabStep(2, -1, n) == 1);
  // Settings' own five tabs behave the same way.
  const int s = 5;
  CHECK(tabStep(0, -1, s) == -1);
  CHECK(tabStep(4, 1, s) == -1);
  CHECK(tabStep(4, -1, s) == 3);
  CHECK(tabStep(0, 1, s) == 1);
  // Rubbish in, nothing out.
  CHECK(tabStep(0, 0, n) == -1);
  CHECK(tabStep(0, 1, 0) == -1);
  CHECK(tabStep(-1, 1, n) == 0);   // a step FROM nowhere still lands on the first tab
  CHECK(tabStep(n, -1, n) == n - 1);
}

// ----------------------------------------------------------------------------------- one row
static void testKeyScroll() {
  // Each list's own row height, one press each way: exactly that many pixels, and NEVER the 10 px of
  // give a finger gets (a key has no fingertip).
  CHECK(om::ui::kRowH == 30);
  CHECK(om::ui::kSelRowH == 30);
  CHECK(om::ui::kNodeInfoRowH == 20);
  CHECK(om::ui::kRptrRowH == 36);
  CHECK(om::ui::kRptrPageRowH == 30);
  CHECK(keyScroll(0, 1, om::ui::kRowH, 200) == 30);
  CHECK(keyScroll(30, 1, om::ui::kRowH, 200) == 60);
  CHECK(keyScroll(60, -1, om::ui::kRowH, 200) == 30);
  CHECK(keyScroll(0, 1, om::ui::kNodeInfoRowH, 200) == 20);
  CHECK(keyScroll(0, 1, om::ui::kRptrRowH, 200) == 36);
  CHECK(keyScroll(0, 1, om::ui::kRptrPageRowH, 200) == 30);
  CHECK(keyScroll(0, 1, om::ui::kSelRowH, 200) == 30);
  // Clamped by listSnapScroll: dead stops at both ends, no stretch past either.
  CHECK(keyScroll(200, 1, om::ui::kRowH, 200) == 200);
  CHECK(keyScroll(0, -1, om::ui::kRowH, 200) == 0);
  CHECK(keyScroll(190, 1, om::ui::kRowH, 200) == 200);   // the last press lands exactly on the end
  CHECK(keyScroll(10, -1, om::ui::kRowH, 200) == 0);
  CHECK(keyScroll(0, 1, om::ui::kRowH, 0) == 0);         // content fits: nothing moves
  CHECK(keyScroll(0, -1, om::ui::kRowH, 0) == 0);
  CHECK(keyScroll(40, 0, om::ui::kRowH, 200) == 40);     // no direction, no move
}

// ----------------------------------------------------------------------------------- the map step
static void testMapStep() {
  // One press pans a quarter of the canvas, expressed in the canvas it moves.
  CHECK(om::ui::kMapKeyPanX == om::ui::kMapCanvasW / 4);
  CHECK(om::ui::kMapKeyPanY == om::ui::kMapCanvasH / 4);
  CHECK(om::ui::kMapKeyPanX == 80);
  CHECK(om::ui::kMapKeyPanY == 53);
  CHECK(om::ui::kMapKeyPanX > 0 && om::ui::kMapKeyPanY > 0);
}

// ------------------------------------------------------------------------ the unplugged keyboard
static void testLostKeyboard() {
  CHECK(om::cardkb::kFailLimit == 10);   // N, about 10
  // At the driver's ~30 ms cadence N failures are inside the ticket's 0.3-0.4 s.
  const int ms = om::cardkb::kFailLimit * 30;
  CHECK(ms >= 300 && ms <= 400);
  // N-1 failures keep the keyboard, the Nth loses it, and it stays lost after that.
  for (int n = 1; n < om::cardkb::kFailLimit; ++n) {
    CHECK(!om::cardkb::lostAfterFailure(n));
  }
  CHECK(om::cardkb::lostAfterFailure(om::cardkb::kFailLimit));
  CHECK(om::cardkb::lostAfterFailure(om::cardkb::kFailLimit + 1));
  CHECK(!om::cardkb::lostAfterFailure(0));
  CHECK(!om::cardkb::lostAfterFailure(-1));
}

// ---------------------------------------------------------------------------------------- the gate
static void testGate() {
  // EST-417: switch on AND found, and nothing else. The text field no longer gates a read.
  CHECK(om::cardkb::shouldPoll(true, true));
  CHECK(!om::cardkb::shouldPoll(true, false));
  CHECK(!om::cardkb::shouldPoll(false, true));    // OFF: no power, no bus, no reads
  CHECK(!om::cardkb::shouldPoll(false, false));
}

int main() {
  testArrowBytes();
  testActionTable();
  testTabStep();
  testKeyScroll();
  testMapStep();
  testLostKeyboard();
  testGate();
  if (g_fail == 0) {
    printf("est417: arrow bytes, the key-to-action table, no-wrap tabs, one-row scroll, map step, "
           "lost keyboard, gate\n");
    return 0;
  }
  printf("est417: %d failure(s)\n", g_fail);
  return 1;
}
