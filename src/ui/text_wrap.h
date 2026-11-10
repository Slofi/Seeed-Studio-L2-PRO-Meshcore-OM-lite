// SPDX-License-Identifier: MIT
// Greedy word wrap for the chat message list. Plain C++ (no Arduino, no LovyanGFX): the width of a
// byte range comes from a callback, so test/host can wrap with a fixed advance and the device wraps
// with the real VLW metrics. Every line is a byte range of the source, so the draw can render the
// exact bytes and the hit test can use the same function.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

// Measure a byte range of `text` (not necessarily NUL-terminated) in pixels.
using MeasureFn = int (*)(void* ctx, const char* text, int len);

struct WrapLine {
  int16_t start = 0;   // byte offset of the line's first byte in the source
  int16_t len = 0;     // bytes on the line (0 for an empty source)
};

constexpr int kMaxWrapLines = 24;

// Words are separated by spaces; the spaces between words are not part of a line and runs of spaces
// collapse to the one line break they cause. A word that cannot fit on a line of its own is split
// between characters (a long URL or a MeshCore key still shows completely). `firstW` is the room left
// on the first line (after the time and the name), `nextW` the room on every later line. Returns the
// number of lines written, at least 1 for any non-null text.
int wrapText(const char* text, MeasureFn measure, void* ctx, int firstW, int nextW,
             WrapLine* lines, int maxLines);

// Byte offset just past the last byte that was wrapped. Less than strlen(text) when maxLines cut it.
int wrapConsumed(const char* text, const WrapLine* lines, int count);

}  // namespace ui
}  // namespace om
