// SPDX-License-Identifier: MIT
// EST-383 host test: the `gps raw` diagnostic and the no-fix clock.
//
// What is proven here, all with the same files the device builds:
//   1. the field stripping: RMC/ZDA/GGA/GSA print only their safe fields and a latitude/longitude
//      never appears, not even when a position-shaped value lands in the time column;
//   2. the type tally and the window (5..120 s, one summary, zda seen/not seen, bad checksums);
//   3. the advance-by-1s gate (accept +1/+2, refuse 0 and a jump, no acceptance on the first
//      sentence, nothing tracked means nothing accepted);
//   4. the default-year rejection for RMC V and ZDA (2080), while a fix RMC is unaffected;
//   5. the precedence: gps-nofix is below fix, Wi-Fi, phone and manual; each of those replaces it at
//      once, and it can never overwrite any of them.
#include <stdio.h>
#include <string.h>

#include <string>

#include "gps_raw.h"
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

static std::string nmea(const std::string& body) {
  unsigned sum = 0;
  for (char c : body) sum ^= (unsigned char)c;
  char out[8];
  snprintf(out, sizeof(out), "*%02X", sum & 0xFF);
  return "$" + body + out;
}

// Distinctive digits so a leaked latitude/longitude is impossible to miss.
static const char* kLat = "9999.9999";
static const char* kLon = "8888.8888";

static void expectClean(const char* what, const char* out) {
  if (strstr(out, kLat) != nullptr || strstr(out, kLon) != nullptr ||
      strstr(out, "9999") != nullptr || strstr(out, "8888") != nullptr) {
    printf("FAIL %s leaked a position: %s\n", what, out);
    ++g_fail;
  }
}

static void testTypeToken() {
  char tok[8];
  CHECK(gpsRawTypeToken(nmea("GNRMC,123456.00,A,1,2,3,4,0,0,101126,,,A").c_str(), tok,
                        sizeof(tok)));
  CHECK(strcmp(tok, "GNRMC") == 0);
  CHECK(gpsRawTypeToken(nmea("GPGSV,3,1,11").c_str(), tok, sizeof(tok)));
  CHECK(strcmp(tok, "GPGSV") == 0);
  CHECK(!gpsRawTypeToken("", tok, sizeof(tok)));
  CHECK(!gpsRawTypeToken("no dollar here", tok, sizeof(tok)));
  CHECK(!gpsRawTypeToken("$,empty*00", tok, sizeof(tok)));
  CHECK(!gpsRawTypeToken(nullptr, tok, sizeof(tok)));
}

static void testFormatRmc() {
  char out[96];
  const std::string v = nmea("GNRMC,123456.00,V,9999.9999,N,8888.8888,E,0.0,0.0,101126,,,N");
  CHECK(gpsRawFormat(v.c_str(), out, sizeof(out)));
  CHECK(strstr(out, "type=GNRMC") != nullptr);
  CHECK(strstr(out, "time=123456.00") != nullptr);
  CHECK(strstr(out, "status=V") != nullptr);
  CHECK(strstr(out, "date=101126") != nullptr);
  expectClean("RMC V", out);

  const std::string a = nmea("GPRMC,235959.00,A,9999.9999,N,8888.8888,E,0.0,0.0,311226,,,A");
  CHECK(gpsRawFormat(a.c_str(), out, sizeof(out)));
  CHECK(strstr(out, "type=GPRMC") != nullptr);
  CHECK(strstr(out, "status=A") != nullptr);
  expectClean("RMC A", out);

  // A position-shaped value in the TIME column is refused, not printed as time: the column can
  // only carry hhmmss[.sss], so 9999.9999 cannot become `time=9999.9999`.
  const std::string bad = nmea("GNRMC,9999.9999,V,9999.9999,N,8888.8888,E,0.0,0.0,101126,,,N");
  CHECK(gpsRawFormat(bad.c_str(), out, sizeof(out)));
  CHECK(strstr(out, "time=-") != nullptr);
  expectClean("RMC bad time", out);

  // An RMC with every field empty is still an RMC: it prints "-" for status and date rather than a
  // guess (a cold receiver sending a bare sentence is itself worth seeing).
  const std::string empty = nmea("GNRMC,,,,,,,,,,,,");
  CHECK(gpsRawFormat(empty.c_str(), out, sizeof(out)));
  CHECK(strstr(out, "status=-") != nullptr);
  CHECK(strstr(out, "date=-") != nullptr);

  // Too few fields to be a sentence at all.
  CHECK(!gpsRawFormat(nmea("GNRMC,123456.00").c_str(), out, sizeof(out)));

  const std::string noDate = nmea("GNRMC,123456.00,V,9999.9999,N,8888.8888,E,0.0,0.0,,,,N");
  CHECK(gpsRawFormat(noDate.c_str(), out, sizeof(out)));
  CHECK(strstr(out, "date=-") != nullptr);
  expectClean("RMC no date", out);
}

