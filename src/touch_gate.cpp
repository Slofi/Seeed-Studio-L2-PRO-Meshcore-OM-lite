// SPDX-License-Identifier: MIT
// Wake-time touch gate. See touch_gate.h for why.
#include "touch_gate.h"

namespace om {

void TouchGate::onWake(uint32_t nowMs, bool downAtWake) {
  active_ = true;
  latch_ = downAtWake;      // the finger that was already there must lift first
  prevDown_ = downAtWake;
  wakeMs_ = nowMs;
}

bool TouchGate::accept(uint32_t nowMs, bool down) {
  const bool pressed = down && !prevDown_;
  prevDown_ = down;

  if (!active_) return down;
  if (!down) {
    latch_ = false;         // the finger lifted: the next press may count
    return false;
  }
  if (latch_) return false; // held through the wake, or begun inside the window

  // A press that begins inside the window is ignored until it lifts, so it can never turn into a
  // tap merely because the finger stayed down past the 300 ms mark.
  if ((uint32_t)(nowMs - wakeMs_) < kGateMs) {
    latch_ = true;
    return false;
  }
  if (!pressed) return false;
  active_ = false;          // the first honest new press ends the gate
  return true;
}

void TouchGate::reset() {
  active_ = false;
  latch_ = false;
  prevDown_ = false;
  wakeMs_ = 0;
}

}  // namespace om
