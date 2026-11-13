// SPDX-License-Identifier: MIT
// EST-422: the ONE rule that decides whether a release is a TAP. One rule for the whole app, no
// per-screen copy.
//
// Filip (2026-11-13): "when I want to 'move fast' through the settings, it detects it as a tap, not a
// scroll", and the setting under the finger changed. Claude found the cause in the loop: on release
// it ran `if (!g_drag && !g_acted) handleTap(g_sx, g_sy, false)`, and `g_drag` was latched only by a
// DOWN sample more than 12 px from the press. A quick flick could start and end between two samples
// (the loop's 20 ms delay plus a ~11 ms frame push is a long blind window), so no sample crossed
// 12 px, `g_drag` stayed false and the release tapped the PRESS point: the row or the switch under
// the finger.
//
// A gesture is a tap unless the finger was SEEN to move (or a drag/hold took it); this struct keeps:
//   * `samples`: how many down samples the gesture was seen in, the press included. EST-422 refused a
//     gesture seen only once; S498 (Filip) made it a tap again, because light taps are seen once too
//     and the important Settings are now behind the confirm sheet. Kept for the log line and tests.
//   * `lastX/lastY`: the last point the panel reported. The release edge is checked against the
//     press point too, so a gesture that was seen to end away from where it began is a drag even if
//     the vertical 12 px test never fired (that test only looks at Y: a sideways flick on the map or
//     the keyboard never set g_drag at all).
//
// 12 px is the same finger slop the long-press rule uses (ui/long_press.h kMovePx), measured per
// axis, so a sloppy tap that drifts 11 px is still a tap and 13 px is not.
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host/test_est422.cpp runs the real arithmetic with
// the system g++ and also proves the OLD rule would have tapped in the flick cases.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

constexpr int kTapSlopPx = 12;   // the long-press rule's own finger slop

// Per axis, like LongPress: 11 px off is still a tap, 13 px is not.
constexpr bool withinTapSlop(int ax, int ay, int bx, int by) {
  const int dx = ax - bx;
  const int dy = ay - by;
  return dx <= kTapSlopPx && dx >= -kTapSlopPx && dy <= kTapSlopPx && dy >= -kTapSlopPx;
}

// The gesture's own record: where it began, where it was last seen, and how many times it was seen.
struct TapTrace {
  int pressX = 0;
  int pressY = 0;
  int lastX = 0;
  int lastY = 0;
  int samples = 0;
  // S498 (Filip, 2026-11-13): a gesture seen in ONE sample is a tap again. EST-422 refused it as a
  // possible flick, but a light tap is often seen once too, so keys, Send, the channel selector etc.
  // needed a hard press ("I have to really press it"). Filip's call: the important Settings are now
  // behind the confirm sheet, so a flick that taps is harmless there; fix it everywhere. What stays from
  // EST-422: a gesture SEEN to move (last point or release > 12 px from the press) is never a tap.

  // The press edge. One sample so far.
  void press(int x, int y) {
    pressX = x;
    pressY = y;
    lastX = x;
    lastY = y;
    samples = 1;
  }

  // One later down sample. The last point is what the release edge is compared with.
  void sample(int x, int y) {
    lastX = x;
    lastY = y;
    ++samples;
  }

  // The finger lifted at (upX, upY): the last point the panel reported (the loop carries it into
  // the read, because LovyanGFX writes no coordinate when the panel reports nothing).
  // `drag` is the loop's own long-standing drag latch, `acted` a hold that already fired.
  bool tapAllowed(int upX, int upY, bool drag, bool acted) const {
    if (drag || acted) return false;              // a scroll, or a hold that already acted
    if (!withinTapSlop(pressX, pressY, lastX, lastY)) return false;
    return withinTapSlop(pressX, pressY, upX, upY);
  }

  void reset() { samples = 0; }
};

// The rule this replaced, kept ONLY so the host test can prove the old one fails on the flick cases
// (and so the ticket's "armed against the current rule" is visible in one line).
constexpr bool tapAllowedLegacy(bool drag, bool acted) { return !drag && !acted; }

}  // namespace ui
}  // namespace om
