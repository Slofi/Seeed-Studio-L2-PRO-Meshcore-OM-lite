// SPDX-License-Identifier: MIT
// EST-347: the Settings list arithmetic, the tab row and the radio prefs store, with the system g++.
//
// It proves, over the same headers the firmware compiles:
//   * the tab row: five equal taps that tile the width, every label measured with the mono advance
//     the font really has (test_fonts.py checks that number against the VLW blob), the gear-to-tab
//     mapping for all four main screens;
//   * a page as a list of row rectangles: the content height, the scroll clamp and give, and the
//     hit test at every edge (a row scrolled above the body is not tappable);
//   * the stepper keys: inside their row, never overlapping, and the +/- hit test;
//   * the manual-time stepper: real month lengths, leap years, and every wrap;
//   * the radio prefs store: seed from the build values, upstream's validation ranges, the preset
//     list and a persistence round trip through an injected backend.
// EST-350 adds two:
//   * the page history: General (scrolled) > Node > Name > Back > Back returns to General at the
//     same scroll, a full history drops its oldest step and an empty one means Back leaves Settings;
//   * the chat selector inside Settings: the Chat tab's own list and hit rule, the dropdown hanging
//     under its selector row, a pick changing only the chat Settings shows, and a tap outside
//     closing it and being SPENT.
#include <stdio.h>
#include <string.h>

#include "ui/chat_selector.h"
#include "ui/settings_layout.h"
#include "ui/settings_prefs.h"

using namespace om::ui;

static int g_fails = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL %s\n", what);
    ++g_fails;
  }
}

static SettingRow mk(SetRowKind kind, const char* label, const char* value = "", bool on = false) {
  SettingRow r;
  r.kind = kind;
  snprintf(r.label, sizeof(r.label), "%s", label);
  snprintf(r.value, sizeof(r.value), "%s", value);
  r.on = on;
  return r;
}

