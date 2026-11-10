// Host test for the chat layer: src/ui/chat_layout.cpp and src/ui/text_wrap.cpp, plus the chat
// rectangles. Plain g++, no Arduino and no LovyanGFX, so the same arithmetic the device uses is
// proven here with a fake 6 px per ASCII character measure (a missing glyph is a space plus 2 px,
// exactly what the device draws).
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
  CHECK(c.head.h == kHeadH);
  CHECK(c.msgs.y == c.head.bottom());
  CHECK(c.msgs.bottom() == c.compose.y);
  CHECK(!c.msgs.overlaps(c.head));

  // the gold dot sits at the right edge, inside the row, and the name stops before it
  CHECK(c.headDot.x + c.headDot.w == c.head.right() - kHeadPadX);
  CHECK(c.headDot.y >= c.head.y && c.headDot.bottom() <= c.head.bottom() - 1);
  CHECK(c.headDot.w == kElsewhereDot && c.headDot.h == kElsewhereDot);
  CHECK(c.headTitle.x == c.head.x + kHeadPadX);
  CHECK(c.headTitle.y == c.head.y && c.headTitle.h == c.head.h - 1);
  CHECK(c.headTitle.right() <= c.headDot.x - kElsewhereGap - kElsewhereW);
  CHECK(!c.headTitle.overlaps(c.headDot));

  // --- the compose line: field + keyboard button, no Send button --------------
  CHECK(c.field.x == c.compose.x + kComposePadX);
  CHECK(c.kbdBtn.right() == c.compose.right() - kComposePadX);
  CHECK(c.field.right() + kComposeGap == c.kbdBtn.x);
  CHECK(!c.field.overlaps(c.kbdBtn));
  CHECK(c.field.h == kFieldH && c.kbdBtn.h == kFieldH);
  // the freed width: the field is the whole row left of the button
  CHECK(c.field.w == c.compose.w - 2 * kComposePadX - kKbdBtnW - kComposeGap);

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
  CHECK(o.head.y == L.content.y && o.head.h == kHeadH);
  CHECK(o.msgs.y == o.head.bottom());
  CHECK(o.msgs.bottom() == o.sheet.y);
  CHECK(o.sheet.bottom() == L.height);
  CHECK(o.compose.empty() && o.field.empty() && o.kbdBtn.empty());
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
  CHECK(o.keyCount[0] == 10 && o.keyCount[1] == 9 && o.keyCount[2] == 9 && o.keyCount[3] == 5);

  // the bottom row of the letters page: 123, comma, space, dot, Send (Filip, 2026-11-10)
  const Rect* row3 = o.keys[3];
  // 123 and Send are the two wide keys (the last one takes the rounding remainder)
  CHECK(row3[0].w >= 60 && row3[4].w >= 60);
  CHECK(row3[0].w - row3[4].w <= 1 && row3[4].w - row3[0].w <= 1);
  CHECK(row3[2].w > row3[1].w && row3[2].w > row3[3].w);   // the space is the widest of the three
  CHECK(row3[1].w == row3[3].w);                       // comma and dot are one unit each

  // the numbers page has the same footprint and its own keys
  const ChatLayout num = chatLayoutFor(L, true, false, true, false);
  CHECK(num.sheet.y == o.sheet.y && num.sheet.h == o.sheet.h);
  CHECK(num.keyCount[0] == 10 && num.keyCount[1] == 10 && num.keyCount[2] == 10 &&
        num.keyCount[3] == 4);
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
  // the new bottom row: 123, comma, space, dot, Send
  CHECK(keyRow(false, 3, defs) == 5);
  CHECK(defs[0].kind == KeyKind::Mode && strcmp(defs[0].label, "123") == 0);
  CHECK(defs[1].kind == KeyKind::Char && defs[1].label[0] == ',');
  CHECK(defs[2].kind == KeyKind::Space);
  CHECK(defs[3].kind == KeyKind::Char && defs[3].label[0] == '.');
  CHECK(defs[4].kind == KeyKind::Send);
  CHECK(defs[2].units > defs[1].units);   // the space takes more of the row
  CHECK(keyRow(true, 0, defs) == 10 && defs[0].label[0] == '1');
  CHECK(keyRow(true, 1, defs) == 10 && defs[0].label[0] == '@');
  CHECK(keyRow(true, 2, defs) == 10);
  CHECK(defs[0].kind == KeyKind::Mode && defs[9].kind == KeyKind::Del);
  // the dot and the comma are on the 123 page too
  CHECK(keyRow(true, 3, defs) == 4);
  CHECK(defs[0].kind == KeyKind::Space);
  CHECK(defs[1].kind == KeyKind::Char && defs[1].label[0] == ',');
  CHECK(defs[2].kind == KeyKind::Char && defs[2].label[0] == '.');
  CHECK(defs[3].kind == KeyKind::Send);
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

int main() {
  testWrap();
  testUtf8();
  testHeadAndSelector();
  testComposeAndSheet();
  testMessages();
  testUnreadElsewhere();
  testRingShift();

  if (g_fail) {
    printf("%d chat check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("chat: all checks passed\n");
  return 0;
}
