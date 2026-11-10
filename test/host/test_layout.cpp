// Host test for src/ui/layout.h: splits, row rectangles under scroll, edge taps, tab overlap.
// Plain g++, no Arduino: layout.h and theme.h must stay device-free.
#include <stdio.h>

#include "ui/layout.h"

using namespace om::ui;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

int main() {
  const Layout& L = l2();

  // --- the screen splits exactly: no gap, no overlap, whole height ---------
  CHECK(L.status.x == 0 && L.status.y == 0 && L.status.w == 320 && L.status.h == 26);
  CHECK(L.content.x == 0 && L.content.y == 26 && L.content.w == 320 && L.content.h == 189);
  CHECK(L.tabbar.x == 0 && L.tabbar.y == 215 && L.tabbar.w == 320 && L.tabbar.h == 25);
  CHECK(L.status.bottom() == L.content.y);
  CHECK(L.content.bottom() == L.tabbar.y);
  CHECK(L.tabbar.bottom() == 240);
  CHECK(L.content.h == 240 - 26 - 25);

  // --- four tabs: adjacent, full width, no overlap --------------------------
  Rect tabs[kTabCount];
  for (int i = 0; i < kTabCount; ++i) {
    tabs[i] = L.tab((Tab)i);
    CHECK(tabs[i].x == i * 80);
    CHECK(tabs[i].y == L.tabbar.y);
    CHECK(tabs[i].w == 80);
    CHECK(tabs[i].h == 25);
    CHECK(tabs[i].x >= L.tabbar.x && tabs[i].right() <= L.tabbar.right());
    CHECK(L.tabAt(tabs[i].x + tabs[i].w / 2, tabs[i].y + tabs[i].h / 2) == i);
  }
  for (int i = 0; i < kTabCount; ++i) {
    for (int j = i + 1; j < kTabCount; ++j) CHECK(!tabs[i].overlaps(tabs[j]));
    if (i > 0) CHECK(tabs[i - 1].right() == tabs[i].x);
  }
  CHECK(tabs[0].x == L.tabbar.x && tabs[3].right() == L.tabbar.right());
  CHECK(L.tabAt(0, 215) == 0);
  CHECK(L.tabAt(319, 239) == 3);
  CHECK(L.tabAt(0, 214) == -1);   // one pixel above the bar is not a tab
  CHECK(L.tabAt(0, 240) == -1);
  CHECK(L.tabAt(320, 220) == -1);
  CHECK(L.tabAt(80, 220) == 1);   // exact shared edge belongs to the right-hand tab

  // --- rows: 30 px pitch, first row starts at the content top ---------------
  CHECK(L.visibleRows() == 7);                               // 189 / 30 -> 6 full + 1
  CHECK(L.row(0, 0).y == 26);
  CHECK(L.row(1, 0).y == 56);
  CHECK(L.row(1, 0).h == 30);
  CHECK(L.row(0, 0).w == 320);
  CHECK(L.row(0, 0).bottom() == L.row(1, 0).y);
  CHECK(!L.row(0, 0).overlaps(L.row(1, 0)));

  // Under scroll: row 0 moves up but its visible part is clipped at the content top.
  CHECK(L.row(0, 17).y == 9);
  CHECK(L.row(0, 17).bottom() == 39);
  CHECK(L.rowVisible(0, 17).y == 26);
  CHECK(L.rowVisible(0, 17).bottom() == 39);
  CHECK(L.rowVisible(0, 17).h == 13);
  CHECK(L.rowVisible(1, 17).y == 39);
  CHECK(L.rowVisible(1, 17).h == 30);

  // A row scrolled fully above the content is not visible and cannot be tapped.
  CHECK(L.rowVisible(0, 30).empty());
  CHECK(L.rowAt(10, 26, 30, 10) == 1);
  CHECK(L.rowAt(10, 25, 30, 10) == -1);

  // The last row is clipped by the tab bar, not drawn under it.
  const int big = 20;
  const int sc = L.maxScroll(big);
  CHECK(sc == big * 30 - 189);
  CHECK(L.rowVisible(big - 1, sc).bottom() == L.content.bottom());
  CHECK(L.rowVisible(big, sc).empty());

  // maxScroll is 0 when everything fits.
  CHECK(L.maxScroll(3) == 0);
  CHECK(L.maxScroll(0) == 0);

  // --- edge taps: top edge in, bottom edge belongs to the next row ----------
  const Rect r1 = L.row(1, 0);   // y 56 .. 85
  CHECK(r1.contains(0, 56));     // top-left is inside
  CHECK(r1.contains(319, 56));
  CHECK(r1.contains(0, 85));     // last pixel row is inside
  CHECK(!r1.contains(0, 86));    // bottom edge is not
  CHECK(!r1.contains(320, 60));  // right edge is not
  CHECK(L.rowAt(0, 56, 0, 5) == 1);
  CHECK(L.rowAt(0, 85, 0, 5) == 1);
  CHECK(L.rowAt(0, 86, 0, 5) == 2);
  CHECK(L.rowAt(0, 25, 0, 5) == -1);
  CHECK(L.rowAt(0, 215, 0, 5) == -1);   // under the tab bar
  CHECK(L.rowAt(0, 500, 0, 5) == -1);

  // A count smaller than the index asked for never returns a phantom row.
  CHECK(L.rowAt(0, 116, 0, 2) == -1);

  // --- gear: inside the status bar, right aligned, and the same rect hit-tests ---------------
  // 13 px since EST-332 (the vector gear mask); the slot is the mask's size, not a literal.
  const Rect g = L.gear();
  CHECK(kGearSize == 13);
  CHECK(g.w == kGearSize && g.h == kGearSize);
  CHECK(g.x + g.w == 320 - 6);
  CHECK(g.y >= 0 && g.bottom() <= 26);
  CHECK(g.contains(g.x, g.y));
  CHECK(g.contains(g.x + g.w - 1, g.y + g.h - 1));
  CHECK(!g.contains(g.x + g.w, g.y));

  // --- a non-320 screen still splits without overlap ------------------------
  const Layout tan = layoutFor(800, 480);
  CHECK(tan.content.h == 480 - 26 - 25);
  CHECK(tan.tab((Tab)3).right() == 800);
  for (int i = 0; i < kTabCount; ++i) {
    for (int j = i + 1; j < kTabCount; ++j) CHECK(!tan.tab((Tab)i).overlaps(tan.tab((Tab)j)));
  }

  if (g_fail) {
    printf("%d layout check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("layout: all checks passed\n");
  return 0;
}
