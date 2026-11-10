// SPDX-License-Identifier: MIT
// The emoji keyboard's curated "most used" set: 120 code points in five groups. These are the
// cells the grid offers; the "Recent" row is the separate last-16 list (emoji_recent.h). Every
// code point here must exist in src/ui/emoji_data.bin - test/host/test_emoji.cpp proves it.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

constexpr int kEmojiGroupCount = 5;

struct EmojiGroup {
  const char* name;       // the rail's label (used by tests and the Info text)
  const uint32_t* cps;
  int count;
  uint32_t icon;          // a code point from the table, drawn as the rail button
};

const EmojiGroup* emojiGroups();
const EmojiGroup* emojiGroupAt(int i);
// Which group holds `cp`, and where in it. False when the curated set does not have it.
bool emojiGroupFind(uint32_t cp, int* group, int* index);
const char* emojiGroupName(int i);

}  // namespace ui
}  // namespace om
