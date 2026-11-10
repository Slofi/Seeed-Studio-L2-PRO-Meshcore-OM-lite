// SPDX-License-Identifier: MIT
// EST-371: the toast and notification module. ui/toast.h owns the text and the 5 s deadline;
// ui/notify.h owns the DND/open-chat/burst decisions. This file is the one place that holds the
// live queue and draws through the shell's redraw callback.
#include "screens/toasts.h"

#include <Arduino.h>

#include "ui/toast.h"

namespace om {
namespace toasts {
namespace {

void (*s_redraw)() = nullptr;
bool s_screenOn = true;
ui::NoticeQueue s_queue;

void redraw() {
  if (s_redraw != nullptr) s_redraw();
}

}  // namespace

void begin(void (*redrawFn)()) { s_redraw = redrawFn; }

void setScreenOn(bool on) { s_screenOn = on; }

void text(const char* text) {
  if (!ui::toastState().show(text, millis(), ui::kToastMs)) return;
  redraw();
}

void notify(ui::NoticeKind kind, const char* text, bool chatOpen, bool dnd) {
  if (text == nullptr || text[0] == 0) return;
  if (!s_screenOn) return;
  if (!ui::noticeWanted(kind, dnd, chatOpen)) return;
  if (ui::toastState().visible(millis())) {
    s_queue.push(text);   // newest wins; the current toast finishes first
    return;
  }
  toasts::text(text);
}

// EST-368: a MESSAGE notice. The title says who and where, the body is the message, and the pop-up
// draws it wrapped over up to four rows (ui/notice_panel.h) instead of one cut line. Everything
// else, the DND rule, the open-chat rule, the one pending slot and the 5 s deadline, is the same.
void notifyNotice(ui::NoticeKind kind, const ui::Notice& n, bool chatOpen, bool dnd) {
  if (n.title[0] == 0) return;
  if (!s_screenOn) return;
  if (!ui::noticeWanted(kind, dnd, chatOpen)) return;
  if (ui::toastState().visible(millis())) {
    s_queue.pushNotice(n);
    // A real redraw so a queued notice is not silently held by a frame that is already dirty.
    return;
  }
  if (ui::toastState().showNotice(n.title, n.body, millis(), ui::kToastMs)) redraw();
}

void pump(bool screenOn) {
  // Every toast (fault, new node, ping, trace) expires on one rule: clear it, then redraw once so
  // the frame is pushed without it. expire() answers true exactly once (EST-344).
  if (ui::toastState().expire(millis())) {
    if (screenOn) redraw();
  }
  // EST-366 (rule 9): a burst leaves ONE newest notification waiting. The moment no toast is on
  // screen (expired, or dismissed by the consumed tap), that one takes the screen. This is also what
  // makes the tap's dismissal enough: no second tap is needed when nothing is pending.
  if (!ui::toastState().visible(millis()) && s_queue.hasPending) {
    // EST-368: a queued MESSAGE keeps its body, so the pop-up that follows is readable too.
    ui::Notice next;
    if (s_queue.takeNotice(&next)) {
      if (screenOn &&
          ui::toastState().showNotice(next.title, next.body, millis(), ui::kToastMs)) {
        redraw();
      }
    }
  }
}

}  // namespace toasts
}  // namespace om
