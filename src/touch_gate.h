// SPDX-License-Identifier: MIT
// Wake-time touch gate: the first touch after the screen wakes is never a tap.
//
// Why this file exists: the wake button brings the panel back and the app pushes a fresh frame, but
// a finger that was already resting on the glass (or one that lands while that frame is still being
// pushed) used to arrive as an ordinary tap and press whatever happened to be under it. The gate
// accepts only a NEW press that begins at least kGateMs after the wake; a press already down at the
// wake, or one that begins inside the window, is ignored until it lifts. The same rule applies to
// any future auto-sleep, because the wake path is the only way the screen comes back.
//
// Plain C++ (no Arduino), so test/host runs the real logic with the system g++.
#pragma once

#include <stdint.h>

namespace om {

class TouchGate {
 public:
  // 300 ms: long enough for the panel's sleep-out and the first frame push, short enough that a
  // deliberate tap right after waking still feels immediate.
  static constexpr uint32_t kGateMs = 300;

  // The screen woke at nowMs; downAtWake says a finger was already on the glass at that moment.
  void onWake(uint32_t nowMs, bool downAtWake);

  // One sample per loop. Returns true when this sample may be treated as touch input; the caller
  // keeps its own press/release edge detection on top. `down` is the raw panel state.
  bool accept(uint32_t nowMs, bool down);

  // No gate at all (boot: the screen was never asleep).
  void reset();

  bool gateActive() const { return active_; }

 private:
  bool active_ = false;
  bool latch_ = false;      // this press must lift before anything counts
  bool prevDown_ = false;
  uint32_t wakeMs_ = 0;
};

}  // namespace om
