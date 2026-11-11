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

namespace om {
namespace map_tab {
namespace {

Host s_host;
bool s_lock = false;              // follow our own position
char s_selName[32] = "";          // the marker whose callout is open ("" = none)
// EST-368: the Map's one 32-row copy of the contact table, shared by build()'s markers and
// traceHopTable()'s path-hash lookup (one 3 KB copy, not two).
MeshContact s_contacts[32];

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

uint8_t traceKeyForName(const char* name) {
  // One byte is enough: it only separates senders for the "newest per sender wins" rule.
  uint32_t h = 2166136261u;
  for (const char* p = (name ? name : ""); *p; ++p) h = (h ^ (uint8_t)*p) * 16777619u;
  return (uint8_t)h;
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
// The repeaters we know with a public key, in the same 32-row view the map's markers use. A hop hash
// is the first 1..3 bytes of the key, so the table carries three and the model compares only the
// packet's own hash size.
int traceHopTable(ui::TracePathHop* table, int cap) {
  MeshContact* list = s_contacts;
  const int n = om::meshContactList(list, 32);
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
                  uint8_t key) {
  ui::TracePathHop table[32];
  const int tn = traceHopTable(table, 32);
  MeshContact sc{};
  const bool senderKnown = (sender != nullptr && sender[0] != 0) &&
                           om::meshContactByName(sender, &sc) && sc.has_pos;
  ui::TraceRoute route;
  ui::traceBuildRoute(path, hashSize, hops, table, tn, senderKnown, sc.lat_e6, sc.lon_e6,
                      s_traceSelfKnown, s_traceSelfLatE6, s_traceSelfLonE6, &route);
  if (route.count == 0) return false;

  ui::TracePt pts[ui::kTraceMaxPoints];
  int n = 0;
  const bool direct = route.hops == 0;
  if (direct) {
    // EST-368 (the ticket): a 0-hop arrival pulses a ring at OUR position, not a line. Without one
    // there is nothing to pulse, and the arrival stays silent on the glass.
    if (!s_traceSelfKnown) return false;
    int x = 0, y = 0;
    if (!om::mapProject((double)s_traceSelfLatE6 / 1e6, (double)s_traceSelfLonE6 / 1e6, x, y)) {
      return false;
    }
    pts[0].x = (int16_t)x;
    pts[0].y = (int16_t)y;
    n = 1;
  } else {
    for (int i = 0; i < route.count; ++i) {
      int x = 0, y = 0;
      if (!om::mapProject((double)route.latE6[i] / 1e6, (double)route.lonE6[i] / 1e6, x, y)) {
        return false;
      }
      pts[n].x = (int16_t)x;
      pts[n].y = (int16_t)y;
      ++n;
    }
  }
  const bool ringOnly = direct || n < 2;
  if (ringOnly) {
    // Fewer than two known points: the line is not drawn, only the ring. It pulses at the one point
    // the route does know (normally our own radio at its end).
    if (n >= 2) pts[0] = pts[n - 1];
    n = 1;
    const bool ok = s_traceQueue.push(pts, 1, true, route.hops, key);
    if (ok) {
      Serial.printf("[map] trace %s %u hop(s), ring only\n", sender ? sender : "?",
                    (unsigned)route.hops);
    }
    return ok;
  }
  const bool ok = s_traceQueue.push(pts, n, false, route.hops, key);
  if (ok) {
    Serial.printf("[map] trace %s %u hop(s), %d known point(s), %d resolved\n", sender ? sender : "?",
                  (unsigned)route.hops, n, route.resolved);
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

void build(ui::MapView& v) {
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
  v.trace = s_traceFrame;

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
  MeshContact* list = s_contacts;
  const int n = om::meshContactList(list, 32);
  const uint32_t nowWall = om::timeKeeper().now();
  const uint32_t nowUp = millis() / 1000;
  const bool nowReal = om::timeKeeper().realSinceBoot();
  for (int i = 0; i < n && v.markerCount < ui::kMapMaxMarkers; ++i) {
    if (!list[i].has_pos) continue;
    int x = 0, y = 0;
    if (!om::mapProject((double)list[i].lat_e6 / 1e6, (double)list[i].lon_e6 / 1e6, x, y)) continue;
    if (x < -4 || x >= ui::kMapCanvasW + 4 || y < ui::kMapCanvasTop - 4 ||
        y >= ui::kMapCanvasTop + ui::kMapCanvasH + 4) {
      continue;
    }
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
  }
#endif
  if (s_selName[0] && !v.callout) s_selName[0] = 0;
}

void drawAnimFrame() {
  const PositionFix& g = fix();
  if (s_lock && g.valid) om::mapSetView(g.lat, g.lon, om::mapZoom());
  ui::MapView v;
  build(v);
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
  build(v);   // the view already carries the NEW state: chip, chevron and dot flip on the first frame
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
void traceCostLine() {
  if (s_traceFrames == 0) {
    s_traceSeqStartMs = 0;
    return;
  }
  const uint32_t span = millis() - s_traceSeqStartMs;
  Serial.printf("[map] traces %lu frames in %lu ms, avg %lu us, worst %lu us, in flight %d\n",
                (unsigned long)s_traceFrames, (unsigned long)span,
                (unsigned long)(s_traceFrameUs / s_traceFrames), (unsigned long)s_traceWorstUs,
                s_traceQueue.pending());
  s_traceFrames = 0;
  s_traceFrameUs = 0;
  s_traceWorstUs = 0;
  s_traceSeqStartMs = 0;
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
void select(const char* name) {
  if (name == nullptr) {
    s_selName[0] = 0;
    return;
  }
  snprintf(s_selName, sizeof(s_selName), "%s", name);
}
void setLock(bool on) { s_lock = on; }
bool locked() { return s_lock; }

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
  build(v);
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
  if (!s_traceQueue.busy()) {
    if (s_traceFrame.live) {
      // The route ended while no frame could be drawn (the panel was off): one clean frame now.
      s_traceFrame = ui::TraceFrame();
      if (uiDrawOk) drawAnimFrame();
    }
    traceCostLine();
    return;
  }
  const uint32_t now = millis();
  if (s_traceNextFrameMs != 0 && (int32_t)(now - s_traceNextFrameMs) < 0) return;
  s_traceNextFrameMs = now + kTraceFrameMs;
  if (s_traceSeqStartMs == 0) s_traceSeqStartMs = now;
  if (!s_traceQueue.tick(now, &s_traceFrame)) {
    // The last route just finished: one clean frame without the dot, then no more redraws.
    s_traceFrame = ui::TraceFrame();
    if (uiDrawOk) drawAnimFrame();
    traceCostLine();
    return;
  }
  if (!uiDrawOk) return;
  const uint32_t t0 = micros();
  drawAnimFrame();
  const uint32_t took = micros() - t0;
  ++s_traceFrames;
  s_traceFrameUs += took;
  if (took > s_traceWorstUs) s_traceWorstUs = took;
}

bool tap(int x, int y) {
  // The chrome's rectangles and the hit test are one function: no number is repeated here.
  ui::MapView v;
  build(v);
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
      const bool on = s_layers.toggleTraces();
      traceMapReset();
      if (on) traceSeedFromRing();
      Serial.printf("[map] traces %s\n", on ? "on" : "off");
      draw();
      return true;
    }
    case ui::MapHitKind::Names: {
      // EST-370: hide or show every marker label; the markers stay. Saved (default on).
      const bool on = s_layers.toggleNames();
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
      Serial.printf("[map] select %s\n", s_selName);
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
