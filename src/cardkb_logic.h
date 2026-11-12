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

// One code -> one action. `hasCursor` is the composer property the ticket names: the current
// editors insert at the end of the draft and draw a caret there, but they have no cursor position
// to move, so the shell passes false and left/right are ignored (and counted) today. A later
// editor with a real cursor passes true and gets the two moves with no change here.
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

// The one polling gate. A read of 0x5F happens only when the switch is on, the probe found the
// keyboard, and a text field is open right now. No background poll, no poll from a callback, no
// poll from the USB handler.
inline bool shouldPoll(bool enabled, bool found, bool textFieldOpen) {
  return enabled && found && textFieldOpen;
}

}  // namespace cardkb
}  // namespace om
