// SPDX-License-Identifier: MIT
// EST-421, the C++ half: the CardKB's non-blocking start window. EST-423 tightened the same window.
// The bug: `begin()` powered the Grove 5 V rail, waited a fixed 50 ms and probed twice; the CardKB's
// ATmega8A needs longer, so a boot with the switch ON said "CardKB not found" (proven on Filip's unit
// on 2026-11-13).
// EST-421 fixed it with a 3000 ms window that alternated the pin order. That still failed on the
// device (09:40), so EST-423 makes the window 10000 ms and uses the measured order (SDA 13 / SCL 14)
// for EVERY probe inside it; the swap (SDA 14 / SCL 13) is exactly ONE attempt after the window, so
// an answer inside the window (cause a: the keyboard needs time) stays apart from an answer only on
// the swap (cause b: the alternation disturbed its TWI).
// This test drives the state machine (src/cardkb_logic.h, plain C++: no Arduino, no Wire) millisecond
// by millisecond and pins:
//  * an answer at 0, 400, 9900 and 9990 ms is found with the right N (the ms since the rail on);
//  * no answer inside the window gives ONE swapped fallback, and only its failure is the timeout;
//  * the switch OFF mid-window cancels: no more probes, no fallback, no timeout;
//  * attempts sit ~100 ms apart, all in the measured order, and a slow loop pass never bunches two
//    probes up;
//  * a fresh start() resets the time, the count, the order and the fallback.
// Against the pre-fix code (the 3000 ms alternating window) this file does not compile (no Fallback):
// the arm the ticket asks for.
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
// driver's successful probe, whatever the action); `cancelAt` is the switch going OFF; `stopAt` is
// the last millisecond simulated. Stepping 1 ms at a time is the loop task's worst case (it really
// passes every few ms).
struct Run {
  uint32_t foundMs = 0;
  bool found = false;
  bool foundOnFallback = false;
  bool timedOut = false;
  uint32_t timedOutMs = 0;
  int probes = 0;
  int fallbacks = 0;
  int orderFaults = 0;
  int spacingFaults = 0;
  uint32_t lastProbeMs = 0;
  bool badSpacing = false;
};

