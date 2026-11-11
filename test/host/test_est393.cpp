// SPDX-License-Identifier: MIT
// EST-393 host test: the map picker's banner and its mark (Filip 2026-11-11 16:38).
//   A. the coordinate banner sits FLUSH with the screen floor: full width, bottom == 240, and the
//      coordinate line and both keys hang off its own rectangle;
//   B. the keys' drawn rectangles and their tap rectangles are the SAME rectangles (the hit test
//      and pressTargetForRptrMapPick both read m.useGps / m.apply);
//   C. the picker's mark is the Map's own centre cross: the same 7x7 box, the same centre (160,133),
//      the same drawing function, two 1 px lines in kMapCrossInk and no dot, ring or outline;
//   D. the cross's centre is the pixel the picker turns into a coordinate (the shell's
//      mapScreenToLatLon(kMapCentreX, kMapCentreY), checked at the source level by test_est393.py).
// Plain g++, no Arduino (ui/rptr_layout.cpp + ui/map_chrome.cpp over the firmware's own headers).
#include <initializer_list>
#include <stdio.h>

#include "ui/map_chrome.h"
#include "ui/press_targets.h"
#include "ui/rptr_layout.h"

using namespace om::ui;

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// A recorder standing in for LovyanGFX: drawMapCentreCross only calls fillRect, and the trace below
// proves it makes exactly the two 1 px strokes and nothing else.
struct Op {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  unsigned colour = 0;
};

struct FakeDev {
  Op ops[8];
  int count = 0;
  void fillRect(int x, int y, int w, int h, unsigned colour) {
    if (count < 8) ops[count++] = Op{x, y, w, h, colour};
  }
};

static bool sameOp(const Op& o, int x, int y, int w, int h, unsigned colour) {
  return o.x == x && o.y == y && o.w == w && o.h == h && o.colour == colour;
}

// --- A. the banner on the screen floor ------------------------------------------------------------
static void testBanner() {
  const Layout& L = l2();
  const RptrMapPickLayout clean = rptrMapPickLayoutFor(L, false);

  // 42 px, the full screen width, and its bottom edge IS the bottom edge of the glass.
  CHECK(kRptrPickPanelH == 42);
  CHECK(clean.panel.x == 0);
  CHECK(clean.panel.w == L.width);
  CHECK(clean.panel.w == 320);
  CHECK(clean.panel.h == kRptrPickPanelH);
  CHECK(clean.panel.bottom() == L.height);
  CHECK(clean.panel.bottom() == 240);
  CHECK(clean.panel.y == L.height - kRptrPickPanelH);
  CHECK(clean.panel.y == 198);
  // It used to end at the tab bar's top edge (215), which is why it hovered above the floor.
  CHECK(clean.panel.bottom() > L.tabbar.y);

  // The coordinate line and both keys are placed from the banner's own rectangle, inside it.
  CHECK(clean.coords.y >= clean.panel.y);
  CHECK(clean.coords.bottom() <= clean.panel.bottom());
  CHECK(clean.coords.x >= 0 && clean.coords.right() <= L.width);
  CHECK(clean.useGps.y >= clean.panel.y);
  CHECK(clean.useGps.bottom() <= clean.panel.bottom());
  CHECK(clean.useGps.x >= 0 && clean.useGps.right() <= L.width);

  const RptrMapPickLayout keys = rptrMapPickLayoutFor(L, true);
  for (const Rect* key : {&keys.useGps, &keys.apply}) {
    CHECK(key->y >= keys.panel.y);
    CHECK(key->bottom() <= keys.panel.bottom());
    CHECK(key->x >= 0 && key->right() <= L.width);
  }
  CHECK(keys.useGps.y == keys.panel.y + 20);
  CHECK(keys.apply.y == keys.panel.y + 20);
  CHECK(keys.useGps.bottom() == keys.panel.bottom() - 2);
  CHECK(keys.apply.bottom() == keys.panel.bottom() - 2);
  CHECK(keys.useGps.right() <= keys.apply.x);   // the two keys never overlap
  CHECK(keys.apply.right() == L.width - 8);

  // A taller/narrower layout keeps the same rule: the banner follows the floor, not the tab bar.
  const Layout wide = layoutFor(800, 480);
  const RptrMapPickLayout big = rptrMapPickLayoutFor(wide, true);
  CHECK(big.panel.bottom() == wide.height);
  CHECK(big.panel.bottom() == 480);
  CHECK(big.useGps.bottom() == big.panel.bottom() - 2);
  CHECK(big.apply.bottom() == big.panel.bottom() - 2);
}

