// Host test for src/touch_gate.cpp: the rule that a wake never turns the finger already on the glass
// (or a press that lands while the panel is still coming up) into a tap.
#include <stdio.h>

#include "touch_gate.h"

using namespace om;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

int main() {
  // --- no wake yet: the panel state passes straight through -------------------
  {
    TouchGate g;
    CHECK(!g.gateActive());
    CHECK(g.accept(0, false) == false);
    CHECK(g.accept(10, true) == true);
    CHECK(g.accept(20, true) == true);
    CHECK(g.accept(30, false) == false);
  }

  // --- a finger held through the wake is ignored until it lifts ---------------
  {
    TouchGate g;
    g.onWake(1000, true);
    CHECK(g.gateActive());
    CHECK(g.accept(1000, true) == false);   // the finger that was there at the wake
    CHECK(g.accept(1100, true) == false);   // still held, well inside the window
    CHECK(g.accept(1500, true) == false);   // still held, window long gone: no tap either
    CHECK(g.accept(1600, false) == false);  // it lifts
    CHECK(g.accept(1700, true) == true);    // a new press after the lift counts
    CHECK(!g.gateActive());                 // and the gate is done
    CHECK(g.accept(1710, true) == true);    // ordinary touches afterwards
  }

  // --- a new press after the 300 ms window is accepted ------------------------
  {
    TouchGate g;
    g.onWake(0, false);
    CHECK(g.accept(299, true) == false);    // one ms early: ignored AND latched
    CHECK(g.accept(300, true) == false);    // the same (latched) finger, now past the window
    CHECK(g.accept(301, false) == false);   // it lifts
    CHECK(g.accept(302, true) == true);     // a genuinely new press counts
  }
  {
    TouchGate g;
    g.onWake(5000, false);
    CHECK(g.accept(5000, false) == false);
    CHECK(g.accept(5300, true) == true);    // exactly kGateMs later
  }

  // --- a press that BEGINS inside the window never becomes a tap -------------
  {
    TouchGate g;
    g.onWake(0, false);
    CHECK(g.accept(50, true) == false);     // begun at 50 ms
    CHECK(g.accept(250, true) == false);
    CHECK(g.accept(450, true) == false);    // held past 300 ms: still not a tap
    CHECK(g.accept(460, false) == false);
    CHECK(g.accept(470, true) == true);     // only a fresh press after that
  }

  // --- a held finger that was NOT down at the wake, but arrives later ---------
  // (the panel takes a moment to report it, e.g. during the redraw)
  {
    TouchGate g;
    g.onWake(0, false);
    CHECK(g.accept(0, false) == false);
    CHECK(g.accept(120, true) == false);
    CHECK(g.accept(130, false) == false);
    CHECK(g.accept(400, true) == true);
  }

  // --- reset() goes back to pass-through -------------------------------------
  {
    TouchGate g;
    g.onWake(0, true);
    CHECK(g.accept(10, true) == false);
    g.reset();
    CHECK(!g.gateActive());
    CHECK(g.accept(11, true) == true);
  }

  // --- a second wake re-arms the gate ----------------------------------------
  {
    TouchGate g;
    g.onWake(0, false);
    CHECK(g.accept(400, true) == true);
    g.onWake(1000, false);
    CHECK(g.accept(1100, true) == false);   // inside the second window
    CHECK(g.accept(1150, false) == false);  // it lifts
    CHECK(g.accept(1400, true) == true);
  }

  if (g_fail) {
    printf("%d touch gate check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("touch gate: all checks passed\n");
  return 0;
}
