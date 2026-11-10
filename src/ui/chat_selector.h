// SPDX-License-Identifier: MIT
// The chat selector (the dropdown list of channels and DMs) in ONE place, used twice: the Chat tab's
// header (EST-347) and the Settings > Chat page (EST-350). The arithmetic and the hit test are pure
// C++ (no LovyanGFX) so test/host proves them; the drawing lives in chat_selector.cpp.
//
// The list holds a "Channels" label, one row per channel, a "Direct messages" label and either one
// row per DM or the "No direct messages yet" line, plus the pinned "Manage channels" row at its
// floor. A tap on a row picks that chat, a tap on the pinned row opens the channel list, a tap
// anywhere else closes the dropdown and is CONSUMED (EST-339's one-press-one-answer rule).
#pragma once

#include <stdint.h>

#include "layout.h"
#include "theme.h"
#include "ui/chat_layout.h"   // selRowAt / selMaxScroll: the list arithmetic both places share
#include "ui/chat_model.h"    // ChatEntry

// The real declaration: LovyanGFX lives in the inline namespace `lgfx::v1`, so this merges with the
// library's own class instead of clashing with it, and the header stays usable without the library
// (the arithmetic and the hit test above need no display; test/host compiles this file as-is).
namespace lgfx {
inline namespace v1 {
class LovyanGFX;
}
}  // namespace lgfx

namespace om {
namespace ui {

// What the dropdown lists and how it is scrolled. `selected` is the chat the caller is SHOWING: on
// the Chat tab the open chat, in Settings the chat whose settings are on the page (never the same
// thing once the user picks another one in Settings).
struct SelectorModel {
  const ChatEntry* chats = nullptr;
  int chatCount = 0;
  int channelCount = 0;   // channels first, then DMs (chat_model.h)
  int dmCount = 0;
  int selected = 0;
  int scroll = 0;         // px the list is scrolled down
  uint32_t nowSec = 0;    // the two clocks the row ages are measured against
  uint32_t nowWall = 0;
  bool nowReal = false;
};

enum class SelectorHitKind : uint8_t { None = 0, Row, Manage, Close };

struct SelectorHitResult {
  SelectorHitKind kind = SelectorHitKind::None;
  int row = -1;   // Row: the entry (an index into chats)
};

// The panel is the list's window; `list` scrolls inside it and `manage` is the pinned floor row.
// A tap on the panel but not on a chat row (a section label, the empty line, the pads) is Close, so
// the dropdown never eats a tap silently. Empty rects answer None (the dropdown is not drawn).
inline SelectorHitResult selectorHit(const SelectorModel& m, const Rect& panel, const Rect& list,
                                     const Rect& manage, int x, int y) {
  SelectorHitResult r;
  if (panel.empty() || !panel.contains(x, y)) {
    r.kind = SelectorHitKind::None;
    return r;
  }
  if (!manage.empty() && manage.contains(x, y)) {
    r.kind = SelectorHitKind::Manage;
    return r;
  }
  if (!list.empty() && list.contains(x, y)) {
    const int i = selRowAt(list, m.channelCount, m.dmCount, m.scroll, y);
    if (i >= 0 && i < m.chatCount) {
      r.kind = SelectorHitKind::Row;
      r.row = i;
      return r;
    }
  }
  r.kind = SelectorHitKind::Close;
  return r;
}

// The list's own scroll clamp, from the same content height the drawing uses.
inline int selectorMaxScroll(const SelectorModel& m, const Rect& list) {
  return selMaxScroll(list, m.channelCount, m.dmCount);
}

// What a PRESS while the dropdown is open means, as the one rule both screens follow: a row picks
// that chat, the pinned row opens the channel list, anything else (the pads, a section label, and
// every pixel outside the panel, Back included) closes it and the whole gesture is SPENT, so the
// press never reaches what is behind (EST-339's one-press-one-answer rule).
struct SelectorPress {
  SelectorHitKind kind = SelectorHitKind::None;
  int row = -1;
  bool picks = false;     // the caller shows that chat
  bool consumes = false;  // the caller spends the gesture and does nothing else
};

inline SelectorPress selectorPress(const SelectorModel& m, const Rect& panel, const Rect& list,
                                   const Rect& manage, int x, int y) {
  const SelectorHitResult h = selectorHit(m, panel, list, manage, x, y);
  SelectorPress p;
  p.kind = h.kind;
  p.row = h.row;
  if (h.kind == SelectorHitKind::Row) {
    p.picks = true;
  } else if (h.kind == SelectorHitKind::None) {
    p.kind = SelectorHitKind::Close;   // outside the panel is the same close, one rule
    p.consumes = true;
  } else {
    p.consumes = true;
  }
  return p;
}

// The pick itself: Settings writes the chat it SHOWS. The chat open on the Chat tab is a different
// variable on purpose, so picking one in Settings never moves the Chat screen (EST-350).
inline void selectorPickShown(const SelectorPress& p, int* shown) {
  if (p.picks && p.row >= 0 && shown != nullptr) *shown = p.row;
}

// Draw the dropdown into the off-screen frame. The caller has already drawn what is behind it and
// keeps its own header visible above `panel` (the Chat tab's head row, the Settings selector row).
void drawChatSelector(lgfx::LovyanGFX* d, const SelectorModel& m, const Rect& panel,
                      const Rect& list, const Rect& manage);

}  // namespace ui
}  // namespace om
