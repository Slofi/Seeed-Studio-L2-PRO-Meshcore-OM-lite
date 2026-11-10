// Host test for the two text rules in this ticket that a host can prove without a panel:
//   * ui/age.cpp: a stored "last heard" older than the build says "--" (the old 2024 clock), and
//     beyond a month says "30d+";
//   * ui/status_cells.h: the status bar's GPS cell text and the arithmetic that keeps "GPS 0/5" (and
//     its short form) out of the left-hand group at 320 px.
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

  // --- with a fix: the satellites USED ---------------------------------------
  c = GpsCell();
  c.valid = true;
  c.talked = true;
  c.used = 7;
  c.inView = 20;
  char out[16];
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 7") == 0);
  gpsCellTextShort(out, sizeof(out), c);
  CHECK(strcmp(out, "7") == 0);

  // --- searching: used / in view, the ticket's own example -------------------
  c = GpsCell();
  c.talked = true;
  c.used = 0;
  c.inView = 5;
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 0/5") == 0);
  CHECK(width(out) == 42);
  gpsCellTextShort(out, sizeof(out), c);
  CHECK(strcmp(out, "0/5") == 0);

  // --- the module has never said anything ------------------------------------
  c = GpsCell();
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS --") == 0);
  gpsCellTextShort(out, sizeof(out), c);
  CHECK(strcmp(out, "--") == 0);

  // --- two digits stay inside the cap; three clamp to 99 ---------------------
  c = GpsCell();
  c.talked = true;
  c.used = 0;
  c.inView = 24;
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 0/24") == 0);
  c.inView = 300;
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 0/99") == 0);
  c.valid = true;
  c.used = 250;
  c.inView = 300;
  gpsCellText(out, sizeof(out), c);
  CHECK(strcmp(out, "GPS 99") == 0);
  CHECK(width(out) <= kGpsCellMax * kMonoStatusAdvancePx);
  CHECK((int)strlen("GPS 0/99") <= kGpsCellMax);

  // --- the row fits: measure it the way widgets.cpp does ---------------------
  // Left group: pad 6 + dot 5 + gap 6 + "MC · EU868 · 2B" (15 glyphs).
  const int leftEnd = kStatusPadX + kStatusDot + kStatusGap + width("MC \xC2\xB7 EU868 \xC2\xB7 2B");
  CHECK(leftEnd == 107);

  // Normal case: two nodes, 93 %, 08:20, "GPS 0/5".
  StatusRightWidths w;
  w.nodes = width("2 nodes");
  w.gps = width("GPS 0/5");
  w.pct = width("93%");
  w.clock = width("08:20");
  CHECK(statusRightWidth(w) == 199);
  CHECK(statusRowFits(kScreenW, leftEnd, w));

  // The tightest realistic case: "12 nodes", a full battery, two-digit in-view. The full cell
  // would run into the left group; the short one must not.
  w.nodes = width("12 nodes");
  w.gps = width("GPS 0/24");
  w.pct = width("100%");
  w.clock = width("08:20");
  CHECK(!statusRowFits(kScreenW, leftEnd, w));
  w.gps = width("0/24");
  CHECK(statusRowFits(kScreenW, leftEnd, w));

  // Even three digits of nodes and the widest short cell (two digits each) still fit.
  w.nodes = width("99 nodes");
  w.gps = width("0/99");
  CHECK(statusRowFits(kScreenW, leftEnd, w));
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
