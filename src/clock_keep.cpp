// SPDX-License-Identifier: MIT
// EST-403: the device half of the clock that survives a warm restart. The decision table and the
// record layout are in clock_keep.h (plain C++, host-tested); this file only talks to the SDK.
//
// The record lives in RTC_NOINIT memory: the same region crash_report.cpp's checkpoint uses, kept
// through esp_restart()/OTA/panic/watchdog and lost at a real power-on or brown-out.
#include "clock_keep.h"

#include <stdint.h>

#include <esp_attr.h>
#include <esp_system.h>

#include "timekeeper.h"

// esp_rtc_get_time_us(): the RTC_CNTL 48-bit counter, converted to microseconds. It is declared in
// the framework's esp_hw_support/include/soc/esp32s3/rtc.h; declared here directly because the
// Arduino build's include order resolves "soc/rtc.h" to the low-level soc header instead. The
// counter keeps counting through a software restart (it is in the RTC domain) and restarts from 0
// only when that domain loses power, which is exactly the elapsed time the restored epoch needs.
extern "C" uint64_t esp_rtc_get_time_us(void);

namespace om {
namespace {

RTC_NOINIT_ATTR ClockKeepRecord s_clockKeep;

// Write the fields first, the magic last: a panic or a power cut in the middle leaves magic = 0 and
// the next boot refuses the half-written record instead of mixing a new epoch with an old RTC time.
void storeRecord(uint32_t epochUtc, TimeSource source, bool real) {
  s_clockKeep.magic = 0;
  __asm__ __volatile__("" ::: "memory");
  s_clockKeep.epoch = epochUtc;
  s_clockKeep.rtcUs = esp_rtc_get_time_us();
  s_clockKeep.source = (uint8_t)source;
  s_clockKeep.real = real ? 1 : 0;
  s_clockKeep.reserved = 0;
  s_clockKeep.reserved2 = 0;
  __asm__ __volatile__("" ::: "memory");
  // A real record must name one of the five sources; otherwise magic stays 0 and the next boot
  // refuses it. A not-real save is written with real = 0, which clockKeepDecide() also refuses.
  if (real && !clockKeepSourceValid(source)) return;
  s_clockKeep.magic = kClockKeepMagic;
}

}  // namespace

ClockKeepBoot clockKeepBegin(uint32_t floorUtc) {
  const GpsBootReason reason = gpsBootReasonOf((int)esp_reset_reason());
  const ClockKeepBoot out = clockKeepDecide(reason, s_clockKeep, esp_rtc_get_time_us(), floorUtc);
  // A reset boot has no trustworthy history. Clearing the record here means a later crash/software
  // restart inside this power session cannot resurrect a record written before it.
  if (gpsBootResetsReceiver(reason)) s_clockKeep.magic = 0;
  return out;
}

void clockKeepStore(uint32_t epochUtc, TimeSource source) {
  storeRecord(epochUtc, source, true);
}

void clockKeepStoreNow() {
  TimeKeeper& tk = timeKeeper();
  // A restart before begin() has no clock to record; leave the previous record untouched rather
  // than overwrite it with a not-real time.
  if (!tk.begun()) return;
  const TimeSource source = tk.source() == TimeSource::Kept ? tk.keptFrom() : tk.source();
  storeRecord(tk.now(), source, tk.realSinceBoot());
}

}  // namespace om
