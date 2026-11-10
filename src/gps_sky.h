// SPDX-License-Identifier: MIT
// The GSV half of the GNSS parser: what the module EXPECTS from its almanac and what it actually
// HEARS, plus the two records an outdoor test needs even when nothing is plugged in.
//
// Why: the L76K indoors sends GSV lines whose C/N0 field is empty for every satellite
// ("$GPGSV,3,1,10,04,05,091,,05,35,311,,..."). Those satellites are the ones its almanac expects,
// with elevation and azimuth, and no signal at all: the old count took GSV's "in view" number for a
// heard count, so the status bar said "GPS 0/13" while the module heard nothing. A satellite is
// HEARD only when its C/N0 field has a value above zero; the expected count is kept beside it.
//
// Two records live in GpsState so the next USB log read can say what happened outside:
//   * bestSnr: the best C/N0 of the last 60 s (one 1 s bucket per second, so a strong sample that
//     left the window drops out and the best of what is left takes over, not just a timestamp),
//   * maxHeard: the most satellites heard at once since boot (a new cycle resets the live count).
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host compiles it with the system g++ and feeds it
// real GSV sentences; board.cpp keeps the UART, GGA and RMC/ZDA halves.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace om {

// GSV talkers map to these; GGA's satellite count is the "used" one and cannot tell "hears nothing"
// from "hears plenty, too weakly", which is exactly what the heard counts are for.
enum GpsConstellation : uint8_t {
  kGpsUs = 0,        // GPGSV
  kGpsGlonass,       // GLGSV
  kGpsBeiDou,        // BDGSV / GBGSV
  kGpsGalileo,       // GAGSV
  kGpsOther,         // GQGSV (QZSS), GNGSV (combined), anything else
  kGpsConstellationCount
};

// The best C/N0 of the last 60 s, kept in one 1 s bucket per second. A bucket holds the strongest
// value seen in that second, so the answer is the exact maximum of the window, not a value with a
// timestamp that either forgets a still-recent sample or keeps a stale one.
//
// The second is stored in 16 bits: (uint16_t)(now - then) wraps correctly for a 60 s window, and a
// bucket is reused after 64 s anyway. peak == 0 means "no sample in that second" (C/N0 is never 0
// when a satellite is heard: the field is empty or a value above zero).
struct GpsSnrWindow {
  static constexpr uint32_t kWindowMs = 60000;
  static constexpr uint16_t kWindowSec = 60;
  static constexpr int kBuckets = 64;   // > the window, so a second's bucket is never shared within it

  uint8_t peak[kBuckets] = {0};
  uint16_t sec[kBuckets] = {0};

  void add(uint32_t nowMs, int snr) {
    if (snr <= 0) return;
    const uint8_t value = (uint8_t)(snr > 255 ? 255 : snr);
    const uint16_t s = (uint16_t)(nowMs / 1000u);
    const int b = (int)(s % (uint16_t)kBuckets);
    if (peak[b] == 0 || sec[b] != s) {
      peak[b] = value;
      sec[b] = s;
    } else if (value > peak[b]) {
      peak[b] = value;
    }
  }

  // The best value seen less than 60 s ago; 0 when nothing was heard in the window.
  uint8_t best(uint32_t nowMs) const {
    const uint16_t s = (uint16_t)(nowMs / 1000u);
    uint8_t out = 0;
    for (int b = 0; b < kBuckets; ++b) {
      if (peak[b] == 0) continue;
      if ((uint16_t)(s - sec[b]) >= kWindowSec) continue;
      if (peak[b] > out) out = peak[b];
    }
    return out;
  }

  void reset() {
    for (int b = 0; b < kBuckets; ++b) {
      peak[b] = 0;
      sec[b] = 0;
    }
  }
};

// The GPS state the app carries: fix/position from GGA, the expected/heard sky from GSV, the clock
// from RMC/ZDA, and the UART counters that say whether the module is talking at all.
struct GpsState {
  bool valid = false;      // fix quality > 0
  uint8_t sats = 0;        // satellites USED in the fix (GGA field 7)
  double lat = 0.0;
  double lon = 0.0;
  uint32_t bytes = 0;      // bytes received on the UART (0 = the module is not talking to us)
  uint32_t sentences = 0;  // complete NMEA lines
  uint32_t checksumBad = 0;  // lines whose '*' checksum did not match (skipped, never parsed)
  uint32_t timeEpoch = 0;  // latest UTC time from RMC/ZDA, 0 = none yet

  // GSV, per constellation: satellites the module EXPECTS from its almanac, and satellites it HEARS
  // (a C/N0 field with a value above zero in the current cycle).
  uint8_t expected[kGpsConstellationCount] = {0, 0, 0, 0, 0};
  uint8_t heard[kGpsConstellationCount] = {0, 0, 0, 0, 0};
  uint8_t expectedTotal = 0;  // sum of the named constellations, or GN's own count when only GN talks
  uint8_t heardTotal = 0;     // the same rule, for the satellites actually received
  uint8_t maxHeard = 0;       // most satellites heard at once since boot
  uint8_t bestSnr = 0;        // best C/N0 of the last 60 s in dB-Hz, 0 = none in the window

