// SPDX-License-Identifier: MIT
// EST-387: GPS aiding, the CASIC AID-INI message, as plain C++ so test/host proves the bytes.
//
// Why: indoors the L2 hears one satellite at 29..32 dB-Hz, which is enough to decode time but not to
// fix. The clock, however, is often real from a non-GPS source (Wi-Fi NTP, the phone, the hand).
// The L76K is an AT6558-class CASIC receiver: it does NOT speak u-blox PMTK and has no NMEA command
// for "here is the time/where you are". What it does have is the CASIC binary class 0x0B (AID):
// AID-INI (0x0B 0x01) injects a reference time and, optionally, a reference position, which is
// exactly the warm-start information a receiver uses to cut its time-to-first-fix.
//
// THE SWITCH IS OFF. Reason, stated so nobody has to guess: this sandbox has no network, so the
// AID-INI field table could not be checked against the CASIC protocol specification; the layout in
// aidIniPayload() below is written out field by field from memory of that specification and is
// marked as unverified at every field that matters. A wrong-but-accepted frame would seed the
// receiver with a wrong time, and the receiver's time is what sets our clock indoors, so a wrong
// aid is worse than none. The frame layer, the ACK/NAK reader, the once-per-boot gate and the
// payload builder are complete and host-tested: turning aiding on is kEnabled = true below, and
// the device then prints `[gps] aid time=ok|nak|no-answer ...`. If that line says nak or no-answer,
// the switch goes back to false (as the ticket requires) and the field table needs checking.
//
// The formats (frame and ACK) are the parts this file is confident about:
//   frame: BA CE | len (2, LE) | class | id | payload | sum (4, LE)
//          len   = class + id + payload + the 4 checksum bytes
//          sum   = 32-bit sum of every byte before it, header included
//   ack:   class 0x05, id 0x00 (ACK) / 0x01 (NAK), payload = class, id, payload length
//
// Rule 9 (the repo is public): nothing here logs a coordinate. The builder takes lat/lon as numbers
// and the device only ever prints tokens (`pos=sent|none`).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "timekeeper.h"   // TimeSource, timeSourcePriority(), saneEpoch()

