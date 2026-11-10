// SPDX-License-Identifier: MIT
// The L76K constellation-mode command ($PCAS04,7 = GPS + BeiDou + GLONASS) as a plain state machine.
//
// Why it is not sent in gpsBegin(): the module's reset line is released in boardBegin(), and the
// command used to go out 250 ms later, while the L76K was still starting. The boot log showed binary
// bytes before the first NMEA line and the heartbeat kept saying expected=12(g9 l0 b3), i.e. the
// module ignored it. So the command is sent only after the first sentence with a valid checksum has
// arrived: at that point the module is up and listening. Then we watch for a GLONASS talker for
// kWaitMs; if none appears the command is sent again, up to kMaxTries, and then we say so.
//
// The module's flash is never written ($PCAS00): the mode is set at every boot, so a module that was
// factory-reset still ends up in the right mode.
//
// A reboot of the module resets the machine: either an explicit onModuleReboot() (gpsBegin) or a
// silence of kRebootGapMs between valid sentences while the module had been talking. The next valid
// sentence then sends the command again.
//
// Plain C++ (no Arduino, no LovyanGFX) so test/host compiles it with the system g++.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace om {

enum class GpsModeAction : uint8_t {
  None = 0,
  Send,      // write "$PCAS04,7" now (tries() is the attempt number, 1..3)
  Seen,      // a GLONASS talker appeared: the mode is on
  GaveUp,    // three tries, no GLONASS inside the window
};

// True when `line` is a GLONASS talker, or a combined sentence that carries GLONASS satellites:
//   * $GLGSV / $GLGSA: the module reports GLONASS directly;
//   * $GNGSV: satellites of every constellation in one sentence, GLONASS PRNs are 65..96;
//   * $GNGSA: the last field is the NMEA 4.1 system id (2 = GLONASS); when that field is missing,
//     a satellite id in 65..96 is the same evidence.
// GPS or BeiDou lines alone are not evidence (the L76K's factory mode speaks exactly those).
inline bool gpsLineHasGlonass(const char* line) {
  if (line == nullptr || line[0] != '$' || strlen(line) < 6) return false;
  const bool gsv = strncmp(line + 3, "GSV", 3) == 0;
  const bool gsa = strncmp(line + 3, "GSA", 3) == 0;
  if (!gsv && !gsa) return false;
  if (strncmp(line, "$GL", 3) == 0) return true;

  const bool combined = strncmp(line, "$GN", 3) == 0;
  if (!combined) return false;

  // Everything between '$' and '*', split in place on commas. A sentence without a checksum is still
  // usable here (the caller only passes lines whose checksum is valid).
  char body[160];
  const char* star = strchr(line, '*');
  size_t len = star ? (size_t)(star - line) : strlen(line);
  if (len < 2 || len - 1 >= sizeof(body)) return false;
  memcpy(body, line + 1, len - 1);
  body[len - 1] = 0;

  char* fields[40] = {nullptr};
  int n = 0;
  char* p = body;
  while (p != nullptr && n < 40) {
    fields[n++] = p;
    char* comma = strchr(p, ',');
    if (comma == nullptr) break;
    *comma = 0;
    p = comma + 1;
  }
  if (n < 3) return false;

  if (combined && gsa) {
    // $GNGSA,A,3,02,05,12,25,,,,,,1.2,1.0,1.0,1   (last field: system id)
    // An older receiver has no system id and ends with the VDOP, a decimal ("1.0"); that is not an
    // id, so it falls through to the PRN check. An integer last field IS the NMEA 4.1 system id.
    const char* last = fields[n - 1];
    if (last[0] != 0 && strchr(last, '.') == nullptr) {
      const int system = atoi(last);
      if (system == 2) return true;
      if (system > 0) return false;
    }
    for (int i = 3; i < n - 3; ++i) {
      if (fields[i][0] == 0) continue;
      const int prn = atoi(fields[i]);
      if (prn >= 65 && prn <= 96) return true;
    }
    return false;
  }

  // $GNGSV,3,1,11,02,10,123,45,65,20,084,38, ...   PRN,elevation,azimuth,C/N0
  for (int i = 4; i + 3 < n; i += 4) {
    if (fields[i][0] == 0) continue;
    const int prn = atoi(fields[i]);
    if (prn >= 65 && prn <= 96) return true;
  }
  return false;
}

class GpsModeSetter {
 public:
  static constexpr uint32_t kWaitMs = 15000;      // how long to watch after each try
  static constexpr int kMaxTries = 3;
  static constexpr uint32_t kRebootGapMs = 5000;  // valid sentences never pause this long in normal use

  // A complete NMEA line whose checksum is valid. Handles both the GLONASS observation and, when
  // the module has just come up, the first Send. Call once per valid line.
  GpsModeAction onValidSentence(const char* line, uint32_t nowMs) {
    if (talked_ && (uint32_t)(nowMs - lastSentenceMs_) >= kRebootGapMs) onModuleReboot();
    talked_ = true;
    lastSentenceMs_ = nowMs;

    if (gpsLineHasGlonass(line)) {
      if (state_ == State::Seen) return GpsModeAction::None;
      state_ = State::Seen;
      return GpsModeAction::Seen;
    }
    if (state_ == State::WaitModule) {
      startTry(nowMs);
      return GpsModeAction::Send;
    }
    return GpsModeAction::None;
  }

  // Call regularly (every poll); returns Send when the 15 s window expired, GaveUp after the third.
  GpsModeAction tick(uint32_t nowMs) {
    if (state_ != State::WaitGlonass) return GpsModeAction::None;
    if ((uint32_t)(nowMs - lastTryMs_) < kWaitMs) return GpsModeAction::None;
    if (tries_ < kMaxTries) {
      startTry(nowMs);
      return GpsModeAction::Send;
    }
    state_ = State::GaveUp;
    return GpsModeAction::GaveUp;
  }

  // The module rebooted (or gpsBegin re-opened the UART): wait for it to talk, then send again.
  void onModuleReboot() {
    state_ = State::WaitModule;
    tries_ = 0;
    lastTryMs_ = 0;
    talked_ = false;
    lastSentenceMs_ = 0;
  }

  int tries() const { return tries_; }
  bool seen() const { return state_ == State::Seen; }
  bool gaveUp() const { return state_ == State::GaveUp; }
  bool waitingForModule() const { return state_ == State::WaitModule; }

 private:
  enum class State : uint8_t { WaitModule, WaitGlonass, Seen, GaveUp };

  void startTry(uint32_t nowMs) {
    ++tries_;
    lastTryMs_ = nowMs;
    state_ = State::WaitGlonass;
  }

  State state_ = State::WaitModule;
  int tries_ = 0;
  uint32_t lastTryMs_ = 0;
  uint32_t lastSentenceMs_ = 0;
  bool talked_ = false;
};

}  // namespace om
