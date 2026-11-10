// SPDX-License-Identifier: MIT
// RPTR screen geometry (EST-351): the repeater list and the repeater page. Plain C++ so test/host
// proves the rectangles and the hit test; screen_rptr.cpp only fills them with the mock's look.
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

struct RptrLayout {
  Rect head;        // "REPEATERS IN RANGE"
  Rect scan;        // the Scan chip at the header's right
  Rect list;        // the scrolling list body
  Rect empty;       // the empty state's area
  Rect back;        // detail page: the back key
  Rect title;       // detail page: the name
  Rect infoCard;    // the status card (path, last heard, hops, SNR)
  Rect loginLabel;
  Rect loginField;
  Rect loginButton;
  Rect note;        // "Remote admin ... not built yet"
  int visibleRows = 0;
};

struct RptrRow {
  int16_t y = 0;
  int16_t h = 0;
};

RptrLayout rptrLayoutFor(const Layout& L, bool detail);

// EST-352: the row's second line. It is built from facts that exist, each NAMED: the last-heard
// TIME (never a bare age here), the SNR, the path (the route MeshCore's advert took). An unknown
// path ("--" from decodeOutPathLen) and an unknown time are dropped rather than printed as a lone
// dash, so a row with nothing to say is one line instead of a puzzle. Empty `out` means: draw no
// second line at all.
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
  None = 0, Scan, Row, Back, LoginField, LoginButton
};

struct RptrHitResult {
  RptrHit kind = RptrHit::None;
  int row = -1;
};

// One hit test for both pages. `count` rows, `scroll` the list's px offset.
RptrHitResult rptrHitAt(const RptrLayout& r, bool detail, int count, int scroll, int x, int y);

}  // namespace ui
}  // namespace om