namespace om {
namespace aid {

// The switch. See the header note: false until the AID-INI field table is verified and the device
// says the module acknowledged the frame.
constexpr bool kEnabled = false;

// ---------------------------------------------------------------------------- the CASIC frame
constexpr uint8_t kSync0 = 0xBA;
constexpr uint8_t kSync1 = 0xCE;
constexpr int kHeaderBytes = 4;     // the two sync bytes and the 2-byte length itself
constexpr int kFrameOverhead = 6;   // class + id + the four checksum bytes
constexpr int kMaxPayload = 64;
constexpr int kMaxFrame = kMaxPayload + kFrameOverhead + kHeaderBytes;

// One complete frame into `out`. Returns its length, or 0 when the arguments do not fit (`cap` too
// small, `n` out of range, a null payload with n > 0). Bytes: sync, length, class, id, payload, then
// the 32-bit little-endian sum of everything before the checksum. The length field counts every byte
// AFTER itself (class + id + payload + the 4 checksum bytes = n + 6), which is 4 less than the total
// frame length on the wire; both numbers are spelled out here because mixing them up is the classic
// way to cut a payload short.
inline int casicFrame(uint8_t cls, uint8_t id, const uint8_t* payload, int n, uint8_t* out,
                      int cap) {
  if (out == nullptr || n < 0 || n > kMaxPayload) return 0;
  if (payload == nullptr && n > 0) return 0;
  const int lenField = n + kFrameOverhead;
  const int total = lenField + kHeaderBytes;
  if (cap < total) return 0;
  out[0] = kSync0;
  out[1] = kSync1;
  out[2] = (uint8_t)(lenField & 0xFF);
  out[3] = (uint8_t)((lenField >> 8) & 0xFF);
  out[4] = cls;
  out[5] = id;
  for (int i = 0; i < n; ++i) out[6 + i] = payload[i];
  uint32_t sum = 0;
  for (int i = 0; i < total - 4; ++i) sum += (uint32_t)out[i];
  out[total - 4] = (uint8_t)(sum & 0xFF);
  out[total - 3] = (uint8_t)((sum >> 8) & 0xFF);
  out[total - 2] = (uint8_t)((sum >> 16) & 0xFF);
  out[total - 1] = (uint8_t)((sum >> 24) & 0xFF);
  return total;
}

// ---------------------------------------------------------------------------- AID-INI
// The message ids this file knows.
constexpr uint8_t kAckClass = 0x05;   // CASIC "ACK"
constexpr uint8_t kAckId = 0x00;      // ... ACK
constexpr uint8_t kNakId = 0x01;      // ... NAK
constexpr uint8_t kAidClass = 0x0B;   // CASIC "AID"
constexpr uint8_t kAidIniId = 0x01;   // ... AID-INI

// How long the module gets to answer before the aid line says no-answer.
constexpr uint32_t kAnswerTimeoutMs = 2000;

// The payload, 56 bytes. VERIFY THIS TABLE against the CASIC protocol specification before
// kEnabled goes true: the offsets below are the ones this file was written from, not a checked
// copy. Every value is little-endian; an "unused" field is zero, and the two validity words say
// whether the receiver may use the position and the time at all.
//
//   off  size  field        what we put there
//   0    4     xo32_freq    0: keep the module's own crystal (we never send a frequency)
//   4    2     pos_valid    1 when the point below is included, else 0
//   6    2     time_valid   1 when the date/time below is included, else 0
//   8    8     lat          degrees, + north (IEEE-754 double)         [unverified offset]
//   16   8     lon          degrees, + east (IEEE-754 double)          [unverified offset]
//   24   4     alt          metres above the WGS-84 ellipsoid (float)  [unverified offset]
//   28   2     year         UTC, 0..9999                               [unverified offset]
//   30   1     month        1..12
//   31   1     day          1..31
//   32   1     hour         0..23
//   33   1     minute       0..59
//   34   1     second       0..60 (60 = leap second)
//   35   1     reserved     0
//   36   20    reserved     0
constexpr int kAidIniBytes = 56;

struct AidTime {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
};

inline void putU16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

inline void putU32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}

inline void putF32(uint8_t* p, float v) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(v), "a float is 32 bits");
  memcpy(&bits, &v, sizeof(bits));
  putU32(p, bits);
}

inline void putF64(uint8_t* p, double v) {
  uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(v), "a double is 64 bits");
  memcpy(&bits, &v, sizeof(bits));
  for (int i = 0; i < 8; ++i) p[i] = (uint8_t)((bits >> (8 * i)) & 0xFF);
}

// The 56 payload bytes. Returns kAidIniBytes, or 0 when a field cannot be encoded (an impossible
// date, a point outside the planet, a small buffer). `posValid` and `timeValid` are independent:
// the caller may aid the time alone, which is the case this ticket cares about (a receiver that
// hears one satellite but cannot fix still has a usable clock reference).
inline int aidIniPayload(const AidTime& t, bool timeValid, bool posValid, double lat, double lon,
                         float altMeters, uint8_t* out, int cap) {
  if (out == nullptr || cap < kAidIniBytes) return 0;
  if (posValid) {
    const bool latOk = lat >= -90.0 && lat <= 90.0;
    const bool lonOk = lon >= -180.0 && lon <= 180.0;
    if (!latOk || !lonOk) return 0;
  }
  if (timeValid) {
    if (t.year < 2000 || t.year > 2099) return 0;
    if (t.month < 1 || t.month > 12) return 0;
    if (t.day < 1 || t.day > daysInMonth(t.year, t.month)) return 0;
    if (t.hour < 0 || t.hour > 23) return 0;
    if (t.minute < 0 || t.minute > 59) return 0;
    if (t.second < 0 || t.second > 60) return 0;
  }
  memset(out, 0, (size_t)kAidIniBytes);
  putU32(out + 0, 0);                          // xo32_freq: the module's own crystal
  putU16(out + 4, posValid ? 1 : 0);
  putU16(out + 6, timeValid ? 1 : 0);
  if (posValid) {
    putF64(out + 8, lat);
    putF64(out + 16, lon);
    putF32(out + 24, altMeters);
  }
  if (timeValid) {
    putU16(out + 28, (uint16_t)t.year);
    out[30] = (uint8_t)t.month;
    out[31] = (uint8_t)t.day;
    out[32] = (uint8_t)t.hour;
    out[33] = (uint8_t)t.minute;
    out[34] = (uint8_t)t.second;
    out[35] = 0;
  }
  return kAidIniBytes;
}

