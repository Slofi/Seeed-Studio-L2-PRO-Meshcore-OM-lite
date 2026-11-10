// SPDX-License-Identifier: MIT
// EST-368 host test: the Map tab's Traces, the message pop-up, and the two RPTR fixes.
//
// 1. Hop mapping: a path hash (1, 2 or 3 bytes) finds the repeater whose public key starts with it;
//    an unknown hash, a node without a position and a hop of the wrong size never become a point.
// 2. The route: sender (when positioned) + known hops in order + us, duplicate points dropped; a
//    route with fewer than two known points is the ring case, and a 0-hop arrival is a ring at us.
// 3. The animation: 420 ms per hop, 35 % tail, 900 ms fade, the direct ring's growth, and the
//    one-at-a-time queue (no two routes ever draw at once, a newer route from one sender replaces
//    the older pending one).
// 4. The pop-up: sender/chat title plus the message wrapped over up to four rows with the chat's own
//    wrapper (UTF-8 and emoji kept on whole sequences), an ellipsis only when cut, and a panel that
//    never leaves the screen above the tab bar.
// 5. RPTR: a late read-back never overwrites a field the finger moved, but does update a clean one;
//    the Position page's Share position switch sends the repeater's own `gps advert none` (or lat/lon
//    0), then one `advert`.
#include <stdio.h>
#include <string.h>

#include "rptr_model.h"
#include "rptr_page.h"
#include "ui/map_trace.h"
#include "ui/notice_panel.h"
#include "ui/notify.h"
#include "ui/tab_bar.h"
#include "ui/text_wrap.h"
#include "ui/theme.h"

using namespace om::ui;
using namespace om::rptr;

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// --------------------------------------------------------------------------------- 1. hop mapping
static void testHopMapping() {
  TracePathHop table[3];
  table[0].key[0] = 0x11; table[0].key[1] = 0x22; table[0].key[2] = 0x33;
  table[0].keyLen = 3;
  table[0].hasPos = true;
  table[0].latE6 = 46100000; table[0].lonE6 = 14500000;
  table[1].key[0] = 0xAA; table[1].key[1] = 0xBB; table[1].key[2] = 0xCC;
  table[1].keyLen = 3;
  table[1].hasPos = true;
  table[1].latE6 = 46200000; table[1].lonE6 = 14600000;
  // A known repeater that never advertised a position.
  table[2].key[0] = 0x77; table[2].key[1] = 0x88; table[2].key[2] = 0x99;
  table[2].keyLen = 3;
  table[2].hasPos = false;

  // 2B is this mesh's size; 1B and 3B must work too (the ticket names all three).
  const uint8_t h2a[] = {0x11, 0x22};
  const uint8_t h1b[] = {0xAA};
  const uint8_t h3[] = {0x11, 0x22, 0x33};
  const uint8_t h3bad[] = {0x11, 0x22, 0x34};   // two bytes match, the third does not
  const uint8_t h1unknown[] = {0x55};
  CHECK(traceMatchHop(h2a, 2, table, 3) == 0);
  CHECK(traceMatchHop(h1b, 1, table, 3) == 1);
  CHECK(traceMatchHop(h3, 3, table, 3) == 0);
  CHECK(traceMatchHop(h3bad, 3, table, 3) == -1);
  CHECK(traceMatchHop(h1unknown, 1, table, 3) == -1);
  CHECK(traceMatchHop(nullptr, 2, table, 3) == -1);
  CHECK(traceMatchHop(h2a, 4, table, 3) == -1);   // a hash is 1..3 bytes

  // A record that only carries one byte can never answer a two-byte hash.
  TracePathHop one;
  one.key[0] = 0x11;
  one.keyLen = 1;
  CHECK(traceMatchHop(h2a, 2, &one, 1) == -1);
  CHECK(traceMatchHop(h2a, 1, &one, 1) == 0);
}

