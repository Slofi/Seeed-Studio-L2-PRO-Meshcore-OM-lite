// SPDX-License-Identifier: MIT
// EST-416 host test: the top layer wins the tap, and a quick re-touch does not jump the map.
//
// BUG 1 (Filip on the glass 2026-11-12): a repeater marker lay under the "-" zoom tab, and tapping
// "-" opened the repeater's info pop-up. mapHitAt() tested MARKERS FIRST (the EST-374 rule "a marker
// still wins over everything"), so any marker under a drawn button stole its tap. The hit test is now
// the DRAW ORDER, top layer first, and it DEPENDS ON THE BARS:
//   shown : status bar -> chip -> the five tabs -> the callout -> markers -> map
//   hidden: status bar -> chip -> the 6 px slivers -> the visible ghost + / - glyph boxes ->
//           the callout -> markers -> the REST of the ghost zoom zones -> map
// So a marker under a SHOWN button never wins, a marker under a HIDDEN ghost glyph does not win
// either, and a marker elsewhere in a hidden ghost zone does. pressTargetForMap() derives from the
// same test, so the pressed look follows (one order, no second copy).
//
// BUG 2: touching the map again within 400 ms / 80 px of a release is a pan continuation (the panel
// lifts between samples), but the old press code panned by g_lastX - x with g_lastX still the
// PREVIOUS swipe's end point, so a fast new swipe jumped by up to 80 px at touch-down. The press now
// only becomes the new base; the moves pan by their own delta.
//
// Plain g++, no Arduino, no LovyanGFX. test/host/run.sh compiles this file with the system g++.
#include <stdio.h>
#include <string.h>

#include <fstream>
#include <string>

#include "ui/map_chrome.h"
#include "ui/press_targets.h"

