// SPDX-License-Identifier: MIT
// EST-366 host checks: the nine review items that are logic, not pixels.
//
//   1. CR is part of the radio read-back and the Radio page's fourth field (5..8, "CR8").
//   2. The one scrolling column: the card is CONTENT (it starts at the body top and moves by scroll),
//      only the header is fixed.
//   3. The region list is parsed completely (multi-line, CRLF, the wildcard-only answer) and a
//      shorter answer can never leave a removed region behind.
//   4. Apply exists only while draft != read-back; each changed row carries the dot; a matching
//      read-back (d.reset) hides it; a failed apply keeps it.
//   5. `neighbors`: "-none-" is a real empty answer, an error is not, the empty page is explained.
//   6. The map picker's layout and hit test (crosshair at the view centre, Apply only when changed).
//   7. PageNav pops exactly one level for every sub-page and restores the scroll.
//   8. The home picker offers "* (anywhere)" and the default picker offers "none".
//   9. The notification rules: DND, the open-chat rule, the newest-wins queue and the text forms.
#include <stdio.h>
#include <string.h>

#include "rptr_model.h"
#include "rptr_page.h"
#include "ui/notify.h"
#include "ui/rptr_layout.h"

using namespace om;
using namespace om::rptr;

static int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                 \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

static bool contains(const char* haystack, const char* needle) {
  if (haystack == nullptr || needle == nullptr) return false;
  return strstr(haystack, needle) != nullptr;
}

static const UiRow* findRow(const ScreenOut& s, const char* label) {
  for (int i = 0; i < s.count; ++i) {
    if (strcmp(s.rows[i].label, label) == 0) return &s.rows[i];
  }
  return nullptr;
}

static ScreenOut screenFor(Page page, const Facts& f, const Draft& d, const RegionList& regions,
                           const char* home, const char* def) {
  ScreenIn in;
  in.page = page;
  in.loggedIn = true;
  in.facts = &f;
  in.draft = &d;
  in.regions = &regions;
  in.home = home;
  in.defaultScope = def;
  ScreenOut out;
  buildScreen(in, &out);
  return out;
}

// --- 1: coding rate ----------------------------------------------------------------------------
static void testCodingRate() {
  RadioValues rv;
  CHECK(radioReplyParse("> 869.6179809,62.5,8,8", &rv));
  CHECK(rv.freq_khz == 869618);
  CHECK(rv.bw_hz == 62500);
  CHECK(rv.sf == 8);
  CHECK(rv.cr == 8);

  RadioValues step;
  step.cr = 8;
  radioStep(&step, RadioKey::Cr, 1);
  CHECK(step.cr == 8);   // 5..8, never 9
  step.cr = 5;
  radioStep(&step, RadioKey::Cr, -1);
  CHECK(step.cr == 5);
  radioStep(&step, RadioKey::Cr, 1);
  CHECK(step.cr == 6);

  Facts f;
  f.radio = rv;
  f.radioKnown = true;
  f.tx = 14;
  f.txKnown = true;
  Draft d;
  RegionList regions;
  d.reset(f, regions, "*", "");
  // The main page's Radio row names all four CLI fields.
  const ScreenOut rep = screenFor(Page::Repeater, f, d, regions, "*", "");
  const UiRow* radio = findRow(rep, "Radio");
  CHECK(radio != nullptr);
  CHECK(contains(radio->value, "CR8"));

  // The Radio page shows the same CR and its Apply appears when CR changes.
  d.v.radio.cr = 7;
  const ScreenOut page = screenFor(Page::Radio, f, d, regions, "*", "");
  const UiRow* cr = findRow(page, "Coding rate");
  CHECK(cr != nullptr);
  CHECK(strcmp(cr->value, "CR7") == 0);
  CHECK(cr->changed);
  CHECK(page.hasApply);
}

// --- 2: the scrolling card ---------------------------------------------------------------------
static void testScrollingCard() {
  const om::ui::Layout& L = om::ui::l2();
  const om::ui::RptrPageLayout a = om::ui::rptrPageLayoutFor(L, true, true, true, false, false);
  CHECK(a.card.y == a.body.y);                 // the card is the FIRST content item
  CHECK(a.rowsY > a.card.bottom());            // rows follow the card and banner
  CHECK(a.banner.y >= a.card.bottom());
  CHECK(a.banner.bottom() <= a.rowsY);
  // The refresh target scrolls with the card: at scroll 0 it is tappable, after the card height it
  // is out of the viewport and no longer answers.
  om::rptr::ScreenOut s;
  s.cellCount = 1;
  CHECK(om::ui::rptrPageHitAt(a, s, 0, a.refreshTap.x + 2, a.refreshTap.y + 2).kind ==
        om::ui::RptrHit::Refresh);
  const int gone = a.card.h + 8;
  CHECK(om::ui::rptrPageHitAt(a, s, gone, a.refreshTap.x + 2, a.refreshTap.y + 2).kind !=
        om::ui::RptrHit::Refresh);
  // Max scroll counts the card and banner above row 0.
  const int content = (a.rowsY - a.body.y) + 3 * om::ui::kRptrPageRowH;
  CHECK(om::ui::rptrPageMaxScroll(a, 3) == (content > a.body.h ? content - a.body.h : 0));
}

