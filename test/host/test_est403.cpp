// Host test for EST-403: the clock kept across a warm restart.
//
// The decision table is clock_keep.h (plain C++), so this runs the exact code the device compiles:
//   * only the GPS warm boot's KEEP reasons may keep a clock (gps_boot.h is the one table);
//   * bad magic, a not-real record, an unknown source, an insane saved epoch, an insane restored
//     epoch, an RTC counter that went backwards, and anything below the floor all mean "no time";
//   * the RTC counter advances the saved epoch by the time that really elapsed;
//   * `kept` is the lowest rank: every fresh source replaces it immediately.
#include <initializer_list>
#include <stdio.h>
#include <string.h>

#include "clock_keep.h"
#include "gps_boot.h"
#include "timekeeper.h"

using namespace om;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// --- a fake system clock and keep callback so the host's own state is never touched ---------------
static uint32_t g_fakeNow = 0;
static int g_setCalls = 0;
static int g_keepCalls = 0;
static uint32_t g_keepEpoch = 0;
static TimeSource g_keepSource = TimeSource::None;

static void fakeSet(uint32_t epoch) {
  g_fakeNow = epoch;
  ++g_setCalls;
}
static uint32_t fakeGet() { return g_fakeNow; }
static void fakeKeep(uint32_t epoch, TimeSource source) {
  g_keepEpoch = epoch;
  g_keepSource = source;
  ++g_keepCalls;
}

static ClockKeepRecord recordOf(uint32_t epoch, TimeSource source, uint64_t rtcUs) {
  ClockKeepRecord r = {};
  r.magic = kClockKeepMagic;
  r.epoch = epoch;
  r.rtcUs = rtcUs;
  r.source = (uint8_t)source;
  r.real = 1;
  return r;
}

static void testReasonTable() {
  const uint32_t epoch = makeUtcEpoch(2026, 11, 11, 18, 30, 0);
  const ClockKeepRecord good = recordOf(epoch, TimeSource::Wifi, 5000000ULL);

  // The KEEP half of EST-387's table: the SoC restarted, the module (and the RTC counter) did not.
  const GpsBootReason keep[] = {GpsBootReason::Software, GpsBootReason::External,
                                GpsBootReason::Panic,    GpsBootReason::IntWdt,
                                GpsBootReason::TaskWdt,  GpsBootReason::Wdt,
                                GpsBootReason::Sdio};
  for (GpsBootReason reason : keep) {
    CHECK(!gpsBootResetsReceiver(reason));
    const ClockKeepBoot out = clockKeepDecide(reason, good, 7000000ULL, epoch);
    CHECK(out.kept);
    CHECK(out.epoch == epoch + 2);   // 2 s of RTC time really elapsed
    CHECK(out.source == TimeSource::Wifi);
  }

  // The RESET half: behave exactly as today, no kept clock.
  const GpsBootReason reset[] = {GpsBootReason::PowerOn, GpsBootReason::Brownout,
                                 GpsBootReason::DeepSleep, GpsBootReason::Unknown};
  for (GpsBootReason reason : reset) {
    CHECK(gpsBootResetsReceiver(reason));
    const ClockKeepBoot out = clockKeepDecide(reason, good, 7000000ULL, epoch);
    CHECK(!out.kept);
  }
  // An SDK number this code has never seen maps to Unknown and therefore RESET.
  CHECK(gpsBootReasonOf(999) == GpsBootReason::Unknown);
  CHECK(!clockKeepDecide(gpsBootReasonOf(999), good, 7000000ULL, epoch).kept);
}

