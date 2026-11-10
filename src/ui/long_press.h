// SPDX-License-Identifier: MIT
// A long press as a rule, not as three copies in the loop: press, hold past 450 ms without moving,
// and the action fires once while the finger is still down. Releasing before the threshold is a tap;
// releasing after it is nothing more (the press was already spent); moving more than 12 px cancels it,
// which is what keeps a scroll from opening a menu (EST-342).
//
// Plain C++ (no Arduino, no LovyanGFX) so test/host drives it with fake milliseconds.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

struct LongPress {
  static constexpr uint32_t kHoldMs = 450;   // the ticket's "about 450 ms"
  static constexpr int kMovePx = 12;         // the same finger slop the screen's drag uses

  bool down = false;
  bool fired = false;
  bool cancelled = false;
  int x = 0;
  int y = 0;
  uint32_t startMs = 0;

  void press(int px, int py, uint32_t nowMs) {
    down = true;
    fired = false;
    cancelled = false;
    x = px;
    y = py;
    startMs = nowMs;
  }

  // Call while the finger is down. Returns true exactly once, on the first call at or past the
  // threshold; a moved finger cancels instead and returns false.
  bool update(int px, int py, uint32_t nowMs) {
    if (!down || fired || cancelled) return false;
    const int dx = px - x;
    const int dy = py - y;
    if (dx > kMovePx || dx < -kMovePx || dy > kMovePx || dy < -kMovePx) {
      cancelled = true;
      return false;
    }
    if ((int32_t)(nowMs - startMs) < (int32_t)kHoldMs) return false;
    fired = true;
    return true;
  }

  // The finger lifted. True when this was a plain tap (down, never fired, never cancelled): the
  // caller may run the tap action. A fired hold returns false, so no tap fires as well.
  bool release() {
    const bool wasTap = down && !fired && !cancelled;
    down = false;
    return wasTap;
  }

  void reset() {
    down = false;
    fired = false;
    cancelled = false;
  }
};

}  // namespace ui
}  // namespace om
