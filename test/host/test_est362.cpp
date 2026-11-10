// EST-362 host test: the RPTR section's redesigned interaction.
//   - the page model: what the repeater page shows BEFORE login (regions, clock, signal, one Log in)
//     and AFTER it (the status card, the plain settings list, Console at the very bottom)
//   - Apply = the exact writes in order, and `region save` appended in the SAME step for regions
//   - the clock guard: our clock unset refused, a repeater ahead refused, a jump over a day asks
//     first with both dates, everything up to a day applies directly
//   - which actions confirm and which do not
//   - the console: log lines are not rows, nothing in them is tappable, and no other page carries
//     CLI text
//   - the read-back parsers (get name/radio/lat/tx/repeat/loop) and the plain-word result
//   - the list order (remembered first, unnamed fallback) and the signal words
//   - the layout: one Apply button, the pop-up's Cancel/Confirm and a tap outside (a consumed Cancel)
// Run by test/host/run.sh (built with the system g++).
#include <stdio.h>
#include <string.h>

#include "rptr_page.h"
#include "ui/rptr_layout.h"

using namespace om::rptr;

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static void checkText(const char* got, const char* want, int line) {
  if (strcmp(got, want) != 0) {
    printf("FAIL %s:%d  got \"%s\", want \"%s\"\n", __FILE__, line, got, want);
    ++g_fail;
  }
}
#define CHECK_TEXT(got, want) checkText(got, want, __LINE__)

static const UiRow* findRow(const ScreenOut& s, const char* label) {
  for (int i = 0; i < s.count; ++i) {
    if (strcmp(s.rows[i].label, label) == 0) return &s.rows[i];
  }
  return nullptr;
}

