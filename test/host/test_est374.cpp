// SPDX-License-Identifier: MIT
// EST-374 host test: the rest of Filip's "hide" idea (2026-11-11).
//   A. the map canvas is the FULL map area (status bar's bottom row .. the screen floor) in both
//      states, so the centre (cross, follow, GPS, the picker's crosshair) never moves on a toggle;
//   B. the chip hides the bottom page bar too: a 3 px strip on the floor that takes no taps, and
//      every other screen keeps its own full bar (mapBarsTucked);
//   C. the hide/show key is a 22 x 22 chip (kPanel/kLine) with a 2 px chevron in kInk, its label in
//      the dark map ink immediately to its left, and a 30 x 26 target that never reaches the top tab;
//   D. an unread dot on the chip's corner, only while the bars are hidden;
//   E. the zoom + / - stay behind as grey ghosts at the SHOWN tab rectangles, which are also their
//      tap zones while hidden (2 px left of one is a map tap);
//   F. the slivers are 3 px and a tap on one still brings the bars back;
//   G. the slide is 4 frames of chrome over the cached canvas, ~31 ms each (under the 35 ms limit).
// Plain g++, no Arduino (ui/map_chrome.cpp + ui/rptr_layout.cpp).
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui/map_chrome.h"
#include "ui/map_layers.h"   // EST-374: the saved tabsHidden key drives both groups
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

// --- A. the full-screen canvas and its one centre ---------------------------------------------------
static void testFullCanvas() {
  const Layout& L = l2();
  // 320 x (240 - 26) = 320 x 214, from the status bar's bottom row to the screen floor. The height
  // is the SAME 214 rows the old canvas had; what changed is that it now starts at 26 instead of 0
  // (26 rows used to be pushed under the status bar and the bottom row of the map band was never
  // pushed at all), so the PSRAM allocation does not grow.
  CHECK(kMapCanvasW == 320);
  CHECK(kMapCanvasTop == kStatusH);
  CHECK(kMapCanvasH == kScreenH - kStatusH);
  CHECK(kMapCanvasH == 214);
  CHECK(kMapCanvasTop + kMapCanvasH == kScreenH);
  CHECK(kMapCentreX == 160);
  CHECK(kMapCentreY == 133);   // 26 + 214/2
  CHECK(kMapCentreY == kMapCanvasTop + kMapCanvasH / 2);

  const MapChrome shown = mapChromeFor(L, false, false);
  const MapChrome hidden = mapChromeFor(L, false, true);
  for (int i = 0; i < 2; ++i) {
    const MapChrome& c = i == 0 ? shown : hidden;
    CHECK(c.view.x == 0 && c.view.w == 320);
    CHECK(c.view.y == L.status.bottom());
    CHECK(c.view.h == 214);
    CHECK(c.view.bottom() == L.height);          // the canvas reaches the screen floor
    CHECK(c.creditBottom > 0);
  }
  // The centre is the view's, in BOTH states (hiding the bars is chrome-only, the map never moves).
  const MapPoint ps = mapCentreOf(shown);
  const MapPoint ph = mapCentreOf(hidden);
  CHECK(ps.x == ph.x && ps.y == ph.y);
  CHECK(ps.x == kMapCentreX && ps.y == kMapCentreY);
  const Rect cs = mapCentreCrossRect(shown);
  const Rect ch = mapCentreCrossRect(hidden);
  CHECK(cs.x == ch.x && cs.y == ch.y && cs.w == ch.w && cs.h == ch.h);
  CHECK(cs.x + cs.w / 2 == 160 && cs.y + cs.h / 2 == 133);

  // The Position picker's crosshair is the same point (map.cpp subtracts the same origin when it
  // turns the centre back into lat/lon, so the picker's value is the view centre).
  const RptrMapPickLayout pick = rptrMapPickLayoutFor(L, false);
  CHECK(pick.map.y == kMapCanvasTop && pick.map.h == kMapCanvasH);
  CHECK(pick.crosshair.w == 12 && pick.crosshair.h == 12);
  CHECK(pick.crosshair.x + pick.crosshair.w / 2 == kMapCentreX);
  CHECK(pick.crosshair.y + pick.crosshair.h / 2 == kMapCentreY);
}

