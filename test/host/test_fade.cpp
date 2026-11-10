// Host test for the non-blocking backlight ramp (fade.h, EST-338).
//
// The controller is an 8-bit PWM LED driver (0..255). The ramp is time based: tick() returns the
// level for the current millisecond and never blocks; at most one level per call. It must start at
// the from level, end exactly at the target, be monotonic, and reverse from the CURRENT level when a
// new start() (a wake during the fade-out) arrives.
#include <stdio.h>

#include "fade.h"

using namespace om;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static void testEaseCurve() {
  CHECK(FadeRamp::easedPermille(0, 200, true) == 0);
  CHECK(FadeRamp::easedPermille(200, 200, true) == 1000);
  CHECK(FadeRamp::easedPermille(400, 200, true) == 1000);   // clamped past the end
  CHECK(FadeRamp::easedPermille(0, 0, true) == 1000);       // zero duration is immediately done
  int prev = -1;
  for (uint32_t t = 0; t <= 200; ++t) {
    const int p = FadeRamp::easedPermille(t, 200, true);
    CHECK(p >= 0 && p <= 1000);
    CHECK(p >= prev);
    prev = p;
  }
  // Ease-out: it is already well past halfway at the halfway time (fast start, soft landing).
  CHECK(FadeRamp::easedPermille(100, 200, true) > 500);
}

static void testFadeIn() {
  FadeRamp f;
  f.reset(0);
  CHECK(f.level() == 0);
  CHECK(!f.active());

  f.start(1000, 0, 160, 200, true);
  CHECK(f.active());
  CHECK(f.level() == 0);
  CHECK(f.tick(1000) == -1);   // t = 0: still the from level, nothing to write

  int last = 0;
  int changes = 0;
  for (uint32_t t = 1001; t <= 1200; ++t) {
    const int l = f.tick(t);
    if (l < 0) continue;
    CHECK(l >= last);
    CHECK(l >= 0 && l <= 255);
    last = l;
    ++changes;
  }
  CHECK(last == 160);
  CHECK(f.level() == 160);
  CHECK(!f.active());
  CHECK(changes == f.steps());
  CHECK(changes > 30);   // 8-bit PWM gets many real levels, not a 4-step "fade"
  CHECK(f.tick(1300) == -1);
}

static void testFadeOut() {
  FadeRamp f;
  f.reset(160);
  f.start(0, 160, 0, 150, false);   // linear out
  int last = 160;
  int changes = 0;
  for (uint32_t t = 1; t <= 150; ++t) {
    const int l = f.tick(t);
    if (l < 0) continue;
    CHECK(l <= last);
    CHECK(l >= 0 && l <= 255);
    last = l;
    ++changes;
  }
  CHECK(last == 0);
  CHECK(!f.active());
  CHECK(changes > 30);
}

static void testReverseDuringFadeOut() {
  FadeRamp f;
  f.reset(160);
  f.start(0, 160, 0, 150, false);
  const int mid = f.tick(75);   // halfway out
  CHECK(mid >= 70 && mid <= 90);
  const int atReverse = f.level();

  // A wake now: the new ramp starts from the CURRENT level, no jump back up or down first.
  f.start(75, atReverse, 160, 200, true);
  CHECK(f.level() == atReverse);
  CHECK(f.active());

  int last = atReverse;
  int changes = 0;
  for (uint32_t t = 76; t <= 275; ++t) {
    const int l = f.tick(t);
    if (l < 0) continue;
    CHECK(l >= last);
    CHECK(l <= 160);
    last = l;
    ++changes;
  }
  CHECK(last == 160);
  CHECK(changes > 30);
  CHECK(!f.active());
}

static void testEdges() {
  FadeRamp f;
  f.reset(300);                 // clamped to 255
  CHECK(f.level() == FadeRamp::kMaxLevel);

  f.start(0, 10, 20, 0, true);  // zero duration: one call reaches the target
  CHECK(f.active());
  CHECK(f.tick(0) == 20);
  CHECK(!f.active());
  CHECK(f.tick(1) == -1);

  f.start(0, 40, 40, 200, true);   // same target: nothing to do
  CHECK(!f.active());
  CHECK(f.tick(100) == -1);

  f.start(0, 255, 0, 150, false);  // full range down
  CHECK(f.tick(0) == -1);
  CHECK(f.tick(150) == 0);
  CHECK(f.level() == 0);
}

int main() {
  testEaseCurve();
  testFadeIn();
  testFadeOut();
  testReverseDuringFadeOut();
  testEdges();
  if (g_fail) {
    printf("%d fade check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("fade: all checks passed\n");
  return 0;
}
