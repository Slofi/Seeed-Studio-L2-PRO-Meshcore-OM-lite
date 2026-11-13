// SPDX-License-Identifier: MIT
// S499: the redraw table for keyboard keys (ui/chat_redraw.h). A typed letter redraws only the sheet;
// anything that changes the layout, or any overlay above the sheet, takes the full redraw.
#include <stdio.h>

#include "ui/chat_redraw.h"

using namespace om::ui;

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static bool sheetOnly(KeyKind k, bool open = true, bool sel = false, bool adv = false,
                      bool msg = false) {
  return chatRedrawForKey(k, open, sel, adv, msg) == ChatRedraw::SheetOnly;
}

int main() {
  // The typing keys: only the sheet.
  CHECK(sheetOnly(KeyKind::Char));
  CHECK(sheetOnly(KeyKind::Space));
  CHECK(sheetOnly(KeyKind::Del));
  CHECK(sheetOnly(KeyKind::Shift));
  // Keys that change the layout or leave the screen: full.
  CHECK(!sheetOnly(KeyKind::Mode));
  CHECK(!sheetOnly(KeyKind::Emoji));
  CHECK(!sheetOnly(KeyKind::Send));
  CHECK(!sheetOnly(KeyKind::Reveal));
  // A letter while anything sits above the sheet, or with no sheet: full.
  CHECK(!sheetOnly(KeyKind::Char, false));
  CHECK(!sheetOnly(KeyKind::Char, true, true));
  CHECK(!sheetOnly(KeyKind::Char, true, false, true));
  CHECK(!sheetOnly(KeyKind::Char, true, false, false, true));
  if (g_fail != 0) {
    printf("test_chat_redraw: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("test_chat_redraw: all checks passed\n");
  return 0;
}
