// SPDX-License-Identifier: MIT
// EST-412, the C++ half: the CardKB's rules, not its bus or its drawing.
//  * every code class maps to the ticket's action: printable ASCII -> Char with the same byte,
//    0x08 Backspace, 0x0D Enter, 0x1B Esc, 0xB4/0xB7 left/right only with a movable cursor;
//  * unknown codes are Ignored (counted by the shell, never inserted), and 0x00 is not a key at all;
//  * the switch is OFF by default, including when the NVS key is missing;
//  * polling needs switch on AND found (EST-417 dropped the text-field term: the arrows act on every
//    screen, so the keys are read on every screen).
// src/cardkb_logic.h is plain C++: no Arduino, no Wire, no display.
#include <stdio.h>

#include "cardkb_logic.h"

using namespace om::cardkb;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static void checkCode(uint8_t code, Action want, char wantCh = 0, bool hasCursor = false) {
  const Key k = mapCode(code, hasCursor);
  CHECK(k.action == want);
  if (want == Action::Char) CHECK(k.ch == wantCh);
  else CHECK(k.ch == 0);
}

// ----------------------------------------------------------------------------------- every code class
static void testMapping() {
  // 0x00 is "nothing is pressed", not a key: not Char and not counted as ignored.
  checkCode(kCodeNone, Action::None);

  // Printable ASCII 0x20..0x7E, both ends included, each byte carried through unchanged.
  checkCode(0x20, Action::Char, ' ');
  checkCode(0x21, Action::Char, '!');
  checkCode('0', Action::Char, '0');
  checkCode('A', Action::Char, 'A');
  checkCode('a', Action::Char, 'a');
  checkCode('z', Action::Char, 'z');
  checkCode('~', Action::Char, '~');
  for (int c = 0x20; c <= 0x7E; ++c) {
    const Key k = mapCode((uint8_t)c, false);
    CHECK(k.action == Action::Char);
    CHECK(k.ch == (char)c);
  }

  // The four named control keys.
  checkCode(kCodeBackspace, Action::Backspace);
  checkCode(kCodeEnter, Action::Enter);
  checkCode(kCodeEsc, Action::Esc);

  // Arrows: left/right exist only for an editor with a cursor; up/down never map to a move.
  checkCode(kCodeLeft, Action::Left, 0, true);
  checkCode(kCodeRight, Action::Right, 0, true);
  checkCode(kCodeLeft, Action::Ignored, 0, false);
  checkCode(kCodeRight, Action::Ignored, 0, false);
  checkCode(kCodeUp, Action::Ignored, 0, true);
  checkCode(kCodeDown, Action::Ignored, 0, true);
  checkCode(kCodeUp, Action::Ignored, 0, false);
  checkCode(kCodeDown, Action::Ignored, 0, false);

  // Everything else is ignored, never a character: C0 controls, DEL, the CardKB's own Delete/Home/
  // End/PageUp/PageDown bytes, high bytes and 0xFF.
  const uint8_t unknown[] = {0x01, 0x07, 0x09, 0x0A, 0x0B, 0x0C, 0x0E, 0x1F,
                             0x7F, 0x80, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xFE, 0xFF};
  for (unsigned i = 0; i < sizeof(unknown); ++i) {
    const Key k = mapCode(unknown[i], false);
    CHECK(k.action == Action::Ignored);
    CHECK(k.ch == 0);
  }
}

// ------------------------------------------------------------------------------------- default OFF
static void testDefaultOff() {
  CHECK(kDefaultEnabled == false);
  // A missing key is OFF whatever the caller's buffer held; a saved value is honoured.
  CHECK(enabledFromNvs(false, false) == false);
  CHECK(enabledFromNvs(false, true) == false);
  CHECK(enabledFromNvs(true, false) == false);
  CHECK(enabledFromNvs(true, true) == true);
}

// ---------------------------------------------------------------------------------- polling gate
static void testPollGate() {
  // EST-417: the gate is switch on AND found, on any screen. A text field no longer gates the poll
  // (the arrows work everywhere), so only the one true/true combination may read 0x5F.
  for (int enabled = 0; enabled <= 1; ++enabled) {
    for (int found = 0; found <= 1; ++found) {
      const bool want = enabled && found;
      CHECK(shouldPoll(enabled != 0, found != 0) == want);
    }
  }
  CHECK(shouldPoll(true, true));            // any screen, so no third term
  CHECK(!shouldPoll(true, false));          // not found: nothing to read
  CHECK(!shouldPoll(false, true));          // switch off: no heap, no bus, no poll
  CHECK(!shouldPoll(false, false));
}

int main() {
  testMapping();
  testDefaultOff();
  testPollGate();
  if (g_fail == 0) {
    printf("est412: CardKB key mapping, default OFF and the switch + found polling gate\n");
    return 0;
  }
  printf("est412: %d failure(s)\n", g_fail);
  return 1;
}