// --- B. drawn rect == tapped rect ----------------------------------------------------------------
static void testKeys() {
  const Layout& L = l2();
  const RptrMapPickLayout m = rptrMapPickLayoutFor(L, true);
  const int gx = m.useGps.x + m.useGps.w / 2;
  const int gy = m.useGps.y + m.useGps.h / 2;
  const int ax = m.apply.x + m.apply.w / 2;
  const int ay = m.apply.y + m.apply.h / 2;

  // The one hit test and the pressed look both read the layout's rectangles.
  CHECK(rptrMapPickHitAt(m, gx, gy) == RptrMapPickHit::UseGps);
  CHECK(rptrMapPickHitAt(m, ax, ay) == RptrMapPickHit::Apply);
  const PressTarget gps = pressTargetForRptrMapPick(m, gx, gy);
  const PressTarget app = pressTargetForRptrMapPick(m, ax, ay);
  CHECK(gps.valid() && gps.rect.x == m.useGps.x && gps.rect.y == m.useGps.y &&
        gps.rect.w == m.useGps.w && gps.rect.h == m.useGps.h);
  CHECK(app.valid() && app.rect.x == m.apply.x && app.rect.y == m.apply.y &&
        app.rect.w == m.apply.w && app.rect.h == m.apply.h);

  // Their old place is now plain map: the banner and its keys moved together, so a tap where the
  // key used to hover is no longer a key.
  const int oldKeyY = L.tabbar.y - 42 + 20 + 10;   // the old panel's key centre
  CHECK(oldKeyY == 203);
  CHECK(rptrMapPickHitAt(m, gx, oldKeyY) == RptrMapPickHit::Map);
  CHECK(!pressTargetForRptrMapPick(m, gx, oldKeyY).valid());
}

// --- C. the Map's own centre cross, at the Map's centre ------------------------------------------
static void testCross() {
  const Layout& L = l2();
  const RptrMapPickLayout m = rptrMapPickLayoutFor(L, false);
  const MapChrome chrome = mapChromeFor(L, false, false);
  const Rect mapCross = mapCentreCrossRect(chrome);

  // Same rectangle as the Map screen's cross: 7x7, centred on (160,133).
  CHECK(kMapCentreCrossArm == 3);
  CHECK(mapCross.w == 7 && mapCross.h == 7);
  CHECK(m.cross.x == mapCross.x && m.cross.y == mapCross.y);
  CHECK(m.cross.w == mapCross.w && m.cross.h == mapCross.h);
  CHECK(m.cross.x == 157 && m.cross.y == 130);
  CHECK(m.cross.x + m.cross.w / 2 == kMapCentreX);
  CHECK(m.cross.y + m.cross.h / 2 == kMapCentreY);
  CHECK(kMapCentreX == 160 && kMapCentreY == 133);
  CHECK(mapCentreCrossAt(160, 133).x == m.cross.x && mapCentreCrossAt(160, 133).y == m.cross.y);

  // The one shared drawing: two 1 px lines, 7 px long, in the dark map ink, and NOTHING else (no
  // dot, no ring, no outline, no plate). The Map's own cross box produces the very same pixels.
  FakeDev picker;
  drawMapCentreCross(&picker, m.cross);
  CHECK(picker.count == 2);
  CHECK(sameOp(picker.ops[0], 157, 133, 7, 1, kMapCrossInk));
  CHECK(sameOp(picker.ops[1], 160, 130, 1, 7, kMapCrossInk));

  FakeDev map;
  drawMapCentreCross(&map, mapCross);
  CHECK(map.count == picker.count);
  for (int i = 0; i < map.count && i < picker.count; ++i) {
    CHECK(sameOp(picker.ops[i], map.ops[i].x, map.ops[i].y, map.ops[i].w, map.ops[i].h,
                 map.ops[i].colour));
  }
}

// --- D. the cross centre is the coordinate pixel (the layout half) --------------------------------
static void testCentre() {
  const Layout& L = l2();
  const RptrMapPickLayout m = rptrMapPickLayoutFor(L, true);
  // The picker's own cross box and the Map's are one rectangle, so the shell's
  // mapScreenToLatLon(kMapCentreX, kMapCentreY) is exactly under the drawn cross.
  CHECK(m.cross.x + m.cross.w / 2 == kMapCentreX);
  CHECK(m.cross.y + m.cross.h / 2 == kMapCentreY);
  // The banner can never cover the cross: a finger on the mark is always a map drop.
  CHECK(!m.panel.contains(m.cross.x + m.cross.w / 2, m.cross.y + m.cross.h / 2));
  CHECK(rptrMapPickHitAt(m, m.cross.x + m.cross.w / 2, m.cross.y + m.cross.h / 2) ==
        RptrMapPickHit::Map);
}

int main() {
  testBanner();
  testKeys();
  testCross();
  testCentre();
  if (g_fail == 0) {
    printf("est393: floor banner, moving key rects, the Map's own cross at (160,133) ok\n");
  }
  return g_fail == 0 ? 0 : 1;
}
