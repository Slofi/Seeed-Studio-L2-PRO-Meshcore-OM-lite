// SPDX-License-Identifier: MIT
// EST-406, the C++ half: Send left the keyboard for the message input line. This proves the ticket's
// rules on the real layout arithmetic, not the drawing:
//  * the chat's letters and 123 bottom rows have NO Send key, while the Settings / RPTR editors keep
//    theirs (it is their sheet's commit key, and nothing about their sheet changes);
//  * Backspace (Del) is EXACTLY where it was on both chat pages, and every other key is where the
//    fingers know it: the keys before the space bar are within 1 px of their old place, and the row
//    still starts on the side pad and ends on the same right edge;
//  * the space bar took Send's room (it is wider by more than Send's own width, less the rounding);
//  * the send button on the draft line is at least 30 x 26, ends the line, and its DRAWN rect IS its
//    hit rect (sendButtonHit() answers the same rectangle on the corners and the centre, and nothing
//    one pixel outside);
//  * the text plate stops before the button, so no typed text can run under it (the tail scroll is
//    measured inside this narrower plate, screen_chat.cpp's drawDraft);
//  * it is disabled (dim, no action) while the draft is null, empty or only whitespace;
//  * it exists only on the chat composer's open sheet: not while the keyboard is closed and not on
//    the Settings / RPTR editors; the emoji page has no Send of its own, the composer button above
//    it is the one door.
// Plain C++: ui/chat_layout.cpp and the theme/layout headers, no LovyanGFX and no Arduino.
#include <stdio.h>
#include <string.h>

#include "ui/chat_layout.h"

