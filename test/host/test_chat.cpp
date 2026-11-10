// Host test for the chat layer: src/ui/chat_layout.cpp and src/ui/text_wrap.cpp, plus the chat
// rectangles. Plain g++, no Arduino and no LovyanGFX, so the same arithmetic the device uses is
// proven here with a fake 6 px-per-character measure.
#include <stdio.h>
#include <string.h>

#include "ui/chat_layout.h"
#include "ui/chat_model.h"
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

// --- a fake font: every byte is 6 px wide -----------------------------------
static int fakeMeasure(void* ctx, const char* text, int len) {
  (void)ctx;
  (void)text;
  return len * 6;
}

static const char* lineText(const char* src, char* buf, size_t cap, const WrapLine& l) {
  size_t n = (size_t)l.len;
  if (n >= cap) n = cap - 1;
  memcpy(buf, src + l.start, n);
  buf[n] = 0;
  return buf;
}

static void testWrap() {
  WrapLine lines[kMaxWrapLines];
  char buf[64];

  // exactly at the limit: 7 chars at 6 px = 42 px
  int n = wrapText("aaa bbb", fakeMeasure, nullptr, 42, 42, lines, kMaxWrapLines);
  CHECK(n == 1);
  CHECK(lines[0].start == 0 && lines[0].len == 7);
  CHECK(strcmp(lineText("aaa bbb", buf, sizeof(buf), lines[0]), "aaa bbb") == 0);

  // one word too many: the break happens before "ccc", the space is not drawn at the end
  n = wrapText("aaa bbb ccc", fakeMeasure, nullptr, 42, 42, lines, kMaxWrapLines);
  CHECK(n == 2);
  CHECK(lines[0].len == 7);
  CHECK(lines[1].start == 8 && lines[1].len == 3);

  // the first line is narrower (the time and the name sit on it), later lines get the full width
  n = wrapText("aaa bbb", fakeMeasure, nullptr, 24, 60, lines, kMaxWrapLines);
  CHECK(n == 2);
  CHECK(lines[0].len == 3);
  CHECK(lines[1].start == 4 && lines[1].len == 3);
  for (int i = 0; i < n; ++i) {
    char b[32];
    CHECK(fakeMeasure(nullptr, lineText("aaa bbb", b, sizeof(b), lines[i]), lines[i].len) <=
          (i == 0 ? 24 : 60));
  }

  // a word wider than a whole line is split between characters, never lost
  n = wrapText("abcdefghij", fakeMeasure, nullptr, 24, 24, lines, kMaxWrapLines);
  CHECK(n == 3);
  CHECK(lines[0].len == 4 && lines[1].start == 4 && lines[1].len == 4);
  CHECK(lines[2].start == 8 && lines[2].len == 2);
  {
    char b[32];
    CHECK(strcmp(lineText("abcdefghij", b, sizeof(b), lines[0]), "abcd") == 0);
    CHECK(strcmp(lineText("abcdefghij", b, sizeof(b), lines[2]), "ij") == 0);
  }

  // leading and repeated spaces never start a line and never end one
  n = wrapText("   aaa   bbb   ", fakeMeasure, nullptr, 18, 18, lines, kMaxWrapLines);
  CHECK(n == 2);
  {
    char b[32];
    CHECK(strcmp(lineText("   aaa   bbb   ", b, sizeof(b), lines[0]), "aaa") == 0);
    CHECK(strcmp(lineText("   aaa   bbb   ", b, sizeof(b), lines[1]), "bbb") == 0);
  }

  // every real message length, at a realistic width: nothing is ever wider than its line
  const char* longMsg =
      "Anyone near Bohinj this afternoon? The weather turns after 15:00 and the path is wet.";
  n = wrapText(longMsg, fakeMeasure, nullptr, 140, 306 - 14, lines, kMaxWrapLines);
  CHECK(n > 1);
  for (int i = 0; i < n; ++i) {
    char b[128];
    const char* s = lineText(longMsg, b, sizeof(b), lines[i]);
    const int w = fakeMeasure(nullptr, s, (int)strlen(s));
    CHECK(w <= (i == 0 ? 140 : 306 - 14));
    CHECK(lines[i].len == 0 || (s[0] != ' ' && s[strlen(s) - 1] != ' '));
  }
  CHECK(wrapConsumed(longMsg, lines, n) == (int)strlen(longMsg));

  // an empty draft still gets one (empty) line, so the height arithmetic never divides by zero
  n = wrapText("", fakeMeasure, nullptr, 100, 100, lines, kMaxWrapLines);
  CHECK(n == 1);
  CHECK(lines[0].len == 0);

  // a cut is detectable: the caller can tell text was lost
  char many[200];
  many[0] = 0;
  for (int i = 0; i < 20; ++i) strcat(many, "word ");
  n = wrapText(many, fakeMeasure, nullptr, 30, 30, lines, 2);
  CHECK(n == 2);
  CHECK(wrapConsumed(many, lines, n) < (int)strlen(many));

  // bad arguments are refused, not crashed on
  CHECK(wrapText(nullptr, fakeMeasure, nullptr, 10, 10, lines, 4) == 0);
  CHECK(wrapText("abc", nullptr, nullptr, 10, 10, lines, 4) == 0);
  CHECK(wrapText("abc", fakeMeasure, nullptr, 10, 10, nullptr, 4) == 0);
  CHECK(wrapText("abc", fakeMeasure, nullptr, 10, 10, lines, 0) == 0);
}

