// SPDX-License-Identifier: MIT
// The screen-change backlight transition. See screen_change.h for why it exists and what it promises.
#include "screen_change.h"

#include "ui/theme.h"

namespace om {
namespace {

int clampLevel(int v) {
  if (v < FadeRamp::kMinLevel) return FadeRamp::kMinLevel;
  if (v > FadeRamp::kMaxLevel) return FadeRamp::kMaxLevel;
  return v;
}

}  // namespace

uint32_t ScreenChange::plannedMs() {
  return ui::kChangeOutMs + ui::kChangeSettleMs + ui::kChangeInMs;
}

void ScreenChange::startFadeOut(uint32_t nowMs, int fromLevel) {
  from_ = clampLevel(fromLevel);
  phase_ = Phase::FadeOut;
  pushAsked_ = false;
  // The light is already at from_: no level is written here, the ramp's own ticks move it down.
  pendingLevel_ = -1;
  ramp_.start(nowMs, from_, 0, ui::kChangeOutMs);
}

void ScreenChange::startFadeIn(uint32_t nowMs) {
  phase_ = Phase::FadeIn;
  fadeInMs_ = nowMs;
  pendingLevel_ = -1;
  ramp_.start(nowMs, 0, to_, ui::kChangeInMs);
}

void ScreenChange::finish() {
  phase_ = Phase::Idle;
  pushAsked_ = false;
  ramp_.reset(to_);
  // The fade-in's last level (the screen brightness) stays queued: it is the level the light must be
  // left at, and takeLevel() hands it to the caller on the same pass as Action::Done.
}

bool ScreenChange::request(uint32_t nowMs, bool dip, int fromLevel, int toLevel) {
  to_ = clampLevel(toLevel);

  if (phase_ == Phase::Idle) {
    from_ = clampLevel(fromLevel);
    dipped_ = false;
    outMs_ = 0;
    pushUs_ = 0;
    settleMs_ = 0;
    inMs_ = 0;
    pushEndMs_ = 0;
    fadeInMs_ = 0;
    startMs_ = nowMs;
    pendingLevel_ = -1;   // a level left over from an earlier change is never written again
    if (dip) {
      startFadeOut(nowMs, from_);
    } else {
      phase_ = Phase::Push;
      pushAsked_ = false;
    }
    return true;
  }

  // Still dark: the light is on its way down (or already down) and the frame has not been composed.
  // Only the destination changes; one push, one dip. A dip the new pair needs can still start here,
  // because nothing has been written yet.
  if (phase_ == Phase::FadeOut) return false;
  if (phase_ == Phase::Push && !pushAsked_) {
    if (dip && !dipped_) startFadeOut(nowMs, ramp_.level());
    return false;
  }

  // The frame is already on the glass (Settle / FadeIn) or being written: a new screen cannot be
  // merged into it. Start a fresh transition from the light level the panel is at now.
  const int atNow = ramp_.level();
  from_ = clampLevel(atNow);
  dipped_ = false;
  outMs_ = 0;
  pushUs_ = 0;
  settleMs_ = 0;
  inMs_ = 0;
  startMs_ = nowMs;
  pendingLevel_ = -1;
  if (dip) {
    startFadeOut(nowMs, from_);
  } else {
    phase_ = Phase::Push;
    pushAsked_ = false;
  }
  return true;
}

ScreenChange::Action ScreenChange::tick(uint32_t nowMs) {
  if (phase_ == Phase::FadeOut) {
    const int level = ramp_.tick(nowMs);
    if (level >= 0) pendingLevel_ = level;
    if (ramp_.active()) return Action::None;
    // The light is out: the frame may be composed and pushed now. Write 0 first even if the ramp had
    // nothing left to say (a zero-length fade), so no pixel is ever pushed into a lit panel.
    outMs_ = nowMs - startMs_;
    phase_ = Phase::Push;
    dipped_ = true;
    pendingLevel_ = 0;
  }

  if (phase_ == Phase::Push) {
    if (pushAsked_) return Action::None;   // the caller is already composing/pushing (one shot)
    pushAsked_ = true;
    return Action::Push;
  }

  if (phase_ == Phase::Settle) {
    // One full panel refresh after the push ended, then the light may come up. The push's own end is
    // reported by pushed(), so this counts from there whatever the caller's push cost.
    if ((uint32_t)(nowMs - pushEndMs_) < ui::kChangeSettleMs) return Action::None;
    settleMs_ = nowMs - pushEndMs_;
    startFadeIn(nowMs);
    return Action::None;   // the fade-in's first level lands on the next pass (1 ms, time-based)
  }

  if (phase_ == Phase::FadeIn) {
    const int level = ramp_.tick(nowMs);
    if (level >= 0) pendingLevel_ = level;
    if (ramp_.active()) return Action::None;
    inMs_ = nowMs - fadeInMs_;
    finish();
    return Action::Done;
  }

  return Action::None;
}

bool ScreenChange::pushed(uint32_t nowMs, uint32_t pushUs, bool darkNow) {
  pushUs_ = pushUs;
  pushAsked_ = true;
  if (!darkNow) {
    // An instant switch: the light never moved, so there is nothing to settle and nothing to fade.
    // (A dip that never happened must not report durations it did not spend.)
    outMs_ = 0;
    settleMs_ = 0;
    inMs_ = 0;
    dipped_ = false;
    finish();
    return true;
  }
  // A dip the caller decided on AFTER measuring the composed frame (a bright page arriving from a
  // dark one) was a hard cut, not a fade: say so in the numbers instead of inventing a fade time.
  if (!dipped_) outMs_ = 0;
  dipped_ = true;
  pushEndMs_ = nowMs;
  phase_ = Phase::Settle;
  return false;
}

int ScreenChange::takeLevel() {
  const int level = pendingLevel_;
  pendingLevel_ = -1;
  return level;
}

void ScreenChange::cancel() {
  phase_ = Phase::Idle;
  pendingLevel_ = -1;
  pushAsked_ = false;
  ramp_.reset(to_);
}

}  // namespace om