using namespace om::ui;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static bool sameRect(const Rect& a, const Rect& b) {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// --- 1. no Send key on the chat, and the editors keep theirs --------------------------------------
static void testSendKeyGone() {
  for (int nums = 0; nums < 2; ++nums) {
    for (int r = 0; r < 4; ++r) {
      KeyDef defs[10];
      const int n = keyRow(nums != 0, r, defs, KeyboardField::Chat);
      CHECK(n > 0);
      for (int i = 0; i < n; ++i) CHECK(defs[i].kind != KeyKind::Send);
    }
  }

  // The letters bottom row: 123, emoji, comma, space, dot.
  KeyDef defs[10];
  CHECK(keyRow(false, 3, defs) == 5);
  CHECK(defs[0].kind == KeyKind::Mode && strcmp(defs[0].label, "123") == 0);
  CHECK(defs[1].kind == KeyKind::Emoji);
  CHECK(defs[2].kind == KeyKind::Char && defs[2].label[0] == ',');
  CHECK(defs[3].kind == KeyKind::Space);
  CHECK(defs[4].kind == KeyKind::Char && defs[4].label[0] == '.');
  CHECK(defs[3].units == kKeyUnitSpaceChat);   // 62 = the old 40 + Send's 22
  CHECK(kKeyUnitSpaceChat == kKeyUnitSpace + kKeyUnitWide);
  CHECK(keyRow(true, 3, defs) == 5);           // abc, emoji, space, comma, dot
  CHECK(defs[0].kind == KeyKind::Mode && strcmp(defs[0].label, "abc") == 0);
  CHECK(defs[1].kind == KeyKind::Emoji);
  CHECK(defs[2].kind == KeyKind::Space);
  CHECK(defs[3].kind == KeyKind::Char && defs[3].label[0] == ',');
  CHECK(defs[4].kind == KeyKind::Char && defs[4].label[0] == '.');
  CHECK(defs[2].units == kKeyUnitSpaceChat);

  // The editors still commit with their Send key (unchanged by this ticket).
  CHECK(keyRow(false, 3, defs, KeyboardField::NodeName) == 4 && defs[3].kind == KeyKind::Send);
  CHECK(keyRow(true, 3, defs, KeyboardField::NodeName) == 4 && defs[3].kind == KeyKind::Send);
  CHECK(keyRow(false, 3, defs, KeyboardField::WifiPassword) == 6 &&
        defs[5].kind == KeyKind::Send);
  CHECK(keyRow(true, 3, defs, KeyboardField::WifiPassword) == 6 &&
        defs[5].kind == KeyKind::Send);
  CHECK(keyRow(false, 3, defs, KeyboardField::Base64Key) == 2 &&
        defs[1].kind == KeyKind::Send);
  CHECK(keyRow(true, 3, defs, KeyboardField::Base64Key) == 5 &&
        defs[4].kind == KeyKind::Send);
  CHECK(keyRow(true, 3, defs, KeyboardField::RptrRegion) == 3 &&
        defs[2].kind == KeyKind::Send);
  CHECK(keyRow(false, 3, defs, KeyboardField::RptrRegion) == 2 &&
        defs[1].kind == KeyKind::Send);
}

// The row rectangles `units` produce on a row of `n` keys. Used to rebuild the PRE-ticket row so the
// leftover keys can be compared with where they were; rowKeyRects is the same arithmetic the layout
// used then and uses now.
static void rowOf(const Layout& L, const uint8_t* units, int n, Rect* out) {
  const Rect row{kOskPad, 0, (int16_t)(L.width - 2 * kOskPad), kOskKeyH};
  CHECK(rowKeyRects(row, units, n, kOskKeyGap, out) == n);
}

// --- 2. Backspace unchanged, every other key where the fingers know it ---------------------------
static void testKeysStay() {
  const Layout& L = l2();
  const ChatLayout letters = chatLayoutFor(L, true, false, false, false);
  const ChatLayout nums = chatLayoutFor(L, true, false, true, false);

  // Backspace: letters row 2, last of the nine keys. The whole rect is the pre-ticket one (dump from
  // the tree before this ticket: [258,190,58,22]); the draft line grew upward only, so the key rows
  // keep their y, and row 2's key table did not change at all.
  CHECK(letters.keyCount[2] == 9);
  CHECK(letters.keys[2][8].x == 258 && letters.keys[2][8].y == 190 &&
        letters.keys[2][8].w == 58 && letters.keys[2][8].h == 22);
  // The hit rect was ALWAYS wider than the drawn one for an outer key (it reaches the screen edge,
  // EST-338); this ticket did not change that: same x, same y, height and right edge as before.
  CHECK(letters.keyHit[2][8].x == letters.keys[2][8].x);
  CHECK(letters.keyHit[2][8].y == letters.keys[2][8].y);
  CHECK(letters.keyHit[2][8].h == letters.keys[2][8].h);
  CHECK(letters.keyHit[2][8].right() == L.width);
  KeyDef defs[10];
  CHECK(keyRow(false, 2, defs) == 9 && defs[8].kind == KeyKind::Del);
  const uint8_t lettersRow2[9] = {22, 10, 10, 10, 10, 10, 10, 10, 22};
  Rect old2[9];
  rowOf(L, lettersRow2, 9, old2);
  for (int c = 0; c < 9; ++c) {
    CHECK(letters.keys[2][c].x == old2[c].x && letters.keys[2][c].w == old2[c].w);
  }
  // The 123 page's Backspace (row 2, last of ten) is unchanged too: [263,190,53,22].
  CHECK(nums.keyCount[2] == 10);
  CHECK(nums.keys[2][9].x == 263 && nums.keys[2][9].y == 190 &&
        nums.keys[2][9].w == 53 && nums.keys[2][9].h == 22);
  const uint8_t numsRow2[10] = {22, 10, 10, 10, 10, 10, 10, 10, 10, 22};
  Rect oldNum2[10];
  rowOf(L, numsRow2, 10, oldNum2);
  for (int c = 0; c < 10; ++c) {
    CHECK(nums.keys[2][c].x == oldNum2[c].x && nums.keys[2][c].w == oldNum2[c].w);
  }

  // The letters bottom row before and after. The keys left of the space bar were at the same place
  // up to the 1 px the row's lost gap costs; the space bar took Send's room; the dot now ends the
  // row where Send ended.
  const uint8_t oldLettersRow3[6] = {22, 22, 10, 40, 10, 22};
  Rect old3[6];
  rowOf(L, oldLettersRow3, 6, old3);
  for (int c = 0; c < 3; ++c) {
    CHECK(letters.keys[3][c].x >= old3[c].x && letters.keys[3][c].x <= old3[c].x + 1);
    CHECK(letters.keys[3][c].w >= old3[c].w && letters.keys[3][c].w <= old3[c].w + 1);
  }
  CHECK(letters.keys[3][3].x >= old3[3].x && letters.keys[3][3].x <= old3[3].x + 1);
  CHECK(letters.keys[3][3].w >= old3[3].w + old3[5].w - 2);   // Send's room went to the space
  CHECK(letters.keys[3][3].w > letters.keys[3][2].w * 4);
  CHECK(letters.keys[3][4].right() == L.width - kOskPad);     // the dot ends the row
  CHECK(letters.keys[3][4].right() == old3[5].right());       // exactly where Send's edge was

  // The same on the 123 page: abc and emoji stay, the space bar grows, comma and dot move into the
  // old Send slot and the row still ends on the same right edge.
  const uint8_t oldNumsRow3[6] = {22, 22, 40, 10, 10, 22};
  Rect oldN3[6];
  rowOf(L, oldNumsRow3, 6, oldN3);
  for (int c = 0; c < 2; ++c) {
    CHECK(nums.keys[3][c].x >= oldN3[c].x && nums.keys[3][c].x <= oldN3[c].x + 1);
    CHECK(nums.keys[3][c].w >= oldN3[c].w && nums.keys[3][c].w <= oldN3[c].w + 1);
  }
  CHECK(nums.keys[3][2].w >= oldN3[2].w + oldN3[5].w - 2);    // abc/emoji/space, comma, dot
  CHECK(nums.keys[3][4].right() == oldN3[5].right());
  CHECK(nums.keys[3][4].right() == L.width - kOskPad);

  // No row has an overlap or a hole: the 2 px gap and both edges are still exact.
  for (int r = 0; r < 4; ++r) {
    const ChatLayout* gs[2] = {&letters, &nums};
    for (int k = 0; k < 2; ++k) {
      const ChatLayout& g = *gs[k];
      CHECK(g.keys[r][0].x == kOskPad);
      CHECK(g.keys[r][g.keyCount[r] - 1].right() == L.width - kOskPad);
      for (int c = 1; c < g.keyCount[r]; ++c) {
        CHECK(g.keys[r][c - 1].right() + kOskKeyGap == g.keys[r][c].x);
      }
    }
  }
}

// --- 3. the send button: one rect, at least 30 x 26, at the right end of the draft line -----------
static void testSendButtonLayout() {
  const Layout& L = l2();
  for (int nums = 0; nums < 2; ++nums) {
    for (int replying = 0; replying < 2; ++replying) {
      const ChatLayout g = chatLayoutFor(L, true, replying != 0, nums != 0, false);
      CHECK(!g.sendBtn.empty());
      CHECK(g.sendBtn.w >= 30 && g.sendBtn.h >= 26);          // the ticket's minimum
      CHECK(g.sendBtn.w == kSendBtnW && g.sendBtn.h == kSendBtnH);
      CHECK(g.sendBtn.h == g.draft.h);                        // it never overhangs the line
      CHECK(g.sendBtn.y == g.draft.y);
      CHECK(g.sendBtn.right() == L.width - kOskPad);          // the line's right end
      CHECK(g.draft.right() + kOskKeyGap == g.sendBtn.x);     // text plate stops before it
      CHECK(!g.draft.overlaps(g.sendBtn));
      CHECK(g.draft.right() < g.sendBtn.x);                   // nothing can be drawn under it
      CHECK(g.draft.w == L.width - 2 * kOskPad - kSendBtnW - kOskKeyGap);
      CHECK(g.draft.w - 2 * kFieldPadX > 200);                // the text room is still wide
      CHECK(g.sendBtn.bottom() + kOskDraftGap == g.keys[0][0].y);   // above the key rows
      CHECK(g.sendBtn.bottom() == g.draft.bottom());
      CHECK(g.sheet.contains(g.sendBtn.x, g.sendBtn.y));
    }
  }

  // The button's rect does not move with the page or the reply bar: same line, same right end.
  const ChatLayout plain = chatLayoutFor(L, true, false, false, false);
  CHECK(sameRect(chatLayoutFor(L, true, true, false, false).sendBtn, plain.sendBtn));
  CHECK(sameRect(chatLayoutFor(L, true, false, true, false).sendBtn, plain.sendBtn));

  // The same rect while the emoji page is up: the composer button is the page's Send door.
  const ChatLayout g = plain;
  const EmojiPadLayout e = emojiPadLayout(L, g, 16, 0, 0, 40);
  CHECK(!e.area.overlaps(g.sendBtn));                        // the emoji area starts below it
  CHECK(g.sendBtn.bottom() <= e.area.y);

  // No button while the keyboard is closed, and none on the Settings / RPTR editors.
  CHECK(chatLayoutFor(L, false, false, false, false).sendBtn.empty());
  const KeyboardField editors[] = {KeyboardField::NodeName, KeyboardField::Base64Key,
                                   KeyboardField::WifiPassword, KeyboardField::RptrPassword,
                                   KeyboardField::RptrPosition, KeyboardField::RptrRegion};
  for (KeyboardField f : editors) {
    const ChatLayout ed = chatLayoutFor(L, true, false, false, false, f);
    CHECK(ed.sendBtn.empty());
    CHECK(ed.draft.w == L.width - 2 * kOskPad);   // their line is unchanged, full width
    CHECK(ed.draft.h == kOskDraftH);
  }
}

// --- 4. the button's drawn rect IS its hit rect ---------------------------------------------------
static void testButtonHitIsItsRect() {
  const Layout& L = l2();
  const ChatLayout g = chatLayoutFor(L, true, false, false, false);
  const Rect& r = g.sendBtn;
  CHECK(r.w == 30 && r.h == 26);
  CHECK(sendButtonHit(g, r.x, r.y));                       // top left
  CHECK(sendButtonHit(g, r.right() - 1, r.y));             // top right
  CHECK(sendButtonHit(g, r.x, r.bottom() - 1));            // bottom left
  CHECK(sendButtonHit(g, r.right() - 1, r.bottom() - 1));  // bottom right
  CHECK(sendButtonHit(g, r.x + r.w / 2, r.y + r.h / 2));   // centre
  CHECK(!sendButtonHit(g, r.x - 1, r.y));
  CHECK(!sendButtonHit(g, r.right(), r.y));
  CHECK(!sendButtonHit(g, r.x, r.y - 1));
  CHECK(!sendButtonHit(g, r.x, r.bottom()));
  // Every point of the rect answers, and no point of the text plate does.
  for (int y = r.y; y < r.bottom(); y += 5) {
    for (int x = r.x; x < r.right(); x += 5) CHECK(sendButtonHit(g, x, y));
  }
  CHECK(!sendButtonHit(g, g.draft.x + 2, g.draft.y + 2));
  // A closed sheet has no button, so nothing answers.
  CHECK(!sendButtonHit(chatLayoutFor(L, false, false, false, false), 300, 120));
}

// --- 5. dim and no action while the draft is empty or only whitespace -----------------------------
static void testDisabledWhenEmpty() {
  CHECK(!sendEnabled(nullptr));
  CHECK(!sendEnabled(""));
  CHECK(!sendEnabled(" "));
  CHECK(!sendEnabled("    "));
  CHECK(!sendEnabled("\t"));
  CHECK(!sendEnabled("\r\n"));
  CHECK(!sendEnabled(" \t\r\n "));
  CHECK(sendEnabled("a"));
  CHECK(sendEnabled(" a "));
  CHECK(sendEnabled("."));
  CHECK(sendEnabled("0"));
  CHECK(sendEnabled("\xC4\x8D"));          // "č", two UTF-8 bytes: a real character
  CHECK(sendEnabled("\xF0\x9F\x98\x80"));  // an emoji: four UTF-8 bytes
  CHECK(sendEnabled(" \n x \t"));
}

int main() {
  testSendKeyGone();
  testKeysStay();
  testSendButtonLayout();
  testButtonHitIsItsRect();
  testDisabledWhenEmpty();
  if (g_fail == 0) {
    printf("est406: Send off the chat keyboard, keys stay put, one 30 x 26 send rect on the draft "
           "line, dim and inert on an empty draft\n");
    return 0;
  }
  printf("est406: %d failure(s)\n", g_fail);
  return 1;
}