// --- the page model -----------------------------------------------------------------------------
static void testBeforeAndAfterLogin() {
  Facts facts;
  Draft draft;
  RegionList regions;
  regionsParse("*\nsi^ F\nlj F", &regions);
  NeighbourList neighbours;
  neighboursParse("a1b2c3d4:12:-34", &neighbours);

  AnonFacts anon;
  anon.valid = true;
  anon.regionCount = 3;
  snprintf(anon.regions, sizeof(anon.regions), "*, si, lj");
  anon.clock = 1715779092u;   // 2024-05-15, the MeshCore restart clock
  anon.signalKnown = true;
  anon.snr4 = 44;             // 11 dB

  ScreenIn in;
  in.page = Page::Repeater;
  in.loggedIn = false;
  in.name = "Argus mobile RPTR";
  in.anon = &anon;
  in.facts = &facts;
  in.draft = &draft;
  in.regions = &regions;
  in.neighbours = &neighbours;
  ScreenOut s;
  buildScreen(in, &s);
  CHECK(s.count == 3);
  CHECK_TEXT(s.rows[0].label, "Regions");
  CHECK_TEXT(s.rows[0].value, "*, si, lj");
  CHECK_TEXT(s.rows[1].label, "Clock");
  CHECK_TEXT(s.rows[1].value, "not set (2024-05-15)");
  CHECK_TEXT(s.rows[2].label, "Signal");
  CHECK_TEXT(s.rows[2].value, "good 11 dB");
  for (int i = 0; i < s.count; ++i) CHECK(!tappable(s.rows[i].act));
  CHECK(s.bigButton);
  CHECK_TEXT(s.bigLabel, "Log in");
  CHECK(!s.hasApply);
  CHECK(s.cellCount == 0);

  // A remembered repeater offers Forget lower on the page; the password never shows.
  in.remembered = true;
  buildScreen(in, &s);
  const UiRow* forget = findRow(s, "Forget password");
  CHECK(forget != nullptr && forget->act == Act::Forget);

  // After login: the status card and the plain settings list, no Apply, no Log in.
  Status status;
  status.valid = true;
  status.battMv = 3590;
  status.noiseFloor = -112;
  status.uptimeSec = 3 * 86400 + 4 * 3600 + 11 * 60;
  status.airtimeSec = 3600;
  facts.nameKnown = true;
  snprintf(facts.name, sizeof(facts.name), "Argus mobile RPTR");
  facts.radioKnown = true;
  facts.radio.freq_khz = 869525;
  facts.radio.bw_hz = 62500;
  facts.radio.sf = 8;
  facts.radio.cr = 5;
  facts.txKnown = true;
  facts.tx = 14;
  facts.repeatKnown = true;
  facts.repeat = true;
  facts.clock = 1794337200u;   // 2026-11-10 19:00 UTC
  facts.hasClock = true;
  draft.reset(facts, regions, "*", "");
  in.loggedIn = true;
  in.status = &status;
  in.home = "*";
  in.defaultScope = "";
  buildScreen(in, &s);
  CHECK(s.cellCount == 5);
  CHECK_TEXT(s.cells[0].label, "Battery");
  CHECK_TEXT(s.cells[0].value, "3.59 V 49 %");
  CHECK_TEXT(s.cells[1].label, "Noise");
  CHECK_TEXT(s.cells[1].value, "-112 dBm");
  CHECK_TEXT(s.cells[2].label, "Uptime");
  CHECK_TEXT(s.cells[4].label, "Clock");
  CHECK_TEXT(s.cells[4].value, "19:00 UTC");   // the card is compact; the page line has the date
  CHECK(!s.bigButton);
  CHECK(!s.hasApply);
  static const char* const kOrder[] = {"Name", "Position", "Radio", "TX power", "Regions",
                                       "Neighbours", "Advertising", "Repeat", "More",
                                       "Restart repeater", "Console"};
  CHECK(s.count == 11);
  for (int i = 0; i < s.count && i < (int)(sizeof(kOrder) / sizeof(kOrder[0])); ++i) {
    CHECK_TEXT(s.rows[i].label, kOrder[i]);
  }
  CHECK(s.rows[9].act == Act::Restart && s.rows[9].danger);
  CHECK(s.rows[10].act == Act::Open && s.rows[10].index == (uint8_t)Page::Console);
  CHECK_TEXT(s.rows[0].value, "Argus mobile RPTR");
  CHECK_TEXT(s.rows[3].value, "14 dBm");
  CHECK(s.rows[7].value[0] != 0);   // "on"
  // The More page's cycle rows carry the ids the shell cycles (10 = loop, 11 = path hash).
  const UiRow* loopRow = nullptr;
  ScreenIn more = in;
  more.page = Page::More;
  buildScreen(more, &s);
  for (int i = 0; i < s.count; ++i) {
    if (strcmp(s.rows[i].label, "Loop detect") == 0) loopRow = &s.rows[i];
  }
  CHECK(loopRow != nullptr && loopRow->index == 10 && loopRow->act == Act::Toggle);

  // The clock line appears only when the repeater's clock is wrong, with the one Set clock button.
  CHECK(!s.bannerButton);
  in.clockWrong = true;
  buildScreen(in, &s);
  CHECK(s.banner[0] != 0 && strstr(s.banner, "Clock is wrong") != nullptr);
  CHECK(s.bannerButton);
}