  // Parser state, not for the screens: heard satellites in the CURRENT cycle (reset on message 1),
  // whether a NAMED constellation has spoken (a GNGSV-only receiver reports the whole sky in GN, so
  // the GN count is used only when no named constellation has reported), and the 1 s buckets behind
  // bestSnr.
  uint8_t cycleHeard[kGpsConstellationCount] = {0, 0, 0, 0, 0};
  bool namedGsv = false;
  GpsSnrWindow snrWindow;
};

inline uint8_t gpsCountU8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// GSV talker -> constellation. GN (combined) and GQ (QZSS) and anything unknown land in kGpsOther.
inline int gpsConstellationOf(const char* line) {
  if (line == nullptr || line[0] != '$' || strlen(line) < 5) return -1;
  const char a = line[1];
  const char b = line[2];
  if (a == 'G' && b == 'P') return kGpsUs;
  if (a == 'G' && b == 'L') return kGpsGlonass;
  if ((a == 'B' && b == 'D') || (a == 'G' && b == 'B')) return kGpsBeiDou;
  if (a == 'G' && b == 'A') return kGpsGalileo;
  return kGpsOther;
}

inline int gpsHexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// XOR of everything between '$' and '*'. A line without a readable checksum is not data we can
// trust (the L76K always sends one), so board.cpp counts it bad and skips it instead of parsing.
inline bool gpsNmeaChecksumOk(const char* line) {
  if (line == nullptr || line[0] != '$') return false;
  const char* star = strrchr(line, '*');
  if (!star || star[1] == 0 || star[2] == 0) return false;
  const int hi = gpsHexDigit(star[1]);
  const int lo = gpsHexDigit(star[2]);
  if (hi < 0 || lo < 0) return false;
  uint8_t sum = 0;
  for (const char* p = line + 1; p < star; ++p) sum ^= (uint8_t)*p;
  return (int)sum == hi * 16 + lo;
}

// The totals the status bar, the heartbeat and Settings use. A receiver that only speaks GNGSV
// reports the whole sky in that one sentence, so GN's count is used only when no named
// constellation has reported at all (its own 0 is a real 0, not a reason to fall back).
inline uint8_t gpsTotalOf(const uint8_t* per, bool namedGsv) {
  unsigned sum = 0;
  if (namedGsv) {
    sum = (unsigned)per[kGpsUs] + per[kGpsGlonass] + per[kGpsBeiDou] + per[kGpsGalileo];
  } else {
    sum = per[kGpsOther];
  }
  return (uint8_t)(sum > 255 ? 255 : sum);
}

inline void gpsSkyRefresh(GpsState& out, uint32_t nowMs) {
  out.expectedTotal = gpsTotalOf(out.expected, out.namedGsv);
  out.heardTotal = gpsTotalOf(out.heard, out.namedGsv);
  if (out.heardTotal > out.maxHeard) out.maxHeard = out.heardTotal;
  out.bestSnr = out.snrWindow.best(nowMs);
}

// $..GSV,total messages,message number,satellites in view,
//        prn,elevation,azimuth,snr, prn,elevation,azimuth,snr, ...
// (the checksum has already been verified by the caller). Returns true when the line was a GSV line
// it understood. An empty or zero C/N0 field means the satellite is EXPECTED, not heard, and a
// cycle's heard count starts again at message 1.
inline bool gpsParseGsv(const char* line, GpsState& out, uint32_t nowMs) {
  const int c = gpsConstellationOf(line);
  if (c < 0) return false;
  const char* star = strchr(line, '*');
  const size_t stop = star ? (size_t)(star - line) : strlen(line);
  if (stop < 2) return false;
  const size_t len = stop - 1;   // everything between '$' and '*'
  char body[120];
  if (len >= sizeof(body)) return false;
  memcpy(body, line + 1, len);
  body[len] = 0;

  char* fields[28] = {nullptr};
  int n = 0;
  char* p = body;
  while (p != nullptr && n < 28) {
    fields[n++] = p;
    char* comma = strchr(p, ',');
    if (comma == nullptr) break;
    *comma = 0;
    p = comma + 1;
  }
  if (n < 4) return false;

  const int msgNo = atoi(fields[2]);
  out.expected[c] = gpsCountU8(atoi(fields[3]));
  if (c != kGpsOther) out.namedGsv = true;
  if (msgNo <= 1) out.cycleHeard[c] = 0;   // a new cycle: last cycle's reception is history
  int heard = out.cycleHeard[c];
  for (int i = 4; i + 3 < n; i += 4) {
    if (fields[i + 3][0] == 0) continue;   // an empty C/N0 field: the almanac's expectation, not a signal
    const int snr = atoi(fields[i + 3]);
    if (snr <= 0) continue;                // a literal "0" is not a measurement either
    if (heard < 255) ++heard;
    out.snrWindow.add(nowMs, snr);
  }
  out.cycleHeard[c] = (uint8_t)heard;
  out.heard[c] = out.cycleHeard[c];
  gpsSkyRefresh(out, nowMs);
  return true;
}

// Forget the sky (expected/heard/max/window). GpsState starts zeroed, so this is only for a caller
// that keeps one across a module power cycle.
inline void gpsResetSky(GpsState& out) {
  for (int i = 0; i < kGpsConstellationCount; ++i) {
    out.expected[i] = 0;
    out.heard[i] = 0;
    out.cycleHeard[i] = 0;
  }
  out.expectedTotal = 0;
  out.heardTotal = 0;
  out.maxHeard = 0;
  out.bestSnr = 0;
  out.namedGsv = false;
  out.snrWindow.reset();
}

}  // namespace om
