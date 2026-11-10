// Host test for the one timestamp formatter (ui/age.cpp, EST-338) and the GPS cell text.
//
// The formatter is the only place that decides between a real local time and an age fallback:
//   today HH:MM, this week "Mon 14:02", older "10.11. 14:02"; the narrow Clock form keeps five glyphs.
// It must use the FIXED 2025-01-01 cut-off, never the firmware's build time (the EST-333 bug).
#include <stdio.h>
#include <string.h>

#include "timekeeper.h"
#include "ui/age.h"
#include "ui/status_cells.h"

using namespace om;
using namespace om::ui;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static bool says(const Stamp& s, uint32_t nowWall, uint32_t nowUp, bool nowReal, StampForm form,
                 const char* want) {
  char out[32];
  stampText(out, sizeof(out), s, nowWall, nowUp, nowReal, form);
  if (strcmp(out, want) != 0) {
    printf("FAIL stamp = \"%s\", want \"%s\"\n", out, want);
    ++g_fail;
    return false;
  }
  return true;
}

static Stamp realStamp(uint32_t wall) {
  Stamp s;
  s.wall = wall;
  s.uptime = 100;
  s.wallReal = true;
  return s;
}

static void testStamp() {
  const uint32_t nowWall = makeUtcEpoch(2026, 11, 10, 13, 2, 0);   // 14:02 CET
  const uint32_t nowUp = 1000;

  const uint32_t today = makeUtcEpoch(2026, 11, 10, 12, 30, 0);     // 13:30 CET
  const uint32_t yesterday = makeUtcEpoch(2026, 11, 9, 13, 2, 0);   // Mon 14:02
  const uint32_t fiveDays = makeUtcEpoch(2026, 11, 5, 13, 2, 0);    // Thu 14:02
  const uint32_t sevenDays = makeUtcEpoch(2026, 11, 3, 13, 2, 0);   // Tue, older than a week
  const uint32_t older = makeUtcEpoch(2026, 10, 10, 12, 2, 0);      // CEST -> 14:02 local

  // --- real time, the DayTime form (sheet header, Nodes list) -----------------
  says(realStamp(today), nowWall, nowUp, true, StampForm::DayTime, "13:30");
  says(realStamp(yesterday), nowWall, nowUp, true, StampForm::DayTime, "Mon 14:02");
  says(realStamp(fiveDays), nowWall, nowUp, true, StampForm::DayTime, "Thu 14:02");
  says(realStamp(sevenDays), nowWall, nowUp, true, StampForm::DayTime, "03.11. 14:02");
  says(realStamp(older), nowWall, nowUp, true, StampForm::DayTime, "10.10. 14:02");

  // --- real time, the narrow Clock form (chat line, selector) -----------------
  // Exactly five glyphs in every branch, so the name and the text start at one column.
  char narrow[16];
  says(realStamp(today), nowWall, nowUp, true, StampForm::Clock, "13:30");
  says(realStamp(yesterday), nowWall, nowUp, true, StampForm::Clock, "09.11");
  says(realStamp(fiveDays), nowWall, nowUp, true, StampForm::Clock, "05.11");
  says(realStamp(sevenDays), nowWall, nowUp, true, StampForm::Clock, "03.11");
  says(realStamp(older), nowWall, nowUp, true, StampForm::Clock, "10.10");
  stampText(narrow, sizeof(narrow), realStamp(yesterday), nowWall, nowUp, true, StampForm::Clock);
  CHECK(strlen(narrow) == 5);

  // --- the clock was not real when the event happened: age, never a made-up time
  Stamp s = realStamp(today);
  s.wallReal = false;
  s.uptime = 800;   // 200 s ago
  says(s, nowWall, nowUp, true, StampForm::DayTime, "3m");
  says(s, nowWall, nowUp, true, StampForm::Clock, "3m");

  // The clock is not real NOW: the same age fallback, even though the event's wall stamp was real.
  s = realStamp(yesterday);
  s.uptime = 800;
  says(s, nowWall, nowUp, false, StampForm::DayTime, "3m");

  // An old 2024 VolatileRTCClock stamp: before the fixed cut-off, no age is available -> "--".
  Stamp old;
  old.wall = 1715770351UL;   // 2024-05-15, the old clock's base
  old.wallReal = true;
  says(old, nowWall, nowUp, true, StampForm::DayTime, "--");
  says(old, nowWall, nowUp, true, StampForm::Clock, "--");

  // Before the CURRENT build but after the cut-off: the EST-333 bug was to call this "--". It shows
  // its time (the build moves at every flash; the cut-off does not).
  const uint32_t beforeBuild = makeUtcEpoch(2026, 11, 10, 7, 0, 0);   // 08:00 CET
  says(realStamp(beforeBuild), nowWall, nowUp, true, StampForm::DayTime, "08:00");

  // --- a stamp in the future: the clock moved after it was stored; "now", never a future time
  Stamp future = realStamp(nowWall + 3600);
  future.uptime = nowUp;
  says(future, nowWall, nowUp, true, StampForm::DayTime, "now");
  Stamp futureNoUp = realStamp(nowWall + 3600);
  futureNoUp.uptime = 0;
  says(futureNoUp, nowWall, nowUp, true, StampForm::Clock, "now");

  // --- unknown / fallback ladder ------------------------------------------------
  says(Stamp{}, nowWall, nowUp, true, StampForm::DayTime, "--");
  Stamp far;
  far.wallReal = false;
  far.uptime = 100;
  says(far, nowWall, 100 + 40UL * 86400UL, true, StampForm::DayTime, "30d+");
  far.uptime = 100;
  says(far, nowWall, 100 + 300, true, StampForm::Clock, "5m");
  far.uptime = 100;
  says(far, nowWall, 100 + 3 * 3600, true, StampForm::Clock, "3h");
  far.uptime = 100;
  says(far, nowWall, 100 + 3 * 86400, true, StampForm::Clock, "3d");

  // --- the fixed cut-off itself ------------------------------------------------
  CHECK(!stampIsReal(1715770351UL));                       // 2024 old clock
  CHECK(!stampIsReal(1735689599UL));                       // one second before 2025-01-01
  CHECK(stampIsReal(1735689600UL));                        // 2025-01-01 00:00 UTC
  CHECK(stampIsReal(makeUtcEpoch(2026, 11, 10, 7, 0, 0)));

  // A tiny cap is never overrun.
  char tiny[4];
  stampText(tiny, sizeof(tiny), realStamp(sevenDays), nowWall, nowUp, true, StampForm::DayTime);
  CHECK(strlen(tiny) < sizeof(tiny));
}

// One glyph per UTF-8 sequence start: the mono face draws U+00B7 (2 bytes) as one 6 px cell.
static int glyphs(const char* s) {
  int n = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
    if ((*p & 0xC0) != 0x80) ++n;
  }
  return n;
}

static int width(const char* s) { return glyphs(s) * kMonoStatusAdvancePx; }

static void testStatusCells() {
  GpsCell c;
  char out[16];

  c = GpsCell();
  c.talked = true;
  c.heard = 4;
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 4") == 0);
  CHECK(width(out) == 30);

  c.valid = true;
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 4") == 0);

  c.heard = 0;
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 0") == 0);

  c = GpsCell();
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS --") == 0);

  c = GpsCell();
  c.talked = true;
  c.heard = 24;
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 24") == 0);
  c.heard = 300;
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 99") == 0);
  CHECK(width(out) <= kGpsCellMax * kMonoStatusAdvancePx);
}

int main() {
  testStamp();
  testStatusCells();
  if (g_fail) {
    printf("%d stamp/status check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("stamp/status: all checks passed\n");
  return 0;
}