// --- Apply = writes, then a save for regions ----------------------------------------------------
static void testApplyPlans() {
  Facts facts;
  Draft d;
  RegionList regions;
  regionsParse("*\nsi^ F\nlj F", &regions);
  facts.radio = RadioValues();
  facts.tx = 14;
  d.reset(facts, regions, "*", "");
  ApplyPlan p;

  snprintf(d.name, sizeof(d.name), "rptr-1");
  CHECK(applyPlan(Page::Name, d, regions, &p) == 1);
  CHECK_TEXT(p.line[0], "set name rptr-1");

  d.latE6 = 46250000;
  d.lonE6 = 14400000;
  CHECK(applyPlan(Page::Position, d, regions, &p) == 2);
  CHECK_TEXT(p.line[0], "set lat 46.25000");
  CHECK_TEXT(p.line[1], "set lon 14.40000");

  d.v.radio.freq_khz = 869525;
  d.v.radio.bw_hz = 62500;
  d.v.radio.sf = 8;
  d.v.radio.cr = 5;
  CHECK(applyPlan(Page::Radio, d, regions, &p) == 1);
  CHECK_TEXT(p.line[0], "set radio 869.525,62.5,8,5");
  d.v.radio.tx = 14;
  CHECK(applyPlan(Page::Tx, d, regions, &p) == 1);
  CHECK_TEXT(p.line[0], "set tx 14");
  d.v.repeat = false;
  CHECK(applyPlan(Page::Repeat, d, regions, &p) == 1);
  CHECK_TEXT(p.line[0], "set repeat off");
  d.v.advertLocal = 120;
  d.v.advertFlood = 13;
  CHECK(applyPlan(Page::Advertising, d, regions, &p) == 2);
  CHECK_TEXT(p.line[0], "set advert.interval 120");
  CHECK_TEXT(p.line[1], "set flood.advert.interval 13");
  d.v.floodMax = 8;
  d.v.loop = LoopDetect::Moderate;
  d.v.pathHash = 1;
  CHECK(applyPlan(Page::More, d, regions, &p) == 3);
  CHECK_TEXT(p.line[0], "set flood.max 8");
  CHECK_TEXT(p.line[1], "set loop.detect moderate");
  CHECK_TEXT(p.line[2], "set path.hash.mode 1");

  // Regions: one deny, the home and the default scope, a new region, then exactly ONE region save,
  // all in the same Apply.
  d.reset(facts, regions, "*", "");
  d.floodChanged[2] = true;
  d.flood[2] = true;   // "lj" was denied, now allowed
  d.homeChanged = true;
  snprintf(d.home, sizeof(d.home), "si");
  d.defaultChanged = true;
  d.defaultNone = false;
  snprintf(d.defaultScope, sizeof(d.defaultScope), "lj");
  d.hasAdd = true;
  snprintf(d.addName, sizeof(d.addName), "primorska");
  d.parentSel = 1;
  snprintf(d.addParent, sizeof(d.addParent), "si");
  const int n = applyPlan(Page::Regions, d, regions, &p);
  CHECK(n == 5);
  CHECK_TEXT(p.line[0], "region allowf lj");
  CHECK_TEXT(p.line[1], "region home si");
  CHECK_TEXT(p.line[2], "region default lj");
  CHECK_TEXT(p.line[3], "region put primorska si");
  CHECK_TEXT(p.line[4], "region save");
  int saves = 0;
  for (int i = 0; i < n; ++i) {
    if (strcmp(p.line[i], "region save") == 0) ++saves;
  }
  CHECK(saves == 1);
  // An unchanged region applies nothing (and no save).
  d.reset(facts, regions, "*", "");
  CHECK(applyPlan(Page::Regions, d, regions, &p) == 0);

  // Picking a home scope is its own Apply: the line plus the save.
  d.reset(facts, regions, "*", "");
  snprintf(d.home, sizeof(d.home), "*");
  d.homeChanged = true;
  CHECK(applyPlan(Page::HomePicker, d, regions, &p) == 2);
  CHECK_TEXT(p.line[0], "region home *");
  CHECK_TEXT(p.line[1], "region save");
  d.defaultNone = true;
  d.defaultChanged = true;
  CHECK(applyPlan(Page::DefaultPicker, d, regions, &p) == 2);
  CHECK_TEXT(p.line[0], "region default <null>");
  CHECK_TEXT(p.line[1], "region save");

  // Add region: one put, one save, no defaults invented.
  d.reset(facts, regions, "*", "");
  d.hasAdd = true;
  snprintf(d.addName, sizeof(d.addName), "gorica");
  CHECK(applyPlan(Page::AddRegion, d, regions, &p) == 2);
  CHECK_TEXT(p.line[0], "region put gorica");
  CHECK_TEXT(p.line[1], "region save");

  char reads[kReadBackMax][24];
  CHECK(readBackLines(Page::Regions, reads) == 3);
  CHECK_TEXT(reads[0], "region");
  CHECK_TEXT(reads[1], "region home");
  CHECK_TEXT(reads[2], "region default");
  CHECK(readBackLines(Page::Name, reads) == 1);
  CHECK_TEXT(reads[0], "get name");
  CHECK(readBackLines(Page::Neighbours, reads) == 1);
  CHECK_TEXT(reads[0], "neighbors");
  CHECK(readBackLines(Page::Console, reads) == 0);
}