static void testTabs() {
  check(kSettingsTabCount == 5, "five settings tabs");
  check(strcmp(settingsTabLabel(SettingsTab::General), "General") == 0, "General label");
  check(strcmp(settingsTabLabel(SettingsTab::Nodes), "Nodes") == 0, "Nodes label");
  check(strcmp(settingsTabLabel(SettingsTab::Chat), "Chat") == 0, "Chat label");
  check(strcmp(settingsTabLabel(SettingsTab::Map), "Map") == 0, "Map label");
  check(strcmp(settingsTabLabel(SettingsTab::Rptr), "RPTR") == 0, "RPTR label");

  // The gear opens the tab of the screen it was pressed on (Nodes, Chat, Map, RPTR), and General is
  // one tap away from every one of them.
  check(settingsTabForMainTab(0) == SettingsTab::Nodes, "Nodes -> Nodes tab");
  check(settingsTabForMainTab(1) == SettingsTab::Chat, "Chat -> Chat tab");
  check(settingsTabForMainTab(2) == SettingsTab::Map, "Map -> Map tab");
  check(settingsTabForMainTab(3) == SettingsTab::Rptr, "RPTR -> RPTR tab");
  check(settingsTabForMainTab(-1) == SettingsTab::General, "no screen -> General");

  // The mono face advances 6 px per ASCII glyph (test_fonts.py checks the blob). 320 / 5 = 64 px
  // per tab, so the longest label, General (42 px + 10 px padding), fits with 12 px to spare.
  check(kSetMonoAdvance == 6, "mono advance is 6 px");
  for (int i = 0; i < kSettingsTabCount; ++i) {
    const SettingsTab t = (SettingsTab)i;
    const int labelW = kSetMonoAdvance * (int)strlen(settingsTabLabel(t));
    check(labelW + 2 * kSetTabLabelPadX <= 320 / kSettingsTabCount, "label fits at 320 px");
    check(settingsTabFits(t, 320), "settingsTabFits at 320");
    check(settingsTabFits(t, 800), "settingsTabFits at 800");
  }
  check(settingsTabLabelWidth(SettingsTab::General) == 42, "General is 7 mono glyphs = 42 px");

  const SettingsLayout L = settingsLayoutFor(320, 240, false);
  int x = 0;
  for (int i = 0; i < kSettingsTabCount; ++i) {
    check(L.tabs[i].x == x, "tabs tile from the left edge");
    check(L.tabs[i].y == 26 && L.tabs[i].h == 25, "tab row is under the 26 px status bar");
    check(L.tabs[i].w == 64, "each tab is 64 px at 320");
    x += L.tabs[i].w;
  }
  check(x == 320, "five tabs tile the whole width");
  check(L.tabrow.y == 26 && L.tabrow.h == 25, "tab row rect");
  check(L.title.y == 51 && L.title.h == 26, "title row follows the tab row");
  check(L.body.y == 77 && L.body.h == 163, "root body height");
  check(L.status.h == 26, "status bar untouched");

  check(settingsTabAtIn(L, 32, 38) == 0, "tap first tab");
  check(settingsTabAtIn(L, 96, 38) == 1, "tap second tab");
  check(settingsTabAtIn(L, 160, 38) == 2, "tap third tab");
  check(settingsTabAtIn(L, 224, 38) == 3, "tap fourth tab");
  check(settingsTabAtIn(L, 300, 38) == 4, "tap fifth tab");
  check(settingsTabAtIn(L, 300, 25) == -1, "status bar is not a tab");
  check(settingsTabAtIn(L, 300, 51) == -1, "title row is not a tab");

  const SettingsLayout S = settingsLayoutFor(320, 240, true);
  check(S.tabrow.empty(), "a sub-page has no tab row");
  check(S.title.y == 26 && S.body.y == 52 && S.body.h == 188, "sub-page gives the body 25 px more");
  check(settingsTabAtIn(S, 160, 38) == -1, "no tabs on a sub-page");
  check(S.backButton.x == 6 && S.backButton.y > S.title.y &&
            S.backButton.bottom() <= S.title.bottom(),
        "back key sits inside the title row");
  check(L.backButton.x == 6 && L.backButton.y > L.title.y &&
            L.backButton.bottom() <= L.title.bottom(),
        "root back key sits inside the title row");
  check(S.body.intersect(S.title).empty(), "body does not overlap the title");
}

