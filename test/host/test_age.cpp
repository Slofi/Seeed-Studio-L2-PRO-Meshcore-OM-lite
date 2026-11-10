// Host test for the two text rules this file owns (EST-335; the status-bar arithmetic moved to
// test_status_bar.cpp with the new top bar, EST-337):
//   * ui/age.cpp: a stored "last heard" older than the build says "--" (the old 2024 clock), and
//     beyond a month says "30d+";
//   * ui/status_cells.h: the GPS cell text, one number (heard), dim searching / fix colour.
#include <stdio.h>
#include <string.h>

#include "ui/age.h"
#include "ui/status_cells.h"

using namespace om::ui;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// One build instant to reason from: 2026-11-10 07:18:53 UTC.
static constexpr uint32_t kB = 1794295133UL;

static bool says(uint32_t lastmod, uint32_t now, uint32_t build, const char* want) {
  char out[8];
  ageText(out, sizeof(out), lastmod, now, build);
  if (strcmp(out, want) != 0) {
    printf("FAIL age(lastmod=%lu now=%lu build=%lu) = \"%s\", want \"%s\"\n",
           (unsigned long)lastmod, (unsigned long)now, (unsigned long)build, out, want);
    ++g_fail;
    return false;
  }
  return true;
}

static void testAge() {
  const uint32_t now = kB + 40UL * 86400UL;   // 40 days of uptime after the build

  // --- the stale 2024 stamp: "--", never "30d+" -------------------------------
  says(1700000000UL, now, kB, "--");     // 2023-11-14, an old-boot clock
  says(kB - 1, now, kB, "--");           // one second before the build: impossible
  says(kB - 86400UL, now, kB, "--");     // a day before the build: impossible
  // The same stamp with no build time to compare against is just a very old age (the rule is off).
  says(kB - 1000UL, kB + 3600UL, 0, "1h");

  // --- unknown / not yet heard ------------------------------------------------
  says(0, now, kB, "--");
  says(kB, 0, kB, "--");                 // no clock at all
  says(now + 3600UL, now, kB, "--");     // in the future: not an age

  // --- the normal ladder still works (every stamp after the build) -----------
  says(now - 10UL, now, kB, "now");
  says(now - 60UL, now, kB, "1m");
  says(now - 3000UL, now, kB, "50m");
  says(now - 7200UL, now, kB, "2h");
  says(now - 3UL * 86400UL, now, kB, "3d");
  says(now - 29UL * 86400UL, now, kB, "29d");
  says(now - 31UL * 86400UL, now, kB, "30d+");
  // The "no 730d" rule on its own, with no build time to rule the stamp out.
  says(now - 730UL * 86400UL, now, 0, "30d+");

  // A row that has just been refreshed after the old stamp is replaced by the advert: a real age.
  says(now - 100UL, now, kB, "1m");
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

  // --- ONE number, the satellites HEARD right now, while searching -----------
  c = GpsCell();
  c.talked = true;
  c.heard = 4;
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 4") == 0);
  CHECK(width(out) == 30);

  // With a fix the number stays the heard count: the fix is the colour (kGreen in widgets.cpp), not
  // a second number. The old "GPS 7" (used) and "GPS 0/4" (used/heard) are gone (EST-336).
  c.valid = true;
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 4") == 0);
  CHECK(width(out) == 30);

  // Indoors, with the almanac listing 13 satellites and nothing heard: "GPS 0", never "0/13". The
  // expected count stays in the heartbeat.
  c.heard = 0;
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 0") == 0);

  // --- the module has never said anything ------------------------------------
  c = GpsCell();
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS --") == 0);

  // --- two digits stay inside the cap; three clamp to 99 ---------------------
  c = GpsCell();
  c.talked = true;
  c.heard = 24;
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 24") == 0);
  c.heard = 300;
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 99") == 0);
  CHECK(width(out) <= kGpsCellMax * kMonoStatusAdvancePx);
  CHECK((int)strlen("GPS 99") <= kGpsCellMax);
}

int main() {
  testAge();
  testStatusCells();
  if (g_fail) {
    printf("%d age/status check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("age/status: all checks passed\n");
  return 0;
}
