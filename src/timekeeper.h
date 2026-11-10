// SPDX-License-Identifier: MIT
// OM Lite timekeeper: one clock (the system clock) fed by ranked sources.
//
// The board has no RTC. MeshCore's VolatileRTCClock used to restart at 15 May 2024 on every boot, so
// every advert we sent looked like a replay to the other nodes. This module replaces that clock with
// the system time (settimeofday/time), which survives a sync and is what MeshCore reads through an
// RTCClock adapter (mesh_link.cpp).
//
// Sources, best first: GPS, Wi-Fi NTP, phone over Bluetooth, manual. A lower source may set the clock
// only when no higher source has set it in the last 6 hours; manual always applies when the user does
// it, and the next GPS fix takes over immediately. Never set the clock from an advert or a message.
//
// A floor (saved in NVS by the shell) keeps MeshCore timestamps rising across reboots even with no
// source. It is a lower bound, NOT a real time: realSinceBoot() stays false until a real source has
// set the clock, and only then may the status bar show HH:MM.
//
// Plain C++ (POSIX time functions only), so test/host compiles the same logic with the system g++.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {

enum class TimeSource : uint8_t { None = 0, Gps, Wifi, Phone, Manual };

const char* timeSourceName(TimeSource s);
// 0 = unknown, 1 = manual, 2 = phone, 3 = Wi-Fi, 4 = GPS. Manual is lowest for ranking but always
// applies when the user sets it.
int timeSourcePriority(TimeSource s);

constexpr uint32_t kTimeSourceHoldSec = 6UL * 60UL * 60UL;   // 6 hours
constexpr uint32_t kFloorSaveIntervalSec = 10UL * 60UL;      // 10 minutes
constexpr uint32_t kMinSaneEpoch = 1735689600UL;             // 2025-01-01 00:00:00 UTC
constexpr uint32_t kMaxSaneEpoch = 4102444800UL;             // 2100-01-01 00:00:00 UTC

// --- calendar and NMEA ---------------------------------------------------------------------------
// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's civil_from_days inverse).
constexpr int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = (unsigned)(year - era * 400);
  const unsigned mp = month > 2 ? month - 3U : month + 9U;   // March = 0 ... February = 11
  const unsigned doy = (153U * mp + 2U) / 5U + day - 1U;
  const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
  return (int64_t)era * 146097LL + (int64_t)doe - 719468LL;
}

constexpr bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

constexpr int daysInMonth(int y, int m) {
  return m == 2 ? (isLeapYear(y) ? 29 : 28)
                : ((m == 4 || m == 6 || m == 9 || m == 11) ? 30 : 31);
}

constexpr uint32_t makeUtcEpoch(int year, int month, int day, int hour, int minute, int second) {
  const int64_t days = daysFromCivil(year, (unsigned)month, (unsigned)day);
  return (uint32_t)(days * 86400LL + hour * 3600LL + minute * 60LL + second);
}

constexpr bool validCivil(int year, int month, int day, int hour, int minute, int second) {
  return year >= 2000 && year <= 2099 && month >= 1 && month <= 12 && day >= 1 &&
         day <= daysInMonth(year, month) && hour >= 0 && hour < 24 && minute >= 0 && minute < 60 &&
         second >= 0 && second <= 60;   // 60 = leap second
}

constexpr bool saneEpoch(uint32_t epoch) {
  return epoch >= kMinSaneEpoch && epoch < kMaxSaneEpoch;
}

struct ParsedTime {
  bool ok = false;
  uint32_t epoch = 0;
  bool hasDate = false;
};

// Parses one complete $..RMC or $..ZDA sentence (checksum included). RMC needs status A AND a sane
// date; ZDA needs a sane date AND a year the build can vouch for (see kZdaYearsAhead). A void RMC
// or the module's default date before a satellite is rejected. Returns ok=false for a bad checksum,
// a split/truncated line, or an impossible date.
ParsedTime parseNmeaTime(const char* sentence);

// The year this firmware was compiled in, taken from this file's __DATE__. 0 if it cannot be read.
int buildYear();

// ZDA carries no validity field, so a receiver that has never seen a satellite can still send its
// default date and there is nothing in the sentence to say so. A ZDA year outside
// build year .. build year + 1 is therefore refused (a device flashed in December is still running
// in January, hence the one year of slack).
constexpr int kZdaYearsAhead = 1;

