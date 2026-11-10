// SPDX-License-Identifier: MIT
// EST-371: the toast and notification module. It owns the one toast (the deadline and the
// consumed-tap dismissal stay in ui/toast.h) and the burst queue decided in ui/notify.h, so no
// screen and no arrival path writes that state. main.cpp only calls it and passes the redraw that
// puts the new frame on the glass.
#pragma once

#include "ui/notify.h"

namespace om {
namespace toasts {

void begin(void (*redraw)());
void setScreenOn(bool on);

// A short line on top of the current frame (the screen's own door, used by every caller).
void text(const char* text);
// EST-366: the notification doors. `chatOpen` suppresses a message pop-up over its own chat.
void notify(ui::NoticeKind kind, const char* text, bool chatOpen, bool dnd);
void notifyNotice(ui::NoticeKind kind, const ui::Notice& n, bool chatOpen, bool dnd);

// Once per loop pass: expiry redraws without the toast, and a queued notice takes the screen when
// the current one is gone.
void pump(bool screenOn);

}  // namespace toasts
}  // namespace om