static void testRows() {
  SettingRow rows[6];
  rows[0] = mk(SetRowKind::Section, "RADIO & MESH");
  rows[1] = mk(SetRowKind::Value, "Path hash", "2B");
  rows[2] = mk(SetRowKind::Chevron, "Node", "OM Lite");
  rows[3] = mk(SetRowKind::Warn, "A node with the wrong preset is deaf.");
  rows[4] = mk(SetRowKind::Stepper, "TX power", "22 dBm");
  rows[5] = mk(SetRowKind::Note, "No node list settings yet.");

  check(settingsRowHeight(SetRowKind::Section) == 20, "section 20 px");
  check(settingsRowHeight(SetRowKind::Value) >= 30, "row at least 30 px (the mock's min)");
  check(settingsRowHeight(SetRowKind::Warn) == 44, "warn box 44 px");
  check(settingsRowHeight(SetRowKind::Note) == 22, "note 22 px");

  const int total = 20 + 30 + 30 + 44 + 30 + 22;
  check(settingsContentHeight(rows, 6) == total, "content height is the sum of row heights");
  check(settingsRowTop(rows, 6, 0) == 0, "first row at 0");
  check(settingsRowTop(rows, 6, 1) == 20, "second row after the section");
  check(settingsRowTop(rows, 6, 6) == total, "end of content");

  const SettingsLayout L = settingsLayoutFor(320, 240, false);
  check(settingsMaxScroll(L, rows, 6) == total - 163, "root max scroll");
  const SettingsLayout S = settingsLayoutFor(320, 240, true);
  check(settingsMaxScroll(S, rows, 6) == 0, "a sub-page with 176 px of rows does not scroll");

  // Rectangles are contiguous and start on the body's top edge; the tap test uses the same ones.
  for (int i = 0; i < 6; ++i) {
    const Rect r = settingsRowRect(L, rows, 6, i, 0);
    check(r.x == 0 && r.w == 320, "row spans the width");
    check(r.y == L.body.y + settingsRowTop(rows, 6, i), "row top follows the content height");
    check(r.h == settingsRowHeight(rows[i].kind), "row height");
    if (i > 0) {
      check(r.y == settingsRowRect(L, rows, 6, i - 1, 0).bottom(), "rows share an edge");
    }
    const Rect vis = settingsRowVisible(L, rows, 6, i, 0);
    check(settingsRowAtIn(L, rows, 6, 10, vis.y + 1, 0) == i, "tap near the row top");
    check(settingsRowAtIn(L, rows, 6, 10, vis.bottom() - 1, 0) == i, "tap near the row bottom");
  }

  // A row scrolled half above the body: its visible part answers, the hidden part does not, and a
  // tap under the status bar / on the title can never select anything.
  const int scroll = 13;
  const Rect r0 = settingsRowVisible(L, rows, 6, 0, scroll);
  check(r0.y == L.body.y && r0.h == 20 - scroll, "row 0 is clipped at the body's top");
  check(settingsRowAtIn(L, rows, 6, 10, L.body.y, scroll) == 0, "visible part of row 0");
  check(settingsRowAtIn(L, rows, 6, 10, L.body.y - 1, scroll) == -1, "above the body is dead");
  check(settingsRowAtIn(L, rows, 6, 10, L.title.y + 1, scroll) == -1, "title row is dead");
  check(settingsRowAtIn(L, rows, 6, 10, L.body.bottom(), scroll) == -1, "below the body is dead");
  check(settingsRowAtIn(L, rows, 6, 10, L.body.bottom() - 1, 12) >= 0,
        "the last visible pixel still answers");

  // The shared drag rule: content follows the finger, 10 px of give past either end, snapped back.
  check(settingsDragScroll(0, 5, 13) == 5, "drag up scrolls down");
  check(settingsDragScroll(5, -10, 13) == -5, "drag down scrolls back up");
  check(settingsDragScroll(0, -100, 13) == -kSelGive, "give above the top");
  check(settingsDragScroll(0, 100, 13) == 13 + kSelGive, "give past the end");
  check(settingsSnapScroll(-10, 13) == 0, "snap top");
  check(settingsSnapScroll(23, 13) == 13, "snap end");
  check(settingsSnapScroll(7, 13) == 7, "no snap inside");

  // A page that fits cannot scroll; a short page has maxScroll 0.
  check(settingsMaxScroll(S, rows, 2) == 0, "short page has no scroll");
}

static void testStepper() {
  SettingRow rows[1];
  rows[0] = mk(SetRowKind::Stepper, "TX power", "22 dBm");
  const SettingsLayout L = settingsLayoutFor(320, 240, true);
  const Rect row = settingsRowRect(L, rows, 1, 0, 0);
  const Rect minus = settingsStepMinusRect(row);
  const Rect plus = settingsStepPlusRect(row);
  const Rect value = settingsStepValueRect(row);

  check(row.contains(minus.x, minus.y) && row.contains(minus.right() - 1, minus.bottom() - 1),
        "minus key inside its row");
  check(row.contains(plus.x, plus.y) && row.contains(plus.right() - 1, plus.bottom() - 1),
        "plus key inside its row");
  check(!minus.overlaps(plus), "the two keys do not overlap");
  check(!minus.overlaps(value) && !plus.overlaps(value), "the number is between the keys");
  check(minus.right() < value.x && value.right() < plus.x, "minus, value, plus in that order");
  check(minus.w >= 18 && plus.w >= 18, "keys are finger sized");
  check(settingsStepperPart(row, minus.x + 1, minus.y + 1) == -1, "minus hit");
  check(settingsStepperPart(row, plus.x + 1, plus.y + 1) == 1, "plus hit");
  check(settingsStepperPart(row, value.x + 1, value.y + 1) == 0, "the number is not a key");
  check(settingsStepperPart(row, row.x + 2, row.y + 2) == 0, "the label is not a key");
}

