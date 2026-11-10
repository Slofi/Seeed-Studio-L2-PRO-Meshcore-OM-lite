// Host test for the L76K one-boot diagnostic (EST-338):
//   * nothing is sent before the module talks;
//   * the $PCAS03 GSV off/on probe proves the TX path (ok) or reports no effect;
//   * mode 7 is tried, then mode 5, then mode 3 is restored unless mode 7 worked;
//   * a reboot starts the sequence over.
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

static GpsDiagStep makeStep(GpsDiagAction a, GpsDiagEvent e) {
  GpsDiagStep s;
  s.action = a;
  s.event = e;
  return s;
}

static void expectStep(const GpsDiagStep& got, GpsDiagAction a, GpsDiagEvent e, int line) {
  const GpsDiagStep want = makeStep(a, e);
  if (!(got == want)) {
    printf("FAIL line %d: action %d event %d, want action %d event %d\n", line, (int)got.action,
           (int)got.event, (int)a, (int)e);
    ++g_fail;
  }
}

#define EXPECT_STEP(got, a, e) expectStep((got), (a), (e), __LINE__)

static const char* kGga = "$GNGGA,091200.00,4603.00,N,01430.00,E,1,9,0.9,300.0,M,0.0,M,,*00";
static const char* kGpGsv = "$GPGSV,3,3,12,32,10,300,40*4A";
static const char* kGlGsv = "$GLGSV,1,1,02,65,10,100,45,66,20,200,38*6A";
static const char* kGnGsvGl = "$GNGSV,3,1,11,02,10,123,45,65,20,084,38,66,30,201,33*7A";
static const char* kGnGsaGps = "$GNGSA,A,3,02,05,12,25,,,,,,1.2,1.0,1.0,1*2A";
static const char* kGnGsaGl = "$GNGSA,A,3,65,66,67,,,,,,,1.2,1.0,1.0,2*2A";

static void testLineDetection() {
  CHECK(!gpsLineHasGsv(nullptr));
  CHECK(!gpsLineHasGsv(""));
  CHECK(gpsLineHasGsv(kGpGsv));
  CHECK(gpsLineHasGsv(kGlGsv));
  CHECK(gpsLineHasGsv(kGnGsvGl));
  CHECK(!gpsLineHasGsv(kGga));
  CHECK(!gpsLineHasGsv(kGnGsaGps));

  CHECK(!gpsLineHasGlonass(nullptr));
  CHECK(!gpsLineHasGlonass(""));
  CHECK(!gpsLineHasGlonass("$GP"));
  CHECK(gpsLineHasGlonass(kGlGsv));
  CHECK(gpsLineHasGlonass(kGnGsvGl));
  CHECK(gpsLineHasGlonass(kGnGsaGl));
  CHECK(!gpsLineHasGlonass(kGga));
  CHECK(!gpsLineHasGlonass(kGpGsv));
  CHECK(!gpsLineHasGlonass(kGnGsaGps));
  CHECK(!gpsLineHasGlonass("$BDGSV,1,1,03,01,10,100,40*00"));
  CHECK(!gpsLineHasGlonass("$GNGSV,1,1,04,02,10,123,45,03,20,084,38,12,30,201,33*00"));
  CHECK(gpsLineHasGlonass("$GNGSA,A,3,65,66,67,,,,,,,1.2,1.0,1.0*00"));
}

static void testNoProbeBeforeModuleTalks() {
  GpsModeSetter m;
  CHECK(m.waitingForModule());
  EXPECT_STEP(m.tick(0), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.tick(60000), GpsDiagAction::None, GpsDiagEvent::None);
  CHECK(m.txPath() == GpsTxPath::Unknown);
}

