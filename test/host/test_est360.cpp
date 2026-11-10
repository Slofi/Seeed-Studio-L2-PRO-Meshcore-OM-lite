// EST-360 host test: the region/clock rework.
//   - the region-name rule (MeshCore's RegionMap::is_name_char), the filter and its limit
//   - every region CLI line, verbatim, including the wildcard/None choices and the parent form
//   - the anonymous nearby-regions reply: clock + list, '*', a real empty answer, garbage, short
//   - the clock guard (our clock unset, the one-day ceiling, the repeater ahead), the clock reply
//     parser and the human difference
//   - battery volts (two decimals, n/a outside 2.5..30 V) and the raw detail string
//   - the keyboard's RptrRegion field offers only legal bytes
// Run by test/host/run.sh (built with the system g++).
#include <stdio.h>
#include <string.h>

#include "rptr_model.h"
#include "ui/chat_layout.h"

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

// --- the region-name rule -----------------------------------------------------------------------
static void testRegionNames() {
  // RegionMap::is_name_char: '-', '$', '#', digits, or any byte >= 'A'. ASCII letters both cases.
  for (char c = 'A'; c <= 'Z'; ++c) CHECK(regionNameChar(c));
  for (char c = 'a'; c <= 'z'; ++c) CHECK(regionNameChar(c));
  for (char c = '0'; c <= '9'; ++c) CHECK(regionNameChar(c));
  CHECK(regionNameChar('-'));
  CHECK(regionNameChar('$'));
  CHECK(regionNameChar('#'));
  CHECK(!regionNameChar(' '));
  CHECK(!regionNameChar('.'));
  CHECK(regionNameChar('_'));      // '_' is 0x5F, above 'A': MeshCore accepts it
  CHECK(regionNameChar('['));      // 0x5B and 0x60 are legal too
  CHECK(regionNameChar('`'));
  CHECK(!regionNameChar('*'));
  CHECK(!regionNameChar('/'));
  CHECK(!regionNameChar('@'));      // 0x40 is below 'A'
  CHECK(!regionNameChar(':'));
  CHECK(!regionNameChar('+'));
  CHECK(!regionNameChar('\t'));

  CHECK(regionNameValid("si"));
  CHECK(regionNameValid("si-gor"));
  CHECK(regionNameValid("#lj"));
  CHECK(regionNameValid("$private"));
  CHECK(!regionNameValid(""));
  CHECK(!regionNameValid("si gor"));        // a space is never part of a name
  CHECK(!regionNameValid("si.gor"));
  CHECK(!regionNameValid("*"));             // the wildcard already exists; putRegion('*') refuses
  CHECK(!regionNameValid("1234567890123456789012345678901"));   // 31 chars, one too many
  CHECK(regionNameValid("123456789012345678901234567890"));     // 30 is the last legal length

  char out[40];
  CHECK(regionNameFilter(" si-lj#1 ", out, sizeof(out)) == 7);
  CHECK_TEXT(out, "si-lj#1");
  CHECK(regionNameFilter("abcdefghijklmnopqrstuvwxyz0123456789", out, sizeof(out)) ==
        kRegionNameMax);
  CHECK_TEXT(out, "abcdefghijklmnopqrstuvwxyz0123");
  CHECK(regionNameFilter("", out, sizeof(out)) == 0);
  CHECK_TEXT(out, "");
  regionNameFilter(nullptr, out, sizeof(out));   // no crash, empty
  CHECK_TEXT(out, "");
}

