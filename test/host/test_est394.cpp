// SPDX-License-Identifier: MIT
// EST-394 host test: a trace belongs to its repeaters, not to the screen.
//
//   A. A route sampled in view A and then in view B (the content panned by +40/-25 px) lands EXACTLY
//      where mapProject() puts the same lat/lon in view B: every route point, the dot, the tail and
//      the direct-arrival ring move by the pan, never stay behind.
//   B. A zoom change re-projects the same route at the new view: every point equals mapProject() in
//      view B, the whole polyline scales about the map centre, and the dot sits at the same fraction
//      of the (new) route.
//   C. sampleActive() re-projects a RUNNING route without restarting its clock (the Map tab calls it
//      from build(), so every draw path gets the current view).
//   D. with the map pack not open the route stays live and draws nothing; it is never thrown away.
//   E. the added cost: a 12-point sample measured on this host.
//   F. the picker's zoom label under the back key, clear of "Cancel" at both screen sizes.
//
// Plain g++, no Arduino and no LovyanGFX. The projector copies map.cpp's mapProject() arithmetic
// (same lonToXf/latToYf, same 320x214 canvas under the 26 px status bar) so "where mapProject puts
// it" is the same expression here.
#include <math.h>
#include <stdio.h>

#include <chrono>

#include "ui/map_chrome.h"
#include "ui/map_trace.h"
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

// --------------------------------------------------------------------- the view (map.cpp's maths)
struct View {
  double clat = 46.05;
  double clon = 14.55;
  int z = 15;
  double cpx = 0.0;   // the centre in tile pixels, as map.cpp keeps it
  double cpy = 0.0;
  bool ready = true;
};

static constexpr double kTile = 256.0;
static constexpr int kViewW = 320;
static constexpr int kViewH = 214;
static constexpr int kViewTop = 26;
static constexpr double kPi = 3.14159265358979323846;

static double lonToXf(double lon, int z) { return (lon + 180.0) / 360.0 * (double)(1UL << z); }
static double latToYf(double lat, int z) {
  const double r = lat * kPi / 180.0;
  const double n = log(tan(r) + 1.0 / cos(r));
  return (1.0 - n / kPi) / 2.0 * (double)(1UL << z);
}

static int iabs(int v) { return v < 0 ? -v : v; }

static void viewSet(View& v, double lat, double lon, int z) {
  v.clat = lat;
  v.clon = lon;
  v.z = z;
  const int rz = (z == 18) ? 17 : z;
  v.cpx = lonToXf(lon, rz) * kTile;
  v.cpy = latToYf(lat, rz) * kTile;
}

// mapPan(dx, dy): the view's centre moves by (dx, dy) tile pixels.
static void viewPan(View& v, int dx, int dy) {
  v.cpx += dx;
  v.cpy += dy;
}

static View g_view;

static bool projectView(double lat, double lon, int& x, int& y) {
  if (!g_view.ready) return false;   // mapProject()'s "the pack is not open"
  const bool mag = (g_view.z == 18);
  const int rz = mag ? 17 : g_view.z;
  const double scale = mag ? 2.0 : 1.0;
  x = (int)lround((lonToXf(lon, rz) * kTile - g_view.cpx) * scale + kViewW / 2.0);
  y = (int)lround((latToYf(lat, rz) * kTile - g_view.cpy) * scale + kViewH / 2.0) + kViewTop;
  return true;
}

// The ticket's question: where does mapProject() put this point in THIS view?
static void where(const View& v, const TraceGeoPt& p, int& x, int& y) {
  const View save = g_view;
  g_view = v;
  const bool ok = projectView((double)p.latE6 / 1e6, (double)p.lonE6 / 1e6, x, y);
  g_view = save;
  CHECK(ok);
}

// Invented positions only (rule 9), a few arc-minutes apart.
static const TraceGeoPt kRoute[4] = {
    {46000000, 14400000},   // sender
    {46020000, 14450000},   // repeater 1
    {46010000, 14500000},   // repeater 2
    {46040000, 14540000},   // us
};

