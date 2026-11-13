// SPDX-License-Identifier: MIT
// EST-412: the M5Stack Unit CardKB on the L2's Grove port. This header is the whole plain-C++ half:
// the key-code mapping, the saved switch's default and the polling gate. No Arduino, no Wire, no
// display, so test/host proves the rules with the system g++.
//
// Key codes: the CardKB answers an I2C read with the pressed key's ASCII code and 0x00 when nothing
// is pressed. The arrows are NOT ASCII; M5Stack's CardKB v1.1 firmware key-code table (the table
// M5Stack publishes for the Unit CardKB, and the same names in its CardKB firmware source) gives
// 0xB4 left, 0xB5 up, 0xB6 down, 0xB7 right. If a unit in the field reports different arrow bytes,
// the handler counts and logs them as unknown instead of inserting them, so the table can be fixed
// from the serial line without a wrong character ever reaching a draft.
//
// EST-417: measured on Filip's own CardKB (S497, the `[kb] ignored code ...` log): the plain arrows
// are 0xB4 left, 0xB7 right, 0xB5 up, 0xB6 down, and Fn is ONE-SHOT on the keyboard itself (press Fn,
// then the key; it applies to the next key only). So Fn+arrow arrives as its own single byte: 0x98
// Fn+left, 0xA5 Fn+right, 0x99 Fn+up, 0xA4 Fn+down. The driver does not track Fn: the byte IS the
// whole story, and cardkb::arrowFor() is the only place that turns one into an Arrow.
#pragma once

#include <stdint.h>

namespace om {
namespace cardkb {

// The switch is OFF on a fresh image and a missing NVS key. That is the safety default: Grove
// power (5 V) and a second I2C bus stay off until the user asks for them.
constexpr bool kDefaultEnabled = false;

// The one boot decision. `keyPresent` is false on a first boot / after an NVS wipe; then the
// answer is kDefaultEnabled whatever the caller's buffer happened to hold. main.cpp passes the
// result of the shared bool-pref reader, so this is the same rule every saved bool uses.
inline bool enabledFromNvs(bool keyPresent, bool saved) {
  return keyPresent ? saved : kDefaultEnabled;
}

enum class Action : uint8_t {
  None = 0,    // 0x00: nothing is pressed. Not a key, never counted.
  Char,        // printable ASCII 0x20..0x7E
  Backspace,   // 0x08
  Enter,       // 0x0D
  Esc,         // 0x1B
  Left,        // 0xB4, only when the editor has a movable cursor
  Right,       // 0xB7, only when the editor has a movable cursor
  Ignored,     // a real code this build does not act on: counted, never inserted
};

struct Key {
  Action action = Action::None;
  char ch = 0;
};

constexpr uint8_t kCodeNone = 0x00;
constexpr uint8_t kCodeBackspace = 0x08;
constexpr uint8_t kCodeEnter = 0x0D;
constexpr uint8_t kCodeEsc = 0x1B;
constexpr uint8_t kCodeLeft = 0xB4;
constexpr uint8_t kCodeUp = 0xB5;
constexpr uint8_t kCodeDown = 0xB6;
constexpr uint8_t kCodeRight = 0xB7;
// EST-417: the one-shot Fn pair, measured on Filip's unit (see the header note).
constexpr uint8_t kCodeFnLeft = 0x98;
constexpr uint8_t kCodeFnRight = 0xA5;
constexpr uint8_t kCodeFnUp = 0x99;
constexpr uint8_t kCodeFnDown = 0xA4;

// EST-417: what the user pressed, independent of the screen. The shell asks keynav_logic.h what that
// means where it is standing; the driver never acts on an arrow itself.
enum class Arrow : uint8_t { None = 0, Left, Right, Up, Down, FnLeft, FnRight, FnUp, FnDown };

// The ONE place a byte becomes an Arrow. Every arrow byte is listed here and nowhere else, so the
// measured table above can be fixed in one line if a second unit reports different bytes.
inline Arrow arrowFor(uint8_t code) {
  switch (code) {
    case kCodeLeft:    return Arrow::Left;
    case kCodeRight:   return Arrow::Right;
    case kCodeUp:      return Arrow::Up;
    case kCodeDown:    return Arrow::Down;
    case kCodeFnLeft:  return Arrow::FnLeft;
    case kCodeFnRight: return Arrow::FnRight;
    case kCodeFnUp:    return Arrow::FnUp;
    case kCodeFnDown:  return Arrow::FnDown;
    default:           return Arrow::None;
  }
}

// One code -> one action. `hasCursor` is the composer property the ticket names: the current
// editors insert at the end of the draft and draw a caret there, but they have no cursor position
// to move, so the shell passes false and left/right are ignored (and counted) today. A later
// editor with a real cursor passes true and gets the two moves with no change here.
// EST-417: the shell asks arrowFor() FIRST, so no arrow byte ever reaches this function; Left/Right
// stay for that future editor cursor. A byte that is neither an arrow nor one of the keys below is
// Ignored (counted and logged, never inserted).
inline Key mapCode(uint8_t code, bool hasCursor) {
  Key k;
  if (code == kCodeNone) return k;
  if (code >= 0x20 && code <= 0x7E) {
    k.action = Action::Char;
    k.ch = (char)code;
    return k;
  }
  switch (code) {
    case kCodeBackspace: k.action = Action::Backspace; return k;
    case kCodeEnter:     k.action = Action::Enter; return k;
    case kCodeEsc:       k.action = Action::Esc; return k;
    case kCodeLeft:      k.action = hasCursor ? Action::Left : Action::Ignored; return k;
    case kCodeRight:     k.action = hasCursor ? Action::Right : Action::Ignored; return k;
    case kCodeUp:
    case kCodeDown:
    default:             k.action = Action::Ignored; return k;
  }
}

// The one polling gate. A read of 0x5F happens only when the switch is on and the probe found the
// keyboard, on ANY screen. EST-412 also required a text field to be open; EST-417 drops that (the
// arrows work everywhere, so the keys must be read everywhere), and the text field now only decides
// what a key DOES. No background poll, no poll from a callback, no poll from the USB handler.
// With the switch off this is false whatever else is true: no power, no bus, no reads.
inline bool shouldPoll(bool enabled, bool found) {
  return enabled && found;
}

// EST-417 rule 8: the unplugged keyboard. A read can fail (no ACK, a short read) and one failure is
// noise, so the driver only gives up after this many in a row. At the ~30 ms cadence that is about
// 0.3 s, inside the ticket's 0.3-0.4 s window.
constexpr int kFailLimit = 10;

// True exactly at the read that finishes the run: N-1 failures keep everything as it is, the Nth
// marks the keyboard lost (the driver then logs once, closes the bus and drops Grove power).
inline bool lostAfterFailure(int consecutiveFailures) {
  return consecutiveFailures >= kFailLimit;
}

// ------------------------------------------------------------------------------------------------
// EST-421: the start window. The CardKB's ATmega8A needs longer than the old fixed 50 ms settle after
// the Grove 5 V rail comes on (proven on Filip's unit on 2026-11-13: the keyboard only answered after
// the rail had been on for about a minute). So the enable no longer waits: it powers the rail and this
// state machine says when the loop task should probe 0x5F, until the keyboard answers or the window
// ends. Nothing here blocks, and nothing polls the keyboard while the window is open (found() is
// false until the answer), so boot and the UI never wait for a keyboard.
//
// EST-423: the window is TEN seconds and ONE pin order only. Every probe inside it uses the measured
// order SDA 13 / SCL 14; the swap is never tried there, because driving the keyboard's SDA as SCL on
// alternate attempts may leave its ATmega8A TWI in an error state and keep it from ever answering.
// The swapped order (SDA 14 / SCL 13) is exactly ONE attempt AFTER the window has failed, so the two
// candidate causes stay apart in the log: an answer at N inside the window is a start-up time, an
// answer only on the fallback is a pin order the keyboard wanted.
constexpr uint32_t kStartWindowMs = 10000;  // how long the one order is tried, from the rail on
constexpr uint32_t kProbeIntervalMs = 100;  // one probe per ~100 ms inside the window

enum class WindowAction : uint8_t {
  Idle = 0,   // no window open, or not due yet: this pass does nothing
  Probe,      // probe 0x5F now, SDA 13 / SCL 14 (the one order inside the window)
  Fallback,   // the window is over: ONE probe with the pins swapped, SDA 14 / SCL 13
  TimedOut,   // both orders failed: drop the rail, tell the user once
};

struct WindowStep {
  WindowAction action = WindowAction::Idle;
  bool sda13First = true;   // Probe and Fallback: the pin order of this attempt
  uint32_t elapsedMs = 0;   // ms since the rail came on, the N in the found log
};

class StartWindow {
 public:
  // Power has just been applied. The first probe is due at once (elapsed 0) because the keyboard may
  // answer immediately when the rail never went down; then one every kProbeIntervalMs in the measured
  // order. At kStartWindowMs the window stops probing and asks for the one swapped attempt; the
  // TimedOut only follows that attempt's failure. A second start() begins a fresh window.
  void start(uint32_t nowMs) {
    active_ = true;
    startMs_ = nowMs;
    dueMs_ = nowMs;
    probes_ = 0;
    fallbackUsed_ = false;
  }

