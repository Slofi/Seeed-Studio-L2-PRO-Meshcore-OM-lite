// Host test for EST-345: the generated Noto emoji table, the UTF-8 sequence walker (variation
// selectors, skin tones, ZWJ, flags), emoji width measuring and the keyboard's Recent row.
//
// It reads the real src/ui/emoji_data.bin that ships in the tree, so a stale or unsorted table fails
// here rather than on the device. Plain g++ (no Arduino, no LovyanGFX, no PIL): the blob is the
// interface between tools/mkemoji.py and the firmware.
#include <stdio.h>
#include <string.h>

#include <vector>

#include "ui/emoji_picker.h"
#include "ui/emoji_recent.h"
#include "ui/emoji_table.h"
#include "ui/text_wrap.h"

using namespace om::ui;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static std::vector<uint8_t> loadBlob() {
  std::vector<uint8_t> data;
  FILE* f = fopen("../../src/ui/emoji_data.bin", "rb");
  if (f == nullptr) {
    printf("FAIL cannot open ../../src/ui/emoji_data.bin (run from test/host)\n");
    ++g_fail;
    return data;
  }
  uint8_t buf[16384];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
  fclose(f);
  return data;
}

// Every cell: RGB565 then the 4-bit mask. A cell with no visible pixel would be a silent blank in
// the grid, so the test insists on at least one non-zero nibble in every entry.
static bool cellHasInk(const EmojiBlob& b, uint32_t offset) {
  const size_t rgbBytes = (size_t)b.w * b.h * 2;
  const size_t alphaBytes = (size_t)b.h * ((b.w + 1) / 2);
  for (size_t i = 0; i < alphaBytes; ++i) {
    if (b.data[offset + rgbBytes + i] != 0) return true;
  }
  return false;
}

static void testBlob() {
  const std::vector<uint8_t> data = loadBlob();
  if (data.empty()) return;
  const EmojiBlob b = emojiParseBlob(data.data(), (uint32_t)data.size());
  CHECK(b.ok);
  CHECK(b.w == 13 && b.h == 13);
  CHECK(b.count >= 1400 && b.count <= 1500);   // the installed font's single-code-point set
  CHECK(b.dataOffset == 16u + (uint32_t)b.count * 8u);

  uint32_t previous = 0;
  for (int i = 0; i < (int)b.count; ++i) {
    EmojiIndexEntry e;
    CHECK(emojiEntry(b, i, &e));
    if (i > 0) CHECK(e.cp > previous);   // strictly sorted, no duplicate key
    previous = e.cp;
    CHECK(e.offset >= b.dataOffset);
    CHECK((e.offset & 3u) == 0);         // 4-byte aligned, so the flash read is safe
    const size_t used = (size_t)b.w * b.h * 2 + (size_t)b.h * ((b.w + 1) / 2);
    const size_t padded = (used + 3u) & ~(size_t)3u;
    CHECK(e.offset + padded <= b.size);
    CHECK(cellHasInk(b, e.offset));

    EmojiIndexEntry found;
    CHECK(emojiFindIn(b, e.cp, &found));
    CHECK(found.cp == e.cp && found.offset == e.offset);
  }

  // ASCII and the sequence-only code points are not in the table: they must take the normal text
  // path or the placeholder/flag path, never an emoji cell.
  EmojiIndexEntry none;
  CHECK(!emojiFindIn(b, 'A', &none));
  CHECK(!emojiFindIn(b, 0x200D, &none));
  CHECK(!emojiFindIn(b, 0xFE0F, &none));
  CHECK(!emojiFindIn(b, 0x1F3FD, &none));
  CHECK(!emojiFindIn(b, 0x1F1F8, &none));   // regional indicator: the flag rule uses boxes

  // Truncated / foreign blobs are refused, which makes every emoji draw the placeholder box.
  CHECK(!emojiParseBlob(data.data(), 15).ok);
  CHECK(!emojiParseBlob(data.data(), (uint32_t)data.size() - 1).ok);
  std::vector<uint8_t> bad = data;
  bad[0] = 'X';
  CHECK(!emojiParseBlob(bad.data(), (uint32_t)bad.size()).ok);
  bad = data;
  bad[4] = 2;   // version
  CHECK(!emojiParseBlob(bad.data(), (uint32_t)bad.size()).ok);
}

