// SPDX-License-Identifier: MIT
// EST-405, the C++ half: the screen auto-off timer and its two saved values. This proves the rules
// the ticket states, not the drawing:
//  * DEFAULT OFF: a fresh store and a missing NVS key are OFF, and a disabled timer never sleeps;
//  * the five choices (30 s, 1 min, 2 min, 5 min, 10 min), 1 min as the default, and TimeIndex out of
//    range refused;
//  * the count restarts on a touch, on a wake-button press (onActivity) and on a USB command, and
//    millis() wrapping does not break it;
//  * each blocker alone keeps the screen on (keyboard/composer, pop-up, OTA, map drag/zoom, and the
//    pop-up toast for its 5 s), and clearing a blocker gives the WHOLE time again, never an instant
//    sleep the moment the keyboard closes;
//  * the 30 s USB quiet window is honoured on its own;
//  * the store writes ONLY when a value really changes, and a failed write still flips RAM.
// No Arduino and no LovyanGFX: src/screen_idle.h is plain C++.
#include <stdio.h>
#include <string.h>

#include "screen_idle.h"

using namespace om;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// ---------------------------------------------------------------------------------------- the table
static void testChoiceTable() {
  CHECK(kIdleTimeCount == 5);
  CHECK(kIdleTimeDefaultIndex == 1);
  // The ticket's five values, in its order, with its labels.
  CHECK(kIdleTimes[0].ms == 30000 && kIdleTimes[1].ms == 60000 && kIdleTimes[2].ms == 120000 &&
        kIdleTimes[3].ms == 300000 && kIdleTimes[4].ms == 600000);
  CHECK(kIdleTimes[kIdleTimeDefaultIndex].ms == 60000);   // 1 min is the default
  for (int i = 0; i < kIdleTimeCount; ++i) {
    // Strictly increasing, so the order on the page is the order of time.
    if (i > 0) CHECK(kIdleTimes[i].ms > kIdleTimes[i - 1].ms);
    CHECK(kIdleTimes[i].label != nullptr && kIdleTimes[i].label[0] != 0);
  }
  CHECK(kUsbQuietMs == 30000);
  CHECK(kIdleTouchPollMs > 0 && kIdleTouchPollMs < 100);   // a poll, not a spin
}

// ------------------------------------------------------------------------------------ default state
static void testDefaultOff() {
  ScreenIdle t;
  t.begin(false, kIdleTimeDefaultIndex, 0);
  CHECK(!t.enabled());
  CHECK(t.timeIndex() == kIdleTimeDefaultIndex);
  CHECK(t.timeoutMs() == 60000);

  const IdleBlockers clear;
  CHECK(!t.step(0, true, clear));
  CHECK(!t.step(60000, true, clear));
  // An hour of idleness with the switch off still never sleeps the screen.
  for (uint32_t now = 0; now < 3600000; now += 60000) CHECK(!t.step(now, true, clear));

  // A fresh object is OFF too, before begin().
  ScreenIdle fresh;
  CHECK(!fresh.enabled());
}

// ----------------------------------------------------------------------------------------- one time
static void testCountdown() {
  ScreenIdle t;
  t.begin(true, 1, 1000);   // 1 min, counting from 1000
  const IdleBlockers clear;
  CHECK(!t.step(60999, true, clear));   // 59.999 s
  CHECK(t.step(61000, true, clear));    // exactly 60 s (1000 + 60000)
  // The count restarted from the moment it slept: no second sleep on the next pass.
  CHECK(!t.step(61001, true, clear));
  CHECK(!t.step(120999, true, clear));
  CHECK(t.step(121000, true, clear));

  // A screen that is already off is never asked to sleep (the caller would get a no-op anyway).
  ScreenIdle off;
  off.begin(true, 0, 0);
  CHECK(!off.step(1000000, false, clear));
}