static void testManualTime() {
  check(civilMonthDays(2026, 1) == 31, "January 31");
  check(civilMonthDays(2026, 4) == 30, "April 30");
  check(civilMonthDays(2025, 2) == 28, "February 2025 28");
  check(civilMonthDays(2024, 2) == 29, "February 2024 leap 29");
  check(civilMonthDays(2000, 2) == 29, "2000 is a leap year");
  check(civilMonthDays(2100, 2) == 28, "2100 is not a leap year");

  CivilTime t{2026, 1, 31, 23, 59};
  t = stepCivilTime(t, TimeField::Minute, 1);
  check(t.minute == 0 && t.hour == 23, "minute 59 wraps to 0, hour untouched");
  t = stepCivilTime(t, TimeField::Minute, -1);
  check(t.minute == 59, "minute 0 wraps back to 59");
  t = stepCivilTime(t, TimeField::Hour, 1);
  check(t.hour == 0, "hour 23 wraps to 0");
  t = stepCivilTime(t, TimeField::Hour, -1);
  check(t.hour == 23, "hour 0 wraps back to 23");

  CivilTime d{2026, 1, 31, 12, 0};
  d = stepCivilTime(d, TimeField::Day, 1);
  check(d.day == 1 && d.month == 1, "day past the 31st wraps to the 1st");
  d = stepCivilTime(d, TimeField::Day, -1);
  check(d.day == 31, "day 1 backwards wraps to the month's last");
  CivilTime feb{2026, 2, 28, 12, 0};
  check(stepCivilTime(feb, TimeField::Day, 1).day == 1, "28 Feb 2026 wraps");
  check(stepCivilTime(feb, TimeField::Day, -1).day == 27, "28 Feb backwards is 27");

  CivilTime m{2026, 12, 31, 0, 0};
  check(stepCivilTime(m, TimeField::Month, 1).month == 1, "December + 1 = January");
  check(stepCivilTime(m, TimeField::Month, -1).month == 11, "December - 1 = November");
  CivilTime jan{2026, 1, 31, 0, 0};
  const CivilTime janToFeb = stepCivilTime(jan, TimeField::Month, 1);
  check(janToFeb.month == 2 && janToFeb.day == 28, "31 Jan + 1 month = 28 Feb");
  CivilTime janLeap{2024, 1, 31, 0, 0};
  check(stepCivilTime(janLeap, TimeField::Month, 1).day == 29, "31 Jan 2024 + 1 month = 29 Feb");

  CivilTime y{2099, 12, 31, 0, 0};
  check(stepCivilTime(y, TimeField::Year, 1).year == 2000, "year 2099 wraps to 2000");
  check(stepCivilTime(y, TimeField::Year, -1).year == 2098, "year 2099 backwards is 2098");
  CivilTime low{2000, 1, 1, 0, 0};
  check(stepCivilTime(low, TimeField::Year, -1).year == 2099, "year 2000 backwards wraps to 2099");

  CivilTime leap{2024, 2, 29, 0, 0};
  check(stepCivilTime(leap, TimeField::Year, 1).day == 28, "29 Feb + 1 year = 28 Feb");
  check(stepCivilTime(leap, TimeField::Year, 4).day == 29, "29 Feb 2028 is still a leap day");
  check(stepCivilTime(leap, TimeField::Year, 0).day == 29, "dir 0 changes nothing");

  const CivilTime same = stepCivilTime(leap, TimeField::Count, 1);
  check(same.year == 2024 && same.month == 2 && same.day == 29, "unknown field is a no-op");
}

// ---------------------------------------------------------------------------------------- radio store
struct FakePrefs {
  RadioPrefs mem;
  bool have = false;
  int writes = 0;
  bool writeFails = false;
};

static bool fakeRead(void* ctx, RadioPrefs& out) {
  FakePrefs* f = (FakePrefs*)ctx;
  if (!f->have) return false;
  out = f->mem;
  return true;
}