// --- C. the chip ---------------------------------------------------------------------------------
static void testChip() {
  const Layout& L = l2();
  const MapChrome c = mapChromeFor(L, false, false);

  // 22 x 22, kPanel fill and kLine edge at the top right below the status bar, flush with the tabs'
  // right edge (the drawing in screen_map.cpp uses kPanel/kLine/kRadius; the sizes are here).
  CHECK(kMapChipSize == 22);
  CHECK(c.chip.w == 22 && c.chip.h == 22);
  CHECK(c.chip.right() == 320);
  CHECK(c.chip.right() == c.names.right());
  CHECK(c.chip.right() == c.view.right());
  CHECK(c.chip.y == L.status.bottom());
  CHECK(!c.chip.overlaps(c.status));

  // The chevron: a 4 x 7 box with a 2 px stroke (the ticket's "1.5-2 px") inside the chip, on the
  // chip's centre line. It points ">" while the bars are out and "<" while they are tucked; the
  // drawing reads c.tabsHidden for that, and it is the only thing in the chip besides the dot.
  CHECK(kMapChevronStroke == 2);
  CHECK(c.chevron.x >= c.chip.x && c.chevron.right() <= c.chip.right());
  CHECK(c.chevron.y >= c.chip.y && c.chevron.bottom() <= c.chip.bottom());
  CHECK(c.chevron.x + c.chevron.w / 2 >= c.chip.x + c.chip.w / 2 - 1);
  CHECK(c.chevron.x + c.chevron.w / 2 <= c.chip.x + c.chip.w / 2 + 1);
  CHECK(c.chevron.y + c.chevron.h / 2 >= c.chip.y + c.chip.h / 2 - 1);
  CHECK(c.chevron.y + c.chevron.h / 2 <= c.chip.y + c.chip.h / 2 + 1);

  // The zoom label is IMMEDIATELY left of the chip, same vertical centre, in the cross's dark ink.
  CHECK(c.zoomLabel.right() + kMapChipGap == c.chip.x);
  CHECK(c.zoomLabel.h == kMapZoomLabelH);
  CHECK(c.zoomLabel.y >= L.status.bottom());
  CHECK(c.zoomLabel.y + c.zoomLabel.h / 2 >= c.chip.y + c.chip.h / 2 - 1);
  CHECK(c.zoomLabel.y + c.zoomLabel.h / 2 <= c.chip.y + c.chip.h / 2 + 1);
  CHECK(kMapCrossInk == rgb565(0x202020));   // the ink the label is drawn in (screen_map.cpp)

  // The target: at least 30 x 26, flush right with the chip, above the top tab and never on it.
  CHECK(kMapChipTapW >= 30 && kMapChipTapH >= 26);
  CHECK(c.chipTap.w == 30 && c.chipTap.h == 26);
  CHECK(c.chipTap.right() == c.chip.right());
  CHECK(c.chipTap.y == c.chip.y);
  CHECK(c.chipTap.contains(c.chip.x, c.chip.y));
  CHECK(c.chipTap.contains(c.chevron.x, c.chevron.y));
  CHECK(!c.chipTap.overlaps(c.names));
  CHECK(!c.chipTap.overlaps(c.status));
  CHECK(c.names.y >= c.chipTap.bottom() + kMapChipGap);   // below the chip, with the usual gap

  // The whole target is the hide/show key, in both states; one pixel left of it is the map.
  const MapChrome hidden = mapChromeFor(L, false, true);
  CHECK(mapHitAt(c, nullptr, 0, c.chipTap.x, c.chipTap.y).kind == MapHitKind::TabsToggle);
  CHECK(mapHitAt(c, nullptr, 0, c.chipTap.right() - 1, c.chipTap.bottom() - 1).kind ==
        MapHitKind::TabsToggle);
  CHECK(mapHitAt(hidden, nullptr, 0, hidden.chipTap.x, hidden.chipTap.y).kind ==
        MapHitKind::TabsToggle);
  CHECK(mapHitAt(c, nullptr, 0, c.chipTap.x - 1, c.chipTap.y + 1).kind == MapHitKind::Map);
  // The chip keeps its place in both states: hiding the bars must not move the way back.
  CHECK(hidden.chip.x == c.chip.x && hidden.chip.y == c.chip.y);
  CHECK(hidden.chipTap.x == c.chipTap.x && hidden.chipTap.y == c.chipTap.y);
  CHECK(hidden.chevron.x == c.chevron.x && hidden.chevron.y == c.chevron.y);
  CHECK(hidden.zoomLabel.x == c.zoomLabel.x && hidden.zoomLabel.y == c.zoomLabel.y);
}