static void testFormatZda() {
  char out[96];
  const std::string z = nmea("GNZDA,123456.00,10,11,2026,00,00");
  CHECK(gpsRawFormat(z.c_str(), out, sizeof(out)));
  CHECK(strstr(out, "type=GNZDA") != nullptr);
  CHECK(strstr(out, "time=123456.00") != nullptr);
  CHECK(strstr(out, "date=10.11.2026") != nullptr);
  expectClean("ZDA", out);

  // A non-digit date field prints "-" instead of a half date.
  const std::string bad = nmea("GNZDA,123456.00,1x,11,2026,00,00");
  CHECK(gpsRawFormat(bad.c_str(), out, sizeof(out)));
  CHECK(strstr(out, "date=-") != nullptr);
}

static void testFormatGga() {
  char out[96];
  const std::string g = nmea("GNGGA,123456.00,9999.9999,N,8888.8888,E,1,08,0.9,100.0,M,0.0,M,,");
  CHECK(gpsRawFormat(g.c_str(), out, sizeof(out)));
  CHECK(strstr(out, "type=GNGGA") != nullptr);
  CHECK(strstr(out, "quality=1") != nullptr);
  CHECK(strstr(out, "sats=08") != nullptr);
  expectClean("GGA", out);

  // Quality is a digit field: an injected position in that column cannot pass.
  const std::string bad = nmea("GNGGA,123456.00,9999.9999,N,8888.8888,E,9999.9999,08,0.9,1,M");
  CHECK(gpsRawFormat(bad.c_str(), out, sizeof(out)));
  CHECK(strstr(out, "quality=-") != nullptr);
  expectClean("GGA bad quality", out);
}

static void testFormatGsa() {
  char out[96];
  const std::string g = nmea("GNGSA,A,3,04,05,09,12,,,,,,,,,2.5,1.3,2.1");
  CHECK(gpsRawFormat(g.c_str(), out, sizeof(out)));
  CHECK(strstr(out, "type=GNGSA") != nullptr);
  CHECK(strstr(out, "time=-") != nullptr);   // GSA carries no time field
  CHECK(strstr(out, "fixmode=3") != nullptr);
  CHECK(strstr(out, "04") == nullptr);       // satellite PRNs are not printed either
  expectClean("GSA", out);

  // GSV and VTG are not part of this diagnostic.
  char other[96];
  CHECK(!gpsRawFormat(nmea("GPGSV,3,1,11,04,05,091,,1,08,45,045,").c_str(), other, sizeof(other)));
  CHECK(!gpsRawFormat(nmea("GNVTG,0.0,T,0.0,M,0.0,N,0.0,K,A").c_str(), other, sizeof(other)));
  CHECK(!gpsRawFormat(nullptr, other, sizeof(other)));
  CHECK(!gpsRawFormat(nmea("GNRMC,123456.00,V,1,N,2,E,0,0,101126,,,N").c_str(), other, 8));
}

