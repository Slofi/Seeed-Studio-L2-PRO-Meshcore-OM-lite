// SPDX-License-Identifier: MIT
// EST-405: the screen auto-off timer and its two saved values, plain C++ so test/host proves them
// with the system g++.
//
// Filip (2026-11-11): screen auto-off "should be a toggleable feature with adjustable time-till-off;
// a good-to-have but not essential, since there is a convenient wake button. I generally do not like
// auto screen-off features." Hence: DEFAULT OFF, a missing NVS key is OFF, and nothing below runs
// while the switch is off (the loop only calls step()).
//
// The timer never owns the light. When step() says "now", the shell runs its ONE sleep transition
// (the same fade-out, DISPOFF and panel sleep the wake button drives) and the same wake path brings
// it back, so there is one way to sleep and one way to wake.
//
// Rules this header implements (the ticket's own):
//   * off after `timeout` ms with no touch, no wake-button press and no USB command;
//   * NOT while the keyboard/composer is open, a pop-up or confirmation is up, an OTA upload runs,
//     or a map drag/zoom is in progress;
//   * NOT within kUsbQuietMs of a USB command;
//   * a touch, a wake press and a USB command all restart the count.
// A blocker PAUSES the count instead of only postponing the moment: while the keyboard is open, or
// a pop-up is up, the count is held at zero, so closing the pop-up gives the full time again. Turning
// the screen off the instant a pop-up closes would be the one behavior nobody asked for.
#pragma once

#include <stdint.h>

namespace om {

// The five choices, in the order the Settings page lists them. 30 s is the shortest the control
// logic allows and 10 min the longest; the default is the ticket's 1 min.
constexpr int kIdleTimeCount = 5;
constexpr int kIdleTimeDefaultIndex = 1;   // 1 min

struct IdleTimeChoice {
  const char* label;
  uint32_t ms;
};

constexpr IdleTimeChoice kIdleTimes[kIdleTimeCount] = {
    {"30 s", 30000},
    {"1 min", 60000},
    {"2 min", 120000},
    {"5 min", 300000},
    {"10 min", 600000},
};

// The time a USB command keeps the screen on, on top of restarting the count.
constexpr uint32_t kUsbQuietMs = 30000;

// How often the loop looks at the glass while the panel is dark, so a touch can wake it (rule 2).
// One GT911 read per this many ms instead of one per loop pass.
constexpr uint32_t kIdleTouchPollMs = 40;

// The names of the two NVS keys (namespace "omlite", like the other saved switches).
constexpr const char* kIdlePrefKey = "saoff";    // u8: 0 off, 1 on
constexpr const char* kIdleTimePrefKey = "saoft"; // u8: index into kIdleTimes

// Everything that must keep the screen awake. The shell fills this once per pass from its own state;
// the labels in the comments are the ticket's words.
struct IdleBlockers {
  bool keyboard = false;    // the keyboard sheet or a composer is open
  bool popup = false;       // a pop-up, a confirmation, a sheet or a dropdown is showing
  bool ota = false;         // an OTA upload runs
  bool mapGesture = false;  // a map drag or a zoom (a stale canvas) is in progress
  // The message/new-node toast on the glass. Not one of the ticket's listed blockers, but rule 3
  // says a new message pop-up keeps the screen on for its 5 s when the screen was on. A toast that
  // arrives on a screen the timer already put to sleep never reaches here: step() is a no-op while
  // the screen is off, so a pop-up still cannot wake an auto-off screen.
  bool toast = false;
};

constexpr bool idleBlocked(const IdleBlockers& b) {
  return b.keyboard || b.popup || b.ota || b.mapGesture || b.toast;
}

class ScreenIdle {
 public:
  // Boot: the saved switch and choice, and the moment the count starts from (millis() at boot, so
  // an image that boots with the switch on does not sleep the screen before anyone sees it).
  void begin(bool enabled, int timeIndex, uint32_t nowMs) {
    enabled_ = enabled;
    timeIndex_ = validIndex(timeIndex) ? timeIndex : kIdleTimeDefaultIndex;
    lastActivityMs_ = nowMs;
    lastUsbMs_ = nowMs;
    usbSeen_ = false;
  }

  // The switch, from the Settings row. Switching it on restarts the count (the user is looking at
  // the screen right now); switching it off only stops the timer.
  void setEnabled(bool on, uint32_t nowMs) {
    if (on == enabled_) return;
    enabled_ = on;
    lastActivityMs_ = nowMs;
  }

  // A new time choice, from the Auto-off page. Restarts the count from now.
  void setTimeoutIndex(int index, uint32_t nowMs) {
    if (!validIndex(index)) return;
    timeIndex_ = index;
    lastActivityMs_ = nowMs;
  }

  // A touch sample with the finger down, or a wake-button press edge.
  void onActivity(uint32_t nowMs) { lastActivityMs_ = nowMs; }