// --- 3: regions ---------------------------------------------------------------------------------
static void testRegions() {
  RegionList l;
  CHECK(regionsParse("*^ F\n lj F\n si\n", &l) == 3);
  CHECK(l.valid && l.count == 3);
  CHECK(strcmp(l.rows[0].name, "*") == 0 && l.rows[0].wildcard && l.rows[0].home &&
        l.rows[0].allow);
  CHECK(strcmp(l.rows[1].name, "lj") == 0 && l.rows[1].allow && !l.rows[1].home);
  CHECK(strcmp(l.rows[2].name, "si") == 0 && !l.rows[2].allow);

  // CRLF is formatting, not a name and not a denied flood flag.
  CHECK(regionsParse("*^ F\r\n lj F\r\n", &l) == 2);
  CHECK(strcmp(l.rows[1].name, "lj") == 0 && l.rows[1].allow);

  // Filip's 21:01 answer: the wildcard only. `lj` must be GONE from the previous list.
  CHECK(regionsParse("*^ F\n lj F\n", &l) == 2);
  CHECK(regionsParse("*^ F\n", &l) == 1);
  CHECK(l.count == 1 && strcmp(l.rows[0].name, "*") == 0);
  for (int i = 1; i < kMaxRegions; ++i) CHECK(l.rows[i].name[0] == 0);
  // "si" is not in that answer either: it was removed with lj.
  bool hasSi = false;
  for (int i = 0; i < l.count; ++i) {
    if (strcmp(l.rows[i].name, "si") == 0) hasSi = true;
  }
  CHECK(!hasSi);

  // An error is not a region called "Err".
  CHECK(regionsParse("Err - unknown region", &l) == 0);
  CHECK(!l.valid);
  CHECK(regionsParse("Error, bad chars", &l) == 0);
  CHECK(!l.valid);

  // A logged-in page takes its list only from the CLI answer, never from the anonymous cache.
  Facts f;
  Draft d;
  RegionList empty;
  empty.valid = true;
  d.reset(f, empty, "*", "");
  ScreenIn in;
  in.page = Page::Repeater;
  in.loggedIn = true;
  in.facts = &f;
  in.draft = &d;
  in.regions = &empty;
  in.home = "*";
  in.defaultScope = "";
  AnonFacts anon;
  anon.valid = true;
  anon.regionCount = 3;
  snprintf(anon.regions, sizeof(anon.regions), "*, si, lj");
  in.anon = &anon;
  ScreenOut s;
  buildScreen(in, &s);
  const UiRow* regs = findRow(s, "Regions");
  CHECK(regs != nullptr);
  CHECK(!contains(regs->value, "lj"));
  CHECK(!contains(regs->value, "si"));
}

// --- 4: Apply only when changed -----------------------------------------------------------------
static Facts baseFacts() {
  Facts f;
  f.name[0] = 0;
  f.latE6 = 46042630;
  f.lonE6 = 14501450;
  f.posKnown = true;
  f.radioKnown = true;
  f.tx = 14;
  f.txKnown = true;
  f.repeat = true;
  f.repeatKnown = true;
  f.advertLocal = 120;
  f.advertFlood = 13;
  f.floodMax = 8;
  f.loop = LoopDetect::Moderate;
  f.pathHash = 1;
  return f;
}

