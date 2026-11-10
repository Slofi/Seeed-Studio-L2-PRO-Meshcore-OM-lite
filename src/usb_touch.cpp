// SPDX-License-Identifier: MIT
// Synthetic touch and wake samples. See usb_touch.h for why this exists and what it must not do.
#include "usb_touch.h"

#include <Arduino.h>

namespace om {
namespace {

constexpr uint32_t kTapHoldMs = 90;      // at least four 20 ms loop passes
constexpr uint32_t kReleaseMs = 60;      // the lifted sample must be seen at least once
constexpr uint32_t kDragMinMs = 20;      // a zero-length drag still has a down and an up
constexpr uint32_t kWakeHoldMs = 120;    // two edges for the wake handler's press/release pair
constexpr uint32_t kWakeReleaseMs = 140;

enum class TouchKind : uint8_t { None, Tap, Drag };

TouchKind s_kind = TouchKind::None;
uint32_t s_startMs = 0;
uint32_t s_holdMs = 0;
uint16_t s_x0 = 0;
uint16_t s_y0 = 0;
uint16_t s_x1 = 0;
uint16_t s_y1 = 0;

bool s_wakeActive = false;
uint32_t s_wakeStartMs = 0;

void start(TouchKind kind, uint32_t nowMs) {
  s_kind = kind;
  s_startMs = nowMs;
}

}  // namespace

void usbTouchTap(uint16_t x, uint16_t y) {
  s_x0 = x;
  s_y0 = y;
  s_x1 = x;
  s_y1 = y;
  s_holdMs = kTapHoldMs;
  start(TouchKind::Tap, millis());
}

void usbTouchDrag(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t ms) {
  s_x0 = x0;
  s_y0 = y0;
  s_x1 = x1;
  s_y1 = y1;
  s_holdMs = ms < kDragMinMs ? kDragMinMs : ms;
  start(TouchKind::Drag, millis());
}

bool usbTouchSample(uint32_t nowMs, uint16_t* x, uint16_t* y, bool* down) {
  if (s_kind == TouchKind::None) return false;
  const uint32_t dt = nowMs - s_startMs;
  if (dt >= s_holdMs + kReleaseMs) {
    s_kind = TouchKind::None;
    return false;
  }
  if (x) *x = s_x0;
  if (y) *y = s_y0;
  if (dt >= s_holdMs) {
    if (x) *x = s_x1;
    if (y) *y = s_y1;
    if (down) *down = false;
    return true;
  }
  if (s_kind == TouchKind::Drag && s_holdMs > 0) {
    // Linear interpolation, rounded down; the last down sample is the end point.
    const uint32_t t = dt;
    const int32_t dx = (int32_t)s_x1 - (int32_t)s_x0;
    const int32_t dy = (int32_t)s_y1 - (int32_t)s_y0;
    if (x) *x = (uint16_t)((int32_t)s_x0 + dx * (int32_t)t / (int32_t)s_holdMs);
    if (y) *y = (uint16_t)((int32_t)s_y0 + dy * (int32_t)t / (int32_t)s_holdMs);
  }
  if (down) *down = true;
  return true;
}

void usbWakePress() {
  s_wakeActive = true;
  s_wakeStartMs = millis();
}

bool usbWakeSample(uint32_t nowMs, bool* pressed) {
  if (!s_wakeActive) return false;
  const uint32_t dt = nowMs - s_wakeStartMs;
  if (dt >= kWakeHoldMs + kWakeReleaseMs) {
    s_wakeActive = false;
    return false;
  }
  if (pressed) *pressed = dt < kWakeHoldMs;
  return true;
}

}  // namespace om
