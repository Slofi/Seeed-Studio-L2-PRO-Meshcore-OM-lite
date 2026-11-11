// EST-373 host test: the three map tweaks Filip asked for on 2026-11-11 ~00:38.
//   1. the centre cross is two thin lines in one dark ink, 7 px, no outline;
//   2. the edge tabs are 25 px (their widest icon plus 4 px each side) and every tab's tap rectangle
//      IS the rectangle the screen draws: one list (mapTabRects) feeds both, and the test proves it
//      at all four corners of every tab, with x + w == 320;
//   3. the hide/show key saves its state with the layer switches (EST-374 moved the key into a chip
//      and the hidden-state checks into test_est374.cpp).
// Plain g++, no Arduino.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui/map_chrome.h"
#include "ui/map_layers.h"

using namespace om::ui;

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// --- 1. the narrower tabs -------------------------------------------------------------------------
static void testTabWidth() {
  // The number is not a guess: the widest thing on the column (the Follow lock: a 5 px radius circle
  // with an arm reaching 8 px from the centre) plus the ticket's 4 px on each side.
  CHECK(kMapTabIconPad == 4);
  CHECK(kMapLockW == 2 * kMapLockArmOut + 1);
  CHECK(kMapLockW == 17);
  CHECK(kMapTabIconW == kMapLockW);
  CHECK(kMapTabW == kMapTabIconW + 2 * kMapTabIconPad);
  CHECK(kMapTabW == 25);

  // 320 - 25: the column starts at 295 and the map gains the 19 px the 44 px column took.
  const MapChrome c = mapChromeFor(l2(), false);
  CHECK(c.names.w == 25 && c.names.x == 295 && c.names.right() == 320);
  CHECK(c.zoomOut.x == 295 && c.zoomIn.x == 295 && c.lock.x == 295 && c.traces.x == 295);
  CHECK(320 - kMapTabW == 295);

  // Heights are the layout's: the same 32 px for all five, computed from the band, not baked in.
  CHECK(c.names.h == c.traces.h && c.traces.h == c.lock.h && c.lock.h == c.zoomIn.h &&
        c.zoomIn.h == c.zoomOut.h);
  CHECK(c.names.h == 31);   // EST-374: the chip target takes 1 px of the band
  CHECK(c.zoomOut.bottom() == l2().tabbar.y);
}

// --- 2. the drawn rectangle IS the tap rectangle --------------------------------------------------
static void testRectsAreTaps() {
  for (int hidden = 0; hidden < 2; ++hidden) {
    const MapChrome c = mapChromeFor(l2(), false, hidden != 0);
    MapTab tabs[kMapTabCount];
    mapTabRects(c, tabs);

    // The list is the column, bottom up, and its rectangles ARE the chrome's named ones: the screen
    // draws the list, the hit test reads the list, so neither can drift.
    CHECK(tabs[0].kind == MapHitKind::ZoomOut && tabs[0].rect.x == c.zoomOut.x &&
          tabs[0].rect.y == c.zoomOut.y);
    CHECK(tabs[1].kind == MapHitKind::ZoomIn && tabs[1].rect.y == c.zoomIn.y);
    CHECK(tabs[2].kind == MapHitKind::Lock && tabs[2].rect.y == c.lock.y);
    CHECK(tabs[3].kind == MapHitKind::Traces && tabs[3].rect.y == c.traces.y);
    CHECK(tabs[4].kind == MapHitKind::Names && tabs[4].rect.y == c.names.y);
    CHECK(tabs[0].rect.w == c.zoomOut.w && tabs[4].rect.h == c.names.h);

    for (int i = 0; i < kMapTabCount; ++i) {
      const Rect r = tabs[i].rect;
      // Shown, the drawn rectangle answers its own switch; hidden, the 4 px sliver it still draws
      // answers the hide/show key (nothing invisible fires).
      const MapHitKind want = hidden ? MapHitKind::TabsToggle : tabs[i].kind;
      // Flush with the screen edge: x + w == 320, always.
      CHECK(r.right() == 320);
      CHECK(r.x + r.w == 320);
      CHECK(r.w == (hidden ? kMapTabSliverW : kMapTabW));
      CHECK(!r.empty());
      // Every pixel of the drawn rectangle answers that tab: the four corners and the centre.
      const int mx = r.x + r.w / 2;
      const int my = r.y + r.h / 2;
      CHECK(mapHitAt(c, nullptr, 0, r.x, r.y).kind == want);
      CHECK(mapHitAt(c, nullptr, 0, r.right() - 1, r.y).kind == want);
      CHECK(mapHitAt(c, nullptr, 0, r.x, r.bottom() - 1).kind == want);
      CHECK(mapHitAt(c, nullptr, 0, r.right() - 1, r.bottom() - 1).kind == want);
      CHECK(mapHitAt(c, nullptr, 0, mx, my).kind == want);
      // One pixel to the LEFT of the drawn rectangle is not that tab: the tap zone did not stay wide
      // while the drawing got narrow.
      CHECK(mapHitAt(c, nullptr, 0, r.x - 1, my).kind != want);
      // No tab overlaps another, and no tab reaches into the status bar.
      CHECK(!r.overlaps(c.status));
      for (int k = 0; k < kMapTabCount; ++k) {
        if (k == i) continue;
        CHECK(!r.overlaps(tabs[k].rect));
      }
    }
  }

  // The old 44 px tap zone is gone: at the old left edge (276) the map answers, not a tab.
  const MapChrome c = mapChromeFor(l2(), false);
  CHECK(mapHitAt(c, nullptr, 0, 276, c.names.y + 2).kind == MapHitKind::Map);
  CHECK(mapHitAt(c, nullptr, 0, 294, c.zoomIn.y + 2).kind == MapHitKind::Map);
  CHECK(mapHitAt(c, nullptr, 0, 295, c.zoomIn.y + 2).kind == MapHitKind::ZoomIn);
}