static bool fakeWrite(void* ctx, const RadioPrefs& p) {
  FakePrefs* f = (FakePrefs*)ctx;
  f->writes++;
  if (f->writeFails) return false;
  f->mem = p;
  f->have = true;
  return true;
}

static void testRadio() {
  const RadioPrefs seed = seedRadioPrefs();
  check(seed.freqKhz == 869618 && seed.bwHz == 62500 && seed.sf == 8 && seed.cr == 8 &&
            seed.tx == 22,
        "seed is the build plan (869.618 MHz, BW 62.5, SF8, CR8, 22 dBm)");
  check(radioPrefsValid(seed), "the seed is valid");
  check(radioPresetIndex(seed) == 0, "the first preset IS the current SLO values");
  const RadioPreset first = radioPresetAt(0);
  check(strcmp(first.name, "EU 868 (SLO)") == 0, "first preset is named for SLO");
  check(first.prefs.freqKhz == seed.freqKhz && first.prefs.bwHz == seed.bwHz &&
            first.prefs.sf == seed.sf && first.prefs.cr == seed.cr,
        "first preset carries the SLO values");
  check(strcmp(first.desc, "869.618 MHz / SF8 / BW62.5 / CR8") == 0, "first preset description");
  check(radioPresetAt(1).prefs.sf == 7 && radioPresetAt(1).prefs.cr == 6,
        "the mock's EU868 row follows");

  RadioPrefs p = seed;
  check(radioPrefsValid(p), "seed valid");
  p.freqKhz = kRadioFreqMin - 1;
  check(!radioPrefsValid(p), "freq below 150 MHz refused");
  p = seed; p.freqKhz = kRadioFreqMax + 1;
  check(!radioPrefsValid(p), "freq above 2500 MHz refused");
  p = seed; p.freqKhz = kRadioFreqMin;
  check(radioPrefsValid(p), "freq at the low edge accepted");
  p.freqKhz = kRadioFreqMax;
  check(radioPrefsValid(p), "freq at the high edge accepted");
  p = seed; p.bwHz = kRadioBwMin - 1;
  check(!radioPrefsValid(p), "bw below 7 kHz refused");
  p = seed; p.bwHz = kRadioBwMax + 1;
  check(!radioPrefsValid(p), "bw above 500 kHz refused");
  p = seed; p.sf = 4;
  check(!radioPrefsValid(p), "SF4 refused");
  p.sf = 13;
  check(!radioPrefsValid(p), "SF13 refused");
  p = seed; p.cr = 4;
  check(!radioPrefsValid(p), "CR4 refused");
  p.cr = 9;
  check(!radioPrefsValid(p), "CR9 refused");
  p = seed; p.tx = -10;
  check(!radioPrefsValid(p), "tx below -9 refused");
  p.tx = 23;
  check(!radioPrefsValid(p), "tx above 22 refused");
  p.tx = -9;
  check(radioPrefsValid(p), "tx -9 accepted");
  p.tx = 22;
  check(radioPrefsValid(p), "tx 22 accepted");
  check(clampTxPower(-100) == -9 && clampTxPower(100) == 22 && clampTxPower(10) == 10,
        "tx clamp");

  char buf[16];
  radioFreqText(seed, buf, sizeof(buf));
  check(strcmp(buf, "869.618") == 0, "frequency text 869.618");
  radioBwText(seed, buf, sizeof(buf));
  check(strcmp(buf, "62.5") == 0, "bandwidth text 62.5");
  RadioPrefs exact = seed;
  exact.freqKhz = 915000;
  exact.bwHz = 125000;
  radioFreqText(exact, buf, sizeof(buf));
  check(strcmp(buf, "915") == 0, "whole MHz has no trailing dot");
  radioBwText(exact, buf, sizeof(buf));
  check(strcmp(buf, "125") == 0, "whole kHz has no trailing dot");

  // A failed first read seeds from the build values and writes them once.
  FakePrefs f;
  RadioPrefsStore store;
  store.begin(fakeRead, fakeWrite, &f);
  check(store.load() == true, "first load reports the seed");
  check(f.have && f.writes == 1, "seed was written once");
  const RadioPrefs seeded = f.mem;
  check(seeded.freqKhz == seed.freqKhz && seeded.bwHz == seed.bwHz && seeded.sf == seed.sf &&
            seeded.cr == seed.cr && seeded.tx == seed.tx,
        "seed written is the build plan");
  check(store.load() == false, "second load finds a valid store");
  check(f.writes == 1, "a valid store is not rewritten");

  // Edits write through and read back (the round trip the device does via NodePrefs).
  check(store.setRadio(869618, 62500, 7, 6), "setRadio accepts a valid preset");
  FakePrefs g;
  RadioPrefsStore store2;
  store2.begin(fakeRead, fakeWrite, &g);
  g = f;   // the same persisted bytes
  store2.load();
  check(store2.prefs().sf == 7 && store2.prefs().cr == 6, "round trip keeps SF and CR");
  check(!store.setRadio(1000, 62500, 8, 8), "setRadio refuses a frequency off the band");
  check(store.prefs().freqKhz == 869618, "a refused edit changes nothing");
  check(!store.setRadio(869618, 62500, 3, 8), "setRadio refuses SF3");
  check(store.prefs().sf == 7, "the refused SF edit left SF7 in place");

  check(store.setTx(60), "setTx accepts and clamps");
  check(store.prefs().tx == 22, "tx clamped to 22");
  store.setTx(-60);
  check(store.prefs().tx == -9, "tx clamped to -9");

  store.setTx(10);
  check(store.setPreset(2), "setPreset works");
  check(store.prefs().freqKhz == radioPresetAt(2).prefs.freqKhz, "preset 2 applied");
  check(store.prefs().tx == 10, "a preset keeps the TX power row");
  check(radioPresetIndex(store.prefs()) == 2, "preset index found again");
  store.setPreset(1);
  check(radioPresetIndex(store.prefs()) == 1, "preset 1 round trips");
  check(store.setPreset(om::ui::kRadioCustomPreset) == false, "the Custom row is not a preset");

  // A corrupt persisted record is replaced by the seed, never trusted.
  FakePrefs bad;
  bad.have = true;
  bad.mem = seed;
  bad.mem.sf = 99;
  RadioPrefsStore store3;
  store3.begin(fakeRead, fakeWrite, &bad);
  check(store3.load() == true, "an invalid record is reseeded");
  check(bad.mem.sf == 8 && bad.writes == 1, "the invalid SF was replaced and saved");

  // A write failure is reported but cannot leave the in-RAM values invalid.
  FakePrefs noWrite;
  noWrite.writeFails = true;
  RadioPrefsStore store4;
  store4.begin(fakeRead, fakeWrite, &noWrite);
  store4.load();
  check(store4.prefs().freqKhz == seed.freqKhz, "RAM values are still the seed");
  check(store4.setRadio(869618, 62500, 7, 6) == false, "a failed save is reported");
  check(store4.prefs().sf == 7, "the edit is kept in RAM even when the save failed");
}


