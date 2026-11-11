// Host test for the chat layer: src/ui/chat_layout.cpp, src/ui/text_wrap.cpp and
// src/ui/message_sheet.cpp, plus the chat rectangles. Plain g++, no Arduino and no LovyanGFX, so
// the same arithmetic the device uses is proven here with a fake 6 px per ASCII character measure
// (a missing glyph is a space plus 2 px, exactly what the device draws).
#include <stdio.h>
#include <string.h>

#include "ui/badge_font.h"
#include "ui/chat_layout.h"
#include "ui/chat_model.h"
#include "ui/compose_text.h"
#include "ui/message_sheet.h"
#include "ui/text_wrap.h"

using namespace om;
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

// --- a fake face with ASCII plus the mono middle dot: 6 px, a missing glyph is space + 2 ---------
static int fakeGlyph(void* ctx, uint32_t cp) {
  (void)ctx;
  if ((cp >= 0x20 && cp <= 0x7E) || cp == 0xB7) return 6;
  return 0;
}

static int fakeUtf8Measure(void* ctx, const char* text, int len) {
  return measureUtf8(text, len, fakeGlyph, ctx, 6);
}

static const char* lineText(const char* src, char* buf, size_t cap, const WrapLine& l) {
  size_t n = (size_t)l.len;
  if (n >= cap) n = cap - 1;
  memcpy(buf, src + l.start, n);
  buf[n] = 0;
  return buf;
}