static void testBadRecord() {
  const uint32_t epoch = makeUtcEpoch(2026, 11, 11, 18, 30, 0);
  const uint32_t floor = epoch - 1000;
  const GpsBootReason keep = GpsBootReason::Software;

  CHECK(clockKeepDecide(keep, recordOf(epoch, TimeSource::Gps, 100), 200, floor).kept);

  // Bad magic: what a power-on leaves in RTC_NOINIT memory.
  ClockKeepRecord r = recordOf(epoch, TimeSource::Gps, 100);
  r.magic = 0;
  CHECK(!clockKeepDecide(keep, r, 200, floor).kept);
  r.magic = kClockKeepMagic + 1;
  CHECK(!clockKeepDecide(keep, r, 200, floor).kept);

  // "system time was real" must be exactly 1.
  for (uint8_t real : {0, 2, 255}) {
    r = recordOf(epoch, TimeSource::Gps, 100);
    r.real = real;
    CHECK(!clockKeepDecide(keep, r, 200, floor).kept);
  }

  // Only the five real sources may be named; `kept` itself and None never.
  for (int s : {0, (int)TimeSource::Kept, 200}) {
    r = recordOf(epoch, TimeSource::Gps, 100);
    r.source = (uint8_t)s;
    CHECK(!clockKeepDecide(keep, r, 200, floor).kept);
  }

  // An insane saved epoch is not a time.
  for (uint32_t bad : {0U, kMinSaneEpoch - 1, kMaxSaneEpoch, kMaxSaneEpoch + 1U, 0xFFFFFFFFU}) {
    r = recordOf(bad, TimeSource::Gps, 100);
    CHECK(!clockKeepDecide(keep, r, 200, 0).kept);
  }

  // The restored epoch must stay inside the window too (the saved one is sane here).
  r = recordOf(kMaxSaneEpoch - 5, TimeSource::Gps, 100);
  CHECK(!clockKeepDecide(keep, r, 100 + 10ULL * 1000000ULL, 0).kept);

  // The RTC counter went backwards: the RTC domain restarted under a record that survived.
  r = recordOf(epoch, TimeSource::Gps, 5000000ULL);
  CHECK(!clockKeepDecide(keep, r, 4999999ULL, floor).kept);
}

static void testElapsedAndFloor() {
  const uint32_t epoch = makeUtcEpoch(2026, 11, 11, 18, 30, 0);
  const GpsBootReason keep = GpsBootReason::Software;
  const uint64_t savedRtc = 10000000ULL;   // 10 s after the RTC domain powered up

  // 3.9 s elapsed -> whole seconds only, never rounded up (the floor must not run ahead of real).
  ClockKeepRecord r = recordOf(epoch, TimeSource::Gps, savedRtc);
  ClockKeepBoot out = clockKeepDecide(keep, r, savedRtc + 3900000ULL, epoch);
  CHECK(out.kept);
  CHECK(out.epoch == epoch + 3);

  // Less than a second elapsed, and exactly one second.
  out = clockKeepDecide(keep, r, savedRtc + 999ULL, epoch);
  CHECK(out.kept && out.epoch == epoch);
  out = clockKeepDecide(keep, r, savedRtc + 1000000ULL, epoch);
  CHECK(out.kept && out.epoch == epoch + 1);

  // The saved epoch itself is the floor: equal is allowed, one second above refuses.
  out = clockKeepDecide(keep, r, savedRtc + 3900000ULL, epoch + 3);
  CHECK(out.kept);
  CHECK(!clockKeepDecide(keep, r, savedRtc + 3900000ULL, epoch + 4).kept);

  // A floor from a newer NVS save (or a newer build) above the restored clock wins.
  CHECK(!clockKeepDecide(keep, r, savedRtc, epoch + 1).kept);

  // A long but possible warm period is just elapsed time: no arbitrary cap below the clock window.
  out = clockKeepDecide(keep, r, savedRtc + 3600ULL * 1000000ULL, epoch);
  CHECK(out.kept && out.epoch == epoch + 3600);
}