static void testChips() {
  const int16_t widths[3] = {40, 50, 60};

  CHECK(chipWidth(30, 0) == 30 + 2 * (kChipPadX + 1));
  CHECK(chipWidth(30, 6) == 30 + 6 + kChipGap + 2 * (kChipPadX + 1));

  CHECK(chipStripWidth(widths, 3) == 2 * kChipRowPadX + 40 + 50 + 60 + 2 * kChipGap);
  CHECK(chipStripWidth(nullptr, 0) == 2 * kChipRowPadX);

  // overflow: three chips of 150 px do not fit a 100 px strip
  CHECK(chipMaxScroll(widths, 3, 1000) == 0);
  const int maxS = chipMaxScroll(widths, 3, 100);
  CHECK(maxS == chipStripWidth(widths, 3) - 100);
  CHECK(maxS > 0);

  // chip hit rectangles: the left edge is inside, the right edge is the next chip's or the gap
  CHECK(chipLeft(widths, 0, 0) == kChipRowPadX);
  CHECK(chipLeft(widths, 1, 0) == kChipRowPadX + 40 + kChipGap);
  CHECK(chipLeft(widths, 2, 0) == kChipRowPadX + 40 + kChipGap + 50 + kChipGap);

  CHECK(chipAt(widths, 3, 0, kChipRowPadX) == 0);
  CHECK(chipAt(widths, 3, 0, kChipRowPadX + 39) == 0);
  CHECK(chipAt(widths, 3, 0, kChipRowPadX + 40) == -1);         // the gap
  CHECK(chipAt(widths, 3, 0, kChipRowPadX + 40 + kChipGap) == 1);
  CHECK(chipAt(widths, 3, 0, 2) == -1);                          // the leading pad
  CHECK(chipAt(widths, 3, 0, 9999) == -1);

  // scrolling moves the same rectangles: what is drawn is what is tapped
  const int scrolled = chipScrollTo(0, widths, 3, 2, 100);
  CHECK(scrolled == 64);
  CHECK(chipLeft(widths, 2, scrolled) >= 0);
  CHECK(chipLeft(widths, 2, scrolled) + widths[2] <= 100);
  CHECK(chipAt(widths, 3, scrolled, chipLeft(widths, 2, scrolled)) == 2);
  CHECK(chipAt(widths, 3, scrolled, chipLeft(widths, 2, scrolled) + 59) == 2);

  // scrolling back to the first chip brings it fully into view, and never past the ends
  CHECK(chipScrollTo(64, widths, 3, 0, 100) == kChipRowPadX);
  CHECK(chipLeft(widths, 0, chipScrollTo(64, widths, 3, 0, 100)) == 0);
  CHECK(chipScrollTo(-5, widths, 3, 0, 100) == 0);
  CHECK(chipScrollTo(999, widths, 3, 1, 100) <= maxS);
  CHECK(chipScrollTo(0, widths, 3, 7, 100) == 0);   // no such chip: unchanged
}

