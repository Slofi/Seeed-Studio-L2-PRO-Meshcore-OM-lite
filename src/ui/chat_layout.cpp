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
        put(out[n++], "123", kKeyUnitWide, KeyKind::Mode);
        put(out[n++], "space", kKeyUnitWide, KeyKind::Space);
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
      put(out[n++], "space", kKeyUnitWide, KeyKind::Space);
      put(out[n++], "Send", kKeyUnitWide, KeyKind::Send);
      return n;
  }
}

ChatLayout chatLayoutFor(const Layout& L, bool osk, bool replying, bool nums) {
  ChatLayout g;
  g.osk = osk;

  g.chips = Rect{L.content.x, L.content.y, L.content.w, kChipRowH};
  g.chipMenu = Rect{(int16_t)(g.chips.right() - kChipMenuW), g.chips.y, kChipMenuW,
                    (int16_t)(g.chips.h - 1)};
  g.chipScroll = Rect{g.chips.x, g.chips.y, (int16_t)(g.chipMenu.x - g.chips.x),
                      (int16_t)(g.chips.h - 1)};

  if (osk) {
    const int16_t sh = (int16_t)sheetHeight(replying);
    g.sheet = Rect{0, (int16_t)(L.height - sh), L.width, sh};
  }

  if (osk) {
    g.msgs = Rect{L.content.x, g.chips.bottom(), L.content.w,
                  (int16_t)(g.sheet.y - g.chips.bottom())};
  } else {
    g.compose = Rect{L.content.x, (int16_t)(L.content.bottom() - kComposeH), L.content.w, kComposeH};
    g.msgs = Rect{L.content.x, g.chips.bottom(), L.content.w,
                  (int16_t)(g.compose.y - g.chips.bottom())};

    const int16_t rowY = (int16_t)(g.compose.y + 1 + (kComposeH - 1 - kFieldH) / 2);
    const int innerL = g.compose.x + kComposePadX;
    const int innerR = g.compose.right() - kComposePadX;
    int x = innerR - kSendW;
    g.sendBtn = Rect{(int16_t)x, rowY, kSendW, kFieldH};
    x -= kComposeGap + kKbdBtnW;
    g.kbdBtn = Rect{(int16_t)x, rowY, kKbdBtnW, kFieldH};
    x -= kComposeGap;
    g.field = Rect{(int16_t)innerL, rowY, (int16_t)(x - innerL), kFieldH};
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
      y += kOskKeyH + kOskKeyGap;
    }
  }

  const int menuH = chatMenuHeight();
  const int menuBottom = (osk ? g.sheet.y : g.compose.y) - kPanelPad;
  g.menuPanel = Rect{(int16_t)((L.width - kMenuW) / 2), (int16_t)(menuBottom - menuH), kMenuW,
                     (int16_t)menuH};
  g.menuHead = Rect{g.menuPanel.x, (int16_t)(g.menuPanel.y + kPanelPad), kMenuW, kMenuHeadH};
  for (int r = 0; r < 3; ++r) {
    g.menuRow[r] = Rect{(int16_t)(g.menuPanel.x + kPanelPad),
                        (int16_t)(g.menuHead.y + g.menuHead.h + r * kActionRowH),
                        (int16_t)(kMenuW - 2 * kPanelPad), kActionRowH};
  }

  const int infoH = chatInfoHeight();
  const int16_t infoW = (int16_t)(L.width - 20);
  g.infoPanel = Rect{10, (int16_t)(L.content.y + (L.content.h - infoH) / 2), infoW,
                     (int16_t)infoH};
  return g;
}

int chatMenuHeight() { return 2 * kPanelPad + kMenuHeadH + 3 * kActionRowH; }

int chatInfoHeight() { return 2 * kPanelPad + kMenuHeadH + 5 * kActionRowH; }

int chipWidth(int labelW, int countW) {
  int w = labelW + 2 * (kChipPadX + 1);
  if (countW > 0) w += kChipGap + countW;
  return w;
}

int chipStripWidth(const int16_t* widths, int n) {
  if (widths == nullptr || n <= 0) return 2 * kChipRowPadX;
  int w = 2 * kChipRowPadX;
  for (int i = 0; i < n; ++i) w += widths[i] + (i > 0 ? kChipGap : 0);
  return w;
}

int chipMaxScroll(const int16_t* widths, int n, int areaW) {
  const int strip = chipStripWidth(widths, n);
  return strip > areaW ? strip - areaW : 0;
}

int chipLeft(const int16_t* widths, int i, int scroll) {
  if (widths == nullptr || i < 0) return kChipRowPadX - scroll;
  int x = kChipRowPadX - scroll;
  for (int j = 0; j < i; ++j) x += widths[j] + kChipGap;
  return x;
}

int chipAt(const int16_t* widths, int n, int scroll, int px) {
  if (widths == nullptr) return -1;
  for (int i = 0; i < n; ++i) {
    const int x0 = chipLeft(widths, i, scroll);
    if (px >= x0 && px < x0 + widths[i]) return i;
  }
  return -1;
}

int chipScrollTo(int cur, const int16_t* widths, int n, int i, int areaW) {
  if (widths == nullptr || i < 0 || i >= n) return cur;
  const int maxS = chipMaxScroll(widths, n, areaW);
  if (cur < 0) cur = 0;
  if (cur > maxS) cur = maxS;
  const int left = chipLeft(widths, i, cur);
  const int right = left + widths[i];
  if (left < 0) cur += left;
  else if (right > areaW) cur += right - areaW;
  if (cur < 0) cur = 0;
  if (cur > maxS) cur = maxS;
  return cur;
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
