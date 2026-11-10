// SPDX-License-Identifier: MIT
// Chat geometry: the chip strip, the message band, the compose line and the keyboard sheet. Plain C++
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
  Rect chips;       // the strip, including its 1 px bottom line
  Rect chipScroll;  // the part the chips scroll in
  Rect chipMenu;    // the fixed button that opens the channel list
  Rect msgs;        // the message band
  Rect compose;     // the compose line (empty while the sheet is open)
  Rect field;
  Rect kbdBtn;
  Rect sendBtn;
  bool osk = false;
  Rect sheet;
  Rect replyBar;    // empty when the draft is not answering a message
  Rect draft;
  Rect keys[4][10];
  uint8_t keyCount[4] = {0, 0, 0, 0};
  Rect infoPanel;
  Rect menuPanel;
  Rect menuHead;
  Rect menuRow[3];
};

ChatLayout chatLayoutFor(const Layout& L, bool osk, bool replying, bool nums);

// --- chip strip ---------------------------------------------------------------------------------
// Width of one chip: its text, an optional unread count, 5 px padding each side and the 1 px border.
int chipWidth(int labelW, int countW);
// Width of n chips including the leading pad and the gaps (no trailing pad).
int chipStripWidth(const int16_t* widths, int n);
int chipMaxScroll(const int16_t* widths, int n, int areaW);
// Left edge of chip i inside the scrolling area at the given scroll. Negative = scrolled off.
int chipLeft(const int16_t* widths, int i, int scroll);
// The chip under px (in the scrolling area), or -1.
int chipAt(const int16_t* widths, int n, int scroll, int px);
// Where the strip must scroll to show chip i completely; clamped to [0, maxScroll].
int chipScrollTo(int cur, const int16_t* widths, int n, int i, int areaW);

// --- message band -------------------------------------------------------------------------------
// heights[] is the drawn height of every message block (text lines, no gaps). Oldest first.
int msgContentHeight(const int16_t* heights, int n);
int msgMaxScroll(const Rect& band, const int16_t* heights, int n);
// Screen y of every message top, bottom-anchored: the newest message sits at the band bottom (minus
// the pad) when scroll is 0, and scroll moves the whole stack up.
void msgSpans(const Rect& band, const int16_t* heights, int n, int scroll, int16_t* top);
// The message under py, or -1. Only the visible part of a block counts, so a tap under the chips row
// or the sheet cannot hit a message hidden behind it.
int msgAt(const Rect& band, const int16_t* heights, int n, int scroll, int py);

// --- keys ---------------------------------------------------------------------------------------
// Split one row into keys by weight so they tile it exactly: the last key takes the rounding
// remainder, so the row's right edge is exact whatever the division leaves over.
int rowKeyRects(const Rect& row, const uint8_t* units, int n, int gap, Rect* out);

// --- the two overlays ---------------------------------------------------------------------------
// Height of the action menu for a given message count (3 rows) and of the INFO panel.
int chatMenuHeight();
int chatInfoHeight();

}  // namespace ui
}  // namespace om
