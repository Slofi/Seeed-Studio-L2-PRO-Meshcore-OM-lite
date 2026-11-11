// SPDX-License-Identifier: MIT
// OM Lite timekeeper: one clock (the system clock) fed by ranked sources.
//
// The board has no RTC. MeshCore's VolatileRTCClock used to restart at 15 May 2024 on every boot, so
// every advert we sent looked like a replay to the other nodes. This module replaces that clock with
// the system time (settimeofday/time), which survives a sync and is what MeshCore reads through an
// RTCClock adapter (mesh_link.cpp).
//
// Sources, best first: GPS fix, Wi-Fi NTP, phone over Bluetooth, manual, GPS without a fix (EST-383,
// an approximate clock). A lower source may set the clock only when no higher source has set it in the
// last 6 hours; manual always applies when the user does it, and the next GPS fix (or Wi-Fi/phone)
// takes over from an approximate GPS time immediately. Never set the clock from an advert or a message.
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

// Kept (EST-403) is a memory of a clock that was real before a software restart, not a fresh
// observation. It is deliberately the LOWEST rank (0, below gps-nofix): any fresh GPS fix, Wi-Fi,
// phone, manual or gps-nofix time replaces it immediately, with no 6-hour hold. The original source
// is remembered separately (TimeKeeper::keptFrom()) for the log and the approximate flag.
enum class TimeSource : uint8_t { None = 0, Gps, Wifi, Phone, Manual, GpsNoFix, Kept };

// The number of TimeSource values, indexed 0..kTimeSourceCount-1 (the class below sizes its
// bookkeeping from it).
constexpr int kTimeSourceCount = 7;

const char* timeSourceName(TimeSource s);
// 0 = unknown, 1 = GPS without a fix (approximate), 2 = manual, 3 = phone, 4 = Wi-Fi, 5 = GPS fix.
// Manual is ranked above the approximate GPS time but always applies when the user sets it. A
// no-fix GPS time can never overwrite a fix, Wi-Fi, phone or manual source within the 6-hour hold.
// Kept = 0 (EST-403): a boot memory never blocks a fresh source.
int timeSourcePriority(TimeSource s);

// EST-403: the sources that can set a real clock: every TimeSource except None and Kept. One list
// for the boot record's validation and TimeKeeper::setKept(), so the two cannot disagree.
constexpr bool realTimeSource(TimeSource s) {
  return s == TimeSource::Gps || s == TimeSource::Wifi || s == TimeSource::Phone ||
         s == TimeSource::Manual || s == TimeSource::GpsNoFix;
}

constexpr uint32_t kTimeSourceHoldSec = 6UL * 60UL * 60UL;   // 6 hours
constexpr uint32_t kFloorSaveIntervalSec = 10UL * 60UL;      // 10 minutes
constexpr uint32_t kMinSaneEpoch = 1735689600UL;             // 2025-01-01 00:00:00 UTC
// EST-397: the top of the window is INT32_MAX (2038-01-19 03:14:07 UTC), NOT 2100. On this
// toolchain time_t is 32 bits and signed, so every epoch above INT32_MAX turns negative in the
// (time_t) cast and settimeofday() would put the clock into 1901. One bound for every source: this
// constant feeds saneEpoch(), TimeKeeper::systemSet(), the NTP capture (ntp_plan.h) and the shell.
constexpr uint32_t kMaxSaneEpoch = 2147483647UL;             // INT32_MAX = 2038-01-19 03:14:07 UTC

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

// EST-400: the UTC civil year an epoch falls in. A constexpr walk over whole years (the epoch is a
// uint32, so it ends at 2106 at worst), used only to derive the UI's limits from the clock's one
// sanity window. No second copy of "2038" or "2025" exists anywhere.
constexpr int civilYearOfEpoch(uint32_t epoch) {
  int64_t days = (int64_t)(epoch / 86400UL);
  int year = 1970;
  while (true) {
    const int64_t yearDays = isLeapYear(year) ? 366 : 365;
    if (days < yearDays) return year;
    days -= yearDays;
    ++year;
  }
}

// The manual-time steppers' real range: the civil years kMinSaneEpoch..kMaxSaneEpoch can name
// (2025..2038). The clock refuses anything outside that window, so offering 2000..2099 was a range
// the Set key could never honour.
constexpr int kMinSaneYear = civilYearOfEpoch(kMinSaneEpoch);   // 2025
constexpr int kMaxSaneYear = civilYearOfEpoch(kMaxSaneEpoch);   // 2038

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
  // EST-383: RMC with status A is a position fix (trusted). ZDA and a void RMC are the no-fix
  // path, which only counts after NoFixTimeGate has accepted it.
  bool hasFix = false;
};

// Parses one complete $..RMC or $..ZDA sentence (checksum included). RMC with status A needs a sane
// date; ZDA needs a sane date AND a year the build can vouch for (see kZdaYearsAhead). A void RMC is
// rejected unless allowNoFix is true, and then its date must pass the same build-year gate: without
// a fix the module can still be sending its default date. Returns ok=false for a bad checksum, a
// split/truncated line, or an impossible date.
ParsedTime parseNmeaTime(const char* sentence, bool allowNoFix = false);

