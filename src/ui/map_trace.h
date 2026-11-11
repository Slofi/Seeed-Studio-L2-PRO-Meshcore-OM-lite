// SPDX-License-Identifier: MIT
// EST-368: the Map's traces, ported from OverMesh (static/js/app.js, MC RX traces) and the mock
// (ui-mock/index.html, "OM's animated Traces").
//
// A received message carries the repeaters' path hashes (2 bytes each in this mesh, 1..3 possible).
// Each hop hash is the first bytes of a repeater's public key, so it can be matched against our
// contacts and the repeater's advertised position. The route is the sender (if it has a position),
// the resolved hops in order, and us at the end. Hops with no known position are skipped: the line
// jumps to the next known point. Fewer than two known points draws nothing but the ring.
//
// The drawing follows OM: the whole route as a faint dashed line, and a dot with a short fading tail
// travelling hop by hop at 420 ms per hop (35 % tail, 900 ms fade on arrival); a direct arrival
// (0 hops) pulses an expanding ring at our own radio instead. Replay runs the routes ONE AT A TIME.
//
// EST-394 (Filip 2026-11-11 16:41: "traces are all screwed up if/when I move the map"): the queue
// and the animation keep the route in micro-degrees, NOT in screen pixels. Every sample projects the
// route through the caller's current view (mapProject on the device), so a pan, a zoom or the bars'
// slide moves the whole trace with the map. A frame is only ever valid for the view it was sampled
// in, which is why the Map tab samples one at the top of build() for the frame it is about to draw.
//
// Plain C++ (no Arduino, no LovyanGFX) so test/host proves the mapping, the geometry, the timings
// and the queue with the system g++.
#pragma once

#include <stdint.h>
#include <string.h>