static void testApplyOnlyWhenChanged() {
  const Facts f = baseFacts();
  RegionList regions;
  CHECK(regionsParse("*^ F\n lj F\n", &regions) == 2);
  Draft d;
  d.reset(f, regions, "*", "");

  struct Case {
    Page page;
    void (*change)(Draft&);
  };
  const Case cases[] = {
      {Page::Name, [](Draft& x) { snprintf(x.name, sizeof(x.name), "new-name"); }},
      {Page::Position, [](Draft& x) { x.latE6 += 1000; }},
      {Page::Radio, [](Draft& x) { x.v.radio.cr = 8; }},
      {Page::Radio, [](Draft& x) { x.v.radio.freq_khz += 25; }},
      {Page::Radio, [](Draft& x) { x.v.radio.bw_hz = 125000; }},
      {Page::Radio, [](Draft& x) { x.v.radio.sf = 9; }},
      {Page::Tx, [](Draft& x) { x.v.radio.tx = 20; }},
      {Page::Repeat, [](Draft& x) { x.v.repeat = false; }},
      {Page::Regions, [](Draft& x) { x.floodChanged[1] = true; }},
      {Page::Regions, [](Draft& x) { x.homeChanged = true; x.home[0] = 0; }},
      {Page::AddRegion, [](Draft& x) { x.hasAdd = true; snprintf(x.addName, sizeof(x.addName), "xy"); }},
      {Page::HomePicker, [](Draft& x) { snprintf(x.home, sizeof(x.home), "lj"); }},
      {Page::DefaultPicker, [](Draft& x) { x.defaultNone = false; snprintf(x.defaultScope, sizeof(x.defaultScope), "lj"); }},
      {Page::Advertising, [](Draft& x) { x.v.advertLocal = 60; }},
      {Page::Advertising, [](Draft& x) { x.v.advertFlood = 4; }},
      {Page::More, [](Draft& x) { x.v.floodMax = 12; }},
      {Page::More, [](Draft& x) { x.v.loop = LoopDetect::Strict; }},
      {Page::More, [](Draft& x) { x.v.pathHash = 2; }},
  };
  for (const Case& c : cases) {
    Draft d2;
    d2.reset(f, regions, "*", "");
    const ScreenOut clean = screenFor(c.page, f, d2, regions, "*", "");
    CHECK(!clean.hasApply);   // nothing changed yet: no Apply
    c.change(d2);
    const ScreenOut dirty = screenFor(c.page, f, d2, regions, "*", "");
    CHECK(dirty.hasApply);
    bool anyDot = false;
    for (int i = 0; i < dirty.count; ++i) anyDot = anyDot || dirty.rows[i].changed;
    CHECK(anyDot);
    // The read-back matching the draft makes it clean again (the shell calls reset after the OK).
    d2.reset(f, regions, "*", "");
    const ScreenOut again = screenFor(c.page, f, d2, regions, "*", "");
    CHECK(!again.hasApply);
  }

  // A failed apply keeps the draft, so Apply stays visible with the error.
  Draft df;
  df.reset(f, regions, "*", "");
  df.v.radio.cr = 8;
  const ScreenOut failed = screenFor(Page::Radio, f, df, regions, "*", "");
  CHECK(failed.hasApply);
  // ...while a repeater answer that already carries CR8 makes a fresh draft clean on read-back.
  Facts f8 = f;
  f8.radio.cr = 8;
  Draft d8;
  d8.reset(f8, regions, "*", "");
  const ScreenOut done = screenFor(Page::Radio, f8, d8, regions, "*", "");
  CHECK(!done.hasApply);
}

// --- 5: neighbours ------------------------------------------------------------------------------
static void testNeighbours() {
  NeighbourList n;
  CHECK(neighboursParse("-none-", &n) == 0);
  CHECK(n.valid);   // a real empty answer, never "not read"
  CHECK(neighboursParse("", &n) == 0);
  CHECK(n.valid);
  CHECK(neighboursParse("Err - unknown command", &n) == 0);
  CHECK(!n.valid);   // not a table: the page must not claim it is empty
  CHECK(neighboursParse("> 86a8eedb:12:44\n0a1b2c3d:100:-8\r\n", &n) == 2);
  CHECK(n.valid && n.count == 2);
  CHECK(n.rows[0].prefix[0] == 0x86 && n.rows[0].secsAgo == 12 && n.rows[0].snr4 == 44);
  CHECK(n.rows[1].secsAgo == 100 && n.rows[1].snr4 == -8);

  // A REAL empty table for the page: "-none-" is the answer the repeater gives.
  neighboursParse("-none-", &n);
  Facts f;
  Draft d;
  RegionList regions;
  d.reset(f, regions, "*", "");
  ScreenIn in;
  in.page = Page::Neighbours;
  in.loggedIn = true;
  in.facts = &f;
  in.draft = &d;
  in.neighbours = &n;
  ScreenOut s;
  buildScreen(in, &s);
  const UiRow* empty = findRow(s, "Neighbours");
  CHECK(empty != nullptr);
  CHECK(contains(empty->value, "heard no neighbours yet"));

  NeighbourList emptyList;
  neighboursParse("-none-", &emptyList);
  in.neighbours = &emptyList;
  buildScreen(in, &s);
  empty = findRow(s, "Neighbours");
  CHECK(empty != nullptr && contains(empty->value, "heard no neighbours yet"));

  // A named row keeps its name (the shell resolves it from our contacts).
  NeighbourList named;
  neighboursParse("86a8eedb:5:20", &named);
  snprintf(named.rows[0].name, sizeof(named.rows[0].name), "Argus mobile RPTR");
  named.rows[0].named = true;
  named.valid = true;
  in.neighbours = &named;
  buildScreen(in, &s);
  CHECK(findRow(s, "Argus mobile RPTR") != nullptr);
}

