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
// EST-364: the refresh icon used to be drawn on top of the right column's values ("-119 dBm" ran
// under it in docs/shots/2026-11-10-2034-argus-logged-in.png). It now has a gutter of its own at the
// card's right edge, outside both value columns, and a 40x40 finger target.
constexpr int16_t kRptrRefreshIcon = 20;   // the ring that is drawn
constexpr int16_t kRptrRefreshTap = 40;    // the finger target around it
constexpr int16_t kRptrCardGutter = 42;    // reserved at the card's right: the 40 px target + 2
// EST-363: the page's own header is two lines. In EST-362 the title was drawn at the vertical centre
// of a 26 px band and the subtitle at the centre of a band that started 16 px below the top, so the
// two glyph rows sat on top of each other (docs/shots/2026-11-10-2008-rptr-page.png). One band per
// line: 18 px for the title, 14 px for the subtitle underneath it.
constexpr int16_t kRptrPageHeadH = 32;  // title 18 + subtitle 14
constexpr int16_t kRptrTitleH = 18;
constexpr int16_t kRptrSubH = 14;
constexpr int16_t kRptrBannerH = 22;    // one 11 px line and its padding (a short sentence)
constexpr int16_t kRptrApplyH = 30;
constexpr int16_t kRptrBigH = 34;
constexpr int16_t kRptrApplyGap = 3;    // between the last row and the Apply button
constexpr int16_t kRptrBigGap = 4;      // between the last row and the big Log in button
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
  // EST-366 (rule 2): the page is ONE scrolling column. `body` is the viewport; the card, the banner
  // and the rows are its content, drawn at their `...y - scroll` position and clipped to `body`. Only
  // the header (back + title + subtitle) is fixed. The rectangles below are the content positions at
  // scroll == 0, so the same rectangle answers a tap once the scroll is subtracted.
  Rect card;           // empty (w 0) when the page has no status card
  Rect refresh;        // the card's refresh ICON (kRptrRefreshIcon square, in its own gutter)
  Rect refreshTap;     // the finger target: kRptrRefreshTap square around the icon
  int16_t cardColW = 0;   // the width of ONE value column: the gutter is not part of it
  Rect banner;         // empty when there is no line to say
  Rect bannerButton;   // "Set clock" inside the banner
  Rect body;           // the scrolling viewport (clip rect)
  int16_t rowsY = 0;   // where row 0 starts, below the card and the banner (at scroll 0)
  Rect apply;          // the one Apply button (empty when the page has none)
  Rect big;            // the one big "Log in" button (empty otherwise)
  Rect input;          // console: the free command line
  Rect send;           // console: Send
  int visibleRows = 0;
  int contentTop() const { return body.y; }
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

// --------------------------------------------------------------------------------- the map picker
// EST-366 (rule 6): the Position page's map. It is the Map tab's own renderer (map.cpp, offline
// tiles) with a crosshair at the view centre; the coordinate under the map is the crosshair's.
// `apply` false means the draft still equals the repeater's position, so the Apply key is not drawn
// (rule 4 applies here too).
struct RptrMapPickLayout {
  Rect map;        // the tile area (map.cpp's 320x214 view)
  Rect back;       // "< back" over the map, under the status bar
  Rect zoomIn;
  Rect zoomOut;
  Rect panel;      // the coordinate bar along the bottom of the map
  Rect coords;     // the two coordinate lines' box
  Rect useGps;     // "Use my GPS position"
  Rect apply;      // empty when there is nothing to apply
  Rect crosshair;  // the target box around the crosshair; the view centre is its centre
};
RptrMapPickLayout rptrMapPickLayoutFor(const Layout& L, bool apply);

enum class RptrMapPickHit : uint8_t {
  None = 0, Back, ZoomIn, ZoomOut, UseGps, Apply, Map,
};
// Buttons win over the map; any other tap inside the tile area is a pan/drop.
RptrMapPickHit rptrMapPickHitAt(const RptrMapPickLayout& m, int x, int y);

}  // namespace ui
}  // namespace om
