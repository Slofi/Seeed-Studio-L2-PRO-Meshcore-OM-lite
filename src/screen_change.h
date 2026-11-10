// SPDX-License-Identifier: MIT
// The screen-change backlight transition (EST-355): fade out -> push -> settle -> fade in.
//
// Why this exists: the panel scans the RAM it is being written to, so every full-frame push leaves a
// diagonal seam (board.h, EST-353). Blanking the display output (DISPOFF/DISPON) did not hide it on
// this panel. What cannot depend on the panel's timing is taking the LIGHT away for the push: with
// the backlight at 0 nothing the panel scans while the frame is being written can be seen, and the
// light comes back after one full panel refresh.
//
// The rule from the ticket, and from Filip ("a short pause to give the page time to load before
// switching"): only a change whose old and new frames differ strongly in brightness gets the dip
// (frame_metrics.h decides); an ordinary switch keeps its one push and no blink. In-page updates (a
// new message line, a clock tick, a pan, a key) never go through here at all.
//
// The machine is non-blocking by construction: it holds no frame, waits on no panel, and its tick()
// only returns an ACTION for the caller (Push = compose the target screen and flush it now; Done =
// the light is up again and the change is over). The caller writes the levels, so the I2C side and
// the frame stay outside; the loop keeps servicing the mesh between every step.
//
// A second change requested while this one is still dark COALESCES into the running transition: the
// frame that is pushed is the LATEST target, and there is still exactly one push and one dip.
// Plain C++ (no Arduino, no LovyanGFX), so test/host runs the real machine with the system g++.
#pragma once

#include <stdint.h>

#include "fade.h"

namespace om {

class ScreenChange {
 public:
  enum class Phase : uint8_t { Idle = 0, FadeOut, Push, Settle, FadeIn };
  enum class Action : uint8_t { None = 0, Push, Done };

  // A change to a screen whose last frame is NOT this one was asked for at nowMs, with the light at
  // fromLevel (0..255); it comes back to toLevel. dip=true fades out FIRST; dip=false asks for the
  // push at once (an instant switch, no blink). Returns true when this began (or restarted) a
  // transition and false when it coalesced into the one already running. A change requested after
  // the push (Settle/FadeIn) cannot be merged into it: the first frame is already on the glass, so
  // it is a NEW transition, started from the level the light is at now.
  bool request(uint32_t nowMs, bool dip, int fromLevel, int toLevel);

  // Advance the transition. May queue a level for the caller (takeLevel()). Action::Push is returned
  // exactly once per transition, when it is time to compose and push; Action::Done when the light has
  // reached its top again. Everything else is Action::None.
  Action tick(uint32_t nowMs);

  // The caller composed the frame and pushed it (one push). `pushUs` is the measured duration and
  // `darkNow` says the light was at 0 for that push (the dip, or a late hard cut the caller decided
  // on after measuring the composed frame). Returns true when the change is over at once, which is
  // the no-dip case: the light never moved, so there is no settle and no fade-in.
  bool pushed(uint32_t nowMs, uint32_t pushUs, bool darkNow);

  // -1, or the brightness to write now (0..255). Clears the pending value.
  int takeLevel();

  // The level the light is at as far as this transition knows (the ramp's own value). Used when the
  // screen goes off in the middle of a change, so the sleep fade starts from what is really on.
  int level() const { return ramp_.level(); }

  bool active() const { return phase_ != Phase::Idle; }
  Phase phase() const { return phase_; }
  // The light is at 0 until the fade-in: the frame on the panel must not be disturbed.
  bool dark() const { return phase_ == Phase::FadeOut || phase_ == Phase::Push || phase_ == Phase::Settle; }

  // The screen went off: forget the transition (the caller owns the light again).
  void cancel();

  // The measured parts of this change, for the one log line and for the tests.
  uint32_t outMs() const { return outMs_; }
  uint32_t pushUs() const { return pushUs_; }
  uint32_t settleMs() const { return settleMs_; }
  uint32_t inMs() const { return inMs_; }
  // The window the caller must not read touches in: what the transition is planned to take. The
  // caller masks touches while active() anyway; this is the number to hand the touch gate.
  static uint32_t plannedMs();
  // True when the last change actually moved the light (a dip or a late cut), false for an instant
  // switch. The tests and the log use it.
  bool dipped() const { return dipped_; }

 private:
  void startFadeOut(uint32_t nowMs, int fromLevel);
  void startFadeIn(uint32_t nowMs);
  void finish();

  Phase phase_ = Phase::Idle;
  FadeRamp ramp_;
  int from_ = 0;          // the level the fade-out starts at (and what the fade-in returns to)
  int to_ = 0;            // the level the light returns to
  int pendingLevel_ = -1; // the duty to write, -1 for nothing
  bool pushAsked_ = false;
  bool dipped_ = false;
  uint32_t startMs_ = 0;     // when the change began (for outMs)
  uint32_t pushEndMs_ = 0;   // when the push ended (for settleMs)
  uint32_t fadeInMs_ = 0;    // when the fade-in began (for inMs)
  uint32_t outMs_ = 0;
  uint32_t pushUs_ = 0;
  uint32_t settleMs_ = 0;
  uint32_t inMs_ = 0;
};

}  // namespace om
