// SPDX-License-Identifier: MIT
// EST-404, the C++ half: the charging bolt is now 7 x 11, drawn over the battery icon and its frame,
// with its tips 2 px above and below the body. This proves the geometry that the ticket asks for:
//  * the box is the middle 40-50 % of the 16 px body and is 4 px taller than the 7 px body;
//  * the tips really stand 2 px clear of the frame, and the 1 px dark edge stays inside the 26 px
//    status bar;
//  * with the WORST-CASE status row (100 %, "GPS 14", every optional icon on) the bolt and its edge
//    overdraw nothing: not the percent text, not the GPS cell, not the gear or any icon;
//  * every row still carries at least 2 px of gold with a >= 4 px dark edge, so the bolt reads over
//    the red low-battery fill as well as the green full one.
// No Arduino, no LovyanGFX: battery_bolt.h, status_cells.h and layout.h are plain C++.
#include <stdio.h>

#include "ui/battery_bolt.h"
#include "ui/layout.h"
#include "ui/status_cells.h"
#include "ui/theme.h"

using namespace om::ui;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// The worst-case status row: a clock, a three-digit percent, the widest GPS cell and every optional
// cell on. This is the row the real bar must keep clear of the bolt.
static StatusBarRects worstRects() {
  StatusCellWidths w;
  w.clock = 5 * kMonoStatusAdvancePx;   // "19:26"
  w.pct = 4 * kMonoStatusAdvancePx;     // "100%"
  w.gps = 6 * kMonoStatusAdvancePx;     // "GPS 14"
  StatusCellFlags f;
  f.fault = true;
  f.dnd = true;
  f.bt = true;
  f.wifi = true;
  return statusBarRects(l2(), w, f);
}

static void testBoltBoxOverTheBody() {
  // The three sizes the ticket names, as arithmetic.
  CHECK(kBoltH == kBattH + 4);                                  // 4 px taller than the body
  CHECK((kBoltH - kBattH) / 2 == 2);                            // 2 px of tip above and below
  CHECK(kBoltW * 100 >= kBattW * 40);                           // the middle 40..
  CHECK(kBoltW * 100 <= kBattW * 50);                           // ..to 50 % of the body

  const StatusBarRects r = worstRects();
  const int iconY = l2().status.y + l2().status.h / 2 - kBattH / 2;
  CHECK(r.battIcon.y == iconY);                                 // the icon's real top row
  const int bx = r.battIcon.x + (kBattW - kBoltW) / 2;
  const int by = iconY - (kBoltH - kBattH) / 2;

  const Rect body{r.battIcon.x, (int16_t)iconY, kBattW, kBattH};
  const Rect gold{(int16_t)bx, (int16_t)by, kBoltW, kBoltH};
  const Rect edge{(int16_t)(bx - 1), (int16_t)(by - 1), (int16_t)(kBoltW + 2),
                  (int16_t)(kBoltH + 2)};

  // The tips stand clear of the frame: 2 px above the top row and 2 px below the bottom row.
  CHECK(gold.y == body.y - 2);
  CHECK(gold.bottom() == body.bottom() + 2);
  CHECK(!body.contains(gold.x, gold.y));                        // the top tip is outside the body
  CHECK(!body.contains(gold.x, gold.bottom() - 1));             // so is the bottom tip

  // The 1 px dark edge still fits the 26 px status bar, and only the height leaves the icon: in x it
  // stays inside the 16 px body, short of the terminal nub on the right.
  CHECK(edge.y >= l2().status.y);
  CHECK(edge.bottom() <= l2().status.bottom());
  CHECK(edge.x >= body.x);
  CHECK(edge.right() <= body.right());
  const Rect nub{(int16_t)body.right(), (int16_t)(iconY + 2), 2, (int16_t)(kBattH - 4)};
  CHECK(!edge.overlaps(nub));

  // Centred on the body and on its centre line.
  CHECK(bx - body.x == 4);                                      // (16 - 7) / 2
  CHECK(gold.y + kBoltH / 2 == body.y + kBattH / 2);
}

static void testNothingElseIsOverdrawn() {
  const StatusBarRects r = worstRects();
  const int iconY = l2().status.y + l2().status.h / 2 - kBattH / 2;
  const int bx = r.battIcon.x + (kBattW - kBoltW) / 2;
  const int by = iconY - (kBoltH - kBattH) / 2;
  const Rect edge{(int16_t)(bx - 1), (int16_t)(by - 1), (int16_t)(kBoltW + 2),
                  (int16_t)(kBoltH + 2)};

  // Every other cell in the bar, on the worst-case row. The bolt may cover its own icon and the
  // battery cell's own background; nothing else.
  CHECK(!edge.overlaps(r.battText));   // "100%"
  CHECK(!edge.overlaps(r.gps));        // "GPS 14"
  CHECK(!edge.overlaps(r.gear));
  CHECK(!edge.overlaps(r.time));       // "19:26"
  CHECK(!edge.overlaps(r.fault));
  CHECK(!edge.overlaps(r.dnd));
  CHECK(!edge.overlaps(r.wifi));
  CHECK(!edge.overlaps(r.bt));
}

static void testReadableOnEveryFill() {
  // The gold core is at least 2 px in every row, and the dark edge adds 1 px each side, so the bolt
  // separates from the red 12 % fill (one interior column) and the green 100 % fill alike.
  for (int row = 0; row < kBoltH; ++row) {
    CHECK(kBoltRuns[row].w >= 2);
    CHECK(kBoltRuns[row].x >= 0);
    CHECK(kBoltRuns[row].x + kBoltRuns[row].w <= kBoltW);
  }
  // The middle bar is the widest row and the only full-width one.
  CHECK(kBoltRuns[kBoltH / 2].w == kBoltW);
  int widest = 0;
  for (int row = 0; row < kBoltH; ++row) {
    if (kBoltRuns[row].w > widest) widest = kBoltRuns[row].w;
  }
  CHECK(widest == kBoltW);
}

int main() {
  testBoltBoxOverTheBody();
  testNothingElseIsOverdrawn();
  testReadableOnEveryFill();
  if (g_fail == 0) {
    printf("est404: bolt over the frame, tips 2 px clear, nothing else overdrawn\n");
    return 0;
  }
  printf("est404: %d failure(s)\n", g_fail);
  return 1;
}