// -------------------------------------------------------------------------------- restarts on input
static void testActivityResets() {
  const IdleBlockers clear;

  // A touch.
  ScreenIdle t;
  t.begin(true, 0, 0);   // 30 s
  CHECK(!t.step(25000, true, clear));
  t.onActivity(25000);
  CHECK(!t.step(54999, true, clear));
  CHECK(t.step(55000, true, clear));

  // A wake-button press is the same call.
  ScreenIdle p;
  p.begin(true, 0, 0);
  p.onActivity(29000);
  CHECK(!p.step(58999, true, clear));
  CHECK(p.step(59000, true, clear));

  // A USB command: activity AND the 30 s quiet window, both.
  ScreenIdle u;
  u.begin(true, 0, 0);
  CHECK(u.usbQuiet(0));
  u.onUsbCommand(10000);
  CHECK(!u.usbQuiet(39999));
  CHECK(u.usbQuiet(40000));
  // A blocked pass holds the count at zero, so the quiet window is a PAUSE: the whole 30 s starts
  // again when it ends (from the last blocked pass, 39999 here), never the instant it closes.
  CHECK(!u.step(39999, true, clear));
  CHECK(!u.step(40000, true, clear));
  CHECK(!u.step(69998, true, clear));
  CHECK(u.step(69999, true, clear));

  // A longer timeout with a USB command: the quiet window alone ends at 30 s, the timeout still runs.
  ScreenIdle usbLong;
  usbLong.begin(true, 4, 0);   // 10 min
  usbLong.onUsbCommand(1000);
  CHECK(!usbLong.step(31000, true, clear));   // quiet is over, the count is not
  CHECK(!usbLong.step(600999, true, clear));
  CHECK(usbLong.step(601000, true, clear));   // 1000 + 600000

  // A switch-off and on again restarts the count; so does a new time choice.
  ScreenIdle s;
  s.begin(false, 0, 0);
  s.setEnabled(true, 5000);
  CHECK(!s.step(34999, true, clear));
  CHECK(s.step(35000, true, clear));
  s.setEnabled(false, 35000);
  CHECK(!s.step(999999, true, clear));
  s.setEnabled(true, 1000000);
  CHECK(!s.step(1029999, true, clear));
  CHECK(s.step(1030000, true, clear));

  ScreenIdle c;
  c.begin(true, 0, 0);
  c.setTimeoutIndex(4, 2000);   // 10 min from 2000
  CHECK(c.timeIndex() == 4);
  CHECK(c.timeoutMs() == 600000);
  CHECK(!c.step(601999, true, clear));
  CHECK(c.step(602000, true, clear));
  c.setTimeoutIndex(-1, 700000);   // refused, nothing changes
  c.setTimeoutIndex(kIdleTimeCount, 700000);
  CHECK(c.timeIndex() == 4);
}

// ------------------------------------------------------------------------------------ millis() wrap
static void testMillisWrap() {
  ScreenIdle t;
  const uint32_t start = 0xFFFFF000u;   // 4096 ms before the wrap
  t.begin(true, 0, start);
  const IdleBlockers clear;
  CHECK(!t.step(0xFFFFFFFFu, true, clear));          // 4095 ms
  CHECK(t.step((uint32_t)(start + 30000), true, clear));   // wraps past zero
  t.onActivity(0xFFFFFF00u);
  CHECK(!t.step((uint32_t)(0xFFFFFF00u + 29999), true, clear));
  CHECK(t.step((uint32_t)(0xFFFFFF00u + 30000), true, clear));

  // The USB quiet window across the wrap too.
  ScreenIdle u;
  u.begin(true, 4, 0);
  u.onUsbCommand(0xFFFFF000u);
  CHECK(!u.usbQuiet((uint32_t)(0xFFFFF000u + 29999)));
  CHECK(u.usbQuiet((uint32_t)(0xFFFFF000u + 30000)));
}