// Is byte offset `at` the start of a code point in s?
static bool onBoundary(const char* s, int at) {
  for (int i = 0; s[i] != 0;) {
    if (i == at) return true;
    utf8Decode(s, &i);
  }
  return s[at] == 0;
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

// UTF-8: a 4-byte emoji is one placeholder a space plus 2 px wide, it never splits a sequence, and
// the wrap measures it the same way the screen draws it.
static void testUtf8() {
  // The bytes the bot replies carry: "[OM Bot Ljubljana] \xF0\x9F\x98\x80PONG" (U+1F600 grinning).
  const char* msg = "[OM Bot] \xF0\x9F\x98\x80PONG";
  const int len = (int)strlen(msg);
  CHECK(len == 17);   // 2 + 3 + 4 + 8: the 4-byte emoji, never 1 byte per glyph

  // decode: ASCII, a 2-byte, a 3-byte and the 4-byte sequence, and a broken byte
  int i = 0;
  CHECK(utf8Decode("A", &i) == 'A' && i == 1);
  i = 0;
  CHECK(utf8Decode("\xC3\xA9", &i) == 0xE9 && i == 2);
  i = 0;
  CHECK(utf8Decode("\xE2\x82\xAC", &i) == 0x20AC && i == 3);
  i = 0;
  CHECK(utf8Decode("\xF0\x9F\x98\x80", &i) == 0x1F600 && i == 4);
  i = 0;
  CHECK(utf8Decode("\x80", &i) == kUtf8Invalid && i == 1);          // stray continuation
  i = 0;
  CHECK(utf8Decode("\xF0\x9F", &i) == kUtf8Invalid && i == 1);      // truncated at the NUL
  CHECK(utf8SeqLen(0xF0) == 4 && utf8SeqLen(0xE2) == 3 && utf8SeqLen('a') == 1);
  CHECK(utf8SeqLen(0x80) == 1 && utf8SeqLen(0xC0) == 1);

  // encode round trip
  char enc[8];
  const uint32_t cps[4] = {(uint32_t)'A', 0xE9u, 0x20ACu, 0x1F600u};
  for (int c = 0; c < 4; ++c) {
    const uint32_t cp = cps[c];
    const int n = utf8Encode(cp, enc);
    int j = 0;
    CHECK(n >= 1 && utf8Decode(enc, &j) == cp && j == n);
  }

  // measure: a,b,c + emoji + d,e,f -> 18 + 8 (space 6 + 2) + 18
  CHECK(measureUtf8("abc", 3, fakeGlyph, nullptr, 6) == 18);
  CHECK(measureUtf8("\xF0\x9F\x98\x80", 4, fakeGlyph, nullptr, 6) == 8);
  CHECK(measureUtf8("abc\xF0\x9F\x98\x80", 7, fakeGlyph, nullptr, 6) == 26);
  // a range that ends inside a sequence counts one placeholder, it never reads past `len`
  CHECK(measureUtf8("\xF0\x9F\x98\x80", 2, fakeGlyph, nullptr, 6) == 8);
  // the real string: 13 ASCII characters at 6 px + one 8 px placeholder
  CHECK(measureUtf8(msg, len, fakeGlyph, nullptr, 6) == 13 * 6 + 8);

  // wrapping with the real metrics: the word is wider than the line, so it splits between code
  // points; no line ever starts or ends inside a sequence and every line fits.
  WrapLine lines[kMaxWrapLines];
  char b[32];
  const int n = wrapText(msg, fakeUtf8Measure, nullptr, 60, 60, lines, kMaxWrapLines);
  CHECK(n == 2);
  // "[OM Bot]" (48 px), then the emoji plus PONG (32 px): the break is before the emoji, never in it
  CHECK(strcmp(lineText(msg, b, sizeof(b), lines[0]), "[OM Bot]") == 0);
  CHECK(lines[1].start == 9 && lines[1].len == 8);
  CHECK(memcmp(msg + lines[1].start, "\xF0\x9F\x98\x80", 4) == 0);
  for (int k = 0; k < n; ++k) {
    CHECK(onBoundary(msg, lines[k].start));
    CHECK(onBoundary(msg, lines[k].start + lines[k].len));
    CHECK(measureUtf8(msg + lines[k].start, lines[k].len, fakeGlyph, nullptr, 6) <= 60);
  }
  CHECK(wrapConsumed(msg, lines, n) == len);

  // exactly the emoji on a short line: it is one glyph, never split into two lines
  const char* justEmoji = "\xF0\x9F\x98\x80";
  CHECK(wrapText(justEmoji, fakeUtf8Measure, nullptr, 8, 8, lines, kMaxWrapLines) == 1);
  CHECK(lines[0].start == 0 && lines[0].len == 4);
  // one pixel too narrow: it still gets a line of its own (never a zero-byte line, never a loop)
  CHECK(wrapText(justEmoji, fakeUtf8Measure, nullptr, 7, 7, lines, kMaxWrapLines) == 1);
  CHECK(lines[0].len == 4);

  // the middle dot the mono face carries measures as a glyph, not as a placeholder
  CHECK(measureUtf8("a\xC2\xB7" "b", 4, fakeGlyph, nullptr, 6) == 18);
}

// The header and the selector: one rectangle set for the drawing and the taps.
static void testHeadAndSelector() {
  const Layout& L = l2();
  const ChatLayout c = chatLayoutFor(L, false, false, false, false);
  const ChatLayout s = chatLayoutFor(L, false, false, false, true);

  // --- the header replaces the chip strip in the same row --------------------
  CHECK(c.head.x == L.content.x && c.head.y == L.content.y && c.head.w == L.content.w);
  // EST-382: the Chat header is 26 px because the ADVERT key (drawn = tapped) must be 30 x 26; the
  // Nodes/Info header keeps kHeadH.
  CHECK(c.head.h == kChatHeadH);
  CHECK(c.msgs.y == c.head.bottom());
  CHECK(c.msgs.bottom() == c.compose.y);
  CHECK(!c.msgs.overlaps(c.head));

  // the gold dot sits left of the ADVERT key, inside the row, and the name stops before it
  CHECK(c.headDot.x + c.headDot.w == c.advert.x - kAdvertGap);
  CHECK(c.headDot.y >= c.head.y && c.headDot.bottom() <= c.head.bottom() - 1);
  CHECK(c.headDot.w == kElsewhereDot && c.headDot.h == kElsewhereDot);
  CHECK(c.headTitle.x == c.head.x + kHeadPadX);
  CHECK(c.headTitle.y == c.head.y && c.headTitle.h == c.head.h - 1);
  CHECK(c.headTitle.right() <= c.headDot.x - kElsewhereGap - kElsewhereW);
  CHECK(!c.headTitle.overlaps(c.headDot));

  // --- the compose line: the draft field only (no Send, no keyboard button) ---
  CHECK(c.field.x == c.compose.x + kComposePadX);
  CHECK(c.field.right() == c.compose.right() - kComposePadX);
  CHECK(c.field.w == c.compose.w - 2 * kComposePadX);
  CHECK(c.field.h == kFieldH);
  // the field sits inside the row, so the whole compose band is one tap target
  CHECK(c.field.y > c.compose.y && c.field.bottom() < c.compose.bottom());
  CHECK(!c.field.empty());

  // --- the dropdown covers exactly the message band, closed = empty -----------
  CHECK(c.selPanel.empty() && c.selList.empty() && c.selManage.empty());
  CHECK(!c.selector);
  CHECK(s.selector);
  CHECK(s.selPanel.x == s.msgs.x && s.selPanel.y == s.msgs.y);
  CHECK(s.selPanel.w == s.msgs.w && s.selPanel.h == s.msgs.h);
  CHECK(s.selManage.bottom() == s.selPanel.bottom());
  CHECK(s.selManage.h == kSelManageH);
  CHECK(s.selList.y == s.selPanel.y);
  CHECK(s.selList.bottom() == s.selManage.y);
  CHECK(!s.selList.overlaps(s.selManage));
  // the message band itself is unchanged by the dropdown: same rect, it is drawn under it
  CHECK(s.msgs.x == c.msgs.x && s.msgs.y == c.msgs.y && s.msgs.h == c.msgs.h);

  // --- the list arithmetic: labels, rows, the empty DM line, scroll -----------
  CHECK(selContentHeight(0, 0) == kSelSectionH * 2 + kSelEmptyH);
  CHECK(selContentHeight(4, 0) == kSelSectionH * 2 + 4 * kSelRowH + kSelEmptyH);
  CHECK(selContentHeight(4, 2) == kSelSectionH * 2 + 6 * kSelRowH);

  const Rect list = s.selList;
  CHECK(selMaxScroll(list, 1, 0) == 0);   // one channel fits the list, even with the DM label
  CHECK(selMaxScroll(list, 4, 0) == selContentHeight(4, 0) - list.h);

  // scroll 0: the first row sits one section label down, rows are 30 px apart
  CHECK(selRowTop(list, 4, 0, 0, 0) == list.y + kSelSectionH);
  CHECK(selRowTop(list, 4, 0, 1, 0) == list.y + kSelSectionH + kSelRowH);
  CHECK(selRowTop(list, 4, 0, 3, 0) == list.y + kSelSectionH + 3 * kSelRowH);
  // the first DM row follows the DM label after the channel rows
  CHECK(selRowTop(list, 4, 2, 4, 0) == list.y + kSelSectionH + 4 * kSelRowH + kSelSectionH);
  CHECK(selRowTop(list, 4, 2, 5, 0) == list.y + kSelSectionH + 4 * kSelRowH + kSelSectionH +
                                     kSelRowH);
  CHECK(selRowTop(list, 4, 2, 6, 0) == -1);   // no such entry

  // scrolling moves the rows by the same amount, and the list clamps to its content
  CHECK(selRowTop(list, 4, 0, 0, 10) == list.y + kSelSectionH - 10);
  CHECK(selRowTop(list, 4, 0, 0, 2 * kSelRowH) == list.y + kSelSectionH - 2 * kSelRowH);
  const int maxS = selMaxScroll(list, 4, 0);
  CHECK(selRowTop(list, 4, 0, 3, maxS) >= list.y - kSelRowH);

  // the tap bands: the top edge is inside, the bottom edge belongs to the next row
  const int top0 = selRowTop(list, 4, 0, 0, 0);
  CHECK(selRowAt(list, 4, 0, 0, top0) == 0);
  CHECK(selRowAt(list, 4, 0, 0, top0 + kSelRowH - 1) == 0);
  CHECK(selRowAt(list, 4, 0, 0, top0 + kSelRowH) == 1);
  CHECK(selRowAt(list, 4, 0, 0, list.y) == -1);            // the "Channels" label
  CHECK(selRowAt(list, 4, 0, 0, list.bottom() - 1) == 3);  // the last visible row
  CHECK(selRowAt(list, 4, 0, 0, list.bottom()) == -1);     // under the Manage row
  CHECK(selRowAt(list, 4, 0, 0, list.y - 1) == -1);
  // scrolled to the end: the rows moved up, the same row still answers at its own top
  CHECK(selRowAt(list, 4, 0, maxS, selRowTop(list, 4, 0, 2, maxS)) == 2);
  CHECK(selRowAt(list, 4, 0, maxS, selRowTop(list, 4, 0, 2, maxS) + kSelRowH) == 3);
  CHECK(selRowAt(list, 4, 0, maxS, list.bottom() - 1) == -1);   // the empty DM line, not a row

  // a tap on a row while it is scrolled half out of the list still needs a visible y
  CHECK(selRowAt(list, 4, 0, maxS, list.y - 5) == -1);

  // --- the sheet's rectangles are unchanged by the header --------------------
  const ChatLayout o = chatLayoutFor(L, true, false, false, false);
  CHECK(o.head.y == L.content.y && o.head.h == kChatHeadH);
  CHECK(o.msgs.y == o.head.bottom());
  CHECK(o.msgs.bottom() == o.sheet.y);
  CHECK(o.sheet.bottom() == L.height);
  CHECK(o.compose.empty() && o.field.empty());
  CHECK(o.selPanel.empty());

  // a tap on the header with the sheet open is still the header: the shell closes the sheet
  CHECK(chatLayoutFor(L, true, false, false, false).head.contains(10, L.content.y));
}

static void testComposeAndSheet() {
  const Layout& L = l2();
  const ChatLayout o = chatLayoutFor(L, true, false, false, false);

  // the sheet covers the tab bar completely and its contents are the four 22 px key rows
  CHECK(o.osk);
  CHECK(o.sheet.y < L.tabbar.y);
  CHECK(o.sheet.h == kOskPad * 2 + kOskDraftH + kOskDraftGap + 4 * kOskKeyH + 3 * kOskKeyGap);
  CHECK(o.draft.y == o.sheet.y + kOskPad);
  CHECK(o.draft.x == kOskPad);
  CHECK(o.draft.w == L.width - 2 * kOskPad);
  CHECK(o.keys[0][0].y == o.draft.bottom() + kOskDraftGap);
  CHECK(o.keys[3][0].bottom() + kOskPad == o.sheet.bottom());
  CHECK(o.replyBar.empty());

  // every row tiles exactly: no overlap, a 2 px gap, both edges flush, all keys 22 px tall
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
  CHECK(kOskKeyH == 22);
  CHECK(o.keyCount[0] == 10 && o.keyCount[1] == 9 && o.keyCount[2] == 9 && o.keyCount[3] == 6);

  // the bottom row of the letters page: 123, emoji, comma, space, dot, Send (EST-345)
  const Rect* row3 = o.keys[3];
  // 123, the smiley and Send are the wide keys (the last one takes the rounding remainder)
  CHECK(row3[0].w >= 50 && row3[1].w >= 50 && row3[5].w >= 50);
  CHECK(row3[0].w - row3[1].w <= 1 && row3[1].w - row3[0].w <= 1);
  CHECK(row3[5].w - row3[0].w <= 1 && row3[0].w - row3[5].w <= 1);
  CHECK(row3[3].w > row3[2].w && row3[3].w > row3[4].w);   // the space is the widest of the three
  CHECK(row3[2].w == row3[4].w);                       // comma and dot are one unit each

  // the numbers page has the same footprint and its own keys
  const ChatLayout num = chatLayoutFor(L, true, false, true, false);
  CHECK(num.sheet.y == o.sheet.y && num.sheet.h == o.sheet.h);
  CHECK(num.keyCount[0] == 10 && num.keyCount[1] == 10 && num.keyCount[2] == 10 &&
        num.keyCount[3] == 6);
  for (int r = 0; r < 4; ++r) {
    for (int i = 0; i < num.keyCount[r]; ++i) CHECK(num.keys[r][i].h == kOskKeyH);
  }

  // a reply bar rides above the draft and makes the sheet taller by exactly its own row
  const ChatLayout rep = chatLayoutFor(L, true, true, false, false);
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
  // the new bottom row: 123, emoji, comma, space, dot, Send
  CHECK(keyRow(false, 3, defs) == 6);
  CHECK(defs[0].kind == KeyKind::Mode && strcmp(defs[0].label, "123") == 0);
  CHECK(defs[1].kind == KeyKind::Emoji);
  CHECK(defs[2].kind == KeyKind::Char && defs[2].label[0] == ',');
  CHECK(defs[3].kind == KeyKind::Space);
  CHECK(defs[4].kind == KeyKind::Char && defs[4].label[0] == '.');
  CHECK(defs[5].kind == KeyKind::Send);
  CHECK(defs[3].units > defs[2].units);   // the space takes more of the row
  CHECK(keyRow(true, 0, defs) == 10 && defs[0].label[0] == '1');
  CHECK(keyRow(true, 1, defs) == 10 && defs[0].label[0] == '@');
  CHECK(keyRow(true, 2, defs) == 10);
  CHECK(defs[0].kind == KeyKind::Mode && defs[9].kind == KeyKind::Del);
  // the dot and the comma are on the 123 page too, and the smiley is one key away there as well
  CHECK(keyRow(true, 3, defs) == 6);
  CHECK(defs[0].kind == KeyKind::Mode && strcmp(defs[0].label, "abc") == 0);
  CHECK(defs[1].kind == KeyKind::Emoji);
  CHECK(defs[2].kind == KeyKind::Space);
  CHECK(defs[3].kind == KeyKind::Char && defs[3].label[0] == ',');
  CHECK(defs[4].kind == KeyKind::Char && defs[4].label[0] == '.');
  CHECK(defs[5].kind == KeyKind::Send);
  CHECK(keyRow(false, 9, defs) == 0);
  CHECK(keyRow(false, 0, nullptr) == 0);
}

static void testMessages() {
  const Layout& L = l2();
  const ChatLayout c = chatLayoutFor(L, false, false, false, false);

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
  const ChatLayout o = chatLayoutFor(L, true, false, false, false);
  CHECK(o.msgs.h < c.msgs.h);
  CHECK(msgMaxScroll(o.msgs, many, 20) > maxS);
}

// The header's gold dot counts unread in the chats the user is NOT reading.
static void testUnreadElsewhere() {
  ChatEntry chats[3];
  snprintf(chats[0].name, sizeof(chats[0].name), "#a");
  snprintf(chats[1].name, sizeof(chats[1].name), "#b");
  snprintf(chats[2].name, sizeof(chats[2].name), "DM: C");
  chats[2].dm = true;
  chats[0].unread = 3;
  chats[1].unread = 2;
  chats[2].unread = 5;

  CHECK(chatUnreadElsewhere(chats, 3, 0) == 7);
  CHECK(chatUnreadElsewhere(chats, 3, 2) == 5);
  CHECK(chatUnreadElsewhere(chats, 0, 0) == 0);
  CHECK(chatUnreadElsewhere(nullptr, 3, 0) == 0);

  chats[0].unread = 0;
  chats[1].unread = 0;
  chats[2].unread = 0;
  CHECK(chatUnreadElsewhere(chats, 3, 1) == 0);   // nothing waiting: no dot is drawn
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

// The selector follows the finger: a fingertip moving UP by N px moves the list content up by N px,
// clamped (with kSelGive of stretch past either end, snapped back on release). This drives the same
// helper the device's drag branch calls (main.cpp: om::ui::selDragScroll).
static void testSelectorDrag() {
  const Layout& L = l2();
  const ChatLayout s = chatLayoutFor(L, false, false, false, true);
  const Rect list = s.selList;
  const int maxS = selMaxScroll(list, 6, 0);
  CHECK(maxS > 0);

  // The scroll grew by exactly the finger's travel, so every row moved up by the same N px.
  const int top0 = selRowTop(list, 6, 0, 1, 0);
  int scroll = selDragScroll(0, 12, maxS);
  CHECK(scroll == 12);
  CHECK(selRowTop(list, 6, 0, 1, scroll) == top0 - 12);
  scroll = selDragScroll(0, 37, maxS);
  CHECK(scroll == 37);
  CHECK(selRowTop(list, 6, 0, 1, scroll) == top0 - 37);

  // The sign is the fixed one: a finger moving DOWN must not scroll further up. (The old code did
  // `base - dy`, which moved the list the other way and is what Filip saw on the device.)
  CHECK(selDragScroll(20, -8, maxS) == 12);
  CHECK(selRowTop(list, 6, 0, 1, 12) == top0 - 12);

  // A second sample of the same gesture is measured from the press, not from the previous sample.
  CHECK(selDragScroll(0, 30, maxS) == 30);
  CHECK(selDragScroll(0, 40, maxS) == 40);

  // Clamped: kSelGive of stretch past the top, and the same past the end of the list.
  CHECK(selDragScroll(0, -4, maxS) == -4);
  CHECK(selDragScroll(0, -kSelGive, maxS) == -kSelGive);
  CHECK(selDragScroll(0, -kSelGive - 9, maxS) == -kSelGive);
  CHECK(selDragScroll(maxS, 5, maxS) == maxS + 5);
  CHECK(selDragScroll(0, maxS + kSelGive, maxS) == maxS + kSelGive);
  CHECK(selDragScroll(0, maxS + kSelGive + 25, maxS) == maxS + kSelGive);

  // Release snaps the stretch back into [0, max]: the same clamp the loop's release path applies.
  int released = selDragScroll(0, -kSelGive, maxS);
  if (released < 0) released = 0;
  CHECK(released == 0);
  released = selDragScroll(0, maxS + kSelGive, maxS);
  if (released > maxS) released = maxS;
  CHECK(released == maxS);

  // A list that cannot scroll stays put (the stretch is the only movement it allows).
  CHECK(selDragScroll(0, 0, 0) == 0);
  CHECK(selDragScroll(0, 30, 0) == kSelGive);
}

// The composer's byte budget: MeshCore's MAX_TEXT_LEN counts "<our name>: ", a reply spends "@Name "
// on top, and a key that would pass the limit does nothing (never a silent cut on send).
static void testComposeBudget() {
  CHECK(kMeshTextMax == 160);                       // MAX_TEXT_LEN, asserted against MeshCore on device
  CHECK(kMeshTextCap == 161);
  CHECK(kMeshOwnPrefix == (int)strlen("OM Lite") + 2);
  const int allow = composeAllowance(false, nullptr);
  CHECK(allow == kMeshTextMax - kMeshOwnPrefix);
  CHECK(allow == 151);

  // A reply pays "@<name> " first, so the draft's budget shrinks by exactly that many bytes.
  const char* name = "OM Bot Ljubljana";
  CHECK(composeAllowance(true, name) == allow - (1 + (int)strlen(name) + 1));
  CHECK(composeAllowance(true, "") == allow);        // replying to nobody is no reply
  CHECK(composeAllowance(false, name) == allow);     // a name without a reply costs nothing

  // Fill the draft to the limit, then one more key: refused, the draft is untouched.
  char draft[kMeshTextCap];
  draft[0] = 0;
  for (int i = 0; i < allow; ++i) CHECK(composeAppend(draft, (int)sizeof(draft), "x", allow));
  CHECK((int)strlen(draft) == allow);
  CHECK(!composeAppend(draft, (int)sizeof(draft), "y", allow));
  CHECK((int)strlen(draft) == allow);
  CHECK(composeRemaining(allow, (int)strlen(draft)) == 0);
  CHECK(composeRemaining(allow, allow - 20) == 20);   // the counter appears from 20 bytes left
  CHECK(composeRemaining(allow, allow + 5) == 0);      // never negative

  // --- a multi-byte character at the limit -----------------------------------
  const char* emoji = "\xF0\x9F\x98\x80";   // U+1F600, 4 bytes
  CHECK(strlen(emoji) == 4);
  draft[0] = 0;
  for (int i = 0; i < allow - 5; ++i) composeAppend(draft, (int)sizeof(draft), "x", allow);
  CHECK(composeRemaining(allow, (int)strlen(draft)) == 5);
  // 4 of the 5 free bytes: the whole sequence goes in and one byte is left
  CHECK(composeAppend(draft, (int)sizeof(draft), emoji, allow));
  CHECK((int)strlen(draft) == allow - 1);
  CHECK(strcmp(draft + strlen(draft) - 4, emoji) == 0);
  // 1 byte free: refused as a unit, so no broken sequence is ever written
  CHECK(!composeAppend(draft, (int)sizeof(draft), emoji, allow));
  CHECK((int)strlen(draft) == allow - 1);
  CHECK(strcmp(draft + strlen(draft) - 4, emoji) == 0);

  // Backspace removes the whole code point, not one byte of it.
  composeBackspace(draft);
  CHECK((int)strlen(draft) == allow - 5);
  composeBackspace(draft);
  CHECK((int)strlen(draft) == allow - 6);
  composeBackspace(nullptr);   // never dereferenced

  // --- COPY: a message over the budget is cut on a code point boundary --------
  // 150 'x' then the 4-byte emoji. The budget is 151, so the ellipsis takes 3 and 148 bytes may be
  // copied: the emoji straddles the cut, so the copy stops before it instead of writing 3 of its 4
  // bytes (a broken sequence the screen would draw as a placeholder box).
  char src[256];
  memset(src, 'x', 150);
  memcpy(src + 150, emoji, 4);
  src[154] = 0;
  char out[kMeshTextCap];
  composeCopy(out, (int)sizeof(out), src, allow);
  CHECK((int)strlen(out) == 148 + 3);          // 148 bytes that fit, then "..."
  CHECK(strcmp(out + 148, "...") == 0);
  for (int i = 0; i < 148; ++i) CHECK(out[i] == 'x');
  // a source that fits is copied whole, without an ellipsis
  composeCopy(out, (int)sizeof(out), "hello", allow);
  CHECK(strcmp(out, "hello") == 0);
  composeCopy(out, (int)sizeof(out), "", allow);
  CHECK(out[0] == 0);
  composeCopy(out, (int)sizeof(out), nullptr, allow);
  CHECK(out[0] == 0);
}

// The unread badge: one red shape for every count, a pixel digit face, and a box that never touches
// the selected tab's gold line nor the tab label.
static void testBadge() {
  const Rect tab = l2().tab(Tab::Chat);
  const Rect one = countBadgeRect(tab, 1);
  const Rect two = countBadgeRect(tab, 2);
  const Rect three = countBadgeRect(tab, 3);

  // Geometry from theme.h: a 13 px circle, a 19x13 pill for two digits, a 23x13 pill for "99+",
  // all on the same centre (EST-364).
  CHECK(one.w == kBadgeD && one.h == kBadgeD);
  CHECK(one.w == 13 && one.h == 13);
  CHECK(two.w == kBadgePillW && two.h == kBadgeD);
  CHECK(two.w == 19 && two.h == 13);
  CHECK(three.w == kBadgePill3W && three.h == kBadgeD);
  CHECK(three.w == 23 && three.h == 13);
  CHECK(one.x + one.w / 2 == two.x + two.w / 2);
  CHECK(one.x + one.w / 2 == three.x + three.w / 2);
  CHECK(one.y == two.y && one.bottom() == two.bottom());
  CHECK(one.y == three.y && one.bottom() == three.bottom());

  // Clear of the gold selected-tab line (the tab's top 2 px) and of the tab label's line box.
  CHECK(one.y >= tab.y + kTabTopLine + 1);
  CHECK(one.y > tab.y + kTabTopLine);
  // bottom() is exclusive: the last red row is one pixel above the label's line box
  CHECK(one.bottom() <= tab.bottom() - kTabLabelPadB - kSansTabPx / 2);
  CHECK(one.bottom() <= tab.bottom());
  // Inside the tab, to the right of its centre: the icon's top-right corner.
  CHECK(one.x >= tab.x && one.right() <= tab.right());
  CHECK(two.x >= tab.x && two.right() <= tab.right());
  CHECK(three.x >= tab.x && three.right() <= tab.right());
  CHECK(one.x + one.w / 2 > tab.x + tab.w / 2);

  // The count rule (EST-364): the exact number up to 99, then "99+".
  char b[4];
  CHECK(badgeText(0, b, sizeof(b)) == 0 && b[0] == 0);
  CHECK(badgeText(1, b, sizeof(b)) == 1 && strcmp(b, "1") == 0);
  CHECK(badgeText(7, b, sizeof(b)) == 1 && strcmp(b, "7") == 0);
  CHECK(badgeText(9, b, sizeof(b)) == 1 && strcmp(b, "9") == 0);
  CHECK(badgeText(10, b, sizeof(b)) == 2 && strcmp(b, "10") == 0);
  CHECK(badgeText(42, b, sizeof(b)) == 2 && strcmp(b, "42") == 0);
  CHECK(badgeText(99, b, sizeof(b)) == 2 && strcmp(b, "99") == 0);
  CHECK(badgeText(100, b, sizeof(b)) == 3 && strcmp(b, "99+") == 0);
  CHECK(badgeText(1000, b, sizeof(b)) == 3 && strcmp(b, "99+") == 0);
  CHECK(badgeText(1, nullptr, 4) == 0);
  CHECK(badgeText(1, b, 1) == 0);
  // Two digits fit the 19 px pill and three the 23 px one (5 px glyphs, 1 px gaps).
  CHECK(badgeTextWidth(2) == 11 && badgeTextWidth(3) == 17);
  CHECK(badgeTextWidth(2) <= kBadgePillW - 8);
  CHECK(badgeTextWidth(3) <= kBadgePill3W - 6);

  // The pixel face: 5x7, only the five low bits used, every digit and the plus present, and both
  // glyphs of "9+" fit inside the pill's rounded ends.
  CHECK(kBadgeGlyphW == 5 && kBadgeGlyphH == 7);
  CHECK(badgeTextWidth(1) == 5);
  CHECK(badgeTextWidth(2) == 5 + kBadgeGlyphGap + 5);
  CHECK(badgeTextWidth(2) <= kBadgePillW - 8);
  for (int i = 0; i < kBadgeFaceCount; ++i) {
    CHECK(badgeGlyphRows(kBadgeFace[i].ch) == kBadgeFace[i].rows);
    for (int r = 0; r < kBadgeGlyphH; ++r) CHECK((kBadgeFace[i].rows[r] & 0xE0) == 0);
  }
  for (char ch = '0'; ch <= '9'; ++ch) CHECK(badgeGlyphRows(ch) != nullptr);
  CHECK(badgeGlyphRows('+') != nullptr);
  // '0' is a glyph now (a two-digit count needs it) but a count of zero still draws nothing.
  CHECK(badgeText(0, b, sizeof(b)) == 0);
}

// The message sheet: the rows each message kind gets (and the ones that are left out because they
// cannot work), the rectangles the drawing and the taps share, and the packet-path naming. This
// drives the same src/ui/message_sheet.cpp the device draws with.
static const char* fakeResolve(void* ctx, const uint8_t* hash, uint8_t len) {
  (void)ctx;
  if (len >= 1 && hash != nullptr && hash[0] == 0xAA) return "OM-RPTR";
  return "";
}

static void testMessageSheet() {
  const Layout& L = l2();

  // --- someone else's message, with every fact available ----------------------
  SheetContext c;
  c.mine = false;
  c.senderHasKey = true;
  c.senderKnown = true;
  c.senderHasPosition = true;
  c.pathKnown = true;
  c.pathText = "OM-RPTR > 7f";
  SheetRow rows[kMaxSheetRows];
  int n = messageSheetRows(c, rows, kMaxSheetRows);
  CHECK(n == 7);
  CHECK(rows[0].action == SheetAction::Reply);
  CHECK(rows[1].action == SheetAction::DirectMessage);
  CHECK(rows[2].action == SheetAction::CopyText);
  CHECK(rows[3].action == SheetAction::Mention);
  CHECK(rows[4].action == SheetAction::NodeDetails);
  CHECK(rows[5].action == SheetAction::ShowOnMap);
  CHECK(rows[6].action == SheetAction::Path);
  CHECK(strcmp(rows[0].label, "Reply") == 0);
  CHECK(strcmp(rows[6].value, "OM-RPTR > 7f") == 0);

  // --- rows that cannot work are LEFT OUT, never greyed -----------------------
  c = SheetContext();
  n = messageSheetRows(c, rows, kMaxSheetRows);
  CHECK(n == 3);   // Reply, Copy text, Mention: no key for a DM, unknown sender, no path
  CHECK(rows[0].action == SheetAction::Reply);
  CHECK(rows[1].action == SheetAction::CopyText);
  CHECK(rows[2].action == SheetAction::Mention);

  // ... the DM row exists only for a sender whose key we hold (EST-342)
  c = SheetContext();
  c.senderHasKey = true;
  n = messageSheetRows(c, rows, kMaxSheetRows);
  CHECK(n == 4);
  CHECK(rows[1].action == SheetAction::DirectMessage);

  // ... Node details only for a known contact, Show on map only with a position
  c = SheetContext();
  c.senderKnown = true;
  n = messageSheetRows(c, rows, kMaxSheetRows);
  CHECK(n == 4);
  CHECK(rows[3].action == SheetAction::NodeDetails);
  c.senderHasPosition = true;
  n = messageSheetRows(c, rows, kMaxSheetRows);
  CHECK(n == 5);
  CHECK(rows[4].action == SheetAction::ShowOnMap);

  // ... Path only for a packet that carried a repeater, and it carries the text
  c = SheetContext();
  c.pathKnown = true;
  c.pathText = "1122 > OM-RPTR";
  n = messageSheetRows(c, rows, kMaxSheetRows);
  CHECK(n == 4);
  CHECK(rows[3].action == SheetAction::Path);
  CHECK(strcmp(rows[3].value, "1122 > OM-RPTR") == 0);

  // --- our own message: Copy text, Resend, Heard repeats ----------------------
  c = SheetContext();
  c.mine = true;
  c.senderHasKey = true;
  c.senderKnown = true;
  c.senderHasPosition = true;
  c.pathKnown = true;
  n = messageSheetRows(c, rows, kMaxSheetRows);
  CHECK(n == 3);
  CHECK(rows[0].action == SheetAction::CopyText);
  CHECK(rows[1].action == SheetAction::Resend);
  CHECK(rows[2].action == SheetAction::HeardRepeats);
  CHECK(strcmp(rows[2].label, "Heard repeats") == 0);
  for (int i = 0; i < n; ++i) {
    CHECK(rows[i].action != SheetAction::Reply);
    CHECK(rows[i].action != SheetAction::Mention);
  }

  // --- the cap: never more than the array holds -------------------------------
  c = SheetContext();
  c.senderHasKey = true;
  n = messageSheetRows(c, rows, 2);
  CHECK(n == 2);
  CHECK(messageSheetRows(c, nullptr, 4) == 0);
  CHECK(messageSheetRows(c, rows, 0) == 0);

  // --- the geometry: a bottom sheet, seven rows, never over the status bar ----
  const MessageSheetLayout g = messageSheetLayout(L, 7);
  CHECK(g.rowCount == 7);
  CHECK(g.panel.x == 0 && g.panel.w == L.width);
  CHECK(g.panel.bottom() == L.height);
  CHECK(g.panel.h == 2 * kSheetPadY + kSheetHeadH + 7 * kSheetRowH);
  CHECK(g.panel.y == L.height - g.panel.h);
  CHECK(g.panel.y >= kStatusH);   // the tallest sheet still leaves the status bar clear
  CHECK(!g.panel.overlaps(L.status));
  CHECK(g.head.x == kSheetPadX && g.head.w == L.width - 2 * kSheetPadX);
  CHECK(g.head.y == g.panel.y + kSheetPadY && g.head.h == kSheetHeadH);
  CHECK(g.head.bottom() == g.rows[0].y);
  for (int i = 0; i < 7; ++i) {
    CHECK(g.rows[i].x == kSheetPadX && g.rows[i].w == L.width - 2 * kSheetPadX);
    CHECK(g.rows[i].h == kSheetRowH);
    if (i > 0) CHECK(g.rows[i].y == g.rows[i - 1].bottom());
    CHECK(!g.rows[i].overlaps(g.head));
  }
  CHECK(g.rows[6].bottom() == g.panel.bottom() - kSheetPadY);

  // --- the hit rects are the drawn rows --------------------------------------
  CHECK(messageSheetRowAt(g, g.rows[0].x, g.rows[0].y) == 0);
  CHECK(messageSheetRowAt(g, g.rows[0].x, g.rows[0].y + kSheetRowH - 1) == 0);
  CHECK(messageSheetRowAt(g, g.rows[1].x, g.rows[1].y) == 1);
  CHECK(messageSheetRowAt(g, g.rows[6].x, g.rows[6].y + kSheetRowH - 1) == 6);
  CHECK(messageSheetRowAt(g, g.rows[0].x, g.head.y) == -1);          // the header
  CHECK(messageSheetRowAt(g, g.rows[0].x, g.rows[6].bottom()) == -1); // under the last row
  CHECK(messageSheetRowAt(g, g.rows[0].x - 1, g.rows[0].y) == -1);    // left pad
  CHECK(messageSheetRowAt(g, 5, g.rows[0].y) == -1);

  // three rows (our own message) are shorter, so the message band above stays visible
  const MessageSheetLayout own = messageSheetLayout(L, 3);
  CHECK(own.panel.bottom() == L.height);
  CHECK(own.panel.h == 2 * kSheetPadY + kSheetHeadH + 3 * kSheetRowH);
  CHECK(own.panel.y > g.panel.y);
  CHECK(messageSheetRowAt(own, own.rows[0].x, g.rows[0].y) == -1);
  CHECK(messageSheetRowAt(own, own.rows[0].x, own.rows[0].y) == 0);
  CHECK(messageSheetLayout(L, 0).panel.h == 2 * kSheetPadY + kSheetHeadH);
  CHECK(messageSheetLayout(L, 99).rowCount == kMaxSheetRows);

  // --- the path naming: a known repeater by name, the rest as hex -------------
  char out[64];
  const uint8_t path2[] = {0xAA, 0xBB, 0x3F, 0xA2};   // two 2-byte hashes
  CHECK(formatMessagePath(out, sizeof(out), path2, 2, 2, fakeResolve, nullptr) ==
        (int)strlen("OM-RPTR > 3fa2"));
  CHECK(strcmp(out, "OM-RPTR > 3fa2") == 0);

  // the hex width follows the packet's own hash size: 1 byte = 2 digits, 3 = 6
  const uint8_t path1[] = {0xAA, 0x07};
  formatMessagePath(out, sizeof(out), path1, 1, 2, fakeResolve, nullptr);
  CHECK(strcmp(out, "OM-RPTR > 07") == 0);
  const uint8_t path3[] = {0xAA, 0xBB, 0xCC, 0x01, 0x02, 0x03};
  formatMessagePath(out, sizeof(out), path3, 3, 2, fakeResolve, nullptr);
  CHECK(strcmp(out, "OM-RPTR > 010203") == 0);

  // an unknown hash is lower-case hex, no resolver is fine, an empty path is ""
  const uint8_t unknown[] = {0x11, 0x22};
  formatMessagePath(out, sizeof(out), unknown, 2, 1, fakeResolve, nullptr);
  CHECK(strcmp(out, "1122") == 0);
  formatMessagePath(out, sizeof(out), unknown, 2, 1, nullptr, nullptr);
  CHECK(strcmp(out, "1122") == 0);
  CHECK(formatMessagePath(out, sizeof(out), nullptr, 2, 0, fakeResolve, nullptr) == 0);
  CHECK(formatMessagePath(out, sizeof(out), path2, 2, 0, fakeResolve, nullptr) == 0);
  CHECK(out[0] == 0);
  CHECK(formatMessagePath(out, sizeof(out), path2, 0, 2, fakeResolve, nullptr) == 0);
  CHECK(formatMessagePath(out, sizeof(out), path2, 4, 2, fakeResolve, nullptr) == 0);

  // the cap is never overrun: a name wider than the buffer writes nothing and says so
  const uint8_t wide[] = {0xAA, 0xBB};
  CHECK(formatMessagePath(out, 4, wide, 2, 1, fakeResolve, nullptr) == 0);
  CHECK(out[0] == 0);
  CHECK(formatMessagePath(out, sizeof(out), wide, 2, 1, fakeResolve, nullptr) == 7);

  // 63 bytes of path (21 hashes of 3) is the most a packet can carry; more is refused
  uint8_t longPath[66];
  memset(longPath, 0x55, sizeof(longPath));
  CHECK(formatMessagePath(out, sizeof(out), longPath, 3, 21, fakeResolve, nullptr) > 0);
  CHECK(strlen(out) < sizeof(out));
  CHECK(formatMessagePath(out, sizeof(out), longPath, 3, 22, fakeResolve, nullptr) == 0);
}

// The keyboard's bottom edge (EST-338). The drawn keys keep the mock's 4 px side pad, but the TOUCH
// rectangles reach the screen edges and the bottom row reaches the floor. A touch in the 2 px gap
// between keys is inside the sheet and does nothing; only a touch above the sheet closes it.
static void testKeyboardEdges() {
  const Layout& L = l2();
  const ChatLayout g = chatLayoutFor(L, true, false, false, false);
  KeyDef defs[10];
  const int n = keyRow(false, 3, defs);
  CHECK(n == 6);
  CHECK(defs[0].kind == KeyKind::Mode && strcmp(defs[0].label, "123") == 0);
  CHECK(defs[1].kind == KeyKind::Emoji);
  CHECK(defs[3].kind == KeyKind::Space);
  CHECK(defs[5].kind == KeyKind::Send);
  CHECK(g.keyCount[3] == 6);

  // The DRAWN rectangles did not move: the 4 px side pad and the 4 px pad under the last row stay.
  CHECK(g.keys[3][0].x == kOskPad);
  CHECK(g.keys[3][0].bottom() == (int16_t)(L.height - kOskPad));
  CHECK(g.keys[3][5].right() == (int16_t)(L.width - kOskPad));

  // The TOUCH rectangles reach the floor and the side edges on every row.
  for (int r = 0; r < 4; ++r) {
    CHECK(g.keyCount[r] > 0);
    CHECK(g.keyHit[r][0].x == 0);
    CHECK(g.keyHit[r][g.keyCount[r] - 1].right() == L.width);
  }
  for (int c = 0; c < g.keyCount[3]; ++c) CHECK(g.keyHit[3][c].bottom() == L.height);

  // y = 239 (the last pixel row) still hits Send, space and 123.
  const int xSend = g.keyHit[3][4].x + g.keyHit[3][4].w / 2;
  const int xSpace = g.keyHit[3][2].x + g.keyHit[3][2].w / 2;
  const int x123 = g.keyHit[3][0].x + g.keyHit[3][0].w / 2;
  KeyHit h = keyAt(g, false, xSend, 239);
  CHECK(h.row == 3 && h.col == 4);
  h = keyAt(g, false, xSpace, 239);
  CHECK(h.row == 3 && h.col == 2);
  h = keyAt(g, false, x123, 239);
  CHECK(h.row == 3 && h.col == 0);

  // A raw y on or past the bottom edge is clamped into the screen before the hit test.
  int cx = xSend, cy = 240;
  clampTouchToScreen(cx, cy, L.width, L.height);
  CHECK(cy == L.height - 1);
  h = keyAt(g, false, cx, cy);
  CHECK(h.row == 3 && h.col == 4);
  cx = xSpace;
  cy = 999;
  clampTouchToScreen(cx, cy, L.width, L.height);
  CHECK(cy == L.height - 1);
  h = keyAt(g, false, cx, cy);
  CHECK(h.row == 3 && h.col == 2);

  // A 2 px gap between two keys is inside the sheet but hits no key: it does nothing (no close).
  const int gapX = g.keyHit[3][0].right();
  CHECK(keyAt(g, false, gapX, 230).row < 0);
  CHECK(!chatSheetClosesAt(g, gapX, 230));
  // A gap between two rows: also nothing.
  const int gapY = g.keys[2][0].bottom();
  CHECK(keyAt(g, false, xSpace, gapY).row < 0);
  CHECK(!chatSheetClosesAt(g, xSpace, gapY));

  // Only a touch ABOVE the sheet closes it.
  CHECK(chatSheetClosesAt(g, 100, g.sheet.y - 1));
  CHECK(!chatSheetClosesAt(g, 100, g.sheet.y));
  CHECK(!chatSheetClosesAt(g, 100, L.height - 1));
}

// Tap-away (EST-339). For every overlay: a press outside it is classified as that overlay and
// OUTSIDE, so the shell closes it and spends the gesture; a press inside is an ordinary gesture. The
// message band behind the keyboard used to answer (Filip's "I pressed away, in the chat area, and it
// opened a chat menu"), so the exact point that would be a message is checked to be "outside".
static void testOverlayTapAway() {
  const Layout& L = l2();

  // No overlay: nothing to dismiss, every press acts.
  {
    const ChatLayout g = chatLayoutFor(L, false, false, false, false);
    const ChatOverlayPress p = chatOverlayPress(g, false, Rect{}, 100, 100);
    CHECK(p.overlay == ChatOverlay::None);
    CHECK(p.inside);
  }

  // The keyboard sheet: a press on the message band above it is OUTSIDE (this is the bug), a press
  // on a key or the draft is inside, and the header above is outside too.
  {
    const ChatLayout g = chatLayoutFor(L, true, false, false, false);
    const int bandY = (g.head.bottom() + g.sheet.y) / 2;
    CHECK(g.msgs.contains(100, bandY));   // a point that used to hit a message behind the sheet
    const ChatOverlayPress outside = chatOverlayPress(g, false, Rect{}, 100, bandY);
    CHECK(outside.overlay == ChatOverlay::Keyboard);
    CHECK(!outside.inside);

    const ChatOverlayPress head = chatOverlayPress(g, false, Rect{}, g.head.x + 4, g.head.y + 4);
    CHECK(head.overlay == ChatOverlay::Keyboard);
    CHECK(!head.inside);

    const ChatOverlayPress key = chatOverlayPress(g, false, Rect{}, g.keys[1][1].x + 2,
                                                  g.keys[1][1].y + 2);
    CHECK(key.overlay == ChatOverlay::Keyboard);
    CHECK(key.inside);
    const ChatOverlayPress gap =
        chatOverlayPress(g, false, Rect{}, g.keyHit[3][0].right(), g.keys[3][0].y + 2);
    CHECK(gap.overlay == ChatOverlay::Keyboard);
    CHECK(gap.inside);   // a gap inside the sheet: inside (it just does nothing on the release)
  }

  // The selector: a press on its panel is inside, a press on the header or the compose row is out.
  {
    const ChatLayout g = chatLayoutFor(L, false, false, false, true);
    const ChatOverlayPress inside =
        chatOverlayPress(g, false, Rect{}, g.selPanel.x + 4, g.selPanel.y + 4);
    CHECK(inside.overlay == ChatOverlay::Selector);
    CHECK(inside.inside);
    const ChatOverlayPress head = chatOverlayPress(g, false, Rect{}, g.head.x + 4, g.head.y + 4);
    CHECK(head.overlay == ChatOverlay::Selector);
    CHECK(!head.inside);
    const ChatOverlayPress compose =
        chatOverlayPress(g, false, Rect{}, g.compose.x + 4, g.compose.y + 4);
    CHECK(compose.overlay == ChatOverlay::Selector);
    CHECK(!compose.inside);
    // ... and the message band is inside the panel, not behind it.
    const ChatOverlayPress band = chatOverlayPress(g, false, Rect{}, g.selPanel.x + 4,
                                                   g.selPanel.bottom() - 2);
    CHECK(band.overlay == ChatOverlay::Selector);
    CHECK(band.inside);
  }

  // The message sheet: inside its panel (a row or the header), outside above it. The message band
  // above the panel is again the exact point that must not be pressed.
  {
    const ChatLayout g = chatLayoutFor(L, false, false, false, false);
    const MessageSheetLayout m = messageSheetLayout(L, 3);
    const ChatOverlayPress row =
        chatOverlayPress(g, true, m.panel, m.rows[0].x + 4, m.rows[0].y + 4);
    CHECK(row.overlay == ChatOverlay::MessageSheet);
    CHECK(row.inside);
    const ChatOverlayPress head = chatOverlayPress(g, true, m.panel, m.head.x + 4, m.head.y + 4);
    CHECK(head.overlay == ChatOverlay::MessageSheet);
    CHECK(head.inside);
    const int aboveY = m.panel.y - 4;
    CHECK(g.msgs.contains(100, aboveY));
    const ChatOverlayPress above = chatOverlayPress(g, true, m.panel, 100, aboveY);
    CHECK(above.overlay == ChatOverlay::MessageSheet);
    CHECK(!above.inside);
    // The sheet is on top of everything: even with the keyboard flag set (it is closed with the
    // sheet) the sheet is the one that answers, so a key under the panel is a sheet touch, not a
    // key press, and a touch above the panel is still "outside".
    const ChatLayout gk = chatLayoutFor(L, true, false, false, false);
    const ChatOverlayPress overKey =
        chatOverlayPress(gk, true, m.panel, gk.keys[3][0].x + 2, gk.keys[3][0].y + 2);
    CHECK(overKey.overlay == ChatOverlay::MessageSheet);
    CHECK(overKey.inside);   // the sheet's band covers the keyboard's bottom rows
    const ChatOverlayPress overBand =
        chatOverlayPress(gk, true, m.panel, 100, gk.sheet.y - 2);
    CHECK(overBand.overlay == ChatOverlay::MessageSheet);
    CHECK(!overBand.inside);
  }
}

// EST-345 item 4: the last list row must be FULLY visible at max scroll, and its bottom must end
// above the pinned "Manage channels" row. The ticket reported the second line sliding under that
// row; this checks every count the app can show, not one screenshot.
static void testSelectorLastRowVisible() {
  const Layout& L = l2();
  const ChatLayout s = chatLayoutFor(L, false, false, false, true);
  const Rect list = s.selList;
  CHECK(list.bottom() == s.selManage.y);      // the scrollable height ends at the pinned row's top
  CHECK(!list.overlaps(s.selManage));
  for (int channels = 0; channels <= 8; ++channels) {
    for (int dms = 0; dms <= 4; ++dms) {
      if (channels + dms == 0) continue;
      const int content = selContentHeight(channels, dms);
      const int maxS = selMaxScroll(list, channels, dms);
      const int last = channels + dms - 1;
      const int top = selRowTop(list, channels, dms, last, maxS);
      CHECK(top >= list.y);
      if (content > list.h) {
        CHECK(maxS == content - list.h);
        // The content's last pixel lands on the scrolling viewport's floor, which is the pinned
        // row's top. With DMs present the last row is the content's end, so its 30 px touch that
        // floor; with no DMs the "No direct messages yet" line is what follows it.
        CHECK(list.y + content - maxS == list.bottom());
        if (dms > 0) CHECK(top + kSelRowH == list.bottom());
      } else {
        CHECK(maxS == 0);
      }
      CHECK(top + kSelRowH <= list.bottom());
      CHECK(top + kSelRowH <= s.selManage.y);
      CHECK(selRowAt(list, channels, dms, maxS, top) == last);
      CHECK(selRowAt(list, channels, dms, maxS, top + kSelRowH - 1) == last);  // the second line
      CHECK(selRowAt(list, channels, dms, maxS, s.selManage.y) == -1);
    }
  }
}

// EST-345: the emoji page takes the key rows' rectangle, the 16-cell Recent strip plus Send sits
// above a 3-row 22 px grid, and its drawn cell is the cell the hit test answers.
static void testEmojiPad() {
  const Layout& L = l2();
  const ChatLayout g = chatLayoutFor(L, true, false, false, false);
  const EmojiPadLayout e = emojiPadLayout(L, g, 16, 0, 0, 40);
  CHECK(!e.area.empty());
  CHECK(e.area.y == g.keys[0][0].y);
  CHECK(e.area.bottom() == g.keys[3][0].bottom());
  CHECK(e.cols == 12);
  CHECK(e.rows == 3);
  CHECK(e.grid.h >= e.rows * kEmojiCell);
  CHECK(e.recent[0].w == kEmojiRecentCell && e.recent[0].h == kEmojiRecentCell);
  CHECK(e.recent[15].right() <= e.send.x);
  CHECK(e.send.right() <= e.recentStrip.right());
  CHECK(e.recentStrip.bottom() <= e.grid.y);
  CHECK(e.cat[4].bottom() <= e.abc.y);

  // Every drawn rectangle answers at its own centre, in the rail, the strip, the grid and Send.
  const Rect c0 = emojiPadCell(e, 0);
  CHECK(c0.x == e.grid.x && c0.y == e.grid.y);
  CHECK(emojiPadHit(e, e.cat[2].x + 2, e.cat[2].y + 2).cat == 2);
  CHECK(emojiPadHit(e, e.recent[3].x + 2, e.recent[3].y + 2).recent == 3);
  CHECK(emojiPadHit(e, e.send.x + 2, e.send.y + 2).send);
  CHECK(emojiPadHit(e, e.abc.x + 2, e.abc.y + 2).abc);
  CHECK(emojiPadHit(e, c0.x + 2, c0.y + 2).cell == 0);
  CHECK(emojiPadHit(e, emojiPadCell(e, 5).x + 2, emojiPadCell(e, 5).y + 2).cell == 5);
  CHECK(emojiPadHit(e, e.grid.x - 2, e.grid.y + 2).cell == -1);   // the rail is not the grid

  // A 40-entry group needs two rows of scroll; at max the last cell ends on the viewport floor.
  const int maxS = emojiPadMaxScroll(e, 40);
  CHECK(maxS > 0);
  CHECK(maxS == emojiPadContentHeight(e, 40) - e.grid.h);
  const EmojiPadLayout e2 = emojiPadLayout(L, g, 16, 0, maxS, 40);
  const Rect last = emojiPadCell(e2, 39);
  CHECK(last.bottom() == e2.grid.bottom());
  CHECK(emojiPadHit(e2, last.x + 2, last.y + 2).cell == 39);
  CHECK(emojiPadMaxScroll(e, 12) == 0);   // a group that fits does not scroll
  CHECK(emojiPadLayout(L, g, 0, 0, 0, 0).area.empty() == false);   // an empty group still lays out
}

int main() {
  testWrap();
  testUtf8();
  testHeadAndSelector();
  testComposeAndSheet();
  testMessages();
  testUnreadElsewhere();
  testRingShift();
  testSelectorDrag();
  testComposeBudget();
  testBadge();
  testMessageSheet();
  testKeyboardEdges();
  testOverlayTapAway();
  testSelectorLastRowVisible();
  testEmojiPad();

  if (g_fail) {
    printf("%d chat check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("chat: all checks passed\n");
  return 0;
}