  // One executed USB command (OMCMD1). Counts as activity and starts the 30 s quiet window.
  void onUsbCommand(uint32_t nowMs) {
    lastUsbMs_ = nowMs;
    usbSeen_ = true;
    lastActivityMs_ = nowMs;
  }

  // True while a USB command is inside its 30 s quiet window. The test checks it directly; step()
  // uses it, together with the blockers, to hold the count at zero.
  bool usbQuiet(uint32_t nowMs) const {
    return !usbSeen_ || (uint32_t)(nowMs - lastUsbMs_) >= kUsbQuietMs;
  }

  // Once per loop pass, AFTER the pass's touch sample and USB poll, so a press or command seen in
  // this pass is already in. Returns true on the ONE pass that must put the screen to sleep; the
  // caller then runs its one sleep transition. A blocked pass holds the count at zero.
  bool step(uint32_t nowMs, bool screenOn, const IdleBlockers& b) {
    if (!screenOn || !enabled_) return false;
    if (idleBlocked(b) || !usbQuiet(nowMs)) {
      lastActivityMs_ = nowMs;   // paused: the blocked time never counts against the user
      return false;
    }
    if ((uint32_t)(nowMs - lastActivityMs_) < timeoutMs()) return false;
    // Restart from now: the wake path's own onActivity() also resets, so a re-enable or a missed
    // wake can never sleep the screen on the first pass after it came back.
    lastActivityMs_ = nowMs;
    return true;
  }

  bool enabled() const { return enabled_; }
  int timeIndex() const { return timeIndex_; }
  uint32_t timeoutMs() const { return kIdleTimes[timeIndex_].ms; }
  const char* timeLabel() const { return kIdleTimes[timeIndex_].label; }

 private:
  static bool validIndex(int i) { return i >= 0 && i < kIdleTimeCount; }

  bool enabled_ = false;              // DEFAULT OFF: a missing key, a fresh install, an upgrade
  int timeIndex_ = kIdleTimeDefaultIndex;
  uint32_t lastActivityMs_ = 0;
  uint32_t lastUsbMs_ = 0;
  bool usbSeen_ = false;
};

// The two saved values. A missing key is the default and nothing is written for it; a set writes
// ONLY when the value really changes, so opening Settings and tapping the row that is already set
// costs no flash commit. The backend is the firmware's Preferences (the shell injects it) and RAM in
// the host test, exactly like power_log_logic.h's PowerLogPref and ui/map_layers.h.
//
// Both values are stored as one byte so the two keys share one callback pair: "saoff" is 0 or 1,
// "saoft" is the index into kIdleTimes.
class ScreenIdlePrefs {
 public:
  // read: true when the key exists (`out` holds it); false leaves the fallback. write: true on
  // success.
  using ReadFn = bool (*)(void* ctx, const char* key, uint8_t fallback, uint8_t& out);
  using WriteFn = bool (*)(void* ctx, const char* key, uint8_t value);

  void begin(ReadFn read, WriteFn write, void* ctx) {
    read_ = read;
    write_ = write;
    ctx_ = ctx;
  }

  // Loads both keys. A missing or out-of-range time falls back to 1 min; a missing switch is OFF.
  // Nothing is written, so an upgrade from an image without these keys changes no NVS byte.
  void load() {
    enabled_ = readU8(kIdlePrefKey, 0) != 0;
    const uint8_t raw = readU8(kIdleTimePrefKey, (uint8_t)kIdleTimeDefaultIndex);
    timeIndex_ = raw < (uint8_t)kIdleTimeCount ? (int)raw : kIdleTimeDefaultIndex;
  }

  bool enabled() const { return enabled_; }
  int timeIndex() const { return timeIndex_; }

  // True when the store holds the value afterwards: a no-op change (already set) is a success and
  // writes nothing; a failed write still leaves the RAM value changed, like PowerLogPref::setOn().
  bool setEnabled(bool on) {
    if (on == enabled_) return true;
    enabled_ = on;
    return write_ != nullptr && write_(ctx_, kIdlePrefKey, on ? 1 : 0);
  }

  bool setTimeIndex(int index) {
    if (index < 0 || index >= kIdleTimeCount) return false;
    if (index == timeIndex_) return true;
    timeIndex_ = index;
    return write_ != nullptr && write_(ctx_, kIdleTimePrefKey, (uint8_t)index);
  }

 private:
  uint8_t readU8(const char* key, uint8_t fallback) {
    uint8_t out = fallback;
    if (read_ != nullptr && read_(ctx_, key, fallback, out)) return out;
    return fallback;
  }

  ReadFn read_ = nullptr;
  WriteFn write_ = nullptr;
  void* ctx_ = nullptr;
  bool enabled_ = false;
  int timeIndex_ = kIdleTimeDefaultIndex;
};

}  // namespace om
