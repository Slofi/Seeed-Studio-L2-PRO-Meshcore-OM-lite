// SPDX-License-Identifier: MIT
// Non-blocking backlight fade.
//
// The L2 Pro panel's backlight is an I2C LED driver at 0x2C. The tree does not name the part; the
// scheme the vendor init uses is a 4-channel LED driver with one 8-bit PWM register per channel
// (0x18..0x1B) and an update latch (0x0F <- 0x55). Its PWM is 8-bit, so the real brightness range
// is 0..255 (256 duty levels). boardSetBrightness() writes exactly one of those levels.
//
// This is a small time-based ramp: tick() is called from loop(), writes at most one level per call
// (one I2C transaction, never a busy wait), and returns -1 when nothing changed. The fade-in uses an
// ease-out cubic (fast at first, settling into the target); the fade-out is linear. A new start()
// while a ramp is running takes the CURRENT level as its start, so a wake during a fade-out reverses
// without a jump.
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host compiles it with the system g++.
#pragma once

#include <stdint.h>

namespace om {

class FadeRamp {
 public:
  static constexpr int kMinLevel = 0;
  static constexpr int kMaxLevel = 255;

  // Start a ramp from `from` to `to` over durationMs. from/to are clamped into 0..255.
  void start(uint32_t nowMs, int from, int to, uint32_t durationMs, bool easeOut);

  // Call regularly. Returns the level to write, or -1 when there is nothing new to write.
  int tick(uint32_t nowMs);

  // Forget any ramp and set the level (used after boardBegin's first brightness write).
  void reset(int level);

  bool active() const { return active_; }
  int level() const { return level_; }
  int target() const { return to_; }
  int steps() const { return steps_; }

  // The eased position (0..1000) for elapsed `t` ms of `duration` ms. Exposed so the host test can
  // prove the curve is monotonic and starts at 0 / ends at 1000.
  static int easedPermille(uint32_t elapsedMs, uint32_t durationMs, bool easeOut);

 private:
  uint32_t startMs_ = 0;
  uint32_t durationMs_ = 0;
  int from_ = 0;
  int to_ = 0;
  int level_ = 0;
  bool easeOut_ = true;
  bool active_ = false;
  int steps_ = 0;
};

}  // namespace om
