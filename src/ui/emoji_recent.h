// SPDX-License-Identifier: MIT
// The emoji keyboard's "Recent" row: the last 16 picked code points, most recent first.
// Plain C++ and no NVS, so test/host can prove the order and the de-duplication; the shell
// loads and stores this struct in Preferences.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

constexpr int kEmojiRecentMax = 16;

struct EmojiRecent {
  uint32_t cps[kEmojiRecentMax] = {0};
  uint8_t count = 0;

  void clear() {
    count = 0;
    for (int i = 0; i < kEmojiRecentMax; ++i) cps[i] = 0;
  }

  int indexOf(uint32_t cp) const {
    if (cp == 0) return -1;
    for (int i = 0; i < count && i < kEmojiRecentMax; ++i) {
      if (cps[i] == cp) return i;
    }
    return -1;
  }

  // Move an already-used emoji to the front, or add a new one and drop the oldest. A zero code
  // point is a "no entry" padding value and is refused.
  void use(uint32_t cp) {
    if (cp == 0) return;
    int at = indexOf(cp);
    if (at < 0) {
      at = count < kEmojiRecentMax ? count : kEmojiRecentMax - 1;
      if (count < kEmojiRecentMax) ++count;
    }
    for (int i = at; i > 0; --i) cps[i] = cps[i - 1];
    cps[0] = cp;
  }

  // Restore from NVS storage (16 u32 + count). Out-of-range counts and duplicate/zero entries
  // are dropped, so a torn or old blob cannot put a phantom cell in the row.
  void restore(const uint32_t* raw, int rawCount, uint8_t savedCount) {
    clear();
    if (raw == nullptr || rawCount <= 0) return;
    int n = savedCount;
    if (n > kEmojiRecentMax) n = kEmojiRecentMax;
    if (n > rawCount) n = rawCount;
    for (int i = 0; i < n; ++i) {
      const uint32_t cp = raw[i];
      if (cp == 0 || indexOf(cp) >= 0) continue;
      cps[count++] = cp;
    }
  }
};

}  // namespace ui
}  // namespace om
