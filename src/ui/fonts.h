// SPDX-License-Identifier: MIT
// Smooth (anti-aliased) font access for the new UI layer.
//
// The four blobs are VLW fonts: 8-bit coverage per pixel, drawn by LovyanGFX's VLWfont through
// draw_alpha_bitmap_common(), which blends between the foreground and background colour. They are
// compiled into flash as PROGMEM arrays (tools/mkvlw.py), not loaded from SD, so the UI never
// depends on a card being present.
//
// Faces:
//   sans  = DejaVu Sans  (Bitstream Vera licence, permissive)
//   mono  = DejaVu Sans Mono (Bitstream Vera licence, permissive)
// Licence text ships with the system package; the generated files carry the same note.
#pragma once

#include <LovyanGFX.hpp>

#include "theme.h"

namespace om {
namespace ui {

extern const uint8_t kSans11Data[];
extern const uint32_t kSans11DataLen;
extern const uint8_t kSans14Data[];
extern const uint32_t kSans14DataLen;
extern const uint8_t kSans9Data[];
extern const uint32_t kSans9DataLen;
extern const uint8_t kMono10Data[];
extern const uint32_t kMono10DataLen;

// What every blob actually contains (EST-346): ASCII and U+00A0..U+017F, the Latin-1 Supplement and
// Latin Extended-A (all Slovenian, Croatian, German, Italian, Czech and Polish letters, and U+00B7).
// Each face is generated with tools/mkvlw.py --extra for that whole range; test/host/test_fonts.py
// reads the four blobs and fails if a code point is missing, so this constant and the data cannot
// drift. fontHasGlyph() uses it, which is what makes the text path DRAW these letters instead of the
// dim placeholder box.
constexpr uint32_t kFontAsciiFirst = 0x20;
constexpr uint32_t kFontAsciiLast = 0x7E;
constexpr uint32_t kFontLatinFirst = 0xA0;
constexpr uint32_t kFontLatinLast = 0x17F;

constexpr bool fontCoversCodePoint(uint32_t cp) {
  return (cp >= kFontAsciiFirst && cp <= kFontAsciiLast) ||
         (cp >= kFontLatinFirst && cp <= kFontLatinLast);
}

// Load the VLW tables once, after setup() has the heap up. Idempotent.
void loadFonts();
bool fontsReady();

// The face for a role, or nullptr if loading failed (widgets fall back to the built-in font).
const lgfx::IFont* fontFor(FontRole role);

}  // namespace ui
}  // namespace om
