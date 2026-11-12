// SPDX-License-Identifier: MIT
// EST-413 host test: the timekeeper's floor is not a real time.
//
//   1. POWER LOG. While the clock is not real since boot a due minute writes NO row and is counted
//      as skipped; once a real source sets the clock the same minute stamps and writes. The stamp
//      is the Ljubljana civil minute of the epoch, so a floor 9 minutes behind (the incident) can
//      never become a row.
//   2. ONE GATE. logLocalStamp() refuses a not-real clock and leaves the caller's stamp untouched;
//      the logger's localNow() delegates to it instead of formatting now() directly.
//   3. MANUAL TIME. The steppers start from the build epoch while the clock is not real (never the
//      stale saved floor) and from the clock only once it is real.
// Run by test/host/run.sh (system g++), no Arduino and no device.
#include <stdio.h>
#include <string.h>

#include "power_log_logic.h"
#include "timekeeper.h"

static int g_fail = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++g_fail;                                                           \
    }                                                                     \
  } while (0)

// ------------------------------------------------------------------------------------- 1. power log
static void testPowerLogGate() {
  om::plog::MinuteClock clock;
  om::plog::LocalStamp t;

  // The boot floor: a lower bound from the saved NVS / the build time. NOT real, so no row is
  // written and every due minute is counted. (The old localNow() stamped these minutes as if they
  // were the wall clock: 13:27, 13:28 ... next to the previous boot's 13:28-13:35 rows.)
  CHECK(!clock.stamp(om::makeUtcEpoch(2026, 11, 12, 12, 27, 0), false, t));
  CHECK(clock.skipped() == 1);
  CHECK(!clock.stamp(om::makeUtcEpoch(2026, 11, 12, 12, 28, 0), false, t));
  CHECK(clock.skipped() == 2);

  // A real source arrives at 12:36 UTC (13:36 local, CET): the row is written and the skip count
  // stops where it was.
  CHECK(clock.stamp(om::makeUtcEpoch(2026, 11, 12, 12, 36, 0), true, t));
  CHECK(clock.skipped() == 2);
  CHECK(t.year == 2026);
  CHECK(t.month == 11);
  CHECK(t.day == 12);
  CHECK(t.hour == 13);
  CHECK(t.minute == 36);

  // The next minute after real: another normal row, still no skip.
  CHECK(clock.stamp(om::makeUtcEpoch(2026, 11, 12, 12, 37, 0), true, t));
  CHECK(t.hour == 13 && t.minute == 37);
  CHECK(clock.skipped() == 2);

  // A fresh logger (begin()) starts the count again.
  clock.reset();
  CHECK(clock.skipped() == 0);
}

// --------------------------------------------------------------------------------------- 2. one gate
static void testLogLocalStamp() {
  om::plog::LocalStamp t;
  t.year = 1999;
  t.month = 1;
  t.day = 1;
  t.hour = 0;
  t.minute = 0;

  // Not real: refused, and the caller's stamp is untouched (no half-filled row).
  CHECK(!om::plog::logLocalStamp(om::makeUtcEpoch(2026, 11, 12, 12, 27, 0), false, t));
  CHECK(t.year == 1999 && t.month == 1 && t.minute == 0);

  // Real: the one formatter (localCivil) fills it.
  CHECK(om::plog::logLocalStamp(om::makeUtcEpoch(2026, 11, 12, 12, 36, 0), true, t));
  CHECK(t.year == 2026 && t.month == 11 && t.day == 12);
  CHECK(t.hour == 13 && t.minute == 36);
}

// ------------------------------------------------------------------------------------ 3. manual time
static void testManualTimeSeed() {
  const uint32_t floor = om::makeUtcEpoch(2026, 11, 12, 12, 27, 0);   // saved NVS / build floor
  const uint32_t build = om::makeUtcEpoch(2026, 11, 10, 8, 0, 0);

  // Not real: the steppers start from the build time, never the stale saved floor.
  CHECK(om::manualTimeSeedEpoch(false, floor, build) == build);
  CHECK(om::manualTimeSeedEpoch(false, floor, build) != floor);
  // Real: the clock is the starting value.
  CHECK(om::manualTimeSeedEpoch(true, floor, build) == floor);
  // An unreadable build time (0) passes through; main.cpp then keeps its own default.
  CHECK(om::manualTimeSeedEpoch(false, floor, 0) == 0);
}

int main() {
  testPowerLogGate();
  testLogLocalStamp();
  testManualTimeSeed();
  if (g_fail != 0) {
    printf("%d check(s) failed\n", g_fail);
    return 1;
  }
  printf("est413: all checks passed\n");
  return 0;
}
