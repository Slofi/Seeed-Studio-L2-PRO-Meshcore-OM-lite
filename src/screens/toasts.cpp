// SPDX-License-Identifier: MIT
// EST-371: the toast and notification module. ui/toast.h owns the text and the 5 s deadline;
// ui/notify.h owns the DND/open-chat/burst decisions and the one `[notice]` line. This file is the
// one place that holds the live queue and draws through the shell's redraw callback.
//
// EST-381: the Map's missing pop-up could not be read out of the source (the door, the overlay and
// every map draw are shared), so this module now (a) logs every decision, (b) treats the redraw as
// something that can be REFUSED and retries it until a frame is really drawn, and (c) reports the
// first frame that painted the toast. Those three lines are what the device will answer with.
#include "screens/toasts.h"

#include <Arduino.h>

#include "ui/toast.h"

namespace om {
namespace toasts {
namespace {

bool (*s_redraw)() = nullptr;
bool s_screenOn = true;
ui::NoticeQueue s_queue;

// EST-381: the notification is on screen but no frame has carried it yet. Set by a show, cleared by
// a redraw that really drew. A screen change owns the frame for ~300 ms and redrawCurrentScreen()
// refuses during it; without this flag that one refusal lost the pop-up for its whole 5 s.
bool s_needFrame = false;
bool s_deferLogged = false;

// The paint report: `paints` is bumped by ui/widgets.cpp's overlay, so a rise proves the toast
// reached a composed frame. One line per toast, from the loop pass, never from the overlay.
uint32_t s_paintBase = 0;
uint32_t s_shownMs = 0;
bool s_paintReported = true;

bool draw() {
  if (s_redraw == nullptr) return false;
  return s_redraw();
}

// The one decision line. Built by ui::noticeLogLine so the words are host-tested, printed here so
// there is exactly one place that prints them.
void logDecision(const char* kindToken, const ui::NoticeDecision& d) {
  char line[96];
  if (ui::noticeLogLine(line, sizeof(line), kindToken, d) > 0) Serial.println(line);
}

// Ask the shell for one frame with the toast in it. A refusal is not an error: the screen change
// machine is composing the same screen and its own frame carries the overlay (omFrameEnd draws the
// toast on every outer frame). We ask again on the next pass anyway, so no refusal can leave the
// glass without the pop-up.
void requestFrame() {
  s_needFrame = true;
  if (draw()) {
    s_needFrame = false;
    s_deferLogged = false;
  } else if (!s_deferLogged) {
    s_deferLogged = true;
    Serial.println("[notice] redraw deferred: the screen change owns the frame, retrying");
  }
}

void markShown() {
  s_paintBase = ui::toastState().paints;
  s_shownMs = millis();
  s_paintReported = false;
}

// The shared body of every door: decide, log, then act. `body` may be nullptr (a one-line notice).
//
// `queueWhenBusy` is the one difference between the two doors, and it is deliberate: a NOTIFICATION
// waits for the toast that is up (the newest pending wins, EST-366's rule 9), while the shell's own
// one-line call (`text`: "Your position", "Sound saved", a ping) means "say this now" and replaces
// it, exactly as it did before this ticket. Neither asks for a frame when it only queued.
ui::NoticeDelivery deliver(const char* kindToken, ui::NoticeKind kind, const char* title,
                           const char* body, bool chatOpen, bool dnd, const char** reason,
                           bool queueWhenBusy) {
  const bool busy = queueWhenBusy && ui::toastState().visible(millis());
  const ui::NoticeDecision dec = ui::noticeDecide(kind, title, s_screenOn, dnd, chatOpen, busy);
  logDecision(kindToken, dec);
  if (reason != nullptr) *reason = dec.reason;
  switch (dec.delivery) {
    case ui::NoticeDelivery::Skipped:
      return dec.delivery;
    case ui::NoticeDelivery::Queued: {
      ui::Notice n;
      ui::noticeCopy(n.title, sizeof(n.title), title);
      ui::noticeCopy(n.body, sizeof(n.body), body);
      if (!s_queue.pushNotice(kind, n)) return ui::NoticeDelivery::Skipped;
      // The queued notice is not drawn yet (the current toast owns the glass); pump() shows it the
      // moment the current one ends. No frame is needed for this one now.
      return dec.delivery;
    }
    case ui::NoticeDelivery::Shown:
    default:
      if (!ui::toastState().showNotice(title, body, millis(), ui::kToastMs)) {
        return ui::NoticeDelivery::Skipped;
      }
      markShown();
      requestFrame();
      return ui::NoticeDelivery::Shown;
  }
}

}  // namespace

void begin(bool (*redrawFn)()) { s_redraw = redrawFn; }

void setScreenOn(bool on) { s_screenOn = on; }

ui::NoticeDelivery text(const char* line) {
  // The shell's short line: no DND rule (it was always shown), and it replaces the toast on screen.
  return deliver("text", ui::NoticeKind::Channel, line, nullptr, false, false, nullptr, false);
}

ui::NoticeDelivery notify(ui::NoticeKind kind, const char* line, bool chatOpen, bool dnd,
                          const char** reason) {
  return deliver(ui::noticeKindToken(kind), kind, line, nullptr, chatOpen, dnd, reason, true);
}

// EST-368: a MESSAGE notice. The title says who and where, the body is the message, and the pop-up
// draws it wrapped over up to four rows (ui/notice_panel.h) instead of one cut line. Everything
// else, the DND rule, the open-chat rule, the one pending slot and the 5 s deadline, is the same.
ui::NoticeDelivery notifyNotice(ui::NoticeKind kind, const ui::Notice& n, bool chatOpen, bool dnd,
                                const char** reason) {
  return deliver(ui::noticeKindToken(kind), kind, n.title, n.body, chatOpen, dnd, reason, true);
}

void pump(bool screenOn) {
  ui::ToastState& t = ui::toastState();
  // Every toast (fault, new node, ping, trace) expires on one rule: clear it, then redraw once so
  // the frame is pushed without it. expire() answers true exactly once (EST-344).
  if (t.expire(millis())) {
    s_needFrame = false;
    s_deferLogged = false;
    if (screenOn) draw();
  }
  // EST-381: the device's proof. One line, the first time a frame painted this toast.
  if (!s_paintReported && t.paints != s_paintBase) {
    s_paintReported = true;
    Serial.printf("[notice] painted after %lu ms (%lu frame(s))\n",
                  (unsigned long)(millis() - s_shownMs), (unsigned long)(t.paints - s_paintBase));
  }
  // EST-381: a frame the shell refused is asked for again here, once per pass, until one is drawn.
  if (s_needFrame && screenOn) {
    if (draw()) {
      s_needFrame = false;
      s_deferLogged = false;
    } else if (!s_deferLogged) {
      s_deferLogged = true;
      Serial.println("[notice] redraw still deferred: the screen change owns the frame");
    }
  }
  // EST-366 (rule 9): a burst leaves ONE newest notification waiting. The moment no toast is on
  // screen (expired, or dismissed by the consumed tap), that one takes the screen. This is also what
  // makes the tap's dismissal enough: no second tap is needed when nothing is pending.
  // EST-381: only while the panel is on, so a notice queued a second before the screen went off is
  // not taken and thrown away in the dark.
  if (screenOn && !t.visible(millis()) && s_queue.hasPending) {
    // EST-368: a queued MESSAGE keeps its body, so the pop-up that follows is readable too.
    ui::Notice next;
    ui::NoticeKind kind = ui::NoticeKind::Channel;
    if (s_queue.takeNotice(&next, &kind)) {
      if (t.showNotice(next.title, next.body, millis(), ui::kToastMs)) {
        // Its own line: the notice that really waited says so, with its own kind.
        char line[96];
        ui::NoticeDecision d;
        d.delivery = ui::NoticeDelivery::Shown;
        d.reason = nullptr;
        if (ui::noticeLogLine(line, sizeof(line), ui::noticeKindToken(kind), d) > 0) {
          Serial.println(line);
        }
        markShown();
        requestFrame();
      }
    }
  }
}

}  // namespace toasts
}  // namespace om
