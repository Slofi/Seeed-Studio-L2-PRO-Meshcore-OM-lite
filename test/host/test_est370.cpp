// EST-370 host test: the Map's Names key (its rectangle, tap and the marker-wins rule), the centre
// dot's position (the same point the tile renderer projects the view centre onto, and the picker's
// crosshair uses), and the saved names/traces switches (defaults, toggle, persistence, a failed
// write and a missing key). Plain g++, no Arduino.
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

// --- the Names key -------------------------------------------------------------------------------
static void testNamesKey() {
  const Layout& L = l2();
  const MapChrome c = mapChromeFor(L, false);
  CHECK(c.names.right() == c.zoomIn.right());
  CHECK(c.names.x == c.zoomOut.x);
  CHECK(c.names.w == kMapKey && c.names.h == kMapKey);
  CHECK(c.names.y == c.traces.y - kMapKey - kMapKeyGap);   // the top key of the column
  CHECK(c.names.y >= c.chip.bottom());                    // never under the info chip
  CHECK(!c.names.overlaps(c.traces));
  CHECK(!c.names.overlaps(c.lock));
  CHECK(!c.names.overlaps(c.zoomIn));
  CHECK(!c.names.overlaps(c.zoomOut));
  CHECK(!c.names.overlaps(c.chip));

  // The 320x240 geometry quoted in PROGRESS.md: right edge 314, x 290, y 73..97.
  CHECK(c.names.x == 290);
  CHECK(c.names.right() == 314);
  CHECK(c.names.y == 73);
  CHECK(c.names.bottom() == 97);

  // The one hit test: the centre of the key is the Names key, every pixel of it is.
  const int cx = c.names.x + c.names.w / 2;
  const int cy = c.names.y + c.names.h / 2;
  CHECK(mapHitAt(c, nullptr, 0, cx, cy).kind == MapHitKind::Names);
  CHECK(mapHitAt(c, nullptr, 0, c.names.x, c.names.y).kind == MapHitKind::Names);
  CHECK(mapHitAt(c, nullptr, 0, c.names.right() - 1, c.names.bottom() - 1).kind ==
        MapHitKind::Names);
  CHECK(mapHitAt(c, nullptr, 0, c.names.x - 1, cy).kind != MapHitKind::Names);
  CHECK(mapHitAt(c, nullptr, 0, c.names.right(), cy).kind != MapHitKind::Names);

  // A marker still wins over the key it sits on (the map's one rule).
  MapMarker m;
  m.x = (int16_t)cx;
  m.y = (int16_t)cy;
  m.type = 2;
  m.name = "R1";
  const MapHit hit = mapHitAt(c, &m, 1, cx, cy);
  CHECK(hit.kind == MapHitKind::Marker);
  CHECK(hit.marker == 0);

  // The credit box never runs under the top key either (its clamp uses the bottom key's column).
  const Rect cr = mapCreditRect(c, 138);
  CHECK(!cr.overlaps(c.names));
}

// --- the centre dot ------------------------------------------------------------------------------
static void testCentre() {
  const MapChrome c = mapChromeFor(l2(), false);
  const MapPoint p = mapCentreOf(c);
  // The tile canvas is 320x214 and map.cpp puts the view's geographic centre at (160,107). The
  // chrome derives the same point from its view rectangle even though the view runs to the tab bar.
  CHECK(kMapCanvasW == 320);
  CHECK(kMapCanvasH == 214);
  CHECK(kMapCentreX == 160);
  CHECK(kMapCentreY == 107);
  CHECK(p.x == kMapCentreX);
  CHECK(p.y == kMapCentreY);
  CHECK(p.x == c.view.x + c.view.w / 2);
  CHECK(p.y == c.view.y + c.view.h / 2);

  // The dot is 2-3 px across: a 3x3 core (the screen's 1 px ring is decoration, not the dot).
  const Rect d = mapCentreDotRect(c);
  CHECK(d.w == 3 && d.h == 3);
  CHECK(d.x + d.w / 2 == p.x);
  CHECK(d.y + d.h / 2 == p.y);
  CHECK(d.w >= 2 && d.w <= 3);
  CHECK(c.view.contains(d.x, d.y));
  CHECK(c.view.contains(d.right() - 1, d.bottom() - 1));
  CHECK(!d.overlaps(c.status));
  CHECK(!d.overlaps(c.chip));
  CHECK(!d.overlaps(c.zoomIn) && !d.overlaps(c.zoomOut) && !d.overlaps(c.lock));
  CHECK(!d.overlaps(c.traces) && !d.overlaps(c.names));
  // It is not a crosshair: its box is small and does not reach the key column or the bars.
  CHECK(d.right() < c.zoomIn.x);

  // The point does not move when the callout opens (it is the view's, not the selection's).
  const MapChrome sel = mapChromeFor(l2(), true);
  const MapPoint ps = mapCentreOf(sel);
  CHECK(ps.x == p.x && ps.y == p.y);
  CHECK(mapCentreDotRect(sel).x == d.x && mapCentreDotRect(sel).y == d.y);

  // The Position picker's crosshair (rptr_layout.cpp: 154,101 12x12) is centred on the same pixel,
  // which is why the picker does not draw this dot: it already has a crosshair there.
  CHECK(154 + 12 / 2 == p.x);
  CHECK(101 + 12 / 2 == p.y);
}