// --- 3. the hide/show key and the slivers ----------------------------------------------------------
// EST-374 moved the hide/show key from the label line into a 22 px chip and narrowed the slivers to
// 3 px, and it gives the hidden state its own tap semantics (the ghost + / -). Those checks live in
// test_est374.cpp; this file keeps EST-373's own truths (a 25 px tab whose drawn rect IS its tap
// rect, and the one dark cross).

// --- 4. the centre cross in one dark ink ----------------------------------------------------------
static double chan5(uint16_t v) { return ((v >> 11) & 0x1F) * 255.0 / 31.0; }
static double chan6(uint16_t v) { return ((v >> 5) & 0x3F) * 255.0 / 63.0; }
static double chan5b(uint16_t v) { return (v & 0x1F) * 255.0 / 31.0; }

static double relativeLuminance(uint16_t c) {
  const double ch[3] = {chan5(c) / 255.0, chan6(c) / 255.0, chan5b(c) / 255.0};
  double lin[3];
  for (int i = 0; i < 3; ++i) {
    lin[i] = ch[i] <= 0.04045 ? ch[i] / 12.92 : pow((ch[i] + 0.055) / 1.055, 2.4);
  }
  return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2];
}

static double contrast(uint16_t a, uint16_t b) {
  const double la = relativeLuminance(a);
  const double lb = relativeLuminance(b);
  return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

static void testCross() {
  // Two 1 px lines, arm 3, so the box is 7x7, centred on the map's own centre (EST-374: 160,133).
  CHECK(kMapCentreCrossArm == 3);
  const MapChrome c = mapChromeFor(l2(), false);
  const Rect d = mapCentreCrossRect(c);
  CHECK(d.w == 7 && d.h == 7);
  CHECK(d.x == 157 && d.y == 130);   // EST-374: the canvas starts at y 26, so the centre is 133
  CHECK(d.x + d.w / 2 == 160 && d.y + d.h / 2 == 133);

  // One dark ink, not the white kInk the outline used to carry.
  CHECK(kMapCrossInk == rgb565(0x202020));
  CHECK(kMapCrossInk != kInk);
  // The floors the ticket cares about: readable on the light topo tiles, with no plate behind it.
  CHECK(contrast(kMapCrossInk, rgb565(0xf0f0e0)) > 12.0);
  CHECK(contrast(kMapCrossInk, rgb565(0xe0e0e0)) > 10.0);
  CHECK(contrast(kMapCrossInk, rgb565(0xd0d0d0)) > 8.0);
  CHECK(contrast(kMapCrossInk, rgb565(0x6c6c6a)) > 2.5);
  // The honest limit of one ink: on a dark tile it would vanish, which is why this pack (no dark
  // tiles) is what the choice rests on.
  CHECK(contrast(kMapCrossInk, kMapBg) < 1.5);
  // The old white line with no outline was the same nothing on a light tile.
  CHECK(contrast(kInk, rgb565(0xf0f0e0)) < 1.2);
}

// --- 5. the saved hidden column -------------------------------------------------------------------
struct FakeStore {
  char keys[8][16] = {};
  bool vals[8] = {};
  int used = 0;
  int writes = 0;
  bool failWrite = false;
};

static bool fakeRead(void* ctx, const char* key, bool fallback, bool& out) {
  FakeStore* s = static_cast<FakeStore*>(ctx);
  for (int i = 0; i < s->used; ++i) {
    if (strcmp(s->keys[i], key) == 0) {
      out = s->vals[i];
      return true;
    }
  }
  (void)fallback;
  return false;   // a missing key: the caller keeps its default
}

static bool fakeWrite(void* ctx, const char* key, bool value) {
  FakeStore* s = static_cast<FakeStore*>(ctx);
  ++s->writes;
  if (s->failWrite) return false;
  for (int i = 0; i < s->used; ++i) {
    if (strcmp(s->keys[i], key) == 0) {
      s->vals[i] = value;
      return true;
    }
  }
  if (s->used >= 8) return false;
  snprintf(s->keys[s->used], sizeof(s->keys[0]), "%s", key);
  s->vals[s->used] = value;
  ++s->used;
  return true;
}

static void testSavedHidden() {
  // Its own NVS key (NVS caps a key at 15 characters), default SHOWN so a first boot is unchanged.
  CHECK(strcmp(kMapPrefTabsHidden, "tabsHidden") == 0);
  CHECK(strlen(kMapPrefTabsHidden) <= 15);
  CHECK(!kMapTabsHiddenDefault);
  CHECK(strcmp(kMapPrefTabsHidden, kMapPrefNames) != 0);
  CHECK(strcmp(kMapPrefTabsHidden, kMapPrefTraces) != 0);

  FakeStore store;
  MapLayerPrefs p;
  p.begin(fakeRead, fakeWrite, &store);
  p.load();
  CHECK(!p.tabsHidden());
  CHECK(p.names() && p.traces());   // the other two keep their defaults
  CHECK(store.writes == 0 && store.used == 0);

  // The arrow's tap: hidden, one key written, the other two untouched.
  CHECK(p.toggleTabsHidden());
  CHECK(p.tabsHidden());
  CHECK(store.used == 1 && store.writes == 1);
  CHECK(strcmp(store.keys[0], "tabsHidden") == 0);
  CHECK(store.vals[0] == true);
  CHECK(p.names() && p.traces());

  // Reopening the map (a fresh store object on the same NVS) keeps it hidden.
  {
    MapLayerPrefs r;
    r.begin(fakeRead, fakeWrite, &store);
    r.load();
    CHECK(r.tabsHidden());
    CHECK(r.toggleTabsHidden());   // the write succeeds (the return is the write's, not the value)
    CHECK(!r.tabsHidden());
  }
  CHECK(store.vals[0] == false);

  // A value set without a flip (a USB command later) persists too.
  CHECK(p.setTabsHidden(true));
  CHECK(p.tabsHidden());
  CHECK(store.vals[0] == true);

  // A failed write still flips the key on screen (the finger must see the change) and says false.
  FakeStore bad;
  bad.failWrite = true;
  MapLayerPrefs q;
  q.begin(fakeRead, fakeWrite, &bad);
  q.load();
  CHECK(!q.toggleTabsHidden());
  CHECK(q.tabsHidden());

  // No reader, no crash: the default (shown). No writer: the value still flips, the return is false.
  MapLayerPrefs bare;
  bare.load();
  CHECK(!bare.tabsHidden());
  CHECK(!bare.toggleTabsHidden());
  CHECK(bare.tabsHidden());
}

int main() {
  testTabWidth();
  testRectsAreTaps();
  testCross();
  testSavedHidden();
  if (g_fail) {
    printf("est373: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("est373: 25 px tabs, one drawn/tap list, 7 px dark cross, saved state ok\n");
  return 0;
}
