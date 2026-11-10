// SPDX-License-Identifier: MIT
#include "fade.h"

namespace om {
namespace {

int clampLevel(int v) {
  if (v < FadeRamp::kMinLevel) return FadeRamp::kMinLevel;
  if (v > FadeRamp::kMaxLevel) return FadeRamp::kMaxLevel;
  return v;
}

}  // namespace

int FadeRamp::easedPermille(uint32_t elapsedMs, uint32_t durationMs, bool easeOut) {
  if (durationMs == 0) return 1000;
  uint32_t t = 1000;
  if (elapsedMs < durationMs) {
    t = (uint32_t)(((uint64_t)elapsedMs * 1000ULL) / durationMs);
  }
  if (!easeOut) return (int)t;
  // Ease-out cubic: p = 1 - (1 - t)^3. Fast at the start, settling into the target.
  const uint32_t inv = 1000U - t;
  const uint32_t p = 1000U - (uint32_t)(((uint64_t)inv * inv * inv) / 1000000ULL);
  return (int)p;
}

void FadeRamp::start(uint32_t nowMs, int from, int to, uint32_t durationMs, bool easeOut) {
  from_ = clampLevel(from);
  to_ = clampLevel(to);
  level_ = from_;
  startMs_ = nowMs;
  durationMs_ = durationMs;
  easeOut_ = easeOut;
  steps_ = 0;
  active_ = (from_ != to_);
}

int FadeRamp::tick(uint32_t nowMs) {
  if (!active_) return -1;
  const uint32_t elapsed = (uint32_t)(nowMs - startMs_);
  const int p = easedPermille(elapsed, durationMs_, easeOut_);
  const int64_t delta = (int64_t)(to_ - from_) * (int64_t)p;
  int value = from_ + (int)((delta >= 0 ? delta + 500 : delta - 500) / 1000);
  value = clampLevel(value);
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
  easeOut_ = true;
  active_ = false;
  steps_ = 0;
}

}  // namespace om