static void testComposeAndSheet() {
  const Layout& L = l2();
  const ChatLayout c = chatLayoutFor(L, false, false, false);

  // the closed screen: chips, messages and the compose line tile the content, no overlap
  CHECK(c.chips.x == L.content.x && c.chips.y == L.content.y && c.chips.h == kChipRowH);
  CHECK(c.msgs.y == c.chips.bottom());
  CHECK(c.msgs.bottom() == c.compose.y);
  CHECK(c.compose.bottom() == L.content.bottom());
  CHECK(!c.msgs.overlaps(c.compose));
  CHECK(c.sheet.empty());
  CHECK(c.replyBar.empty());
  CHECK(c.keyCount[0] == 0);

  // the compose line's three pieces sit in order with exactly the gap between them
  CHECK(c.field.x == c.compose.x + kComposePadX);
  CHECK(c.sendBtn.right() == c.compose.right() - kComposePadX);
  CHECK(c.field.right() + kComposeGap == c.kbdBtn.x);
  CHECK(c.kbdBtn.right() + kComposeGap == c.sendBtn.x);
  CHECK(!c.field.overlaps(c.kbdBtn) && !c.kbdBtn.overlaps(c.sendBtn));
  CHECK(c.field.h == kFieldH && c.kbdBtn.h == kFieldH && c.sendBtn.h == kFieldH);

  // the sheet covers the tab bar completely, and the compose line is gone while it is open
  const ChatLayout o = chatLayoutFor(L, true, false, false);
  CHECK(o.osk);
  CHECK(o.sheet.bottom() == L.height);
  CHECK(o.sheet.y < L.tabbar.y);
  CHECK(o.sheet.y + o.sheet.h >= L.tabbar.bottom());
  CHECK(o.compose.empty() && o.field.empty() && o.sendBtn.empty());
  CHECK(o.msgs.bottom() == o.sheet.y);
  CHECK(!o.msgs.overlaps(o.sheet));

  // its contents: pad, draft, gap, four 22 px rows with 2 px gaps, and the bottom pad
  CHECK(o.sheet.h == kOskPad * 2 + kOskDraftH + kOskDraftGap + 4 * kOskKeyH + 3 * kOskKeyGap);
  CHECK(o.draft.y == o.sheet.y + kOskPad);
  CHECK(o.draft.x == kOskPad);
  CHECK(o.draft.w == L.width - 2 * kOskPad);
  CHECK(o.keys[0][0].y == o.draft.bottom() + kOskDraftGap);
  CHECK(o.keys[3][0].bottom() + kOskPad == o.sheet.bottom());
  CHECK(o.replyBar.empty());

  // the keys tile every row exactly: no overlap, the inner gap, both edges flush
  for (int r = 0; r < 4; ++r) {
    const int n = o.keyCount[r];
    CHECK(n >= 2);
    CHECK(o.keys[r][0].x == kOskPad);
    CHECK(o.keys[r][n - 1].right() == L.width - kOskPad);
    for (int i = 0; i < n; ++i) {
      CHECK(o.keys[r][i].h == kOskKeyH);
      CHECK(o.keys[r][i].w > 0);
      CHECK(o.keys[r][i].y == o.keys[0][0].y + r * (kOskKeyH + kOskKeyGap));
      if (i > 0) {
        CHECK(!o.keys[r][i - 1].overlaps(o.keys[r][i]));
        CHECK(o.keys[r][i - 1].right() + kOskKeyGap == o.keys[r][i].x);
      }
    }
  }
  CHECK(o.keyCount[0] == 10 && o.keyCount[1] == 9 && o.keyCount[2] == 9 && o.keyCount[3] == 3);

  // the numbers page has the same footprint and its own keys
  const ChatLayout num = chatLayoutFor(L, true, false, true);
  CHECK(num.sheet.y == o.sheet.y && num.sheet.h == o.sheet.h);
  CHECK(num.keyCount[0] == 10 && num.keyCount[1] == 10 && num.keyCount[2] == 10 &&
        num.keyCount[3] == 2);

  // a reply bar rides above the draft and makes the sheet taller by exactly its own row
  const ChatLayout rep = chatLayoutFor(L, true, true, false);
  CHECK(rep.sheet.h == o.sheet.h + kOskReplyH + kOskDraftGap);
  CHECK(rep.replyBar.y == rep.sheet.y + kOskPad);
  CHECK(rep.replyBar.bottom() + kOskDraftGap == rep.draft.y);
  CHECK(!rep.replyBar.overlaps(rep.draft));

  // key definitions: the device's own characters, in the mock's row structure
  KeyDef defs[10];
  CHECK(keyRow(false, 0, defs) == 10);
  CHECK(defs[0].label[0] == 'q' && defs[9].label[0] == 'p');
  CHECK(keyRow(false, 1, defs) == 9);
  CHECK(defs[0].label[0] == 'a');
  CHECK(keyRow(false, 2, defs) == 9);
  CHECK(defs[0].kind == KeyKind::Shift);
  CHECK(defs[1].label[0] == 'y' && defs[7].label[0] == 'm');
  CHECK(defs[8].kind == KeyKind::Del);
  CHECK(keyRow(false, 3, defs) == 3);
  CHECK(defs[0].kind == KeyKind::Mode && defs[1].kind == KeyKind::Space &&
        defs[2].kind == KeyKind::Send);
  CHECK(keyRow(true, 0, defs) == 10 && defs[0].label[0] == '1');
  CHECK(keyRow(true, 1, defs) == 10 && defs[0].label[0] == '@');
  CHECK(keyRow(true, 2, defs) == 10);
  CHECK(defs[0].kind == KeyKind::Mode && defs[9].kind == KeyKind::Del);
  CHECK(keyRow(true, 3, defs) == 2 && defs[0].kind == KeyKind::Space);
  CHECK(keyRow(false, 9, defs) == 0);
  CHECK(keyRow(false, 0, nullptr) == 0);
}