// --- the saved switches --------------------------------------------------------------------------
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

static void testSavedSwitches() {
  // The keys and defaults are the ticket's: names ON, traces ON (EST-368 kept).
  CHECK(strcmp(kMapPrefNames, "names") == 0);
  CHECK(strcmp(kMapPrefTraces, "traces") == 0);
  CHECK(kMapNamesDefault);
  CHECK(kMapTracesDefault);

  FakeStore store;
  MapLayerPrefs p;
  p.begin(fakeRead, fakeWrite, &store);

  // A first boot: both on, nothing written (a missing key never seeds NVS).
  p.load();
  CHECK(p.names());
  CHECK(p.traces());
  CHECK(store.writes == 0);
  CHECK(store.used == 0);

  // Tapping Names hides the labels, saves "names" = 0 and writes only that one key.
  CHECK(p.toggleNames());
  CHECK(!p.names());
  CHECK(p.traces());                 // the other switch is untouched
  CHECK(store.writes == 1);
  CHECK(store.used == 1);
  CHECK(strcmp(store.keys[0], "names") == 0);
  CHECK(store.vals[0] == false);

  // A reboot: a fresh store object on the same NVS reads the saved value back.
  {
    MapLayerPrefs r;
    r.begin(fakeRead, fakeWrite, &store);
    r.load();
    CHECK(!r.names());
    CHECK(r.traces());
  }

  // Tabbing Traces saves its own key independently.
  CHECK(p.toggleTraces());
  CHECK(!p.traces());
  CHECK(store.used == 2);
  CHECK(store.writes == 2);

  // Back on: the value round-trips through another fresh load.
  CHECK(p.toggleNames());
  CHECK(p.names());
  CHECK(store.vals[0] == true);
  {
    MapLayerPrefs r;
    r.begin(fakeRead, fakeWrite, &store);
    r.load();
    CHECK(r.names());
  }

  // A value set without a flip also persists.
  CHECK(p.setTraces(true));
  CHECK(p.traces());
  {
    MapLayerPrefs r;
    r.begin(fakeRead, fakeWrite, &store);
    r.load();
    CHECK(r.traces());
  }

  // A failed write still flips the key on screen (the finger must see the change) and says false.
  FakeStore bad;
  bad.failWrite = true;
  MapLayerPrefs q;
  q.begin(fakeRead, fakeWrite, &bad);
  q.load();
  CHECK(!q.toggleNames());
  CHECK(!q.names());
  CHECK(bad.writes == 1);

  // No reader: the defaults, no crash. No writer: the value still flips, the return is false.
  MapLayerPrefs bare;
  bare.load();
  CHECK(bare.names() && bare.traces());
  CHECK(!bare.toggleNames());
  CHECK(!bare.names());

  // A reader that says "not stored" for one key and stores the other: each key is independent.
  FakeStore half;
  snprintf(half.keys[0], sizeof(half.keys[0]), "traces");
  half.vals[0] = false;
  half.used = 1;
  MapLayerPrefs h;
  h.begin(fakeRead, fakeWrite, &half);
  h.load();
  CHECK(!h.traces());                // read from the store
  CHECK(h.names());                  // missing: the default
}

int main() {
  testNamesKey();
  testCentre();
  testSavedSwitches();
  if (g_fail) {
    printf("est370: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("est370: names key + centre dot + saved switches ok\n");
  return 0;
}
