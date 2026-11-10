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
  // gateMs is how long a NEW press is refused; the wake path uses kGateMs and a screen change
  // (EST-355) passes its own short window, so both use this one rule.
  void onWake(uint32_t nowMs, bool downAtWake, uint32_t gateMs = kGateMs);

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
  uint32_t gateMs_ = kGateMs;
};

// Tap-away for overlays (EST-339). Filip: "tapping away from the keyboard should hide it. It does,
// but now I can also press something: I pressed away, in the chat area, and it opened a chat menu.
// The first tap away should only hide it and not press anything behind it."
//
// So a press that lands outside an open overlay CLOSES the overlay and spends the rest of that
// gesture: its release and any drag that follows from the same press must not reach what was behind
// it. Only the next new press may act. The caller closes the overlay when onPress() returns true and
// then does nothing else (no tap, no scroll) until the finger lifts.
class OverlayDismiss {
 public:
  // A new press. `outside` is true when an overlay is open and the finger landed outside it.
  // Returns true when the caller must close that overlay now; the whole gesture is then spent.
  bool onPress(bool outside) {
    spent_ = outside;
    return spent_;
  }

  // True from that press until it lifts: drag and release must do nothing.
  bool spent() const { return spent_; }

  // The finger lifted: the next new press is a fresh one and may act on what is behind.
  void onRelease() { spent_ = false; }

 private:
  bool spent_ = false;
};

}  // namespace om
