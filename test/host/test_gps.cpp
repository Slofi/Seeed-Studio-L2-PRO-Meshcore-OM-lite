// Host test for the GSV half of the GNSS parser (src/gps_sky.h), the part of EST-335 that a host can
// prove without a device:
//   * the indoor case that started the ticket: GSV lines whose C/N0 field is EMPTY for every
//     satellite ("$GPGSV,3,1,10,04,05,091,,...") say what the almanac EXPECTS, so heard is 0;
//   * a mixed cycle counts only the satellites whose C/N0 field has a value above zero (an empty
//     field and a literal "0" are both "not heard");
//   * per-constellation heard counts, the GN-only fallback, and a new cycle starting the count over;
//   * the best C/N0 of the last 60 s (1 s buckets: the strongest sample expires, the best of what is
//     left takes over) and the most satellites heard at once since boot.
//
// The sentences are real NMEA with real checksums, so the test also proves the checksum the board
// uses to throw a garbled line away.
#include <stdio.h>
#include <string.h>

#include "gps_sky.h"

using namespace om;

static int g_fail = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
      ++g_fail;                                                  \
    }                                                            \
  } while (0)

#define CHECK_EQ(got, want)                                                        \
  do {                                                                             \
    const long a_ = (long)(got), b_ = (long)(want);                                \
    if (a_ != b_) {                                                                \
      printf("FAIL %s:%d  %s = %ld, want %ld\n", __FILE__, __LINE__, #got, a_, b_); \
      ++g_fail;                                                                    \
    }                                                                              \
  } while (0)

// The L76K indoors: every satellite with elevation and azimuth, not one C/N0 value.
static const char* kIndoor1 = "$GPGSV,3,1,10,04,05,091,,05,35,311,,10,31,082,,15,20,300,*7F";
static const char* kIndoor2 = "$GPGSV,3,2,10,18,62,120,,24,15,045,,28,10,210,,29,41,180,*7B";
static const char* kIndoor3 = "$GPGSV,3,3,10,31,22,330,,32,10,020,*78";
// Mixed (one satellite really received, one blank, one received, one literal "0" = not measured).
static const char* kMixed1 = "$GPGSV,2,1,08,04,05,091,31,05,35,311,,10,31,082,22,15,20,300,0*45";
static const char* kMixed2 = "$GPGSV,2,2,08,18,62,120,45,24,15,045,12,28,10,210,,29,41,180,*71";
static const char* kGlonass = "$GLGSV,1,1,04,68,10,120,28,69,20,200,25,70,30,280,,71,40,010,*62";
static const char* kBeiDou = "$BDGSV,1,1,05,01,30,090,33,02,40,180,18,03,50,270,,04,60,000,29,05,10,120,*5D";
static const char* kGalileo = "$GAGSV,1,1,03,11,15,060,41,12,25,150,,13,35,240,26*5A";
static const char* kCombined = "$GNGSV,1,1,09,01,10,020,30,02,20,110,28,03,30,200,22,04,40,290,35*6A";

static GpsState fresh() { return GpsState(); }

// --- 1. the indoor case: 10 expected, 0 heard ---------------------------------
static void testIndoorEmptyCno() {
  GpsState s = fresh();
  CHECK(gpsNmeaChecksumOk(kIndoor1));
  CHECK(gpsNmeaChecksumOk(kIndoor2));
  CHECK(gpsNmeaChecksumOk(kIndoor3));

  CHECK(gpsParseGsv(kIndoor1, s, 0));
  CHECK_EQ(s.expected[kGpsUs], 10);
  CHECK_EQ(s.expectedTotal, 10);
  CHECK_EQ(s.heard[kGpsUs], 0);
  CHECK_EQ(s.heardTotal, 0);
  CHECK_EQ(s.bestSnr, 0);

  CHECK(gpsParseGsv(kIndoor2, s, 1000));
  CHECK(gpsParseGsv(kIndoor3, s, 2000));
  // Nothing heard anywhere in the cycle: the cell must read "GPS 0/0", never "GPS 0/13".
  CHECK_EQ(s.expectedTotal, 10);
  CHECK_EQ(s.heardTotal, 0);
  CHECK_EQ(s.maxHeard, 0);
  CHECK_EQ(s.bestSnr, 0);
}

// --- 2. a mixed cycle counts only the filled C/N0 fields ----------------------
static void testMixedCycle() {
  GpsState s = fresh();
  CHECK(gpsParseGsv(kMixed1, s, 0));
  CHECK_EQ(s.expected[kGpsUs], 8);
  // 04 has 31, 10 has 22; 05 is blank and 15 is a literal 0: two heard, four expected in the line.
  CHECK_EQ(s.heard[kGpsUs], 2);
  CHECK_EQ(s.heardTotal, 2);
  CHECK_EQ(s.bestSnr, 31);
  CHECK_EQ(s.maxHeard, 2);

  CHECK(gpsParseGsv(kMixed2, s, 1500));
  // 18 has 45 and 24 has 12; 28 blank and 29 blank.
  CHECK_EQ(s.heard[kGpsUs], 4);
  CHECK_EQ(s.heardTotal, 4);
  CHECK_EQ(s.bestSnr, 45);
  CHECK_EQ(s.maxHeard, 4);

  // A new cycle starts the heard count over; the most-heard record does not move.
  CHECK(gpsParseGsv(kMixed1, s, 3000));
  CHECK_EQ(s.heard[kGpsUs], 2);
  CHECK_EQ(s.heardTotal, 2);
  CHECK_EQ(s.maxHeard, 4);
}

// --- 3. per constellation, and the totals ------------------------------------
static void testConstellations() {
  GpsState s = fresh();
  CHECK(gpsParseGsv(kGlonass, s, 0));
  CHECK(gpsParseGsv(kBeiDou, s, 0));
  CHECK(gpsParseGsv(kGalileo, s, 0));
  CHECK_EQ(s.expected[kGpsGlonass], 4);
  CHECK_EQ(s.heard[kGpsGlonass], 2);   // 28 and 25
  CHECK_EQ(s.expected[kGpsBeiDou], 5);
  CHECK_EQ(s.heard[kGpsBeiDou], 3);    // 33, 18, 29
  CHECK_EQ(s.expected[kGpsGalileo], 3);
  CHECK_EQ(s.heard[kGpsGalileo], 2);   // 41 and 26
  CHECK_EQ(s.expectedTotal, 12);
  CHECK_EQ(s.heardTotal, 7);
  CHECK_EQ(s.maxHeard, 7);
  CHECK_EQ(s.bestSnr, 41);

  // A GNGSV-only receiver reports the whole sky in GN: with no named constellation the totals are
  // GN's own. Once a named constellation reports, GN is no longer counted (it would double everyone).
  GpsState g = fresh();
  CHECK(gpsParseGsv(kCombined, g, 0));
  CHECK_EQ(g.expectedTotal, 9);
  CHECK_EQ(g.heardTotal, 4);
  CHECK(g.namedGsv == false);
  CHECK(gpsParseGsv(kIndoor1, g, 0));
  CHECK_EQ(g.expectedTotal, 10);
  CHECK_EQ(g.heardTotal, 0);   // GP's own 0 is a real 0, not a reason to fall back to GN's 5
}

// --- 4. the best C/N0 of the last 60 s ---------------------------------------
static void testSnrWindow() {
  GpsState s = fresh();
  CHECK(gpsParseGsv(kMixed1, s, 0));      // 31 at t = 0
  CHECK_EQ(s.bestSnr, 31);
  gpsSkyRefresh(s, 59999);                // 59.999 s later: still inside the window
  CHECK_EQ(s.bestSnr, 31);
  gpsSkyRefresh(s, 60000);                // 60 s: the sample is out of the window
  CHECK_EQ(s.bestSnr, 0);

  // A strong sample expires while a weaker recent one stays: the best of the window takes over.
  GpsState w = fresh();
  CHECK(gpsParseGsv(kMixed2, w, 0));      // 45 at t = 0
  CHECK(gpsParseGsv(kMixed1, w, 30000));  // 31 at t = 30 s
  CHECK_EQ(w.bestSnr, 45);
  gpsSkyRefresh(w, 61000);                // 45 is 61 s old, 31 is 31 s old
  CHECK_EQ(w.bestSnr, 31);

  // One second holds its strongest value only.
  GpsSnrWindow bucket;
  bucket.add(0, 20);
  bucket.add(0, 35);
  bucket.add(400, 22);                    // same second, weaker: the peak stays 35
  CHECK_EQ(bucket.best(400), 35);
  CHECK_EQ(bucket.best(59900), 35);
  CHECK_EQ(bucket.best(60400), 0);        // (uint16_t)(60 - 0) >= 60

  // A 16-bit second wraps; 60 s of unsigned arithmetic still says "inside".
  GpsSnrWindow wrapped;
  wrapped.add(65530000UL, 40);            // second 65530
  CHECK_EQ(wrapped.best(65541000UL), 40); // second 5 after the wrap: 11 s ago
  CHECK_EQ(wrapped.best(65601000UL), 0);  // second 65 after the wrap: 71 s ago

  bucket.reset();
  CHECK_EQ(bucket.best(0), 0);
}

// --- 5. reset, and the checksum ---------------------------------------------
static void testResetAndChecksum() {
  GpsState s = fresh();
  CHECK(gpsParseGsv(kMixed2, s, 0));
  CHECK_EQ(s.heardTotal, 2);
  gpsResetSky(s);
  CHECK_EQ(s.expectedTotal, 0);
  CHECK_EQ(s.heardTotal, 0);
  CHECK_EQ(s.maxHeard, 0);
  CHECK_EQ(s.bestSnr, 0);
  CHECK(s.namedGsv == false);

  // One wrong character in the body must fail the checksum (board.cpp skips that line).
  char bad[96];
  snprintf(bad, sizeof(bad), "%s", kMixed1);
  bad[17] = '9';   // 04 -> 94
  CHECK(!gpsNmeaChecksumOk(bad));
  CHECK(!gpsNmeaChecksumOk("$GPGSV,1,1,00"));
  CHECK(!gpsNmeaChecksumOk(nullptr));
  // A line that is not GSV is not counted (constellationOf returns -1 only for a bad '$' prefix).
  CHECK(!gpsParseGsv("GPGSV,1,1,00*00", s, 0));   // not a sentence: no '$'
}

int main() {
  testIndoorEmptyCno();
  testMixedCycle();
  testConstellations();
  testSnrWindow();
  testResetAndChecksum();
  if (g_fail) {
    printf("%d gps check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("gps: all checks passed\n");
  return 0;
}