static Run simulate(uint32_t answerAt, uint32_t cancelAt = kNever, uint32_t stopAt = 12000) {
  Run r;
  StartWindow w;
  w.start(0);
  for (uint32_t t = 0; t <= stopAt; ++t) {
    if (t == cancelAt) w.cancel();
    const WindowStep s = w.next(t);
    if (s.action == WindowAction::Probe || s.action == WindowAction::Fallback) {
      const bool fallback = s.action == WindowAction::Fallback;
      if (!fallback) {
        if (r.probes > 0 && (t - r.lastProbeMs) != kProbeIntervalMs) r.badSpacing = true;
        if (r.probes > 0 && (t - r.lastProbeMs) < kProbeIntervalMs) r.spacingFaults++;
        r.lastProbeMs = t;
        ++r.probes;
        if (!s.sda13First) r.orderFaults++;   // EST-423: never swapped inside the window
      } else {
        ++r.fallbacks;
        if (s.sda13First) r.orderFaults++;    // the one fallback is 14/13, always
        if (r.fallbacks > 1) r.orderFaults++; // and there is only ever one of it
      }
      if (!r.found && t >= answerAt) {
        r.found = true;
        r.foundMs = s.elapsedMs;
        r.foundOnFallback = fallback;
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
    CHECK(r.fallbacks == 0);
    CHECK(!r.foundOnFallback);
    CHECK(!r.timedOut);
    CHECK(r.orderFaults == 0);
  }
  // 400 ms: the fifth attempt (0, 100, 200, 300, 400) is the one that answers, and N is 400.
  {
    const Run r = simulate(400);
    CHECK(r.found);
    CHECK(r.foundMs == 400);
    CHECK(r.probes == 5);
    CHECK(r.fallbacks == 0);
    CHECK(!r.timedOut);
    CHECK(!r.badSpacing);
    CHECK(r.orderFaults == 0);
  }
  // 9900 ms, the last attempt inside the window: found with N = 9900, the 100th probe, no fallback.
  {
    const Run r = simulate(9900);
    CHECK(r.found);
    CHECK(r.foundMs == 9900);
    CHECK(r.probes == 100);
    CHECK(r.fallbacks == 0);
    CHECK(!r.foundOnFallback);
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
  // 9990 ms is past the last probe (9900) but still inside the window: nobody asks before 10000, so
  // the answer is only seen by the ONE swapped fallback. That is cause (b) in the log: found only at
  // 14/13, after the window.
  {
    const Run r = simulate(9990);
    CHECK(r.found);
    CHECK(r.foundMs == 10000);
    CHECK(r.probes == 100);
    CHECK(r.fallbacks == 1);
    CHECK(r.foundOnFallback);
    CHECK(!r.timedOut);
    CHECK(r.orderFaults == 0);
  }
  // The same if the keyboard only answers to the swapped order: the fallback finds it, at 14/13.
  {
    const Run r = simulate(kStartWindowMs);
    CHECK(r.found);
    CHECK(r.foundMs == kStartWindowMs);
    CHECK(r.probes == 100);
    CHECK(r.fallbacks == 1);
    CHECK(r.foundOnFallback);
    CHECK(!r.timedOut);
  }
}

// ----------------------------------------------------------------------------------- no answer at all
static void testTimeout() {
  const Run r = simulate(kNever, kNever, 20000);
  CHECK(!r.found);
  CHECK(r.timedOut);
  CHECK(r.timedOutMs == kStartWindowMs + 1);   // the pass after the fallback, never earlier
  CHECK(r.probes == 100);                      // 0 .. 9900: the closing instant is not probed
  CHECK(r.lastProbeMs == kStartWindowMs - kProbeIntervalMs);
  CHECK(r.fallbacks == 1);                     // exactly one swapped attempt
  CHECK(r.orderFaults == 0);                   // 100 in the measured order, the one in the swap
  CHECK(!r.badSpacing);

  // Nothing times out and no fallback happens before the window's edge, and the edge itself is the
  // fallback, not the timeout.
  StartWindow w;
  w.start(0);
  bool earlyTimeout = false;
  bool earlyFallback = false;
  for (uint32_t t = 0; t < kStartWindowMs; ++t) {
    const WindowStep s = w.next(t);
    if (s.action == WindowAction::TimedOut) earlyTimeout = true;
    if (s.action == WindowAction::Fallback) earlyFallback = true;
  }
  CHECK(!earlyTimeout);
  CHECK(!earlyFallback);
  CHECK(w.active());
  CHECK(w.probes() == 100);
  CHECK(!w.fallbackUsed());

  const WindowStep edge = w.next(kStartWindowMs);
  CHECK(edge.action == WindowAction::Fallback);
  CHECK(!edge.sda13First);
  CHECK(edge.elapsedMs >= kStartWindowMs);
  CHECK(w.active());                            // the window is not closed until the fallback fails

  // The driver's timeout branch drops the rail once and asks for one toast; the state machine says
  // TimedOut once and closes, so a later pass is Idle and cannot toast again. No second fallback,
  // however long the loop keeps running.
  const WindowStep end = w.next(kStartWindowMs + 1);
  CHECK(end.action == WindowAction::TimedOut);
  CHECK(!w.active());
  CHECK(w.next(kStartWindowMs + 2).action == WindowAction::Idle);
  CHECK(w.next(60000).action == WindowAction::Idle);
}

// ----------------------------------------------------------------------- switch OFF during the window
static void testCancel() {
  const Run r = simulate(kNever, 150);
  CHECK(!r.found);
  CHECK(!r.timedOut);                 // switching OFF is not a timeout: no toast
  CHECK(r.probes == 2);               // the attempts at 0 and 100, none at or after 150
  CHECK(r.fallbacks == 0);
  CHECK(r.lastProbeMs == 100);

  // OFF exactly at the window's edge: the window is cancelled before next() is asked, so the one
  // fallback is never tried and no toast follows.
  {
    const Run off = simulate(kNever, kStartWindowMs);
    CHECK(off.probes == 100);
    CHECK(off.fallbacks == 0);
    CHECK(!off.timedOut);
  }

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
  // Probes exactly 100 ms apart, starting at once, all in the measured order 13/14, 100 of them.
  StartWindow w;
  w.start(0);
  int probes = 0;
  for (uint32_t t = 0; t < kStartWindowMs; ++t) {
    const WindowStep s = w.next(t);
    if (s.action != WindowAction::Probe) continue;
    CHECK((t % kProbeIntervalMs) == 0);
    CHECK(s.sda13First);
    CHECK(s.elapsedMs == t);
    ++probes;
  }
  CHECK(probes == 100);
  CHECK(w.probes() == 100);
  // The one fallback comes after the window, swapped, and only once.
  const WindowStep fb = w.next(kStartWindowMs);
  CHECK(fb.action == WindowAction::Fallback);
  CHECK(!fb.sda13First);
  CHECK(w.next(kStartWindowMs + 5).action == WindowAction::TimedOut);

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

  // A fresh window after OFF/ON starts again at once, with order 13 first and every counter reset,
  // including the fallback (a second window gets its own one).
  {
    StartWindow s;
    s.start(0);
    for (uint32_t t = 0; t < kStartWindowMs; ++t) s.next(t);   // probes at 0 .. 9900
    CHECK(s.probes() == 100);
    CHECK(s.next(kStartWindowMs).action == WindowAction::Fallback);
    CHECK(s.fallbackUsed());
    s.cancel();
    s.start(100);
    CHECK(!s.fallbackUsed());
    const WindowStep first = s.next(100);
    CHECK(first.action == WindowAction::Probe);
    CHECK(first.sda13First);
    CHECK(first.elapsedMs == 0);
    CHECK(s.probes() == 1);
    CHECK(s.next(100 + kStartWindowMs).action == WindowAction::Fallback);
    CHECK(s.next(100 + kStartWindowMs + 1).action == WindowAction::TimedOut);
  }

  // The constants are the ticket's, in one place.
  CHECK(kStartWindowMs == 10000);
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
    printf("est421: CardKB start window: answers at 0/400/9900/9990 -> found N, 100 probes in one "
           "order, one swapped fallback, timeout after it, cancel\n");
    return 0;
  }
  printf("est421: %d failure(s)\n", g_fail);
  return 1;
}
