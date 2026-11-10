// SPDX-License-Identifier: MIT
// Synthetic touch and wake-button samples for the OMCMD1 test commands.
//
// The shell's loop reads the panel once per pass and feeds the same sample to the wake gate and to
// the press/drag/release gesture code. A USB tap/drag cannot call handleTap() directly: that would
// skip the gate, the drag rule and the overlay dismissal, so a test would not prove the real path.
// Instead this file replaces the raw sample for a few loop passes; everything downstream is the
// code the glass drives.
#pragma once

#include <stdint.h>

namespace om {

// A tap: one finger down at (x,y), held long enough for a loop pass, then lifted.
void usbTouchTap(uint16_t x, uint16_t y);

// A drag: down at (x0,y0), interpolated to (x1,y1) over ms milliseconds, then lifted. A zero or
// tiny duration still holds the finger for one sample so the press is seen.
void usbTouchDrag(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t ms);

// The next raw panel sample while a synthetic gesture is active. False means the injector is idle
// and the caller must read the real panel.
bool usbTouchSample(uint32_t nowMs, uint16_t* x, uint16_t* y, bool* down);

// A wake-button press and release. The shell feeds this edge through the same wake handler.
void usbWakePress();

// True while an injected wake edge is active; *pressed is its state.
bool usbWakeSample(uint32_t nowMs, bool* pressed);

}  // namespace om
