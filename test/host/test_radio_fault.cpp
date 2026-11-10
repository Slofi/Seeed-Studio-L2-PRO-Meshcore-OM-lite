// Host test for the radio-fault mark's state (EST-337): each of the three reasons sets it, a good TX
// clears it, a radio re-init clears it, and silence has no way to set it.
#include <stdio.h>
#include <string.h>

#include "radio_fault.h"

using namespace om;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static void testEachReason() {
  RadioFaultState s;
  CHECK(!s.active());
  CHECK(s.reason() == RadioFault::None);

  CHECK(s.set(RadioFault::Tx));
  CHECK(s.active());
  CHECK(s.reason() == RadioFault::Tx);
  CHECK(s.set(RadioFault::Tx) == false);   // no change, no second log line

  // A more severe reason replaces it; a less severe one cannot hide it.
  CHECK(s.set(RadioFault::Mesh));
  CHECK(s.reason() == RadioFault::Mesh);
  CHECK(s.set(RadioFault::Tx) == false);
  CHECK(s.reason() == RadioFault::Mesh);
  CHECK(s.set(RadioFault::Radio));
  CHECK(s.reason() == RadioFault::Radio);
  CHECK(s.set(RadioFault::Mesh) == false);
  CHECK(s.set(RadioFault::Radio) == false);

  // Each reason on its own, from clean.
  RadioFaultState alone;
  CHECK(alone.set(RadioFault::Radio) && alone.reason() == RadioFault::Radio);
  alone.clear();
  CHECK(alone.set(RadioFault::Mesh) && alone.reason() == RadioFault::Mesh);
  alone.clear();
  CHECK(alone.set(RadioFault::Tx) && alone.reason() == RadioFault::Tx);
  CHECK(alone.set(RadioFault::None) == false);   // "none" is not a reason
}

static void testClearPaths() {
  // A good TX clears any reason: radio, mesh and TX all just proved themselves.
  const RadioFault reasons[] = {RadioFault::Tx, RadioFault::Mesh, RadioFault::Radio};
  for (RadioFault r : reasons) {
    RadioFaultState s;
    s.set(r);
    CHECK(s.onTxOk());
    CHECK(!s.active());
    CHECK(s.reason() == RadioFault::None);
    CHECK(!s.onTxOk());   // already clear: no second "cleared" log
  }

  // A radio (re-)init clears it too.
  RadioFaultState s;
  CHECK(s.set(RadioFault::Radio));
  CHECK(s.onRadioReinit());
  CHECK(!s.active());
  CHECK(!s.onRadioReinit());
}

static void testSilenceNeverSets() {
  // There is no silence/timer input at all (radio_fault.h has only set/clear/onTxOk/onRadioReinit):
  // reading the state for an hour changes nothing. This is the property the ticket asks for: a quiet
  // band or a missing ACK is not a fault.
  RadioFaultState s;
  for (int i = 0; i < 3600; ++i) {
    (void)s.active();
    (void)s.reason();
  }
  CHECK(!s.active());
}

static void testText() {
  CHECK(strcmp(radioFaultToken(RadioFault::Radio), "radio") == 0);
  CHECK(strcmp(radioFaultToken(RadioFault::Mesh), "mesh") == 0);
  CHECK(strcmp(radioFaultToken(RadioFault::Tx), "tx") == 0);
  CHECK(strcmp(radioFaultToken(RadioFault::None), "none") == 0);
  CHECK(strcmp(radioFaultText(RadioFault::Radio), "Radio chip failed to start") == 0);
  CHECK(strcmp(radioFaultText(RadioFault::Mesh), "MeshCore did not start") == 0);
  CHECK(strcmp(radioFaultText(RadioFault::Tx), "Last transmission failed") == 0);
  CHECK(radioFaultRank(RadioFault::Radio) > radioFaultRank(RadioFault::Mesh));
  CHECK(radioFaultRank(RadioFault::Mesh) > radioFaultRank(RadioFault::Tx));
  CHECK(radioFaultRank(RadioFault::Tx) > radioFaultRank(RadioFault::None));
}

int main() {
  testEachReason();
  testClearPaths();
  testSilenceNeverSets();
  testText();
  if (g_fail) {
    printf("%d radio-fault check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("radio fault: all checks passed\n");
  return 0;
}
