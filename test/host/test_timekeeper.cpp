// Host test for src/timekeeper.cpp: NMEA RMC/ZDA parsing, the source ranking, the floor and the
// Europe/Ljubljana clock. Plain g++ and POSIX time functions, exactly the code the device runs.
#include <stdio.h>
#include <string.h>

#include <string>

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

// --- NMEA helper: body is the chars between '$' and '*', checksum is computed here --------------
static std::string nmea(const std::string& body) {
  unsigned sum = 0;
  for (char c : body) sum ^= (unsigned char)c;
  char out[8];
  snprintf(out, sizeof(out), "*%02X", sum & 0xFF);
  return "$" + body + out;
}

static void testNmea() {
  // Valid RMC: 10 Nov 2026 12:34:56 UTC.
  {
    const std::string s = nmea("GNRMC,123456.00,A,4603.1234,N,01430.5678,E,0.0,0.0,101126,,,A");
    const ParsedTime t = parseNmeaTime(s.c_str());
    CHECK(t.ok);
    CHECK(t.hasDate);
    CHECK(t.epoch == makeUtcEpoch(2026, 11, 10, 12, 34, 56));
  }
  // GP talker is accepted too.
  {
    const std::string s = nmea("GPRMC,235959.00,A,4603.1234,N,01430.5678,E,0.0,0.0,311226,,,A");
    const ParsedTime t = parseNmeaTime(s.c_str());
    CHECK(t.ok);
    CHECK(t.epoch == makeUtcEpoch(2026, 12, 31, 23, 59, 59));
  }
  // ZDA carries the same time/date and is used when the receiver sends it.
  {
    const std::string s = nmea("GNZDA,123456.00,10,11,2026,00,00");
    const ParsedTime t = parseNmeaTime(s.c_str());
    CHECK(t.ok);
    CHECK(t.epoch == makeUtcEpoch(2026, 11, 10, 12, 34, 56));
  }
  // Void RMC (status V): no fix, so no time.
  {
    const std::string s = nmea("GNRMC,123456.00,V,4603.1234,N,01430.5678,E,0.0,0.0,101126,,,N");
    CHECK(!parseNmeaTime(s.c_str()).ok);
  }
  // The module's default date before its first satellite (2024) is outside 2025..2099.
  {
    const std::string s = nmea("GNRMC,123456.00,A,4603.1234,N,01430.5678,E,0.0,0.0,150524,,,A");
    CHECK(!parseNmeaTime(s.c_str()).ok);
  }
  // Bad checksum.
  {
    std::string s = nmea("GNRMC,123456.00,A,4603.1234,N,01430.5678,E,0.0,0.0,101126,,,A");
    s[s.size() - 1] = (s[s.size() - 1] == '0') ? '1' : '0';
    CHECK(!parseNmeaTime(s.c_str()).ok);
  }
  // Split/truncated line (the UART delivered only part of it).
  {
    const std::string s = nmea("GNRMC,123456.00,A,4603.1234,N");
    CHECK(!parseNmeaTime(s.c_str()).ok);
  }
  // An impossible date is rejected even with a valid status and checksum.
  {
    const std::string s = nmea("GNRMC,120000.00,A,4603.1234,N,01430.5678,E,0.0,0.0,290225,,,A");
    CHECK(!parseNmeaTime(s.c_str()).ok);
  }
  // ZDA with a 2024 date is rejected too.
  {
    const std::string s = nmea("GNZDA,123456.00,10,11,2024,00,00");
    CHECK(!parseNmeaTime(s.c_str()).ok);
  }
  // No date at all: a clock without a date cannot be turned into an epoch.
  {
    const std::string s = nmea("GNRMC,123456.00,A,4603.1234,N,01430.5678,E,0.0,0.0,,,,A");
    CHECK(!parseNmeaTime(s.c_str()).ok);
  }
  // Bad field count / empty sentence.
  CHECK(!parseNmeaTime(nullptr).ok);
  CHECK(!parseNmeaTime("").ok);
  CHECK(!parseNmeaTime("$GNRMC*00").ok);

  int h = 0, m = 0, sec = 0;
  CHECK(parseNmeaClock("235959.99", h, m, sec));
  CHECK(h == 23 && m == 59 && sec == 59);
  CHECK(!parseNmeaClock("246000", h, m, sec));
  CHECK(!parseNmeaClock("1234", h, m, sec));
}

