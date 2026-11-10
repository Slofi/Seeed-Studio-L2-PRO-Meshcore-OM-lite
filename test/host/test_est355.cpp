// SPDX-License-Identifier: MIT
// EST-355: the screen-change backlight transition and the dip rule, with the system g++.
//
// It proves, over the same headers the firmware compiles:
//   * the ORDER of a dipped change: fade out (down only, exactly kChangeOutMs), then exactly one push
//     asked while the light is 0, then one panel frame of settle (kChangeSettleMs) with no level
//     written, then a fade in that ends at the screen brightness (kChangeInMs) and reports Done;
//   * an instant change (two frames of the same brightness): the push is asked at once, no level is
//     written at all, and the change is over with out/settle/in all 0, so nothing blinks;
//   * a second change asked for while the light is still going down COALESCES: it does not restart the
//     ramp, does not raise the light, and there is still exactly one push;
//   * a dip asked for before the (instant) push has happened escalates in place, and a change asked for
//     after the push is a new transition (the first frame is already on the glass);
//   * the touches: nothing is read while the change runs, a finger that is down when it ends must lift
//     first (the wake path's own TouchGate, with the change's window), and the next new press counts;
//   * the brightness numbers: the luma arithmetic, the sample mean and the dip rule (the map on either
//     side, and a brightness difference above the threshold).
#include <stdio.h>

#include "frame_metrics.h"
#include "screen_change.h"
#include "touch_gate.h"
#include "ui/theme.h"

using SC = om::ScreenChange;
using Phase = om::ScreenChange::Phase;
using Action = om::ScreenChange::Action;

static int g_fail = 0;

// board.h's kScreenBrightness, which cannot be included here (it pulls in LovyanGFX).
static constexpr int kScreen = 160;

static void check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL %s\n", what);
    ++g_fail;
  }
}

// The light: what the caller would write to the backlight driver.
struct Light {
  int level = kScreen;
  int writes = 0;

  void write(int v) {
    level = v;
    ++writes;
  }
};

// One "loop pass": tick the machine, write whatever level it asked for. Returns the action.
static Action step(SC& c, Light& l, uint32_t nowMs) {
  const Action a = c.tick(nowMs);
  const int v = c.takeLevel();
  if (v >= 0) l.write(v);
  return a;
}

// --------------------------------------------------------------------------------- the dip, in order
static void testDipSequence() {
  SC c;
  Light light;
  const uint32_t t0 = 1000;
  check(c.request(t0, true, kScreen, kScreen), "a dipped change starts");
  check(c.phase() == Phase::FadeOut, "the first phase is the fade out");
  check(c.active(), "the change is active");
  check(c.dark(), "the light is owned by the change from the start");

  const int startLevel = light.level;
  int lastLevel = light.level;
  bool downOnly = true;
  int pushes = 0;
  uint32_t pushAt = 0;
  for (uint32_t t = t0; t <= t0 + 500 && pushes == 0; ++t) {
    const Action a = step(c, light, t);
    if (light.level > lastLevel) downOnly = false;
    lastLevel = light.level;
    if (a == Action::Push) {
      ++pushes;
      pushAt = t;
      break;
    }
  }
  check(pushes == 1, "exactly one push is asked");
  check(downOnly, "the light only goes down during the fade out");
  check(light.level == 0, "the light is 0 when the push is asked");
  check(pushAt == t0 + om::ui::kChangeOutMs, "the push is asked after exactly kChangeOutMs");
  check(c.outMs() == om::ui::kChangeOutMs, "the fade out's own duration is what out reports");
  check(c.pushUs() == 0, "the push has not been reported yet");
  check(c.phase() == Phase::Push, "the machine waits for the push to be reported");
  check(step(c, light, pushAt) == Action::None, "the push is asked once, not every pass");
  check(light.writes > 5, "the fade out really stepped the PWM, not one jump");

  // The push itself: the caller composes, flushes and reports. The light must not be written again
  // until one full panel frame has passed.
  const uint32_t pushEnd = pushAt + 12;   // the measured 11.4 ms push
  check(!c.pushed(pushEnd, 11400, true), "a dipped push is not the end of the change");
  check(c.phase() == Phase::Settle, "then one full panel refresh of settle");
  int writesBeforeSettle = light.writes;
  bool levelDuringSettle = false;
  for (uint32_t t = pushEnd + 1; t < pushEnd + om::ui::kChangeSettleMs; ++t) {
    if (step(c, light, t) != Action::None) levelDuringSettle = true;
    if (light.writes != writesBeforeSettle) levelDuringSettle = true;
  }
  check(!levelDuringSettle, "nothing is written and nothing happens during the settle");

  // The fade in starts at kChangeSettleMs and ends at the screen brightness, exactly kChangeInMs later.
  (void)step(c, light, pushEnd + om::ui::kChangeSettleMs);
  check(c.phase() == Phase::FadeIn, "the light starts coming up after the settle");
  check(c.settleMs() == om::ui::kChangeSettleMs, "settle reports one panel frame");
  int upOnly = 0;
  lastLevel = light.level;
  Action done = Action::None;
  uint32_t doneAt = 0;
  for (uint32_t t = pushEnd + om::ui::kChangeSettleMs + 1; t <= pushEnd + 400; ++t) {
    const Action a = step(c, light, t);
    if (a == Action::Done) {
      done = a;
      doneAt = t;
      break;
    }
    if (light.level < lastLevel) upOnly = -1;
    lastLevel = light.level;
  }
  check(done == Action::Done, "the change reports Done");
  check(upOnly == 0, "the light only goes up during the fade in");
  check(light.level == kScreen, "the light ends at the screen brightness");
  check(c.inMs() == om::ui::kChangeInMs, "in reports the fade in's duration");
  check(doneAt == pushEnd + om::ui::kChangeSettleMs + om::ui::kChangeInMs,
        "out + push + settle + in is the whole transition");
  check(!c.active(), "and the change is over");
  check(c.dipped(), "it did dip");
  check(c.pushUs() == 11400, "the measured push is kept for the log line");
  (void)startLevel;
}

