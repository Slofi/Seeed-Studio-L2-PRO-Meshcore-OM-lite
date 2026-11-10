// SPDX-License-Identifier: MIT
#include "ui/chat_layout.h"

#include <stdio.h>
#include <string.h>

namespace om {
namespace ui {
namespace {

// The sheet is exactly as tall as its contents: padding, the draft line, and four 22 px key rows.
int sheetHeight(bool replying) {
  return kOskPad * 2 + kOskDraftH + kOskDraftGap + 4 * kOskKeyH + 3 * kOskKeyGap +
         (replying ? kOskReplyH + kOskDraftGap : 0);
}

const char kLetters0[] = "qwertzuiop";
const char kLetters1[] = "asdfghjkl";
const char kLetters2[] = "yxcvbnm";
const char kNumbers0[] = "1234567890";
const char kNumbers1[] = "@#$%&*()!-";   // 10 keys, the old row plus "!"
const char kSymbols[] = "+=/:;!?_";   // 8: the numbers row already carries "-"

int addChars(KeyDef* out, int at, const char* chars) {
  for (int i = 0; chars[i]; ++i) {
    out[at].label[0] = chars[i];
    out[at].label[1] = 0;
    out[at].units = kKeyUnit;
    out[at].kind = KeyKind::Char;
    ++at;
  }
  return at;
}

void put(KeyDef& k, const char* label, uint8_t units, KeyKind kind) {
  snprintf(k.label, sizeof(k.label), "%s", label);
  k.units = units;
  k.kind = kind;
}

}  // namespace

int keyRow(bool nums, int row, KeyDef* out) {
  if (out == nullptr || row < 0 || row > 3) return 0;
  int n = 0;
  if (!nums) {
    switch (row) {
      case 0:
        return addChars(out, 0, kLetters0);
      case 1:
        return addChars(out, 0, kLetters1);
      case 2:
        put(out[n++], "shift", kKeyUnitWide, KeyKind::Shift);
        n = addChars(out, n, kLetters2);
        put(out[n++], "", kKeyUnitWide, KeyKind::Del);
        return n;
      default:
        // Filip, 2026-11-10: "where are the dot and comma?" The bottom row is
        // 123, comma, space, dot, Send.
        put(out[n++], "123", kKeyUnitWide, KeyKind::Mode);
        put(out[n++], ",", kKeyUnit, KeyKind::Char);
        put(out[n++], "space", kKeyUnitSpace, KeyKind::Space);
        put(out[n++], ".", kKeyUnit, KeyKind::Char);
        put(out[n++], "Send", kKeyUnitWide, KeyKind::Send);
        return n;
    }
  }
  switch (row) {
    case 0:
      return addChars(out, 0, kNumbers0);
    case 1:
      return addChars(out, 0, kNumbers1);
    case 2:
      put(out[n++], "abc", kKeyUnitWide, KeyKind::Mode);
      n = addChars(out, n, kSymbols);
      put(out[n++], "", kKeyUnitWide, KeyKind::Del);
      return n;
    default:
      // The dot and the comma stay on the 123 page too.
      put(out[n++], "space", kKeyUnitSpace, KeyKind::Space);
      put(out[n++], ",", kKeyUnit, KeyKind::Char);
      put(out[n++], ".", kKeyUnit, KeyKind::Char);
      put(out[n++], "Send", kKeyUnitWide, KeyKind::Send);
      return n;
  }
}

ChatLayout chatLayoutFor(const Layout& L, bool osk, bool replying, bool nums, bool selector) {
  ChatLayout g;
  g.osk = osk;
  g.selector = selector;

  // The header replaces the old chip strip in the same row, so the message band keeps its top edge.
  g.head = Rect{L.content.x, L.content.y, L.content.w, kHeadH};
  g.headDot = Rect{(int16_t)(g.head.right() - kHeadPadX - kElsewhereDot),
                   (int16_t)(g.head.y + (g.head.h - 1 - kElsewhereDot) / 2), kElsewhereDot,
                   kElsewhereDot};
  // The title stops before the reserved unread-count room, so a count appearing beside the dot never
  // moves the name. The chevron follows the measured name inside this rect.
  g.headTitle = Rect{(int16_t)(g.head.x + kHeadPadX), g.head.y,
                     (int16_t)(g.headDot.x - kElsewhereGap - kElsewhereW - (g.head.x + kHeadPadX)),
                     (int16_t)(g.head.h - 1)};

  if (osk) {
    const int16_t sh = (int16_t)sheetHeight(replying);
    g.sheet = Rect{0, (int16_t)(L.height - sh), L.width, sh};
  }

  if (osk) {
    g.msgs = Rect{L.content.x, g.head.bottom(), L.content.w,
                  (int16_t)(g.sheet.y - g.head.bottom())};
  } else {
    g.compose = Rect{L.content.x, (int16_t)(L.content.bottom() - kComposeH), L.content.w, kComposeH};
    g.msgs = Rect{L.content.x, g.head.bottom(), L.content.w,
                  (int16_t)(g.compose.y - g.head.bottom())};

    // The draft field is the whole compose row: the Send button moved into the keyboard (Filip,
    // 2026-11-10) and the keyboard button is gone too (tapping the field opens the sheet).
    const int16_t rowY = (int16_t)(g.compose.y + 1 + (kComposeH - 1 - kFieldH) / 2);
    g.field = Rect{(int16_t)(g.compose.x + kComposePadX), rowY,
                   (int16_t)(g.compose.w - 2 * kComposePadX), kFieldH};
  }

  // The selector drops down over the message area: the header's bottom edge to the compose line (or
  // the keyboard sheet), with its last row pinned to the panel's floor.
  if (selector) {
    g.selPanel = g.msgs;
    g.selManage = Rect{g.selPanel.x, (int16_t)(g.selPanel.bottom() - kSelManageH), g.selPanel.w,
                       kSelManageH};
    g.selList = Rect{g.selPanel.x, g.selPanel.y, g.selPanel.w,
                     (int16_t)(g.selManage.y - g.selPanel.y)};
  }

  if (osk) {
    int y = g.sheet.y + kOskPad;
    const int16_t innerW = (int16_t)(L.width - 2 * kOskPad);
    if (replying) {
      g.replyBar = Rect{kOskPad, (int16_t)y, innerW, kOskReplyH};
      y += kOskReplyH + kOskDraftGap;
    }
    g.draft = Rect{kOskPad, (int16_t)y, innerW, kOskDraftH};
    y += kOskDraftH + kOskDraftGap;
    for (int r = 0; r < 4; ++r) {
      KeyDef defs[10];
      const int n = keyRow(nums, r, defs);
      uint8_t units[10];
      for (int i = 0; i < n; ++i) units[i] = defs[i].units;
      const Rect row{kOskPad, (int16_t)y, innerW, kOskKeyH};
      g.keyCount[r] = (uint8_t)rowKeyRects(row, units, n, kOskKeyGap, g.keys[r]);
      // Hit rectangles: the drawn keys stay where the mock puts them (4 px side pad), but a finger in
      // that pad on the outermost key, or on the last pixel rows under the bottom row, belongs to the
      // key. Only the 2 px gaps BETWEEN keys still do nothing (EST-338).
      for (int c = 0; c < n; ++c) g.keyHit[r][c] = g.keys[r][c];
      if (n > 0) {
        if (n == 1) {
          g.keyHit[r][0] = Rect{0, g.keys[r][0].y, L.width, g.keys[r][0].h};
        } else {
          g.keyHit[r][0].x = 0;
          g.keyHit[r][0].w = (int16_t)g.keys[r][0].right();
          g.keyHit[r][n - 1].w = (int16_t)(L.width - g.keys[r][n - 1].x);
        }
        if (r == 3) {
          for (int c = 0; c < n; ++c) {
            g.keyHit[r][c].h = (int16_t)(L.height - g.keyHit[r][c].y);
          }
        }
      }
      y += kOskKeyH + kOskKeyGap;
    }
  }

  return g;
}

KeyHit keyAt(const ChatLayout& g, bool nums, int x, int y) {
  KeyHit hit;
  for (int r = 0; r < 4; ++r) {
    KeyDef defs[10];
    const int n = keyRow(nums, r, defs);
    for (int c = 0; c < n && c < g.keyCount[r]; ++c) {
      if (!g.keyHit[r][c].contains(x, y)) continue;
      hit.row = r;
      hit.col = c;
      return hit;
    }
  }
  return hit;
}

ChatOverlayPress chatOverlayPress(const ChatLayout& g, bool menuOpen, const Rect& menuPanel,
                                  int x, int y) {
  ChatOverlayPress p;
  // The order is the drawing order: the message sheet sits over everything (screen_chat draws it
  // last), then the selector over the message band, then the keyboard sheet.
  if (menuOpen) {
    p.overlay = ChatOverlay::MessageSheet;
    p.inside = menuPanel.contains(x, y);
    return p;
  }
  if (g.selector) {
    p.overlay = ChatOverlay::Selector;
    p.inside = g.selPanel.contains(x, y);
    return p;
  }
  if (g.osk) {
    p.overlay = ChatOverlay::Keyboard;
    p.inside = g.sheet.contains(x, y);
    return p;
  }
  return p;
}

int selContentHeight(int channelCount, int dmCount) {
  if (channelCount < 0) channelCount = 0;
  if (dmCount < 0) dmCount = 0;
  const int channels = kSelSectionH + channelCount * kSelRowH;
  const int dms = kSelSectionH + (dmCount > 0 ? dmCount * kSelRowH : kSelEmptyH);
  return channels + dms;
}

int selMaxScroll(const Rect& list, int channelCount, int dmCount) {
  const int content = selContentHeight(channelCount, dmCount);
  return content > list.h ? content - list.h : 0;
}

namespace {

// Top of one entry's row, in scrolling-list pixels (0 = the list's first pixel at scroll 0).
int selRowOffset(int channelCount, int dmCount, int entry) {
  if (entry < 0) return -1;
  if (entry < channelCount) return kSelSectionH + entry * kSelRowH;
  const int dm = entry - channelCount;
  if (dm >= dmCount) return -1;
  return kSelSectionH + channelCount * kSelRowH + kSelSectionH + dm * kSelRowH;
}

}  // namespace

int selRowTop(const Rect& list, int channelCount, int dmCount, int entry, int scroll) {
  const int off = selRowOffset(channelCount, dmCount, entry);
  if (off < 0) return -1;
  return list.y + off - scroll;
}

int selRowAt(const Rect& list, int channelCount, int dmCount, int scroll, int py) {
  if (py < list.y || py >= list.bottom()) return -1;
  for (int i = 0; i < channelCount + dmCount; ++i) {
    const int top = selRowTop(list, channelCount, dmCount, i, scroll);
    if (py >= top && py < top + kSelRowH) return i;
  }
  return -1;
}

// The list follows the finger. A larger scroll moves the rows UP (selRowTop subtracts it), so a finger
// that went UP by dyUp (screen y decreased) has to INCREASE the scroll by dyUp. This is the sign that
// was wrong: the old drag did `base - dy` and the list ran away from the finger (EST-334).
int selDragScroll(int base, int dyUp, int maxScroll) {
  int next = base + dyUp;
  if (next < -kSelGive) next = -kSelGive;
  if (next > maxScroll + kSelGive) next = maxScroll + kSelGive;
  return next;
}

int msgContentHeight(const int16_t* heights, int n) {
  if (heights == nullptr || n <= 0) return 0;
  int h = 2 * kMsgPadY + (n - 1) * kMsgGap;
  for (int i = 0; i < n; ++i) h += heights[i];
  return h;
}

int msgMaxScroll(const Rect& band, const int16_t* heights, int n) {
  const int content = msgContentHeight(heights, n);
  return content > band.h ? content - band.h : 0;
}

void msgSpans(const Rect& band, const int16_t* heights, int n, int scroll, int16_t* top) {
  if (heights == nullptr || top == nullptr) return;
  int y = band.bottom() - kMsgPadY + scroll;
  for (int i = n - 1; i >= 0; --i) {
    y -= heights[i];
    top[i] = (int16_t)y;
    y -= kMsgGap;
  }
}

int msgAt(const Rect& band, const int16_t* heights, int n, int scroll, int py) {
  if (heights == nullptr || n <= 0) return -1;
  if (py < band.y || py >= band.bottom()) return -1;
  int y = band.bottom() - kMsgPadY + scroll;
  for (int i = n - 1; i >= 0; --i) {
    const int top = y - heights[i];
    if (py >= top && py < top + heights[i]) return i;
    y = top - kMsgGap;
  }
  return -1;
}

int rowKeyRects(const Rect& row, const uint8_t* units, int n, int gap, Rect* out) {
  if (units == nullptr || out == nullptr || n <= 0) return 0;
  int total = 0;
  for (int i = 0; i < n; ++i) total += units[i] > 0 ? units[i] : 1;
  const int avail = row.w - gap * (n - 1);
  if (total <= 0 || avail <= 0) return 0;
  // Boundaries from the running unit count, so rounding never accumulates: every key is within one
  // pixel of its exact share and the row still ends on its right edge.
  int cum = 0;
  for (int i = 0; i < n; ++i) {
    const int left = row.x + i * gap + (avail * cum) / total;
    cum += units[i] > 0 ? units[i] : 1;
    const int right = (i == n - 1) ? row.right() : row.x + i * gap + (avail * cum) / total;
    int w = right - left;
    if (w < 1) w = 1;
    out[i] = Rect{(int16_t)left, row.y, (int16_t)w, row.h};
  }
  return n;
}

}  // namespace ui
}  // namespace om