// ---------------------------------------------------------------------------- UTC civil time
// Local civil time exists in timekeeper.h; the receiver wants UTC, so this is the inverse of that
// file's daysFromCivil (Howard Hinnant's civil_from_days). Plain integer arithmetic, no <time.h>.
struct UtcCivil {
  int year = 1970;
  int month = 1;
  int day = 1;
  int hour = 0;
  int minute = 0;
  int second = 0;
};

inline void utcCivilFromEpoch(uint32_t epoch, UtcCivil& out) {
  const int64_t days = (int64_t)(epoch / 86400u);
  const int64_t secs = (int64_t)(epoch % 86400u);
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = (int64_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  out.year = (int)(y + (m <= 2 ? 1 : 0));
  out.month = (int)m;
  out.day = (int)d;
  out.hour = (int)(secs / 3600);
  out.minute = (int)((secs % 3600) / 60);
  out.second = (int)(secs % 60);
}

// One call for the caller that has an epoch and wants the struct: false when the clock cannot be a
// real clock (saneEpoch), so a floor or a default date is never sent to the receiver.
inline bool aidTimeFromEpoch(uint32_t epoch, AidTime& out) {
  if (!saneEpoch(epoch)) return false;
  UtcCivil c;
  utcCivilFromEpoch(epoch, c);
  out.year = c.year;
  out.month = c.month;
  out.day = c.day;
  out.hour = c.hour;
  out.minute = c.minute;
  out.second = c.second;
  return true;
}

// ---------------------------------------------------------------------------- reading the answer
// The ACK arrives on the same UART as NMEA, in the middle of the lines, so the byte stream needs a
// reader that can tell a binary frame from a sentence. NMEA is printable ASCII and never contains
// 0xBA, so a 0xBA that is not followed by 0xCE is not a frame start and is handed back to the NMEA
// parser (Pass). Everything inside a well-formed frame is Taken; Complete is returned once, with the
// frame ready to read.
class CasicReader {
 public:
  enum class Step : uint8_t { Pass = 0, Taken, Complete };

  Step feed(uint8_t b) {
    switch (state_) {
      case State::Idle:
        if (b != kSync0) return Step::Pass;
        sum_ = b;
        ok_ = false;
        state_ = State::Sync;
        return Step::Taken;
      case State::Sync:
        if (b == kSync1) {
          sum_ += b;
          lenBytes_ = 0;
          state_ = State::Length;
        } else if (b == kSync0) {
          sum_ = b;   // a second 0xBA: this one may be the real frame start
        } else {
          state_ = State::Idle;
          return Step::Pass;   // the 0xBA was not a frame start; this byte is stream data
        }
        return Step::Taken;
      case State::Length:
        sum_ += b;
        lenBytes_ |= (uint16_t)b << (8 * head_);
        if (++head_ < 2) return Step::Taken;
        len_ = (int)lenBytes_;
        head_ = 0;
        payloadLen_ = 0;
        if (len_ < kFrameOverhead || len_ > kMaxFrame) {
          state_ = State::Idle;   // not a frame we can follow: drop it and resynchronise
          return Step::Taken;
        }
        state_ = State::Head;
        return Step::Taken;
      case State::Head:
        sum_ += b;
        if (head_ == 0) {
          cls_ = b;
          head_ = 1;
        } else {
          id_ = b;
          bodyLeft_ = len_ - kFrameOverhead;
          state_ = bodyLeft_ > 0 ? State::Body : State::Sum;
          sumIndex_ = 0;
          sumGot_ = 0;
        }
        return Step::Taken;
      case State::Body:
        sum_ += b;
        if (payloadLen_ < kMaxPayload) payload_[payloadLen_++] = b;
        if (--bodyLeft_ == 0) {
          state_ = State::Sum;
          sumIndex_ = 0;
          sumGot_ = 0;
        }
        return Step::Taken;
      case State::Sum:
        sumGot_ |= (uint32_t)b << (8 * sumIndex_);
        if (++sumIndex_ < 4) return Step::Taken;
        ok_ = (sumGot_ == sum_);
        state_ = State::Idle;
        return Step::Complete;
    }
    return Step::Pass;
  }

  bool valid() const { return ok_; }          // the last completed frame's checksum matched
  uint8_t cls() const { return cls_; }
  uint8_t id() const { return id_; }
  const uint8_t* payload() const { return payload_; }
  int payloadLen() const { return payloadLen_; }

  // Is the last completed frame the answer (ACK or NAK) to one class/id? The CASIC ACK payload is
  // the acknowledged class, the acknowledged id and its payload length; the length is not required
  // here, so a receiver that fills it differently still counts.
  bool answersFor(uint8_t cls, uint8_t id) const {
    return ok_ && cls_ == kAckClass && (id_ == kAckId || id_ == kNakId) && payloadLen_ >= 2 &&
           payload_[0] == cls && payload_[1] == id;
  }
  bool isNak() const { return id_ == kNakId; }

  void reset() {
    state_ = State::Idle;
    ok_ = false;
    cls_ = id_ = 0;
    payloadLen_ = 0;
    len_ = 0;
    head_ = 0;
    bodyLeft_ = 0;
    sum_ = 0;
    sumGot_ = 0;
    sumIndex_ = 0;
    lenBytes_ = 0;
  }

 private:
  enum class State : uint8_t { Idle, Sync, Length, Head, Body, Sum };
  State state_ = State::Idle;
  bool ok_ = false;
  uint8_t cls_ = 0;
  uint8_t id_ = 0;
  uint8_t payload_[kMaxPayload] = {};
  int payloadLen_ = 0;
  int len_ = 0;
  int head_ = 0;
  int bodyLeft_ = 0;
  uint16_t lenBytes_ = 0;
  uint32_t sum_ = 0;
  uint32_t sumGot_ = 0;
  int sumIndex_ = 0;
};

// ---------------------------------------------------------------------------- when to aid
// The clock sources that are independent of the receiver. GPS itself is the receiver; GpsNoFix came
// FROM it, so neither can aid it.
inline bool aidSourceEligible(TimeSource s) {
  return s == TimeSource::Wifi || s == TimeSource::Phone || s == TimeSource::Manual;
}

// The ticket's rule, as one object: at most one frame per boot, and a second one only when a
// BETTER time source has arrived since (Wi-Fi 4 > phone 3 > manual 2), never for a worse or equal
// one. A GPS fix closes the door (no aid needed: time and position come from the sky).
class AidGate {
 public:
  bool shouldSend(bool clockReal, TimeSource src, uint32_t epochUtc, bool gpsHasFix) const {
    if (!clockReal || gpsHasFix) return false;
    if (!aidSourceEligible(src)) return false;
    if (!saneEpoch(epochUtc)) return false;
    if (!sent_) return true;
    return timeSourcePriority(src) > sentPriority_;
  }

  void onSent(TimeSource src) {
    sent_ = true;
    sentPriority_ = timeSourcePriority(src);
    ++sends_;
  }

  bool sent() const { return sent_; }
  int sends() const { return sends_; }

 private:
  bool sent_ = false;
  int sentPriority_ = 0;
  int sends_ = 0;
};

}  // namespace aid
}  // namespace om
