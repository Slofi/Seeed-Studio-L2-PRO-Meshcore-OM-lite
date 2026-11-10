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

// Load the VLW tables once, after setup() has the heap up. Idempotent.
void loadFonts();
bool fontsReady();

// The face for a role, or nullptr if loading failed (widgets fall back to the built-in font).
const lgfx::IFont* fontFor(FontRole role);

}  // namespace ui
}  // namespace om
