// SPDX-License-Identifier: MIT
#include "fade.h"

#include <math.h>

namespace om {
namespace {

int clampLevel(int v) {
  if (v < FadeRamp::kMinLevel) return FadeRamp::kMinLevel;
  if (v > FadeRamp::kMaxLevel) return FadeRamp::kMaxLevel;
  return v;
}

}  // namespace

int FadeRamp::levelAt(int from, int to, uint32_t elapsedMs, uint32_t durationMs) {
  from = clampLevel(from);
  to = clampLevel(to);
  if (durationMs == 0 || elapsedMs >= durationMs) return to;
  const double p = (double)elapsedMs / (double)durationMs;   // 0..1 through the fade
  double duty = 0.0;
  if (to >= from) {
    // Fade up: the perceived brightness rises linearly, the duty follows p^2.2.
    duty = (double)from + (double)(to - from) * pow(p, kGamma);
  } else {
    // Fade down: the same curve played backwards, so the fade looks as smooth as the fade-in.
    duty = (double)to + (double)(from - to) * pow(1.0 - p, kGamma);
  }
  return clampLevel((int)(duty + 0.5));
}

void FadeRamp::start(uint32_t nowMs, int from, int to, uint32_t durationMs) {
  from_ = clampLevel(from);
  to_ = clampLevel(to);
  level_ = from_;
  startMs_ = nowMs;
  durationMs_ = durationMs;
  steps_ = 0;
  active_ = (from_ != to_);
}

int FadeRamp::tick(uint32_t nowMs) {
  if (!active_) return -1;
  const uint32_t elapsed = (uint32_t)(nowMs - startMs_);
  int value = levelAt(from_, to_, elapsed, durationMs_);
  const bool done = (durationMs_ == 0) || (elapsed >= durationMs_);
  if (done) {
    value = to_;
    active_ = false;
  }
  if (value == level_) return -1;
  level_ = value;
  ++steps_;
  return level_;
}

void FadeRamp::reset(int level) {
  level_ = clampLevel(level);
  from_ = level_;
  to_ = level_;
  startMs_ = 0;
  durationMs_ = 0;
  active_ = false;
  steps_ = 0;
}

}  // namespace om