// ------------------------------------------------------------------------------- 2. the route
static void testRoute() {
  TracePathHop table[3];
  table[0].key[0] = 0x11; table[0].key[1] = 0x22; table[0].key[2] = 0x33;
  table[0].keyLen = 3; table[0].hasPos = true;
  table[0].latE6 = 46100000; table[0].lonE6 = 14500000;
  table[1].key[0] = 0xAA; table[1].key[1] = 0xBB; table[1].key[2] = 0xCC;
  table[1].keyLen = 3; table[1].hasPos = true;
  table[1].latE6 = 46200000; table[1].lonE6 = 14600000;
  table[2].key[0] = 0x77; table[2].key[1] = 0x88; table[2].key[2] = 0x99;
  table[2].keyLen = 3; table[2].hasPos = false;

  const uint8_t two[] = {0x11, 0x22, 0xAA, 0xBB};           // R1 then R2
  const uint8_t skip[] = {0x11, 0x22, 0x55, 0x55, 0xAA, 0xBB};  // R1, unknown, R2
  const uint8_t nopos[] = {0x77, 0x88};                     // known but no position

  TraceRoute r;
  // The full route: sender, two hops, us.
  traceBuildRoute(two, 2, 2, table, 3, true, 46000000, 14400000, true, 46300000, 14700000, &r);
  CHECK(r.count == 4);
  CHECK(r.hops == 2);
  CHECK(!r.direct);
  CHECK(r.resolved == 2);
  CHECK(r.latE6[0] == 46000000 && r.latE6[1] == 46100000 && r.latE6[2] == 46200000 &&
        r.latE6[3] == 46300000);

  // An unknown middle hop is skipped: the line jumps R1 -> R2, and the hop count is still 3.
  traceBuildRoute(skip, 2, 3, table, 3, false, 0, 0, true, 46300000, 14700000, &r);
  CHECK(r.count == 3);
  CHECK(r.hops == 3);
  CHECK(r.resolved == 2);

  // A hop whose node has no position is skipped too.
  traceBuildRoute(nopos, 2, 1, table, 3, false, 0, 0, true, 46300000, 14700000, &r);
  CHECK(r.count == 1);      // us only: the less-than-two-points case (a ring)
  CHECK(r.resolved == 0);

  // 0 hops: direct=true, and the sender and us are both endpoints of the raw route. The Map draws a
  // ring for this case (the ticket), not a line, and never needs these two points.
  traceBuildRoute(nullptr, 2, 0, table, 3, true, 46000000, 14400000, true, 46300000, 14700000, &r);
  CHECK(r.direct);
  CHECK(r.count == 2);

  // No sender position, no own position: nothing at all.
  traceBuildRoute(two, 2, 2, table, 3, false, 0, 0, false, 0, 0, &r);
  CHECK(r.count == 2 && r.direct == false);   // the two repeaters still make a route

  // A sender that sits exactly on the first hop is one point, not a zero-length segment.
  traceBuildRoute(two, 2, 2, table, 3, true, 46100000, 14500000, true, 46300000, 14700000, &r);
  CHECK(r.count == 3);
  CHECK(r.latE6[0] == 46100000 && r.latE6[1] == 46200000);

  // Three-byte and one-byte paths both resolve through the same table.
  const uint8_t three[] = {0x11, 0x22, 0x33, 0xAA, 0xBB, 0xCC};
  traceBuildRoute(three, 3, 2, table, 3, false, 0, 0, true, 46300000, 14700000, &r);
  CHECK(r.count == 3 && r.resolved == 2);
  const uint8_t oneB[] = {0x77};
  traceBuildRoute(oneB, 1, 1, table, 3, false, 0, 0, true, 46300000, 14700000, &r);
  CHECK(r.count == 1 && r.resolved == 0);   // matched, but no position: skipped
}