// --- every CLI line -----------------------------------------------------------------------------
static void testCliLines() {
  char line[96];

  CHECK(cliRegionPut(line, sizeof(line), "lj", nullptr) == 13);
  CHECK_TEXT(line, "region put lj");
  CHECK(cliRegionPut(line, sizeof(line), "lj", "si") == 16);
  CHECK_TEXT(line, "region put lj si");
  CHECK(cliRegionPut(line, sizeof(line), "new", "") == 14);
  CHECK_TEXT(line, "region put new");
  CHECK(cliRegionPut(line, sizeof(line), "new", "*") == 16);
  CHECK_TEXT(line, "region put new *");
  // A bad name or parent is refused whole, never truncated into a legal-looking line.
  line[0] = 'x';
  CHECK(cliRegionPut(line, sizeof(line), "bad name", nullptr) == 0);
  CHECK(line[0] == 0);
  CHECK(cliRegionPut(line, sizeof(line), "*", nullptr) == 0);
  CHECK(cliRegionPut(line, sizeof(line), "ok", "bad parent") == 0);
  // A buffer too small writes nothing.
  char small[8];
  CHECK(cliRegionPut(small, sizeof(small), "longname", nullptr) == 0);
  CHECK(small[0] == 0);

  cliRegionFlood(line, sizeof(line), "lj", true);
  CHECK_TEXT(line, "region allowf lj");
  cliRegionFlood(line, sizeof(line), "lj", false);
  CHECK_TEXT(line, "region denyf lj");
  cliRegionFlood(line, sizeof(line), "*", false);
  CHECK_TEXT(line, "region denyf *");
  cliRegionHome(line, sizeof(line), "lj");
  CHECK_TEXT(line, "region home lj");
  cliRegionDefault(line, sizeof(line), "si");
  CHECK_TEXT(line, "region default si");
  cliRegionRemove(line, sizeof(line), "lj");
  CHECK_TEXT(line, "region remove lj");
  cliRegionHomeNone(line, sizeof(line));
  CHECK_TEXT(line, "region home *");
  cliRegionDefaultNone(line, sizeof(line));
  CHECK_TEXT(line, "region default <null>");
  cliTime(line, sizeof(line), 1762790400u);
  CHECK_TEXT(line, "time 1762790400");
  CHECK_TEXT(kCliRegionSave, "region save");
  CHECK_TEXT(kCliRegionList, "region");
  CHECK_TEXT(kCliRegionHomeRead, "region home");
  CHECK_TEXT(kCliRegionDefaultRead, "region default");
  CHECK_TEXT(kCliClockRead, "clock");
}

// --- the repeater's own answers ------------------------------------------------------------------
static void testReplyParsers() {
  char name[32];
  CHECK(regionHomeReplyParse(" home is lj", name, sizeof(name)));
  CHECK_TEXT(name, "lj");
  CHECK(regionHomeReplyParse(" home is *", name, sizeof(name)));
  CHECK_TEXT(name, "*");
  CHECK(!regionHomeReplyParse("Err - unknown region", name, sizeof(name)));
  CHECK(!regionHomeReplyParse("", name, sizeof(name)));
  CHECK(!regionHomeReplyParse(nullptr, name, sizeof(name)));

  CHECK(regionDefaultReplyParse(" default scope is si", name, sizeof(name)));
  CHECK_TEXT(name, "si");
  CHECK(regionDefaultReplyParse(" default scope is <null>", name, sizeof(name)));
  CHECK_TEXT(name, "");
  CHECK(!regionDefaultReplyParse(" default scope is", name, sizeof(name)));
  CHECK(!regionDefaultReplyParse("Err - ??", name, sizeof(name)));
}

// --- the anonymous nearby-regions reply ---------------------------------------------------------
static void testAnonRegions() {
  AnonRegions a;
  // MyMesh strips the 4-byte tag, so the buffer starts at the clock (4 B LE) + the comma list.
  uint8_t buf[64];
  const uint32_t clock = 1762790400u;
  memcpy(buf, &clock, 4);
  memcpy(&buf[4], "si,lj,*", 7);
  CHECK(anonRegionsParse(buf, 11, &a));
  CHECK(a.valid);
  CHECK(a.clock == clock);
  CHECK(a.clockValid);
  CHECK(a.count == 3);
  CHECK_TEXT(a.text, "si, lj, *");
  CHECK(a.clean);

  // A real empty answer (the repeater answered with 8 bytes: clock + no names).
  CHECK(anonRegionsParse(buf, 4, &a));
  CHECK(a.valid && a.count == 0);
  CHECK_TEXT(a.text, "");

  // A NUL terminator inside the length is accepted (an older note described this shape).
  memcpy(&buf[4], "si,lj", 5);
  buf[9] = 0;
  CHECK(anonRegionsParse(buf, 12, &a));
  CHECK(a.count == 2);
  CHECK_TEXT(a.text, "si, lj");
  CHECK(a.clean);

  // Garbage: header residue or a mangled token is dropped, and the answer says it was not clean.
  memcpy(&buf[4], "si;j*,lj", 8);
  CHECK(anonRegionsParse(buf, 12, &a));
  CHECK(a.valid);
  CHECK(a.count == 1);           // "si;j*" is one invalid token; only "lj" survives
  CHECK_TEXT(a.text, "lj");
  CHECK(!a.clean);

  // A short reply cannot be this answer at all.
  CHECK(!anonRegionsParse(buf, 3, &a));
  CHECK(!a.valid);
  CHECK(!anonRegionsParse(nullptr, 10, &a));

  // The clock field must be plausible to be used; the list still parses. Argus's 2024-05-15
  // MeshCore restart clock is below kMinSaneEpoch, so it is not real, and the label says the year.
  const uint32_t zero = 0;
  memcpy(buf, &zero, 4);
  memcpy(&buf[4], "*", 1);
  CHECK(anonRegionsParse(buf, 5, &a));
  CHECK(!a.clockValid);
  CHECK(a.count == 1);
  CHECK_TEXT(a.text, "*");
  const uint32_t restart2024 = 1715779092u;
  memcpy(buf, &restart2024, 4);
  CHECK(anonRegionsParse(buf, 5, &a));
  CHECK(a.clock == restart2024);
  CHECK(!a.clockValid);
  char year[24];
  clockText(year, sizeof(year), restart2024);
  CHECK_TEXT(year, "not set (2024)");

  // More names than the card keeps: the first 12 are shown, clean goes false.
  char body[8 * 12];
  int at = 0;
  for (int i = 0; i < 14; ++i) {
    at += snprintf(body + at, sizeof(body) - at, "r%d,", i);
  }
  body[at - 1] = 0;
  memcpy(buf, &clock, 4);
  memcpy(&buf[4], body, strlen(body));
  CHECK(anonRegionsParse(buf, 4 + (int)strlen(body), &a));
  CHECK(a.count == kMaxRegions);
  CHECK(!a.clean);
}