static void testWindow() {
  GpsRawWindow w;
  CHECK(!w.active());
  CHECK(!w.start(4, 1000));      // below 5
  CHECK(!w.start(121, 1000));    // above 120
  CHECK(w.start(30, 1000));
  CHECK(w.active());
  CHECK(w.secondsLeft(1000) == 30);
  CHECK(w.elapsedSec(11000) == 10);
  CHECK(!w.finished(30999));
  CHECK(w.finished(31000));
  CHECK(!w.start(10, 11000));    // one window at a time

  w.noteSentence(nmea("GNRMC,123456.00,V,1,N,2,E,0,0,101126,,,N").c_str(), true);
  w.noteSentence(nmea("GNRMC,123457.00,V,1,N,2,E,0,0,101126,,,N").c_str(), true);
  w.noteSentence(nmea("GNGGA,123457.00,1,N,2,E,0,08,0.9,1,M").c_str(), true);
  w.noteSentence(nmea("GPGSV,3,1,11").c_str(), true);
  w.noteSentence(nmea("GNZDA,123457.00,10,11,2026,00,00").c_str(), false);
  w.noteSentence("$GNZDA,broken", true);   // no checksum field: still tallied as GNZDA

  CHECK(w.typeCount() == 4);
  CHECK(strcmp(w.typeAt(0), "GNRMC") == 0 && w.countAt(0) == 2);
  CHECK(strcmp(w.typeAt(1), "GNGGA") == 0 && w.countAt(1) == 1);
  CHECK(strcmp(w.typeAt(2), "GPGSV") == 0 && w.countAt(2) == 1);
  CHECK(strcmp(w.typeAt(3), "GNZDA") == 0 && w.countAt(3) == 2);
  CHECK(w.typeAt(9) != nullptr && w.typeAt(9)[0] == 0);
  CHECK(w.countAt(-1) == 0 && w.countAt(99) == 0);
  CHECK(w.sawZda());
  CHECK(w.badSum() == 1);
  CHECK(w.droppedTypes() == 0);

  w.stop();
  CHECK(!w.active());
  CHECK(w.secondsLeft(31000) == 0);
  // A stopped window ignores sentences and starts clean next time.
  w.noteSentence(nmea("GNRMC,123458.00,V,1,N,2,E,0,0,101126,,,N").c_str(), true);
  CHECK(w.typeCount() == 4);
  CHECK(w.start(5, 40000));
  CHECK(w.typeCount() == 0 && w.badSum() == 0);
  CHECK(!w.sawZda());

  // No ZDA at all: the answer to "does the L76K send ZDA by default?".
  CHECK(!w.sawZda());
  w.noteSentence(nmea("GPGSV,3,1,11").c_str(), true);
  CHECK(!w.sawZda());
}

static void testNoFixGate() {
  NoFixTimeGate g;
  const uint32_t t = makeUtcEpoch(2026, 11, 10, 12, 0, 0);
  CHECK(!g.offer(t, true));       // first sentence: nothing to compare with
  CHECK(g.haveLast() && g.last() == t);
  CHECK(g.offer(t + 1, true));    // +1 s
  CHECK(g.offer(t + 2, true));    // +1 s
  CHECK(!g.offer(t + 2, true));   // frozen clock: step 0
  CHECK(g.offer(t + 4, true));    // +2 s: one missed sentence is tolerated
  CHECK(!g.offer(t + 9, true));   // +5 s: a jump or a restart
  CHECK(!g.offer(t + 10, false)); // no satellite tracked: refused...
  CHECK(g.offer(t + 11, true));   // ...but the chain moved on, so +1 is accepted next

  g.reset();
  CHECK(!g.haveLast());
  CHECK(!g.offer(t + 12, true));
  CHECK(g.offer(t + 13, true));

  // An insane epoch never enters the chain.
  NoFixTimeGate h;
  CHECK(!h.offer(0, true));
  CHECK(!h.haveLast());
}

static void testDefaultYear() {
  const int by = buildYear();
  CHECK(by >= 2024);
  char body[80];

  // RMC V with the module's 2080 default is refused even on the no-fix path.
  CHECK(!parseNmeaTime(nmea("GNRMC,123456.00,V,,N,,E,0.0,0.0,010180,,,N").c_str(), true).ok);
  // Impossible date.
  CHECK(!parseNmeaTime(nmea("GNRMC,123456.00,V,,N,,E,0.0,0.0,290225,,,N").c_str(), true).ok);
  // ZDA 2080 remains refused with the no-fix path allowed.
  CHECK(!parseNmeaTime(nmea("GNZDA,123456.00,01,01,2080,00,00").c_str(), true).ok);

  // A void RMC whose year matches the build passes the parser (the gate still decides the clock).
  snprintf(body, sizeof(body), "GNRMC,123456.00,V,,N,,E,0.0,0.0,10%02d%02d,,,N", 11, by % 100);
  const ParsedTime v = parseNmeaTime(nmea(body).c_str(), true);
  CHECK(v.ok);
  CHECK(!v.hasFix);
  CHECK(v.epoch == makeUtcEpoch(by, 11, 10, 12, 34, 56));

  // Without allowNoFix the same void sentence is still refused (today's rule).
  CHECK(!parseNmeaTime(nmea(body).c_str()).ok);

  // A fix RMC ignores the build-year gate (a fix date is real) and is marked as a fix.
  snprintf(body, sizeof(body), "GNRMC,123456.00,A,,N,,E,0.0,0.0,10%02d%02d,,,A", 11, by % 100);
  const ParsedTime a = parseNmeaTime(nmea(body).c_str());
  CHECK(a.ok);
  CHECK(a.hasFix);

  // ZDA is never a fix.
  char zda[64];
  snprintf(zda, sizeof(zda), "GNZDA,123456.00,10,11,%d,00,00", by);
  const ParsedTime z = parseNmeaTime(nmea(zda).c_str(), true);
  CHECK(z.ok);
  CHECK(!z.hasFix);
}

