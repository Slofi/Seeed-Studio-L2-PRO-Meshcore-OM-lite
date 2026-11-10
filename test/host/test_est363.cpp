// EST-363 host test: the RPTR page model at its LARGEST inputs, the size guard that keeps it off the
// loop task's stack, and the three layout defects Filip photographed at 20:08.
//
// What this proves, and why:
//   1. No buffer overrun. Every RPTR screen state (list, page before login, logging in, logged in with
//      a full status card, every settings page, regions, neighbours, console, home/default pickers) is
//      built with the longest names, the maximum region and neighbour counts, a full 8-line console
//      log and the extreme numbers. Every text buffer is checked to be NUL-terminated INSIDE its own
//      array, and the whole ScreenOut is built inside guard words, so a write past the end of the
//      struct is caught too.
//   2. The size guard. The EST-362 panic was the loop task's stack: ScreenOut (3,812 B) and RptrView
//      (6,204 B) were copied as temporaries (ELF frames 4,224 B and 7,504 B on a 16,384 B stack). The
//      static_asserts below are the tripwire: if either model grows past its cap, this test stops
//      compiling and the fix (a static/PSRAM buffer) has to be revisited, not silently regressed.
//   3. The layout: the title and the subtitle get one band each (they used to be drawn over each
//      other), the row area ends on a row boundary above the button (the Signal row's value used to be
//      cut in half by the Log in button), and the pre-login facts all fit without scrolling.
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

// ------------------------------------------------------------------------------------- the size guard
// Measured on the EST-362 ELF's DWARF (docs/PROGRESS.md, 2026-11-10 21:10 entry). The loop task's
// stack is 16,384 B and the RPTR path may not spend a third of it on a copy of the page model.
static_assert(sizeof(ScreenOut) <= 4096, "ScreenOut grew: the RPTR stack budget (EST-363)");
static_assert(sizeof(ScreenIn) <= 128, "ScreenIn grew: it is passed by reference, keep it small");
static_assert(sizeof(UiRow) <= 128, "UiRow grew: 26 rows would no longer fit ScreenOut");
static_assert(sizeof(ScreenOut::title) == 40, "page title buffer changed");
static_assert(sizeof(ScreenOut::subtitle) == 80, "page subtitle buffer changed");
static_assert(sizeof(ScreenOut::banner) == 120, "page banner buffer changed");
static_assert(sizeof(ScreenOut::logLines[0]) == 88, "console log line buffer changed");
static_assert(sizeof(UiRow::label) == 24, "row label buffer changed");
static_assert(sizeof(UiRow::value) == 64, "row value buffer changed");

// NUL-terminated inside its own array: the only definition of "did not overrun" the model can have.
static bool fits(const char* s, size_t cap) { return memchr(s, 0, cap) != nullptr; }

// The whole model inside guard words: a write past the end of ScreenOut lands in `post`.
struct Guarded {
  uint32_t pre[8];
  ScreenOut out;
  uint32_t post[8];
};
constexpr uint32_t kGuard = 0xA5C3F00Du;

static void guardFill(Guarded* g) {
  for (int i = 0; i < 8; ++i) {
    g->pre[i] = kGuard;
    g->post[i] = kGuard;
  }
}

static bool guardIntact(const Guarded& g) {
  for (int i = 0; i < 8; ++i) {
    if (g.pre[i] != kGuard || g.post[i] != kGuard) return false;
  }
  return true;
}

