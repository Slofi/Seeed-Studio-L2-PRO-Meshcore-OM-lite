// SPDX-License-Identifier: MIT
// RPTR screen geometry (EST-351, extended EST-358). Three stages, three sets of rectangles:
//   list    the scan page: a header with the Scan chip, the state line and the found rows
//   login   one repeater's login sheet: back, name, the facts card, the password field, Login, the
//           result line and Forget
//   admin   the remote CLI panel: back, name, six view chips, the scrolling rows and the answer strip
// Every rectangle is handed out here and the SAME rectangle answers the tap (rptrHitAt /
// rptrAdminRowAt), so no screen repeats a coordinate.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "layout.h"
#include "theme.h"

namespace om {
namespace ui {

constexpr int16_t kRptrHeadH = 26;     // header row under the status bar
constexpr int16_t kRptrRowH = 34;      // name + one mono line: a finger-sized list row
constexpr int16_t kRptrBackW = 64;
constexpr int16_t kRptrChipH = 18;     // the mock's .seg button
constexpr int16_t kRptrChipGap = 3;
constexpr int16_t kRptrAdminRowH = 34;
constexpr int16_t kRptrKeyW = 26;      // a row's small key (- / + / H / D)
constexpr int16_t kRptrKeyH = 24;
constexpr int16_t kRptrAnswerH = 34;   // the last command and its answer, above the tab bar

struct RptrLayout {
  Rect head;        // "REPEATERS"
  Rect scan;        // the Scan chip at the header's right
  Rect list;        // the scrolling list body
  Rect empty;       // the empty state's area
  Rect back;        // detail page: the back key
  Rect title;       // detail page: the name
  Rect sub;         // under the name: the one mono line (repeater, hops, heard)
  Rect infoCard;    // the facts card (login page)
  Rect loginLabel;
  Rect loginField;
  Rect loginButton;
  Rect result;      // the login result line
  Rect forget;      // "Forget" for a remembered repeater
  Rect note;        // the hint / warning box
  int visibleRows = 0;
};

struct RptrRow {
  int16_t y = 0;
  int16_t h = 0;
};

RptrLayout rptrLayoutFor(const Layout& L, bool detail);

// EST-352: the row's second line, built from facts that exist, each NAMED. An unknown path ("--")
// and an unknown time are dropped rather than printed as a lone dash, so a row with nothing to say
// is one line instead of a puzzle. Empty `out` means: draw no second line at all.
struct RptrRowFacts {
  const char* path = "";    // "direct", "a1 > b2", or "--" when the encoded path is unknown
  const char* heard = "";   // already formatted by stampText: "14:02", "Mon 14:02", or "--"
  const char* snr = "";     // "SNR 7.5 dB", empty when the advert carried none
};
void rptrRowMeta(char* out, size_t cap, const RptrRowFacts& facts);

// The row's rectangle in screen coordinates (may be above/below the list body under scroll).
RptrRow rptrRow(const RptrLayout& r, int index, int scroll);

// The row under a point, or -1. Only the visible part counts.
int rptrRowAt(const RptrLayout& r, int count, int scroll, int x, int y);

constexpr int rptrMaxScroll(const RptrLayout& r, int count) {
  const int total = count * (int)kRptrRowH;
  return total > r.list.h ? total - (int)r.list.h : 0;
}

enum class RptrHit : uint8_t {
  None = 0, Scan, Row, Back, LoginField, LoginButton, Forget
};

struct RptrHitResult {
  RptrHit kind = RptrHit::None;
  int row = -1;
};

// One hit test for both pages. `count` rows, `scroll` the list's px offset.
RptrHitResult rptrHitAt(const RptrLayout& r, bool detail, int count, int scroll, int x, int y);

// ------------------------------------------------------------------------------- the admin panel
constexpr int kRptrViewCount = 6;
// The mock's .seg labels, in its order. Neigh. is shortened like the mock's own button.
constexpr const char* kRptrViewLabel[kRptrViewCount] = {"Overview", "Radio", "Regions",
                                                        "Mesh", "Neigh.", "Actions"};

struct RptrAdminLayout {
  Rect back;
  Rect title;
  Rect sub;
  Rect chips[kRptrViewCount];
  Rect body;          // the scrolling rows
  Rect answer;        // the last command and its answer
  int visibleRows = 0;
};

RptrAdminLayout rptrAdminLayoutFor(const Layout& L);

// A row's pieces. `body` is the whole row; `keyA`/`keyB` are empty when the row has no small keys;
// `value` is the part the value text is drawn in (left of the keys).
struct RptrRowRects {
  Rect body;
  Rect label;
  Rect value;
  Rect keyA;
  Rect keyB;
};
// `keys` is 0 (none), 1 (one key at the right) or 2 (a -/+ or H/D pair).
RptrRowRects rptrRowRects(const RptrAdminLayout& a, int keys, int index, int scroll);
int rptrAdminMaxScroll(const RptrAdminLayout& a, int rowCount);

// What a point hits: `row` (0-based), and `part` 0 = the row body, 1 = key A, 2 = key B.
struct RptrRowHit {
  int row = -1;
  int part = 0;
};
// `keys` is one byte per row (how many small keys that row has), so one table drives both the
// drawing and the hit test.
RptrRowHit rptrAdminRowAt(const RptrAdminLayout& a, const uint8_t* keys, int rowCount, int scroll,
                          int x, int y);
int rptrAdminChipAt(const RptrAdminLayout& a, int x, int y);

}  // namespace ui
}  // namespace om
