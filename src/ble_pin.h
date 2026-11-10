// SPDX-License-Identifier: MIT
// EST-356: the Bluetooth pairing PIN, plain C++ so test/host can prove the rule with the system g++.
//
// The PIN is MeshCore's own 6-digit companion PIN: it lives in NodePrefs::ble_pin (the same field the
// phone's CMD_SET_DEVICE_PIN writes) and is generated once, at the first switch-on. 123456 is the
// upstream example's fixed code, so this firmware refuses to hand it out: a random PIN that is the
// world's most famous one is not a random PIN.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace om {
namespace ble {

constexpr uint32_t kPinMin = 100000;
constexpr uint32_t kPinMax = 999999;
constexpr uint32_t kPinForbidden = 123456;   // upstream's BLE_PIN_CODE default: never ours

// A PIN this firmware may use: exactly six digits, zero is "not made yet".
inline bool pinValid(uint32_t pin) {
  return pin >= kPinMin && pin <= kPinMax && pin != kPinForbidden;
}

// Maps one 32-bit random word onto the legal range. Every value in 100000..999999 is reachable; the
// forbidden value is pushed one step up (and 999999 wraps to 100000), never returned.
inline uint32_t pinFromRandom(uint32_t r) {
  uint32_t pin = kPinMin + (r % (kPinMax - kPinMin + 1));
  if (pin == kPinForbidden) pin = (pin == kPinMax) ? kPinMin : pin + 1;
  return pin;
}

// "123456" (six characters, no sign, zero padded). `cap` must be at least 7.
inline void pinText(char* out, size_t cap, uint32_t pin) {
  if (out == nullptr || cap == 0) return;
  snprintf(out, cap, "%06lu", (unsigned long)pin);
}

}  // namespace ble
}  // namespace om
