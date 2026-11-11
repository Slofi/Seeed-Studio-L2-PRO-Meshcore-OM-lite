// SPDX-License-Identifier: MIT
// EST-371: the toast and notification module. It owns the one toast (the deadline and the
// consumed-tap dismissal stay in ui/toast.h) and the burst queue decided in ui/notify.h, so no
// screen and no arrival path writes that state. main.cpp only calls it and passes the redraw that
// puts the new frame on the glass.
//
// EST-381 (Filip: "on the Map a new message plays the sound but no pop-up"): the decision, the log
// line and the redraw are now one contract, because the Map was the one screen where the source
// looked right and the glass was wrong:
//   - the decision is ui::noticeDecide() (plain C++, host-tested), so a real message, a DM, a ping
//     and the USB `notify test` hook are judged by the same rule and print the same line;
//   - every notice logs exactly one `[notice] show|queue|skip(<reason>) kind=..` line;
//   - the redraw reports whether it really drew. A frame the shell refuses (a screen change owns
//     the light) is asked for again on the next pass instead of being silently dropped, and one
//     `[notice] painted after N ms` line says the toast reached a composed, pushed frame.
#pragma once

#include "ui/notify.h"

namespace om {
namespace toasts {

// The redraw that puts the current screen (with the toast in its frame) on the glass. It returns
// true when it drew; false when it had to refuse (the screen-change machine owns the frame).
void begin(bool (*redraw)());
void setScreenOn(bool on);

// A short line on top of the current frame (the screen's own door, used by every caller).
ui::NoticeDelivery text(const char* text);
// EST-366: the notification doors. `chatOpen` suppresses a message pop-up over its own chat.
// `reason` (optional) is the skip reason, for a caller that reports it (`notify test` does).
ui::NoticeDelivery notify(ui::NoticeKind kind, const char* text, bool chatOpen, bool dnd,
                          const char** reason = nullptr);
ui::NoticeDelivery notifyNotice(ui::NoticeKind kind, const ui::Notice& n, bool chatOpen, bool dnd,
                                const char** reason = nullptr);

// Once per loop pass: expiry redraws without the toast, a refused frame is requested again, and a
// queued notice takes the screen when the current one is gone.
void pump(bool screenOn);

}  // namespace toasts
}  // namespace om
