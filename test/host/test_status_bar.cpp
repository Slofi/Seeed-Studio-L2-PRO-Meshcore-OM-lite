// Host test for the new top bar (EST-337) and the Nodes header.
//
// Bar: time on the left (mono, ink, five glyphs always), then from the right edge the gear, the
// battery (percent + icon), the GPS cell and the status icons that appear only when they are on
// (DND, Wi-Fi, Bluetooth). Every adjacent pair is exactly one kStatusGap; the fault mark follows the
// clock. The worst case (all icons on, "100%", "GPS 14") must fit at 320 px.
#include <stdio.h>
#include <string.h>

#include "ui/layout.h"
#include "ui/nodes_header.h"
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

// The mono face advances 6 px per glyph; the tests use the same arithmetic widgets.cpp measures.
static int w(const char* s) {
  int n = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
    if ((*p & 0xC0) != 0x80) ++n;
  }
  return n * kMonoStatusAdvancePx;
}

static StatusCellWidths worstWidths() {
  StatusCellWidths ww;
  ww.clock = w("08:53");
  ww.pct = w("100%");
  ww.gps = w("GPS 14");
  return ww;
}

static void checkVertical(const Rect& r, const Layout& L) {
  CHECK(r.y >= L.status.y);
  CHECK(r.bottom() <= L.status.bottom());
}

static void testAllOff() {
  const Layout& L = l2();
  const StatusCellWidths ww = worstWidths();
  const StatusCellFlags f;
  const StatusBarRects r = statusBarRects(L, ww, f);

  // LEFT: the clock, in the left padding, five glyphs wide.
  CHECK(r.time.x == kStatusPadX);
  CHECK(r.time.w == kStatusClockW);
  CHECK(r.time.right() == kStatusPadX + kStatusClockW);
  checkVertical(r.time, L);

  // No fault, no optional icons: empty rectangles, nothing is drawn and nothing can be tapped.
  CHECK(r.fault.empty());
  CHECK(r.dnd.empty());
  CHECK(r.wifi.empty());
  CHECK(r.bt.empty());
  CHECK(r.leftEnd == r.time.right());

  // RIGHT, from the edge: gear, battery cell, GPS.
  CHECK(r.gear.x == L.gear().x && r.gear.y == L.gear().y);
  CHECK(r.gear.right() == kScreenW - kStatusPadX);
  CHECK(r.battText.right() == r.gear.x - kStatusGap);
  CHECK(r.battery.x == r.battIcon.x);
  CHECK(r.battery.right() == r.battText.right());
  CHECK(r.battText.x == r.battIcon.x + kBattW + kBattGap);
  CHECK(r.gps.right() == r.battery.x - kStatusGap);
  CHECK(r.rightStart == r.gps.x);
  CHECK(r.fits);
  checkVertical(r.gear, L);
  checkVertical(r.battery, L);
  checkVertical(r.gps, L);
}

static void testAllIconsOn() {
  const Layout& L = l2();
  const StatusCellWidths ww = worstWidths();
  StatusCellFlags f;
  f.dnd = f.wifi = f.bt = true;
  f.fault = true;
  const StatusBarRects r = statusBarRects(L, ww, f);

  // The fault mark: one gap after the clock, the same rectangle Layout hands the tap test.
  CHECK(!r.fault.empty());
  CHECK(r.fault.x == r.time.right() + kStatusGap);
  CHECK(r.fault.w == kStatusIconFault && r.fault.h == kStatusIconFault);
  CHECK(r.fault.x == L.radioFault().x && r.fault.y == L.radioFault().y);
  CHECK(r.leftEnd == r.fault.right());
  checkVertical(r.fault, L);

  // The right chain, left to right: Bluetooth, Wi-Fi, DND, GPS, battery, gear. Every adjacent pair
  // is exactly kStatusGap, which is what "right-aligned with one gap" means.
  CHECK(!r.bt.empty() && !r.wifi.empty() && !r.dnd.empty());
  CHECK(r.bt.right() + kStatusGap == r.wifi.x);
  CHECK(r.wifi.right() + kStatusGap == r.dnd.x);
  CHECK(r.dnd.right() + kStatusGap == r.gps.x);
  CHECK(r.gps.right() + kStatusGap == r.battery.x);
  CHECK(r.battery.right() + kStatusGap == r.gear.x);
  CHECK(r.bt.w == kStatusIconBtW && r.bt.h == kStatusIconBtH);
  CHECK(r.wifi.w == kStatusIconWifi && r.wifi.h == kStatusIconWifi);
  CHECK(r.dnd.w == kStatusIconDnd && r.dnd.h == kStatusIconDnd);
  checkVertical(r.bt, L);
  checkVertical(r.wifi, L);
  checkVertical(r.dnd, L);

  // The left group and the right group keep one gap too.
  CHECK(r.rightStart == r.bt.x);
  CHECK(r.leftEnd + kStatusGap <= r.rightStart);
  CHECK(r.fits);
}