static void testTxPathOkThenMode7() {
  GpsModeSetter m;
  EXPECT_STEP(m.onValidSentence(kGga, 1000), GpsDiagAction::TxProbeOff, GpsDiagEvent::None);
  // A real module talks every second; keep the reboot-gap detector quiet while the windows run.
  EXPECT_STEP(m.onValidSentence(kGga, 3000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.onValidSentence(kGga, 5000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.tick(5999), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.tick(6000), GpsDiagAction::TxProbeOn, GpsDiagEvent::None);
  // No GSV arrived during the OFF window, and the first one after ON proves the TX path.
  EXPECT_STEP(m.onValidSentence(kGpGsv, 6100), GpsDiagAction::Mode7, GpsDiagEvent::TxPathOk);
  CHECK(m.txPath() == GpsTxPath::Ok);
  CHECK(m.mode7() == GpsModeResult::NotTried);
  CHECK(!m.done());

  EXPECT_STEP(m.onValidSentence(kGlGsv, 7000), GpsDiagAction::None, GpsDiagEvent::Mode7Taken);
  CHECK(m.mode7() == GpsModeResult::Taken);
  CHECK(m.done());
  EXPECT_STEP(m.tick(100000), GpsDiagAction::None, GpsDiagEvent::None);
}

static void testTxPathNoEffectThenMode5() {
  GpsModeSetter m;
  EXPECT_STEP(m.onValidSentence(kGga, 1000), GpsDiagAction::TxProbeOff, GpsDiagEvent::None);
  // A GSV line in the tail of the burst the command landed in is ignored (settle window), then GSV
  // keeps arriving during the OFF window: the command had no effect.
  EXPECT_STEP(m.onValidSentence(kGpGsv, 1300), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.onValidSentence(kGpGsv, 2000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.onValidSentence(kGpGsv, 4000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.tick(6000), GpsDiagAction::TxProbeOn, GpsDiagEvent::None);
  EXPECT_STEP(m.onValidSentence(kGpGsv, 7000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.tick(11000), GpsDiagAction::Mode7, GpsDiagEvent::TxPathNoEffect);
  CHECK(m.txPath() == GpsTxPath::NoEffect);
  CHECK(m.gsvDuringOff() == 2);

  // Keep the module "talking" while mode 7 gets its 15 s (a real one sends a sentence every second).
  EXPECT_STEP(m.onValidSentence(kGga, 11000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.onValidSentence(kGga, 14000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.onValidSentence(kGga, 17000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.onValidSentence(kGga, 20000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.onValidSentence(kGga, 23000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.onValidSentence(kGga, 25000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.tick(25999), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.tick(26000), GpsDiagAction::Mode5, GpsDiagEvent::Mode7NotTaken);
  EXPECT_STEP(m.onValidSentence(kGnGsaGl, 27000), GpsDiagAction::Mode3, GpsDiagEvent::Mode5Taken);
  CHECK(m.mode7() == GpsModeResult::NotTaken);
  CHECK(m.mode5() == GpsModeResult::Taken);
  CHECK(m.done());
}

static void testBothModesRefused() {
  GpsModeSetter m;
  EXPECT_STEP(m.onValidSentence(kGga, 1000), GpsDiagAction::TxProbeOff, GpsDiagEvent::None);
  EXPECT_STEP(m.onValidSentence(kGga, 3000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.onValidSentence(kGga, 5000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.tick(6000), GpsDiagAction::TxProbeOn, GpsDiagEvent::None);
  EXPECT_STEP(m.onValidSentence(kGpGsv, 6100), GpsDiagAction::Mode7, GpsDiagEvent::TxPathOk);
  EXPECT_STEP(m.tick(21100), GpsDiagAction::Mode5, GpsDiagEvent::Mode7NotTaken);
  EXPECT_STEP(m.tick(36100), GpsDiagAction::Mode3, GpsDiagEvent::Mode5NotTaken);
  CHECK(m.mode7() == GpsModeResult::NotTaken);
  CHECK(m.mode5() == GpsModeResult::NotTaken);
  CHECK(m.done());
  EXPECT_STEP(m.tick(90000), GpsDiagAction::None, GpsDiagEvent::None);
}

static void testRebootResets() {
  GpsModeSetter m;
  EXPECT_STEP(m.onValidSentence(kGga, 1000), GpsDiagAction::TxProbeOff, GpsDiagEvent::None);
  m.onModuleReboot();
  CHECK(m.waitingForModule());
  CHECK(!m.done());
  CHECK(m.txPath() == GpsTxPath::Unknown);
  CHECK(m.mode7() == GpsModeResult::NotTried);
  EXPECT_STEP(m.tick(5000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(m.onValidSentence(kGga, 6000), GpsDiagAction::TxProbeOff, GpsDiagEvent::None);

  // A long silence while the module had been talking is a reboot too.
  GpsModeSetter gap;
  EXPECT_STEP(gap.onValidSentence(kGga, 1000), GpsDiagAction::TxProbeOff, GpsDiagEvent::None);
  EXPECT_STEP(gap.onValidSentence(kGga, 2000), GpsDiagAction::None, GpsDiagEvent::None);
  EXPECT_STEP(gap.onValidSentence(kGga, 8000), GpsDiagAction::TxProbeOff, GpsDiagEvent::None);
}

int main() {
  testLineDetection();
  testNoProbeBeforeModuleTalks();
  testTxPathOkThenMode7();
  testTxPathNoEffectThenMode5();
  testBothModesRefused();
  testRebootResets();
  if (g_fail) {
    printf("%d gps diagnostic check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("gps diagnostic: all checks passed\n");
  return 0;
}
