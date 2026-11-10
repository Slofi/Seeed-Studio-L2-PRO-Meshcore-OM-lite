// SPDX-License-Identifier: MIT
// The unread badge's digit face: 5x7 pixels, one bit per pixel, no anti-aliasing at all.
//
// At 13 px a smooth font's coverage map is what made the badge read fuzzy and pink: white bled into
// the red over the whole glyph, and the digit was a 9 px SansTab at a fractional position. A pixel
// face on a solid fill is exactly what reads sharp at this size (Filip: "too fuzzy, make it sharper").
// tools/mkbadge.py parses this table for the 1:1 + 8x preview, so the preview and the device can
// never drift apart.
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host can prove the metrics.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

constexpr int kBadgeGlyphW = 5;
constexpr int kBadgeGlyphH = 7;
constexpr int kBadgeGlyphGap = 1;   // between two glyphs ("10", "99+")

struct BadgeGlyph {
  char ch;
  uint8_t rows[kBadgeGlyphH];   // bit 4 = the leftmost pixel of the row, top row first
};

// 0..9 (a two-digit count needs '0') and the "+" of "99+".
constexpr BadgeGlyph kBadgeFace[] = {
    // EST-364: '0' was missing while counts stopped at nine; a two-digit count needs it.
    {'0', {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}},
    {'1', {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'2', {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}},
    {'3', {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110}},
    {'4', {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}},
    {'5', {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}},
    {'6', {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}},
    {'7', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}},
    {'8', {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}},
    {'9', {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}},
    {'+', {0b00000, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0b00000}},
};
constexpr int kBadgeFaceCount = (int)(sizeof(kBadgeFace) / sizeof(kBadgeFace[0]));

// The rows of one glyph, or nullptr when the face does not carry it.
constexpr const uint8_t* badgeGlyphRows(char ch) {
  for (int i = 0; i < kBadgeFaceCount; ++i) {
    if (kBadgeFace[i].ch == ch) return kBadgeFace[i].rows;
  }
  return nullptr;
}

// A count as the badge writes it (EST-364, Filip: "the two numbers would fit just fine, so please add
// it"): 1..99 exactly, and "99+" above that. The faces are the same 5x7 pixel digits, so two of them
// fit the 19 px pill and three ("99+") the 23 px one (theme.h holds both widths).
constexpr int badgeText(int count, char* out, int cap) {
  if (out == nullptr || cap < 2) return 0;
  if (count < 1) {
    out[0] = 0;
    return 0;
  }
  if (count <= 9) {
    out[0] = (char)('0' + count);
    out[1] = 0;
    return 1;
  }
  if (count <= 99) {
    if (cap < 3) return 0;
    out[0] = (char)('0' + count / 10);
    out[1] = (char)('0' + count % 10);
    out[2] = 0;
    return 2;
  }
  if (cap < 4) return 0;
  out[0] = '9';
  out[1] = '9';
  out[2] = '+';
  out[3] = 0;
  return 3;
}

// Pixels a badge text occupies: one glyph or two plus the gap between them.
constexpr int badgeTextWidth(int chars) {
  if (chars <= 0) return 0;
  return chars * kBadgeGlyphW + (chars - 1) * kBadgeGlyphGap;
}

}  // namespace ui
}  // namespace om