// --- the clock guard ----------------------------------------------------------------------------
static void testTimeGuard() {
  const uint32_t our = 1762790400u;
  // Our clock unset: always refused, whatever the repeater says.
  CHECK(clockGuard(our, false, false, 0) == ClockVerdict::OurClockNotSet);
  CHECK(clockGuard(0, true, true, our) == ClockVerdict::OurClockNotSet);
  CHECK(clockGuard(1234567u, true, true, our) == ClockVerdict::OurClockNotSet);
  CHECK_TEXT(clockVerdictText(ClockVerdict::OurClockNotSet), "our clock is not set");

  // EST-362: an unset repeater clock (or the 2024 restart clock) is a one-way jump over a day, so
  // the page asks first with both dates; it is no longer refused.
  CHECK(clockGuard(our, true, true, 0) == ClockVerdict::NeedConfirm);
  CHECK(clockGuard(our, true, false, 0) == ClockVerdict::NeedConfirm);
  CHECK(clockGuard(our, true, true, 1715779092u) == ClockVerdict::NeedConfirm);

  // The one-day boundary: exactly 86400 behind applies directly, 86401 asks first.
  CHECK(clockGuard(our, true, true, our - 86400u) == ClockVerdict::Ok);
  CHECK(clockGuard(our, true, true, our - 86401u) == ClockVerdict::NeedConfirm);
  CHECK(clockGuard(our, true, true, our - 100000u) == ClockVerdict::NeedConfirm);

  // A repeater slightly ahead is fine (up to a minute); more is the only refusal.
  CHECK(clockGuard(our, true, true, our + 59u) == ClockVerdict::Ok);
  CHECK(clockGuard(our, true, true, our + 60u) == ClockVerdict::Ok);
  CHECK(clockGuard(our, true, true, our + 61u) == ClockVerdict::RepeaterAhead);
  CHECK_TEXT(clockVerdictText(ClockVerdict::RepeaterAhead),
             "the repeater's clock is ahead of ours");

  char t[32];
  clockAgoText(t, sizeof(t), 0);
  CHECK_TEXT(t, "in sync");
  clockAgoText(t, sizeof(t), 1);
  CHECK_TEXT(t, "in sync");
  clockAgoText(t, sizeof(t), 3);
  CHECK_TEXT(t, "3s behind");
  clockAgoText(t, sizeof(t), -125);
  CHECK_TEXT(t, "2m 05s ahead");
  clockAgoText(t, sizeof(t), 3725);
  CHECK_TEXT(t, "1h 02m behind");
  clockAgoText(t, sizeof(t), 90000);
  CHECK_TEXT(t, "1d 01h behind");
}

// CommonCLI's `clock` answer: "14:02 - 10/11/2026 UTC" (UTC, no zone work).
static void testClockReply() {
  uint32_t epoch = 0;
  // 2026-11-10 14:02 UTC = 1794319320 (checked with date -u -d @1794319320).
  CHECK(clockReplyParse("14:02 - 10/11/2026 UTC", &epoch));
  CHECK(epoch == 1794319320u);
  CHECK(clockReplyParse("00:00 - 01/01/2020 UTC", &epoch));
  CHECK(epoch == 1577836800u);
  CHECK(!clockReplyParse("14:02 - 10/11/2026", &epoch));       // no UTC
  CHECK(!clockReplyParse("14:02 - 10/13/2026 UTC", &epoch));   // no 13th month
  CHECK(!clockReplyParse("24:00 - 10/11/2026 UTC", &epoch));
  CHECK(!clockReplyParse("OK - clock set", &epoch));
  CHECK(!clockReplyParse("", &epoch));
  CHECK(!clockReplyParse(nullptr, &epoch));
  char t[24];
  clockText(t, sizeof(t), 1794319320u);
  // EST-364: 14:02 UTC is 15:02 in Ljubljana (CET), shown without a zone label.
  CHECK_TEXT(t, "15:02");
}

