// SPDX-License-Identifier: MIT
#include "ui/emoji_picker.h"

#include <stddef.h>

namespace om {
namespace ui {
namespace {

// Curated, not usage-ranked: the set a phone keyboard would call "most used". Faces first so a
// new user sees the familiar row, then hands, hearts, objects and symbols. Every entry is a
// single code point (no FE0F): the draw walker adds the presentation selector's meaning itself.
const uint32_t kFaces[] = {
    0x1F600, 0x1F603, 0x1F604, 0x1F601, 0x1F606, 0x1F605, 0x1F602, 0x1F923, 0x1F60A, 0x1F607,
    0x1F642, 0x1F643, 0x1F609, 0x1F60C, 0x1F60D, 0x1F970, 0x1F618, 0x1F617, 0x1F619, 0x1F61A,
    0x1F60B, 0x1F61B, 0x1F61D, 0x1F61C, 0x1F92A, 0x1F928, 0x1F9D0, 0x1F913, 0x1F60E, 0x1F978,
    0x1F929, 0x1F973, 0x1F60F, 0x1F612, 0x1F61E, 0x1F614, 0x1F61F, 0x1F615, 0x1F641, 0x1F623,
};

const uint32_t kHands[] = {
    0x1F44B, 0x1F91A, 0x1F590, 0x270B, 0x1F596, 0x1F44C, 0x1F90C, 0x1F90F, 0x270C, 0x1F91E,
    0x1F91F, 0x1F918, 0x1F919, 0x1F448, 0x1F449, 0x1F446, 0x1F595, 0x1F447, 0x261D, 0x1F44D,
    0x1F44E, 0x270A, 0x1F44A, 0x1F91B,
};

const uint32_t kHearts[] = {
    0x2764, 0x1F9E1, 0x1F49B, 0x1F49A, 0x1F499, 0x1F49C, 0x1F5A4, 0x1F90D, 0x1F90E, 0x1F494,
    0x2763, 0x1F495, 0x1F49E, 0x1F493, 0x1F497, 0x1F496, 0x1F498, 0x1F49D, 0x1F49F, 0x2665,
};

const uint32_t kObjects[] = {
    0x1F525, 0x2B50, 0x1F31F, 0x2728, 0x1F4A5, 0x1F4AB, 0x1F4A6, 0x1F4A8, 0x1F389, 0x1F38A,
    0x1F381, 0x1F4F1, 0x1F4BB, 0x231A, 0x1F4F7, 0x1F50B, 0x1F4A1, 0x1F511,
};

const uint32_t kSymbols[] = {
    0x2705, 0x274C, 0x26A0, 0x2757, 0x2753, 0x2795, 0x2796, 0x2B06, 0x2B07,
    0x2B05, 0x27A1, 0x1F504, 0x1F501, 0x23F0, 0x23F3, 0x1F514, 0x1F4F6, 0x1F197,
};

const EmojiGroup kGroups[kEmojiGroupCount] = {
    {"faces", kFaces, (int)(sizeof(kFaces) / sizeof(kFaces[0])), 0x1F600},
    {"hands", kHands, (int)(sizeof(kHands) / sizeof(kHands[0])), 0x1F44D},
    {"hearts", kHearts, (int)(sizeof(kHearts) / sizeof(kHearts[0])), 0x2764},
    {"objects", kObjects, (int)(sizeof(kObjects) / sizeof(kObjects[0])), 0x1F525},
    {"symbols", kSymbols, (int)(sizeof(kSymbols) / sizeof(kSymbols[0])), 0x26A1},
};

}  // namespace

const EmojiGroup* emojiGroups() { return kGroups; }

const EmojiGroup* emojiGroupAt(int i) {
  return (i >= 0 && i < kEmojiGroupCount) ? &kGroups[i] : nullptr;
}

const char* emojiGroupName(int i) {
  return (i >= 0 && i < kEmojiGroupCount) ? kGroups[i].name : "";
}

bool emojiGroupFind(uint32_t cp, int* group, int* index) {
  for (int g = 0; g < kEmojiGroupCount; ++g) {
    for (int i = 0; i < kGroups[g].count; ++i) {
      if (kGroups[g].cps[i] == cp) {
        if (group) *group = g;
        if (index) *index = i;
        return true;
      }
    }
  }
  return false;
}

}  // namespace ui
}  // namespace om