static void testBuildTimestamp() {
  uint32_t e = 0;
  // __DATE__/__TIME__ are the compiler's LOCAL time: 08:00 CET is 07:00 UTC in November.
  CHECK(parseBuildTimestamp("Nov 10 2026", "08:00:00", e));
  CHECK(e == makeUtcEpoch(2026, 11, 10, 7, 0, 0));
  // __DATE__ pads a single digit day with an extra space.
  CHECK(parseBuildTimestamp("Nov  9 2026", "08:00:00", e));
  CHECK(e == makeUtcEpoch(2026, 11, 9, 7, 0, 0));
  // Summer time is CEST (UTC+2).
  CHECK(parseBuildTimestamp("Jul 10 2026", "08:00:00", e));
  CHECK(e == makeUtcEpoch(2026, 7, 10, 6, 0, 0));
  CHECK(!parseBuildTimestamp("Xxx 10 2026", "08:00:00", e));
  CHECK(!parseBuildTimestamp("Nov 10 2026", "eight", e));
}

static void testClockFormat() {
  char out[8];
  // Winter: CET = UTC+1.
  CHECK(formatClock(makeUtcEpoch(2026, 11, 10, 14, 0, 0), out, sizeof(out)));
  CHECK(strcmp(out, "15:00") == 0);
  // Summer: CEST = UTC+2.
  CHECK(formatClock(makeUtcEpoch(2026, 7, 10, 12, 0, 0), out, sizeof(out)));
  CHECK(strcmp(out, "14:00") == 0);
  // DST starts on the last Sunday of March at 02:00 local: 00:59 UTC is 01:59, 01:00 UTC is 03:00.
  CHECK(formatClock(makeUtcEpoch(2026, 3, 29, 0, 59, 0), out, sizeof(out)));
  CHECK(strcmp(out, "01:59") == 0);
  CHECK(formatClock(makeUtcEpoch(2026, 3, 29, 1, 0, 0), out, sizeof(out)));
  CHECK(strcmp(out, "03:00") == 0);
  // DST ends on the last Sunday of October at 03:00 local: 00:59 UTC is 02:59, 01:00 UTC is 02:00.
  CHECK(formatClock(makeUtcEpoch(2026, 10, 25, 0, 59, 0), out, sizeof(out)));
  CHECK(strcmp(out, "02:59") == 0);
  CHECK(formatClock(makeUtcEpoch(2026, 10, 25, 1, 0, 0), out, sizeof(out)));
  CHECK(strcmp(out, "02:00") == 0);
  CHECK(!formatClock(0, out, 3));   // no room for "HH:MM"
}

static void testFloor() {
  const uint32_t a = makeUtcEpoch(2026, 1, 1, 0, 0, 0);
  const uint32_t b = makeUtcEpoch(2026, 1, 2, 0, 0, 0);
  CHECK(bootEpoch(0, b) == b);
  CHECK(bootEpoch(a, b) == b);
  CHECK(bootEpoch(b, a) == b);
  CHECK(bootEpoch(0, 0) == kMinSaneEpoch);
  CHECK(raisedFloor(a, b) == b);
  CHECK(raisedFloor(b, a) == b);
  CHECK(raisedFloor(a, 0) == a);
}

// --- a fake system clock so the host's own time is never touched --------------------------------
static uint32_t g_fakeNow = 0;
static int g_setCalls = 0;

static void fakeSet(uint32_t epoch) {
  g_fakeNow = epoch;
  ++g_setCalls;
}
static uint32_t fakeGet() { return g_fakeNow; }

