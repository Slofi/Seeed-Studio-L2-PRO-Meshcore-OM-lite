// SPDX-License-Identifier: MIT
// The status bar's RADIO FAULT mark: what this device can know for certain about ITSELF.
//
// Filip asked what "mesh not working" would even mean. The answer is only these three, each observed
// locally, never inferred from the air:
//   * Radio:  the SX1262 failed to start (or a radio operation later reported an error);
//   * Mesh:   MeshCore did not start (identity, settings or contact store);
//   * Tx:     our last transmission failed (see mesh_link's send path).
// Hearing nothing is NOT a fault: a quiet band is a place, not a broken radio, and channel messages
// have no ACK, so a missing reply proves nothing. There is deliberately no "silence" input here.
//
// The mark clears when the next TX succeeds or the radio is re-initialised (both mean radio and mesh
// are working). A more severe reason replaces a less severe one; a less severe one never hides a
// more severe one.
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host compiles it with the system g++.
#pragma once

#include <stdint.h>

namespace om {

enum class RadioFault : uint8_t {
  None = 0,
  Tx,      // our last transmission failed
  Mesh,    // MeshCore did not start
  Radio,   // the radio chip failed to start / stopped answering
};

// Rank decides which reason the one-line toast names when more than one is true. Higher wins.
inline uint8_t radioFaultRank(RadioFault f) {
  switch (f) {
    case RadioFault::Radio: return 3;
    case RadioFault::Mesh:  return 2;
    case RadioFault::Tx:    return 1;
    case RadioFault::None:  return 0;
  }
  return 0;
}

// The token used in the boot/USB log: "[radio] fault: <token>".
inline const char* radioFaultToken(RadioFault f) {
  switch (f) {
    case RadioFault::Radio: return "radio";
    case RadioFault::Mesh:  return "mesh";
    case RadioFault::Tx:    return "tx";
    case RadioFault::None:  return "none";
  }
  return "none";
}

// The one line the toast shows after a tap on the mark.
inline const char* radioFaultText(RadioFault f) {
  switch (f) {
    case RadioFault::Radio: return "Radio chip failed to start";
    case RadioFault::Mesh:  return "MeshCore did not start";
    case RadioFault::Tx:    return "Last transmission failed";
    case RadioFault::None:  return "Radio ok";
  }
  return "Radio ok";
}

class RadioFaultState {
 public:
  RadioFault reason() const { return reason_; }
  bool active() const { return reason_ != RadioFault::None; }

  // A reason became true. Returns true when the visible mark changed, so the caller logs exactly once
  // per change and not on every failed retry.
  bool set(RadioFault f) {
    if (f == RadioFault::None) return false;
    if (radioFaultRank(f) <= radioFaultRank(reason_)) return false;
    reason_ = f;
    return true;
  }

  // The mark is gone. Returns true when it actually was showing.
  bool clear() {
    if (reason_ == RadioFault::None) return false;
    reason_ = RadioFault::None;
    return true;
  }

  // A transmission went out: radio and mesh are demonstrably working, whatever was wrong before.
  bool onTxOk() { return clear(); }

  // The radio (and the mesh built on it) was re-initialised.
  bool onRadioReinit() { return clear(); }

 private:
  RadioFault reason_ = RadioFault::None;
};

// One instance for the running firmware. Header-only, so the host test gets its own.
inline RadioFaultState& radioFaultState() {
  static RadioFaultState s;
  return s;
}

}  // namespace om