// --- the source ranking, on a fake clock ---------------------------------------------------------
static uint32_t g_fakeNow = 0;
static void fakeSet(uint32_t epoch) { g_fakeNow = epoch; }
static uint32_t fakeGet() { return g_fakeNow; }

static void testPrecedence() {
  const uint32_t saved = makeUtcEpoch(2026, 3, 1, 0, 0, 0);
  const uint32_t build = makeUtcEpoch(2026, 2, 1, 0, 0, 0);
  const uint32_t t = saved + 1000;

  // No-fix time is real enough to show, and it is marked approximate.
  TimeKeeper nofix;
  nofix.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(nofix.setFromSource(t, TimeSource::GpsNoFix, 100));
  CHECK(nofix.realSinceBoot());
  CHECK(nofix.approximate());
  CHECK(nofix.source() == TimeSource::GpsNoFix);
  CHECK(nofix.now() == t);
  CHECK(strcmp(timeSourceName(TimeSource::GpsNoFix), "gps-nofix") == 0);

  // A fix replaces it at once (no 6-hour wait).
  CHECK(nofix.setFromSource(t + 10, TimeSource::Gps, 200));
  CHECK(!nofix.approximate());
  CHECK(nofix.source() == TimeSource::Gps);

  // Wi-Fi replaces it at once.
  TimeKeeper wifi;
  wifi.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(wifi.setFromSource(t, TimeSource::GpsNoFix, 100));
  CHECK(wifi.setFromSource(t + 10, TimeSource::Wifi, 200));
  CHECK(wifi.source() == TimeSource::Wifi);

  // Phone replaces it at once.
  TimeKeeper phone;
  phone.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(phone.setFromSource(t, TimeSource::GpsNoFix, 100));
  CHECK(phone.setFromSource(t + 10, TimeSource::Phone, 200));
  CHECK(phone.source() == TimeSource::Phone);

  // Manual replaces it at once.
  TimeKeeper manual;
  manual.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(manual.setFromSource(t, TimeSource::GpsNoFix, 100));
  CHECK(manual.setFromSource(t + 10, TimeSource::Manual, 200));
  CHECK(manual.source() == TimeSource::Manual);

  // And it can never overwrite any of them.
  TimeKeeper overWifi;
  overWifi.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(overWifi.setFromSource(t, TimeSource::Wifi, 100));
  CHECK(!overWifi.setFromSource(t + 10, TimeSource::GpsNoFix, 200));
  CHECK(overWifi.source() == TimeSource::Wifi);

  TimeKeeper overFix;
  overFix.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(overFix.setFromSource(t, TimeSource::Gps, 100));
  CHECK(!overFix.setFromSource(t + 10, TimeSource::GpsNoFix, 200));
  CHECK(overFix.source() == TimeSource::Gps);

  TimeKeeper overPhone;
  overPhone.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(overPhone.setFromSource(t, TimeSource::Phone, 100));
  CHECK(!overPhone.setFromSource(t + 10, TimeSource::GpsNoFix, 200));
  CHECK(overPhone.source() == TimeSource::Phone);

  TimeKeeper overManual;
  overManual.begin(saved, build, 0, fakeSet, fakeGet);
  CHECK(overManual.setFromSource(t, TimeSource::Manual, 100));
  CHECK(!overManual.setFromSource(t + 10, TimeSource::GpsNoFix, 200));
  CHECK(overManual.source() == TimeSource::Manual);

  // The full ranking is the ticket's: fix > Wi-Fi > phone > manual > no-fix > none.
  CHECK(timeSourcePriority(TimeSource::Gps) > timeSourcePriority(TimeSource::Wifi));
  CHECK(timeSourcePriority(TimeSource::Wifi) > timeSourcePriority(TimeSource::Phone));
  CHECK(timeSourcePriority(TimeSource::Phone) > timeSourcePriority(TimeSource::Manual));
  CHECK(timeSourcePriority(TimeSource::Manual) > timeSourcePriority(TimeSource::GpsNoFix));
  CHECK(timeSourcePriority(TimeSource::GpsNoFix) > timeSourcePriority(TimeSource::None));
}

int main() {
  testTypeToken();
  testFormatRmc();
  testFormatZda();
  testFormatGga();
  testFormatGsa();
  testWindow();
  testNoFixGate();
  testDefaultYear();
  testPrecedence();
  if (g_fail) {
    printf("%d gps_raw check(s) failed\n", g_fail);
    return 1;
  }
  printf("gps_raw: all checks passed\n");
  return 0;
}