// --- the clock guard ----------------------------------------------------------------------------
static void testClockGuard() {
  const uint32_t our = 1794337200u;   // 2026-11-10 19:00 UTC
  CHECK(clockGuard(our, false, true, our) == ClockVerdict::OurClockNotSet);
  CHECK(clockGuard(0, true, true, our) == ClockVerdict::OurClockNotSet);
  // A repeater ahead of us is the only refusal left.
  CHECK(clockGuard(our, true, true, our + 60u) == ClockVerdict::Ok);
  CHECK(clockGuard(our, true, true, our + 61u) == ClockVerdict::RepeaterAhead);
  // Up to a day behind applies directly; more asks first.
  CHECK(clockGuard(our, true, true, our - 86400u) == ClockVerdict::Ok);
  CHECK(clockGuard(our, true, true, our - 86401u) == ClockVerdict::NeedConfirm);
  // The MeshCore 2024 restart clock and an unset clock are the same one-way jump.
  CHECK(clockGuard(our, true, true, 1715779092u) == ClockVerdict::NeedConfirm);
  CHECK(clockGuard(our, true, true, 0) == ClockVerdict::NeedConfirm);
  CHECK(clockGuard(our, true, false, 0) == ClockVerdict::NeedConfirm);
  CHECK_TEXT(clockVerdictText(ClockVerdict::OurClockNotSet), "our clock is not set");
  CHECK_TEXT(clockVerdictText(ClockVerdict::RepeaterAhead),
             "the repeater's clock is ahead of ours");

  // The line on the page, and its two dates in the confirmation.
  CHECK(clockWrong(false, 0, our, true));
  CHECK(clockWrong(true, 1715779092u, our, true));
  CHECK(!clockWrong(true, our, our, true));
  CHECK(clockWrong(true, our + 61u, our, true));
  CHECK(!clockWrong(true, our + 60u, our, true));
  CHECK(!clockWrong(true, our, our, false));   // our clock unset: nothing to compare
  char t[96];
  clockStampText(t, sizeof(t), 1794337200u);
  CHECK_TEXT(t, "2026-11-10 19:00 UTC");
  clockStampText(t, sizeof(t), 1715779092u);
  CHECK_TEXT(t, "not set (2024-05-15)");
  clockStampText(t, sizeof(t), 0);
  CHECK_TEXT(t, "not set");
  clockJumpText(t, sizeof(t), 1715779092u, 1794337200u);
  CHECK(strstr(t, "2024-05-15 13:18") != nullptr);
  CHECK(strstr(t, "2026-11-10 19:00") != nullptr);

  // The pop-up is only for a jump over a day.
  CHECK(!confirmClock(our - 3600u, our).need);
  CHECK(!confirmClock(our - 86400u, our).need);
  const ConfirmAsk ask = confirmClock(our - 86400u - 1u, our);
  CHECK(ask.need);
  CHECK_TEXT(ask.verb, "Set clock");
  CHECK(strstr(ask.text, "2026-11-10") != nullptr);
}

