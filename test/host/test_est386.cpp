// SPDX-License-Identifier: MIT
// EST-386: the NTP attempt's modem-sleep decision, with the system g++ over the header the firmware
// compiles (src/ntp_sleep.h).
//
// It proves the four cases the ticket names, plus the ones the fix would otherwise get wrong:
//   * Bluetooth OFF: the attempt turns modem sleep off and its end turns it back on, once;
//   * Bluetooth ON: the radio is never touched, not at the start, not at the end, not by poll();
//   * Bluetooth starts mid-attempt: sleep is put back at once, once, and a later BLE stop does not
//     turn it off again (the guard never fights a BLE start/stop);
//   * a refused change (the framework said no) leaves the guard neutral, so the attempt's end does
//     not turn sleep on behind the caller's back;
//   * the four entry points compose into the final radio state a small fake applies.
#include <stdio.h>

#include "ntp_sleep.h"

using namespace om::ntp;

static int g_fail = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL %s\n", what);
    ++g_fail;
  }
}

static bool is(SleepAction a, SleepAction b) { return a == b; }

// ---- 1. Bluetooth off: off for the attempt, back on at its end --------------------------------
static void testBluetoothOff() {
  SleepGuard g;
  check(is(g.begin(false), SleepAction::TurnOff), "BT off: the attempt may turn sleep off");
  check(g.attemptOpen(), "BT off: the attempt is open");
  g.offResult(true);
  check(g.ownsOff(), "BT off: the attempt owns the off state");
  for (int i = 0; i < 5; ++i) {
    check(is(g.poll(false), SleepAction::None), "BT off: poll touches nothing");
  }
  check(is(g.end(), SleepAction::TurnOn), "BT off: the end restores modem sleep");
  check(!g.ownsOff() && !g.attemptOpen(), "BT off: the guard is closed after the end");
  check(is(g.end(), SleepAction::None), "BT off: a second end touches nothing");
  check(is(g.poll(false), SleepAction::None), "BT off: poll after the end touches nothing");
}

// ---- 2. Bluetooth on at the start: untouched, start, middle and end ----------------------------
static void testBluetoothOn() {
  SleepGuard g;
  check(is(g.begin(true), SleepAction::None), "BT on: the start does not touch modem sleep");
  check(g.attemptOpen(), "BT on: the attempt is open");
  check(!g.ownsOff(), "BT on: the attempt owns nothing");
  check(is(g.poll(true), SleepAction::None), "BT on: poll does not touch it");
  // Even a caller that wrongly reports a change must not make the guard restore anything.
  g.offResult(true);
  check(!g.ownsOff(), "BT on: a stray off-report cannot create an off state");
  check(is(g.poll(true), SleepAction::None), "BT on: poll still does not touch it");
  check(is(g.end(), SleepAction::None), "BT on: the end leaves modem sleep exactly as it was");
  check(is(g.end(), SleepAction::None), "BT on: a second end touches nothing");
}

// ---- 3. Bluetooth starts mid-attempt: restored at once, then left alone ------------------------
static void testBluetoothStartsMidAttempt() {
  SleepGuard g;
  check(is(g.begin(false), SleepAction::TurnOff), "mid: BT was off, sleep may go off");
  g.offResult(true);
  check(is(g.poll(false), SleepAction::None), "mid: still no BT, nothing to do");
  check(is(g.poll(true), SleepAction::TurnOn), "mid: BT started, sleep goes back AT ONCE");
  check(!g.ownsOff(), "mid: the guard no longer owns an off state");
  for (int i = 0; i < 3; ++i) {
    check(is(g.poll(true), SleepAction::None), "mid: restored once only");
  }
  // A BLE stop inside the same attempt must not turn sleep off again.
  check(is(g.poll(false), SleepAction::None), "mid: a BLE stop does not turn sleep off again");
  check(!g.ownsOff(), "mid: the attempt never re-owns the off state");
  check(is(g.end(), SleepAction::None), "mid: the end does not fight the restored state");
}

// ---- 4. A refused change: neutral, so the end does not turn sleep on ---------------------------
static void testRefusedChange() {
  SleepGuard g;
  check(is(g.begin(false), SleepAction::TurnOff), "refused: the attempt asks for off");
  g.offResult(false);   // the framework refused, modem sleep is still on
  check(!g.ownsOff(), "refused: the guard does not own a state it did not create");
  check(is(g.poll(true), SleepAction::None), "refused: a BLE start has nothing to restore");
  check(is(g.end(), SleepAction::None), "refused: the end does not turn sleep on spuriously");
}

// ---- 5. A stale attempt is dropped, not fought over --------------------------------------------
static void testRestart() {
  SleepGuard g;
  check(is(g.begin(false), SleepAction::TurnOff), "restart: first attempt takes the off state");
  g.offResult(true);
  check(is(g.begin(true), SleepAction::None), "restart: a new attempt with BT on touches nothing");
  check(!g.ownsOff(), "restart: the old state is not carried into the new attempt");
  check(is(g.poll(true), SleepAction::None), "restart: poll does not restore a dropped state");
  check(is(g.end(), SleepAction::None), "restart: the end touches nothing");
}

// ---- 6. One full attempt, composed with a fake radio -------------------------------------------
// The fake is the caller's half of the contract: TurnOff -> the radio goes off, TurnOn -> back on.
static void testFakeRadio() {
  bool sleepOn = true;
  int changes = 0;
  auto apply = [&](SleepAction a) {
    if (a == SleepAction::TurnOff) {
      sleepOn = false;
      ++changes;
    } else if (a == SleepAction::TurnOn) {
      sleepOn = true;
      ++changes;
    }
  };

  SleepGuard g;
  apply(g.begin(false));
  g.offResult(!sleepOn);
  check(!sleepOn, "radio: BT off -> modem sleep really off during the attempt");
  apply(g.poll(false));
  apply(g.end());
  check(sleepOn, "radio: BT off -> restored at the end");
  check(changes == 2, "radio: exactly one off and one on");

  sleepOn = true;
  changes = 0;
  SleepGuard h;
  apply(h.begin(true));
  apply(h.poll(true));
  apply(h.end());
  check(sleepOn, "radio: BT on -> still on");
  check(changes == 0, "radio: BT on -> the radio was never touched");

  changes = 0;
  SleepGuard i;
  apply(i.begin(false));
  i.offResult(!sleepOn);
  check(!sleepOn, "radio: mid-attempt starts with sleep off");
  apply(i.poll(true));
  check(sleepOn, "radio: a BLE start turns sleep back on at once");
  apply(i.end());
  check(changes == 2, "radio: one off, one on, and the end adds nothing");
}

int main() {
  testBluetoothOff();
  testBluetoothOn();
  testBluetoothStartsMidAttempt();
  testRefusedChange();
  testRestart();
  testFakeRadio();
  if (g_fail != 0) {
    printf("%d est386 sleep-guard check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("est386: the NTP modem-sleep guard leaves BT alone, restores an off state once, and "
         "restores at once when BT starts mid-attempt\n");
  return 0;
}