static void testOneOptionalAtATime() {
  const Layout& L = l2();
  const StatusCellWidths ww = worstWidths();

  StatusCellFlags onlyDnd;
  onlyDnd.dnd = true;
  StatusBarRects r = statusBarRects(L, ww, onlyDnd);
  CHECK(!r.dnd.empty() && r.wifi.empty() && r.bt.empty());
  CHECK(r.dnd.right() + kStatusGap == r.gps.x);
  CHECK(r.rightStart == r.dnd.x);

  StatusCellFlags onlyWifi;
  onlyWifi.wifi = true;
  r = statusBarRects(L, ww, onlyWifi);
  CHECK(r.dnd.empty() && !r.wifi.empty() && r.bt.empty());
  CHECK(r.wifi.right() + kStatusGap == r.gps.x);
  CHECK(r.rightStart == r.wifi.x);

  StatusCellFlags onlyBt;
  onlyBt.bt = true;
  r = statusBarRects(L, ww, onlyBt);
  CHECK(r.dnd.empty() && r.wifi.empty() && !r.bt.empty());
  CHECK(r.bt.right() + kStatusGap == r.gps.x);
  CHECK(r.rightStart == r.bt.x);
}

static void testFit() {
  const Layout& L = l2();
  const StatusCellWidths ww = worstWidths();

  StatusCellFlags all;
  all.fault = all.dnd = all.wifi = all.bt = true;
  // The worst case the ticket names: all icons on, 100 %, "GPS 14" (36 px) and the fault mark.
  CHECK(statusBarFits(L, ww, all));

  // A screen too narrow to hold the two groups is reported, not drawn into the left one.
  const Layout narrow = layoutFor(200, 240);
  CHECK(!statusBarFits(narrow, ww, all));

  // The Tanmatsu arrangement (800 px) has plenty of room and the same arithmetic.
  const Layout tan = layoutFor(800, 480);
  const StatusBarRects tr = statusBarRects(tan, ww, all);
  CHECK(tr.fits);
  CHECK(tr.gear.right() == 800 - kStatusPadX);
  CHECK(tr.bt.right() + kStatusGap == tr.wifi.x);
}

static void testSilentClockAndGps() {
  const Layout& L = l2();
  StatusCellWidths ww;
  ww.clock = w("--:--");
  ww.pct = w("--");
  ww.gps = w("GPS --");
  CHECK(ww.clock == kStatusClockW);   // the fault slot never moves when the time arrives
  StatusCellFlags f;
  f.fault = true;
  const StatusBarRects r = statusBarRects(L, ww, f);
  CHECK(r.time.w == kStatusClockW);
  CHECK(r.fault.x == r.time.right() + kStatusGap);
  CHECK(r.fits);
}

static void headerIs(int nodes, int lastHour, bool known, const char* want) {
  char buf[64];
  nodesHeaderText(buf, sizeof(buf), nodes, lastHour, known);
  if (strcmp(buf, want) != 0) {
    printf("FAIL nodesHeaderText(%d,%d,%d) = \"%s\", want \"%s\"\n", nodes, lastHour,
           (int)known, buf, want);
    ++g_fail;
  }
}

static void testNodesHeader() {
  headerIs(0, 0, false, "0 nodes heard");
  headerIs(1, 0, false, "1 node heard");
  headerIs(4, 0, false, "4 nodes heard");
  headerIs(12, 5, false, "12 nodes heard");   // unknown timestamps: no second clause
  headerIs(4, 2, true, "4 nodes heard \xC2\xB7 2 in the last hour");
  headerIs(1, 0, true, "1 node heard \xC2\xB7 0 in the last hour");
  headerIs(4, -2, true, "4 nodes heard \xC2\xB7 0 in the last hour");
  headerIs(1000, 1000, true, "999 nodes heard \xC2\xB7 999 in the last hour");
  headerIs(-3, 0, false, "0 nodes heard");

  char buf[64];
  nodesHeaderText(nullptr, sizeof(buf), 4, 2, true);   // no crash, nothing written
  nodesHeaderText(buf, 0, 4, 2, true);
  nodesHeaderText(buf, 8, 4, 2, true);                 // truncated but terminated
  CHECK(strlen(buf) < 8);
}

int main() {
  testAllOff();
  testAllIconsOn();
  testOneOptionalAtATime();
  testFit();
  testSilentClockAndGps();
  testNodesHeader();
  if (g_fail) {
    printf("%d status-bar check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("status bar: all checks passed\n");
  return 0;
}