  // Switch OFF mid-window: no more probes, no timeout toast. The driver drops the rail itself.
  void cancel() { active_ = false; }

  bool active() const { return active_; }
  uint32_t probes() const { return probes_; }
  bool fallbackUsed() const { return fallbackUsed_; }
  uint32_t elapsed(uint32_t nowMs) const { return nowMs - startMs_; }

  // The one decision for this loop pass. The window's edge is checked FIRST, so the last probe inside
  // it is the one due at kStartWindowMs - 100 and none is made at the closing instant. A slow pass
  // cannot bunch probes up: the next due time is 100 ms after the attempt that really happened,
  // never a catch-up burst. At the edge the state machine asks for the ONE swapped attempt and only
  // after that attempt has failed does it say TimedOut (once: the window closes there).
  WindowStep next(uint32_t nowMs) {
    WindowStep s;
    if (!active_) return s;
    s.elapsedMs = elapsed(nowMs);
    if (s.elapsedMs >= kStartWindowMs) {
      // EST-423: one order inside the window, one swapped attempt after it, then the end.
      if (!fallbackUsed_) {
        fallbackUsed_ = true;
        s.action = WindowAction::Fallback;
        s.sda13First = false;
        return s;
      }
      s.action = WindowAction::TimedOut;
      active_ = false;
      return s;
    }
    if ((int32_t)(nowMs - dueMs_) < 0) return s;
    s.action = WindowAction::Probe;
    s.sda13First = true;   // EST-423: the measured order only, never the swap inside the window
    ++probes_;
    dueMs_ = nowMs + kProbeIntervalMs;
    return s;
  }

 private:
  bool active_ = false;
  bool fallbackUsed_ = false;
  uint32_t startMs_ = 0;
  uint32_t dueMs_ = 0;
  uint32_t probes_ = 0;
};

}  // namespace cardkb
}  // namespace om
