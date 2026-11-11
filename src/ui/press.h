// SPDX-License-Identifier: MIT
// EST-390: touch feedback, as ONE rule. The L2 has no vibration motor, so the pressed look is what
// tells the finger that the app saw it: on the press edge the element under the finger gets the
// pressed tint, the tint stays while the finger is down, and it goes away on the release (the action
// then runs exactly as before) or when the finger slides off the element's own rectangle.
//
// This header is the whole rule and it is plain C++ (no Arduino, no LovyanGFX), so test/host runs
// the real arithmetic with the system g++:
//   * PressTarget carries the rectangle the press is drawn on. It is ALWAYS the rectangle the hit
//     test returned (press_targets.h asks the same hit test the tap path asks), never a second
//     measurement, so the pressed look can never sit somewhere the tap would not land.
//   * PressTracker is the one state machine: arm on the press edge, clear on slide-off or release.
//     A consumed press (a tap that only closed a pop-up or a keyboard) is never armed, so nothing
//     under it is drawn pressed.
//   * pressBlend + kPressedAlpha are the one look: the accent mixed into whatever the element
//     already drew (an accent-tinted fill, the same everywhere, one theme constant).
//   * pressBubbleRect is the key preview's placement, derived from the accent bubble's own rules
//     (EST-346/352): centred on the key, above it, clamped to the screen, and it never covers the
//     composer's draft line (the text being typed).
//   * PressLatencyLog keeps the worst press-to-pixel time and hands it out once per minute.
//
// The feedback is visual only. It changes nothing about what a tap DOES: pressTracker.move() clears
// the LOOK when the finger leaves the rectangle, and the shell's press/drag/release logic that
// decides the action is untouched (Filip's ticket: "Nothing changes in what a tap DOES").
#pragma once

#include <stdint.h>

#include "layout.h"
#include "theme.h"

namespace om {
namespace ui {

// What is being pressed. Only Key and Emoji carry a character, so only they get the preview bubble.
enum class PressKind : uint8_t {
  None = 0,
  Button,   // a key in a header, the gear, a sheet/action button
  Tab,      // a tab: the bottom bar, the settings tab row, a map edge tab, the chip
  Row,      // a list/settings/selector row or a message block
  Key,      // a keyboard key (pressed look + the character preview bubble)
  Emoji,    // an emoji cell (pressed look only: its glyph is already the size of the key)
};

struct PressTarget {
  PressKind kind = PressKind::None;
  Rect rect;                 // EXACTLY the rectangle the hit test uses
  uint32_t cp = 0;           // Key: the character the preview bubble shows (0 = none)
  Rect bubbleKey;            // Key: the DRAWN key the bubble is centred on (empty = rect)
  int16_t bubbleTopLimit = 0;   // Key: the draft line's top y (0 = no limit)

  bool valid() const { return kind != PressKind::None && !rect.empty(); }
};

// The one pressed look: the accent at kPressedAlpha over the pixels the element already drew.
// 0 returns the background unchanged, 255 returns the accent, so a caller can prove both ends.
constexpr uint16_t pressBlend(uint16_t fg, uint16_t bg, uint8_t a) {
  if (a == 0) return bg;
  if (a == 255) return fg;
  const uint32_t fr = (fg >> 11) & 0x1F, fg6 = (fg >> 5) & 0x3F, fb = fg & 0x1F;
  const uint32_t br = (bg >> 11) & 0x1F, bg6 = (bg >> 5) & 0x3F, bb = bg & 0x1F;
  const uint32_t inv = 255U - a;
  const uint32_t r = (fr * a + br * inv + 127U) / 255U;
  const uint32_t g = (fg6 * a + bg6 * inv + 127U) / 255U;
  const uint32_t b = (fb * a + bb * inv + 127U) / 255U;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// The key preview bubble's rectangle. Same box, gap, rounding and font as the accent bubble
// (drawAccentBubble, EST-346/352): centred on the key, its bottom kAccentBubbleGap above the key's
// top, clamped 2 px inside the screen and below the status bar.
//
// `draftTop` is the composer's draft line (the text being typed) or 0 when there is none. The bubble
// must never cover it: if the key sits directly under the draft (the top key row), the bubble moves
// up to clear it instead of covering the typed text. For a lower row this changes nothing, because
// the accent bubble already sits in that gap.
constexpr Rect pressBubbleRect(const Rect& key, int16_t draftTop) {
  int16_t x = (int16_t)(key.x + key.w / 2 - kAccentBubbleW / 2);
  int16_t bottom = (int16_t)(key.y - kAccentBubbleGap);
  if (draftTop > 0) {
    const int16_t limit = (int16_t)(draftTop - kAccentBubbleGap);
    if (bottom > limit) bottom = limit;
  }
  int16_t y = (int16_t)(bottom - kAccentBubbleH);
  if (x < 2) x = 2;
  if (x > kScreenW - 2 - kAccentBubbleW) x = (int16_t)(kScreenW - 2 - kAccentBubbleW);
  if (y < kStatusH + 2) y = (int16_t)(kStatusH + 2);
  return Rect{x, y, kAccentBubbleW, kAccentBubbleH};
}

// The pressed state, one instance app-wide (ui/press_feedback.cpp owns it).
class PressTracker {
 public:
  // A new press on `t`. An invalid target (nothing actionable under the finger) clears instead.
  void arm(const PressTarget& t, uint32_t nowMs) {
    t_ = t;
    active_ = t.valid();
    armedMs_ = nowMs;
  }

  // One sample while the finger is down. Returns true when the look was cleared by this sample
  // (the finger slid off the target's rectangle); false when it was already clear or is still on.
  bool move(int x, int y) {
    if (!active_) return false;
    if (t_.rect.contains(x, y)) return false;
    clear();
    return true;
  }

  // EST-390: the key preview bubble is dropped but the pressed look stays, when another preview owns
  // that spot (the long-press accent bubble is drawn at the same place, EST-346/352).
  void dropPreview() { t_.cp = 0; }

  void clear() {
    active_ = false;
    t_ = PressTarget{};
  }

  bool active() const { return active_; }
  const PressTarget& target() const { return t_; }
  uint32_t armedMs() const { return armedMs_; }

 private:
  PressTarget t_;
  bool active_ = false;
  uint32_t armedMs_ = 0;
};

// The worst press-to-pixels time, printed once a minute like the other timings. `sample()` is fed
// the measured µs for each armed press; `tick()` is called every loop and answers true exactly once
// when a 60 s window ended with samples in it (the caller logs *worstUs and the count).
class PressLatencyLog {
 public:
  static constexpr uint32_t kWindowMs = 60000;

  void sample(uint32_t us) {
    if (us > worstUs_) worstUs_ = us;
    ++count_;
    any_ = true;
  }

  bool tick(uint32_t nowMs, uint32_t* worstUs, uint32_t* count) {
    if (!started_) {
      started_ = true;
      windowMs_ = nowMs;
      return false;
    }
    if ((uint32_t)(nowMs - windowMs_) < kWindowMs) return false;
    windowMs_ = nowMs;
    if (!any_) return false;
    if (worstUs != nullptr) *worstUs = worstUs_;
    if (count != nullptr) *count = count_;
    worstUs_ = 0;
    count_ = 0;
    any_ = false;
    return true;
  }

 private:
  uint32_t windowMs_ = 0;
  uint32_t worstUs_ = 0;
  uint32_t count_ = 0;
  bool any_ = false;
  bool started_ = false;
};

}  // namespace ui
}  // namespace om
