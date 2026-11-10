// SPDX-License-Identifier: MIT
// The L76K diagnostic: does our TX path reach the module at all, and which constellation modes does
// this module accept? (EST-338, after `$PCAS04,7` was still not taken in EST-337.)
//
// One boot answers it in this order, after the module has proved it is up (the first checksum-valid
// NMEA sentence):
//
//   1. TX PATH. Send a harmless command with a visible effect: `$PCAS03` turns GSV output OFF for
//      5 s, then ON again. If the GSV lines stop and come back, the write path (GPIO17) works; if
//      they never stop, the module is not receiving our bytes. The caller logs
//      `[gps] tx path: ok` or `[gps] tx path: no effect`. L76K PCAS03 takes the per-sentence output
//      period in the order GGA,GLL,GSA,GSV,RMC,VTG,ZDA,ANT (0 = off, 1 = every fix); the probe
//      changes only the GSV field.
//   2. MODE 7 (GPS + BeiDou + GLONASS). Watch 15 s for a GLONASS talker.
//   3. If mode 7 did not take, MODE 5 (GPS + GLONASS). Watch 15 s for a GLONASS talker.
//   4. Restore MODE 3 (GPS + BeiDou, the module's factory mode) unless mode 7 worked.
//
// The module's flash is never written ($PCAS00 is never sent), so this survives a factory reset.
// A module reboot (explicit onModuleReboot(), or a >5 s silence between valid sentences) starts the
// sequence over.
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host compiles it with the system g++.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace om {

// One action for the caller: write one of the commands.
enum class GpsDiagAction : uint8_t {
  None = 0,
  TxProbeOff,   // $PCAS03 with GSV output off
  TxProbeOn,    // $PCAS03 with GSV output on
  Mode7,        // $PCAS04,7 (GPS + BeiDou + GLONASS)
  Mode5,        // $PCAS04,5 (GPS + GLONASS)
  Mode3,        // $PCAS04,3 (GPS + BeiDou, factory mode)
};

// What just happened, for the caller's log line. One step carries one event and one action.
enum class GpsDiagEvent : uint8_t {
  None = 0,
  TxPathOk,
  TxPathNoEffect,
  Mode7Taken,
  Mode7NotTaken,
  Mode5Taken,
  Mode5NotTaken,
};

enum class GpsTxPath : uint8_t { Unknown = 0, Ok, NoEffect };
enum class GpsModeResult : uint8_t { NotTried = 0, Taken, NotTaken };

struct GpsDiagStep {
  GpsDiagAction action = GpsDiagAction::None;
  GpsDiagEvent event = GpsDiagEvent::None;

  bool operator==(const GpsDiagStep& o) const {
    return action == o.action && event == o.event;
  }
};

// True for any GSV sentence (GPGSV, GLGSV, BDGSV, GNGSV): the TX probe watches these lines stop and
// return. The checksum is the caller's job; only complete, valid lines reach the machine.
inline bool gpsLineHasGsv(const char* line) {
  return line != nullptr && line[0] == '$' && strlen(line) >= 6 && strncmp(line + 3, "GSV", 3) == 0;
}

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
  static constexpr uint32_t kProbeOffMs = 5000;   // GSV is off this long, then turned back on
  static constexpr uint32_t kProbeOnMs = 5000;    // and expected back inside this window
  // The module sends its NMEA lines in one burst each second. The OFF command can land in the middle
  // of a burst, so GSV lines in the first 500 ms are still from the old setting and do not count.
  static constexpr uint32_t kProbeSettleMs = 500;
  static constexpr uint32_t kModeWaitMs = 15000;  // GLONASS watch after mode 7 / mode 5
  static constexpr uint32_t kRebootGapMs = 5000;  // valid sentences never pause this long in normal use

  // One complete NMEA line whose checksum is valid. Call once per line.
  GpsDiagStep onValidSentence(const char* line, uint32_t nowMs) {
    if (talked_ && (uint32_t)(nowMs - lastSentenceMs_) >= kRebootGapMs) onModuleReboot();
    talked_ = true;
    lastSentenceMs_ = nowMs;

    switch (state_) {
      case State::WaitModule:
        // The module is up and listening: start the TX-path probe.
        state_ = State::ProbeOff;
        phaseMs_ = nowMs;
        gsvDuringOff_ = 0;
        gsvDuringOn_ = 0;
        return step(GpsDiagAction::TxProbeOff, GpsDiagEvent::None);

      case State::ProbeOff:
        // Ignore the tail of the burst the command landed in.
        if (gpsLineHasGsv(line) && (uint32_t)(nowMs - phaseMs_) >= kProbeSettleMs) ++gsvDuringOff_;
        return {};

      case State::ProbeOn:
        if (gpsLineHasGsv(line)) {
          if (gsvDuringOff_ == 0) {
            // GSV disappeared during the OFF window and is back: the bytes reached the module.
            txPath_ = GpsTxPath::Ok;
            state_ = State::WaitMode7;
            phaseMs_ = nowMs;
            return step(GpsDiagAction::Mode7, GpsDiagEvent::TxPathOk);
          }
          ++gsvDuringOn_;
        }
        return {};

      case State::WaitMode7:
        if (gpsLineHasGlonass(line)) {
          mode7_ = GpsModeResult::Taken;
          state_ = State::Done;
          return step(GpsDiagAction::None, GpsDiagEvent::Mode7Taken);
        }
        return {};

      case State::WaitMode5:
        if (gpsLineHasGlonass(line)) {
          mode5_ = GpsModeResult::Taken;
          state_ = State::Done;
          // Mode 5 proved the module can do GLONASS but is not the mode we keep.
          return step(GpsDiagAction::Mode3, GpsDiagEvent::Mode5Taken);
        }
        return {};

      case State::Done:
      default:
        return {};
    }
  }

  // Call regularly (every gpsPoll). Runs the windows and the fallbacks.
  GpsDiagStep tick(uint32_t nowMs) {
    switch (state_) {
      case State::ProbeOff:
        if ((uint32_t)(nowMs - phaseMs_) < kProbeOffMs) return {};
        // 5 s of OFF are over. Turn GSV back on and wait for the first one.
        gsvDuringOn_ = 0;
        state_ = State::ProbeOn;
        phaseMs_ = nowMs;
        return step(GpsDiagAction::TxProbeOn, GpsDiagEvent::None);

      case State::ProbeOn:
        if ((uint32_t)(nowMs - phaseMs_) < kProbeOnMs) return {};
        // GSV never came back (or never stopped): the write had no visible effect.
        txPath_ = GpsTxPath::NoEffect;
        state_ = State::WaitMode7;
        phaseMs_ = nowMs;
        return step(GpsDiagAction::Mode7, GpsDiagEvent::TxPathNoEffect);

      case State::WaitMode7:
        if ((uint32_t)(nowMs - phaseMs_) < kModeWaitMs) return {};
        mode7_ = GpsModeResult::NotTaken;
        state_ = State::WaitMode5;
        phaseMs_ = nowMs;
        return step(GpsDiagAction::Mode5, GpsDiagEvent::Mode7NotTaken);

      case State::WaitMode5:
        if ((uint32_t)(nowMs - phaseMs_) < kModeWaitMs) return {};
        mode5_ = GpsModeResult::NotTaken;
        state_ = State::Done;
        return step(GpsDiagAction::Mode3, GpsDiagEvent::Mode5NotTaken);

      case State::WaitModule:
      case State::Done:
      default:
        return {};
    }
  }

  // The module rebooted (or gpsBegin re-opened the UART): wait for it to talk, then run again.
  void onModuleReboot() {
    state_ = State::WaitModule;
    phaseMs_ = 0;
    lastSentenceMs_ = 0;
    talked_ = false;
    gsvDuringOff_ = 0;
    gsvDuringOn_ = 0;
    txPath_ = GpsTxPath::Unknown;
    mode7_ = GpsModeResult::NotTried;
    mode5_ = GpsModeResult::NotTried;
  }

  bool waitingForModule() const { return state_ == State::WaitModule; }
  bool done() const { return state_ == State::Done; }
  GpsTxPath txPath() const { return txPath_; }
  GpsModeResult mode7() const { return mode7_; }
  GpsModeResult mode5() const { return mode5_; }
  int gsvDuringOff() const { return gsvDuringOff_; }
  int gsvDuringOn() const { return gsvDuringOn_; }

 private:
  enum class State : uint8_t { WaitModule, ProbeOff, ProbeOn, WaitMode7, WaitMode5, Done };

  static GpsDiagStep step(GpsDiagAction action, GpsDiagEvent event) {
    GpsDiagStep s;
    s.action = action;
    s.event = event;
    return s;
  }

  State state_ = State::WaitModule;
  uint32_t phaseMs_ = 0;
  uint32_t lastSentenceMs_ = 0;
  bool talked_ = false;
  int gsvDuringOff_ = 0;
  int gsvDuringOn_ = 0;
  GpsTxPath txPath_ = GpsTxPath::Unknown;
  GpsModeResult mode7_ = GpsModeResult::NotTried;
  GpsModeResult mode5_ = GpsModeResult::NotTried;
};

}  // namespace om