// Every text buffer of the model, with its own capacity.
static void checkBuffers(const ScreenOut& s, int line) {
  bool ok = true;
  ok = ok && fits(s.title, sizeof(s.title));
  ok = ok && fits(s.subtitle, sizeof(s.subtitle));
  ok = ok && fits(s.banner, sizeof(s.banner));
  ok = ok && fits(s.bigLabel, sizeof(s.bigLabel));
  ok = ok && fits(s.consoleInput, sizeof(s.consoleInput));
  ok = ok && s.count >= 0 && s.count <= kUiRowCap;
  ok = ok && s.cellCount >= 0 && s.cellCount <= 6;
  ok = ok && s.logLineCount >= 0 && s.logLineCount <= kConsoleLines;
  for (int i = 0; i < s.count; ++i) {
    ok = ok && fits(s.rows[i].label, sizeof(s.rows[i].label));
    ok = ok && fits(s.rows[i].value, sizeof(s.rows[i].value));
    ok = ok && fits(s.rows[i].key, sizeof(s.rows[i].key));
  }
  for (int i = 0; i < s.cellCount; ++i) {
    ok = ok && fits(s.cells[i].label, sizeof(s.cells[i].label));
    ok = ok && fits(s.cells[i].value, sizeof(s.cells[i].value));
  }
  for (int i = 0; i < s.logLineCount; ++i) {
    ok = ok && fits(s.logLines[i], sizeof(s.logLines[i]));
  }
  if (!ok) {
    printf("FAIL %s:%d  a text buffer was not NUL-terminated inside its array\n", __FILE__, line);
    ++g_fail;
  }
}

// ---------------------------------------------------------------------------------- the largest input
static const char* kLongName = "REPEATER-LJUBLJANA-SLOVENIJA-01";   // 31 characters (kNameMax)
static const char* kLongRegion = "REGION-ABCDEFGHIJKLMNOPQRSTUVW";   // 30 characters (kRegionNameMax)

struct Largest {
  Facts facts;
  Draft draft;
  RegionList regions;
  NeighbourList neighbours;
  Status status;
  AnonFacts anon;
  char home[32];
  char defScope[32];
  char result[80];
  char consoleInput[64];
  const char* log[kConsoleLines];
  char logBuf[kConsoleLines][88];
  ScreenIn in;