namespace om {
namespace ui {

constexpr int kTraceMaxPoints = 12;    // sender + 8 hops + us, with room to spare
constexpr int kTraceMaxHops = 8;       // MeshCore's own trace limit
constexpr uint32_t kTraceHopMs = 420;  // travel time per hop
constexpr uint32_t kTraceFadeMs = 900; // tail fade once the packet arrives
constexpr uint32_t kTraceDirectMs = 1400;   // the expanding ring of a direct arrival
constexpr uint16_t kTraceTailPct = 35;      // tail length, percent of the whole route
constexpr int16_t kTraceRingStartR = 3;
constexpr int16_t kTraceRingMaxR = 26;
constexpr int kTraceQueueMax = 4;      // pending routes; the newest per sender wins
constexpr int kTraceSeedMax = 4;       // how many routes the first draw replays

// One repeater as the mapping needs it: the path hash prefix of its public key and its position.
// `hasPos` false means the node is known but was never heard with a position: its hop is skipped.
struct TracePathHop {
  uint8_t key[3] = {};
  uint8_t keyLen = 0;      // 1..3, the bytes of the public key this record carries
  bool hasPos = false;
  int32_t latE6 = 0;
  int32_t lonE6 = 0;
};

// The contact whose public-key prefix equals this hop hash, or -1. `len` is the path hash size the
// packet used (1, 2 or 3 bytes); a contact matches when the first `len` bytes are equal.
int traceMatchHop(const uint8_t* hash, uint8_t len, const TracePathHop* table, int count);

// One route in micro-degrees, oldest hop first. `hops` is the path hop count the packet carried
// (0 = direct); `count` is the number of KNOWN points actually drawn.
struct TraceRoute {
  int count = 0;
  int32_t latE6[kTraceMaxPoints] = {};
  int32_t lonE6[kTraceMaxPoints] = {};
  uint8_t hops = 0;
  bool direct = false;
  int resolved = 0;    // hops that matched a positioned contact
};

// Build the route for one received message. `path` holds `hopCount` hashes of `hashSize` bytes.
// The sender endpoint is used only when it has a position; us at the end only when we have one
// (our fix, else our last known position). Duplicate consecutive points are dropped.
void traceBuildRoute(const uint8_t* path, uint8_t hashSize, uint8_t hopCount,
                     const TracePathHop* table, int tableCount,
                     bool hasSender, int32_t senderLatE6, int32_t senderLonE6,
                     bool hasUs, int32_t usLatE6, int32_t usLonE6,
                     TraceRoute* out);

// --------------------------------------------------------------------------------- the geometry
// Screen pixels, built from ONE sample's projection of the route (the animation needs distances).
struct TracePt {
  int16_t x = 0;
  int16_t y = 0;
};

// EST-394: one georeferenced route point, exactly the micro-degrees traceBuildRoute() produces.
// This is what the queue and the animation keep; a TracePt only ever exists for one sample, in the
// view that sample was taken in.
struct TraceGeoPt {
  int32_t latE6 = 0;
  int32_t lonE6 = 0;
};

// The caller's current view: the device passes mapProject(), a host test passes its own. False means
// "the map is not open", which draws nothing but never loses the route.
typedef bool (*TraceProject)(double lat, double lon, int& x, int& y);

struct TraceGeom {
  int count = 0;
  int32_t total = 0;                        // route length in pixels
  int32_t start[kTraceMaxPoints] = {};      // cumulative start of each segment
  int32_t len[kTraceMaxPoints] = {};        // its length
};

void traceGeom(const TracePt* pts, int count, TraceGeom* g);
// A point at a fraction of the route's length.  are the geometry's own points.
bool tracePointAt(const TraceGeom& g, const TracePt* pts, float frac, TracePt* out);
// The visible tail between two fractions, including every vertex in between.
int traceSlice(const TraceGeom& g, const TracePt* pts, float from, float to, TracePt* out,
               int cap);

// One frame's worth of drawing for ONE route, valid only for the view it was sampled in.
struct TraceFrame {
  bool live = false;        // something is animating (draw it)
  bool direct = false;      // a ring, no line
  // the whole route, faint and dashed (also drawn for a direct arrival: it is one point)
  TracePt route[kTraceMaxPoints];
  int routeCount = 0;
  // the moving dot and its tail
  TracePt dot;
  bool hasDot = false;
  uint8_t dotAlpha = 0;
  TracePt tail[kTraceMaxPoints + 1];
  int tailCount = 0;
  uint8_t tailAlpha = 0;
  // the expanding ring of a direct arrival
  int16_t ringX = 0;
  int16_t ringY = 0;
  int16_t ringR = 0;
  uint8_t ringAlpha = 0;
};

// One route's animation. begin() starts the clock on a georeferenced route; sample() projects it
// through the CURRENT view and answers what to draw at nowMs, false once the route has arrived AND
// faded. A route whose points cannot be projected (the map pack is not open) stays live and draws
// nothing; it is never dropped.
class TraceAnim {
 public:
  void begin(const TraceGeoPt* route, int count, bool direct, uint8_t hops, uint32_t nowMs);
  void cancel() { active_ = false; }
  bool active() const { return active_; }
  uint32_t durationMs() const { return durMs_; }
  bool sample(uint32_t nowMs, TraceProject project, TraceFrame* out);

 private:
  bool active_ = false;
  bool direct_ = false;
  TraceGeoPt route_[kTraceMaxPoints] = {};
  int routeCount_ = 0;
  uint32_t startedAt_ = 0;
  uint32_t durMs_ = 0;
};

// The replay queue: one route at a time, and a new message from the SAME sender replaces the older
// pending one, so three messages from one node cannot draw three tails on one corridor.
class TraceQueue {
 public:
  void reset();
  bool busy() const { return anim_.active() || count_ > 0; }
  int pending() const { return count_; }
  // False when the route has nothing to draw (fewer than two points and not a usable direct point).
  bool push(const TraceGeoPt* route, int count, bool direct, uint8_t hops, uint8_t key);
  // Start the next route when idle, sample the current one. False when nothing is live.
  bool tick(uint32_t nowMs, TraceProject project, TraceFrame* out);
  // EST-394: re-sample the route that is already running in a NEW view, without starting a pending
  // one. The Map tab calls this from build(), so the frame it draws was projected for the view under
  // it (a pan, a zoom, a slide). False when no route is running.
  bool sampleActive(uint32_t nowMs, TraceProject project, TraceFrame* out);

 private:
  struct Item {
    TraceGeoPt pts[kTraceMaxPoints] = {};
    int count = 0;
    bool direct = false;
    uint8_t hops = 0;
    uint8_t key = 0;
  };
  void removeAt(int i);
  TraceAnim anim_;
  Item pending_[kTraceQueueMax];
  int count_ = 0;
};

}  // namespace ui
}  // namespace om
