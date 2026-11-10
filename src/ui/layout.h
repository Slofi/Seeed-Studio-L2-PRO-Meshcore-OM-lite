// SPDX-License-Identifier: MIT
// OM Lite UI layout: rectangles handed out by splitting the screen, never repeated in a screen.
// The same Rect draws and answers the tap. Plain C++ so test/host can prove the arithmetic.
#pragma once

#include <stdint.h>

#include "theme.h"

namespace om {
namespace ui {

struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;

  constexpr int16_t right() const { return (int16_t)(x + w); }
  constexpr int16_t bottom() const { return (int16_t)(y + h); }
  constexpr bool empty() const { return w <= 0 || h <= 0; }

  // Half-open: the top/left edge belongs to the rectangle, the bottom/right edge does not. Row
  // bands share edges, so a pixel on a boundary is in exactly one of them.
  constexpr bool contains(int px, int py) const {
    return px >= x && px < right() && py >= y && py < bottom();
  }

  constexpr bool overlaps(const Rect& o) const {
    return x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom();
  }

  constexpr Rect intersect(const Rect& o) const {
    const int16_t nx = x > o.x ? x : o.x;
    const int16_t ny = y > o.y ? y : o.y;
    const int16_t nr = right() < o.right() ? right() : o.right();
    const int16_t nb = bottom() < o.bottom() ? bottom() : o.bottom();
    return Rect{nx, ny, (int16_t)(nr - nx), (int16_t)(nb - ny)};
  }
};

enum class Tab : uint8_t { Nodes = 0, Chat, Map, Rptr, Count };
constexpr int kTabCount = 4;

constexpr const char* tabLabel(Tab t) {
  switch (t) {
    case Tab::Nodes: return "Nodes";
    case Tab::Chat:  return "Chat";
    case Tab::Map:   return "Map";
    case Tab::Rptr:  return "RPTR";
    default:         return "";
  }
}

// One screen split. The fractions are computed from the passed size, not baked in, so a second
// device (800x480, no touch) reuses the same structure.
struct Layout {
  int16_t width = 0;
  int16_t height = 0;
  Rect status;
  Rect content;
  Rect tabbar;

  constexpr Rect tab(Tab t) const {
    const int i = (int)t;
    if (i < 0 || i >= kTabCount) return Rect{};
    const int16_t x0 = (int16_t)(tabbar.x + (tabbar.w * i) / kTabCount);
    const int16_t x1 = (int16_t)(tabbar.x + (tabbar.w * (i + 1)) / kTabCount);
    return Rect{x0, tabbar.y, (int16_t)(x1 - x0), tabbar.h};
  }

  // -1 when the point is not in the tab bar; otherwise the tab index. Uses the same tab() rects.
  constexpr int tabAt(int px, int py) const {
    if (!tabbar.contains(px, py)) return -1;
    for (int i = 0; i < kTabCount; ++i) {
      if (tab((Tab)i).contains(px, py)) return i;
    }
    return -1;
  }

  // Full row rectangle in screen coordinates; negative y when scrolled above the content.
  constexpr Rect row(int index, int scroll) const {
    return Rect{content.x, (int16_t)(content.y + index * kRowH - scroll), content.w, kRowH};
  }

  // The part of the row that is actually on screen. Empty for rows above or below the list.
  constexpr Rect rowVisible(int index, int scroll) const {
    return row(index, scroll).intersect(content);
  }

  // -1 when the point hits no row. Only the visible part of a row counts, so a tap under the status
  // bar or the tab bar can never select a row hidden behind it.
  constexpr int rowAt(int px, int py, int scroll, int count) const {
    for (int i = 0; i < count; ++i) {
      const Rect v = rowVisible(i, scroll);
      if (!v.empty() && v.contains(px, py)) return i;
    }
    return -1;
  }

  constexpr int maxScroll(int count) const {
    const int total = count * kRowH;
    return total > content.h ? total - content.h : 0;
  }

  constexpr int visibleRows() const {
    return (content.h + kRowH - 1) / kRowH;
  }

  constexpr Rect gear() const {
    return Rect{(int16_t)(status.right() - kStatusPadX - kGearSize),
                (int16_t)(status.y + (status.h - kGearSize) / 2), kGearSize, kGearSize};
  }
};

// The unread badge's box on a tab: a 13 px circle for one digit, the 19 px pill for two, its centre
// at the top-right corner of the tab icon and its top 1 px under the selected tab's gold line
// (theme.h holds the numbers). Drawing and the preview tool use this same rectangle.
constexpr Rect countBadgeRect(const Rect& tab, bool wide) {
  const int16_t w = wide ? kBadgePillW : kBadgeD;
  const int16_t cx = (int16_t)(tab.x + tab.w / 2 + kBadgeCxOfs);
  const int16_t cy = (int16_t)(tab.y + kBadgeTop + kBadgeD / 2);
  return Rect{(int16_t)(cx - w / 2), (int16_t)(cy - kBadgeD / 2), w, kBadgeD};
}

constexpr Layout layoutFor(int w, int h) {
  Layout L;
  L.width = (int16_t)w;
  L.height = (int16_t)h;
  L.status = Rect{0, 0, (int16_t)w, kStatusH};
  L.tabbar = Rect{0, (int16_t)(h - kTabH), (int16_t)w, kTabH};
  L.content = Rect{0, kStatusH, (int16_t)w, (int16_t)(h - kStatusH - kTabH)};
  return L;
}

constexpr Layout kL2 = layoutFor(kScreenW, kScreenH);

inline const Layout& l2() {
  static constexpr Layout L = layoutFor(kScreenW, kScreenH);
  return L;
}

}  // namespace ui
}  // namespace om
