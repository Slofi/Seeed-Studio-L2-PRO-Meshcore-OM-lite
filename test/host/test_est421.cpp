// SPDX-License-Identifier: MIT
// EST-421, the C++ half: the CardKB's non-blocking start window.
// The bug: `begin()` powered the Grove 5 V rail, waited a fixed 50 ms and probed twice; the CardKB's
// ATmega8A needs longer, so a boot with the switch ON said "CardKB not found" (proven on Filip's unit
// on 2026-11-13). The fix is a window: power the rail, then probe every ~100 ms from the loop task
// until the keyboard answers or 3000 ms pass, alternating the pin order.
// This test drives the state machine (src/cardkb_logic.h, plain C++: no Arduino, no Wire) millisecond
// by millisecond and pins:
//  * an answer at 0, 400 and 2900 ms is found with the right N (the ms since the rail came on);
//  * no answer is a timeout at exactly 3000 ms, never earlier and never with a probe at the edge;
//  * the switch OFF mid-window cancels: no more probes, no timeout;
//  * attempts sit ~100 ms apart, alternate their pin order (SDA 13 first), and a slow loop pass never
//    bunches two probes up;
//  * a fresh start() resets the time, the count and the order.
// Against the old 50 ms single-shot code this file does not compile (no StartWindow, no
// kStartWindowMs): the arm the ticket asks for.
#include <stdio.h>

#include "cardkb_logic.h"

using namespace om::cardkb;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

constexpr uint32_t kNever = 0xFFFFFFFFu;

// One simulated run of the window. `answerAt` is the elapsed ms at which the keyboard answers (the
// driver's successful probe); `cancelAt` is the switch going OFF; `stopAt` is the last millisecond
// simulated. Stepping 1 ms at a time is the loop task's worst case (it really passes every few ms).
struct Run {
  uint32_t foundMs = 0;
  bool found = false;
  bool timedOut = false;
  uint32_t timedOutMs = 0;
  int probes = 0;
  int orderFaults = 0;
  int spacingFaults = 0;
  uint32_t lastProbeMs = 0;
  bool badSpacing = false;
};

static Run simulate(uint32_t answerAt, uint32_t cancelAt = kNever, uint32_t stopAt = 4000) {
  Run r;
  StartWindow w;
  w.start(0);
  for (uint32_t t = 0; t <= stopAt; ++t) {
    if (t == cancelAt) w.cancel();
    const WindowStep s = w.next(t);
    if (s.action == WindowAction::Probe) {
      if (r.probes > 0 && (t - r.lastProbeMs) != kProbeIntervalMs) r.badSpacing = true;
      if (r.probes > 0 && (t - r.lastProbeMs) < kProbeIntervalMs) r.spacingFaults++;
      // Even attempts: SDA 13 / SCL 14 first (the measured order). Odd attempts: the swap.
      const bool want13 = (r.probes % 2) == 0;
      if (s.sda13First != want13) r.orderFaults++;
      r.lastProbeMs = t;
      ++r.probes;
      if (!r.found && t >= answerAt) {
        r.found = true;
        r.foundMs = s.elapsedMs;
        w.cancel();   // the driver closes the window on the answer
      }
    } else if (s.action == WindowAction::TimedOut) {
      r.timedOut = true;
      r.timedOutMs = s.elapsedMs;
    }
  }
  return r;
}

// ------------------------------------------------------------------------------ the ticket's answers
static void testAnswers() {
  // The keyboard answers on the very first probe: found at 0, nothing else happens.
  {
    const Run r = simulate(0);
    CHECK(r.found);
    CHECK(r.foundMs == 0);
    CHECK(r.probes == 1);
    CHECK(!r.timedOut);
    CHECK(r.orderFaults == 0);
  }
  // 400 ms: the fifth attempt (0, 100, 200, 300, 400) is the one that answers, and N is 400.
  {
    const Run r = simulate(400);
    CHECK(r.found);
    CHECK(r.foundMs == 400);
    CHECK(r.probes == 5);
    CHECK(!r.timedOut);
    CHECK(!r.badSpacing);
    CHECK(r.orderFaults == 0);
  }
  // 2900 ms, the last attempt inside the window: found with N = 2900, the 30th probe, no timeout.
  {
    const Run r = simulate(2900);
    CHECK(r.found);
    CHECK(r.foundMs == 2900);
    CHECK(r.probes == 30);
    CHECK(!r.timedOut);
    CHECK(!r.badSpacing);
    CHECK(r.orderFaults == 0);
  }
  // A time between probes: the answer is only seen at the next attempt, and N is that attempt's
  // elapsed time (what the log prints), not the moment the keyboard really came up.
  {
    const Run r = simulate(450);
    CHECK(r.found);
    CHECK(r.foundMs == 500);
    CHECK(r.probes == 6);
  }
}