// ------------------------------------------------------------------------------- an instant change
static void testInstantChange() {
  SC c;
  Light light;
  check(c.request(500, false, kScreen, kScreen),
        "an instant change starts");
  check(c.phase() == Phase::Push, "no fade out at all");
  check(c.takeLevel() == -1, "an instant change writes no level");
  check(c.tick(500) == Action::Push, "the push is asked at once");
  check(c.tick(501) == Action::None, "and asked once");
  check(c.pushed(506, 11376, false), "an instant change is over with the push");
  check(!c.active(), "not active any more");
  check(!c.dipped(), "it did not dip");
  check(c.outMs() == 0 && c.settleMs() == 0 && c.inMs() == 0, "no phase spent any time");
  check(light.writes == 0, "the light was never touched");
  check(light.level == kScreen, "and it is still where it was");
}

// ------------------------------------------------------------------------- a second change mid-dip
static void testCoalesce() {
  SC c;
  Light light;
  check(c.request(0, true, kScreen, kScreen), "first change");
  int levelAtCoalesce = kScreen;
  for (uint32_t t = 0; t < 20; ++t) step(c, light, t);
  levelAtCoalesce = light.level;
  check(levelAtCoalesce < kScreen, "the light is already going down");

  // The second request (the finger reached another tab) arrives 20 ms in: one dip, one push.
  check(!c.request(20, true, levelAtCoalesce, kScreen),
        "a second change during the dip coalesces into the running one");
  check(c.phase() == Phase::FadeOut, "the same ramp keeps running");
  int pushes = 0;
  bool roseAgain = false;
  int last = light.level;
  for (uint32_t t = 21; t <= 200 && pushes == 0; ++t) {
    const Action a = step(c, light, t);
    if (light.level > last) roseAgain = true;
    last = light.level;
    if (a == Action::Push) ++pushes;
  }
  check(pushes == 1, "still exactly one push for the two requests");
  check(!roseAgain, "the light never jumps back up between them");
  check(c.dipped(), "one dip");
}

// ------------------------------------------- a dip asked for before the push, and one asked after it
static void testEscalateAndRestart() {
  SC c;
  Light light;
  check(c.request(0, false, kScreen, kScreen),
        "an instant change is armed");
  check(c.phase() == Phase::Push, "it waits for its push");
  check(!c.request(1, true, kScreen, kScreen),
        "a dip asked for before the push joins it");
  check(c.phase() == Phase::FadeOut, "it fades out first instead");
  int pushes = 0;
  for (uint32_t t = 2; t <= 300 && pushes == 0; ++t) {
    if (step(c, light, t) == Action::Push) ++pushes;
  }
  check(pushes == 1, "and still one push");
  check(light.level == 0, "with the light out");
  check(!c.pushed(70, 11000, true), "a dark push still waits for the settle");
  for (uint32_t t = 71; t <= 400 && c.active(); ++t) step(c, light, t);
  check(!c.active(), "the escalated change completes");

  // A change asked for AFTER the push cannot be merged: the first frame is already on the glass.
  SC d;
  Light l2;
  d.request(0, false, kScreen, kScreen);
  (void)d.tick(0);
  check(d.pushed(5, 1000, false), "the first change is over");
  check(d.request(6, true, kScreen, kScreen),
        "a later change is a new transition");
  check(d.phase() == Phase::FadeOut, "with its own fade out");
}