// -------------------------------------------------------------------------- 3. the animation
static void testGeometry() {
  const TracePt pts[3] = {{0, 0}, {100, 0}, {100, 100}};
  TraceGeom g;
  traceGeom(pts, 3, &g);
  CHECK(g.count == 3);
  CHECK(g.total == 200);
  CHECK(g.len[0] == 100 && g.len[1] == 100);
  CHECK(g.start[0] == 0 && g.start[1] == 100);

  TracePt p;
  CHECK(tracePointAt(g, pts, 0.0f, &p) && p.x == 0 && p.y == 0);
  CHECK(tracePointAt(g, pts, 0.5f, &p) && p.x == 100 && p.y == 0);
  CHECK(tracePointAt(g, pts, 0.75f, &p) && p.x == 100 && p.y == 50);
  CHECK(tracePointAt(g, pts, 1.0f, &p) && p.x == 100 && p.y == 100);

  // A slice that crosses the vertex carries it.
  TracePt out[6];
  const int n = traceSlice(g, pts, 0.25f, 0.75f, out, 6);
  CHECK(n >= 3);
  CHECK(out[0].x == 50 && out[0].y == 0);
  CHECK(out[n - 1].x == 100 && out[n - 1].y == 50);
  bool vertex = false;
  for (int i = 0; i < n; ++i) {
    if (out[i].x == 100 && out[i].y == 0) vertex = true;
  }
  CHECK(vertex);

  // A degenerate route (all points equal) is not a divide by zero: total 0, the dot stays put.
  const TracePt same[2] = {{50, 50}, {50, 50}};
  TraceGeom z;
  traceGeom(same, 2, &z);
  CHECK(z.total == 0);
  CHECK(tracePointAt(z, same, 0.7f, &p) && p.x == 50 && p.y == 50);
}

static void testAnimation() {
  // A 2-hop route: three points, 840 ms of travel (2 x 420), then 900 ms of fade.
  TraceAnim a;
  const TracePt pts[3] = {{0, 0}, {100, 0}, {100, 100}};
  a.begin(pts, 3, false, 2, 1000);
  CHECK(a.active());
  CHECK(a.durationMs() == 2 * kTraceHopMs);
  TraceFrame f;

  CHECK(a.sample(1000, &f));
  CHECK(f.live && !f.direct);
  CHECK(f.hasDot && f.dot.x == 0 && f.dot.y == 0);
  CHECK(f.tailCount >= 2);
  CHECK(f.routeCount == 3);

  CHECK(a.sample(1000 + kTraceHopMs, &f));   // one hop later: the middle vertex
  CHECK(f.dot.x == 100 && f.dot.y == 0);
  // The tail is 35 % of the route behind the dot: head .5, from .15 -> 30 px, not from 0.
  CHECK(f.tailCount >= 2);
  CHECK(f.tail[0].x == 30 && f.tail[0].y == 0);

  CHECK(a.sample(1000 + 2 * kTraceHopMs - 1, &f));   // just before the arrival
  CHECK(f.dot.y > 90);

  // Arrived: the tail holds the last 35 % and fades with the dot over 900 ms.
  CHECK(a.sample(1000 + 2 * kTraceHopMs + kTraceFadeMs / 2, &f));
  CHECK(f.live);
  CHECK(f.hasDot);    // the dot is still drawn during the fade, only dimmer
  CHECK(f.dotAlpha > 0 && f.tailAlpha > 0);
  const uint8_t midDot = f.dotAlpha;
  CHECK(a.sample(1000 + 2 * kTraceHopMs + 3 * kTraceFadeMs / 4, &f));
  CHECK(f.dotAlpha < midDot);

  CHECK(!a.sample(1000 + 2 * kTraceHopMs + kTraceFadeMs, &f));
  CHECK(!a.active());

  // The hop count, not the number of drawn points, sets the duration: a 3-hop path with one unknown
  // hop still takes 1260 ms.
  TraceAnim b;
  b.begin(pts, 3, false, 3, 0);
  CHECK(b.durationMs() == 3 * kTraceHopMs);

  // A direct arrival: a ring at our radio that grows and fades over 1400 ms.
  TraceAnim d;
  const TracePt us[1] = {{160, 107}};
  d.begin(us, 1, true, 0, 500);
  CHECK(d.durationMs() == kTraceDirectMs);
  CHECK(d.sample(500, &f));
  CHECK(f.direct && f.live);
  const int16_t r0 = f.ringR;
  const uint8_t a0 = f.ringAlpha;
  CHECK(f.ringX == 160 && f.ringY == 107);
  CHECK(d.sample(500 + kTraceDirectMs / 2, &f));
  CHECK(f.ringR > r0);
  CHECK(f.ringAlpha < a0);
  CHECK(!d.sample(500 + kTraceDirectMs, &f));
}