static void testKeeper() {
  const uint32_t saved = makeUtcEpoch(2026, 3, 1, 0, 0, 0);
  const uint32_t build = makeUtcEpoch(2026, 2, 1, 0, 0, 0);
  const uint32_t gps = saved + 1000;

  TimeKeeper tk;
  tk.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(tk.floor() == saved);           // saved floor beats the older build time
  CHECK(tk.now() == saved);
  CHECK(!tk.realSinceBoot());           // booting from a floor is NOT a real time
  CHECK(tk.source() == TimeSource::None);
  CHECK(tk.sourceAgeSec(0) == 0);
  CHECK(!tk.saveDue(0));
  CHECK(tk.saveDue(kFloorSaveIntervalSec));

  // The first GPS fix makes the clock real, writes the system clock and raises the floor.
  CHECK(tk.setFromSource(gps, TimeSource::Gps, 100));
  CHECK(tk.realSinceBoot());
  CHECK(tk.now() == gps);
  CHECK(tk.floor() == gps);
  CHECK(tk.source() == TimeSource::Gps);
  CHECK(tk.sourceAgeSec(250) == 150);
  CHECK(tk.saveDue(100));               // an applied sync is followed by a floor save
  tk.onFloorSaved(100);
  CHECK(!tk.saveDue(100 + kFloorSaveIntervalSec - 1));
  CHECK(tk.saveDue(100 + kFloorSaveIntervalSec));

  // A Wi-Fi source is lower than a GPS fix less than 6 hours old.
  TimeKeeper rank;
  rank.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(rank.setFromSource(gps, TimeSource::Gps, 100));
  CHECK(!rank.setFromSource(gps + 5000, TimeSource::Wifi, 200));
  CHECK(rank.now() == gps);
  // At exactly 6 hours the higher source is no longer "recent" and Wi-Fi may set.
  CHECK(rank.setFromSource(gps + 5000, TimeSource::Wifi, 100 + kTimeSourceHoldSec));
  CHECK(rank.source() == TimeSource::Wifi);

  // Phone is lower than Wi-Fi.
  TimeKeeper phone;
  phone.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(phone.setFromSource(gps, TimeSource::Wifi, 100));
  CHECK(!phone.setFromSource(gps + 10, TimeSource::Phone, 200));
  CHECK(phone.setFromSource(gps + 10, TimeSource::Phone, 100 + kTimeSourceHoldSec));

  // GPS takes over from Wi-Fi/phone immediately.
  TimeKeeper up;
  up.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(up.setFromSource(gps, TimeSource::Phone, 100));
  CHECK(up.setFromSource(gps + 100, TimeSource::Gps, 200));
  CHECK(up.source() == TimeSource::Gps);

  // Manual always applies, even under a fresh GPS fix, and a later Wi-Fi/GPS may take over.
  TimeKeeper man;
  man.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(man.setFromSource(gps, TimeSource::Gps, 100));
  CHECK(man.setFromSource(gps + 200, TimeSource::Manual, 200));
  CHECK(man.source() == TimeSource::Manual);
  // Wi-Fi is still lower than the GPS fix seen 200 s ago, so it waits; the next GPS fix takes over.
  CHECK(!man.setFromSource(gps + 300, TimeSource::Wifi, 300));
  CHECK(man.setFromSource(gps + 400, TimeSource::Gps, 400));
  CHECK(man.source() == TimeSource::Gps);

  // A real source behind the floor is refused: the floor never goes backwards.
  TimeKeeper back;
  back.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(!back.setFromSource(saved - 1000, TimeSource::Gps, 100));
  CHECK(!back.realSinceBoot());
  CHECK(back.now() == saved);
  CHECK(back.floor() == saved);

  // MeshCore's plumbing setter may raise the clock but never makes it "real".
  TimeKeeper hw;
  hw.begin(saved, build, 0, fakeSet, fakeGet);
  hw.setHardware(saved - 500);
  CHECK(hw.now() == saved);
  hw.setHardware(saved + 500);
  CHECK(hw.now() == saved + 500);
  CHECK(hw.floor() == saved + 500);
  CHECK(!hw.realSinceBoot());

  // A first real source that lands exactly on the floor still counts as applied (real flag + save),
  // even though no clock write is needed.
  TimeKeeper first;
  first.begin(gps, build, 0, fakeSet, fakeGet);
  CHECK(first.now() == gps);
  CHECK(first.setFromSource(gps, TimeSource::Gps, 100));
  CHECK(first.realSinceBoot());
  CHECK(first.saveDue(100));

  // Same-second source refreshes are allowed but do not ask for an NVS write.
  TimeKeeper same;
  same.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(same.setFromSource(gps, TimeSource::Gps, 100));
  CHECK(!same.setFromSource(gps + 1, TimeSource::Gps, 101));
  CHECK(same.sourceAgeSec(160) == 59);
}

int main() {
  testNmea();
  testBuildTimestamp();
  testClockFormat();
  testFloor();
  testKeeper();
  if (g_fail) {
    printf("%d timekeeper check(s) failed\n", g_fail);
    return 1;
  }
  printf("timekeeper: all checks passed\n");
  return 0;
}