// ------------------------------------------------------------------------------------------ blockers
static void testBlockers() {
  const IdleBlockers clear;
  CHECK(!idleBlocked(clear));

  // Each blocker on its own, on a timer whose time has more than run out.
  IdleBlockers b;
  b.keyboard = true;
  CHECK(idleBlocked(b));
  b = IdleBlockers{};
  b.popup = true;
  CHECK(idleBlocked(b));
  b = IdleBlockers{};
  b.ota = true;
  CHECK(idleBlocked(b));
  b = IdleBlockers{};
  b.mapGesture = true;
  CHECK(idleBlocked(b));
  b = IdleBlockers{};
  b.toast = true;
  CHECK(idleBlocked(b));

  struct Case {
    const char* name;
    IdleBlockers blockers;
  };
  const Case cases[] = {
      {"keyboard", IdleBlockers{true, false, false, false, false}},
      {"popup", IdleBlockers{false, true, false, false, false}},
      {"ota", IdleBlockers{false, false, true, false, false}},
      {"map", IdleBlockers{false, false, false, true, false}},
      {"toast", IdleBlockers{false, false, false, false, true}},
  };
  for (const Case& c : cases) {
    ScreenIdle t;
    t.begin(true, 0, 0);   // 30 s
    // Way past the timeout: a blocked timer never sleeps, however long it is blocked.
    for (uint32_t now = 0; now <= 600000; now += 5000) {
      if (!t.step(now, true, c.blockers)) continue;
      printf("FAIL blocked by %s slept at %lu ms\n", c.name, (unsigned long)now);
      ++g_fail;
      break;
    }
    // The blocker lifts at 600000: the WHOLE time is still needed, never an instant sleep.
    CHECK(!t.step(600001, true, clear));
    CHECK(!t.step(629999, true, clear));
    CHECK(t.step(630000, true, clear));
  }

  // Two blockers at once behave like one.
  ScreenIdle t;
  t.begin(true, 0, 0);
  IdleBlockers both;
  both.keyboard = true;
  both.ota = true;
  CHECK(!t.step(100000, true, both));
  CHECK(!t.step(100001, true, clear));
  CHECK(t.step(130001, true, clear));
}

// -------------------------------------------------------------------------------------- the store
struct FakeNvs {
  bool has[4] = {false, false, false, false};
  uint8_t value[4] = {0, 0, 0, 0};
  int reads = 0;
  int writes = 0;
  bool failWrite = false;
  const char* keys[4] = {kIdlePrefKey, kIdleTimePrefKey, "other1", "other2"};
};

static int keyIndex(FakeNvs& nvs, const char* key) {
  for (int i = 0; i < 4; ++i) {
    if (key != nullptr && nvs.keys[i] != nullptr && strcmp(key, nvs.keys[i]) == 0) return i;
  }
  return -1;
}

static bool fakeRead(void* ctx, const char* key, uint8_t fallback, uint8_t& out) {
  FakeNvs& nvs = *(FakeNvs*)ctx;
  ++nvs.reads;
  const int i = keyIndex(nvs, key);
  if (i < 0 || !nvs.has[i]) return false;
  (void)fallback;
  out = nvs.value[i];
  return true;
}

static bool fakeWrite(void* ctx, const char* key, uint8_t value) {
  FakeNvs& nvs = *(FakeNvs*)ctx;
  ++nvs.writes;
  if (nvs.failWrite) return false;
  const int i = keyIndex(nvs, key);
  if (i < 0) return false;
  nvs.has[i] = true;
  nvs.value[i] = value;
  return true;
}

