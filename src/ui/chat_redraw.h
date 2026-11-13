// SPDX-License-Identifier: MIT
// S499: which redraw a keyboard key needs on the chat screen.
//
// Filip (2026-11-13): "is it possible to support quicker typing? Now I have to wait a bit before
// hitting the next letter" (one finger). Every key was followed by a FULL drawChat(): the status bar,
// the header, every message bubble and the sheet, composed from scratch, and the touch panel is barely
// read while that draws, so the next tap came too early. A letter changes only the sheet, and the
// off-screen frame still holds the rest of the last full picture, so the sheet alone is redrawn over
// it.
//
// Safe only when the sheet's rectangles cannot move: chatLayoutFor() takes osk, reply, nums and
// selector, never the draft text, so a Char, Space, Del or Shift leaves every rectangle where it was.
// Mode (the 123/abc key) changes `nums` and Emoji changes the page: both take the full redraw, and so
// does any overlay above the sheet (the selector, the ADVERT menu, the message sheet), because the
// sheet would be drawn over it. A toast is safe: its expiry calls redrawCurrentScreen().
//
// Plain C++ so test/host/test_chat_redraw.cpp proves the table.
#pragma once

#include "chat_layout.h"

namespace om {
namespace ui {

enum class ChatRedraw : uint8_t { Full, SheetOnly };

constexpr ChatRedraw chatRedrawForKey(KeyKind kind, bool sheetOpen, bool selector, bool advertMenu,
                                      bool messageSheet) {
  if (!sheetOpen || selector || advertMenu || messageSheet) return ChatRedraw::Full;
  switch (kind) {
    case KeyKind::Char:
    case KeyKind::Space:
    case KeyKind::Del:
    case KeyKind::Shift:
      return ChatRedraw::SheetOnly;
    default:
      return ChatRedraw::Full;   // Mode, Emoji, Send, Reveal: the layout or the screen changes
  }
}

}  // namespace ui
}  // namespace om