static void testCuratedGroups() {
  const std::vector<uint8_t> data = loadBlob();
  const EmojiBlob blob = emojiParseBlob(data.data(), (uint32_t)data.size());
  CHECK(blob.ok);
  if (!blob.ok) return;

  int total = 0;
  uint32_t seen[160];
  for (int g = 0; g < kEmojiGroupCount; ++g) {
    const EmojiGroup* group = emojiGroupAt(g);
    CHECK(group != nullptr);
    if (group == nullptr) continue;
    CHECK(group->name != nullptr && group->name[0] != 0);
    EmojiIndexEntry icon;
    CHECK(emojiFindIn(blob, group->icon, &icon));
    for (int i = 0; i < group->count; ++i) {
      const uint32_t cp = group->cps[i];
      EmojiIndexEntry e;
      CHECK(emojiFindIn(blob, cp, &e));
      int atGroup = -1;
      int atIndex = -1;
      CHECK(emojiGroupFind(cp, &atGroup, &atIndex));
      CHECK(atGroup == g && atIndex == i);
      for (int k = 0; k < total; ++k) CHECK(seen[k] != cp);   // no duplicates across groups
      CHECK(total < (int)(sizeof(seen) / sizeof(seen[0])));
      if (total < (int)(sizeof(seen) / sizeof(seen[0]))) seen[total++] = cp;
    }
  }
  CHECK(total == 120);
  CHECK(emojiGroupFind(0x1F600, nullptr, nullptr));
  CHECK(!emojiGroupFind('A', nullptr, nullptr));
  CHECK(emojiGroupAt(-1) == nullptr && emojiGroupAt(kEmojiGroupCount) == nullptr);
  CHECK(strcmp(emojiGroupName(0), "faces") == 0);
  CHECK(emojiGroupName(99)[0] == 0);
}

// utf8Decode itself: 2-, 3-, 4-byte and broken input (the chat-polish rule, still the base).
static void testUtf8Decode() {
  int i = 0;
  CHECK(utf8Decode("A", &i) == 'A' && i == 1);
  i = 0;
  CHECK(utf8Decode("\xC3\xA9", &i) == 0xE9 && i == 2);
  i = 0;
  CHECK(utf8Decode("\xE2\x82\xAC", &i) == 0x20AC && i == 3);
  i = 0;
  CHECK(utf8Decode("\xF0\x9F\x98\x80", &i) == 0x1F600 && i == 4);
  i = 0;
  CHECK(utf8Decode("\x80", &i) == kUtf8Invalid && i == 1);              // stray continuation
  i = 0;
  CHECK(utf8Decode("\xC3", &i) == kUtf8Invalid && i == 1);              // truncated 2-byte
  i = 0;
  CHECK(utf8Decode("\xF0\x9F\x98", &i) == kUtf8Invalid && i == 1);      // truncated 4-byte
}

// textUnitAt: the sequence rules the ticket names.
static void testTextUnits() {
  TextUnit u;
  const char* smile = "\xF0\x9F\x98\x80";   // U+1F600
  CHECK(textUnitAt(smile, 4, 0, &u) == 4);
  CHECK(!u.zero && u.cp == 0x1F600 && u.start == 0 && u.coreEnd == 4 && u.end == 4);

  const char* heart = "\xE2\x9D\xA4\xEF\xB8\x8F";   // U+2764 U+FE0F
  CHECK(textUnitAt(heart, 6, 0, &u) == 6);
  CHECK(!u.zero && u.cp == 0x2764 && u.coreEnd == 3 && u.end == 6);

  const char* thumb = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD";   // U+1F44D U+1F3FD
  CHECK(textUnitAt(thumb, 8, 0, &u) == 8);
  CHECK(u.cp == 0x1F44D && u.coreEnd == 4 && u.end == 8);

  // A ZWJ sequence draws its parts in order: one unit per part, the joiner belongs to the part before.
  const char* family =
      "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
  CHECK(textUnitAt(family, 18, 0, &u) == 7);
  CHECK(u.cp == 0x1F468 && u.coreEnd == 4 && u.end == 7);
  CHECK(textUnitAt(family, 18, 7, &u) == 14);
  CHECK(u.cp == 0x1F469 && u.coreEnd == 11 && u.end == 14);
  CHECK(textUnitAt(family, 18, 14, &u) == 18);
  CHECK(u.cp == 0x1F467 && u.coreEnd == 18 && u.end == 18);

  // A flag pair is two regional indicators, each its own (missing) glyph: two boxes, no merge.
  const char* flag = "\xF0\x9F\x87\xB8\xF0\x9F\x87\xAE";   // SI
  CHECK(textUnitAt(flag, 8, 0, &u) == 4);
  CHECK(!u.zero && u.cp == 0x1F1F8);
  CHECK(textUnitAt(flag, 8, 4, &u) == 8);
  CHECK(!u.zero && u.cp == 0x1F1EE);

  // A stray selector has no base: it takes no room and draws nothing.
  CHECK(textUnitAt("\xEF\xB8\x8F", 3, 0, &u) == 3);
  CHECK(u.zero && u.cp == 0xFE0F);
  // A malformed byte still advances exactly one byte.
  CHECK(textUnitAt("\x80", 1, 0, &u) == 1);
  CHECK(!u.zero && u.cp == kUtf8Invalid);
}

