// SPDX-License-Identifier: MIT
// Chat geometry: the header, the chat selector, the message band, the compose line, the keyboard
// sheet and the two overlays. Plain C++
// (no LovyanGFX) so test/host can prove the arithmetic with the system g++. One function hands out
// the rectangles, and the same rectangles answer the taps: a screen never repeats a coordinate.
#pragma once

#include <stdint.h>

#include "layout.h"
#include "theme.h"

namespace om {
namespace ui {

enum class KeyKind : uint8_t { Char, Shift, Mode, Space, Del, Send };

struct KeyDef {
  char label[8] = "";
  uint8_t units = kKeyUnit;
  KeyKind kind = KeyKind::Char;
};

// The four key rows of the sheet: 10/9/9/3 keys in letters, 10/10/10/2 in numbers. Labels are the
// device keyboard's own characters (QWERTZ letters as today) with the mock's row structure. Glyphs
// the ASCII-only faces do not carry (the mock's shift arrow and backspace) are drawn as vectors by
// screen_chat.cpp; the label stays empty for Del.
int keyRow(bool nums, int row, KeyDef* out);

struct ChatLayout {
  Rect head;        // the header row: the whole row toggles the selector
  Rect headTitle;   // the room the open chat's name has (chevron and unread dot sit outside it)
  Rect headDot;     // the gold dot counting unread in the OTHER chats (empty while none wait)
  Rect msgs;        // the message band
  Rect compose;     // the compose line (empty while the sheet is open)
  Rect field;       // the draft field: the whole compose row (tapping it opens the keyboard)
  bool osk = false;
  Rect sheet;
  Rect replyBar;    // empty when the draft is not answering a message
  Rect draft;
  Rect keys[4][10];
  uint8_t keyCount[4] = {0, 0, 0, 0};
  bool selector = false;   // the chat dropdown is open
  Rect selPanel;           // the dropdown: the message band, header bottom to compose top
  Rect selList;            // the scrolling part (section labels and chat rows)
  Rect selManage;          // the pinned last row: "Manage channels"
};

ChatLayout chatLayoutFor(const Layout& L, bool osk, bool replying, bool nums, bool selector);

// --- the chat selector arithmetic ---------------------------------------------------------------
// The list holds a "Channels" label, one row per channel, a "Direct messages" label and either one
// row per DM or the "No direct messages yet" line. `entry` counts channels first, then DMs.
int selContentHeight(int channelCount, int dmCount);
int selMaxScroll(const Rect& list, int channelCount, int dmCount);
// Screen y of one entry's row at a scroll (negative or past the list when scrolled out of view).
int selRowTop(const Rect& list, int channelCount, int dmCount, int entry, int scroll);
// The entry under py, or -1 (labels, the empty-DM line, the pads and the gaps answer -1).
int selRowAt(const Rect& list, int channelCount, int dmCount, int scroll, int py);
// The scroll for a drag that began at `base` while the finger has moved `dyUp` px UP the glass
// (screen y decreases, so dyUp is startY - nowY). The list follows the finger: dyUp px up moves the
// rows dyUp px up, and the scroll grows by dyUp. kSelGive px of stretch is allowed past either end;
// the release path snaps it back into [0, maxScroll].
int selDragScroll(int base, int dyUp, int maxScroll);

// --- message band -------------------------------------------------------------------------------
// heights[] is the drawn height of every message block (text lines, no gaps). Oldest first.
int msgContentHeight(const int16_t* heights, int n);
int msgMaxScroll(const Rect& band, const int16_t* heights, int n);
// Screen y of every message top, bottom-anchored: the newest message sits at the band bottom (minus
// the pad) when scroll is 0, and scroll moves the whole stack up.
void msgSpans(const Rect& band, const int16_t* heights, int n, int scroll, int16_t* top);
// The message under py, or -1. Only the visible part of a block counts, so a tap under the chips row
// or the header cannot hit a message hidden behind it.
int msgAt(const Rect& band, const int16_t* heights, int n, int scroll, int py);

// --- keys ---------------------------------------------------------------------------------------
// Split one row into keys by weight so they tile it exactly: the last key takes the rounding
// remainder, so the row's right edge is exact whatever the division leaves over.
int rowKeyRects(const Rect& row, const uint8_t* units, int n, int gap, Rect* out);

}  // namespace ui
}  // namespace om
