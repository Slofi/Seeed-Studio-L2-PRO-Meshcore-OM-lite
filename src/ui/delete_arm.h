// SPDX-License-Identifier: MIT
// EST-399: ONE "tap again" arm, and it belongs to ONE row.
//
// The shell had a single `g_deleteArmed` flag for three different delete rows (the Wi-Fi Forget
// button of every saved network, the Chat page's "Delete channel" and the Channel page's). The Wi-Fi
// page builds one Forget button per saved network, and the handler only asked whether SOME row was
// armed, never whether it was THIS row: arm network A, tap Forget on network B, and B's password was
// deleted on the first tap with no second tap and no way to cancel (EST-398 #1, S2).
//
// This struct is one arm per row family. It remembers WHICH row was armed and WHEN, so:
//   * a second tap on the SAME row inside the window is the confirmation,
//   * a tap on any other row re-arms THAT row (tap B after arming A -> B is armed, B is not
//     forgotten), which is exactly what the ticket asks,
//   * the arm dies by itself after kDeleteArmMs, so a half-pressed button is never left live.
// `now` is millis(); every comparison is subtraction in uint32_t, so it survives the wrap.
//
// Plain C++ (no Arduino, no LovyanGFX): test/host/test_est399.cpp proves the four rules.
//
// The shell keeps one DeleteArm per row family (Wi-Fi Forget rows, the Chat page's delete, the
// Channel page's delete), never one for all of them: that was the bug.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

// How long a "tap again" stays armed. Long enough to read the new label and tap again, short enough
// that a button left half-pressed is not still live a minute later.
constexpr uint32_t kDeleteArmMs = 6000;

struct DeleteArm {
  // What a tap on a row means. Confirmed is the ONLY value that may run the destructive call.
  enum class Tap : uint8_t { Armed = 0, Confirmed = 1 };

  int16_t index = -1;      // the armed row, -1 when nothing is armed
  uint32_t armedMs = 0;    // when it was armed, on the millis() clock

  void clear() {
    index = -1;
    armedMs = 0;
  }

  bool expired(uint32_t now) const {
    return index >= 0 && (uint32_t)(now - armedMs) >= kDeleteArmMs;
  }

  // The row that may show "Tap again ..." at `now`, or -1 once the window has run out. The drawing
  // asks THIS, so a stale arm cannot light a label that a tap would no longer honour.
  int armedRow(uint32_t now) const {
    if (index < 0 || expired(now)) return -1;
    return (int)index;
  }

  bool isArmed(int row, uint32_t now) const { return row >= 0 && armedRow(now) == row; }

  // One tap on `row`. The same row inside the window confirms (and disarms); every other row arms
  // itself and disarms whatever was armed before.
  Tap tap(int row, uint32_t now) {
    if (isArmed(row, now)) {
      clear();
      return Tap::Confirmed;
    }
    index = (int16_t)row;
    armedMs = now;
    return Tap::Armed;
  }
};

}  // namespace ui
}  // namespace om
