// Host test for the L76K mode command (EST-339): ONE `$PCAS04,7`, sent exactly once and never before
// the module talks (the first checksum-valid sentence). EST-338's TX probe, the mode 5 fallback, the
// mode 3 restore and the "GLONASS seen" detector are gone, so none of them can send anything.
#include <stdio.h>

#include "gps_mode.h"

using namespace om;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static const char* kGga = "$GNGGA,091200.00,4603.00,N,01430.00,E,1,9,0.9,300.0,M,0.0,M,,*00";
static const char* kGpGsv = "$GPGSV,3,3,12,32,10,300,40*4A";
// The line EST-338's detector was fooled by: a GLONASS GSA slot with the system id and no satellite.
static const char* kGnGsaGlSlot = "$GNGSA,A,1,,,,,,,,,,,,,,1*2A";

// Nothing may leave before the module is up: a null line, an empty line and binary garbage send
// nothing; the command goes out once when the module finally talks.
static void testNothingBeforeTheModuleTalks() {
  GpsModeCommand m;
  CHECK(!m.sent());
  CHECK(!m.onValidSentence(nullptr));
  CHECK(!m.onValidSentence(""));
  CHECK(!m.onValidSentence("binary\x01garbage"));
  CHECK(!m.onValidSentence("GNGGA,091200.00,4603.00,N"));   // no '$', not a sentence at all
  CHECK(!m.sent());

  CHECK(m.onValidSentence(kGga));
  CHECK(m.sent());
}

// The whole point: exactly one command per module start, not one per sentence (the EST-338 machine
// sent several over 30 s, and its "done" line flooded the log). The sentence kind does not matter.
static void testSentExactlyOnce() {
  GpsModeCommand m;
  CHECK(m.onValidSentence(kGga));
  for (int i = 0; i < 600; ++i) {
    CHECK(!m.onValidSentence(kGga));
    CHECK(!m.onValidSentence(kGpGsv));
    CHECK(!m.onValidSentence(kGnGsaGlSlot));
  }
  CHECK(m.sent());
}

// A module restart (gpsBegin re-opened the UART) arms it again: one more command, after the module
// talks again, and still not before.
static void testRearmAfterModuleReboot() {
  GpsModeCommand m;
  CHECK(m.onValidSentence(kGga));
  m.onModuleReboot();
  CHECK(!m.sent());
  CHECK(!m.onValidSentence(""));   // nothing before the module talks again
  CHECK(m.onValidSentence(kGga));
  CHECK(!m.onValidSentence(kGga));

  // Two re-arms before any sentence are the same as one: one command.
  m.onModuleReboot();
  m.onModuleReboot();
  CHECK(m.onValidSentence(kGpGsv));
  CHECK(!m.onValidSentence(kGpGsv));
}

int main() {
  testNothingBeforeTheModuleTalks();
  testSentExactlyOnce();
  testRearmAfterModuleReboot();
  if (g_fail) {
    printf("%d gps mode check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("gps mode: all checks passed\n");
  return 0;
}
