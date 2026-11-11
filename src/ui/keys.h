// SPDX-License-Identifier: MIT
// EST-400: the small controls two screens used to keep private copies of. ui/screen_rptr.cpp and
// ui/map_pick.cpp drew the same small key, the same back key and the same strong action key byte for
// byte (map_pick even carried the comment "kept local so this file does not need the RPTR screen's
// private drawing helpers": EST-384 moved the picker here to avoid exactly that drift, and the
// helpers came along as a second copy). They now live here, once. The editor's key hit test is the
// same story: settingsEditorKeyAt() and rptrEditorKeyAt() were fifteen identical lines.
#pragma once

#include <LovyanGFX.hpp>

#include "ui/chat_layout.h"
#include "ui/layout.h"

namespace om {
namespace ui {

// A small mono key: the page rows' - / + / one-glyph keys and the zoom + / -. `strong` is the gold
// selected form (kSelBg2 fill, gold edge), the resting form is kPanel2 with a kLine edge.
void drawSmallKey(lgfx::LovyanGFX* d, const Rect& r, const char* text, bool strong);

// The "< back" / "< list" key over a page or the map: a kPanel2 plate with the label 8 px in.
void drawSheetBackKey(lgfx::LovyanGFX* d, const Rect& r, const char* label);

// A page's one action key (Apply, Send, Log in, Cancel, Confirm). `danger` is the red form the RPTR
// confirmation uses; the map picker's Apply is the same key without it.
void drawActionKey(lgfx::LovyanGFX* d, const Rect& r, const char* label, bool danger);

// One key of the shared keyboard sheet, resolved to what the shell does with it. `ch` is the
// character for KeyKind::Char with shift already applied; row/col locate the drawn key so the accent
// bubble and the held-key look can find it.
struct EditorKeyHit {
  bool pressed = false;
  KeyKind kind = KeyKind::Char;
  char ch = 0;
  int row = -1;
  int col = -1;
};

// The key under (x, y) on the sheet `g` for this field. pressed=false when the point is not on a key
// (above the sheet, in a gap, or past a row's last key). Callers pass the SAME ChatLayout they drew.
EditorKeyHit editorKeyAt(const ChatLayout& g, KeyboardField field, bool nums, bool shift, int x,
                         int y);

}  // namespace ui
}  // namespace om