// --- which actions confirm ----------------------------------------------------------------------
static void testConfirmRules() {
  Facts facts;
  Draft d;
  RegionList regions;
  regionsParse("*\nsi^ F", &regions);
  d.reset(facts, regions, "*", "");

  CHECK(!confirmForApply(Page::Name, d).need);
  CHECK(!confirmForApply(Page::Position, d).need);
  CHECK(!confirmForApply(Page::Tx, d).need);
  CHECK(!confirmForApply(Page::Advertising, d).need);
  CHECK(!confirmForApply(Page::More, d).need);
  CHECK(!confirmForApply(Page::Regions, d).need);
  CHECK(!confirmForApply(Page::HomePicker, d).need);
  CHECK(!confirmForApply(Page::DefaultPicker, d).need);

  const ConfirmAsk radio = confirmForApply(Page::Radio, d);
  CHECK(radio.need);
  CHECK_TEXT(radio.verb, "Apply");
  CHECK(strstr(radio.text, "restarts") != nullptr);
  CHECK(strstr(radio.text, "drop off the mesh") != nullptr);

  // Repeat OFF is risky, Repeat ON is not.
  d.v.repeat = false;
  const ConfirmAsk off = confirmForApply(Page::Repeat, d);
  CHECK(off.need);
  CHECK_TEXT(off.verb, "Apply");
  CHECK(strstr(off.text, "stop passing floods") != nullptr);
  d.v.repeat = true;
  CHECK(!confirmForApply(Page::Repeat, d).need);

  const ConfirmAsk restart = confirmRestart();
  CHECK(restart.need && strcmp(restart.verb, "Restart") == 0);
  CHECK(strstr(restart.text, "restart") != nullptr);
  const ConfirmAsk remove = confirmRemoveRegion("si");
  CHECK(remove.need && strcmp(remove.verb, "Remove") == 0);
  CHECK(strstr(remove.text, "si") != nullptr);
}

// --- the console ---------------------------------------------------------------------------------
static void testConsole() {
  const char* log[] = {"> region allowf lj", "< OK", "> region save", "< OK",
                       "> get radio", "< 869.525,62.5,8,5"};
  ScreenIn in;
  in.page = Page::Console;
  in.loggedIn = true;
  in.log = log;
  in.logCount = 6;
  in.consoleInput = "";
  Facts facts;
  Draft d;
  RegionList regions;
  in.facts = &facts;
  in.draft = &d;
  in.regions = &regions;
  ScreenOut s;
  buildScreen(in, &s);
  CHECK(s.hasConsole);
  CHECK(s.logLineCount == 6);
  CHECK_TEXT(s.logLines[1], "< OK");
  CHECK_TEXT(s.logLines[3], "< OK");
  // The log lines are NOT rows, and the page has no rows at all: the free command line is the
  // layout's own input + Send, so an answer can never be tapped.
  CHECK(s.count == 0);
  CHECK(!tappable(Act::None));
  in.consoleInput = "get radio";
  buildScreen(in, &s);
  CHECK_TEXT(s.consoleInput, "get radio");

  // ">region save OK" style answers never leak onto a setting page, and no row is a CLI line.
  in.page = Page::Regions;
  in.home = "*";
  in.defaultScope = "";
  buildScreen(in, &s);
  for (int i = 0; i < s.count; ++i) {
    CHECK(s.rows[i].value[0] != '>');
    CHECK(strstr(s.rows[i].value, "OK") == nullptr);
  }
}

// --- the read-backs and plain words -------------------------------------------------------------
static void testReadBacks() {
  char v[40];
  CHECK(valueReplyParse("> rptr-1", v, sizeof(v)));
  CHECK_TEXT(v, "rptr-1");
  CHECK(valueReplyParse("> *\n", v, sizeof(v)));
  CHECK_TEXT(v, "*");
  int32_t n = 0;
  CHECK(intReplyParse("> 14", &n) && n == 14);
  CHECK(!intReplyParse("> 14 hops", &n));
  RadioValues r;
  CHECK(radioReplyParse("> 869.525,62.5,8,5", &r));
  CHECK(r.freq_khz == 869525 && r.bw_hz == 62500 && r.sf == 8 && r.cr == 5);
  CHECK(!radioReplyParse("> 869.525,62.5,1,5", &r));   // SF1 is not a radio
  int32_t e6 = 0;
  CHECK(coordReplyParse("> 46.25000", &e6) && e6 == 46250000);
  CHECK(coordReplyParse("> -14.4", &e6) && e6 == -14400000);
  CHECK(!coordReplyParse("> 200.25", &e6));             // beyond a longitude
  bool on = false;
  CHECK(switchReplyParse("> off", &on) && !on);
  CHECK(switchReplyParse("> on", &on) && on);
  LoopDetect loop = LoopDetect::Off;
  CHECK(loopReplyParse("> strict", &loop) && loop == LoopDetect::Strict);

  CHECK(answerOk("OK"));
  CHECK(answerOk("OK - reboot to apply"));
  CHECK(answerOk(" home is now si"));
  CHECK(answerOk(" default scope is now lj"));
  CHECK(answerOk("OK - (flood allowed)"));
  CHECK(!answerOk("Err - unknown region"));
  CHECK(!answerOk("Error, invalid radio params"));
  char plain[64];
  answerPlainText("OK", plain, sizeof(plain));
  CHECK_TEXT(plain, "Applied");
  answerPlainText("Err - unknown region", plain, sizeof(plain));
  CHECK_TEXT(plain, "The repeater does not know that region.");
  answerPlainText("OK - reboot to apply", plain, sizeof(plain));
  CHECK_TEXT(plain, "Applied. Restart the repeater to use it.");
  answerPlainText("Err - not empty", plain, sizeof(plain));
  CHECK_TEXT(plain, "That region still has child regions.");
}

