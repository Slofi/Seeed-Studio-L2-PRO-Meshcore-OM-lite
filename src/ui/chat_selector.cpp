// SPDX-License-Identifier: MIT
// The chat selector's drawing: ONE implementation for the Chat tab's header dropdown and for the
// Settings > Chat selector (EST-350). It was screen_chat.cpp's drawSelector/drawSelRow, moved here
// unchanged so the two places cannot drift apart; the rectangles come from the caller (chat_layout.h
// for the Chat tab, settings_layout.h for Settings) and the hit test from chat_selector.h.
#include "ui/chat_selector.h"

#include <stdio.h>
#include <string.h>

#include "ui/age.h"          // the one timestamp formatter (row ages)
#include "ui/badge_font.h"   // the one unread-count rule
#include "ui/fonts.h"
#include "ui/scroll_indicator.h"
#include "ui/widgets.h"

namespace om {
namespace ui {
namespace {

// The one formatter (ui/age.cpp) with the narrow Clock form, exactly as the header and the chat lines
// use it: a selector row and the message list never disagree about an age.
void entryStamp(char* out, size_t cap, const SelectorModel& m, const ChatEntry& e) {
  Stamp s;
  s.wall = e.lastWall;
  s.uptime = e.lastTs;
  s.wallReal = e.lastWallReal;
  stampText(out, cap, s, m.nowWall, m.nowSec, m.nowReal, StampForm::Clock);
}

// The header's and the Manage row's chevrons: vectors, because the faces carry no arrows.
void drawChevron(lgfx::LovyanGFX* d, int x, int cy, uint16_t col, bool right) {
  if (right) {
    d->drawLine(x, cy - 3, x + 3, cy, col);
    d->drawLine(x + 3, cy, x, cy + 3, col);
    return;
  }
  d->drawLine(x, cy - 2, x + 3, cy + 2, col);
  d->drawLine(x + 3, cy + 2, x + 6, cy - 2, col);
}

// One row of the dropdown: the chat's name and the last message's age on the first line, its sender
// and the start of its text on the second, the unread count at the right.
//
// EST-382: the dot now means UNREAD, not "selected". The selected chat keeps only its highlight. The
// dot's and the number's rectangles come from selRowLayoutFor(), the same function the hit test uses,
// and both are driven by the ONE field ChatEntry::unread, so they cannot disagree. A chat with no
// unread draws neither, and a muted chat keeps whatever the count does (no new muted rule here).
void drawSelRow(lgfx::LovyanGFX* d, const SelectorModel& m, const ChatEntry& e, int entry,
                const Rect& list) {
  const SelRowLayout o = selRowLayoutFor(list, m.channelCount, m.dmCount, entry, m.scroll, e.unread,
                                         entry == m.selected);
  if (!o.row.overlaps(list)) return;
  const Rect& row = o.row;
  const bool on = o.highlight;
  const uint16_t bg = on ? kSelBg : kPanel;
  if (on) d->fillRect(row.x, row.y, row.w, row.h, kSelBg);

  if (o.hasDot) {
    d->fillCircle((int16_t)(o.dot.x + o.dot.w / 2), (int16_t)(o.dot.y + o.dot.h / 2),
                  (int16_t)(o.dot.w / 2), kAcc);
  }
  const int nameX = o.nameX;

  int right = row.right() - kSelPadX;
  char age[16];
  if (e.hasLast) {
    entryStamp(age, sizeof(age), m, e);
    drawTextRight(d, FontRole::MonoMeta, kDimmer, bg, right, o.line1Cy, age);
    right -= textWidth(d, FontRole::MonoMeta, age) + kSelGap;
  }
  drawTextEllipsis(d, FontRole::SansBody, on ? kAccInk : kInk, bg, nameX, o.line1Cy, right - nameX,
                   e.name);

  int textRight = row.right() - kSelPadX;
  if (o.hasCount) {
    drawTextRight(d, FontRole::MonoMeta, kAcc, bg, o.count.right(), o.line2Cy, o.countText);
    textRight -= textWidth(d, FontRole::MonoMeta, o.countText) + kSelGap;
  }
  char line[kMeshNameCap + 2 + kMeshTextCap];   // "from: text", both at MeshCore's own limits
  if (e.hasLast) {
    snprintf(line, sizeof(line), "%s: %s", e.lastFrom, e.lastText);
    // A preview line may still be too wide: drawTextEllipsis cuts on a code point boundary and ends
    // the line with "...", never mid-sequence and never without saying so.
    drawTextEllipsis(d, FontRole::MonoMeta, kDim, bg, nameX, o.line2Cy, textRight - nameX, line);
  } else {
    snprintf(line, sizeof(line), "no messages yet");
    drawTextEllipsis(d, FontRole::MonoMeta, kDimmer, bg, nameX, o.line2Cy, textRight - nameX, line);
  }

  const int lineY = row.bottom() - 1;
  if (lineY >= list.y && lineY < list.bottom()) {
    d->drawFastHLine(row.x, lineY, row.w, kRowLine);
  }
}

}  // namespace

void drawChatSelector(lgfx::LovyanGFX* d, const SelectorModel& m, const Rect& panel,
                      const Rect& list, const Rect& manage) {
  if (d == nullptr || panel.empty()) return;
  d->fillRect(panel.x, panel.y, panel.w, panel.h, kPanel);
  d->drawRect(panel.x, panel.y, panel.w, panel.h, kLine);

  d->setClipRect(list.x, list.y, list.w, list.h);
  int y = list.y - m.scroll;
  drawTextLeft(d, FontRole::MonoMeta, kDim, kPanel, list.x + kSelPadX, y + kSelSectionH / 2,
               "Channels");
  y += kSelSectionH;
  for (int i = 0; i < m.channelCount && i < m.chatCount; ++i) {
    drawSelRow(d, m, m.chats[i], i, list);
  }
  y += m.channelCount * kSelRowH;
  drawTextLeft(d, FontRole::MonoMeta, kDim, kPanel, list.x + kSelPadX, y + kSelSectionH / 2,
               "Direct messages");
  y += kSelSectionH;
  if (m.dmCount <= 0) {
    drawTextLeft(d, FontRole::MonoMeta, kDimmer, kPanel, list.x + kSelPadX, y + kSelEmptyH / 2,
                 "No direct messages yet");
  } else {
    for (int i = 0; i < m.dmCount; ++i) {
      const int entry = m.channelCount + i;
      if (entry < m.chatCount) drawSelRow(d, m, m.chats[entry], entry, list);
    }
  }
  d->clearClipRect();

  // EST-414: the channel list's scroll indicator, from the list rectangle the rows were drawn into
  // and the same content height selMaxScroll() clamps against. It sits in the list's 7 px right
  // padding, so no row's name, age or unread count is under it. The pinned Manage row below is
  // outside `list`, so it never gets a track.
  drawScrollIndicator(d, scrollIndicatorFor(list, selContentHeight(m.channelCount, m.dmCount),
                                           list.h, m.scroll,
                                           selMaxScroll(list, m.channelCount, m.dmCount), false));

  if (manage.empty()) return;
  const int cy = manage.y + manage.h / 2;
  d->drawFastHLine(manage.x, manage.y, manage.w, kLine);
  drawTextLeft(d, FontRole::SansBody, kAcc, kPanel, manage.x + kSelPadX, cy, "Manage channels");
  drawChevron(d, manage.right() - kSelPadX - kSelChevronW, cy, kAcc, true);
}

}  // namespace ui
}  // namespace om