// --------------------------------------------------------------------------------- EST-350: history
// The shell's two ends (setGo / setBack in main.cpp) are one push and one pop of this stack; the test
// drives the same calls in the same order the taps make them.
static void testNav() {
  check(kSetPageCount >= 20, "every page has an index");
  check(settingsRootPage(SetPage::GeneralRoot), "General is a root");
  check(settingsRootPage(SetPage::NodesRoot) && settingsRootPage(SetPage::ChatRoot) &&
            settingsRootPage(SetPage::MapRoot) && settingsRootPage(SetPage::RptrRoot),
        "the five tabs are roots");
  check(!settingsRootPage(SetPage::Node) && !settingsRootPage(SetPage::NameEdit) &&
            !settingsRootPage(SetPage::Channel) && !settingsRootPage(SetPage::SoundPicker),
        "a row's page is not a root");

  SettingsNav nav;
  SetPage page = SetPage::GeneralRoot;
  int scroll = 0;
  check(nav.empty(), "a fresh Settings has no history");
  check(!nav.pop(&page, &scroll), "Back on a root has no history: it leaves Settings");
  check(page == SetPage::GeneralRoot && scroll == 0, "a failed pop writes nothing");

  // General, scrolled down 40 px (its list is taller than the body), > Node > Name > Back > Back.
  int sc = 40;
  nav.push(page, sc);            // setGo(Node)
  page = SetPage::Node;
  sc = 0;
  check(nav.depth() == 1, "one step away from General");
  nav.push(page, sc);            // setGo(NameEdit), from the Node page's Name row
  page = SetPage::NameEdit;
  check(nav.depth() == 2, "two steps: General > Node > Name");
  check(nav.pop(&page, &sc), "Back from the name editor has a step");
  check(page == SetPage::Node && sc == 0, "Back from Name lands on the Node page");
  check(nav.pop(&page, &sc), "Back from the Node page has a step");
  check(page == SetPage::GeneralRoot && sc == 40,
        "Back from Node lands on General at the SAME scroll position");
  check(nav.empty(), "General is the bottom of the history again");

  // A page can be left and re-entered: every step keeps its own scroll.
  SettingsNav nav2;
  nav2.push(SetPage::GeneralRoot, 12);
  nav2.push(SetPage::Channel, 30);
  SetPage p2 = SetPage::GeneralRoot;
  int s2 = 0;
  check(nav2.pop(&p2, &s2) && p2 == SetPage::Channel && s2 == 30, "the Channel page's scroll");
  check(nav2.pop(&p2, &s2) && p2 == SetPage::GeneralRoot && s2 == 12, "General's scroll again");

  // Overflow: a long walk keeps the NEWEST steps, so Back still has a way up.
  SettingsNav deep;
  for (int i = 0; i < SettingsNav::kMaxDepth + 4; ++i) {
    deep.push(i % 2 == 0 ? SetPage::GeneralRoot : SetPage::Node, i);
  }
  check(deep.depth() == SettingsNav::kMaxDepth, "the history is bounded");
  SetPage dp = SetPage::GeneralRoot;
  int ds = 0;
  check(deep.pop(&dp, &ds), "the newest step survives the overflow");
  const int newest = SettingsNav::kMaxDepth + 3;
  check(ds == newest, "the top of the history is the last push");

  nav.reset();
  check(nav.empty(), "a new door into Settings clears the history");
}

