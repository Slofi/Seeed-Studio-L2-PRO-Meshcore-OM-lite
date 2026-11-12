// SPDX-License-Identifier: MIT
// EST-409: the Map fixes that can be proven without a device.
//
//  1. a route point projected outside the int16-safe band rejects the whole frame (draw nothing,
//     keep the animation clock) instead of being cast to a wrapped pixel,
//  9. a packet with more hops than MeshCore's trace limit reports the clamped count and a flag,
// 10. the "newest route per sender wins" key is the full 32-bit hash, and a removal does not skip
//     the next pending route,
//  4. the signature cache rebuilds its rows only when the source's signature moves.
//
// Plain C++ against the same headers the device builds, no Arduino.
#include <cmath>
#include <cstdio>

#include "ui/map_trace.h"
#include "ui/signature_cache.h"

using namespace om::ui;

static int g_fail = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++g_fail;
  }
}

// map.cpp's z18 projection is 256 * 2^17 / 360 * 2 pixels per degree of longitude (kZMax = 18).
static constexpr double kPxPerDegLonZ18 = 256.0 * 131072.0 / 360.0 * 2.0;
static constexpr int32_t kCentreLonE6 = 14500000;   // invented, never a real position (rule 9)

static bool projectZ18(double lat, double lon, int& x, int& y) {
  (void)lat;
  x = (int)std::lround((lon - (double)kCentreLonE6 / 1e6) * kPxPerDegLonZ18);
  y = 133;
  return true;
}

// ------------------------------------------------------------------ 1. the far point, before the cast
static void testFarPointRejected() {
  // 50 km east of the centre at 46 N is about 0.647 degrees of longitude; at z18 that is ~120,000
  // px, far beyond what an int16 can hold.
  const double deg = 50.0 / 77.3;
  const int32_t lonE6 = kCentreLonE6 + (int32_t)std::lround(deg * 1e6);
  int px = 0, py = 0;
  projectZ18(46.0, (double)lonE6 / 1e6, px, py);
  check(px > 16384, "the 50 km point projects outside the int16-safe band");
  check((int16_t)px != px, "the old (int16_t) cast would wrap it to the opposite sign");

  const TraceGeoPt route[2] = {{46000000, kCentreLonE6}, {46000000, lonE6}};
  TraceAnim anim;
  TraceFrame f;
  anim.begin(route, 2, false, 1, 0);
  check(anim.sample(0, projectZ18, &f), "an unprojectable frame still samples");
  check(f.live, "it stays live (the clock is not restarted or stopped)");
  check(f.routeCount == 0, "no route point is placed outside the int16-safe band");
  check(f.tailCount == 0 && !f.hasDot, "no wrapped coordinate reaches the tail or the dot");
  check(anim.active(), "the animation keeps its clock");

  // A near route still draws: the guard rejects the far frame, not every frame.
  const TraceGeoPt near[2] = {{46000000, kCentreLonE6}, {46000000, kCentreLonE6 + 100}};
  TraceAnim ok;
  TraceFrame g;
  ok.begin(near, 2, false, 1, 0);
  check(ok.sample(0, projectZ18, &g), "a near frame samples");
  check(g.routeCount == 2 && g.hasDot, "a near route still draws");
}

// ------------------------------------------------------------- 9. the hop count is clamped once
static void testHopsClamp() {
  uint8_t path[12] = {};
  TraceRoute r;
  traceBuildRoute(path, 1, 12, nullptr, 0, false, 0, 0, false, 0, 0, &r);
  check(r.hops == kTraceMaxHops, "a 12-hop packet reports the clamped 8 hops");
  check(r.hopsClamped, "the clamp is flagged");
  check(!r.direct, "a clamped route is not direct");

  TraceRoute d;
  traceBuildRoute(nullptr, 1, 0, nullptr, 0, false, 0, 0, false, 0, 0, &d);
  check(d.hops == 0 && !d.hopsClamped, "a direct route is not flagged");
}

// ------------------------------------------------------- 10. the full sender key and the dedupe loop
static bool flatProject(double, double, int& x, int& y) {
  x = 160;
  y = 133;
  return true;
}

static void testQueueKeys() {
  const TraceGeoPt a[2] = {{0, 0}, {0, 50}};
  const TraceGeoPt b[3] = {{100, 0}, {100, 50}, {100, 100}};
  const uint32_t keyA = 0x00000123u;
  const uint32_t keyB = 0x00000223u;   // shares the low byte with keyA
  TraceQueue q;
  check(q.push(a, 2, false, 1, keyA), "sender A is queued");
  check(q.push(b, 3, false, 1, keyB), "sender B is queued");
  check(q.pending() == 2, "two senders that share the low byte do not replace each other");
  check(q.push(b, 3, false, 1, keyB), "sender B again is queued");
  check(q.pending() == 2, "the same full sender key still replaces its pending route");

  // A repeat of the first sender removes it at index 0; the route after it must not be skipped.
  TraceQueue q2;
  check(q2.push(a, 2, false, 1, 10), "first route is queued");
  check(q2.push(b, 3, false, 1, 20), "second route is queued");
  check(q2.push(a, 2, false, 1, 10), "the first sender repeats");
  check(q2.pending() == 2, "the repeat leaves the other sender's route in the queue");
  TraceFrame f;
  check(q2.tick(0, flatProject, &f), "the surviving route plays");
  check(f.routeCount == 3, "the other sender's route is the one that plays");
}

// ------------------------------------------------------------- 4. the signature cache rebuild rule
struct Row {
  int v = 0;
};

static void testSignatureCache() {
  SignatureCache<Row, 4> c;
  check(c.begin(100), "the first signature asks for a rebuild");
  c.rows()[0].v = 7;
  c.rows()[1].v = 8;
  c.commit(2);
  check(!c.begin(100), "the same signature reuses the cached rows");
  check(c.count() == 2 && c.rows()[0].v == 7 && c.rows()[1].v == 8,
        "the cached rows are unchanged");
  check(c.begin(101), "a moved signature asks for a rebuild");
  c.rows()[0].v = 9;
  c.commit(1);
  check(c.count() == 1 && c.rows()[0].v == 9, "the rebuilt rows are the new ones");
  check(c.begin(99), "the signature can move back (a later advert changes the list again)");
  c.commit(0);
  check(c.count() == 0 && !c.begin(99), "an empty list is a valid cached snapshot");
  c.commit(99);
  check(c.count() == 4, "commit clamps to the capacity");
}

int main() {
  testFarPointRejected();
  testHopsClamp();
  testQueueKeys();
  testSignatureCache();
  if (g_fail) {
    std::printf("%d est409 check(s) FAILED\n", g_fail);
    return 1;
  }
  std::printf("est409: far frame rejected, hops clamped, 32-bit sender keys, signature cache\n");
  return 0;
}
