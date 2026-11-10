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

// --- UTF-8 -------------------------------------------------------------------------------------
// MeshCore messages are UTF-8 and often carry characters the faces cannot draw (a bot's emoji). A
// byte walk that splits a sequence, or that gives a whole sequence the font's missing-glyph box,
// is what made a bright caret-like bar appear in the middle of a reply. These helpers are plain C++
// so the host tests and the device use the same decode.
constexpr uint32_t kUtf8Invalid = 0xFFFDu;   // one malformed byte decodes as the replacement char

// Bytes of the sequence a lead byte starts: 1..4, or 1 for an invalid lead (or a continuation).
int utf8SeqLen(uint8_t lead);
// Decode the sequence at s[i]; writes the code point, advances *i by its length (1 on bad input) and
// returns the code point (kUtf8Invalid for a malformed or truncated sequence).
uint32_t utf8Decode(const char* s, int* i);
// Write cp as UTF-8 into out (at least 5 bytes); returns the byte count (0 for a bad code point).
int utf8Encode(uint32_t cp, char* out);

// Width of one drawable code point, or 0 when the face has no glyph for it.
using GlyphWidthFn = int (*)(void* ctx, uint32_t cp);

// Width of a byte range walked code point by code point. A code point the face lacks measures as a
// space plus kMissingExtra, the same width as the dim placeholder box the screen draws for it, so the
// wrap and the drawing agree. Never splits a sequence.
int measureUtf8(const char* text, int len, GlyphWidthFn glyph, void* ctx, int spaceW);

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