// --- B. the tucked bottom bar --------------------------------------------------------------------
static void testTuckedBar() {
  const Layout& L = l2();
  const MapChrome shown = mapChromeFor(L, false, false);
  const MapChrome hidden = mapChromeFor(L, false, true);

  // Shown: the bar is the real 25 px page bar and its four zones are exactly as before.
  CHECK(L.tabbar.y == 215 && L.tabbar.h == 25);
  CHECK(shown.zoomOut.bottom() == L.tabbar.y);
  for (int i = 0; i < kTabCount; ++i) {
    const Rect r = L.tab((Tab)i);
    CHECK(r.y == L.tabbar.y && r.h == 25);
    CHECK(L.tabAt(r.x + r.w / 2, r.y + r.h / 2) == i);
  }

  // Hidden: a 3 px strip in kPanel on the screen floor, showing the Map tab's gold segment. The
  // screen draws it from c.barStrip and l2().tab(v.tabs.active) (screen_map.cpp drawBarStrip).
  CHECK(kMapBarStripH == 3);
  CHECK(hidden.barStrip.x == 0 && hidden.barStrip.w == 320);
  CHECK(hidden.barStrip.h == 3);
  CHECK(hidden.barStrip.bottom() == L.height);
  CHECK(hidden.barStrip.y == 237);
  CHECK(shown.barStrip.y == hidden.barStrip.y);   // the strip's place does not depend on the state
  // The strip takes no taps: mapHitAt has no rectangle for it, so those rows are map gestures.
  CHECK(mapHitAt(hidden, nullptr, 0, 100, 238).kind == MapHitKind::Map);
  CHECK(mapHitAt(hidden, nullptr, 0, 160, 238).kind == MapHitKind::Map);
  CHECK(mapHitAt(hidden, nullptr, 0, 319, 239).kind == MapHitKind::Map);
  // ...and a drag that starts there pans: the rows are inside the map's own view rectangle.
  CHECK(hidden.view.contains(100, 238));
  CHECK(hidden.view.contains(319, 239));
  // It also must never eat the attribution.
  CHECK(mapCreditRect(hidden, 138).bottom() < hidden.barStrip.y);

  // The saved key drives BOTH groups, and the tuck belongs to the Map screen only: every other
  // screen draws its own full bar, so leaving the Map by any route shows the bar on the next one.
  CHECK(!kMapTabsHiddenDefault);
  CHECK(mapBarsTucked(true, true));
  CHECK(!mapBarsTucked(true, false));    // Nodes, Chat, RPTR (and Settings): never tucked
  CHECK(!mapBarsTucked(false, true));
  for (int i = 0; i < kTabCount; ++i) {
    const bool onMap = ((Tab)i == Tab::Map);
    CHECK(mapBarsTucked(true, onMap) == onMap);
    CHECK(!mapBarsTucked(false, onMap));
  }
}