// The year this firmware was compiled in, taken from this file's __DATE__. 0 if it cannot be read.
int buildYear();

// ZDA carries no validity field, so a receiver that has never seen a satellite can still send its
// default date and there is nothing in the sentence to say so. A ZDA year outside
// build year .. build year + 1 is therefore refused (a device flashed in December is still running
// in January, hence the one year of slack).
constexpr int kZdaYearsAhead = 1;

// EST-383: "two consecutive sentences advance by 1 s (+-1 s)". One second is the sentence period,
// two seconds tolerate one missed line; 1..2 s is the only accepted step. A step of 0 is a frozen
// clock and a larger jump is a restart or a default, so both are refused. The first sentence in a
// stream is never accepted (there is nothing to compare it with) and a reset() starts a new stream.
class NoFixTimeGate {
 public:
  void reset();
  // Offers one parsed RMC/ZDA time. `satsTracked` is the ticket's third condition (at least one
  // satellite with a signal); a sentence offered while nothing is tracked still moves the chain on,
  // it just cannot be accepted. True only when this sentence may set the clock without a fix.
  bool offer(uint32_t epochUtc, bool satsTracked);
  bool haveLast() const { return have_; }
  uint32_t last() const { return last_; }

 private:
  bool have_ = false;
  uint32_t last_ = 0;
};

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
  // EST-403: called with every accepted real source, so the device can refresh its RTC_NOINIT
  // record. Plain function pointer: the host tests pass a fake and timekeeper.cpp stays SDK-free.
  using KeepFn = void (*)(uint32_t epochUtc, TimeSource source);

  static void systemSet(uint32_t epoch);
  static uint32_t systemGet();

  // Seeds the system clock at max(saved floor, build time). realSinceBoot() stays false. uptimeSec
  // is the caller's monotonic seconds (millis()/1000 on the device). keepFn (optional) is called
  // after every accepted real source, including the same-second refreshes that return false.
  void begin(uint32_t savedFloorUtc, uint32_t buildEpochUtc, uint32_t uptimeSec,
             SetEpochFn setFn = &TimeKeeper::systemSet,
             GetEpochFn getFn = &TimeKeeper::systemGet,
             KeepFn keepFn = nullptr);

  // Returns true when a write to the clock (and therefore an NVS floor save) is wanted. A source
  // that is within 1 s of the running clock still refreshes its 6-hour hold, but returns false.
  bool setFromSource(uint32_t epochUtc, TimeSource source, uint32_t uptimeSec);

  // EST-403: the boot restore. Makes the clock real at once with source `kept`, remembering the
  // source the record came from. Refused for an insane epoch, an invalid original source, or an
  // epoch below the floor. Only the clock_keep boot decision may call this; a fresh source still
  // goes through setFromSource() and replaces it immediately.
  bool setKept(uint32_t epochUtc, TimeSource original, uint32_t uptimeSec);

  // MeshCore's RTCClock::setCurrentTime() path. It writes the system clock if the epoch is sane and
  // not below the floor, but it never marks a source: only setFromSource() may make the clock real.
  void setHardware(uint32_t epochUtc);

  uint32_t now() const { return get_ ? get_() : 0; }
  uint32_t floor() const { return floor_; }
  bool begun() const { return begun_; }
  bool realSinceBoot() const { return real_; }
  TimeSource source() const { return source_; }
  // EST-383: the clock was set from GPS without a position fix, so it is real enough to show but
  // approximate. Any fix, Wi-Fi, phone or manual source replaces it as soon as it arrives.
  // EST-403: a clock kept from a gps-nofix record is still approximate.
  bool approximate() const {
    return source_ == TimeSource::GpsNoFix ||
           (source_ == TimeSource::Kept && keptFrom_ == TimeSource::GpsNoFix);
  }
  uint32_t sourceAgeSec(uint32_t uptimeSec) const;

  // EST-403: the source a `kept` clock was restored from; None when the clock is not kept. The log
  // line and the record written before the next restart use this, never `kept` itself.
  TimeSource keptFrom() const { return source_ == TimeSource::Kept ? keptFrom_ : TimeSource::None; }

  bool saveDue(uint32_t uptimeSec) const;
  void onFloorSaved(uint32_t uptimeSec);

  // A lower source may set the clock only when no higher source has set it in the last 6 hours.
  bool maySet(TimeSource source, uint32_t uptimeSec) const;

 private:
  uint32_t floor_ = 0;
  bool real_ = false;
  bool floorDirty_ = false;
  TimeSource source_ = TimeSource::None;
  TimeSource keptFrom_ = TimeSource::None;   // EST-403: original source behind `kept`
  bool seen_[kTimeSourceCount] = {};
  uint32_t seenAt_[kTimeSourceCount] = {};
  uint32_t lastSavedUptime_ = 0;
  bool begun_ = false;
  SetEpochFn set_ = &TimeKeeper::systemSet;
  GetEpochFn get_ = &TimeKeeper::systemGet;
  KeepFn keep_ = nullptr;                     // EST-403: RTC_NOINIT record refresh
};

TimeKeeper& timeKeeper();

}  // namespace om
