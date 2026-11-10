// Host test for the non-blocking backlight ramp (fade.h, EST-338/EST-339).
//
// The controller is an 8-bit PWM LED driver (0..255). The ramp is time based: tick() returns the
// level for the current millisecond and never blocks; at most one level per call. EST-339: the curve
// is perceptual (gamma 2.2) in BOTH directions, because the eye sees the duty's 2.2 root: a linear
// PWM ramp looks like a jump at the start of a fade-in and smooth on a fade-out. The fade-in is
// 300 ms now. The curve must be monotonic, exact at both ends, and still under 5 % of the target
// after the first 20 % of the time; the fade-out is the same curve played backwards.
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

// The curve itself: starts and ends exactly, monotonic, and slow at the start.
static void testCurve() {
  // Both ends are exact, and a zero duration is immediately done.
  CHECK(FadeRamp::levelAt(0, 160, 0, kFadeInMs) == 0);
  CHECK(FadeRamp::levelAt(0, 160, kFadeInMs, kFadeInMs) == 160);
  CHECK(FadeRamp::levelAt(0, 160, kFadeInMs * 4, kFadeInMs) == 160);   // clamped past the end
  CHECK(FadeRamp::levelAt(0, 160, 0, 0) == 160);

  // Monotonic over the whole fade-in, and never outside 0..target.
  int prev = -1;
  for (uint32_t t = 0; t <= kFadeInMs; ++t) {
    const int l = FadeRamp::levelAt(0, 160, t, kFadeInMs);
    CHECK(l >= 0 && l <= 160);
    CHECK(l >= prev);
    prev = l;
  }

  // The point of the curve: after the first fifth of the time the light is still under 5 % of the
  // target (a linear ramp would already be at 32 of 160 there).
  CHECK(kFadeInMs / 5 == 60);
  CHECK(FadeRamp::levelAt(0, 160, kFadeInMs / 5, kFadeInMs) < 160 * 5 / 100);
  CHECK(FadeRamp::levelAt(0, 160, kFadeInMs / 5, kFadeInMs) < 8);
  // ... and it is a real gamma curve, not a late step: a quarter of the way in it is still well
  // below the linear value (40), and at three quarters it is already most of the way up.
  CHECK(FadeRamp::levelAt(0, 160, kFadeInMs / 4, kFadeInMs) < 40);
  CHECK(FadeRamp::levelAt(0, 160, 3 * kFadeInMs / 4, kFadeInMs) > 80);

  // The fade-out is the fade-in mirrored in time: at every point the two directions agree.
  for (uint32_t t = 0; t <= kFadeOutMs; ++t) {
    CHECK(FadeRamp::levelAt(160, 0, t, kFadeOutMs) ==
          FadeRamp::levelAt(0, 160, kFadeOutMs - t, kFadeOutMs));
  }
  CHECK(FadeRamp::levelAt(160, 0, 0, kFadeOutMs) == 160);
  CHECK(FadeRamp::levelAt(160, 0, kFadeOutMs, kFadeOutMs) == 0);
  prev = 161;
  for (uint32_t t = 0; t <= kFadeOutMs; ++t) {
    const int l = FadeRamp::levelAt(160, 0, t, kFadeOutMs);
    CHECK(l >= 0 && l <= 160);
    CHECK(l <= prev);
    prev = l;
  }
}

static void testFadeIn() {
  FadeRamp f;
  f.reset(0);
  CHECK(f.level() == 0);
  CHECK(!f.active());

  f.start(1000, 0, 160, kFadeInMs);
  CHECK(f.active());
  CHECK(f.level() == 0);
  CHECK(f.tick(1000) == -1);   // t = 0: still the from level, nothing to write

  int last = 0;
  int changes = 0;
  const uint32_t fifth = 1000 + kFadeInMs / 5;
  for (uint32_t t = 1001; t <= 1000 + kFadeInMs; ++t) {
    const int l = f.tick(t);
    if (l < 0) continue;
    CHECK(l >= last);
    CHECK(l >= 0 && l <= 255);
    if (t <= fifth) CHECK(l < 8);   // the first 20 % of the time: under 5 % of 160
    last = l;
    ++changes;
  }
  CHECK(last == 160);
  CHECK(f.level() == 160);
  CHECK(!f.active());
  CHECK(changes == f.steps());
  CHECK(changes > 30);   // 8-bit PWM gets many real levels, not a 4-step "fade"
  CHECK(f.tick(2000) == -1);
}

static void testFadeOut() {
  FadeRamp f;
  f.reset(160);
  f.start(0, 160, 0, kFadeOutMs);
  int last = 160;
  int changes = 0;
  for (uint32_t t = 1; t <= kFadeOutMs; ++t) {
    const int l = f.tick(t);
    if (l < 0) continue;
    CHECK(l <= last);
    CHECK(l >= 0 && l <= 255);
    last = l;
    ++changes;
  }
  CHECK(last == 0);
  CHECK(f.level() == 0);
  CHECK(!f.active());
  CHECK(changes > 30);
}

static void testReverseDuringFadeOut() {
  FadeRamp f;
  f.reset(160);
  f.start(0, 160, 0, kFadeOutMs);
  const int mid = f.tick(kFadeOutMs / 2);   // halfway out
  // 160 * 0.5^2.2 = 35: the light is already low halfway through, which is what the eye reads as a
  // steady fade instead of a glide that lingers.
  CHECK(mid >= 33 && mid <= 37);
  const int atReverse = f.level();

  // A wake now: the new ramp starts from the CURRENT level, no jump back up or down first.
  f.start(kFadeOutMs / 2, atReverse, 160, kFadeInMs);
  CHECK(f.level() == atReverse);
  CHECK(f.active());

  int last = atReverse;
  int changes = 0;
  for (uint32_t t = kFadeOutMs / 2 + 1; t <= kFadeOutMs / 2 + kFadeInMs; ++t) {
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

  f.start(0, 10, 20, 0);        // zero duration: one call reaches the target
  CHECK(f.active());
  CHECK(f.tick(0) == 20);
  CHECK(!f.active());
  CHECK(f.tick(1) == -1);

  f.start(0, 40, 40, kFadeInMs);   // same target: nothing to do
  CHECK(!f.active());
  CHECK(f.tick(100) == -1);

  f.start(0, 255, 0, kFadeOutMs);  // full range down
  CHECK(f.tick(0) == -1);
  CHECK(f.tick(kFadeOutMs) == 0);
  CHECK(f.level() == 0);
}

int main() {
  CHECK(kFadeInMs == 300);   // Filip's longer fade-in (it was 200)
  CHECK(kFadeOutMs == 150);
  testCurve();
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