// ------------------------------------------------------------------------------- the touches
static void testTouches() {
  SC c;
  Light light;
  om::TouchGate gate;
  const uint32_t t0 = 100;
  c.request(t0, true, kScreen, kScreen);

  // The loop's rule, exactly: while the change runs the gate is fed "no finger" and the sample is
  // forced up; when it ends the gate is armed with the finger state at that moment.
  bool anyTapDuringChange = false;
  bool downNow = false;
  uint32_t endedAt = 0;
  for (uint32_t t = t0; t < t0 + 600; ++t) {
    // A finger lands 30 ms into the dip and stays down through the whole transition.
    downNow = (t >= t0 + 30);
    if (c.active()) {
      gate.accept(t, false);   // what the loop does while a change runs
    } else if (gate.accept(t, downNow)) {
      anyTapDuringChange = true;
    }
    const Action a = step(c, light, t);
    if (a == Action::Push) c.pushed(t, 11376, true);
    if (a == Action::Done) {
      endedAt = t;
      gate.onWake(t, downNow, om::ui::kChangeGateMs);
    }
    if (endedAt != 0 && t > endedAt + 2) break;
  }
  check(!c.active(), "the change finished");
  check(!anyTapDuringChange, "no touch is accepted while the change runs");

  // The finger that was down when it ended must lift before it can tap.
  check(!gate.accept(endedAt + 3, true), "the finger that came through the change is swallowed");
  check(!gate.accept(endedAt + 4, true), "and stays swallowed while it is held");
  check(!gate.accept(endedAt + 5, false), "lifting is not a tap");
  check(!gate.accept(endedAt + 6, true), "a new press inside the window is still swallowed");
  check(!gate.accept(endedAt + 7, false), "lift again");
  check(gate.accept(endedAt + om::ui::kChangeGateMs, true), "a new press after the window counts");
  check(!gate.accept(endedAt + om::ui::kChangeGateMs + 1, false), "and its release is not a tap");

  // A finger that lifts during the change never becomes a tap either: the gate saw "no finger" all
  // the way through, so there is no press edge waiting when the change is over.
  om::TouchGate g2;
  g2.accept(0, false);   // no finger before the change
  g2.accept(1, false);   // the change fed it "no finger" while a finger was really down
  g2.accept(2, false);
  check(!g2.accept(3, false), "lifting during the change is not a tap");
}

// ------------------------------------------------------------------------ the brightness numbers
static void testLumaAndRule() {
  check(om::lumaFromRgb565(0x0000) == 0, "black is 0");
  check(om::lumaFromRgb565(0xFFFF) == 250, "white is 250 (5-bit red and blue stop at 248)");
  const uint8_t gold = om::lumaFromRgb565(om::ui::kAcc);   // the boot probe's colour
  check(gold == 181, "the theme gold's luma");
  // A dark page: the app background #131210 and a row line #333230 are both near the bottom.
  const uint8_t bg = om::lumaFromRgb565(om::ui::kBg);
  check(bg < 25, "the app background is dark");
  const uint8_t line = om::lumaFromRgb565(om::ui::kLine);
  check(line > bg && line < 70, "a separator is brighter but still dark");
  // A topo tile: the map's beige is far above every dark page.
  const uint8_t beige = om::lumaFromRgb565(om::ui::rgb565(0xE8D0A0));
  check(beige > 150, "a map tile is bright");

  check(om::meanLumaFromSum(0, 0) == 0, "no samples read as black");
  check(om::meanLumaFromSum(1000, 100) == 10, "the mean is sum / count");
  check(om::meanLumaFromSum(999, 100) == 9, "and it truncates");
  check(om::meanLumaFromSum(100 * 255, 100) == 255, "a full-white frame is 255");
  // The sample count the firmware takes: 320x240 at a 15 px step.
  const int samples = ((320 - om::ui::kChangeLumaStep / 2 - 1) / om::ui::kChangeLumaStep + 1) *
                      ((240 - om::ui::kChangeLumaStep / 2 - 1) / om::ui::kChangeLumaStep + 1);
  check(samples >= 300 && samples <= 400, "the brightness sample is about 336 pixels");

  const uint8_t t = om::ui::kChangeLumaDelta;
  check(om::screenChangeNeedsDip(bg, beige, false, t), "a bright page coming in from a dark one dips");
  check(om::screenChangeNeedsDip(beige, bg, false, t), "and a dark page coming in from it too");
  check(!om::screenChangeNeedsDip(18, 22, false, t), "two dark pages stay instant");
  check(!om::screenChangeNeedsDip(bg, line, false, t), "a separator does not need a dip");
  check(om::screenChangeNeedsDip(18, 18, true, t), "the map on the old side always dips");
  check(om::screenChangeNeedsDip(18, 18, true, t), "the map on the new side always dips");
  check(!om::screenChangeNeedsDip(100, 100, false, t), "two equally bright pages stay instant");
  check(!om::screenChangeNeedsDip(100, 100 + t, false, t), "exactly the threshold is not enough");
  check(om::screenChangeNeedsDip(100, 100 + t + 1, false, t), "one above the threshold is");
  check(om::screenChangeNeedsDip(100 + t + 1, 100, false, t), "in either direction");
  check(om::ui::kChangeSettleMs == 17, "one panel frame at ~60 Hz");
  check(SC::plannedMs() == om::ui::kChangeOutMs + om::ui::kChangeSettleMs + om::ui::kChangeInMs,
        "the planned window is out + settle + in");
  check(SC::plannedMs() < 200, "a change is short enough to feel like a switch");
}

int main() {
  testLumaAndRule();
  testInstantChange();
  testDipSequence();
  testCoalesce();
  testEscalateAndRestart();
  testTouches();
  if (g_fail) {
    printf("est355: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("est355: the screen-change dip (order, durations, coalescing, touches) and the dip rule ok\n");
  return 0;
}
