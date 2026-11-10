// SPDX-License-Identifier: MIT
// Non-blocking backlight fade, on a perceptual curve.
//
// The L2 Pro panel's backlight is an I2C LED driver at 0x2C. The tree does not name the part; the
// scheme the vendor init uses is a 4-channel LED driver with one 8-bit PWM register per channel
// (0x18..0x1B) and an update latch (0x0F <- 0x55). Its PWM is 8-bit, so the real brightness range
// is 0..255 (256 duty levels). boardSetBrightness() writes exactly one of those levels.
//
// Why the curve changed (EST-339, Filip: "fade looks nicer on the fading out than on wake up"): a
// linear PWM ramp does not look linear. The eye sees roughly the 2.2 root of the duty, so a linear
// ramp is perceived as a fast jump at the start of a fade-in and as a gentle glide on a fade-out.
// With gamma 2.2 the PERCEIVED brightness moves linearly with time: duty = target * p^2.2, where p
// is how far through the fade we are. The fade-out is the same curve played backwards (mirrored in
// time), so the two directions look alike. The fade-in also gets 300 ms (it was 200) because a
// perceptual ramp spends its first two thirds near the bottom; the fade-out stays at 150 ms.
//
// This is a small time-based ramp: tick() is called from loop(), writes at most one level per call
// (one I2C transaction, never a busy wait), and returns -1 when nothing changed. A new start() while
// a ramp is running takes the CURRENT level as its start, so a wake during a fade-out reverses
// without a jump.
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host compiles it with the system g++.
#pragma once

#include <stdint.h>

namespace om {

constexpr uint32_t kFadeInMs = 300;    // wake: dark to the screen brightness
constexpr uint32_t kFadeOutMs = 150;   // sleep: the screen brightness to dark

class FadeRamp {
 public:
  static constexpr int kMinLevel = 0;
  static constexpr int kMaxLevel = 255;
  static constexpr double kGamma = 2.2;   // the eye's response to the duty cycle

  // Start a ramp from `from` to `to` over durationMs. from/to are clamped into 0..255.
  void start(uint32_t nowMs, int from, int to, uint32_t durationMs);

  // Call regularly. Returns the level to write, or -1 when there is nothing new to write.
  int tick(uint32_t nowMs);

  // Forget any ramp and set the level (used after boardBegin's first brightness write).
  void reset(int level);

  bool active() const { return active_; }
  int level() const { return level_; }
  int target() const { return to_; }
  int steps() const { return steps_; }

  // The duty for a ramp from `from` to `to` at `elapsedMs` of `durationMs`, on the gamma 2.2 curve:
  // fade up `from + (to-from) * p^2.2`, fade down `to + (from-to) * (1-p)^2.2` (the same curve
  // played backwards). Exposed so the host test can prove the curve itself: monotonic, exact at
  // both ends, and still near the bottom after the first fifth of the time.
  static int levelAt(int from, int to, uint32_t elapsedMs, uint32_t durationMs);

 private:
  uint32_t startMs_ = 0;
  uint32_t durationMs_ = 0;
  int from_ = 0;
  int to_ = 0;
  int level_ = 0;
  bool active_ = false;
  int steps_ = 0;
};

}  // namespace om