// --- D. the unread dot on the chip ---------------------------------------------------------------
static void testUnreadDot() {
  const Layout& L = l2();
  const MapChrome hidden = mapChromeFor(L, false, true);

  CHECK(kMapUnreadDotD == 6);
  CHECK(!mapChipShowsUnread(false, 7));   // shown: the bar's own badge says it
  CHECK(!mapChipShowsUnread(true, 0));    // nothing unread: no dot
  CHECK(!mapChipShowsUnread(true, -1));
  CHECK(mapChipShowsUnread(true, 1));
  CHECK(mapChipShowsUnread(true, 99));

  // On the chip's top-left corner, entirely inside the map area (the status bar is drawn last and
  // would cover anything above it) and on the chip.
  const Rect d = mapChipUnreadDotRect(hidden);
  CHECK(d.w == 6 && d.h == 6);
  CHECK(d.x == hidden.chip.x && d.y == hidden.chip.y);
  CHECK(d.y >= L.status.bottom());
  CHECK(d.right() <= hidden.chip.x + hidden.chip.w / 2);
  CHECK(d.bottom() <= hidden.chip.y + hidden.chip.h / 2);
  CHECK(!d.overlaps(hidden.status));
  // Gold, the same accent the bar's new-node dot uses.
  CHECK(kAcc == rgb565(0xe8b04f));
}

// --- E. the ghost zoom glyphs --------------------------------------------------------------------
static void testGhostZooms() {
  const Layout& L = l2();
  const MapChrome shown = mapChromeFor(L, false, false);
  const MapChrome hidden = mapChromeFor(L, false, true);

  // The SLIVER is the only tab pixel left: 3 px at the screen edge, at the shown rows and height.
  CHECK(kMapTabSliverW == 3);
  MapTab hs[kMapTabCount];
  MapTab ss[kMapTabCount];
  mapTabRects(hidden, hs);
  mapTabRects(shown, ss);
  for (int i = 0; i < kMapTabCount; ++i) {
    CHECK(hs[i].rect.w == 3);
    CHECK(hs[i].rect.x == 317);
    CHECK(hs[i].rect.right() == 320);
    CHECK(hs[i].rect.y == ss[i].rect.y);
    CHECK(hs[i].rect.h == shown.names.h);
    // A tap on a sliver brings the bars back (never a hidden switch).
    CHECK(mapHitAt(hidden, nullptr, 0, hs[i].rect.x + 1, hs[i].rect.y + 2).kind ==
          MapHitKind::TabsToggle);
  }

  // The ghost zones ARE the shown zoom tab rectangles, and their centres are the same points.
  CHECK(hidden.zoomInShown.x == shown.zoomIn.x && hidden.zoomInShown.y == shown.zoomIn.y);
  CHECK(hidden.zoomInShown.w == shown.zoomIn.w && hidden.zoomInShown.h == shown.zoomIn.h);
  CHECK(hidden.zoomOutShown.x == shown.zoomOut.x && hidden.zoomOutShown.y == shown.zoomOut.y);
  CHECK(hidden.zoomOutShown.w == shown.zoomOut.w && hidden.zoomOutShown.h == shown.zoomOut.h);
  CHECK(hidden.zoomInShown.x + hidden.zoomInShown.w / 2 ==
        shown.zoomIn.x + shown.zoomIn.w / 2);
  CHECK(hidden.zoomInShown.y + hidden.zoomInShown.h / 2 ==
        shown.zoomIn.y + shown.zoomIn.h / 2);

  // Active only while hidden: the glyph's centre zooms, and 2 px left of the zone is a map tap.
  CHECK(mapHitAt(hidden, nullptr, 0, hidden.zoomInShown.x + 12,
                 hidden.zoomInShown.y + 15).kind == MapHitKind::ZoomIn);
  CHECK(mapHitAt(hidden, nullptr, 0, hidden.zoomOutShown.x + 12,
                 hidden.zoomOutShown.y + 15).kind == MapHitKind::ZoomOut);
  CHECK(mapHitAt(hidden, nullptr, 0, hidden.zoomInShown.x - 2,
                 hidden.zoomInShown.y + 15).kind == MapHitKind::Map);
  CHECK(mapHitAt(hidden, nullptr, 0, hidden.zoomOutShown.x - 2,
                 hidden.zoomOutShown.y + 15).kind == MapHitKind::Map);
  // Shown, the same pixels are the tab itself (no ghost zone exists then).
  CHECK(mapHitAt(shown, nullptr, 0, shown.zoomInShown.x + 12,
                 shown.zoomInShown.y + 15).kind == MapHitKind::ZoomIn);
  CHECK(mapHitAt(shown, nullptr, 0, shown.zoomInShown.x - 2,
                 shown.zoomInShown.y + 15).kind == MapHitKind::Map);
  // The 3 px sliver wins over the ghost zone: the hide/show key is the visible thing.
  CHECK(mapHitAt(hidden, nullptr, 0, 319, hidden.zoomInShown.y + 15).kind ==
        MapHitKind::TabsToggle);
  // A marker still wins over both (the map's one rule).
  MapMarker m;
  m.x = (int16_t)(hidden.zoomInShown.x + 12);
  m.y = (int16_t)(hidden.zoomInShown.y + 15);
  m.type = 2;
  m.name = "R1";
  CHECK(mapHitAt(hidden, &m, 1, m.x, m.y).kind == MapHitKind::Marker);

  // The ghost is the SAME character as the tab's (SansTitle "+" / "-"), so only the ink changes:
  // the tab paints kInk on kPanel, the ghost paints kMapGhostInk with no body.
  CHECK(kMapGhostInk == rgb565(0x707070));
  CHECK(kMapGhostInk != kInk);
  CHECK(kMapGhostInk != kPanel);
}