// ------------------------------------------------- A. a pan moves every point of the trace -------
static void testPan() {
  View a;
  viewSet(a, 46.05, 14.55, 15);
  View b = a;
  viewPan(b, -40, +25);   // the content moves +40 x and -25 y, exactly the ticket's pan

  TraceAnim anim;
  anim.begin(kRoute, 4, false, 3, 1000);
  const uint32_t at = 1000 + 500;   // mid-route, so a dot and a tail exist

  TraceFrame fa;
  g_view = a;
  CHECK(anim.sample(at, projectView, &fa));
  TraceFrame fb;
  g_view = b;
  CHECK(anim.sample(at, projectView, &fb));

  // Every route point is the same lat/lon, projected in B.
  CHECK(fa.routeCount == 4 && fb.routeCount == 4);
  bool moved = false;
  for (int i = 0; i < 4; ++i) {
    int x = 0, y = 0;
    where(b, kRoute[i], x, y);
    CHECK(fb.route[i].x == x);
    CHECK(fb.route[i].y == y);
    // And, for a pure pan, exactly the old point plus the pan.
    CHECK(fb.route[i].x == fa.route[i].x + 40);
    CHECK(fb.route[i].y == fa.route[i].y - 25);
    if (fb.route[i].x != fa.route[i].x) moved = true;
  }
  CHECK(moved);

  // The dot and every tail vertex too: nothing is left at the old pixels.
  CHECK(fa.hasDot && fb.hasDot);
  CHECK(fb.dot.x == fa.dot.x + 40);
  CHECK(fb.dot.y == fa.dot.y - 25);
  CHECK(fb.dotAlpha == fa.dotAlpha);
  CHECK(fa.tailCount >= 2 && fb.tailCount == fa.tailCount);
  for (int i = 0; i < fa.tailCount; ++i) {
    CHECK(fb.tail[i].x == fa.tail[i].x + 40);
    CHECK(fb.tail[i].y == fa.tail[i].y - 25);
  }
}

// ------------------------------------------- B. a zoom change re-projects the whole animation -----
static void testZoom() {
  View a;
  viewSet(a, 46.05, 14.55, 15);
  View z = a;
  viewSet(z, a.clat, a.clon, 16);   // the same geographic centre, one level in

  TraceAnim anim;
  anim.begin(kRoute, 4, false, 3, 0);
  const uint32_t at = 600;          // mid-route

  TraceFrame fa;
  g_view = a;
  CHECK(anim.sample(at, projectView, &fa));
  TraceFrame fz;
  g_view = z;
  CHECK(anim.sample(at, projectView, &fz));

  CHECK(fa.routeCount == 4 && fz.routeCount == 4);
  for (int i = 0; i < 4; ++i) {
    int x = 0, y = 0;
    where(z, kRoute[i], x, y);
    CHECK(fz.route[i].x == x);
    CHECK(fz.route[i].y == y);
    // A zoom about the centre scales the offset: 160 + 2 * (xA - 160), 133 + 2 * (yA - 133).
    CHECK(iabs(fz.route[i].x - (160 + 2 * (fa.route[i].x - 160))) <= 1);
    CHECK(iabs(fz.route[i].y - (133 + 2 * (fa.route[i].y - 133))) <= 1);
  }

  // The dot is recomputed along the NEW geometry at the same fraction of the route.
  TracePt zr[4] = {fz.route[0], fz.route[1], fz.route[2], fz.route[3]};
  TraceGeom g;
  traceGeom(zr, 4, &g);
  const float head = (float)at / (float)(3 * kTraceHopMs);
  TracePt want;
  CHECK(tracePointAt(g, zr, head, &want));
  CHECK(fz.hasDot);
  CHECK(fz.dot.x == want.x && fz.dot.y == want.y);
  CHECK(iabs(fz.dot.x - (160 + 2 * (fa.dot.x - 160))) <= 1);
  CHECK(iabs(fz.dot.y - (133 + 2 * (fa.dot.y - 133))) <= 1);
}

// --------------------------------- C. a running route is re-projected, its clock is not restarted --
static void testSampleActive() {
  View a;
  viewSet(a, 46.05, 14.55, 15);
  View b = a;
  viewPan(b, -40, +25);

  TraceQueue q;
  CHECK(q.push(kRoute, 4, false, 3, 1));
  TraceFrame f;
  g_view = a;
  CHECK(q.tick(1000, projectView, &f));   // starts the clock at the first point
  int x0 = 0, y0 = 0;
  where(a, kRoute[0], x0, y0);
  CHECK(f.dot.x == x0 && f.dot.y == y0);

  g_view = a;
  CHECK(q.sampleActive(1000 + 630, projectView, &f));   // half a route later
  const TracePt dotA = f.dot;
  CHECK(dotA.x != x0 || dotA.y != y0);                  // it really travelled

  g_view = b;
  CHECK(q.sampleActive(1000 + 630, projectView, &f));
  // Same moment, new view: the dot is where it was, moved by the pan, NOT back at the start.
  CHECK(f.dot.x == dotA.x + 40);
  CHECK(f.dot.y == dotA.y - 25);
  CHECK(f.dot.x != x0 + 40);

  // A direct arrival's ring is georeferenced too.
  const TraceGeoPt us[1] = {{46040000, 14540000}};
  TraceQueue r;
  CHECK(r.push(us, 1, true, 0, 2));
  TraceFrame ring;
  g_view = a;
  CHECK(r.tick(0, projectView, &ring));
  CHECK(ring.direct && ring.live);
  int rx = 0, ry = 0;
  where(a, us[0], rx, ry);
  CHECK(ring.ringX == rx && ring.ringY == ry);
  g_view = b;
  CHECK(r.sampleActive(300, projectView, &ring));
  where(b, us[0], rx, ry);
  CHECK(ring.ringX == rx && ring.ringY == ry);
}

