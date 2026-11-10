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

// --- emoji sequences ---------------------------------------------------------------------------
// A face can draw one code point; an emoji is often more than one. The walker below answers with
// ONE drawable unit at a time and eats the sequence-only code points: a variation selector, a
// skin-tone modifier and U+20E3 belong to their base; a ZWJ belongs to the part BEFORE it, so the
// next call returns the part after it and a joining sequence draws its parts in order. Regional
// indicators are not consumed as a pair: each is its own missing glyph, which is the ticket's
// "a flag pair draws as two regional letters' placeholder".
constexpr uint32_t kUtf8Zwj = 0x200Du;
constexpr uint32_t kUtf8Vs15 = 0xFE0Eu;
constexpr uint32_t kUtf8Vs16 = 0xFE0Fu;
constexpr uint32_t kUtf8Keycap = 0x20E3u;

constexpr bool utf8IsSkinTone(uint32_t cp) { return cp >= 0x1F3FBu && cp <= 0x1F3FFu; }
constexpr bool utf8IsEmojiModifier(uint32_t cp) {
  return cp == kUtf8Vs15 || cp == kUtf8Vs16 || cp == kUtf8Keycap || utf8IsSkinTone(cp);
}
constexpr bool utf8IsRegionalIndicator(uint32_t cp) {
  return cp >= 0x1F1E6u && cp <= 0x1F1FFu;
}

// One drawable unit of a string. `start..coreEnd` is the base code point (what the face draws),
// `start..end` also covers the modifiers and a following ZWJ. `zero` marks a stray modifier with
// no base: it takes no room and draws nothing.
struct TextUnit {
  uint32_t cp = 0;
  int16_t start = 0;
  int16_t coreEnd = 0;
  int16_t end = 0;
  bool zero = false;
};

// Decode the unit at s[i], never reading past `end`. Returns the byte offset of the next unit.
int textUnitAt(const char* s, int end, int i, TextUnit* out);

// Width of one drawable code point, or 0 when the face has no glyph for it.
using GlyphWidthFn = int (*)(void* ctx, uint32_t cp);

// Width of a byte range walked unit by unit (emoji modifiers take no room of their own). A code
// point the face lacks measures as a space plus kMissingExtra, the same width as the dim
// placeholder box the screen draws for it, so the wrap and the drawing agree.
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
