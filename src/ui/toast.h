// SPDX-License-Identifier: MIT
// The toast's state: what it says, until when, and one expiry signal (EST-344).
//
// Why the state is separate from the drawing: the old showToast() opened a frame, drew the toast
// on top of the panel and pushed once. The next redraw (the Nodes screen every second, the clock
// every minute, any mesh event) painted the whole screen again and the toast vanished after a few
// milliseconds. The toast is now part of every frame: the UI draws the ACTIVE toast last, just
// before the push, and the loop asks expire() for the one redraw that clears it.
//
// EST-368: a one-line notice (a ping, a refusal, the radio fault) is exactly what it was. A MESSAGE
// notice carries a second field, `body`, and is drawn as a growing panel: the title line (who and
// where) then the text wrapped over up to four rows (ui/notice_panel.h owns that arithmetic). The
// 5 s deadline and the consumed-tap dismissal are the same for both (EST-346).
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host proves active/expired/replaced with g++.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ui/notice_panel.h"

namespace om {
namespace ui {

constexpr int kToastTextCap = 64;

struct ToastState {
  // `text` is the one-line notice, or a message notice's first (mono) line.
  char text[kToastTextCap] = {0};
  // EST-368: the message's own text. Empty for a plain one-line notice.
  char body[kNoticeBodyCap] = {0};
  uint32_t until = 0;   // millis() deadline, wrap-safe via int32 difference
  uint32_t serial = 0;  // changes on show and on expiry (a future animator can watch it)
  // EST-381: how many frames have actually PAINTED this toast (ui/widgets.cpp's overlay bumps it
  // after it draws). The notification module logs one line when it goes up, so the device's serial
  // says whether the toast reached a composed, pushed frame at all. That is the one fact the Map
  // bug could not be read out of the source.
  uint32_t paints = 0;

  // A toast exists until it is expired or replaced; the title is the whole "active" test.
  bool active() const { return text[0] != 0; }
  bool hasBody() const { return body[0] != 0; }

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
    body[0] = 0;
    until = nowMs + durationMs;
    ++serial;
    return true;
  }

  // EST-368: a message pop-up. The title is required (it says who and where); the body may be
  // empty, in which case this is a one-line notice with a title.
  bool showNotice(const char* t, const char* b, uint32_t nowMs, uint32_t durationMs) {
    if (t == nullptr || t[0] == 0 || durationMs == 0) return false;
    snprintf(text, sizeof(text), "%s", t);
    if (b != nullptr) {
      snprintf(body, sizeof(body), "%s", b);
    } else {
      body[0] = 0;
    }
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
    body[0] = 0;
    until = 0;
    ++serial;
    return true;
  }

  void clear() {
    text[0] = 0;
    body[0] = 0;
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

// EST-346, Filip: any touch while a toast shows dismisses it. The rule is the one EST-339 already
// applies to a press away from an overlay: that press is CONSUMED, so its release and any drag of it
// do nothing else, and only the NEXT new press may act. Called on the press edge only.
// Returns true when this press dismissed a toast and the caller must spend the whole gesture.
inline bool toastDismissOnPress(ToastState& t, uint32_t nowMs) {
  if (!t.visible(nowMs)) return false;   // no toast, or one whose time ran out: the touch is free
  t.clear();
  return true;
}

}  // namespace ui
}  // namespace om