static void testMessages() {
  const Layout& L = l2();
  const ChatLayout c = chatLayoutFor(L, false, false, false);

  // content height: pads, the blocks and the gaps between them
  const int16_t heights[3] = {14, 28, 14};
  CHECK(msgContentHeight(heights, 3) == 2 * kMsgPadY + 56 + 2 * kMsgGap);
  CHECK(msgContentHeight(nullptr, 0) == 0);
  CHECK(msgContentHeight(heights, 0) == 0);

  // everything fits: no scroll possible, and the newest sits on the bottom pad
  CHECK(msgMaxScroll(c.msgs, heights, 3) == 0);
  int16_t tops[3];
  msgSpans(c.msgs, heights, 3, 0, tops);
  CHECK(tops[2] + heights[2] == c.msgs.bottom() - kMsgPadY);
  CHECK(tops[1] + heights[1] + kMsgGap == tops[2]);
  CHECK(tops[0] + heights[0] + kMsgGap == tops[1]);

  // the newest message is the one under the finger just above the bottom pad
  CHECK(msgAt(c.msgs, heights, 3, 0, c.msgs.bottom() - kMsgPadY - 1) == 2);
  CHECK(msgAt(c.msgs, heights, 3, 0, tops[1]) == 1);
  CHECK(msgAt(c.msgs, heights, 3, 0, tops[1] + heights[1] - 1) == 1);
  CHECK(msgAt(c.msgs, heights, 3, 0, tops[1] + heights[1]) == -1);   // the gap
  CHECK(msgAt(c.msgs, heights, 3, 0, c.msgs.bottom() - 1) == -1);    // the bottom pad
  CHECK(msgAt(c.msgs, heights, 3, 0, c.msgs.y - 1) == -1);           // above the band
  CHECK(msgAt(c.msgs, heights, 3, 0, c.msgs.bottom()) == -1);        // below the band

  // a full ring of short messages overflows: the scroll is clamped to the real content
  int16_t many[20];
  for (int i = 0; i < 20; ++i) many[i] = 14;
  const int content = msgContentHeight(many, 20);
  const int maxS = msgMaxScroll(c.msgs, many, 20);
  CHECK(maxS == content - c.msgs.h);
  CHECK(maxS > 0);

  int16_t mtops[20];
  msgSpans(c.msgs, many, 20, maxS, mtops);
  CHECK(mtops[0] == c.msgs.y + kMsgPadY);                 // scrolled to the top of the history
  CHECK(mtops[19] + 14 == c.msgs.bottom() - kMsgPadY + maxS);
  CHECK(msgAt(c.msgs, many, 20, maxS, c.msgs.y + kMsgPadY) == 0);   // the oldest, at the top

  // scrolled fully down: the first message is off the band and cannot be tapped there
  msgSpans(c.msgs, many, 20, 0, mtops);
  CHECK(mtops[0] < c.msgs.y);
  CHECK(msgAt(c.msgs, many, 20, 0, c.msgs.y) != 0);

  // the band shrinks while the sheet is open, so the same history scrolls further
  const ChatLayout o = chatLayoutFor(L, true, false, false);
  CHECK(o.msgs.h < c.msgs.h);
  CHECK(msgMaxScroll(o.msgs, many, 20) > maxS);
}