static void testKeptPriority() {
  const uint32_t saved = makeUtcEpoch(2026, 3, 1, 0, 0, 0);
  const uint32_t build = makeUtcEpoch(2026, 2, 1, 0, 0, 0);
  const uint32_t keptEpoch = saved + 1000;

  // The source name and rank: `kept` is below every fresh source, including gps-nofix.
  CHECK(strcmp(timeSourceName(TimeSource::Kept), "kept") == 0);
  CHECK(timeSourcePriority(TimeSource::Kept) < timeSourcePriority(TimeSource::GpsNoFix));
  for (TimeSource s : {TimeSource::Gps, TimeSource::Wifi, TimeSource::Phone, TimeSource::Manual,
                       TimeSource::GpsNoFix}) {
    CHECK(timeSourcePriority(s) > timeSourcePriority(TimeSource::Kept));
  }

  TimeKeeper tk;
  g_fakeNow = 0;
  g_setCalls = 0;
  tk.begin(saved, build, 0, fakeSet, fakeGet, fakeKeep);
  CHECK(!tk.realSinceBoot());
  CHECK(tk.setKept(keptEpoch, TimeSource::Wifi, 0));
  CHECK(tk.realSinceBoot());
  CHECK(tk.source() == TimeSource::Kept);
  CHECK(tk.keptFrom() == TimeSource::Wifi);
  CHECK(tk.now() == keptEpoch);
  CHECK(tk.floor() == keptEpoch);
  CHECK(!tk.approximate());
  CHECK(tk.saveDue(0));   // the restored floor should be saved

  // Every fresh source may replace it AT ONCE, with no 6-hour hold.
  for (TimeSource s : {TimeSource::Gps, TimeSource::Wifi, TimeSource::Phone, TimeSource::Manual,
                       TimeSource::GpsNoFix}) {
    TimeKeeper fresh;
    fresh.begin(saved, build, 0, fakeSet, fakeGet, nullptr);
    CHECK(fresh.setKept(keptEpoch, TimeSource::Wifi, 0));
    CHECK(fresh.maySet(s, 1));
    CHECK(fresh.setFromSource(keptEpoch + 10, s, 1));
    CHECK(fresh.source() == s);
    CHECK(fresh.keptFrom() == TimeSource::None);
  }

  // Kept from a gps-nofix record is still approximate; kept from GPS is not.
  TimeKeeper nofix;
  nofix.begin(saved, build, 0, fakeSet, fakeGet, nullptr);
  CHECK(nofix.setKept(keptEpoch, TimeSource::GpsNoFix, 0));
  CHECK(nofix.approximate());
  TimeKeeper fix;
  fix.begin(saved, build, 0, fakeSet, fakeGet, nullptr);
  CHECK(fix.setKept(keptEpoch, TimeSource::Gps, 0));
  CHECK(!fix.approximate());

  // setKept refuses what the boot decision should never pass, and `kept` cannot be claimed as a
  // fresh source.
  TimeKeeper bad;
  bad.begin(saved, build, 0, fakeSet, fakeGet, nullptr);
  CHECK(!bad.setKept(saved - 1, TimeSource::Gps, 0));   // below the floor
  CHECK(!bad.setKept(0, TimeSource::Gps, 0));           // insane
  CHECK(!bad.setKept(kMaxSaneEpoch, TimeSource::Gps, 0));
  CHECK(!bad.setKept(keptEpoch, TimeSource::None, 0));
  CHECK(!bad.setKept(keptEpoch, TimeSource::Kept, 0));
  CHECK(!bad.setKept(keptEpoch, (TimeSource)99, 0));   // an unknown number never names a source
  CHECK(!bad.realSinceBoot());
  CHECK(!bad.setFromSource(keptEpoch, TimeSource::Kept, 0));
  CHECK(bad.source() == TimeSource::None);
}

static void testKeepCallback() {
  const uint32_t saved = makeUtcEpoch(2026, 3, 1, 0, 0, 0);
  const uint32_t build = makeUtcEpoch(2026, 2, 1, 0, 0, 0);
  const uint32_t keptEpoch = saved + 500;

  g_fakeNow = 0;
  g_keepCalls = 0;
  g_keepEpoch = 0;
  g_keepSource = TimeSource::None;

  TimeKeeper tk;
  tk.begin(saved, build, 0, fakeSet, fakeGet, fakeKeep);
  CHECK(tk.setKept(keptEpoch, TimeSource::Phone, 0));
  CHECK(g_keepCalls == 1);
  CHECK(g_keepEpoch == keptEpoch);
  CHECK(g_keepSource == TimeSource::Phone);   // the record names the original, never `kept`

  CHECK(tk.setFromSource(keptEpoch + 100, TimeSource::Gps, 1));
  CHECK(g_keepCalls == 2);
  CHECK(g_keepEpoch == keptEpoch + 100);
  CHECK(g_keepSource == TimeSource::Gps);

  // A same-second refresh returns false (no clock write) but still refreshes the record.
  CHECK(!tk.setFromSource(keptEpoch + 101, TimeSource::Gps, 2));
  CHECK(g_keepCalls == 3);
  CHECK(g_keepEpoch == keptEpoch + 101);
}

int main() {
  testReasonTable();
  testBadRecord();
  testElapsedAndFloor();
  testKeptPriority();
  testKeepCallback();
  if (g_fail) {
    printf("%d EST-403 check(s) failed\n", g_fail);
    return 1;
  }
  printf("EST-403: all checks passed\n");
  return 0;
}