static void testQueue() {
  TraceQueue q;
  q.reset();
  const TracePt a[3] = {{0, 0}, {50, 0}, {100, 0}};
  const TracePt b[2] = {{0, 100}, {100, 100}};
  const TracePt c[1] = {{160, 100}};
  CHECK(q.push(a, 3, false, 2, 1));
  CHECK(q.push(b, 2, false, 1, 2));
  CHECK(q.push(c, 1, true, 0, 3));
  CHECK(q.pending() == 3);
  CHECK(q.busy());

  TraceFrame f;
  CHECK(q.tick(0, &f));
  CHECK(f.live && !f.direct);
  CHECK(f.dot.x == 0 && f.dot.y == 0);   // the FIRST route is the one playing
  CHECK(q.pending() == 2);

  // Halfway through the first route, still only that route is on the glass.
  CHECK(q.tick(kTraceHopMs, &f));
  CHECK(f.routeCount == 3);
  CHECK(q.pending() == 2);

  // The first route ends after its travel + fade; the next one starts in the same tick at that
  // moment, never on top of it.
  const uint32_t done = 2 * kTraceHopMs + kTraceFadeMs;
  CHECK(q.tick(done, &f));
  CHECK(f.live && !f.direct);
  CHECK(f.routeCount == 2);              // route b: no two routes at once
  CHECK(f.dot.x == 0 && f.dot.y == 100);
  CHECK(q.pending() == 1);

  // The direct route waits its turn too.
  const uint32_t done2 = done + kTraceHopMs + kTraceFadeMs;
  CHECK(q.tick(done2, &f));
  CHECK(f.live && f.direct);
  CHECK(f.ringX == 160 && f.ringY == 100);
  CHECK(q.pending() == 0);

  CHECK(!q.tick(done2 + kTraceDirectMs, &f));
  CHECK(!q.busy());

  // A newer route from the SAME sender replaces the pending one: three messages from one node can
  // never draw three tails on one corridor.
  TraceQueue q2;
  CHECK(q2.push(a, 3, false, 2, 7));
  CHECK(q2.push(b, 2, false, 1, 7));
  CHECK(q2.pending() == 1);
  CHECK(q2.tick(0, &f));
  CHECK(f.routeCount == 2);              // the newest one, not both
  // A route with no drawable point is refused.
  TraceQueue q3;
  CHECK(!q3.push(nullptr, 2, false, 1, 0));
  CHECK(!q3.push(a, 0, false, 1, 0));
  CHECK(!q3.busy());
}

// ------------------------------------------------------------------------------ 4. the pop-up
static int fixedMeasure(void* ctx, const char* text, int len) {
  (void)ctx;
  (void)text;
  return len * 6;
}

static bool validUtf8(const char* s, int len) {
  int i = 0;
  while (i < len) {
    uint32_t cp = utf8Decode(s, &i);
    if (cp == kUtf8Invalid) return false;
    if (i > len) return false;
  }
  return i == len;
}