// ------------------------------------------------------------- D. no map open: nothing, not loss --
static void testNoView() {
  View v;
  viewSet(v, 46.05, 14.55, 15);
  v.ready = false;

  TraceAnim anim;
  anim.begin(kRoute, 4, false, 3, 0);
  TraceFrame f;
  g_view = v;
  CHECK(anim.sample(100, projectView, &f));
  CHECK(f.live);                 // the route is still running
  CHECK(f.routeCount == 0);      // but there is nowhere to put it
  CHECK(!f.hasDot && f.tailCount == 0 && f.ringAlpha == 0);
  CHECK(anim.active());          // not dropped

  View open;
  viewSet(open, 46.05, 14.55, 15);
  g_view = open;
  CHECK(anim.sample(200, projectView, &f));
  CHECK(f.routeCount == 4 && f.hasDot);
}

// ------------------------------------------------------------------- E. the cost of a sample -----
static void testCost() {
  TraceGeoPt big[12];
  for (int i = 0; i < 12; ++i) {
    big[i].latE6 = 46000000 + i * 150;   // a few pixels apart at z15
    big[i].lonE6 = 14400000 + i * 250;
  }
  View v;
  viewSet(v, 46.05, 14.55, 15);
  g_view = v;

  TraceAnim anim;
  anim.begin(big, 12, false, 8, 0);
  TraceFrame f;
  const uint32_t window = 8 * kTraceHopMs - 2;
  const int n = 20000;
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < n; ++i) {
    if (!anim.sample((uint32_t)(i % window), projectView, &f)) {
      CHECK(false);
      break;
    }
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double us =
      (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / 1000.0 / n;
  CHECK(f.routeCount == 12);

  // 12 lat/lon projections (log/tan each), the geometry and the tail. The device frame is a ~20 ms
  // canvas re-push; even a slow ESP32 cannot make this the frame's cost. Informational, not a hard
  // device number: this is the HOST measurement the ticket asks for.
  printf("[est394] 12-point trace sample: %.2f us/frame (%d projections) on this host\n", us,
         (int)12);
  CHECK(us < 2000.0);
}

// ---------------------------------------------- F. the picker's zoom label, clear of the back key --
static void checkHint(const Layout& L) {
  const RptrMapPickLayout m = rptrMapPickLayoutFor(L, true);
  CHECK(!m.hint.empty());
  CHECK(m.hint.w >= 30);                        // "z15" at 6 px per glyph
  // Under the back key, left-aligned to it, with a real gap: no label can reach "Cancel".
  CHECK(m.hint.x == m.back.x);
  CHECK(m.hint.y >= m.back.bottom() + 4);
  CHECK(!m.hint.overlaps(m.back));
  CHECK(!m.hint.overlaps(m.zoomIn));
  CHECK(!m.hint.overlaps(m.zoomOut));
  CHECK(!m.hint.overlaps(m.cross));
  CHECK(!m.hint.overlaps(m.panel));
  // On the map itself, not in the status bar or over the banner.
  CHECK(m.hint.y >= m.map.y);
  CHECK(m.hint.bottom() <= m.map.bottom());
  CHECK(m.hint.x >= 0 && m.hint.right() <= L.width);
  // It sits above the banner even on the short screen.
  CHECK(m.hint.bottom() < m.panel.y);
}

static void testHint() {
  checkHint(l2());
  const Layout wide = layoutFor(800, 480);
  checkHint(wide);
}

int main() {
  testPan();
  testZoom();
  testSampleActive();
  testNoView();
  testCost();
  testHint();
  if (g_fail) {
    printf("%d failure(s)\n", g_fail);
    return 1;
  }
  printf("EST-394: all checks passed\n");
  return 0;
}