// --- the list ------------------------------------------------------------------------------------
static void testList() {
  FoundList found;
  found.begin(0x1234u, 1000);
  uint8_t keyA[32] = {0x86, 0xa8, 0xee, 0x01};
  uint8_t keyB[32] = {0x11, 0x22, 0x33, 0x02};
  found.addAnswer(keyA, 32, "", false, false, 0, true, 44, 0, 2000);   // unnamed, 11 dB
  found.addAnswer(keyB, 32, "Bohinj-RPTR", false, false, 0, true, -36, 0, 2000);  // -9 dB
  const char* remembered[] = {"Argus mobile RPTR", "Bohinj-RPTR"};
  ListRow rows[8];
  const int n = listRows(found, remembered, 2, rows, 8);
  CHECK(n == 3);
  CHECK_TEXT(rows[0].name, "Argus mobile RPTR");
  CHECK(rows[0].saved && rows[0].foundIndex == 255);        // remembered, not heard now
  CHECK_TEXT(rows[1].name, "Bohinj-RPTR");
  CHECK(rows[1].saved && rows[1].foundIndex == 1);          // remembered AND heard
  CHECK_TEXT(rows[1].signal, "poor -9 dB");
  CHECK_TEXT(rows[2].name, "unnamed 86a8ee");
  CHECK(!rows[2].saved && rows[2].foundIndex == 0);
  CHECK_TEXT(rows[2].signal, "good 11 dB");

  char t[24];
  signalText(true, 44, t, sizeof(t));
  CHECK_TEXT(t, "good 11 dB");
  CHECK(!signalText(false, 0, t, sizeof(t)));
  CHECK_TEXT(signalWord(0), "fair");
  CHECK_TEXT(signalWord(60), "excellent");
  CHECK_TEXT(signalWord(-40), "poor");
}

