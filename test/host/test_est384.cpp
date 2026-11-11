// SPDX-License-Identifier: MIT
// EST-384 host test: the node's own position source (pure C++, the same header the firmware
// builds). Proves the ticket's precedence and storage rules:
//   1. Fixed beats a fresh GPS fix; when Fixed is on, a later fix never moves the node;
//   2. GPS is used when GPS is selected (and none when there is no fix);
//   3. the phone's set-location switches to Fixed and survives a later GPS fix;
//   4. the NVS record round-trips, and an invalid Fixed record falls back to GPS;
//   5. the status token is one of gps / fixed / none and carries no coordinate.
//
// The numbers below are a synthetic grid (10.x / 20.x), not any node's position (REBUILD.md rule 9).
#include <stdio.h>
#include <string.h>

#include "position_source.h"

using namespace om;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static bool near(double a, double b) { return a > b - 1e-9 && a < b + 1e-9; }

static void testDefaults() {
  PositionSource s;
  CHECK(!s.own().valid);
  CHECK(!s.own().fixed);
  CHECK(s.source() == kPosSourceGps);
  CHECK(strcmp(s.statusToken(), "none") == 0);

  // GPS selected with a fix: used, and it is not the Fixed pin.
  s.setGpsFix(10.2500, 20.5000, true);
  CHECK(s.own().valid);
  CHECK(!s.own().fixed);
  CHECK(near(s.own().lat, 10.2500));
  CHECK(near(s.own().lon, 20.5000));
  CHECK(strcmp(s.statusToken(), "gps") == 0);

  // An invalid fix clears the point again.
  s.setGpsFix(0, 0, false);
  CHECK(!s.own().valid);
  CHECK(strcmp(s.statusToken(), "none") == 0);
  // A fix outside the range is refused by the same validity rule.
  s.setGpsFix(91.0, 20.0, true);
  CHECK(!s.own().valid);
  s.setGpsFix(0.0, 0.0, true);   // (0,0) is MeshCore's "no position"
  CHECK(!s.own().valid);
}

static void testFixedBeatsGps() {
  PositionSource s;
  s.setGpsFix(10.2000, 20.6000, true);
  CHECK(s.selectFixed(10.1000, 20.4000));
  CHECK(s.sourceFixed());
  CHECK(s.own().valid);
  CHECK(s.own().fixed);
  CHECK(near(s.own().lat, 10.1000));
  CHECK(near(s.own().lon, 20.4000));
  CHECK(strcmp(s.statusToken(), "fixed") == 0);

  // A FRESH fix arrives while Fixed is on: the point must not move.
  s.setGpsFix(10.3000, 20.7000, true);
  CHECK(s.own().valid);
  CHECK(s.own().fixed);
  CHECK(near(s.own().lat, 10.1000));
  CHECK(near(s.own().lon, 20.4000));
  CHECK(strcmp(s.statusToken(), "fixed") == 0);

  // Back to GPS: the SAME source uses the latest fix at once.
  s.selectGps();
  CHECK(!s.own().fixed);
  CHECK(s.own().valid);
  CHECK(near(s.own().lat, 10.3000));
  CHECK(near(s.own().lon, 20.7000));
  CHECK(strcmp(s.statusToken(), "gps") == 0);
}

static void testPhoneSetLocation() {
  PositionSource s;
  s.setGpsFix(10.2000, 20.6000, true);
  CHECK(s.phoneSetLocation(10.0500, 20.5100));
  CHECK(s.sourceFixed());
  CHECK(s.own().fixed);
  CHECK(near(s.own().lat, 10.0500));
  CHECK(near(s.own().lon, 20.5100));
  // The device's GPS keeps running, but cannot fight the phone.
  s.setGpsFix(10.4000, 20.8000, true);
  CHECK(near(s.own().lat, 10.0500));
  CHECK(near(s.own().lon, 20.5100));
  CHECK(s.hasGpsFix());   // still there for the clock path

  // A second phone set replaces the point (the phone is the user's latest intent).
  CHECK(s.phoneSetLocation(10.0600, 20.5200));
  CHECK(near(s.own().lat, 10.0600));

  // An invalid phone point is refused and changes nothing.
  CHECK(!s.phoneSetLocation(91.0, 20.0));
  CHECK(near(s.own().lat, 10.0600));
  CHECK(!s.phoneSetLocation(0.0, 0.0));
  CHECK(near(s.own().lat, 10.0600));
}

static void testNvsRoundTrip() {
  PositionSource s;
  s.setGpsFix(10.2500, 20.5000, true);
  CHECK(s.selectFixed(10.1100, 20.4100));
  StoredPosition st = s.stored();
  CHECK(st.source == kPosSourceFixed);
  CHECK(near(st.lat, 10.1100));
  CHECK(near(st.lon, 20.4100));

  PositionSource boot;
  boot.restore(st.source, st.lat, st.lon);
  CHECK(boot.sourceFixed());
  CHECK(boot.fixedSet());
  CHECK(boot.own().valid);
  CHECK(boot.own().fixed);
  CHECK(near(boot.own().lat, 10.1100));
  CHECK(near(boot.own().lon, 20.4100));
  // No GPS fix at boot (indoors): the saved Fixed point still works.
  CHECK(!boot.hasGpsFix());
  CHECK(strcmp(boot.statusToken(), "fixed") == 0);

  // Saving GPS clears the point in the record; a later boot is the default.
  s.selectGps();
  st = s.stored();
  CHECK(st.source == kPosSourceGps);
  CHECK(st.lat == 0.0 && st.lon == 0.0);
  boot.restore(st.source, st.lat, st.lon);
  CHECK(!boot.sourceFixed());
  CHECK(strcmp(boot.statusToken(), "none") == 0);

  // A corrupted Fixed record (missing or out-of-range point) falls back to GPS, never to a guess.
  boot.restore(kPosSourceFixed, 0.0, 0.0);
  CHECK(!boot.sourceFixed());
  CHECK(!boot.own().valid);
  boot.restore(kPosSourceFixed, 999.0, 20.0);
  CHECK(!boot.sourceFixed());
  // A source byte that is not one of the two values is the default too.
  boot.restore(77, 10.1, 20.4);
  CHECK(!boot.sourceFixed());
  CHECK(!boot.fixedSet());
}

static void testTokenHasNoCoordinates() {
  PositionSource s;
  s.setGpsFix(10.2500, 20.5000, true);
  char text[40];
  snprintf(text, sizeof(text), "%s", s.statusToken());
  CHECK(strstr(text, "10") == nullptr);
  CHECK(strstr(text, "20") == nullptr);
  CHECK(strchr(text, '.') == nullptr);
  CHECK(strcmp(text, "gps") == 0);
  s.selectFixed(10.1000, 20.4000);
  snprintf(text, sizeof(text), "%s", s.statusToken());
  CHECK(strstr(text, "10") == nullptr);
  CHECK(strcmp(text, "fixed") == 0);
}

int main() {
  testDefaults();
  testFixedBeatsGps();
  testPhoneSetLocation();
  testNvsRoundTrip();
  testTokenHasNoCoordinates();
  if (g_fail) {
    printf("%d position-source check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("est384: all checks passed\n");
  return 0;
}