  void fill() {
    // --- facts: everything known, the extremes of every number
    facts.reset();
    snprintf(facts.name, sizeof(facts.name), "%s", kLongName);
    facts.nameKnown = true;
    facts.latE6 = -90000000;      // -90.00000
    facts.lonE6 = -180000000;     // -180.00000
    facts.posKnown = true;
    facts.radio.freq_khz = 2500000;   // the top of the ladder
    facts.radio.bw_hz = 500000;
    facts.radio.sf = 12;
    facts.radio.cr = 8;
    facts.radio.tx = -128;
    facts.radioKnown = true;
    facts.tx = -128;
    facts.txKnown = true;
    facts.repeat = true;
    facts.repeatKnown = true;
    facts.advertLocal = 255;
    facts.advertFlood = 255;
    facts.floodMax = 255;
    facts.loop = LoopDetect::Strict;
    facts.pathHash = 3;
    facts.advertLocalKnown = facts.advertFloodKnown = true;
    facts.floodMaxKnown = facts.loopKnown = facts.pathHashKnown = true;
    facts.clock = 0xFFFFFFFFu;    // a clock in 2106: the longest stamp the formatter can make
    facts.hasClock = true;

    // --- regions: every slot filled with the longest name
    regions.count = kMaxRegions;
    regions.valid = true;
    for (int i = 0; i < kMaxRegions; ++i) {
      snprintf(regions.rows[i].name, sizeof(regions.rows[i].name), "%s", kLongRegion);
      regions.rows[i].allow = true;
      regions.rows[i].home = i == 1;
      regions.rows[i].wildcard = i == 0;
    }

    // --- neighbours: every slot, the longest name, the extreme SNR and age
    neighbours.valid = true;
    neighbours.count = kMaxNeighbours;
    for (int i = 0; i < kMaxNeighbours; ++i) {
      snprintf(neighbours.rows[i].name, sizeof(neighbours.rows[i].name), "%s", kLongName);
      neighbours.rows[i].secsAgo = 0x7FFFFFFF;
      neighbours.rows[i].snr4 = -128;
    }

    // --- status: a full card, huge uptime/airtime, the lowest battery cell
    status.valid = true;
    status.battMv = 4200;
    status.noiseFloor = -32768;
    status.uptimeSec = 0xFFFFFFFFu;
    status.airtimeSec = 0xFFFFFFFFu;
    status.clock = 0xFFFFFFFFu;
    status.clockValid = true;

    // --- the anonymous read: the longest region summary that fits
    anon.asked = true;
    anon.valid = true;
    anon.regionCount = kMaxRegions;
    snprintf(anon.regions, sizeof(anon.regions), "%s", "* , si, lj, aaa, bbb, ccc, ddd, eee, fff");
    anon.clock = 0xFFFFFFFFu;
    anon.signalKnown = true;
    anon.snr4 = 127;              // 31.75 dB: "excellent 32 dB"

    // --- the draft: long names everywhere, every region changed, a long add-region name
    draft.reset(facts, regions, "*", "");
    snprintf(draft.name, sizeof(draft.name), "%s", kLongName);
    snprintf(draft.home, sizeof(draft.home), "%s", kLongRegion);
    snprintf(draft.defaultScope, sizeof(draft.defaultScope), "%s", kLongRegion);
    snprintf(draft.addName, sizeof(draft.addName), "%s", kLongRegion);
    snprintf(draft.addParent, sizeof(draft.addParent), "%s", kLongRegion);
    draft.parentSel = kMaxRegions - 1;
    draft.hasAdd = true;
    draft.homeChanged = true;
    draft.defaultChanged = true;
    for (int i = 0; i < kMaxRegions; ++i) {
      draft.flood[i] = true;
      draft.floodChanged[i] = true;
    }

    // --- the console: 8 full 87-character lines and a full input line
    for (int i = 0; i < kConsoleLines; ++i) {
      char* b = logBuf[i];
      const size_t cap = sizeof(logBuf[i]);
      snprintf(b, cap, "> %s", i % 2 == 0 ? "set radio 2500000 500000 12 8 -128" : "get radio");
      const size_t n = strlen(b);
      for (size_t k = n; k + 1 < cap; ++k) b[k] = 'x';   // pad to 87 characters
      b[cap - 1] = 0;
      log[i] = logBuf[i];
    }
    for (int i = 0; i < (int)sizeof(consoleInput) - 1; ++i) consoleInput[i] = 'y';
    consoleInput[sizeof(consoleInput) - 1] = 0;

    snprintf(home, sizeof(home), "%s", kLongRegion);
    snprintf(defScope, sizeof(defScope), "%s", kLongRegion);
    snprintf(result, sizeof(result), "%s", "Error: the repeater refused the radio change (invalid)");

    // --- every field of ScreenIn, at its widest
    in.page = Page::Repeater;
    in.loggedIn = false;
    in.busy = false;
    in.remembered = true;
    in.name = kLongName;
    in.status = &status;
    in.facts = &facts;
    in.draft = &draft;
    in.regions = &regions;
    in.neighbours = &neighbours;
    in.anon = &anon;
    in.home = home;
    in.defaultScope = defScope;
    in.result = result;
    in.resultOk = false;
    in.resultPending = false;
    in.clockWrong = true;
    in.log = log;
    in.logCount = kConsoleLines;
    in.consoleInput = consoleInput;
  }
};

// ---------------------------------------------------------------------------------- every page, wide
static const Page kPages[] = {
    Page::List,       Page::Repeater,    Page::Name,       Page::Position,  Page::Radio,
    Page::Tx,         Page::Repeat,      Page::Regions,    Page::AddRegion, Page::HomePicker,
    Page::DefaultPicker, Page::Neighbours, Page::Advertising, Page::More,   Page::Console,
};

