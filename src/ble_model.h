// SPDX-License-Identifier: MIT
// EST-356: the Bluetooth switch, its three states, and the rule that turns a changed store into the
// one-line toast. Plain C++ (no Arduino, no LovyanGFX), compiled by test/host with the system g++.
//
// The model is the CLIENT side only: ble_link.cpp owns the radio. Here are the pieces a host can
// prove without a phone:
//   * the switch starts OFF and the phase follows it: off -> advertising -> connected -> advertising
//     -> off, with one serial bump per visible change;
//   * the root row / page / USB token for each phase;
//   * the change watch: the first poll is a baseline (nothing is "changed" just because the store has
//     content), a change our own screens made is not the phone's, and a real outside change becomes
//     one of the three toasts.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace ble {

enum class Phase : uint8_t { Off = 0, Advertising, Connected };

// The switch's default, on a fresh NVS namespace and after "forget everything".
constexpr bool kDefaultEnabled = false;

// USB/state token: the words `ble status` and the page's State row print.
inline const char* phaseToken(Phase p) {
  switch (p) {
    case Phase::Advertising: return "advertising";
    case Phase::Connected:   return "connected";
    default:                 return "off";
  }
}

// The General tab's row value: "off" / "on" / "connected" (short enough for the row).
inline const char* phaseLabel(Phase p) {
  switch (p) {
    case Phase::Advertising: return "on";
    case Phase::Connected:   return "connected";
    default:                 return "off";
  }
}

// One switch, one phase, one serial. The serial changes when the page or the status bar would look
// different, never on a poll.
class Model {
 public:
  void reset() {
    enabled_ = kDefaultEnabled;
    phase_ = Phase::Off;
    serial_ = 1;
  }

  void enable() {
    if (enabled_) return;
    enabled_ = true;
    phase_ = Phase::Advertising;
    bump();
  }

  void disable() {
    if (!enabled_ && phase_ == Phase::Off) return;
    enabled_ = false;
    phase_ = Phase::Off;
    bump();
  }

  // The transport reports a peer; only a switched-on transport can be connected.
  void noteConnected() {
    if (!enabled_ || phase_ == Phase::Connected) return;
    phase_ = Phase::Connected;
    bump();
  }

  // A peer left: back to advertising, because the switch is still on.
  void noteDisconnected() {
    if (!enabled_ || phase_ != Phase::Connected) return;
    phase_ = Phase::Advertising;
    bump();
  }

  bool enabled() const { return enabled_; }
  Phase phase() const { return phase_; }
  bool connected() const { return enabled_ && phase_ == Phase::Connected; }
  uint32_t serial() const { return serial_; }

 private:
  void bump() {
    ++serial_;
    if (serial_ == 0) serial_ = 1;
  }

  bool enabled_ = kDefaultEnabled;
  Phase phase_ = Phase::Off;
  uint32_t serial_ = 1;
};

// --- what the phone changed -----------------------------------------------------------------------
enum PhoneChange : uint8_t {
  kChangeNone = 0,
  kChangeChannels = 1u << 0,
  kChangeRadio = 1u << 1,
  kChangeName = 1u << 2,
};

inline const char* phoneChangeText(uint8_t change, char* out, size_t cap) {
  const char* text = "";
  if (change & kChangeChannels) text = "Channels changed from the phone";
  else if (change & kChangeRadio) text = "Radio changed from the phone";
  else if (change & kChangeName) text = "Name changed from the phone";
  if (out == nullptr || cap == 0) return text;
  if (text[0] == 0) { out[0] = 0; return out; }
  size_t n = 0;
  while (text[n] != 0 && n + 1 < cap) {
    out[n] = text[n];
    ++n;
  }
  out[n] = 0;
  return out;
}

// The store watched once per loop pass. `own*Gen` are counters our own write paths bump; comparing
// them with the value seen at the previous poll is what separates "I renamed a channel" from "the
// phone renamed a channel". A poll with no change answers kChangeNone.
class ChangeWatch {
 public:
  uint8_t poll(uint32_t channelSig, uint32_t radioSig, uint32_t nameSig, uint32_t ownChannelGen,
               uint32_t ownRadioGen) {
    if (!have_) {
      have_ = true;
      channelSig_ = channelSig;
      radioSig_ = radioSig;
      nameSig_ = nameSig;
      ownChannelGen_ = ownChannelGen;
      ownRadioGen_ = ownRadioGen;
      return kChangeNone;
    }
    uint8_t out = kChangeNone;
    if (channelSig != channelSig_) {
      const bool ours = ownChannelGen != ownChannelGen_;
      channelSig_ = channelSig;
      if (!ours) out |= kChangeChannels;
    }
    ownChannelGen_ = ownChannelGen;
    const bool radioOurs = ownRadioGen != ownRadioGen_;
    if (radioSig != radioSig_) {
      radioSig_ = radioSig;
      if (!radioOurs) out |= kChangeRadio;
    }
    ownRadioGen_ = ownRadioGen;
    if (nameSig != nameSig_) {
      // A name change is never made by our radio settings path; our own rename writes the same
      // NodePrefs field through meshNodeNameSet(), which bumps the radio generation.
      nameSig_ = nameSig;
      if (!radioOurs) out |= kChangeName;
    }
    return out;
  }

 private:
  bool have_ = false;
  uint32_t channelSig_ = 0;
  uint32_t radioSig_ = 0;
  uint32_t nameSig_ = 0;
  uint32_t ownChannelGen_ = 0;
  uint32_t ownRadioGen_ = 0;
};

}  // namespace ble
}  // namespace om