// The reply target and the open menu hold a message sequence number, not an index into the view.
// A full ring plus one new message drops its oldest entry and shifts every index by one; the held ids
// must resolve to the same message afterwards.
static void testRingShift() {
  ChatMsgView msgs[kMaxChatMsgs];
  char names[kMaxChatMsgs + 1][12];
  char texts[kMaxChatMsgs + 1][12];
  for (int i = 0; i < kMaxChatMsgs; ++i) {
    snprintf(names[i], sizeof(names[i]), "n%d", i + 1);
    snprintf(texts[i], sizeof(texts[i]), "m%d", i + 1);
    msgs[i].seq = (uint32_t)(i + 1);
    msgs[i].name = names[i];
    msgs[i].text = texts[i];
  }

  // The user opens the menu on the 6th message and taps REPLY on the 21st.
  const uint32_t menuSeq = msgs[5].seq;
  const uint32_t replySeq = msgs[20].seq;
  char replyName[sizeof(names[0])];
  snprintf(replyName, sizeof(replyName), "%s", msgs[20].name);
  CHECK(chatMsgIndexBySeq(msgs, kMaxChatMsgs, menuSeq) == 5);
  CHECK(chatMsgIndexBySeq(msgs, kMaxChatMsgs, replySeq) == 20);

  // One new message arrives while the ring is full: drop the oldest, shift, append seq 33.
  memmove(&msgs[0], &msgs[1], sizeof(msgs[0]) * (kMaxChatMsgs - 1));
  const uint32_t newSeq = 33;
  msgs[kMaxChatMsgs - 1] = ChatMsgView{};
  msgs[kMaxChatMsgs - 1].seq = newSeq;
  msgs[kMaxChatMsgs - 1].name = names[kMaxChatMsgs];
  msgs[kMaxChatMsgs - 1].text = texts[kMaxChatMsgs];
  snprintf(names[kMaxChatMsgs], sizeof(names[0]), "n33");
  snprintf(texts[kMaxChatMsgs], sizeof(texts[0]), "m33");

  // The same ids now resolve one slot lower but to the same names/texts.
  const int menuAt = chatMsgIndexBySeq(msgs, kMaxChatMsgs, menuSeq);
  const int replyAt = chatMsgIndexBySeq(msgs, kMaxChatMsgs, replySeq);
  CHECK(menuAt == 4);
  CHECK(replyAt == 19);
  CHECK(strcmp(msgs[menuAt].text, "m6") == 0);
  CHECK(strcmp(msgs[replyAt].text, "m21") == 0);
  CHECK(strcmp(replyName, "n21") == 0);   // copied at tap time, so still the right sender

  // Only a target that really fell out of the ring is gone (the old first message).
  CHECK(chatMsgIndexBySeq(msgs, kMaxChatMsgs, 1) == -1);
  CHECK(chatMsgIndexBySeq(msgs, kMaxChatMsgs, newSeq) == kMaxChatMsgs - 1);

  // 0 is "no message" and a null list is never dereferenced.
  CHECK(chatMsgIndexBySeq(msgs, kMaxChatMsgs, 0) == -1);
  CHECK(chatMsgIndexBySeq(nullptr, kMaxChatMsgs, 7) == -1);
  CHECK(chatMsgIndexBySeq(msgs, 0, 7) == -1);
}

int main() {
  testWrap();
  testChips();
  testComposeAndSheet();
  testMessages();
  testRingShift();

  if (g_fail) {
    printf("%d chat check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("chat: all checks passed\n");
  return 0;
}
