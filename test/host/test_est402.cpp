// SPDX-License-Identifier: MIT
// EST-402, the C++ half: the charging bolt's debounce and its "unknown = no bolt" rule, plus the
// pixel shape's bounds. No Arduino, no LovyanGFX: charge_state.h and battery_bolt.h are plain C++.
#include <stdio.h>

#include "charge_state.h"
#include "ui/battery_bolt.h"

using om::ChargeBolt;
using om::VbusRead;
using om::VbusState;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// Feed `n` copies of one read; return how many of them reported a state change.
static int feed(ChargeBolt& b, VbusRead read, int n) {
  int changes = 0;
  for (int i = 0; i < n; ++i) {
    if (b.sample(read)) ++changes;
  }
  return changes;
}

static void testConstants() {
  CHECK(om::kChargePollMs == 2000);   // the ticket's 2 s
  CHECK(om::ui::kBoltW == 5);
  CHECK(om::ui::kBoltH == 7);
  CHECK(om::ui::kBoltH == om::ui::kBattH);   // it spans the icon's seven rows
}

static void testUnknownAtBoot() {
  ChargeBolt b;
  b.begin(true);
  CHECK(b.controllerReady());
  CHECK(b.state() == VbusState::Unknown);
  CHECK(!b.bolt());
  CHECK(b.unknown());
}

static void testTwoEqualReadsRaiseTheBolt() {
  ChargeBolt b;
  b.begin(true);
  CHECK(feed(b, VbusRead::Present, 1) == 0);   // one read is not a decision
  CHECK(!b.bolt());
  CHECK(b.unknown());
  CHECK(feed(b, VbusRead::Present, 1) == 1);   // the second equal read adopts it
  CHECK(b.bolt());
  CHECK(b.state() == VbusState::Present);
  // A steady cable is not a change on every poll.
  CHECK(feed(b, VbusRead::Present, 10) == 0);
  CHECK(b.bolt());
}

static void testOneBadReadDoesNotFlicker() {
  ChargeBolt b;
  b.begin(true);
  feed(b, VbusRead::Present, 2);
  CHECK(b.bolt());
  // A failed read and then a good one: the candidate run never reaches two, the bolt stays up.
  CHECK(feed(b, VbusRead::Unavailable, 1) == 0);
  CHECK(b.bolt());
  CHECK(feed(b, VbusRead::Present, 1) == 0);
  CHECK(b.bolt());
  // A single "no cable" is a twitch, not a removal.
  CHECK(feed(b, VbusRead::Absent, 1) == 0);
  CHECK(b.bolt());
  // ...but two equal "no cable" reads remove it.
  CHECK(feed(b, VbusRead::Absent, 1) == 1);
  CHECK(!b.bolt());
  CHECK(b.state() == VbusState::Absent);
}

static void testTwoUnavailableReadsBecomeUnknown() {
  ChargeBolt b;
  b.begin(true);
  feed(b, VbusRead::Present, 2);
  CHECK(b.bolt());
  CHECK(feed(b, VbusRead::Unavailable, 1) == 0);
  CHECK(b.bolt());
  CHECK(feed(b, VbusRead::Unavailable, 1) == 1);
  CHECK(b.unknown());
  CHECK(!b.bolt());
  // Unknown can be re-decided by two good reads; that is a later cable, and the flag follows it.
  CHECK(feed(b, VbusRead::Present, 2) == 1);
  CHECK(b.bolt());
}

static void testFlappingNeverRaisesTheBolt() {
  ChargeBolt b;
  b.begin(true);
  for (int i = 0; i < 50; ++i) {
    CHECK(feed(b, (i & 1) ? VbusRead::Absent : VbusRead::Present, 1) == 0);
    CHECK(!b.bolt());
  }
  CHECK(b.unknown());
}

static void testControllerNotReadyLatchesUnknown() {
  ChargeBolt b;
  b.begin(false);
  CHECK(!b.controllerReady());
  CHECK(!b.bolt());
  // The strongest possible evidence cannot override a boot that failed: ticket item 4.
  CHECK(feed(b, VbusRead::Present, 100) == 0);
  CHECK(b.unknown());
  CHECK(!b.bolt());
  CHECK(feed(b, VbusRead::Absent, 100) == 0);
  CHECK(b.unknown());
  CHECK(!b.bolt());
}

static void testBeginResetsToUnknown() {
  ChargeBolt b;
  b.begin(true);
  feed(b, VbusRead::Present, 2);
  CHECK(b.bolt());
  b.begin(false);
  CHECK(b.unknown());
  CHECK(!b.bolt());
  b.begin(true);
  CHECK(b.unknown());
  CHECK(!b.bolt());
  CHECK(feed(b, VbusRead::Present, 2) == 1);
  CHECK(b.bolt());
}

static void testBoltShapeFitsTheIcon() {
  int gold = 0;
  for (int r = 0; r < om::ui::kBoltH; ++r) {
    const int x = om::ui::kBoltRuns[r].x;
    const int w = om::ui::kBoltRuns[r].w;
    CHECK(x >= 0);
    CHECK(w >= 1);
    CHECK(x + w <= om::ui::kBoltW);
    gold += w;
    // Every row is non-empty, so the shape cannot break into a dotted line.
    CHECK(w > 0);
    if (r > 0) {
      // 8-connected from row to row: the next run overlaps this one (or touches it sideways).
      const int px = om::ui::kBoltRuns[r - 1].x;
      const int pw = om::ui::kBoltRuns[r - 1].w;
      CHECK(x <= px + pw && px <= x + w);
    }
  }
  CHECK(gold == 17);
  CHECK(om::ui::kBoltRuns[3].w == om::ui::kBoltW);   // the middle bar is the full bolt width
  // Centred on the 16 px battery icon with a margin on both sides.
  const int bx = (om::ui::kBattW - om::ui::kBoltW) / 2;
  CHECK(bx == 5);
  CHECK(bx > 0);
  CHECK(bx + om::ui::kBoltW < om::ui::kBattW);
}

int main() {
  testConstants();
  testUnknownAtBoot();
  testTwoEqualReadsRaiseTheBolt();
  testOneBadReadDoesNotFlicker();
  testTwoUnavailableReadsBecomeUnknown();
  testFlappingNeverRaisesTheBolt();
  testControllerNotReadyLatchesUnknown();
  testBeginResetsToUnknown();
  testBoltShapeFitsTheIcon();
  if (g_fail == 0) {
    printf("est402: debounce, unknown-means-no-bolt and the 5x7 shape all pass\n");
    return 0;
  }
  printf("est402: %d failure(s)\n", g_fail);
  return 1;
}