static void testPrefs() {
  // A missing key: OFF and 1 min, and NOTHING is written for it.
  {
    FakeNvs nvs;
    ScreenIdlePrefs p;
    p.begin(fakeRead, fakeWrite, &nvs);
    p.load();
    CHECK(!p.enabled());
    CHECK(p.timeIndex() == kIdleTimeDefaultIndex);
    CHECK(nvs.writes == 0);
    CHECK(nvs.reads == 2);   // both keys are read, once each
  }

  // A set writes once; setting the same value again writes nothing.
  {
    FakeNvs nvs;
    ScreenIdlePrefs p;
    p.begin(fakeRead, fakeWrite, &nvs);
    p.load();
    CHECK(p.setEnabled(true));
    CHECK(nvs.writes == 1);
    CHECK(nvs.has[0] && nvs.value[0] == 1);
    CHECK(p.setEnabled(true));       // already on
    CHECK(nvs.writes == 1);
    CHECK(p.setTimeIndex(1));        // already 1 min
    CHECK(nvs.writes == 1);
    CHECK(p.setTimeIndex(3));
    CHECK(nvs.writes == 2);
    CHECK(nvs.has[1] && nvs.value[1] == 3);
    CHECK(p.setTimeIndex(3));        // already 5 min
    CHECK(nvs.writes == 2);
    CHECK(p.setEnabled(false));
    CHECK(nvs.writes == 3);
    CHECK(nvs.value[0] == 0);
    // Out of range: refused, no write.
    CHECK(!p.setTimeIndex(-1));
    CHECK(!p.setTimeIndex(kIdleTimeCount));
    CHECK(nvs.writes == 3);
    CHECK(p.timeIndex() == 3);
  }

  // What was saved loads back.
  {
    FakeNvs nvs;
    ScreenIdlePrefs a;
    a.begin(fakeRead, fakeWrite, &nvs);
    a.load();
    a.setEnabled(true);
    a.setTimeIndex(2);
    ScreenIdlePrefs b;
    b.begin(fakeRead, fakeWrite, &nvs);
    b.load();
    CHECK(b.enabled());
    CHECK(b.timeIndex() == 2);
  }

  // An out-of-range stored time falls back to 1 min; a non-zero switch byte is ON.
  {
    FakeNvs nvs;
    nvs.has[0] = true;
    nvs.value[0] = 2;   // any non-zero is on
    nvs.has[1] = true;
    nvs.value[1] = 9;   // not a choice
    ScreenIdlePrefs p;
    p.begin(fakeRead, fakeWrite, &nvs);
    p.load();
    CHECK(p.enabled());
    CHECK(p.timeIndex() == kIdleTimeDefaultIndex);
  }

  // A failed write still flips RAM (the switch behaves like the other saved switches).
  {
    FakeNvs nvs;
    ScreenIdlePrefs p;
    p.begin(fakeRead, fakeWrite, &nvs);
    p.load();
    nvs.failWrite = true;
    CHECK(!p.setEnabled(true));
    CHECK(p.enabled());          // RAM follows the request
    CHECK(!p.setTimeIndex(4));
    CHECK(p.timeIndex() == 4);
  }

  // No backend at all: defaults load and a set is reported as not saved but still flips RAM.
  {
    ScreenIdlePrefs p;
    p.begin(nullptr, nullptr, nullptr);
    p.load();
    CHECK(!p.enabled());
    CHECK(p.timeIndex() == kIdleTimeDefaultIndex);
    CHECK(!p.setEnabled(true));
    CHECK(p.enabled());
    CHECK(!p.setTimeIndex(0));
    CHECK(p.timeIndex() == 0);
  }

  // The saved values drive the timer: ON + 2 min sleeps at 120 s from boot.
  {
    FakeNvs nvs;
    nvs.has[0] = true;
    nvs.value[0] = 1;
    nvs.has[1] = true;
    nvs.value[1] = 2;
    ScreenIdlePrefs p;
    p.begin(fakeRead, fakeWrite, &nvs);
    p.load();
    ScreenIdle t;
    t.begin(p.enabled(), p.timeIndex(), 0);
    const IdleBlockers clear;
    CHECK(!t.step(119999, true, clear));
    CHECK(t.step(120000, true, clear));
  }
}

int main() {
  testChoiceTable();
  testDefaultOff();
  testCountdown();
  testActivityResets();
  testMillisWrap();
  testBlockers();
  testPrefs();
  if (g_fail == 0) {
    printf("est405: idle timer and store, every blocker, default OFF and write-only-on-change\n");
    return 0;
  }
  printf("est405: %d failure(s)\n", g_fail);
  return 1;
}
