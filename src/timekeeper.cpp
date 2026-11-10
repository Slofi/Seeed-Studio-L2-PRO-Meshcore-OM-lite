// SPDX-License-Identifier: MIT
// OM Lite timekeeper: NMEA time parsing, the source ranking, the floor and the Ljubljana clock.
//
// Plain C++ plus POSIX time calls; there is no Arduino in this file, so test/host links it with the
// system g++ and runs the real logic on the build machine.
#define _POSIX_C_SOURCE 200809L

#include "timekeeper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

namespace om {
namespace {

const char* const kLjubljanaTz = "CET-1CEST,M3.5.0,M10.5.0/3";
bool s_tzReady = false;

void ensureLjubljanaTz() {
  if (s_tzReady) return;
  // POSIX TZ: CET in winter (UTC+1), CEST in summer (UTC+2), from the last Sunday in March at
  // 02:00 local to the last Sunday in October at 03:00 local. formatClock() and the build-time
  // parser both use libc's localtime/mktime with this rule, so they cannot disagree.
  setenv("TZ", kLjubljanaTz, 1);
  tzset();
  s_tzReady = true;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

int monthFromAbbrev(const char* mon) {
  static const char* const kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (int i = 0; i < 12; ++i) {
    if (strncmp(mon, kMonths[i], 3) == 0) return i + 1;
  }
  return 0;
}

// Two decimal digits at p; -1 when either is not a digit.
int twoDigits(const char* p) {
  if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
  return (p[0] - '0') * 10 + (p[1] - '0');
}

}  // namespace

const char* timeSourceName(TimeSource s) {
  switch (s) {
    case TimeSource::Gps: return "GPS";
    case TimeSource::Wifi: return "Wi-Fi";
    case TimeSource::Phone: return "Phone";
    case TimeSource::Manual: return "Manual";
    default: return "None";
  }
}

int timeSourcePriority(TimeSource s) {
  switch (s) {
    case TimeSource::Gps: return 4;
    case TimeSource::Wifi: return 3;
    case TimeSource::Phone: return 2;
    case TimeSource::Manual: return 1;
    default: return 0;
  }
}

bool parseNmeaClock(const char* field, int& hour, int& minute, int& second) {
  if (!field) return false;
  if (strlen(field) < 6) return false;
  for (int i = 0; i < 6; ++i) {
    if (field[i] < '0' || field[i] > '9') return false;
  }
  hour = twoDigits(field);
  minute = twoDigits(field + 2);
  second = twoDigits(field + 4);
  return hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && second >= 0 && second <= 60;
}

ParsedTime parseNmeaTime(const char* sentence) {
  ParsedTime out;
  if (!sentence) return out;
  const char* dollar = strchr(sentence, '$');
  if (!dollar) return out;
  const char* star = strchr(dollar, '*');
  if (!star || star - dollar < 8) return out;
  if (hexValue(star[1]) < 0 || hexValue(star[2]) < 0) return out;

  uint8_t sum = 0;
  for (const char* p = dollar + 1; p < star; ++p) sum ^= (uint8_t)*p;
  const int want = hexValue(star[1]) * 16 + hexValue(star[2]);
  if ((int)sum != want) return out;

  const size_t bodyLen = (size_t)(star - dollar - 1);
  if (bodyLen == 0 || bodyLen >= 120) return out;
  char body[120];
  memcpy(body, dollar + 1, bodyLen);
  body[bodyLen] = 0;

  char* fields[20] = {nullptr};
  int n = 0;
  char* p = body;
  while (p && n < 20) {
    fields[n++] = p;
    char* comma = strchr(p, ',');
    if (!comma) break;
    *comma = 0;
    p = comma + 1;
  }
  if (n < 2) return out;

  const char* type = fields[0];
  const size_t typeLen = strlen(type);
  if (typeLen < 3) return out;
  const char* tail = type + typeLen - 3;

  if (strcmp(tail, "RMC") == 0) {
    // $..RMC,hhmmss.ss,A,lat,N,lon,E,...,ddmmyy,...
    if (n < 10) return out;
    if (fields[2][0] != 'A') return out;   // V = void: no fix, no date worth trusting
    int hh = 0, mm = 0, ss = 0;
    if (!parseNmeaClock(fields[1], hh, mm, ss)) return out;
    const char* date = fields[9];
    if (strlen(date) < 6) return out;
    const int day = twoDigits(date);
    const int mon = twoDigits(date + 2);
    const int yy = twoDigits(date + 4);
    if (day < 0 || mon < 0 || yy < 0) return out;
    const int year = 2000 + yy;
    if (!validCivil(year, mon, day, hh, mm, ss)) return out;
    const uint32_t epoch = makeUtcEpoch(year, mon, day, hh, mm, ss);
    if (!saneEpoch(epoch)) return out;
    out.ok = true;
    out.hasDate = true;
    out.epoch = epoch;
    return out;
  }

  if (strcmp(tail, "ZDA") == 0) {
    // $..ZDA,hhmmss.ss,dd,mm,yyyy,...
    if (n < 5) return out;
    int hh = 0, mm = 0, ss = 0;
    if (!parseNmeaClock(fields[1], hh, mm, ss)) return out;
    if (strlen(fields[2]) < 2 || strlen(fields[3]) < 2 || strlen(fields[4]) < 4) return out;
    const int day = twoDigits(fields[2]);
    const int mon = twoDigits(fields[3]);
    const int year = twoDigits(fields[4]) * 100 + twoDigits(fields[4] + 2);
    if (day < 0 || mon < 0 || year < 0) return out;
    if (!validCivil(year, mon, day, hh, mm, ss)) return out;
    const uint32_t epoch = makeUtcEpoch(year, mon, day, hh, mm, ss);
    if (!saneEpoch(epoch)) return out;
    out.ok = true;
    out.hasDate = true;
    out.epoch = epoch;
    return out;
  }

  return out;
}

bool localEpochFromCivil(int year, int month, int day, int hour, int minute, int second,
                         uint32_t& epochUtc) {
  if (!validCivil(year, month, day, hour, minute, second)) return false;
  ensureLjubljanaTz();
  struct tm tmv;
  memset(&tmv, 0, sizeof(tmv));
  tmv.tm_year = year - 1900;
  tmv.tm_mon = month - 1;
  tmv.tm_mday = day;
  tmv.tm_hour = hour;
  tmv.tm_min = minute;
  tmv.tm_sec = second;
  tmv.tm_isdst = -1;   // let libc decide CET/CEST
  const time_t t = mktime(&tmv);
  if (t == (time_t)-1) return false;
  epochUtc = (uint32_t)t;
  return true;
}

bool parseBuildTimestamp(const char* date, const char* time, uint32_t& epochUtc) {
  if (!date || !time) return false;
  char mon[8] = {0};
  int day = 0, year = 0, hour = 0, minute = 0, second = 0;
  if (sscanf(date, "%7s %d %d", mon, &day, &year) != 3) return false;
  if (sscanf(time, "%d:%d:%d", &hour, &minute, &second) != 3) return false;
  const int month = monthFromAbbrev(mon);
  if (month == 0) return false;
  return localEpochFromCivil(year, month, day, hour, minute, second, epochUtc);
}

bool formatClock(uint32_t epochUtc, char* out, size_t cap) {
  if (!out || cap < 6) return false;
  ensureLjubljanaTz();
  const time_t t = (time_t)epochUtc;
  struct tm tmv;
  if (localtime_r(&t, &tmv) == nullptr) return false;
  snprintf(out, cap, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  return true;
}

uint32_t bootEpoch(uint32_t savedFloorUtc, uint32_t buildEpochUtc) {
  const uint32_t saved = saneEpoch(savedFloorUtc) ? savedFloorUtc : 0;
  const uint32_t build = saneEpoch(buildEpochUtc) ? buildEpochUtc : 0;
  const uint32_t floor = saved > build ? saved : build;
  return floor ? floor : kMinSaneEpoch;
}

uint32_t raisedFloor(uint32_t floorUtc, uint32_t epochUtc) {
  if (!saneEpoch(epochUtc)) return floorUtc;
  return epochUtc > floorUtc ? epochUtc : floorUtc;
}

void TimeKeeper::systemSet(uint32_t epoch) {
  struct timeval tv;
  tv.tv_sec = (time_t)epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

uint32_t TimeKeeper::systemGet() { return (uint32_t)time(nullptr); }

void TimeKeeper::begin(uint32_t savedFloorUtc, uint32_t buildEpochUtc, uint32_t uptimeSec,
                       SetEpochFn setFn, GetEpochFn getFn) {
  set_ = setFn ? setFn : &TimeKeeper::systemSet;
  get_ = getFn ? getFn : &TimeKeeper::systemGet;
  floor_ = bootEpoch(savedFloorUtc, buildEpochUtc);
  real_ = false;
  floorDirty_ = false;
  source_ = TimeSource::None;
  for (int i = 0; i < 5; ++i) {
    seen_[i] = false;
    seenAt_[i] = 0;
  }
  lastSavedUptime_ = uptimeSec;
  begun_ = true;
  set_(floor_);
}

bool TimeKeeper::maySet(TimeSource source, uint32_t uptimeSec) const {
  if (source == TimeSource::None) return false;
  if (source == TimeSource::Manual) return true;   // the user did it now
  const int incoming = timeSourcePriority(source);
  for (int i = 1; i <= 4; ++i) {
    const TimeSource other = (TimeSource)i;
    if (timeSourcePriority(other) <= incoming) continue;   // equal or lower does not block
    if (!seen_[i]) continue;
    if ((uint32_t)(uptimeSec - seenAt_[i]) < kTimeSourceHoldSec) return false;
  }
  return true;
}

bool TimeKeeper::setFromSource(uint32_t epochUtc, TimeSource source, uint32_t uptimeSec) {
  if (!begun_ || source == TimeSource::None) return false;
  if (!saneEpoch(epochUtc)) return false;
  if (!maySet(source, uptimeSec)) return false;
  // The floor is a one-way ratchet. A real source below it is not usable: applying it would move
  // MeshCore timestamps backwards, which is worse than waiting for the next fix.
  if (epochUtc < floor_) return false;

  const uint32_t current = now();
  const bool firstReal = !real_;
  seen_[(int)source] = true;
  seenAt_[(int)source] = uptimeSec;
  source_ = source;
  if (epochUtc > floor_) floor_ = epochUtc;
  real_ = true;

  const bool near = epochUtc <= current + 1U && current <= epochUtc + 1U;
  // A refresh within the same second does not need a write, but the FIRST real source does: the
  // status bar becomes honest and the floor should survive a reboot.
  if (near && source != TimeSource::Manual && !firstReal) return false;
  if (epochUtc != current) set_(epochUtc);
  floorDirty_ = true;   // an applied sync is saved immediately by the caller
  return true;
}

void TimeKeeper::setHardware(uint32_t epochUtc) {
  if (!begun_ || !saneEpoch(epochUtc)) return;
  const uint32_t effective = epochUtc < floor_ ? floor_ : epochUtc;
  if (effective > floor_) {
    floor_ = effective;
    floorDirty_ = true;
  }
  if (effective != now()) set_(effective);
  // Deliberately not real_: this path is MeshCore plumbing, not a trusted source.
}

uint32_t TimeKeeper::sourceAgeSec(uint32_t uptimeSec) const {
  if (source_ == TimeSource::None || !seen_[(int)source_]) return 0;
  return (uint32_t)(uptimeSec - seenAt_[(int)source_]);
}

bool TimeKeeper::saveDue(uint32_t uptimeSec) const {
  return begun_ &&
         (floorDirty_ || (uint32_t)(uptimeSec - lastSavedUptime_) >= kFloorSaveIntervalSec);
}

void TimeKeeper::onFloorSaved(uint32_t uptimeSec) {
  lastSavedUptime_ = uptimeSec;
  floorDirty_ = false;
}

TimeKeeper& timeKeeper() {
  static TimeKeeper k;   // function-local: no global constructor runs before USB CDC exists
  return k;
}

}  // namespace om