static void testNotice() {
  // A short message: one row, a panel that grows no further than it needs to.
  {
    const NoticeLayout lo = noticeLayout("Hi there", fixedMeasure, nullptr, 320, 215, 26);
    CHECK(lo.lineCount == 1);
    CHECK(!lo.truncated);
    CHECK(lo.lastLen == (int)strlen("Hi there"));
    CHECK(lo.boxW == 304);                      // 320 less the two 8 px margins
    CHECK(lo.boxH == 2 * kNoticePad + kNoticeTitleH + kNoticeLineH);
    CHECK(lo.boxY + lo.boxH == 215 - kToastMargin);   // sits above the tab bar
  }
  // Four rows is the cap; a longer text ends in "..." on a whole character that still fits.
  {
    char body[kNoticeBodyCap];
    memset(body, 'x', sizeof(body) - 1);
    body[sizeof(body) - 1] = 0;
    const NoticeLayout lo = noticeLayout(body, fixedMeasure, nullptr, 320, 215, 26);
    CHECK(lo.lineCount == kNoticeMaxLines);
    CHECK(lo.truncated);
    const int textW = 304 - 2 * kNoticePad;
    CHECK(fixedMeasure(nullptr, body + lo.lines[lo.lineCount - 1].start, lo.lastLen) +
              fixedMeasure(nullptr, "...", 3) <= textW);
    CHECK(lo.lastLen > 0);
    CHECK(lo.boxY + lo.boxH == 215 - kToastMargin);
    CHECK(lo.boxY >= 26 + kToastMargin);
  }
  // UTF-8 and an emoji: every wrapped line is a whole sequence (no split š/č/ž and no half emoji),
  // and the title row is not counted as body text.
  {
    const char* text =
        "Ščž: ěščřžýáíé 😀😀😀 and a long tail so that this message really wraps over several rows "
        "of the panel, exactly as a real one does on the glass";
    const NoticeLayout lo = noticeLayout(text, fixedMeasure, nullptr, 320, 215, 26);
    CHECK(lo.lineCount >= 2);
    const int total = (int)strlen(text);
    for (int i = 0; i < lo.lineCount; ++i) {
      const int len = (i == lo.lineCount - 1) ? lo.lastLen : lo.lines[i].len;
      CHECK(validUtf8(text + lo.lines[i].start, len));
      CHECK(fixedMeasure(nullptr, text + lo.lines[i].start, len) <= 304 - 2 * kNoticePad);
    }
    CHECK(lo.truncated == (wrapConsumed(text, lo.lines, lo.lineCount) < total));
  }
  // A shorter screen loses BODY rows before the title, and the panel still ends above the tab bar.
  {
    char body[kNoticeBodyCap];
    memset(body, 'y', sizeof(body) - 1);
    body[sizeof(body) - 1] = 0;
    const NoticeLayout lo = noticeLayout(body, fixedMeasure, nullptr, 320, 120, 26);
    CHECK(lo.lineCount == 3);
    CHECK(lo.truncated);
    CHECK(lo.boxY + lo.boxH <= 120 - kToastMargin);
    CHECK(lo.boxY >= 26 + kToastMargin);
  }
  // Title and body: the builder keeps the whole message and never splits a UTF-8 sequence at the cap.
  {
    Notice n;
    const int t = noticeChannelNotice(&n, "TehiLL RAK", "#slovenija", "živjo, kako si? 😀", false);
    CHECK(t > 0);
    CHECK(strcmp(n.title, "TehiLL RAK \xC2\xB7 #slovenija") == 0);
    CHECK(strcmp(n.body, "živjo, kako si? 😀") == 0);
    noticeChannelNotice(&n, "A", "#b", "x", true);
    CHECK(strncmp(n.title, "Mention ", 8) == 0);
    noticeDmNotice(&n, "ERA-3", "direct hello");
    CHECK(strcmp(n.title, "DM \xC2\xB7 ERA-3") == 0);
    CHECK(strcmp(n.body, "direct hello") == 0);

    char longText[kNoticeBodyCap + 40];
    memset(longText, 'z', sizeof(longText) - 1);
    longText[sizeof(longText) - 1] = 0;
    memcpy(longText, "ščž", 6);   // 3 two-byte letters at the very start
    noticeDmNotice(&n, "X", longText);
    CHECK((int)strlen(n.body) < (int)sizeof(n.body));
    CHECK(validUtf8(n.body, (int)strlen(n.body)));
  }
}

