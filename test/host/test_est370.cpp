// EST-370 host test: the Map's Names tab (its rectangle, tap and the marker-wins rule), the centre
// cross's position (the same point the tile renderer projects the view centre onto, and the picker's
// crosshair uses), and the saved names/traces switches (defaults, toggle, persistence, a failed
// write and a missing key). EST-372 adds the edge-tab geometry, the plain z13 line, the centre
// cross's box and the trace colour's measured contrast; EST-373 narrows the tab to 25 px and the
// cross to 7x7 (test_est373.cpp owns the hidden column and the new ink). Plain g++, no Arduino.
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

// --- the Names tab -------------------------------------------------------------------------------
static void testNamesKey() {
  const Layout& L = l2();
  const MapChrome c = mapChromeFor(L, false);
  CHECK(c.names.right() == c.zoomIn.right());
  CHECK(c.names.x == c.zoomOut.x);
  // EST-373/374: 25 px wide (the widest icon, 17, plus 4 px each side), 31 px tall: EST-374's chip
  // puts a 26 px tap target above the top tab, which costs the column 1 px of its band.
  CHECK(c.names.w == kMapTabW && c.names.w == 25);
  CHECK(c.names.h == 31);
  CHECK(c.names.y == c.traces.y - c.traces.h - kMapTabGap);   // the top tab of the column
  CHECK(c.names.y >= c.chipTap.bottom());                     // below the chip's target, never on it
  CHECK(!c.names.overlaps(c.traces));
  CHECK(!c.names.overlaps(c.lock));
  CHECK(!c.names.overlaps(c.zoomIn));
  CHECK(!c.names.overlaps(c.zoomOut));
  CHECK(!c.names.overlaps(c.zoomLabel));

  // The 320x240 geometry quoted in PROGRESS.md: right edge 320, x 295, y 56..87.
  CHECK(c.names.x == 295);
  CHECK(c.names.right() == 320);
  CHECK(c.names.y == 56);
  CHECK(c.names.bottom() == 87);

  // The one hit test: the centre of the tab is the Names tab, every pixel of it is.
  const int cx = c.names.x + c.names.w / 2;
  const int cy = c.names.y + c.names.h / 2;
  CHECK(mapHitAt(c, nullptr, 0, cx, cy).kind == MapHitKind::Names);
  CHECK(mapHitAt(c, nullptr, 0, c.names.x, c.names.y).kind == MapHitKind::Names);
  CHECK(mapHitAt(c, nullptr, 0, c.names.right() - 1, c.names.bottom() - 1).kind ==
        MapHitKind::Names);
  CHECK(mapHitAt(c, nullptr, 0, c.names.x - 1, cy).kind != MapHitKind::Names);
  CHECK(mapHitAt(c, nullptr, 0, c.names.right(), cy).kind != MapHitKind::Names);

  // A marker still wins over the tab it sits on (the map's one rule).
  MapMarker m;
  m.x = (int16_t)cx;
  m.y = (int16_t)cy;
  m.type = 2;
  m.name = "R1";
  const MapHit hit = mapHitAt(c, &m, 1, cx, cy);
  CHECK(hit.kind == MapHitKind::Marker);
  CHECK(hit.marker == 0);

  // The credit box never runs under the tab column.
  const Rect cr = mapCreditRect(c, 138);
  CHECK(!cr.overlaps(c.names));
}