// NMEA time hhmmss(.sss) -> seconds since midnight.
bool parseNmeaClock(const char* field, int& hour, int& minute, int& second);

// __DATE__/__TIME__ (the compiler's local time, Europe/Ljubljana) -> UTC epoch. Returns false if the
// strings are malformed; the caller then uses only the saved floor.
bool parseBuildTimestamp(const char* date, const char* time, uint32_t& epochUtc);

// --- local time ----------------------------------------------------------------------------------
// Europe/Ljubljana, exactly the POSIX TZ string CET-1CEST,M3.5.0,M10.5.0/3. `out` becomes "HH:MM",
// 24-hour. The first call installs TZ for the process; later calls are cheap.
bool formatClock(uint32_t epochUtc, char* out, size_t cap);

// The local civil fields of an epoch (the same Europe/Ljubljana TZ as formatClock). weekday is
// 0 = Sunday. The one timestamp formatter (ui/age.h) and the status bar must agree on which day a
// stamp belongs to, so both go through this.
bool localCivil(uint32_t epochUtc, int& year, int& month, int& day, int& hour, int& minute,
                int* weekday = nullptr);

// Local civil date -> UTC epoch with the same TZ rules (used for the build floor only).
bool localEpochFromCivil(int year, int month, int day, int hour, int minute, int second,
                         uint32_t& epochUtc);

// --- policy helpers (also used by the class below) ----------------------------------------------
// The floor may never go backwards: the higher of the saved floor and the build time seeds it.
uint32_t bootEpoch(uint32_t savedFloorUtc, uint32_t buildEpochUtc);
// Raises a floor with an epoch, never lowers it.
uint32_t raisedFloor(uint32_t floorUtc, uint32_t epochUtc);

// One clock and its source bookkeeping. A production instance is behind timeKeeper(); tests create
// local instances with fake get/set functions, so no host clock is ever changed.
class TimeKeeper {
 public:
  using SetEpochFn = void (*)(uint32_t epoch);
  using GetEpochFn = uint32_t (*)();

  static void systemSet(uint32_t epoch);
  static uint32_t systemGet();

  // Seeds the system clock at max(saved floor, build time). realSinceBoot() stays false. uptimeSec
  // is the caller's monotonic seconds (millis()/1000 on the device).
  void begin(uint32_t savedFloorUtc, uint32_t buildEpochUtc, uint32_t uptimeSec,
             SetEpochFn setFn = &TimeKeeper::systemSet,
             GetEpochFn getFn = &TimeKeeper::systemGet);

  // Returns true when a write to the clock (and therefore an NVS floor save) is wanted. A source
  // that is within 1 s of the running clock still refreshes its 6-hour hold, but returns false.
  bool setFromSource(uint32_t epochUtc, TimeSource source, uint32_t uptimeSec);

  // MeshCore's RTCClock::setCurrentTime() path. It writes the system clock if the epoch is sane and
  // not below the floor, but it never marks a source: only setFromSource() may make the clock real.
  void setHardware(uint32_t epochUtc);

  uint32_t now() const { return get_ ? get_() : 0; }
  uint32_t floor() const { return floor_; }
  bool realSinceBoot() const { return real_; }
  TimeSource source() const { return source_; }
  uint32_t sourceAgeSec(uint32_t uptimeSec) const;

  bool saveDue(uint32_t uptimeSec) const;
  void onFloorSaved(uint32_t uptimeSec);

  // A lower source may set the clock only when no higher source has set it in the last 6 hours.
  bool maySet(TimeSource source, uint32_t uptimeSec) const;

 private:
  uint32_t floor_ = 0;
  bool real_ = false;
  bool floorDirty_ = false;
  TimeSource source_ = TimeSource::None;
  bool seen_[5] = {false, false, false, false, false};
  uint32_t seenAt_[5] = {0, 0, 0, 0, 0};
  uint32_t lastSavedUptime_ = 0;
  bool begun_ = false;
  SetEpochFn set_ = &TimeKeeper::systemSet;
  GetEpochFn get_ = &TimeKeeper::systemGet;
};

TimeKeeper& timeKeeper();

}  // namespace om
