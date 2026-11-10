// Host test for the L76K mode-7 state machine (EST-337): the command waits for the module to talk,
// watches 15 s for GLONASS, retries up to three times, gives up, and a reboot resets it.
#include <stdio.h>
#include <string.h>

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
static const char* kGlGsv = "$GLGSV,1,1,02,65,10,100,45,66,20,200,38*6A";
static const char* kGnGsvGl = "$GNGSV,3,1,11,02,10,123,45,65,20,084,38,66,30,201,33*7A";
static const char* kGnGsaGps = "$GNGSA,A,3,02,05,12,25,,,,,,1.2,1.0,1.0,1*2A";
static const char* kGnGsaGl = "$GNGSA,A,3,65,66,67,,,,,,,1.2,1.0,1.0,2*2A";

static void testLineDetection() {
  CHECK(!gpsLineHasGlonass(nullptr));
  CHECK(!gpsLineHasGlonass(""));
  CHECK(!gpsLineHasGlonass("$GP"));
  CHECK(gpsLineHasGlonass(kGlGsv));
  CHECK(gpsLineHasGlonass(kGnGsvGl));
  CHECK(gpsLineHasGlonass(kGnGsaGl));
  CHECK(!gpsLineHasGlonass(kGga));      // GGA says nothing about constellations
  CHECK(!gpsLineHasGlonass(kGpGsv));    // GPS alone is the factory mode, not the fix
  CHECK(!gpsLineHasGlonass(kGnGsaGps));
  CHECK(!gpsLineHasGlonass("$BDGSV,1,1,03,01,10,100,40*00"));
  CHECK(!gpsLineHasGlonass("$GNGSV,1,1,04,02,10,123,45,03,20,084,38,12,30,201,33*00"));
  // A combined GSA with no system id falls back to the PRN range.
  CHECK(gpsLineHasGlonass("$GNGSA,A,3,65,66,67,,,,,,,1.2,1.0,1.0*00"));
}

static void testSendOnlyWhenModuleTalks() {
  GpsModeSetter m;
  CHECK(m.waitingForModule());
  CHECK(m.tick(0) == GpsModeAction::None);          // silence: nothing is ever guessed
  CHECK(m.onValidSentence(kGga, 1000) == GpsModeAction::Send);
  CHECK(m.tries() == 1);
  // A second sentence inside the window does not resend.
  CHECK(m.onValidSentence(kGga, 1500) == GpsModeAction::None);
}

static void testSeen() {
  GpsModeSetter m;
  CHECK(m.onValidSentence(kGga, 1000) == GpsModeAction::Send);
  CHECK(m.onValidSentence(kGlGsv, 2000) == GpsModeAction::Seen);
  CHECK(m.seen());
  CHECK(m.onValidSentence(kGlGsv, 3000) == GpsModeAction::None);   // once only
  CHECK(m.tick(60000) == GpsModeAction::None);

  // GLONASS already on when we start: no command is needed, and none is sent.
  GpsModeSetter already;
  CHECK(already.onValidSentence(kGlGsv, 1000) == GpsModeAction::Seen);
  CHECK(already.tries() == 0);
}

static void testRetriesAndGiveUp() {
  GpsModeSetter m;
  CHECK(m.onValidSentence(kGga, 1000) == GpsModeAction::Send);     // try 1 at 1000
  CHECK(m.tick(15999) == GpsModeAction::None);
  CHECK(m.tick(16000) == GpsModeAction::Send);                     // exactly 15 s: try 2
  CHECK(m.tries() == 2);
  CHECK(m.tick(30999) == GpsModeAction::None);
  CHECK(m.tick(31000) == GpsModeAction::Send);                     // try 3
  CHECK(m.tries() == 3);
  CHECK(m.tick(45999) == GpsModeAction::None);
  CHECK(m.tick(46000) == GpsModeAction::GaveUp);
  CHECK(m.gaveUp());
  CHECK(m.tick(60000) == GpsModeAction::None);                     // no fourth try

  // A late GLONASS talker is still the truth: the mode was taken after all.
  CHECK(m.onValidSentence(kGlGsv, 61000) == GpsModeAction::Seen);
  CHECK(m.seen());
}

static void testRebootResets() {
  GpsModeSetter m;
  CHECK(m.onValidSentence(kGga, 1000) == GpsModeAction::Send);
  CHECK(m.onValidSentence(kGga, 2000) == GpsModeAction::None);
  m.onModuleReboot();
  CHECK(m.waitingForModule());
  CHECK(m.tries() == 0);
  CHECK(m.onValidSentence(kGga, 3000) == GpsModeAction::Send);
  CHECK(m.tries() == 1);

  // A long silence while it had been talking is a reboot too: the next sentence starts a fresh try.
  GpsModeSetter gap;
  CHECK(gap.onValidSentence(kGga, 1000) == GpsModeAction::Send);
  CHECK(gap.onValidSentence(kGlGsv, 1500) == GpsModeAction::Seen);
  CHECK(gap.onValidSentence(kGga, 2000) == GpsModeAction::None);
  CHECK(gap.onValidSentence(kGga, 8000) == GpsModeAction::Send);   // 6 s gap > kRebootGapMs
  CHECK(gap.tries() == 1);
}

int main() {
  testLineDetection();
  testSendOnlyWhenModuleTalks();
  testSeen();
  testRetriesAndGiveUp();
  testRebootResets();
  if (g_fail) {
    printf("%d gps-mode check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("gps mode: all checks passed\n");
  return 0;
}