// ----------------------------------------------------------------------------------- no answer at all
static void testTimeout() {
  const Run r = simulate(kNever);
  CHECK(!r.found);
  CHECK(r.timedOut);
  CHECK(r.timedOutMs == 3000);        // exactly the window, never earlier
  CHECK(r.probes == 30);              // 0 .. 2900: the closing instant is not probed
  CHECK(r.lastProbeMs == 2900);
  CHECK(!r.badSpacing);

  // The millisecond before the end is still a window (the 2900 probe is due), not a timeout.
  StartWindow w;
  w.start(0);
  bool earlyTimeout = false;
  for (uint32_t t = 0; t < kStartWindowMs; ++t) {
    const WindowStep s = w.next(t);
    if (s.action == WindowAction::TimedOut) earlyTimeout = true;
  }
  CHECK(!earlyTimeout);
  CHECK(w.active());

  // The driver's timeout branch drops the rail once and asks for one toast; the state machine says
  // TimedOut once and closes, so a later pass is Idle and cannot toast again.
  const WindowStep end = w.next(kStartWindowMs);
  CHECK(end.action == WindowAction::TimedOut);
  CHECK(!w.active());
  CHECK(w.next(kStartWindowMs + 1).action == WindowAction::Idle);
  CHECK(w.next(10000).action == WindowAction::Idle);
}

// ----------------------------------------------------------------------- switch OFF during the window
static void testCancel() {
  const Run r = simulate(kNever, 150);
  CHECK(!r.found);
  CHECK(!r.timedOut);                 // switching OFF is not a timeout: no toast
  CHECK(r.probes == 2);               // the attempts at 0 and 100, none at or after 150
  CHECK(r.lastProbeMs == 100);

  // Cancel before the first probe: nothing is ever probed.
  StartWindow w;
  w.start(0);
  w.cancel();
  CHECK(!w.active());
  CHECK(w.next(0).action == WindowAction::Idle);
  CHECK(w.next(500).action == WindowAction::Idle);
  CHECK(w.probes() == 0);

  // Cancel after the answer changes nothing (the driver's success path also cancels).
  StartWindow w2;
  w2.start(0);
  CHECK(w2.next(0).action == WindowAction::Probe);
  w2.cancel();
  CHECK(w2.next(100).action == WindowAction::Idle);
}

// ------------------------------------------------------------------- spacing, order, slow loop, restart
static void testSpacingAndOrder() {
  // Probes exactly 100 ms apart, starting at once, and pin order 13/14, 14/13, 13/14 ...
  StartWindow w;
  w.start(0);
  int probes = 0;
  for (uint32_t t = 0; t <= 2900; ++t) {
    const WindowStep s = w.next(t);
    if (s.action != WindowAction::Probe) continue;
    CHECK((t % kProbeIntervalMs) == 0);
    CHECK(s.sda13First == ((probes % 2) == 0));
    CHECK(s.elapsedMs == t);
    ++probes;
  }
  CHECK(probes == 30);

  // A slow pass (the loop task busy for 250 ms) must not fire a catch-up burst: the next attempt is
  // 100 ms after the one that really happened.
  {
    StartWindow s;
    s.start(0);
    const WindowStep a = s.next(0);
    CHECK(a.action == WindowAction::Probe);
    CHECK(s.next(250).action == WindowAction::Probe);   // one attempt, at the pass that arrives late
    CHECK(s.next(300).action == WindowAction::Idle);    // not due again yet
    CHECK(s.next(349).action == WindowAction::Idle);
    CHECK(s.next(350).action == WindowAction::Probe);   // 100 ms after the 250 one
  }

  // A fresh window after OFF/ON starts again at once, with order 13 first and the count reset.
  {
    StartWindow s;
    s.start(0);
    for (uint32_t t = 0; t <= 300; ++t) s.next(t);      // probes at 0, 100, 200, 300
    CHECK(s.probes() == 4);
    s.cancel();
    s.start(5000);
    const WindowStep first = s.next(5000);
    CHECK(first.action == WindowAction::Probe);
    CHECK(first.sda13First);
    CHECK(first.elapsedMs == 0);
    CHECK(s.probes() == 1);
  }

  // The constants are the ticket's, in one place.
  CHECK(kStartWindowMs == 3000);
  CHECK(kProbeIntervalMs == 100);

  // A millisecond just before a due time waits, and an idle window reports Idle with elapsed 0.
  {
    StartWindow s;
    CHECK(s.next(123).action == WindowAction::Idle);
    CHECK(s.next(123).elapsedMs == 0);
    s.start(1000);
    CHECK(s.next(1000).action == WindowAction::Probe);
    CHECK(s.next(1050).action == WindowAction::Idle);
    CHECK(s.next(1050).elapsedMs == 50);
    CHECK(s.next(1100).action == WindowAction::Probe);
    CHECK(s.next(1100).elapsedMs == 100);
  }
}

int main() {
  testAnswers();
  testTimeout();
  testCancel();
  testSpacingAndOrder();
  if (g_fail == 0) {
    printf("est421: CardKB start window: answers at 0/400/2900 -> found N, timeout at 3000, "
           "cancel, 100 ms alternating probes\n");
    return 0;
  }
  printf("est421: %d failure(s)\n", g_fail);
  return 1;
}