// --- layout and the pop-up ----------------------------------------------------------------------
static void testLayout() {
  const om::ui::Layout& L = om::ui::l2();
  const om::ui::RptrListLayout list = om::ui::rptrListLayoutFor(L);
  CHECK(list.scan.right() <= L.width);
  CHECK(list.list.y == list.head.bottom());
  CHECK(list.list.bottom() == L.tabbar.y);
  CHECK(om::ui::rptrListHitAt(list, 3, 0, list.scan.x + 2, list.scan.y + 2).kind ==
        om::ui::RptrHit::Scan);
  CHECK(om::ui::rptrListHitAt(list, 3, 0, 4, list.list.y + 4).kind == om::ui::RptrHit::ListRow);

  // A page with a card, a banner and one Apply.
  const om::ui::RptrPageLayout a = om::ui::rptrPageLayoutFor(L, true, true, true, false, false);
  CHECK(a.card.y >= L.status.bottom());
  CHECK(a.banner.y >= a.card.bottom());
  CHECK(a.body.y >= a.banner.bottom());
  CHECK(a.body.bottom() <= a.apply.y);
  CHECK(a.apply.bottom() <= L.tabbar.y);
  CHECK(a.refresh.contains(a.refresh.x + 2, a.refresh.y + 2));
  CHECK(a.bannerButton.right() <= a.banner.right());

  // The rows the hit test answers with, from the same model the drawing uses.
  ScreenIn in;
  Facts facts;
  Draft d;
  RegionList regions;
  regionsParse("*\nsi^ F\nlj F", &regions);
  d.reset(facts, regions, "*", "");
  in.page = Page::Regions;
  in.loggedIn = true;
  in.draft = &d;
  in.regions = &regions;
  in.facts = &facts;
  in.home = "*";
  in.defaultScope = "";
  ScreenOut s;
  buildScreen(in, &s);
  const om::ui::RptrPageLayout b = om::ui::rptrPageLayoutFor(L, false, false, s.hasApply, false, false);
  CHECK(s.hasApply);
  CHECK(om::ui::rptrPageHitAt(b, s, 0, b.apply.x + 2, b.apply.y + 2).kind ==
        om::ui::RptrHit::Apply);
  const om::ui::RptrRowRects r0 = om::ui::rptrPageRowRects(b, s.rows[0], 0, 0);
  CHECK(om::ui::rptrPageHitAt(b, s, 0, r0.body.x + 5, r0.body.y + 5).kind ==
        om::ui::RptrHit::RowBody);
  // A region row's small "x" key is its own target (the remove confirmation). The wildcard has
  // no key: it is the region tree's root, not something to remove.
  CHECK(s.rows[0].key[0] == 0);
  CHECK(s.rows[1].key[0] == 'x');
  const om::ui::RptrRowRects r1x = om::ui::rptrPageRowRects(b, s.rows[1], 1, 0);
  CHECK(om::ui::rptrPageHitAt(b, s, 0, r1x.keyA.x + 2, r1x.keyA.y + 2).kind ==
        om::ui::RptrHit::RowKey);
  // A stepper's two keys.
  in.page = Page::Radio;
  buildScreen(in, &s);
  const om::ui::RptrPageLayout c = om::ui::rptrPageLayoutFor(L, false, false, s.hasApply, false, false);
  const om::ui::RptrRowRects r1 = om::ui::rptrPageRowRects(c, s.rows[0], 0, 0);
  CHECK(s.rows[0].stepKeys);
  CHECK(om::ui::rptrPageHitAt(c, s, 0, r1.keyA.x + 2, r1.keyA.y + 2).kind ==
        om::ui::RptrHit::RowMinus);
  CHECK(om::ui::rptrPageHitAt(c, s, 0, r1.keyB.x + 2, r1.keyB.y + 2).kind ==
        om::ui::RptrHit::RowPlus);

  // The pop-up: Cancel and Confirm are buttons, everywhere else (including outside the box) is a
  // Cancel the caller consumes.
  const om::ui::RptrModalLayout m = om::ui::rptrModalLayoutFor(L, "Apply");
  CHECK(om::ui::rptrModalHitAt(m, m.cancel.x + 2, m.cancel.y + 2) == om::ui::RptrModalHit::Cancel);
  CHECK(om::ui::rptrModalHitAt(m, m.confirm.x + 2, m.confirm.y + 2) == om::ui::RptrModalHit::Confirm);
  CHECK(om::ui::rptrModalHitAt(m, 1, 1) == om::ui::RptrModalHit::Outside);
  CHECK(om::ui::rptrModalHitAt(m, m.box.right() + 5, m.box.y + 5) == om::ui::RptrModalHit::Outside);
}

int main() {
  testBeforeAndAfterLogin();
  testApplyPlans();
  testClockGuard();
  testConfirmRules();
  testConsole();
  testReadBacks();
  testList();
  testLayout();
  if (g_fail == 0) {
    printf("  OK  est362: page model, Apply plans, clock guard, confirms, console, read-backs, list\n");
  }
  return g_fail == 0 ? 0 : 1;
}
