// SPDX-License-Identifier: MIT
// Device-side access to the generated Noto Color Emoji table (src/ui/emoji_data.bin).
// The parser and the blob layout live in emoji_table.h, which test/host also uses.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

// One 13x13 cell, pointing straight into flash. `rgb` is w*h RGB565 words (little endian);
// `alpha` is h * ceil(w/2) bytes of 4-bit coverage, high nibble = left pixel.
struct EmojiGlyph {
  const uint8_t* rgb = nullptr;
  const uint8_t* alpha = nullptr;
  uint8_t w = 0;
  uint8_t h = 0;
};

bool emojiReady();
int emojiTableCount();
// Null for a code point the table does not hold (ASCII, a flag's regional indicator, a
// modifier that the text walker should already have consumed, ...).
const EmojiGlyph* emojiFind(uint32_t cp);

}  // namespace ui
}  // namespace om
