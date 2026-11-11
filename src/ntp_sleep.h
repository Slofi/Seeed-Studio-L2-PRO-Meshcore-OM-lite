// SPDX-License-Identifier: MIT
// EST-386: modem sleep during an NTP attempt, as plain C++ so test/host proves the decision and the
// firmware compiles the very same rule.
//
// Why a guard: EST-385 turned Wi-Fi modem sleep off (WiFi.setSleep(false), i.e. WIFI_PS_NONE) for
// every attempt without looking at Bluetooth. ESP-IDF's coexistence rule is the opposite: with Wi-Fi
// and BT both enabled the station must keep modem sleep on. On this build (Arduino-ESP32 2.0.17 /
// ESP-IDF 4.4) the precompiled libpp.a pm.o `pm_set_sleep_type` does not just warn: asked for a type
// other than MIN/MAX modem, not in mesh mode, it queries the coexistence adapter for "BT enabled" and,
// when that answers yes, logs "Should enable WiFi modem sleep when both WiFi and Bluetooth are
// enabled" and calls abort(). esp_wifi_set_ps() itself returns ESP_OK first (it only queues an
// ieee80211_ioctl to the Wi-Fi task), so the caller cannot notice the refusal; the device panics
// asynchronously. See PROGRESS.md EST-386 for the disassembly.
//
// The rule this class owns, one attempt at a time:
//   * Bluetooth OFF at the start: the attempt may turn modem sleep off, and must turn it back on when
//     the attempt ends.
//   * Bluetooth ON at the start: modem sleep is never touched (the radio is left exactly as it is).
//   * Bluetooth starts while sleep is off: put sleep back at once, once, and leave it on for the rest
//     of the attempt. A BLE stop later in the same attempt does not turn it off again, so the guard
//     never fights a BLE start/stop.
//   * The end (a result, the timeout, the link going down, the switch going off) restores sleep only
//     when this attempt turned it off and never restored it.
#pragma once

#include <stdint.h>

namespace om {
namespace ntp {

enum class SleepAction : uint8_t { None = 0, TurnOff, TurnOn };

class SleepGuard {
 public:
  // Opens an attempt. TurnOff means the caller may call WiFi.setSleep(false) now; None means the
  // radio must be left alone (Bluetooth is on). A previous attempt that was never closed is dropped
  // without touching the radio: the caller owns the one restore, at that attempt's own end.
  SleepAction begin(bool bluetoothOn) {
    m_open = true;
    m_wantOff = !bluetoothOn;
    m_off = false;
    return m_wantOff ? SleepAction::TurnOff : SleepAction::None;
  }

  // The radio's answer to begin()'s TurnOff: true when modem sleep is off now and this attempt owns
  // restoring it. A refused change leaves the guard neutral, so nothing tries to undo it at the end.
  void offResult(bool offNow) { m_off = m_open && m_wantOff && offNow; }

  // Called once per loop pass while the attempt is in flight. TurnOn exactly once, when Bluetooth
  // came on while this attempt had modem sleep off.
  SleepAction poll(bool bluetoothOn) {
    if (!m_open || !m_off || !bluetoothOn) return SleepAction::None;
    m_off = false;
    return SleepAction::TurnOn;
  }

  // Closes the attempt. TurnOn only when the guard owns an off state; a neutral guard (Bluetooth on
  // at the start, a refused change, or a mid-attempt restore) returns None, so an attempt's end can
  // never turn sleep on over a running BT stack.
  SleepAction end() {
    const bool restore = m_open && m_off;
    m_open = false;
    m_wantOff = false;
    m_off = false;
    return restore ? SleepAction::TurnOn : SleepAction::None;
  }

  bool attemptOpen() const { return m_open; }
  bool ownsOff() const { return m_off; }

 private:
  bool m_open = false;
  bool m_wantOff = false;
  bool m_off = false;
};

}  // namespace ntp
}  // namespace om