// ------------------------------------------------------------------------------- EST-350: selector
// Two channels and one DM, drawn with the Settings layout's rectangles (the Chat tab's list is the
// same list, and test_chat.cpp proves its geometry).
static void testSettingsSelector() {
  const SettingsLayout closed = settingsLayoutFor(320, 240, false);
  check(closed.selPanel.empty() && closed.selList.empty() && closed.selManage.empty(),
        "no dropdown rectangles while it is closed");

  const SettingsLayout L = settingsLayoutFor(320, 240, false, true);
  const int rowH = settingsRowHeight(SetRowKind::Selector);
  check(rowH >= 30, "the selector row is a list row");
  check(L.selPanel.empty() == false, "the dropdown has a panel when open");
  check(L.selPanel.y == L.body.y + rowH, "it hangs under the selector row, which stays visible");
  check(L.selPanel.w == L.body.w && L.selPanel.x == L.body.x, "the panel spans the body");
  check(L.selPanel.bottom() == L.body.bottom(), "the panel reaches the body's floor");
  check(L.selManage.h == kSelManageH, "the pinned row is the chat's own height");
  check(L.selList.y == L.selPanel.y && L.selList.bottom() == L.selManage.y,
        "the list is the panel above the pinned row");
  check(!L.selList.overlaps(L.selManage), "the pinned row is not part of the list");
  // 320x240: the body is 163 px, the selector row takes 30 and the pinned row 30, so the list is
  // 103 px: three 30 px chat rows and the two 15 px section labels fit, four rows do not.
  check(L.selList.h == L.body.h - rowH - kSelManageH, "the list is what the body has left");
  check(L.selList.h >= 3 * kSelRowH, "on the L2 at least three chats fit without scrolling");

  SelectorModel m;
  m.chatCount = 3;
  m.channelCount = 2;
  m.dmCount = 1;
  m.selected = 1;   // the chat Settings is showing (the Chat tab is on 0, deliberately different)
  m.scroll = 0;

  // The content and the clamp are the Chat tab's own arithmetic.
  check(selContentHeight(2, 1) == kSelSectionH + 2 * kSelRowH + kSelSectionH + kSelRowH,
        "two channels and one DM");
  const int maxScroll = selectorMaxScroll(m, L.selList);
  check(maxScroll == selContentHeight(2, 1) - L.selList.h,
        "the clamp is the list's content height over its window (the chat's own arithmetic)");
  check(maxScroll > 0, "two channels and a DM are 17 px taller than the window");

  // Rows answer where they are drawn.
  const int firstTop = selRowTop(L.selList, 2, 1, 0, 0);
  const SelectorPress r0 = selectorPress(m, L.selPanel, L.selList, L.selManage, 100,
                                         firstTop + kSelRowH / 2);
  check(r0.picks && r0.row == 0 && !r0.consumes, "a tap on a row picks that chat");
  // The "Channels" section label is not a row: it closes the dropdown and spends the gesture.
  const SelectorPress lab = selectorPress(m, L.selPanel, L.selList, L.selManage, 100,
                                          L.selList.y + 2);
  check(!lab.picks && lab.consumes && lab.kind == SelectorHitKind::Close,
        "a section label closes the dropdown and is spent");

  // The DM row is the last entry: scroll the list to its floor, as a finger would, then tap it.
  m.scroll = maxScroll;
  const int dmTop = selRowTop(L.selList, 2, 1, 2, m.scroll);
  const SelectorPress r2 = selectorPress(m, L.selPanel, L.selList, L.selManage, 100,
                                         dmTop + kSelRowH / 2);
  check(r2.picks && r2.row == 2, "the DM row is entry 2");
  const SelectorPress man = selectorPress(m, L.selPanel, L.selList, L.selManage, 100,
                                          L.selManage.y + kSelManageH / 2);
  check(man.kind == SelectorHitKind::Manage && man.consumes && !man.picks,
        "the pinned row opens the channel list");
  const SelectorPress outside = selectorPress(m, L.selPanel, L.selList, L.selManage, 100, 60);
  check(outside.kind == SelectorHitKind::Close && outside.consumes && !outside.picks,
        "a tap outside closes it and is consumed (nothing behind is pressed)");
  const SelectorPress title = selectorPress(m, L.selPanel, L.selList, L.selManage, 25,
                                            L.title.y + 4);
  check(title.consumes, "Back while it is open is the same consumed close");

  // The pick changes the chat SETTINGS shows and nothing else.
  int shown = 1;
  const int openChat = 0;   // the Chat tab's open chat, a different variable on purpose
  selectorPickShown(r2, &shown);
  check(shown == 2, "the picked chat becomes the one Settings shows");
  check(openChat == 0, "the Chat screen's open chat is untouched by a pick in Settings");
  selectorPickShown(lab, &shown);
  check(shown == 2, "a close is not a pick");
  selectorPickShown(man, &shown);
  check(shown == 2, "the pinned row is not a pick");

  // The same model scrolls with the finger: the chat's own drag rule.
  SelectorModel tall;
  tall.chatCount = 10;
  tall.channelCount = 10;
  tall.dmCount = 0;
  const int tallMax = selectorMaxScroll(tall, L.selList);
  check(tallMax > 0, "ten channels need scrolling at 320x240");
  const int dragged = selDragScroll(0, 24, tallMax);
  check(dragged == 24, "the list follows the finger");
  check(selRowAt(L.selList, 10, 0, dragged, selRowTop(L.selList, 10, 0, 2, dragged)) == 2,
        "the row under the same finger after the drag is the one drawn there");
}

int main() {
  testTabs();
  testRows();
  testStepper();
  testManualTime();
  testRadio();
  testNav();
  testSettingsSelector();
  if (g_fails == 0) printf("settings: all checks passed\n");
  return g_fails == 0 ? 0 : 1;
}