// ------------------------------------------------------------------------------- 5. the RPTR fixes
static void testLateAnswerKeepsDraft() {
  Facts f;
  f.advertLocal = 120;
  f.advertLocalKnown = true;
  RegionList regions;
  Draft d;
  d.reset(f, regions, "*", nullptr);

  // The page's opening read-back reports 60 while the finger has already picked 30.
  d.v.advertLocal = 30;
  f.advertLocal = 60;
  d.syncClean(f, regions, "*", nullptr);
  CHECK(d.v.advertLocal == 30);                    // the pick survives the late answer
  DirtyFields ch = draftChanges(f, d, regions, "*", nullptr);
  CHECK(ch.advertLocal);
  CHECK(ch.any);

  // A field the finger did NOT touch does take the answer (that is what a read-back is for).
  f.advertFlood = 7;
  d.syncClean(f, regions, "*", nullptr);
  CHECK(d.v.advertFlood == 7);
  ch = draftChanges(f, d, regions, "*", nullptr);
  CHECK(!ch.advertFlood);

  // The page shows Apply for the dirty field after the answer (buildScreen's own rule).
  ScreenIn in;
  in.page = Page::Advertising;
  in.loggedIn = true;
  in.facts = &f;
  in.draft = &d;
  ScreenOut out;
  const int n = buildScreen(in, &out);
  CHECK(n == 2);
  CHECK(out.hasApply);
  CHECK(out.rows[0].value && strcmp(out.rows[0].value, "30 min") == 0);
  CHECK(out.rows[0].changed);
  CHECK(!out.rows[1].changed);

  // The same rule on the radio's steppers: the first step is not undone by `get radio`.
  Facts rf;
  rf.radioKnown = true;
  rf.radio = RadioValues();
  Draft rd;
  rd.reset(rf, regions, "*", nullptr);
  radioStep(&rd.v.radio, RadioKey::Freq, +1);
  const uint32_t picked = rd.v.radio.freq_khz;
  CHECK(picked != rf.radio.freq_khz);
  rf.radio.freq_khz = 869550;   // a late `get radio` with a different value
  rd.syncClean(rf, regions, "*", nullptr);
  CHECK(rd.v.radio.freq_khz == picked);
}

