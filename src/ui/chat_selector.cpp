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

// An unread count as the screens show it: one digit, or "9+" above nine (ui/badge_font.h holds the
// one rule, so the red badge and the gold text can never disagree).
void countText(char* out, size_t cap, int n) { badgeText(n, out, (int)cap); }

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
// and the start of its text on the second, the unread count at the right. The selected chat carries
// the gold dot.
void drawSelRow(lgfx::LovyanGFX* d, const SelectorModel& m, const ChatEntry& e, int entry,
                const Rect& list) {
  const int top = selRowTop(list, m.channelCount, m.dmCount, entry, m.scroll);
  const Rect row{list.x, (int16_t)top, list.w, kSelRowH};
  if (!row.overlaps(list)) return;
  const bool on = (entry == m.selected);
  if (on) d->fillRect(row.x, row.y, row.w, row.h, kSelBg);

  const int cy1 = row.y + kSelLine1Cy;
  const int cy2 = row.y + kSelLine2Cy;
  const int dotX = row.x + kSelPadX;
  if (on) d->fillCircle(dotX + kSelDot / 2, cy1, kSelDot / 2, kAcc);
  const int nameX = dotX + kSelDot + kSelDotGap;

  int right = row.right() - kSelPadX;
  char age[16];
  if (e.hasLast) {
    entryStamp(age, sizeof(age), m, e);
    drawTextRight(d, FontRole::MonoMeta, kDimmer, on ? kSelBg : kPanel, right, cy1, age);
    right -= textWidth(d, FontRole::MonoMeta, age) + kSelGap;
  }
  drawTextEllipsis(d, FontRole::SansBody, on ? kAccInk : kInk, on ? kSelBg : kPanel, nameX, cy1,
                   right - nameX, e.name);

  int textRight = row.right() - kSelPadX;
  if (e.unread > 0) {
    char b[4];
    countText(b, sizeof(b), e.unread);
    drawTextRight(d, FontRole::MonoMeta, kAcc, on ? kSelBg : kPanel, textRight, cy2, b);
    textRight -= textWidth(d, FontRole::MonoMeta, b) + kSelGap;
  }
  char line[kMeshNameCap + 2 + kMeshTextCap];   // "from: text", both at MeshCore's own limits
  if (e.hasLast) {
    snprintf(line, sizeof(line), "%s: %s", e.lastFrom, e.lastText);
    // A preview line may still be too wide: drawTextEllipsis cuts on a code point boundary and ends
    // the line with "...", never mid-sequence and never without saying so.
    drawTextEllipsis(d, FontRole::MonoMeta, kDim, on ? kSelBg : kPanel, nameX, cy2,
                     textRight - nameX, line);
  } else {
    snprintf(line, sizeof(line), "no messages yet");
    drawTextEllipsis(d, FontRole::MonoMeta, kDimmer, on ? kSelBg : kPanel, nameX, cy2,
                     textRight - nameX, line);
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

  if (manage.empty()) return;
  const int cy = manage.y + manage.h / 2;
  d->drawFastHLine(manage.x, manage.y, manage.w, kLine);
  drawTextLeft(d, FontRole::SansBody, kAcc, kPanel, manage.x + kSelPadX, cy, "Manage channels");
  drawChevron(d, manage.right() - kSelPadX - kSelChevronW, cy, kAcc, true);
}

}  // namespace ui
}  // namespace om
