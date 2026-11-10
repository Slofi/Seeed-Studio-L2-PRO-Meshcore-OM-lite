// SPDX-License-Identifier: MIT
// EST-362: the RPTR screen. Two things only: the list of repeaters (which scans when the tab opens)
// and one page at a time (the repeater page, one setting, the regions, the neighbours, the console).
// A confirmation is a pop-up over the page. The rows come from rptr_page.h; this file owns pixels and
// rectangles only.
#pragma once

#include <LovyanGFX.hpp>

#include "rptr_model.h"
#include "rptr_page.h"
#include "ui/chat_layout.h"
#include "ui/layout.h"
#include "ui/rptr_layout.h"
#include "ui/widgets.h"

namespace om {
namespace ui {

constexpr int kRptrMaxItems = 20;    // remembered repeaters plus the scan's own cap

struct RptrItem {
  char name[32] = "";
  char meta[64] = "";      // "good 11 dB · 14:02"
  bool saved = false;      // a remembered password: the small key icon
  uint8_t foundIndex = 255;
};

struct RptrView {
  StatusModel status;
  bool list = true;                 // the list is showing; otherwise `page`
  rptr::Page page = rptr::Page::List;

  // --- the list
  int count = 0;
  RptrItem items[kRptrMaxItems];
  int scroll = 0;
  bool scanning = false;
  char state[40] = "";              // "listening, 2 found" / "3 found" / "none answered"

  // --- one page
  rptr::ScreenOut screen;
  int pageScroll = 0;
  char result[80] = "";             // "Applied" or the repeater's error, in plain words
  bool resultOk = false;
  bool resultPending = false;

  // --- the confirmation pop-up
  bool modal = false;
  char modalText[128] = "";
  char modalVerb[12] = "";
  int modalWhat = 0;                // the shell's own id for what the pop-up will do

  // --- the keyboard sheet (name, position, region name, console line, password)
  bool osk = false;
  bool shift = false;
  bool nums = false;
  uint8_t field = 0;                // KeyboardField as a byte
  char editTitle[32] = "";
  const char* okLabel = "OK";
  char text[64] = "";               // the sheet's buffer (never the password)
  int textLen = 0;
  bool secret = false;              // draw dots instead of the text
  uint8_t accentRow = 255;
  uint8_t accentCol = 255;
  uint32_t accentCp = 0;

  // EST-363: clear IN PLACE. `v = RptrView()` built a 6,204-byte temporary on the loop task's stack
  // on every redraw of the RPTR tab, which is half of what panicked the L2 Pro when Log in was
  // tapped (EST-362, exc cause 65). One static instance (g_rptrViewStore) is filled field by field.
  void reset();
};

void rptrScreenDraw(const RptrView& v);

// The floor the screen must not put a tap on: true while the sheet covers the tab bar.
bool rptrSheetOpen(const RptrView& v);

// One key of the open sheet, resolved for the shell (the Settings editor's pattern).
struct RptrKeyHit {
  bool pressed = false;
  KeyKind kind = KeyKind::Char;
  int row = -1;
  int col = -1;
  char ch = 0;
};
RptrKeyHit rptrEditorKeyAt(const RptrView& v, int x, int y);
KeyboardField rptrEditorField(const RptrView& v);

}  // namespace ui
}  // namespace om
