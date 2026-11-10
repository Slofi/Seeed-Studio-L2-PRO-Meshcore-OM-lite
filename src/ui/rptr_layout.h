// SPDX-License-Identifier: MIT
// EST-362: RPTR geometry. Two layouts only: the list (a header with Scan, the rows) and one page (a
// back key, a title, an optional status card with its refresh icon, an optional banner with one
// button, the scrolling rows, and at most one Apply button). The confirmation is a pop-up over the
// page, so it has its own small layout.
//
// Every rectangle is handed out here and the same rectangle answers the tap, so no screen repeats a
// coordinate.
#pragma once

#include <stdint.h>

#include "layout.h"
#include "rptr_page.h"
#include "theme.h"

namespace om {
namespace ui {

constexpr int16_t kRptrHeadH = 26;      // list header under the status bar
constexpr int16_t kRptrRowH = 36;       // list row: name + one mono line
constexpr int16_t kRptrPageRowH = 30;   // a page row: label + value (or one line)
constexpr int16_t kRptrBackW = 62;
constexpr int16_t kRptrCardH = 44;      // the status card (2 columns x 3 rows)
constexpr int16_t kRptrCardRowH = 14;
constexpr int16_t kRptrBannerH = 27;
constexpr int16_t kRptrApplyH = 30;
constexpr int16_t kRptrBigH = 36;
constexpr int16_t kRptrKeyW = 26;       // a row's small key
constexpr int16_t kRptrKeyH = 24;
constexpr int16_t kRptrModalW = 288;
constexpr int16_t kRptrModalH = 118;

struct RptrListLayout {
  Rect head;       // "REPEATERS"
  Rect scan;       // the Scan chip (the tab scans by itself; this is the rescan)
  Rect list;       // the scrolling list body
  Rect empty;      // the empty state's area
  int visibleRows = 0;
};

RptrListLayout rptrListLayoutFor(const Layout& L);

struct RptrRow {
  int16_t y = 0;
  int16_t h = 0;
};

RptrRow rptrListRow(const RptrListLayout& r, int index, int scroll);
int rptrListRowAt(const RptrListLayout& r, int count, int scroll, int x, int y);
constexpr int rptrListMaxScroll(const RptrListLayout& r, int count) {
  const int total = count * (int)kRptrRowH;
  return total > r.list.h ? total - (int)r.list.h : 0;
}

struct RptrPageLayout {
  Rect back;
  Rect title;
  Rect sub;
  Rect card;           // empty (w 0) when the page has no status card
  Rect refresh;        // the card's refresh icon
  Rect banner;         // empty when there is no line to say
  Rect bannerButton;   // "Set clock" inside the banner
  Rect body;           // the scrolling rows
  Rect apply;          // the one Apply button (empty when the page has none)
  Rect big;            // the one big "Log in" button (empty otherwise)
  Rect input;          // console: the free command line
  Rect send;           // console: Send
  int visibleRows = 0;
};

// `hasCard` / `hasBanner` / `hasApply` / `big` / `console` say which pieces the page uses; the shell
// passes what buildScreen() produced.
RptrPageLayout rptrPageLayoutFor(const Layout& L, bool hasCard, bool hasBanner, bool hasApply,
                                 bool big, bool console);

// A page row's pieces. `stepKeys` adds the -/+ pair, `key` the one small key at the right, `arrow`
// the ">" (a key too). The body is the whole row.
struct RptrRowRects {
  Rect body;
  Rect label;
  Rect value;
  Rect keyA;   // "-" (stepKeys) or the one key
  Rect keyB;   // "+"
  Rect box;    // the checkbox (0 when the row has none)
};

RptrRowRects rptrPageRowRects(const RptrPageLayout& a, const rptr::UiRow& row, int index,
                              int scroll);
int rptrPageMaxScroll(const RptrPageLayout& a, int rowCount);

enum class RptrHit : uint8_t {
  None = 0,
  Scan, ListRow, Back, Refresh, BannerButton, BigButton, Apply,
  RowBody, RowMinus, RowPlus, RowKey, Input, Send,
};

struct RptrHitResult {
  RptrHit kind = RptrHit::None;
  int row = -1;
};

RptrHitResult rptrListHitAt(const RptrListLayout& r, int count, int scroll, int x, int y);

// One hit test for a page: the back key, the card's refresh, the banner's button, the Apply button,
// the console's input/Send, and the row's own parts. `screen` is the same model the drawing uses.
RptrHitResult rptrPageHitAt(const RptrPageLayout& a, const rptr::ScreenOut& screen, int scroll,
                            int x, int y);

// The one confirmation pop-up. A tap outside the box is a Cancel and is consumed.
struct RptrModalLayout {
  Rect box;
  Rect text;
  Rect cancel;
  Rect confirm;
};

RptrModalLayout rptrModalLayoutFor(const Layout& L, const char* verb);
enum class RptrModalHit : uint8_t { None = 0, Cancel, Confirm, Outside };
RptrModalHit rptrModalHitAt(const RptrModalLayout& m, int x, int y);

}  // namespace ui
}  // namespace om