static void testEveryPageLargest() {
  static Largest M;
  M.fill();
  static Guarded g;

  for (size_t p = 0; p < sizeof(kPages) / sizeof(kPages[0]); ++p) {
    // The repeater page in its three shapes: before login (remembered password), logging in, and
    // logged in with a full status card. Every other page runs once.
    const int shapes = kPages[p] == Page::Repeater ? 3 : 1;
    for (int shape = 0; shape < shapes; ++shape) {
      M.in.page = kPages[p];
      if (kPages[p] == Page::Repeater) {
        M.in.loggedIn = shape == 2;
        M.in.busy = shape == 1;
        M.in.clockWrong = shape == 2;
      } else {
        M.in.loggedIn = true;
      }
      guardFill(&g);
      const int n = buildScreen(M.in, &g.out);
      CHECK(guardIntact(g));
      CHECK(n == g.out.count);
      CHECK(n >= 0 && n <= kUiRowCap);
      checkBuffers(g.out, __LINE__);
      CHECK(fits(g.out.title, sizeof(g.out.title)));
    }
  }
}

// A build must leave nothing behind from the page before it: the reset is what makes the static
// instance behave like a fresh one (the EST-362 code got this from a temporary; the fix must keep it).
static void testResetBetweenPages() {
  static Largest M;
  M.fill();
  static ScreenOut s;
  M.in.page = Page::Console;
  M.in.loggedIn = true;
  buildScreen(M.in, &s);
  CHECK(s.hasConsole);
  CHECK(s.logLineCount == kConsoleLines);
  CHECK(s.consoleInput[0] == 'y');

  M.in.page = Page::Name;
  buildScreen(M.in, &s);
  CHECK(!s.hasConsole);
  CHECK(s.logLineCount == 0);
  CHECK(s.consoleInput[0] == 0);
  CHECK(s.cellCount == 0);          // no status card left over from the repeater page
  CHECK(s.count == 1);              // just Name
  CHECK(strcmp(s.title, "Name") == 0);

  // ScreenOut::reset() itself zeroes every byte (it is the in-place replacement of `= ScreenOut()`).
  s.reset();
  CHECK(s.count == 0 && s.cellCount == 0 && s.logLineCount == 0);
  CHECK(s.title[0] == 0 && s.subtitle[0] == 0 && s.banner[0] == 0 && s.consoleInput[0] == 0);
  CHECK(!s.bigButton && !s.hasApply && !s.hasConsole && !s.applyDanger && !s.bannerButton);
}