using namespace om::ui;

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static bool sameRect(const Rect& a, const Rect& b) {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static MapMarker markerAt(int x, int y) {
  MapMarker m;
  m.x = (int16_t)x;
  m.y = (int16_t)y;
  m.type = 2;   // a repeater, as on Filip's glass
  m.name = "R1";
  return m;
}

// --- BUG 1, SHOWN: every drawn piece of chrome wins over a marker under it ------------------------
static void testShownTopLayerWins() {
  const Layout& L = l2();
  const MapChrome c = mapChromeFor(L, false, false);

  // The status bar is drawn last and is never a map tap, even with a marker right under it.
  {
    const MapMarker m = markerAt(4, 4);
    CHECK(mapHitAt(c, &m, 1, 4, 4).kind == MapHitKind::None);
  }

  // The chip (the hide/show key, its whole 30 x 26 target) wins over a marker under it.
  {
    const int x = c.chipTap.x + c.chipTap.w / 2;
    const int y = c.chipTap.y + c.chipTap.h / 2;
    const MapMarker m = markerAt(x, y);
    const MapHit h = mapHitAt(c, &m, 1, x, y);
    CHECK(h.kind == MapHitKind::TabsToggle);
    const PressTarget t = pressTargetForMap(c, &m, 1, x, y);
    CHECK(t.valid() && sameRect(t.rect, c.chipTap));
  }

  // Every one of the five tabs wins over a marker on its centre, and the pressed look is the tab.
  MapTab tabs[kMapTabCount];
  mapTabRects(c, tabs);
  for (int i = 0; i < kMapTabCount; ++i) {
    const int x = tabs[i].rect.x + tabs[i].rect.w / 2;
    const int y = tabs[i].rect.y + tabs[i].rect.h / 2;
    const MapMarker m = markerAt(x, y);
    const MapHit h = mapHitAt(c, &m, 1, x, y);
    CHECK(h.kind == tabs[i].kind);
    CHECK(h.kind != MapHitKind::Marker);
    const PressTarget t = pressTargetForMap(c, &m, 1, x, y);
    CHECK(t.valid() && sameRect(t.rect, tabs[i].rect));
  }
  // The "-" tab is the ticket's own case: a marker on it answers ZoomOut, not Marker.
  {
    const Rect& zo = c.zoomOut;
    const int x = zo.x + zo.w / 2;
    const int y = zo.y + zo.h / 2;
    const MapMarker m = markerAt(x, y);
    CHECK(mapHitAt(c, &m, 1, x, y).kind == MapHitKind::ZoomOut);
  }

  // The callout (drawn over the map and over the markers) is its own answer.
  {
    const MapChrome sel = mapChromeFor(L, true, false);
    CHECK(!sel.callout.empty());
    const int x = sel.callout.x + sel.callout.w / 2;
    const int y = sel.callout.y + sel.callout.h / 2;
    const MapMarker m = markerAt(x, y);
    const MapHit h = mapHitAt(sel, &m, 1, x, y);
    CHECK(h.kind == MapHitKind::Callout);
    const PressTarget t = pressTargetForMap(sel, &m, 1, x, y);
    CHECK(t.valid() && sameRect(t.rect, sel.callout));
  }

  // A marker NOT under any chrome still wins over the plain map (the old rule, kept where it is
  // right), and one 2 px left of the tab column is a marker, not a tab.
  {
    const MapMarker m = markerAt(120, 120);
    CHECK(mapHitAt(c, &m, 1, 120, 120).kind == MapHitKind::Marker);
    CHECK(mapHitAt(c, &m, 1, 120, 120).marker == 0);
    CHECK(mapHitAt(c, &m, 1, 150, 150).kind == MapHitKind::Map);   // 42 px away: the plain map
    const int x = c.names.x - 2;
    const int y = c.names.y + c.names.h / 2;
    const MapMarker near = markerAt(x, y);
    CHECK(mapHitAt(c, &near, 1, x, y).kind == MapHitKind::Marker);
  }
  // Nothing under the finger: the map pans, exactly as before.
  CHECK(mapHitAt(c, nullptr, 0, 160, 120).kind == MapHitKind::Map);
}

// --- BUG 1, HIDDEN: the visible ghost glyph wins, the rest of its zone does not -------------------
static void testHiddenGhostGlyphs() {
  const Layout& L = l2();
  const MapChrome c = mapChromeFor(L, false, true);

  const Rect in = c.zoomInShown;
  const Rect out = c.zoomOutShown;
  const Rect inGlyph = mapZoomGlyphRect(in);
  const Rect outGlyph = mapZoomGlyphRect(out);

  // The glyph box is a small box inside the 25 x 31 ghost zone, centred on the same point the
  // drawing puts the character (drawZoomGlyph uses the box's centre), and it covers the glyph's ink
  // (the "+" is 12 px wide, so 6 px each side of that centre).
  CHECK(inGlyph.w == kMapTabIconW && inGlyph.h == kMapTabIconW);
  CHECK(inGlyph.x >= in.x && inGlyph.right() <= in.right());
  CHECK(inGlyph.y >= in.y && inGlyph.bottom() <= in.bottom());
  CHECK(inGlyph.x + inGlyph.w / 2 == in.x + in.w / 2);
  CHECK(inGlyph.y + inGlyph.h / 2 == in.y + in.h / 2);
  CHECK(inGlyph.x <= in.x + in.w / 2 - 6 && inGlyph.right() >= in.x + in.w / 2 + 6);
  CHECK(inGlyph.w < in.w && inGlyph.h < in.h);

  // A marker under the visible + / - glyph does NOT win: the glyph zooms.
  {
    const int x = inGlyph.x + inGlyph.w / 2;
    const int y = inGlyph.y + inGlyph.h / 2;
    const MapMarker m = markerAt(x, y);
    CHECK(mapHitAt(c, &m, 1, x, y).kind == MapHitKind::ZoomIn);
    const PressTarget t = pressTargetForMap(c, &m, 1, x, y);
    CHECK(t.valid() && sameRect(t.rect, c.zoomInShown));

    const int ox = outGlyph.x + outGlyph.w / 2;
    const int oy = outGlyph.y + outGlyph.h / 2;
    const MapMarker om = markerAt(ox, oy);
    CHECK(mapHitAt(c, &om, 1, ox, oy).kind == MapHitKind::ZoomOut);
    const PressTarget ot = pressTargetForMap(c, &om, 1, ox, oy);
    CHECK(ot.valid() && sameRect(ot.rect, c.zoomOutShown));
  }

  // A marker elsewhere in a ghost zone DOES win (the old behaviour, kept where it is right): left of
  // the glyph box and above it, both still inside the 25 x 31 zone. With no marker those same pixels
  // still zoom, so the ghost zone is not lost.
  const int outsideX[2] = {(int)(in.x + 1), (int)(in.x + in.w / 2)};
  const int outsideY[2] = {(int)(in.y + in.h / 2), (int)(in.y + 1)};
  for (int i = 0; i < 2; ++i) {
    const int x = outsideX[i];
    const int y = outsideY[i];
    CHECK(in.contains(x, y));
    CHECK(!inGlyph.contains(x, y));
    const MapMarker m = markerAt(x, y);
    CHECK(mapHitAt(c, &m, 1, x, y).kind == MapHitKind::Marker);
    const PressTarget t = pressTargetForMap(c, &m, 1, x, y);
    CHECK(t.valid() && t.rect.w == 2 * kMapMarkerHitR && t.rect.h == 2 * kMapMarkerHitR);
    // The same point with nothing under it is still a zoom (the rest of the ghost zone).
    CHECK(mapHitAt(c, nullptr, 0, x, y).kind == MapHitKind::ZoomIn);
    // ...and the same point in the - zone, too.
    const int ox = out.x + (x - in.x);
    const int oy = out.y + (y - in.y);
    if (!mapZoomGlyphRect(out).contains(ox, oy)) {
      const MapMarker om = markerAt(ox, oy);
      CHECK(mapHitAt(c, &om, 1, ox, oy).kind == MapHitKind::Marker);
      CHECK(mapHitAt(c, nullptr, 0, ox, oy).kind == MapHitKind::ZoomOut);
    }
  }

  // The 6 px sliver (the hide/show key) still wins over a marker on it, and over the ghost glyph
  // box where the two overlap (the sliver is the visible thing).
  MapTab tabs[kMapTabCount];
  mapTabRects(c, tabs);
  for (int i = 0; i < kMapTabCount; ++i) {
    const int x = tabs[i].rect.x + 1;
    const int y = tabs[i].rect.y + 2;
    const MapMarker m = markerAt(x, y);
    CHECK(mapHitAt(c, &m, 1, x, y).kind == MapHitKind::TabsToggle);
    const PressTarget t = pressTargetForMap(c, &m, 1, x, y);
    CHECK(t.valid() && sameRect(t.rect, c.chipTap));
  }
  {
    const int x = 315;   // inside both the sliver (314..320) and the 17 px glyph box (299..316)
    const int y = c.zoomInShown.y + c.zoomInShown.h / 2;
    CHECK(c.zoomIn.x <= x && x < c.zoomIn.right());
    CHECK(mapZoomGlyphRect(c.zoomInShown).contains(x, y));
    const MapMarker m = markerAt(x, y);
    CHECK(mapHitAt(c, &m, 1, x, y).kind == MapHitKind::TabsToggle);
  }

  // The chip still wins over a marker while hidden.
  {
    const int x = c.chipTap.x + c.chipTap.w / 2;
    const int y = c.chipTap.y + c.chipTap.h / 2;
    const MapMarker m = markerAt(x, y);
    CHECK(mapHitAt(c, &m, 1, x, y).kind == MapHitKind::TabsToggle);
  }

  // The callout, when shown hidden, wins over a marker under it.
  {
    const MapChrome sel = mapChromeFor(L, true, true);
    CHECK(!sel.callout.empty());
    const int x = sel.callout.x + sel.callout.w / 2;
    const int y = sel.callout.y + sel.callout.h / 2;
    const MapMarker m = markerAt(x, y);
    CHECK(mapHitAt(sel, &m, 1, x, y).kind == MapHitKind::Callout);
    const PressTarget t = pressTargetForMap(sel, &m, 1, x, y);
    CHECK(t.valid() && sameRect(t.rect, sel.callout));
  }

  // The 3 px tucked strip takes no taps (the map's), so a marker there is a marker.
  {
    const MapMarker m = markerAt(100, 238);
    CHECK(c.barStrip.contains(100, 238));
    CHECK(mapHitAt(c, &m, 1, 100, 238).kind == MapHitKind::Marker);
    CHECK(mapHitAt(c, nullptr, 0, 100, 238).kind == MapHitKind::Map);
  }

  // A marker on the plain map while hidden still wins, and nothing under the finger pans.
  {
    const MapMarker m = markerAt(120, 120);
    CHECK(mapHitAt(c, &m, 1, 120, 120).kind == MapHitKind::Marker);
    CHECK(mapHitAt(c, nullptr, 0, 160, 120).kind == MapHitKind::Map);
  }
}

// --- BUG 2: the continuation decision and its base ------------------------------------------------
static void testPanContinuation() {
  // The release was at (100,100); 120 ms later the finger lands 60 px right and 10 px down. That is
  // the same pan gesture (no tap) and the press point becomes the base. The second flag is
  // NOT-spent (the shell passes !g_spent), the third says a previous release exists.
  const MapPressPan p = mapPressPan(true, true, true, 1000, 100, 100, 1120, 160, 110);
  CHECK(p.cont);
  CHECK(p.baseX == 160 && p.baseY == 110);

  // ZERO pan at the press: the old code would have panned by the previous swipe's end point
  // (100 - 160 = -60 px here). The new base is the finger's own point, so its own delta is 0.
  CHECK(p.baseX - 160 == 0 && p.baseY - 110 == 0);
  CHECK(p.baseX - 160 != 100 - 160);

  // The NEXT move pans by its own delta from the press point: to (180,130) it is 20 px each way,
  // not the 80 px the old base would have given.
  const int moveX = 180, moveY = 130;
  const int panX = p.baseX - moveX;
  const int panY = p.baseY - moveY;
  CHECK(panX == -20 && panY == -20);
  CHECK(panX != 100 - moveX && panY != 100 - moveY);

  // The window's edges: just inside continues, at/over the limit is a new gesture.
  CHECK(mapPressPan(true, true, true, 0, 100, 100, 399, 160, 100).cont);
  CHECK(!mapPressPan(true, true, true, 0, 100, 100, 400, 160, 100).cont);
  CHECK(mapPressPan(true, true, true, 0, 100, 100, 10, 179, 100).cont);
  CHECK(!mapPressPan(true, true, true, 0, 100, 100, 10, 180, 100).cont);
  // The same distance to the LEFT is the same continuation (the distance is absolute).
  CHECK(mapPressPan(true, true, true, 0, 100, 100, 10, 40, 100).cont);
  CHECK(mapPressPan(true, true, true, 0, 100, 100, 10, 21, 100).cont);   // 79 px
  CHECK(!mapPressPan(true, true, true, 0, 100, 100, 10, 20, 100).cont);  // 80 px
  // Not on the Map, a spent gesture or no previous release: never a continuation. The base is still
  // the press point in every one of them.
  const MapPressPan off = mapPressPan(false, true, true, 0, 100, 100, 10, 160, 110);
  CHECK(!off.cont && off.baseX == 160 && off.baseY == 110);
  CHECK(!mapPressPan(true, false, true, 0, 100, 100, 10, 160, 110).cont);
  CHECK(!mapPressPan(true, true, false, 0, 100, 100, 10, 160, 110).cont);
  // A millis wrap still measures the gap (unsigned subtraction): up at 0xFFFFFF00, press at 100 is
  // a 356 ms gap, still a continuation.
  CHECK(mapPressPan(true, true, true, 0xFFFFFF00u, 100, 100, 0x00000064u, 160, 110).cont);

  // The shell has ONE pan call left, in the MOVE path. The press path must not pan (it used to be
  // the second call, with the previous swipe's base). This is the source arm against the old line.
  std::ifstream in("../../src/main.cpp");
  const std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(src.size() > 100000);
  const std::string call = "om::mapPan(g_lastX - (int)x";
  size_t calls = 0;
  for (size_t pos = src.find(call, 0); pos != std::string::npos; pos = src.find(call, pos + 1)) {
    ++calls;
  }
  CHECK(calls == 1);
  CHECK(src.find("mapPressPan(") != std::string::npos);
}

int main() {
  testShownTopLayerWins();
  testHiddenGhostGlyphs();
  testPanContinuation();
  if (g_fail) {
    printf("est416: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("est416: draw-order hit test in both bar states (a marker under a shown tab, the chip, the "
         "callout, a sliver or a ghost +/- glyph loses; elsewhere in a ghost zone it wins), "
         "pressTargetForMap follows, and a fast re-touch pans by nothing at the press, ok\n");
  return 0;
}
