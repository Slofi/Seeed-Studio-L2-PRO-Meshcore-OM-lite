// SPDX-License-Identifier: MIT
// The generated Noto Color Emoji table, read without LovyanGFX so test/host can prove it.
//
// Blob (src/ui/emoji_data.bin, made by tools/mkemoji.py):
//   16-byte header  "OMEM", u16 version, u16 cellW, u16 cellH, u16 count, u32 dataOffset
//   count entries   u32 codePoint, u32 dataOffset, sorted by code point (binary search)
//   each cell       w*h*2 B little-endian RGB565, then h * ceil(w/2) B of 4-bit alpha,
//                   high nibble = left pixel; every cell starts 4-byte aligned.
// The licence text is next to the blob (emoji_data.LICENSE.txt, SIL OFL 1.1).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace ui {

constexpr uint32_t kEmojiBlobMagic = 0x4D454D4Fu;   // "OMEM" read little-endian
constexpr uint16_t kEmojiBlobVersion = 1;
constexpr int kEmojiHeaderBytes = 16;
constexpr int kEmojiIndexBytes = 8;

// One index entry. Built field by field from the bytes: the embedded blob has no alignment
// promise, and Xtensa must not be handed an unaligned uint32_t load.
struct EmojiIndexEntry {
  uint32_t cp = 0;
  uint32_t offset = 0;
};

struct EmojiBlob {
  const uint8_t* data = nullptr;
  uint32_t size = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  uint16_t count = 0;
  uint32_t dataOffset = 0;
  bool ok = false;
};

inline uint16_t emojiReadU16(const uint8_t* p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

inline uint32_t emojiReadU32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

// Validate the header and the index range. A truncated or unknown blob is `ok = false`,
// which the device treats as "no emoji table": every emoji then draws the placeholder box.
inline EmojiBlob emojiParseBlob(const uint8_t* data, uint32_t size) {
  EmojiBlob b;
  b.data = data;
  b.size = size;
  if (data == nullptr || size < (uint32_t)kEmojiHeaderBytes) return b;
  if (emojiReadU32(data) != kEmojiBlobMagic) return b;
  if (emojiReadU16(data + 4) != kEmojiBlobVersion) return b;
  b.w = emojiReadU16(data + 6);
  b.h = emojiReadU16(data + 8);
  b.count = emojiReadU16(data + 10);
  b.dataOffset = emojiReadU32(data + 12);
  if (b.w == 0 || b.h == 0 || b.w > 64 || b.h > 64) return b;
  const uint32_t indexEnd = (uint32_t)kEmojiHeaderBytes + (uint32_t)b.count * kEmojiIndexBytes;
  if (indexEnd > size) return b;
  const uint32_t cellBytes = (uint32_t)b.w * b.h * 2 + (uint32_t)b.h * ((b.w + 1) / 2);
  const uint32_t aligned = (cellBytes + 3u) & ~3u;
  if (b.dataOffset < indexEnd || b.dataOffset + (uint32_t)b.count * aligned > size) return b;
  b.ok = true;
  return b;
}

inline bool emojiEntry(const EmojiBlob& b, int i, EmojiIndexEntry* out) {
  if (out == nullptr || !b.ok || i < 0 || i >= (int)b.count) return false;
  const uint8_t* p = b.data + kEmojiHeaderBytes + i * kEmojiIndexBytes;
  out->cp = emojiReadU32(p);
  out->offset = emojiReadU32(p + 4);
  return true;
}

// The entry for a code point, or false. The table holds single code points only: the
// variation selectors and skin tones are consumed by the text walker, flags are boxes.
inline bool emojiFindIn(const EmojiBlob& b, uint32_t cp, EmojiIndexEntry* out) {
  if (!b.ok || out == nullptr) return false;
  int lo = 0;
  int hi = (int)b.count - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const uint8_t* p = b.data + kEmojiHeaderBytes + mid * kEmojiIndexBytes;
    const uint32_t at = emojiReadU32(p);
    if (at == cp) {
      out->cp = at;
      out->offset = emojiReadU32(p + 4);
      return true;
    }
    if (at < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return false;
}

}  // namespace ui
}  // namespace om