// -------------------------------------------------------------------------------------- the layout
static void testLayoutDefects() {
  const om::ui::Layout& L = om::ui::l2();
  static Largest M;
  M.fill();

  // --- 1. the page before login: title, subtitle, the hint banner, three facts, one big Log in
  M.in.page = Page::Repeater;
  M.in.loggedIn = false;
  M.in.busy = false;
  static ScreenOut s;
  buildScreen(M.in, &s);
  const om::ui::RptrPageLayout a =
      om::ui::rptrPageLayoutFor(L, s.cellCount > 0, s.banner[0] != 0, s.hasApply, s.bigButton,
                                s.hasConsole);

  // One band per line: the title never shares a band with the subtitle (EST-362 drew them on top of
  // each other, docs/shots/2026-11-10-2008-rptr-page.png).
  CHECK(a.title.bottom() <= a.sub.y);
  CHECK(a.sub.bottom() <= a.banner.y);
  CHECK(a.title.h >= 18);
  CHECK(a.sub.h >= 14);

  // The hint reaches the banner in one piece and its text fits the banner's width. 7 px per
  // character is a deliberately generous estimate for the 11 px sans face (the mono advance is 6).
  CHECK(strcmp(s.banner, "Password remembered: Log in is one tap.") == 0);
  const int hintPx = (int)strlen(s.banner) * 7;
  CHECK(hintPx <= a.banner.w - 12);

  // --- 2/the row area ends on a row boundary and the button sits under the last row
  CHECK(a.body.h % om::ui::kRptrPageRowH == 0);
  CHECK(a.body.bottom() <= a.big.y);
  CHECK(a.big.bottom() <= L.tabbar.y);
  CHECK(a.visibleRows >= 3);                       // regions, clock, signal all visible, no scrolling
  CHECK(s.count >= 3);
  for (int i = 0; i < a.visibleRows && i < s.count; ++i) {
    const om::ui::RptrRowRects r = om::ui::rptrPageRowRects(a, s.rows[i], i, 0);
    CHECK(r.body.y >= a.body.y);
    CHECK(r.body.bottom() <= a.body.bottom());     // a row never runs into the button
    CHECK(r.value.bottom() <= a.body.bottom());    // ...and neither does its value line
  }
  // The third fact is the Signal row: its value box is fully inside the row area.
  const om::ui::RptrRowRects sig = om::ui::rptrPageRowRects(a, s.rows[2], 2, 0);
  CHECK(sig.value.bottom() <= a.body.bottom());
  CHECK(sig.value.y < a.big.y);

  // --- the same rule on every page shape: Apply, big button and console
  const om::ui::RptrPageLayout apply = om::ui::rptrPageLayoutFor(L, false, true, true, false, false);
  CHECK(apply.body.h % om::ui::kRptrPageRowH == 0);
  CHECK(apply.body.bottom() <= apply.apply.y);
  const om::ui::RptrPageLayout card = om::ui::rptrPageLayoutFor(L, true, true, true, false, false);
  CHECK(card.body.h % om::ui::kRptrPageRowH == 0);
  CHECK(card.card.bottom() <= card.banner.y);
  CHECK(card.banner.bottom() <= card.body.y);
  CHECK(card.body.bottom() <= card.apply.y);
  CHECK(card.visibleRows >= 1);
  // The console keeps its full area: it draws log lines, not 30 px rows.
  const om::ui::RptrPageLayout con = om::ui::rptrPageLayoutFor(L, false, false, false, false, true);
  CHECK(con.body.bottom() <= con.input.y);
  CHECK(con.body.h > 0);

  // The scroll still reaches every row that does not fit (nothing is unreachable).
  for (int p = 0; p < (int)(sizeof(kPages) / sizeof(kPages[0])); ++p) {
    M.in.page = kPages[p];
    M.in.loggedIn = true;
    buildScreen(M.in, &s);
    const om::ui::RptrPageLayout lp = om::ui::rptrPageLayoutFor(
        L, s.cellCount > 0, s.banner[0] != 0, s.hasApply, s.bigButton, s.hasConsole);
    if (s.hasConsole) continue;
    const int maxScroll = om::ui::rptrPageMaxScroll(lp, s.count);
    if (s.count > lp.visibleRows) {
      CHECK(maxScroll == s.count * om::ui::kRptrPageRowH - lp.body.h);
      CHECK(maxScroll > 0);
      // At the bottom of the scroll the last row is fully visible.
      const om::ui::RptrRowRects last = om::ui::rptrPageRowRects(lp, s.rows[s.count - 1],
                                                                s.count - 1, maxScroll);
      CHECK(last.body.bottom() <= lp.body.bottom());
      CHECK(last.body.y >= lp.body.y);
    } else {
      CHECK(maxScroll == 0);
    }
  }
}

