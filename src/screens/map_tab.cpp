// SPDX-License-Identifier: MIT
// EST-371: the Map tab's state, drawing glue and tap handling, moved out of main.cpp. The pixels,
// rectangles and hit test stay in ui/screen_map.cpp and ui/map_chrome.h; map.cpp stays the tile
// reader. This file only builds the one MapView from the real contact table and our own fix, owns
// the Traces animation and its queue, and remembers the two saved layer switches.
#include "screens/map_tab.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "map.h"
#include "mesh_link.h"
#include "timekeeper.h"
#include "ui/age.h"
#include "ui/map_chrome.h"
#include "ui/press_targets.h"   // EST-390: the pressed rectangle comes from the same hit test
#include "ui/signature_cache.h" // EST-409: the contact table, copied only when its signature moves

namespace om {
namespace map_tab {
namespace {

Host s_host;
bool s_lock = false;              // follow our own position
char s_selName[32] = "";          // the marker whose callout is open ("" = none)
// EST-409: ONE copy of the contact table, shared by build()'s markers and traceHopTable()'s path-hash
// lookup, and rebuilt only when om::meshContactSignature() moves. Before this, meshContactList()
// copied and insertion-sorted up to 32 records on every animation frame and twice per tap.
ui::SignatureCache<MeshContact, 32> s_contacts;
bool s_markerBudgetLogged = false;   // the "N of M drawn" line, once per rebuilt contact list

// --- EST-368 / EST-370: the layer switches and OM's Traces ---------------------------------------
ui::MapLayerPrefs s_layers;
ui::TraceQueue s_traceQueue;     // one route at a time
ui::TraceFrame s_traceFrame;     // what the frame being drawn shows
uint32_t s_traceNextFrameMs = 0; // the animation's own frame clock
uint32_t s_traceSeqStartMs = 0;  // when the current replay started (cost line)
uint32_t s_traceFrames = 0;      // animation frames drawn in this replay
uint32_t s_traceFrameUs = 0;     // their total
uint32_t s_traceWorstUs = 0;     // the slowest one
int32_t s_traceSelfLatE6 = 0;    // our fix, else the last position we knew
int32_t s_traceSelfLonE6 = 0;
bool s_traceSelfKnown = false;
// EST-409: the three ways a route can vanish before it is queued, counted by reason and printed in
// traceCostLine(). A silent drop made "the trace did not show" indistinguishable from "traces off".
uint32_t s_traceDropped = 0;
uint32_t s_traceDropNoPoint = 0;
uint32_t s_traceDropNoSelf = 0;
uint32_t s_traceDropQueue = 0;
constexpr uint32_t kTraceFrameMs = 33;   // ~30 fps while a trace runs; nothing when idle

// EST-371: the fast open. enter() arms s_openFast; draw() then repushes the canvas map.cpp already
// composited (PSRAM, 320x214x2 = 133.8 KB) and redraws every piece of chrome, so the frame is current
// except for its tiles. Only a view that changed since that composite queues a refresh, which pump()
// runs one loop pass later, after the cached frame is on the glass.
bool s_openFast = false;
bool s_refreshPending = false;

const PositionFix& fix() {
  static const PositionFix none{};
  return (s_host.gps != nullptr) ? *s_host.gps() : none;
}

ui::TabBarModel tabBar(ui::Tab active) {
  if (s_host.tabBar == nullptr) return ui::TabBarModel();
  return s_host.tabBar(active);
}

void status(ui::StatusModel& out) {
  if (s_host.buildStatus != nullptr) s_host.buildStatus(out);
}

void toast(const char* text) {
  if (s_host.toast != nullptr) s_host.toast(text);
}

void traceRememberSelf() {
  const PositionFix& g = fix();
  if (!g.valid) return;
  s_traceSelfLatE6 = (int32_t)lround(g.lat * 1e6);
  s_traceSelfLonE6 = (int32_t)lround(g.lon * 1e6);
  s_traceSelfKnown = true;
}

uint32_t traceKeyForName(const char* name) {
  // EST-409: the full 32-bit FNV hash. The old one-byte key made two senders collide about once in
  // 256 pairs (the queue is only four deep, but the "newest per sender wins" rule must still separate
  // them); the queue stores all 32 bits.
  uint32_t h = 2166136261u;
  for (const char* p = (name ? name : ""); *p; ++p) h = (h ^ (uint8_t)*p) * 16777619u;
  return h;
}

void traceMapReset() {
  s_traceQueue.reset();
  s_traceFrame = ui::TraceFrame();
  s_traceNextFrameMs = 0;
  s_traceFrames = 0;
  s_traceFrameUs = 0;
  s_traceWorstUs = 0;
  s_traceSeqStartMs = 0;
}

#if defined(OM_ENABLE_MESH)
// EST-409: the contact table is copied and insertion-sorted only when its source signature moves.
// build() (every animation frame), pressTarget() and tap() all reuse this one snapshot.
void contactsEnsure() {
  if (s_contacts.begin(om::meshContactSignature())) {
    s_contacts.commit(om::meshContactList(s_contacts.rows(), s_contacts.capacity()));
    s_markerBudgetLogged = false;   // a new list may hit the 32-contact budget again
  }
}

// The repeaters we know with a public key, in the same 32-row view the map's markers use. A hop hash
// is the first 1..3 bytes of the key, so the table carries three and the model compares only the
// packet's own hash size.
int traceHopTable(ui::TracePathHop* table, int cap) {
  contactsEnsure();
  const MeshContact* list = s_contacts.rows();
  const int n = s_contacts.count();
  int k = 0;
  for (int i = 0; i < n && k < cap; ++i) {
    if (list[i].type != 2 || !list[i].has_key) continue;
    ui::TracePathHop& h = table[k++];
    h.key[0] = list[i].pub_key[0];
    h.key[1] = list[i].pub_key[1];
    h.key[2] = list[i].pub_key[2];
    h.keyLen = 3;
    h.hasPos = list[i].has_pos;
    h.latE6 = list[i].lat_e6;
    h.lonE6 = list[i].lon_e6;
  }
  return k;
}

// Build one message's route and hand it to the replay queue. The endpoints: the sender when its
// contact has a position, us at the end (the GPS fix, else the last position we knew).
bool traceEnqueue(const char* sender, const uint8_t* path, uint8_t hashSize, uint8_t hops,
                  uint32_t key) {
  ui::TracePathHop table[32];
  const int tn = traceHopTable(table, 32);
  MeshContact sc{};
  const bool senderKnown = (sender != nullptr && sender[0] != 0) &&
                           om::meshContactByName(sender, &sc) && sc.has_pos;
  ui::TraceRoute route;
  ui::traceBuildRoute(path, hashSize, hops, table, tn, senderKnown, sc.lat_e6, sc.lon_e6,
                      s_traceSelfKnown, s_traceSelfLatE6, s_traceSelfLonE6, &route);
  if (route.count == 0) {
    ++s_traceDropped;
    ++s_traceDropNoPoint;
    return false;
  }

  // EST-394: queue the route in micro-degrees. Nothing is projected here: the animation samples it
  // through mapProject() at draw time, so a pan, a zoom or the bars' slide moves the trace with the
  // map instead of leaving it at the pixels it had when the message arrived.
  ui::TraceGeoPt pts[ui::kTraceMaxPoints];
  int n = 0;
  const bool direct = route.hops == 0;
  if (direct) {
    // EST-368 (the ticket): a 0-hop arrival pulses a ring at OUR position, not a line. Without one
    // there is nothing to pulse, and the arrival stays silent on the glass.
    if (!s_traceSelfKnown) {
      ++s_traceDropped;
      ++s_traceDropNoSelf;
      return false;
    }
    pts[0].latE6 = s_traceSelfLatE6;
    pts[0].lonE6 = s_traceSelfLonE6;
    n = 1;
  } else {
    for (int i = 0; i < route.count; ++i) {
      pts[n].latE6 = route.latE6[i];
      pts[n].lonE6 = route.lonE6[i];
      ++n;
    }
  }
  const bool ringOnly = direct || n < 2;
  bool evicted = false;
  if (ringOnly) {
    // Fewer than two known points: the line is not drawn, only the ring. It pulses at the one point
    // the route does know (normally our own radio at its end).
    if (n >= 2) pts[0] = pts[n - 1];
    n = 1;
    const bool ok = s_traceQueue.push(pts, 1, true, route.hops, key, &evicted);
    if (evicted) {
      ++s_traceDropped;
      ++s_traceDropQueue;
    }
    if (ok) {
      // EST-409: counts only (rule 9). The sender's name used to be printed here.
      Serial.printf("[map] trace %u hop(s)%s, ring only\n", (unsigned)route.hops,
                    route.hopsClamped ? " (hops clamped)" : "");
    }
    return ok;
  }
  const bool ok = s_traceQueue.push(pts, n, false, route.hops, key, &evicted);
  if (evicted) {
    ++s_traceDropped;
    ++s_traceDropQueue;
  }
  if (ok) {
    // EST-409: counts only (rule 9): hops, known points, resolved hops.
    Serial.printf("[map] trace %u hop(s)%s, %d known point(s), %d resolved\n",
                  (unsigned)route.hops, route.hopsClamped ? " (hops clamped)" : "", n,
                  route.resolved);
  }
  return ok;
}

void traceEnqueueRingMessage(int idx) {
  uint8_t hashSize = 1, count = 0;
  const uint8_t* path = om::meshMsgPath(idx, &hashSize, &count);
  const char* who = om::meshMsgName(idx);
  traceEnqueue(who, path, hashSize, count, traceKeyForName(who));
}

// The Map's first replay: the newest message per sender, up to kTraceSeedMax, oldest first so the
// queue plays them in the order they arrived.
void traceSeedFromRing() {
  traceRememberSelf();
  int picked = 0;
  int idx[ui::kTraceSeedMax];
  char seen[ui::kTraceSeedMax][32];
  for (int i = om::meshMsgCount() - 1; i >= 0 && picked < ui::kTraceSeedMax; --i) {
    if (om::meshMsgMine(i)) continue;
    const char* who = om::meshMsgName(i);
    bool dup = false;
    for (int k = 0; k < picked; ++k) {
      if (strcmp(seen[k], who) == 0) dup = true;
    }
    if (dup) continue;
    snprintf(seen[picked], sizeof(seen[picked]), "%s", who);
    idx[picked++] = i;
  }
  for (int k = picked - 1; k >= 0; --k) traceEnqueueRingMessage(idx[k]);

  // One DM: the newest received message across the thread table, so a DM's hop also animates.
  int bestThread = -1;
  uint32_t bestSeq = 0;
  MeshDm best{};
  for (int t = 0; t < om::meshDmCount(); ++t) {
    MeshDm m{};
    if (!om::meshDmNewest(t, &m) || m.mine) continue;
    if (m.seq < bestSeq) continue;
    bestSeq = m.seq;
    bestThread = t;
    best = m;
  }
  if (bestThread >= 0) {
    traceEnqueue(best.name, best.pathCount > 0 ? best.path : nullptr, best.pathHashSize,
                 best.pathCount, traceKeyForName(best.name));
  }
  if (s_traceQueue.pending() > 0) s_traceSeqStartMs = millis();
}
#else
// The mesh-off build has no messages to trace; the key and the model are still compiled (host tests
// and the Map screen use them) but nothing is ever replayed.
void traceSeedFromRing() {}
#endif  // OM_ENABLE_MESH

const char* contactTypeName(uint8_t type) {
  return type == 2 ? "repeater" : (type == 3 ? "room" : (type == 4 ? "sensor" : "client"));
}

// EST-394: re-project the route that is running through the view as it is NOW. build() calls this for
// every MapView (the full draw, the animation frame, the slide, the tap's hit test), so no draw path
// can paint a trace that was sampled before a pan or a zoom. A frame that is not live stays as it is;
// the queue's own tick() still starts the next pending route and reports completion.
void traceSampleNow() {
  if (!s_traceFrame.live) return;
  s_traceQueue.sampleActive(millis(), om::mapProject, &s_traceFrame);
}

// EST-409: `sampleTrace` is false for the two QUERY calls (the pressed look and the tap's hit test).
// Those must not change state: no trace clock advance, no selection change. The draw paths pass true.
void build(ui::MapView& v, bool sampleTrace) {
  v = ui::MapView();
  status(v.status);
  v.tabs = tabBar(ui::Tab::Map);
  v.zoom = om::mapZoom();
  v.locked = s_lock;
  v.hasFix = fix().valid;
  v.markerCount = 0;
  // EST-368: the Traces key's highlight and the one live animation frame. EST-370: the Names key's
  // highlight; both come from the one saved-switch store. EST-373: and the hidden tab column.
  v.tracesOn = s_layers.traces();
  v.namesOn = s_layers.names();
  v.tabsHidden = s_layers.tabsHidden();
  if (sampleTrace) {
    traceSampleNow();   // EST-394: the CURRENT view, not the one the route was queued in
    v.trace = s_traceFrame;
  }

  // Our own position: the white GPS dot, or the small gold pin when the point is Fixed (EST-384).
  const PositionFix& g = fix();
  if (g.valid) {
    int x = 0, y = 0;
    // EST-374: mapProject() answers in screen coordinates (the canvas starts under the status bar),
    // so the visible band is kMapCanvasTop..kMapCanvasTop + kMapCanvasH.
    if (om::mapProject(g.lat, g.lon, x, y) && x >= 0 && x < ui::kMapCanvasW &&
        y >= ui::kMapCanvasTop && y < ui::kMapCanvasTop + ui::kMapCanvasH) {
      ui::MapMarker& m = v.markers[v.markerCount++];
      m = ui::MapMarker();
      m.x = (int16_t)x;
      m.y = (int16_t)y;
      m.type = 1;
      m.name = om::meshNodeName();
      m.self = true;
      m.fixed = g.fixed;   // EST-384: the same rect, a pin instead of the GPS dot
    }
  }

#if defined(OM_ENABLE_MESH)
  contactsEnsure();   // EST-409: the cached table, rebuilt only when its signature moves
  const MeshContact* list = s_contacts.rows();
  const int n = s_contacts.count();
  const uint32_t nowWall = om::timeKeeper().now();
  const uint32_t nowUp = millis() / 1000;
  const bool nowReal = om::timeKeeper().realSinceBoot();
  // EST-409: our own dot has its own slot (screen_map.h, kMapMaxMarkers + 1); the contact budget is
  // exactly kMapMaxMarkers, so a full table no longer loses its 32nd row to the self marker.
  int candidates = 0;
  int drawn = 0;
  for (int i = 0; i < n; ++i) {
    if (!list[i].has_pos) continue;
    int x = 0, y = 0;
    if (!om::mapProject((double)list[i].lat_e6 / 1e6, (double)list[i].lon_e6 / 1e6, x, y)) continue;
    if (x < -ui::kMapMarkerR || x >= ui::kMapCanvasW + ui::kMapMarkerR ||
        y < ui::kMapCanvasTop - ui::kMapMarkerR ||
        y >= ui::kMapCanvasTop + ui::kMapCanvasH + ui::kMapMarkerR) {
      continue;
    }
    ++candidates;
    if (drawn >= ui::kMapMaxMarkers) continue;
    ui::MapMarker& m = v.markers[v.markerCount++];
    m = ui::MapMarker();
    m.x = (int16_t)x;
    m.y = (int16_t)y;
    m.type = list[i].type;
    m.name = list[i].name;
    if (s_selName[0] && strcmp(s_selName, list[i].name) == 0) {
      m.selected = true;
      v.callout = true;
      snprintf(v.calloutTitle, sizeof(v.calloutTitle), "%s", list[i].name);
      char stamp[24] = "--";
      ui::stampText(stamp, sizeof(stamp),
                    ui::Stamp{list[i].lastmod, list[i].lastmodUp, list[i].lastmodReal}, nowWall,
                    nowUp, nowReal, ui::StampForm::DayTime);
      char path[48] = "";
      om::meshContactPathText(list[i].name, path, sizeof(path));
      snprintf(v.calloutLine, sizeof(v.calloutLine), "%s \xC2\xB7 %s \xC2\xB7 %s",
               contactTypeName(list[i].type), path, stamp);
    }
    ++drawn;
  }
  if (candidates > drawn && !s_markerBudgetLogged) {
    s_markerBudgetLogged = true;
    Serial.printf("[map] markers %d of %d drawn (budget %d)\n", drawn, candidates,
                  ui::kMapMaxMarkers);
  }
#endif
  // EST-409: the selection is NOT cleared here. build() is a query from pressTarget() and tap(), so a
  // touch-down used to clear it; and a marker panned off the canvas must keep its callout, which comes
  // back when it is panned back. Only an explicit tap elsewhere clears it (the Map case in tap()).
}

void drawAnimFrame() {
  const PositionFix& g = fix();
  if (s_lock && g.valid) om::mapSetView(g.lat, g.lon, om::mapZoom());
  ui::MapView v;
  build(v, true);
  ui::mapScreenAnimate(v);
}

// EST-374: the hide/show slide. The map never moves (the canvas is full height in both states), so
// every frame is a cached-canvas repaint with only the chrome moved: the five tabs ride right by
// 19 px and the bottom page bar drops by 22 px, each by the same fraction of its own travel. The
// tab offset is the one that must land on the hidden sliver (x 314); the bar keeps its 3 px strip.
// kMapSlideSteps - 1
// interpolated frames plus the steady frame; from the EST-371 numbers a frame is a 19.8 ms canvas
// repush plus an 11.5 ms panel push (31 ms, under the ticket's 35 ms limit), so 4 frames is about
// 125 ms inside the 120-150 ms window. With no canvas to repush (a first entry after boot) the loop
// is skipped: an empty map sliding would say nothing, and the one draw below is the whole change.
void slideBars(bool hidden) {
  ui::MapView v;
  build(v, true);   // the view already carries the NEW state: chip, chevron and dot flip on the first frame
  const uint32_t t0 = millis();
  int frames = 0;
  if (ui::kMapSlideSteps > 1 && om::mapCanvasReady()) {
    for (int i = 1; i < ui::kMapSlideSteps; ++i) {
      int tabOff = (int)ui::kMapTabSlideDx * i / ui::kMapSlideSteps;
      int barOff = (int)ui::kMapBarSlideDy * i / ui::kMapSlideSteps;
      // Tucking moves the groups AWAY from home (off grows); restoring walks them back.
      if (!hidden) {
        tabOff = (int)ui::kMapTabSlideDx - tabOff;
        barOff = (int)ui::kMapBarSlideDy - barOff;
      }
      ui::mapScreenSlide(v, (int16_t)tabOff, (int16_t)barOff);
      ++frames;
    }
  }
  draw();   // the steady frame of the new state
  Serial.printf("[map] bars %s: %d slide frame(s) + 1 in %lu ms%s\n", hidden ? "tuck" : "restore",
                frames, (unsigned long)(millis() - t0),
                frames == 0 ? " (instant: no canvas)" : "");
}

#if defined(OM_ENABLE_MESH)
// The arrival watcher: a channel message or a received DM that lands while the Map is open animates
// at once. Only the newest packet of a pass is traced (a burst is one route, not five).
void traceWatchArrivals() {
  static uint32_t seenMsg = 0;
  static uint32_t seenDm = 0;
  const uint32_t msg = om::meshMsgSerial();
  const uint32_t dm = om::meshDmRecvSerial();
  if (seenMsg == 0) seenMsg = msg;
  if (seenDm == 0) seenDm = dm;
  if (msg != seenMsg) {
    seenMsg = msg;
    const int last = om::meshMsgCount() - 1;
    if (last >= 0 && !om::meshMsgMine(last)) {
      traceEnqueueRingMessage(last);
      if (s_traceSeqStartMs == 0) s_traceSeqStartMs = millis();
    }
  }
  if (dm != seenDm) {
    seenDm = dm;
    int thread = -1;
    char from[32] = "";
    char text[om::kMeshTextCap] = "";
    if (om::meshDmLatest(&thread, from, sizeof(from), text, sizeof(text))) {
      MeshDm m{};
      if (om::meshDmNewest(thread, &m)) {
        traceEnqueue(m.name, m.pathCount > 0 ? m.path : nullptr, m.pathHashSize, m.pathCount,
                     traceKeyForName(m.name));
      } else {
        traceEnqueue(from, nullptr, 1, 0, traceKeyForName(from));
      }
      if (s_traceSeqStartMs == 0) s_traceSeqStartMs = millis();
    }
  }
}
#endif

// The measured cost of the replay that just drained: the ticket's "measure the cost per frame".
// EST-409: the three trace drops are reported here too, by reason, and only once (the counters are
// cleared after the line). A pass with drops but no frames still prints, so a route that never
// reached the queue can never be silent again.
void traceCostLine() {
  if (s_traceFrames == 0 && s_traceDropped == 0) {
    s_traceSeqStartMs = 0;
    return;
  }
  const uint32_t span = s_traceFrames == 0 ? 0 : millis() - s_traceSeqStartMs;
  const unsigned long avg =
      s_traceFrames == 0 ? 0 : (unsigned long)(s_traceFrameUs / s_traceFrames);
  Serial.printf("[map] traces %lu frames in %lu ms, avg %lu us, worst %lu us, in flight %d, "
                "dropped %lu (no point %lu, no self %lu, queue %lu)\n",
                (unsigned long)s_traceFrames, (unsigned long)span, avg,
                (unsigned long)s_traceWorstUs, s_traceQueue.pending(), (unsigned long)s_traceDropped,
                (unsigned long)s_traceDropNoPoint, (unsigned long)s_traceDropNoSelf,
                (unsigned long)s_traceDropQueue);
  s_traceFrames = 0;
  s_traceFrameUs = 0;
  s_traceWorstUs = 0;
  s_traceSeqStartMs = 0;
  s_traceDropped = 0;
  s_traceDropNoPoint = 0;
  s_traceDropNoSelf = 0;
  s_traceDropQueue = 0;
}

}  // namespace

void begin(const Host& host) { s_host = host; }

void beginLayers(bool (*read)(void*, const char*, bool, bool&),
                 bool (*write)(void*, const char*, bool), void* ctx) {
  s_layers.begin(read, write, ctx);
}

void loadLayers() { s_layers.load(); }
bool tracesOn() { return s_layers.traces(); }
bool namesOn() { return s_layers.names(); }
bool tabsHidden() { return s_layers.tabsHidden(); }

void clearSelection() { s_selName[0] = 0; }
// EST-409: internal only (showPoint and the marker tap); it is no longer in the header.
static void select(const char* name) {
  if (name == nullptr) {
    s_selName[0] = 0;
    return;
  }
  snprintf(s_selName, sizeof(s_selName), "%s", name);
}
void setLock(bool on) { s_lock = on; }

// EST-417: an arrow key's pan. It takes the DRAG's own path exactly (the move branch of the shell's
// touch handler): drop the follow lock, close the callout, move the view through om::mapPan(), then
// draw. So a marker, the callout and a running trace re-project for the new view through the same
// code a finger uses; there is no second way to move the map.
void panStep(int dx, int dy) {
  if (dx == 0 && dy == 0) return;
  clearSelection();
  s_lock = false;
  om::mapPan(dx, dy);   // positive dx moves the view east, positive dy south
  draw();
}

// EST-417 (rule 4, guarded by keynav_logic.h's kFnZoomOnMap): one zoom level in or out, the tap
// path's own two steps (mapZoomBy, close the callout, log, draw).
void zoomStep(int delta) {
  if (delta == 0) return;
  om::mapZoomBy(delta);
  clearSelection();
  Serial.printf("[map] zoom %d\n", om::mapZoom());
  draw();
}

void showPoint(double lat, double lon, const char* name) {
  s_lock = false;
  select(name);
  om::mapSetView(lat, lon, om::mapZoom());
}

void enter() {
  s_openFast = om::mapCanvasReady();
  s_refreshPending = false;
}

// EST-374: the Map is being left. The tucked bar is THIS screen's chrome: every other screen draws
// its own full bar from drawTabBar(), whatever the saved tabsHidden key says, so the bar is on the
// next screen by construction. This hook exists so the leaving side has one named place (showTab
// calls it for every route: a pop-up action, a notification, a USB tap, a page switch from code)
// and nothing transient can outlive the screen. The log line is the device-side evidence.
void leave() {
  s_openFast = false;
  Serial.printf("[map] leave: the next screen draws its own full bar (tucked key=%d)\n",
                s_layers.tabsHidden() ? 1 : 0);
}

void draw() {
  const PositionFix& g = fix();
  // Follow mode: a locked map recentres on our fix before the tiles are composed.
  if (s_lock && g.valid) om::mapSetView(g.lat, g.lon, om::mapZoom());
  traceRememberSelf();
  ui::MapView v;
  build(v, true);
  // EST-371: a Map open draws the canvas that is already in PSRAM first. mapScreenAnimate repushes
  // it and redraws all the chrome (status, markers, zN, tabs, callout), so the frame is current
  // except for the tiles; only a changed view needs those decoded, one pass later. The same rule
  // covers a plain in-page redraw (a marker select, the once-a-minute clock): an unchanged view
  // never decodes a tile again.
  const bool cached = om::mapCanvasReady() && !om::mapViewStale();
  if (s_openFast || cached) {
    const bool stale = om::mapViewStale();
    const bool opening = s_openFast;
    const uint32_t t0 = micros();
    ui::mapScreenAnimate(v);
    s_openFast = false;
    if (opening) {
      Serial.printf("[map] open: cached canvas %lu us%s\n", (unsigned long)(micros() - t0),
                    stale ? ", tile refresh queued" : ", view unchanged");
      if (stale) s_refreshPending = true;
    }
    return;
  }
  ui::mapScreenDraw(v);
}

void pump(bool visible, bool uiDrawOk) {
  if (!visible) {
    // Off the Map: drop any route and stop redrawing. The screen change that is leaving draws the
    // next tab itself; drawing the Map here would paint the wrong screen into it.
    if (s_traceQueue.busy() || s_traceFrame.live) traceMapReset();
    s_refreshPending = false;   // EST-371: a queued tile refresh belongs to the Map screen
    return;
  }
  // EST-371: the cached open frame is on the glass. If its tiles no longer match the view (a follow
  // recentre, Show on map, a zoom before the tab changed), decode them now, after the open, not
  // inside the screen change. One pass, once; an unchanged view never gets here.
  if (s_refreshPending && uiDrawOk) {
    s_refreshPending = false;
    const uint32_t t0 = micros();
    draw();   // full frame: mapRender() + chrome
    Serial.printf("[map] refresh: tiles after cached open %lu ms\n",
                  (unsigned long)((micros() - t0) / 1000));
  }
  if (!s_layers.traces()) return;
#if defined(OM_ENABLE_MESH)
  traceWatchArrivals();
#endif
  // EST-394: an animation frame re-pushes the composited canvas. While a pan has moved the view past
  // that canvas it would put the OLD tiles under a trace projected for the NEW one, so the 30 fps
  // replay frames run only over a canvas that matches the current view. The drag's own draw() (at
  // most every ~60 ms) is the frame that shows the trace during a drag; the animation resumes on the
  // next pass after that draw has refreshed the canvas. Nothing is skipped for long: the route's
  // clock keeps running, so it reappears at the right place and the right time.
  const bool freshCanvas = om::mapCanvasReady() && !om::mapViewStale();
  if (!s_traceQueue.busy()) {
    if (s_traceFrame.live) {
      // The route ended while no frame could be drawn (the panel was off): one clean frame now.
      s_traceFrame = ui::TraceFrame();
      if (uiDrawOk && freshCanvas) drawAnimFrame();
    }
    traceCostLine();
    return;
  }
  const uint32_t now = millis();
  if (s_traceNextFrameMs != 0 && (int32_t)(now - s_traceNextFrameMs) < 0) return;
  s_traceNextFrameMs = now + kTraceFrameMs;
  if (s_traceSeqStartMs == 0) s_traceSeqStartMs = now;
  if (!s_traceQueue.tick(now, om::mapProject, &s_traceFrame)) {
    // The last route just finished: one clean frame without the dot, then no more redraws.
    s_traceFrame = ui::TraceFrame();
    if (uiDrawOk && freshCanvas) drawAnimFrame();
    traceCostLine();
    return;
  }
  if (!uiDrawOk || !freshCanvas) return;
  const uint32_t t0 = micros();
  drawAnimFrame();
  const uint32_t took = micros() - t0;
  ++s_traceFrames;
  s_traceFrameUs += took;
  if (took > s_traceWorstUs) s_traceWorstUs = took;
}

// EST-390: the pressed look goes on the rectangle tap() would use. build() and mapScreenChrome() are
// the same two calls tap() makes, so the tint can never sit off the element the tap lands on.
om::ui::PressTarget pressTarget(int x, int y) {
  ui::MapView v;
  build(v, false);   // EST-409: a query: it must not advance the trace clock or touch the selection
  const ui::MapChrome c = ui::mapScreenChrome(v);
  return ui::pressTargetForMap(c, v.markers, v.markerCount, x, y);
}

bool tap(int x, int y) {
  // The chrome's rectangles and the hit test are one function: no number is repeated here.
  ui::MapView v;
  build(v, false);   // EST-409: a query; the actions below draw() their own state changes
  const ui::MapChrome c = ui::mapScreenChrome(v);
  const ui::MapHit hit = ui::mapHitAt(c, v.markers, v.markerCount, x, y);
  switch (hit.kind) {
    case ui::MapHitKind::ZoomIn:
    case ui::MapHitKind::ZoomOut: {
      om::mapZoomBy(hit.kind == ui::MapHitKind::ZoomIn ? 1 : -1);
      clearSelection();
      Serial.printf("[map] zoom %d\n", om::mapZoom());
      draw();
      return true;
    }
    case ui::MapHitKind::Lock: {
      s_lock = !s_lock;
      const PositionFix& g = fix();
      if (s_lock && g.valid) om::mapSetView(g.lat, g.lon, om::mapZoom());
      Serial.printf("[map] follow %s\n", s_lock ? "on" : "off");
      draw();
      return true;
    }
    case ui::MapHitKind::Traces: {
      // EST-368: the toggle, highlighted while on, saved (default on). Turning it off ends the
      // current route and clears the dot; turning it on replays the newest message per sender.
      // EST-409: the return is the NVS write's, so read the state back (as TabsToggle does); the old
      // line treated a healthy write as "on" and re-seeded a replay while turning Traces OFF.
      s_layers.toggleTraces();
      const bool on = s_layers.traces();
      traceMapReset();
      if (on) traceSeedFromRing();
      Serial.printf("[map] traces %s\n", on ? "on" : "off");
      draw();
      return true;
    }
    case ui::MapHitKind::Names: {
      // EST-370: hide or show every marker label; the markers stay. Saved (default on).
      // EST-409: same rule as Traces: the write's result is not the state.
      s_layers.toggleNames();
      const bool on = s_layers.names();
      Serial.printf("[map] names %s\n", on ? "on" : "off");
      draw();
      return true;
    }
    case ui::MapHitKind::TabsToggle: {
      // EST-373/374: the chip (or a tap on a hidden sliver) hides BOTH groups at once, the five tabs
      // to 6 px rounded slivers and the whole bottom page bar to a 3 px strip, and brings them back. One
      // saved key ("tabsHidden") drives both. Saved with the other two switches, so reopening the
      // map keeps them as they were left. The slide is chrome over the cached canvas (slideBars).
      s_layers.toggleTabsHidden();     // the return is the write's, so read the state back below
      const bool hidden = s_layers.tabsHidden();
      Serial.printf("[map] bars %s\n", hidden ? "hidden" : "shown");
      slideBars(hidden);
      return true;
    }
    case ui::MapHitKind::Marker: {
      const ui::MapMarker& m = v.markers[hit.marker];
      if (m.self) {
        toast("Your position");
        return true;
      }
      select(m.name);
      // EST-399: the marker's index, not the node's name (rule 9).
      Serial.printf("[map] select marker %d\n", hit.marker);
      draw();
      return true;
    }
    case ui::MapHitKind::Map:
    case ui::MapHitKind::Callout: {
      // A tap on empty map closes the callout; a drag is still the pan (the shell's loop).
      if (s_selName[0]) {
        clearSelection();
        draw();
      }
      return true;
    }
    default:
      return false;
  }
}

void tracesResetAndSeed() {
  traceMapReset();
  if (s_layers.traces()) traceSeedFromRing();
}

}  // namespace map_tab
}  // namespace om
