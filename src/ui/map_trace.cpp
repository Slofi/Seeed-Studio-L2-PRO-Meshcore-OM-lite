// SPDX-License-Identifier: MIT
// EST-368: the Map's trace model. No Arduino, no LovyanGFX: test/host builds this file directly.
#include "ui/map_trace.h"

#include <math.h>

namespace om {
namespace ui {

int traceMatchHop(const uint8_t* hash, uint8_t len, const TracePathHop* table, int count) {
  if (hash == nullptr || table == nullptr || len < 1 || len > 3) return -1;
  for (int i = 0; i < count; ++i) {
    if (table[i].keyLen < len) continue;   // this record cannot be the match
    if (memcmp(table[i].key, hash, len) == 0) return i;
  }
  return -1;
}

namespace {

bool samePoint(int32_t latA, int32_t lonA, int32_t latB, int32_t lonB) {
  return latA == latB && lonA == lonB;
}

void appendPoint(TraceRoute* out, int32_t lat, int32_t lon) {
  if (out == nullptr || out->count >= kTraceMaxPoints) return;
  if (out->count > 0 &&
      samePoint(out->latE6[out->count - 1], out->lonE6[out->count - 1], lat, lon)) {
    return;   // the sender is the first hop: one point, no zero-length segment
  }
  out->latE6[out->count] = lat;
  out->lonE6[out->count] = lon;
  ++out->count;
}

}  // namespace

void traceBuildRoute(const uint8_t* path, uint8_t hashSize, uint8_t hopCount,
                     const TracePathHop* table, int tableCount,
                     bool hasSender, int32_t senderLatE6, int32_t senderLonE6,
                     bool hasUs, int32_t usLatE6, int32_t usLonE6,
                     TraceRoute* out) {
  if (out == nullptr) return;
  out->count = 0;
  out->hops = hopCount;
  out->direct = hopCount == 0;
  out->resolved = 0;

  // The sender starts the route when it has a position (a channel sender is a contact by name; a
  // sender without a position simply has no start point, and the line begins at the first repeater).
  if (hasSender) appendPoint(out, senderLatE6, senderLonE6);

  const uint8_t size = (hashSize < 1) ? 1 : (hashSize > 3 ? 3 : hashSize);
  int hops = hopCount;
  if (hops > kTraceMaxHops) hops = kTraceMaxHops;
  for (int i = 0; i < hops; ++i) {
    const uint8_t* hash = path + (size_t)i * size;   // path is not read when hops == 0
    const int at = traceMatchHop(hash, size, table, tableCount);
    if (at < 0) continue;            // an unknown hop: the line jumps to the next known point
    if (!table[at].hasPos) continue; // known node, no position: the same skip
    ++out->resolved;
    appendPoint(out, table[at].latE6, table[at].lonE6);
  }

  // Us at the end: our GPS fix, else the last position we knew. Without one the route simply ends
  // at the last repeater (OM's rule: a radio without GPS must not blank the view).
  if (hasUs) appendPoint(out, usLatE6, usLonE6);
}

// --------------------------------------------------------------------------------- the geometry

void traceGeom(const TracePt* pts, int count, TraceGeom* g) {
  if (g == nullptr) return;
  g->count = 0;
  g->total = 0;
  if (pts == nullptr || count < 2) return;
  if (count > kTraceMaxPoints) count = kTraceMaxPoints;
  g->count = count;
  for (int i = 0; i < count - 1; ++i) {
    const double dx = (double)pts[i + 1].x - pts[i].x;
    const double dy = (double)pts[i + 1].y - pts[i].y;
    const int32_t len = (int32_t)lround(sqrt(dx * dx + dy * dy));
    g->start[i] = g->total;
    g->len[i] = len;
    g->total += len;
  }
}

bool tracePointAt(const TraceGeom& g, const TracePt* pts, float frac, TracePt* out) {
  if (out == nullptr || pts == nullptr || g.count < 2) return false;
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  const double target = (double)frac * (double)g.total;
  double walked = 0.0;
  for (int i = 0; i < g.count - 1; ++i) {
    const double len = (double)g.len[i];
    if (target <= walked + len || i == g.count - 2) {
      double t = (len > 0.0) ? (target - walked) / len : 0.0;
      if (t < 0.0) t = 0.0;
      if (t > 1.0) t = 1.0;
      out->x = (int16_t)lround(pts[i].x + ((double)pts[i + 1].x - pts[i].x) * t);
      out->y = (int16_t)lround(pts[i].y + ((double)pts[i + 1].y - pts[i].y) * t);
      return true;
    }
    walked += len;
  }
  return false;
}

int traceSlice(const TraceGeom& g, const TracePt* pts, float from, float to, TracePt* out,
               int cap) {
  if (out == nullptr || cap <= 0 || pts == nullptr || g.count < 2) return 0;
  if (from < 0.0f) from = 0.0f;
  if (to > 1.0f) to = 1.0f;
  if (to < from) {
    const float t = from;
    from = to;
    to = t;
  }
  int n = 0;
  TracePt first;
  if (!tracePointAt(g, pts, from, &first)) return 0;
  out[n++] = first;
  const double a = (double)from * (double)g.total;
  const double b = (double)to * (double)g.total;
  for (int i = 0; i < g.count - 1 && n < cap; ++i) {
    const double end = (double)g.start[i] + (double)g.len[i];
    if (end > a && end < b) out[n++] = pts[i + 1];
  }
  TracePt last;
  if (n < cap && tracePointAt(g, pts, to, &last)) out[n++] = last;
  return n;
}

// ------------------------------------------------------------------------------------ animation

void TraceAnim::begin(const TracePt* route, int count, bool direct, uint8_t hops,
                      uint32_t nowMs) {
  active_ = false;
  direct_ = direct;
  routeCount_ = 0;
  startedAt_ = nowMs;
  if (count < 0) count = 0;
  if (count > kTraceMaxPoints) count = kTraceMaxPoints;
  for (int i = 0; i < count; ++i) route_[i] = route[i];
  routeCount_ = count;
  traceGeom(route_, routeCount_, &geom_);
  int h = hops;
  if (h < 1) h = 1;
  if (h > kTraceMaxHops) h = kTraceMaxHops;
  durMs_ = direct_ ? kTraceDirectMs : (uint32_t)h * kTraceHopMs;
  active_ = true;
}

bool TraceAnim::sample(uint32_t nowMs, TraceFrame* out) {
  if (out == nullptr || !active_) return false;
  out->live = true;
  out->direct = direct_;
  out->routeCount = routeCount_;
  for (int i = 0; i < routeCount_; ++i) out->route[i] = route_[i];
  out->hasDot = false;
  out->tailCount = 0;
  out->dotAlpha = 0;
  out->tailAlpha = 0;
  out->ringAlpha = 0;
  out->ringR = 0;

  const uint32_t elapsed = nowMs - startedAt_;   // wrap-safe via the unsigned difference
  if (direct_) {
    if (elapsed >= durMs_) {
      active_ = false;
      out->live = false;
      return false;
    }
    const float t = (float)elapsed / (float)durMs_;
    out->ringX = routeCount_ > 0 ? route_[0].x : 0;
    out->ringY = routeCount_ > 0 ? route_[0].y : 0;
    out->ringR = (int16_t)lround((double)kTraceRingStartR +
                                 (double)t * (double)(kTraceRingMaxR - kTraceRingStartR));
    out->ringAlpha = (uint8_t)lround((1.0 - (double)t) * 255.0);
    return true;
  }

  if (geom_.count < 2) {
    // Fewer than two known points: nothing to travel along. A route with a known point still gets
    // the ring at that point (the ticket: "draws nothing but the ring").
    if (routeCount_ == 1 && elapsed < durMs_) {
      const float t = (float)elapsed / (float)durMs_;
      out->direct = true;
      out->ringX = route_[0].x;
      out->ringY = route_[0].y;
      out->ringR = (int16_t)lround((double)kTraceRingStartR +
                                   (double)t * (double)(kTraceRingMaxR - kTraceRingStartR));
      out->ringAlpha = (uint8_t)lround((1.0 - (double)t) * 255.0);
      return true;
    }
    active_ = false;
    out->live = false;
    return false;
  }

  const float tailFrac = (float)kTraceTailPct / 100.0f;
  if (elapsed <= durMs_) {
    const float head = (durMs_ == 0) ? 1.0f : (float)elapsed / (float)durMs_;
    const float from = head > tailFrac ? head - tailFrac : 0.0f;
    out->tailCount = traceSlice(geom_, route_, from, head, out->tail, kTraceMaxPoints + 1);
    out->tailAlpha = 230;
    out->hasDot = tracePointAt(geom_, route_, head, &out->dot);
    out->dotAlpha = 255;
    return true;
  }

  // Arrived: hold the last tail, then fade tail and dot together over kTraceFadeMs.
  const uint32_t into = elapsed - durMs_;
  if (into >= kTraceFadeMs) {
    active_ = false;
    out->live = false;
    return false;
  }
  const float fade = (float)into / (float)kTraceFadeMs;
  out->tailCount = traceSlice(geom_, route_, 1.0f - tailFrac, 1.0f, out->tail,
                              kTraceMaxPoints + 1);
  out->tailAlpha = (uint8_t)lround((1.0 - (double)fade) * 230.0);
  out->hasDot = tracePointAt(geom_, route_, 1.0f, &out->dot);
  out->dotAlpha = (uint8_t)lround((1.0 - (double)fade) * 255.0);
  return true;
}

// ---------------------------------------------------------------------------------------- queue

void TraceQueue::reset() {
  anim_.cancel();
  count_ = 0;
}

void TraceQueue::removeAt(int i) {
  if (i < 0 || i >= count_) return;
  for (int k = i; k + 1 < count_; ++k) pending_[k] = pending_[k + 1];
  --count_;
}

bool TraceQueue::push(const TracePt* route, int count, bool direct, uint8_t hops, uint8_t key) {
  if (route == nullptr || count < 0) return false;
  if (count > kTraceMaxPoints) count = kTraceMaxPoints;
  if (count < 2 && !(direct && count == 1)) return false;
  // A newer route from the same sender replaces the one still waiting (one tail per corridor).
  for (int i = 0; i < count_; ++i) {
    if (pending_[i].key == key) removeAt(i);
  }
  if (count_ >= kTraceQueueMax) removeAt(0);   // drop the oldest pending route
  Item& it = pending_[count_++];
  for (int i = 0; i < count; ++i) it.pts[i] = route[i];
  it.count = count;
  it.direct = direct;
  it.hops = hops;
  it.key = key;
  return true;
}

bool TraceQueue::tick(uint32_t nowMs, TraceFrame* out) {
  if (out == nullptr) return false;
  out->live = false;
  for (;;) {
    if (!anim_.active()) {
      if (count_ == 0) return false;
      const Item it = pending_[0];
      removeAt(0);
      anim_.begin(it.pts, it.count, it.direct, it.hops, nowMs);
    }
    if (anim_.sample(nowMs, out)) return true;
    // That one finished inside this call: the next pending route starts at once.
    out->live = false;
    if (count_ == 0 && !anim_.active()) return false;
  }
}

}  // namespace ui
}  // namespace om
