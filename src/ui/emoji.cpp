// SPDX-License-Identifier: MIT
#include "ui/emoji.h"

#include "ui/emoji_table.h"

// PlatformIO's board_build.embed_files turns src/ui/emoji_data.bin into these two symbols.
// They point into the flash-mapped application image, so no RAM copy is made.
extern "C" const uint8_t _binary_src_ui_emoji_data_bin_start[] asm(
    "_binary_src_ui_emoji_data_bin_start");
extern "C" const uint8_t _binary_src_ui_emoji_data_bin_end[] asm(
    "_binary_src_ui_emoji_data_bin_end");

namespace om {
namespace ui {
namespace {

EmojiBlob s_blob;
bool s_parsed = false;

// Parsed once, on the first lookup. A blob that fails validation stays "not ok": every
// emoji then takes the dim placeholder path, the same as a character the face lacks.
const EmojiBlob& blob() {
  if (!s_parsed) {
    s_parsed = true;
    const uint32_t size =
        (uint32_t)(_binary_src_ui_emoji_data_bin_end - _binary_src_ui_emoji_data_bin_start);
    s_blob = emojiParseBlob(_binary_src_ui_emoji_data_bin_start, size);
  }
  return s_blob;
}

}  // namespace

bool emojiReady() { return blob().ok; }

int emojiTableCount() { return blob().ok ? (int)blob().count : 0; }

const EmojiGlyph* emojiFind(uint32_t cp) {
  static EmojiGlyph glyph;
  const EmojiBlob& b = blob();
  EmojiIndexEntry e;
  if (!emojiFindIn(b, cp, &e)) return nullptr;
  glyph.rgb = b.data + e.offset;
  glyph.alpha = glyph.rgb + (size_t)b.w * b.h * 2;
  glyph.w = (uint8_t)b.w;
  glyph.h = (uint8_t)b.h;
  return &glyph;
}

}  // namespace ui
}  // namespace om