// --- 6: map picker ------------------------------------------------------------------------------
static void testMapPicker() {
  const om::ui::Layout& L = om::ui::l2();
  const om::ui::RptrMapPickLayout clean = om::ui::rptrMapPickLayoutFor(L, false);
  CHECK(clean.apply.empty());
  CHECK(clean.crosshair.x + clean.crosshair.w / 2 == 160);
  CHECK(clean.crosshair.y + clean.crosshair.h / 2 == 107);
  CHECK(clean.back.right() <= L.width);
  CHECK(clean.zoomIn.right() <= L.width);
  CHECK(clean.useGps.right() <= L.width);
  CHECK(clean.panel.bottom() <= L.tabbar.y);
  CHECK(clean.map.h == 214);
  CHECK(om::ui::rptrMapPickHitAt(clean, clean.back.x + 2, clean.back.y + 2) ==
        om::ui::RptrMapPickHit::Back);
  CHECK(om::ui::rptrMapPickHitAt(clean, clean.zoomIn.x + 2, clean.zoomIn.y + 2) ==
        om::ui::RptrMapPickHit::ZoomIn);
  CHECK(om::ui::rptrMapPickHitAt(clean, clean.zoomOut.x + 2, clean.zoomOut.y + 2) ==
        om::ui::RptrMapPickHit::ZoomOut);
  CHECK(om::ui::rptrMapPickHitAt(clean, clean.useGps.x + 2, clean.useGps.y + 2) ==
        om::ui::RptrMapPickHit::UseGps);
  CHECK(om::ui::rptrMapPickHitAt(clean, 160, 100) == om::ui::RptrMapPickHit::Map);
  const om::ui::RptrMapPickLayout dirty = om::ui::rptrMapPickLayoutFor(L, true);
  CHECK(!dirty.apply.empty());
  CHECK(dirty.useGps.right() <= dirty.apply.x);   // the two keys never overlap
  CHECK(om::ui::rptrMapPickHitAt(dirty, dirty.apply.x + 2, dirty.apply.y + 2) ==
        om::ui::RptrMapPickHit::Apply);
}

// --- 7: page history ----------------------------------------------------------------------------
static void testNav() {
  // Every sub-page is reachable through the stack; Back pops ONE level with that page's scroll.
  for (int p = (int)Page::Name; p <= (int)Page::Console; ++p) {
    PageNav nav;
    nav.reset();
    nav.push(Page::Repeater, 7);
    nav.push((Page)p, 11 + p);
    CHECK(nav.depth() == 2);
    Page back = Page::List;
    int scroll = -1;
    CHECK(nav.pop(&back, &scroll));
    CHECK(back == (Page)p && scroll == 11 + p);
    CHECK(nav.pop(&back, &scroll));
    CHECK(back == Page::Repeater && scroll == 7);
    CHECK(nav.empty());
    CHECK(!nav.pop(&back, &scroll));
  }
  // A Regions -> HomePicker -> Back chain returns to Regions, not straight to the repeater page.
  PageNav chain;
  chain.push(Page::Repeater, 0);
  chain.push(Page::Regions, 4);
  chain.push(Page::HomePicker, 0);
  Page back = Page::List;
  int scroll = 0;
  CHECK(chain.pop(&back, &scroll) && back == Page::HomePicker);
  CHECK(chain.pop(&back, &scroll) && back == Page::Regions && scroll == 4);
  // The overflow drops the OLDEST entry, never the newest.
  PageNav deep;
  for (int i = 0; i < PageNav::kMaxDepth + 3; ++i) deep.push((Page)((int)Page::Name + (i % 14)), i);
  CHECK(deep.depth() == PageNav::kMaxDepth);
  CHECK(deep.pop(&back, &scroll));
  CHECK(scroll == PageNav::kMaxDepth + 2);   // the newest scroll survives
}

