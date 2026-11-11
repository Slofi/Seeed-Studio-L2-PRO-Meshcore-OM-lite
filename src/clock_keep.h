// SPDX-License-Identifier: MIT
// EST-403: keeping a real clock across a warm restart.
//
// The board has no battery-backed RTC. The ESP32-S3's RTC counter (RTC_CNTL, slow clock) keeps
// counting through a software restart (OTA, ESP.restart, panic, watchdog) and stops only when the
// RTC domain loses power (a real power-on or a brown-out), but the WALL CLOCK does not survive: on
// this framework (Arduino 2.0.17 / IDF 4.4.7) the epoch lives in `s_microseconds_offset` in
// `.dram0.bss` (checked in the built firmware.elf: 0x3fcb5830), and `esp_time_impl_init()` ->
// `esp_set_time_from_rtc()` re-derives it from `esp_rtc_get_time_us()` on every boot. That makes
// gettimeofday() return the RTC counter (time since the RTC domain powered up), not the epoch.
//
// So this header keeps the epoch itself: a small record in RTC_NOINIT memory (clock_keep.cpp), and
// the pure boot decision here, which test/host compiles with the system g++.
//
// The decision is the SAME reset-reason table as the GPS warm boot (gps_boot.h): a KEEP reason plus
// a valid record may mark the clock real; a RESET reason behaves exactly as before. The record also
// carries the RTC counter at the save, so the epoch is advanced by the RTC time that really elapsed
// across the restart (that counter is the one clock that did survive). Nothing is ever invented: a
// bad magic, a non-real record, an insane epoch, an RTC counter that went backwards, or a result
// below the saved floor all leave the clock at "no time".
#pragma once

#include <stdint.h>

#include "gps_boot.h"
#include "timekeeper.h"

namespace om {

// "OMCL" as the four bytes in RTC_NOINIT memory (little-endian, so 0x4C434D4F). A power-on leaves
// random bytes there, so the magic is the first gate.
constexpr uint32_t kClockKeepMagic = 0x4C434D4FUL;

// The record is stored as one block in RTC_NOINIT memory. Do not reorder or resize a field without
// bumping the magic: an image upgrade may start on a record written by the previous image.
struct ClockKeepRecord {
  uint32_t magic;      // kClockKeepMagic, written LAST so a torn save is never trusted
  uint32_t epoch;      // the wall clock (UTC seconds) when the record was written
  uint64_t rtcUs;      // esp_rtc_get_time_us() at the same moment; survives a software restart
  uint8_t source;      // the TimeSource that had set the clock (never None/Kept)
  uint8_t real;        // 1 = the system time was real when the record was written
  uint16_t reserved;   // 0
  uint32_t reserved2;  // 0; fills the struct, so no uninitialized tail padding is ever written
};
static_assert(sizeof(ClockKeepRecord) == 24, "the RTC_NOINIT layout is part of the magic");

// What the boot may do with the record.
struct ClockKeepBoot {
  bool kept = false;
  uint32_t epoch = 0;                // the reconstructed wall clock
  TimeSource source = TimeSource::None;   // the original source, for `[time] ... (was <source>)`
};

// True for the five sources a record may name. `kept` itself is a boot memory, never a record
// source: a record written after a kept boot stores the source it was kept from. One list with
// TimeKeeper::setKept() (realTimeSource, timekeeper.h).
inline bool clockKeepSourceValid(TimeSource source) { return realTimeSource(source); }

// The ONE boot decision. `rtcNowUs` is esp_rtc_get_time_us() on this boot; `floorUtc` is the
// timekeeper's floor after begin() (max(saved NVS floor, build time)). Pure: no SDK, no globals,
// so test/host compiles it with the system g++.
inline ClockKeepBoot clockKeepDecide(GpsBootReason reason, const ClockKeepRecord& record,
                                     uint64_t rtcNowUs, uint32_t floorUtc) {
  ClockKeepBoot out;
  // The SAME table the GPS warm boot uses (EST-387): only a software/OTA/panic/watchdog restart
  // keeps anything. A power-on, brown-out, deep sleep or unknown reason is a cold start.
  if (gpsBootResetsReceiver(reason)) return out;
  if (record.magic != kClockKeepMagic) return out;
  if (record.real != 1) return out;
  if (!clockKeepSourceValid((TimeSource)record.source)) return out;
  if (!saneEpoch(record.epoch)) return out;
  // The counter went backwards: the RTC domain restarted under a record that somehow survived.
  // That is a cold start, not elapsed time.
  if (rtcNowUs < record.rtcUs) return out;

  const uint64_t elapsedUs = rtcNowUs - record.rtcUs;
  const uint64_t elapsedSec = elapsedUs / 1000000ULL;
  // Guard the add and the cast: a value at or above kMaxSaneEpoch is outside the clock's window
  // anyway, so no need to reach the (uint32_t) cast.
  if (elapsedSec > (uint64_t)kMaxSaneEpoch) return out;
  const uint64_t restored = (uint64_t)record.epoch + elapsedSec;
  if (restored >= (uint64_t)kMaxSaneEpoch) return out;
  if (!saneEpoch((uint32_t)restored)) return out;
  // The NVS floor is a lower bound for every source. Below it, stay at "no time" (today's boot).
  if ((uint32_t)restored < floorUtc) return out;

  out.kept = true;
  out.epoch = (uint32_t)restored;
  out.source = (TimeSource)record.source;
  return out;
}

// --- device glue (clock_keep.cpp); the host test compiles the two inline functions above ---------
// Reads the SDK reset reason and the RTC_NOINIT record, decides, and clears the record on a RESET
// boot (so a pre-power-off record can never be trusted by a later crash restart).
ClockKeepBoot clockKeepBegin(uint32_t floorUtc);

// TimeKeeper's KeepFn: writes the record after every accepted real source. Always real = 1.
void clockKeepStore(uint32_t epochUtc, TimeSource source);

// Writes the record just before a restart path we control. Real = timeKeeper().realSinceBoot();
// a not-real clock is stored as real = 0 so the next boot refuses it.
void clockKeepStoreNow();

}  // namespace om