static void testSharePosition() {
  Facts f;
  f.posKnown = true;
  f.latE6 = 46100000;
  f.lonE6 = 14500000;
  f.posPolicy = kAdvertLocPrefs;
  f.posPolicyKnown = true;
  RegionList regions;
  Draft d;
  d.reset(f, regions, "*", nullptr);
  CHECK(d.sharePos);
  CHECK(d.policyKnown);
  DirtyFields ch = draftChanges(f, d, regions, "*", nullptr);
  CHECK(!ch.sharePos && !ch.any);

  // Off with a repeater that knows `gps advert`: its own command, then ONE advert.
  d.sharePos = false;
  ch = draftChanges(f, d, regions, "*", nullptr);
  CHECK(ch.sharePos && ch.any);
  ApplyPlan plan;
  int n = applyPlan(Page::Position, d, regions, &plan);
  CHECK(n == 2);
  CHECK(strcmp(plan.line[0], "gps advert none") == 0);
  CHECK(strcmp(plan.line[1], "advert") == 0);

  // The page shows the switch and the note, and Apply while it differs.
  ScreenIn in;
  in.page = Page::Position;
  in.loggedIn = true;
  in.facts = &f;
  in.draft = &d;
  ScreenOut out;
  const int rows = buildScreen(in, &out);
  CHECK(rows == 5);   // Pick on map, Latitude, Longitude, Share position, Note
  CHECK(out.hasApply);
  CHECK(strcmp(out.rows[3].label, "Share position") == 0);
  CHECK(strcmp(out.rows[3].value, "off") == 0);
  CHECK(!out.rows[3].on);
  CHECK(out.rows[3].changed);
  // The two halves of the sentence: the banner says when others drop it, the note says what a node
  // that stored the point may still do.
  CHECK(strstr(out.banner, "when they hear the next advert") != nullptr);
  CHECK(strcmp(out.rows[4].label, "Note") == 0);
  CHECK(strstr(out.rows[4].value, "may keep showing") != nullptr);
  CHECK(out.rows[4].act == Act::None);

  // A repeater whose firmware has no `gps advert`: lat/lon 0 (MeshCore's "no position") + advert.
  Facts old;
  old.posPolicyKnown = false;
  Draft od;
  od.reset(old, regions, "*", nullptr);
  CHECK(!od.policyKnown);
  od.sharePos = false;
  n = applyPlan(Page::Position, od, regions, &plan);
  CHECK(n == 3);
  CHECK(strcmp(plan.line[0], "set lat 0.00000") == 0);
  CHECK(strcmp(plan.line[1], "set lon 0.00000") == 0);
  CHECK(strcmp(plan.line[2], "advert") == 0);

  // Back on: the policy read-back, then the coordinates as before.
  d.sharePos = true;
  n = applyPlan(Page::Position, d, regions, &plan);
  CHECK(n == 3);
  CHECK(strcmp(plan.line[0], "gps advert prefs") == 0);
  CHECK(strcmp(plan.line[1], "set lat 46.10000") == 0);
  CHECK(strcmp(plan.line[2], "set lon 14.50000") == 0);

  // The `gps advert` answer parser and the reply shape check.
  uint8_t policy = 255;
  CHECK(gpsAdvertParse("> none", &policy) && policy == kAdvertLocNone);
  CHECK(gpsAdvertParse("> prefs", &policy) && policy == kAdvertLocPrefs);
  CHECK(gpsAdvertParse("> share", &policy) && policy == kAdvertLocShare);
  CHECK(!gpsAdvertParse("unknown command", &policy));
  CHECK(!gpsAdvertParse("Err - unsupported", &policy));
  CHECK(replyFitsCmd("gps advert", "> none"));
  CHECK(!replyFitsCmd("gps advert", "unknown command"));

  // The Position page asks for it among its opening read-backs.
  char reads[kReadBackMax][24];
  const int rn = readBackLines(Page::Position, reads);
  bool asked = false;
  for (int i = 0; i < rn; ++i) {
    if (strcmp(reads[i], "gps advert") == 0) asked = true;
  }
  CHECK(rn == 3);
  CHECK(asked);

  // A clean switch follows a late policy answer; a flipped one does not.
  Facts later = f;
  later.posPolicy = kAdvertLocNone;
  Draft clean;
  clean.reset(f, regions, "*", nullptr);
  clean.syncClean(later, regions, "*", nullptr);
  CHECK(!clean.sharePos);
  CHECK(!draftChanges(later, clean, regions, "*", nullptr).sharePos);
  Draft moved;
  moved.reset(f, regions, "*", nullptr);
  moved.sharePos = false;   // the finger turned it off
  later.posPolicy = kAdvertLocPrefs;
  moved.syncClean(later, regions, "*", nullptr);
  CHECK(!moved.sharePos);
}

// ------------------------------------------------------------------------------- the tab bar badge
static void testMapBadge() {
  // EST-368 rule 4: the Map tab's bar comes from the ONE model, so the unread count is there.
  const TabBarModel m = tabBarModel(Tab::Map, 7, true);
  CHECK(m.active == Tab::Map);
  CHECK(m.unread == 7);
  CHECK(m.newNodeDot);
  CHECK(tabBarModel(Tab::Map, -3, false).unread == 0);
}

int main() {
  testHopMapping();
  testRoute();
  testGeometry();
  testAnimation();
  testQueue();
  testNotice();
  testLateAnswerKeepsDraft();
  testSharePosition();
  testMapBadge();
  if (g_fail) {
    printf("est368: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("est368: hop mapping, route, traces timing/queue, message pop-up wrap/height, RPTR clean-draft "
         "and Share position ok\n");
  return 0;
}
