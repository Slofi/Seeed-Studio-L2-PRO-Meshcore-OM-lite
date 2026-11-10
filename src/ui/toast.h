// SPDX-License-Identifier: MIT
// The one-line toast's state: which text, until when, and one expiry signal (EST-344).
//
// Why the state is separate from the drawing: the old showToast() opened a frame, drew the toast
// on top of the panel and pushed once. The next redraw (the Nodes screen every second, the clock
// every minute, any mesh event) painted the whole screen again and the toast vanished after a few
// milliseconds. The toast is now part of every frame: the UI draws the ACTIVE toast last, just
// before the push, and the loop asks expire() for the one redraw that clears it.
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host proves active/expired/replaced with g++.
#pragma once

#include <stdint.h>
#include <stdio.h>

namespace om {
namespace ui {

constexpr int kToastTextCap = 64;

struct ToastState {
  char text[kToastTextCap] = {0};
  uint32_t until = 0;   // millis() deadline, wrap-safe via int32 difference
  uint32_t serial = 0;  // changes on show and on expiry (a future animator can watch it)

  // A toast exists until it is expired or replaced; the text is the whole "active" test.
  bool active() const { return text[0] != 0; }

  // Drawn right now? At `until` exactly the toast is over, so the frame after it is clean.
  bool visible(uint32_t nowMs) const {
    return text[0] != 0 && (int32_t)(nowMs - until) < 0;
  }

  // Show `text` for `durationMs`. A newer toast always replaces an older one, text and deadline
  // both (the new-node notice must not be held to the ping's remaining time). Empty text and a
  // zero duration are refused so a caller cannot create an invisible-but-active state.
  bool show(const char* t, uint32_t nowMs, uint32_t durationMs) {
    if (t == nullptr || t[0] == 0 || durationMs == 0) return false;
    snprintf(text, sizeof(text), "%s", t);
    until = nowMs + durationMs;
    ++serial;
    return true;
  }

  // The one expiry event: true exactly once, when a toast that existed has run out. The caller
  // redraws the current screen once; that frame no longer carries the toast.
  bool expire(uint32_t nowMs) {
    if (text[0] == 0) return false;
    if ((int32_t)(nowMs - until) < 0) return false;
    text[0] = 0;
    until = 0;
    ++serial;
    return true;
  }

  void clear() {
    text[0] = 0;
    until = 0;
    ++serial;
  }
};

// One state per program, reachable from the screen draws (which close their own frames) and from
// the loop (which expires it), without either owning a copy.
inline ToastState& toastState() {
  static ToastState state;
  return state;
}

}  // namespace ui
}  // namespace om