// --- the centre cross ----------------------------------------------------------------------------
static void testCentre() {
  const MapChrome c = mapChromeFor(l2(), false);
  const MapPoint p = mapCentreOf(c);
  // The tile canvas is 320x214 and, since EST-374, it starts on the status bar's bottom row, so the
  // view's geographic centre is at (160, 133) on the screen. The chrome derives the same point from
  // its view rectangle, which now IS the canvas.
  CHECK(kMapCanvasW == 320);
  CHECK(kMapCanvasH == 214);
  CHECK(kMapCanvasTop == kStatusH);
  CHECK(kMapCentreX == 160);
  CHECK(kMapCentreY == 133);
  CHECK(p.x == kMapCentreX);
  CHECK(p.y == kMapCentreY);
  CHECK(p.x == c.view.x + c.view.w / 2);
  CHECK(p.y == c.view.y + c.view.h / 2);
  // EST-374: the same centre in BOTH states: hiding the bars never moves the cross.
  const MapChrome hid = mapChromeFor(l2(), false, true);
  CHECK(mapCentreOf(hid).x == p.x && mapCentreOf(hid).y == p.y);
  CHECK(mapCentreCrossRect(hid).x == mapCentreCrossRect(c).x);
  CHECK(mapCentreCrossRect(hid).y == mapCentreCrossRect(c).y);

  // EST-373: two 1 px lines, 7 px long (arm 3), so the box is 7x7 and centred on the same point.
  const Rect d = mapCentreCrossRect(c);
  CHECK(d.w == 7 && d.h == 7);
  CHECK(kMapCentreCrossArm == 3);
  CHECK(d.w == 2 * kMapCentreCrossArm + 1);
  CHECK(d.x + d.w / 2 == p.x);
  CHECK(d.y + d.h / 2 == p.y);
  CHECK(c.view.contains(d.x, d.y));
  CHECK(c.view.contains(d.right() - 1, d.bottom() - 1));
  CHECK(!d.overlaps(c.status));
  CHECK(!d.overlaps(c.zoomLabel));
  CHECK(!d.overlaps(c.zoomIn) && !d.overlaps(c.zoomOut) && !d.overlaps(c.lock));
  CHECK(!d.overlaps(c.traces) && !d.overlaps(c.names));
  // It stays a small mark, far from the tab column and the bars.
  CHECK(d.right() < c.zoomIn.x);

  // The point does not move when the callout opens (it is the view's, not the selection's).
  const MapChrome sel = mapChromeFor(l2(), true);
  const MapPoint ps = mapCentreOf(sel);
  CHECK(ps.x == p.x && ps.y == p.y);
  CHECK(mapCentreCrossRect(sel).x == d.x && mapCentreCrossRect(sel).y == d.y);

  // EST-393: the Position picker now draws THIS cross from mapCentreCrossAt(), on the same pixel
  // and the same 7x7 box (test_est393 owns the picker's rectangle and the shared drawing).
  CHECK(mapCentreCrossAt(p.x, p.y).x == d.x && mapCentreCrossAt(p.x, p.y).y == d.y);
  CHECK(mapCentreCrossAt(p.x, p.y).w == d.w && mapCentreCrossAt(p.x, p.y).h == d.h);
}

// --- the trace colours and widths ----------------------------------------------------------------
// The ticket asks for a bolder colour, measured against the tile colours, and for the widths to
// live in theme.h. These are the same numbers the python measurement in PROGRESS.md reports,
// repeated here so a later palette change cannot silently undo EST-372.
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

static void testTraceColour() {
  CHECK(kTraceLine == rgb565(0xc026d3));
  CHECK(kTraceTail == kTraceLine);
  CHECK(kTraceDot == kTraceLine);
  CHECK(kTraceRing == kTraceLine);
  CHECK(kTraceRouteW == 2);
  CHECK(kTraceTailW == 2);
  CHECK(kTraceDotR == 2);              // 5 px fill
  CHECK(kTraceDotOutlineW == 1);
  CHECK(kTraceRingW == 2);
  // The measured tile colours from the real z13 image: the light ground and a dark tile.
  CHECK(contrast(kTraceLine, rgb565(0xf0f0e0)) > 3.5);
  CHECK(contrast(kTraceLine, rgb565(0xe0e0e0)) > 3.0);
  CHECK(contrast(kTraceLine, rgb565(0x181818)) > 3.5);
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
  testTraceColour();
  testSavedSwitches();
  if (g_fail) {
    printf("est370: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("est370: edge tabs + centre cross + trace colour + saved switches ok\n");
  return 0;
}