// --- G. the slide ---------------------------------------------------------------------------------
static void testSlide() {
  // Both groups move 22 px (a 25 px tab -> 3 px sliver; a 25 px bar -> 3 px strip), which is why one
  // number drives both. The header's static_assert enforces the equality.
  CHECK(kMapTabSlideDx == kMapTabW - kMapTabSliverW);
  CHECK(kMapTabSlideDx == 22);
  CHECK(kMapBarSlideDy == kTabH - kMapBarStripH);
  CHECK(kMapBarSlideDy == 22);
  CHECK(kMapTabSlideDx == kMapBarSlideDy);

  // 4 frames: 3 interpolated steps plus the steady one, 22 px over 4 steps = 5, 11 and 16 px.
  CHECK(kMapSlideSteps >= 4 && kMapSlideSteps <= 5);
  int prev = 0;
  for (int i = 1; i < kMapSlideSteps; ++i) {
    const int off = kMapTabSlideDx * i / kMapSlideSteps;
    CHECK(off > prev);
    CHECK(off < kMapTabSlideDx);
    prev = off;
  }
  CHECK(kMapTabSlideDx * 1 / 4 == 5 && kMapTabSlideDx * 2 / 4 == 11 &&
        kMapTabSlideDx * 3 / 4 == 16);

  // The cost estimate from EST-371's measured numbers: a cached-canvas repush (19.8 ms) plus one
  // panel push (11.5 ms) is 31.3 ms per frame, under the ticket's ~35 ms limit for an animated
  // switch, and 4 frames is 125 ms inside the 120-150 ms window. If the device ever measures more
  // than 35 ms, kMapSlideSteps goes to 1 (the instant switch) instead.
  const double repushMs = 19.79;
  const double pushMs = 11.5;
  const double stepMs = repushMs + pushMs;
  CHECK(stepMs <= 35.0);
  const double totalMs = stepMs * kMapSlideSteps;
  CHECK(totalMs >= 120.0 && totalMs <= 150.0);
}

int main() {
  testFullCanvas();
  testChip();
  testTuckedBar();
  testUnreadDot();
  testGhostZooms();
  testSlide();
  if (g_fail) {
    printf("est374: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("est374: full canvas centre (160,133) in both states, 22 px chip + 30x26 target, "
         "3 px tucked strip with no taps, ghost zooms on the shown rects, unread dot, 4-frame slide "
         "ok\n");
  return 0;
}