// --- 8: the home picker -------------------------------------------------------------------------
static void testPickers() {
  Facts f;
  Draft d;
  RegionList regions;
  CHECK(regionsParse("*^ F\n lj F\n", &regions) == 2);
  d.reset(f, regions, "*", "");
  const ScreenOut home = screenFor(Page::HomePicker, f, d, regions, "*", "");
  CHECK(home.count >= 2);
  CHECK(strcmp(home.rows[0].label, "* (anywhere)") == 0);
  CHECK(contains(home.rows[0].value, "no home"));
  CHECK(home.rows[0].on);   // "*" is the current (unset) home
  ApplyPlan plan;
  d.homeChanged = true;
  snprintf(d.home, sizeof(d.home), "*");
  CHECK(applyPlan(Page::HomePicker, d, regions, &plan) == 2);
  CHECK(strcmp(plan.line[0], "region home *") == 0);
  CHECK(strcmp(plan.line[1], "region save") == 0);

  const ScreenOut def = screenFor(Page::DefaultPicker, f, d, regions, "*", "");
  CHECK(def.count >= 3);
  CHECK(strcmp(def.rows[0].label, "none") == 0);
  CHECK(strcmp(def.rows[1].label, "* (anywhere)") == 0);
}

// --- 9: notifications ---------------------------------------------------------------------------
static void testNotifications() {
  // DND silences every pop-up except error and low battery.
  CHECK(!om::ui::noticeWanted(om::ui::NoticeKind::Channel, true, false));
  CHECK(!om::ui::noticeWanted(om::ui::NoticeKind::Dm, true, false));
  CHECK(!om::ui::noticeWanted(om::ui::NoticeKind::Mention, true, false));
  CHECK(!om::ui::noticeWanted(om::ui::NoticeKind::NewNode, true, false));
  CHECK(om::ui::noticeWanted(om::ui::NoticeKind::Error, true, false));
  CHECK(om::ui::noticeWanted(om::ui::NoticeKind::LowBattery, true, false));
  CHECK(!om::ui::noticeWanted(om::ui::NoticeKind::Rptr, true, false));   // only error/battery
  // The chat already on screen does not pop up over itself.
  CHECK(!om::ui::noticeWanted(om::ui::NoticeKind::Channel, false, true));
  CHECK(!om::ui::noticeWanted(om::ui::NoticeKind::Dm, false, true));
  CHECK(!om::ui::noticeWanted(om::ui::NoticeKind::Mention, false, true));
  CHECK(om::ui::noticeWanted(om::ui::NoticeKind::NewNode, false, true));

  // One line, ellipsis, and the sender/chat forms.
  char t[om::ui::kNoticeCap];
  om::ui::noticeChannelText(t, sizeof(t), "Ana", "SLO", "hello\nthere\tworld", false);
  CHECK(strcmp(t, "Ana in SLO: hello there world") == 0);
  om::ui::noticeChannelText(t, sizeof(t), "Ana", "SLO", "hi all", true);
  CHECK(strcmp(t, "Mention: Ana in SLO: hi all") == 0);
  om::ui::noticeDmText(t, sizeof(t), "Bostjan", "direct hello");
  CHECK(strcmp(t, "DM Bostjan: direct hello") == 0);
  char longText[120];
  memset(longText, 'x', sizeof(longText) - 1);
  longText[sizeof(longText) - 1] = 0;
  om::ui::noticeChannelText(t, sizeof(t), "A", "C", longText, false);
  CHECK(strlen(t) < om::ui::kNoticeCap);
  CHECK(strlen(t) >= 3 && strcmp(t + strlen(t) - 3, "...") == 0);
  om::ui::noticeBatteryText(t, sizeof(t), 12);
  CHECK(strcmp(t, "Low battery: 12 %") == 0);

  // A burst: ten notifications while one is showing leave the NEWEST pending; take it once.
  om::ui::NoticeQueue q;
  char text[32];
  for (int i = 0; i < 10; ++i) {
    snprintf(text, sizeof(text), "message %d", i);
    CHECK(q.push(text));
  }
  CHECK(q.hasPending);
  char out[om::ui::kNoticeCap];
  CHECK(q.take(out, sizeof(out)));
  CHECK(strcmp(out, "message 9") == 0);
  CHECK(!q.hasPending);
  CHECK(!q.take(out, sizeof(out)));
}

int main() {
  testCodingRate();
  testScrollingCard();
  testRegions();
  testApplyOnlyWhenChanged();
  testNeighbours();
  testMapPicker();
  testNav();
  testPickers();
  testNotifications();
  if (g_failures == 0) {
    printf("est366: CR, scrolling card, regions, Apply-only-when-changed, neighbours, map picker, nav, pickers, notifications ok\n");
    return 0;
  }
  printf("est366: %d failure(s)\n", g_failures);
  return 1;
}