struct WidthCtx {
  const EmojiBlob* b;
};

static int widthGlyph(void* ctx, uint32_t cp) {
  const WidthCtx* c = (const WidthCtx*)ctx;
  EmojiIndexEntry e;
  if (emojiFindIn(*c->b, cp, &e)) return (int)c->b->w + 1;   // 13 + 1 px of air
  if (cp >= 0x20 && cp <= 0x7E) return 6;
  return 0;
}

static void testMeasureWidth() {
  const std::vector<uint8_t> data = loadBlob();
  const EmojiBlob blob = emojiParseBlob(data.data(), (uint32_t)data.size());
  CHECK(blob.ok);
  if (!blob.ok) return;
  WidthCtx ctx{&blob};
  const int space = 4;
  const int miss = space + 2;   // theme kMissingExtra, the placeholder box

  CHECK(measureUtf8("AB", 2, widthGlyph, &ctx, space) == 12);
  CHECK(measureUtf8("\xF0\x9F\x98\x80", 4, widthGlyph, &ctx, space) == 14);
  CHECK(measureUtf8("\xF0\x9F\x98\x80\xF0\x9F\x98\x80", 8, widthGlyph, &ctx, space) == 28);
  // The variation selector is consumed with the base: it never adds a placeholder width.
  CHECK(measureUtf8("\xE2\x9D\xA4\xEF\xB8\x8F", 6, widthGlyph, &ctx, space) == 14);
  CHECK(measureUtf8("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", 8, widthGlyph, &ctx, space) == 14);
  // ZWJ draws three parts, so it measures as three cells.
  CHECK(measureUtf8("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7",
                    18, widthGlyph, &ctx, space) == 42);
  // Flag: two regional indicators, each a placeholder box.
  CHECK(measureUtf8("\xF0\x9F\x87\xB8\xF0\x9F\x87\xAE", 8, widthGlyph, &ctx, space) == 2 * miss);
  // A stray selector is zero; a broken byte is one placeholder.
  CHECK(measureUtf8("\xEF\xB8\x8F", 3, widthGlyph, &ctx, space) == 0);
  CHECK(measureUtf8("\x80", 1, widthGlyph, &ctx, space) == miss);
  // Text around emoji keeps its own width.
  CHECK(measureUtf8("x\xF0\x9F\x98\x80y", 6, widthGlyph, &ctx, space) == 6 + 14 + 6);
  // A byte range that ends after the base but before its selector has the same width.
  CHECK(measureUtf8("\xE2\x9D\xA4", 3, widthGlyph, &ctx, space) == 14);
}

static void testRecent() {
  EmojiRecent r;
  CHECK(r.count == 0);
  r.use(0x1F600);
  r.use(0x1F44D);
  CHECK(r.count == 2);
  CHECK(r.cps[0] == 0x1F44D && r.cps[1] == 0x1F600);
  r.use(0x1F600);   // already there: move to the front, do not duplicate
  CHECK(r.count == 2 && r.cps[0] == 0x1F600 && r.cps[1] == 0x1F44D);
  r.use(0);         // padding is not an entry
  CHECK(r.count == 2);
  for (uint32_t cp = 0x1F601; cp < 0x1F601 + 20; ++cp) r.use(cp);
  CHECK(r.count == kEmojiRecentMax);
  CHECK(r.cps[0] == 0x1F601 + 19);   // newest first
  CHECK(r.indexOf(0x1F44D) == -1);   // the oldest fell off
  CHECK(r.indexOf(0x1F601 + 19) == 0);

  // Restore drops a duplicate, a zero and an out-of-range count, so a torn blob cannot add a cell.
  const uint32_t raw[16] = {0x1F600, 0x1F600, 0, 0x2764};
  EmojiRecent s;
  s.restore(raw, 16, 4);
  CHECK(s.count == 2 && s.cps[0] == 0x1F600 && s.cps[1] == 0x2764);
  s.restore(raw, 16, 200);
  CHECK(s.count <= kEmojiRecentMax);
  s.restore(nullptr, 0, 0);
  CHECK(s.count == 0);
  s.clear();
  CHECK(s.count == 0 && s.cps[0] == 0);
}

int main() {
  testBlob();
  testCuratedGroups();
  testUtf8Decode();
  testTextUnits();
  testMeasureWidth();
  testRecent();

  if (g_fail) {
    printf("%d emoji check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("emoji: all checks passed\n");
  return 0;
}