// The longest single strings the model can produce, checked against the layout widths that have to
// hold them (the drawing clips, but a value that is always clipped is a layout bug).
static void testLongestText() {
  static Largest M;
  M.fill();
  static ScreenOut s;

  // Before login with the longest anonymous regions string and a 2106 clock.
  M.in.page = Page::Repeater;
  M.in.loggedIn = false;
  buildScreen(M.in, &s);
  CHECK(s.count >= 3);
  CHECK(strlen(s.rows[0].value) <= sizeof(s.rows[0].value) - 1);
  CHECK(strstr(s.rows[1].value, "2106-") != nullptr);   // 0xFFFFFFFF is a real date, not "not set"
  CHECK(fits(s.rows[1].value, sizeof(s.rows[1].value)));

  // Logged in: the clock cell and the "Clock is wrong" banner carry the same stamp.
  M.in.loggedIn = true;
  M.in.clockWrong = true;
  buildScreen(M.in, &s);
  CHECK(s.cellCount == 5);          // battery, noise, uptime, airtime, clock (the card's 6th slot is free)
  CHECK(s.bannerButton);
  CHECK(strncmp(s.banner, "Clock is wrong: ", 16) == 0);
  CHECK(fits(s.banner, sizeof(s.banner)));

  // The console: 8 lines, the newest kept, the input line intact.
  M.in.page = Page::Console;
  buildScreen(M.in, &s);
  CHECK(s.logLineCount == kConsoleLines);
  CHECK(strlen(s.consoleInput) == sizeof(s.consoleInput) - 1);
  CHECK(fits(s.consoleInput, sizeof(s.consoleInput)));

  // The pickers and the regions list at kMaxRegions: the row cap is never exceeded.
  M.in.page = Page::Regions;
  buildScreen(M.in, &s);
  CHECK(s.count <= kUiRowCap);
  CHECK(s.hasApply);
  M.in.page = Page::HomePicker;
  buildScreen(M.in, &s);
  CHECK(s.count <= kUiRowCap);
  M.in.page = Page::DefaultPicker;
  buildScreen(M.in, &s);
  CHECK(s.count <= kUiRowCap);
  M.in.page = Page::Neighbours;
  buildScreen(M.in, &s);
  CHECK(s.count <= kUiRowCap);
}

// The LIST is a screen state too: 8 remembered repeaters plus a full scan (kMaxFound answers), all
// with the longest names, into a 20-row page. The list's own buffers (name 32, signal 24) must hold
// what listRows() writes, and an unnamed answer must stay inside its 32 bytes.
static void testLargestList() {
  static FoundList found;
  found.begin(0x12345678u, 0);
  for (int i = 0; i < kMaxFound; ++i) {
    Found& f = found.rows[i];
    f.keyLen = 6;
    f.prefix[0] = (uint8_t)(0x80 + i);
    if (i % 2 == 0) snprintf(f.name, sizeof(f.name), "%s", kLongName);
    f.hasSnrIn = true;
    f.snrIn4 = -128;
    f.hasSnrOut = true;
    f.snrOut4 = 127;
    f.heardReal = true;
    f.heardWall = 0xFFFFFFFFu;
    f.heardMs = 0xFFFFFFFFu;
  }
  found.count = kMaxFound;
  static const char* remembered[8];
  static char rememberedBuf[8][32];
  for (int i = 0; i < 8; ++i) {
    snprintf(rememberedBuf[i], sizeof(rememberedBuf[i]), "%s", kLongName);
    remembered[i] = rememberedBuf[i];
  }
  static ListRow rows[20];
  const int n = listRows(found, remembered, 8, rows, 20);
  CHECK(n >= 8 && n <= 20);
  for (int i = 0; i < n; ++i) {
    CHECK(fits(rows[i].name, sizeof(rows[i].name)));
    CHECK(fits(rows[i].signal, sizeof(rows[i].signal)));
  }
  // The unnamed fallback ("unnamed 86a8ee") fits its 32 bytes.
  static char unnamed[32];
  const uint8_t prefix[6] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0xff};
  CHECK(unnamedText(prefix, unnamed, sizeof(unnamed)) > 0);
  CHECK(fits(unnamed, sizeof(unnamed)));
  CHECK(strcmp(unnamed, "unnamed deadbe") == 0);
  // The signal words at the extremes also fit a 24-byte list cell.
  static char sig[24];
  CHECK(signalText(true, 127, sig, sizeof(sig)));
  CHECK(fits(sig, sizeof(sig)));
  CHECK(signalText(true, -128, sig, sizeof(sig)));
  CHECK(fits(sig, sizeof(sig)));
}

int main() {
  testEveryPageLargest();
  testResetBetweenPages();
  testLayoutDefects();
  testLongestText();
  testLargestList();
  if (g_fail != 0) {
    printf("test_est363: %d FAILURE(S)\n", g_fail);
    return 1;
  }
  printf("test_est363: ok (15 pages x 3 repeater shapes, the full 20-row list, every buffer inside its"
         " array, layout bands separated, rows above the button)\n");
  return 0;
}