// --- battery ------------------------------------------------------------------------------------
static void testBattery() {
  char v[24];
  batteryVoltsText(v, sizeof(v), 4120);
  CHECK_TEXT(v, "4.12 V");
  batteryVoltsText(v, sizeof(v), 3950);
  CHECK_TEXT(v, "3.95 V");
  batteryVoltsText(v, sizeof(v), 2500);
  CHECK_TEXT(v, "2.50 V");
  batteryVoltsText(v, sizeof(v), 30000);
  CHECK_TEXT(v, "30.00 V");
  batteryVoltsText(v, sizeof(v), 2499);
  CHECK_TEXT(v, "n/a");
  batteryVoltsText(v, sizeof(v), 30001);
  CHECK_TEXT(v, "n/a");
  batteryVoltsText(v, sizeof(v), 0);
  CHECK_TEXT(v, "n/a");
  CHECK(!batteryPlausible(0));
  CHECK(!batteryPlausible(2499));
  CHECK(batteryPlausible(2500));
  CHECK(batteryPlausible(30000));
  CHECK(!batteryPlausible(30001));
  batteryRawText(v, sizeof(v), 4120);
  CHECK_TEXT(v, "4120 mV");
  batteryRawText(v, sizeof(v), 0);
  CHECK_TEXT(v, "0 mV");
}

// --- the keyboard's region field ----------------------------------------------------------------
static void testKeyboard() {
  const om::ui::KeyboardField f = om::ui::KeyboardField::RptrRegion;
  CHECK(om::ui::keyboardAllowsChar(f, 'a'));
  CHECK(om::ui::keyboardAllowsChar(f, 'Z'));
  CHECK(om::ui::keyboardAllowsChar(f, '7'));
  CHECK(om::ui::keyboardAllowsChar(f, '-'));
  CHECK(om::ui::keyboardAllowsChar(f, '#'));
  CHECK(om::ui::keyboardAllowsChar(f, '$'));
  CHECK(!om::ui::keyboardAllowsChar(f, ' '));
  CHECK(!om::ui::keyboardAllowsChar(f, '*'));
  CHECK(!om::ui::keyboardAllowsChar(f, '.'));
  CHECK(om::ui::keyboardAllowsChar(f, '_'));
  // The keyboard and the model agree on every ASCII byte: one rule, not two copies that drift.
  for (int c = 0; c < 128; ++c) {
    CHECK(om::ui::keyboardAllowsChar(f, (char)c) == regionNameChar((char)c));
  }
  CHECK(om::ui::keyboardMaxBytes(f) == kRegionNameMax);
  CHECK(om::ui::keyboardAcceptsBytes(f, 29, 1));
  CHECK(!om::ui::keyboardAcceptsBytes(f, 30, 1));
  CHECK(!om::ui::keyboardAccents(f));
  CHECK(!om::ui::keyboardEmoji(f));

  // The letters page drops everything the field cannot take, and the 123 page carries '#', '$', '-'.
  for (int row = 0; row < 4; ++row) {
    om::ui::KeyDef defs[10];
    const int n = om::ui::keyRow(false, row, defs, f);
    for (int i = 0; i < n; ++i) {
      if (defs[i].kind == om::ui::KeyKind::Char) {
        CHECK(regionNameChar(defs[i].label[0]));
      }
      CHECK(defs[i].kind != om::ui::KeyKind::Space);
      CHECK(defs[i].kind != om::ui::KeyKind::Emoji);
    }
    CHECK(n > 0);
  }
  bool hasHash = false, hasDollar = false, hasDash = false;
  for (int row = 0; row < 4; ++row) {
    om::ui::KeyDef defs[10];
    const int n = om::ui::keyRow(true, row, defs, f);
    for (int i = 0; i < n; ++i) {
      if (defs[i].label[0] == '#') hasHash = true;
      if (defs[i].label[0] == '$') hasDollar = true;
      if (defs[i].label[0] == '-') hasDash = true;
    }
  }
  CHECK(hasHash && hasDollar && hasDash);
}

int main() {
  testRegionNames();
  testCliLines();
  testReplyParsers();
  testAnonRegions();
  testTimeGuard();
  testClockReply();
  testBattery();
  testKeyboard();
  if (g_fail == 0) {
    printf("  OK  est360: region names, CLI lines, anon regions, clock guard, battery, keyboard\n");
  }
  return g_fail == 0 ? 0 : 1;
}
