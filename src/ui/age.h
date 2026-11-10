// SPDX-License-Identifier: MIT
// The one timestamp formatter: every "when did this happen" on the glass goes through it.
//
// Filip (2026-11-10): "time instead of 'heard ago' is always preferred, it gives better info". So a
// stamp that is known to be real is shown as local time (Europe/Ljubljana):
//   today                 HH:MM                 (5 mono glyphs, the chat line's fixed column)
//   within the last week  Mon 14:02             (DayTime only; Clock uses the date alone, 5 glyphs)
//   older                 10.11. 14:02          (DayTime only; Clock uses the date alone, 5 glyphs)
// An age is used ONLY when the clock was not real at that moment (a message from before the first
// GPS fix, a contact from the old 2024 clock): "now", "5m", "3h", "12d", "30d+".
//
// The fixed cut-off is kMinSaneEpoch = 2025-01-01 00:00 UTC. The old VolatileRTCClock started at
// 15 May 2024 plus uptime, and any stamp in the 2024 era is that clock, never a real time. The old
// rule compared with the BUILD time, which moves forward at every flash, so every node heard before
// the latest flash turned into "--" (EST-333 bug, seen on the device 2026-11-10 09:49). One constant,
// used here and by the Nodes "in the last hour" count.
//
// Plain C++ (POSIX time only), so test/host proves it with the system g++.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "timekeeper.h"

namespace om {
namespace ui {

enum class StampForm : uint8_t {
  Clock,     // a narrow column: HH:MM today, dd.mm otherwise (exactly five mono glyphs)
  DayTime,   // HH:MM today, "Mon 14:02" this week, "10.11. 14:02" older
};

// One event's two clocks. wall is the system clock at that moment (0 = unknown), uptime is the
// monotonic seconds at that moment (0 = unknown), wallReal says the clock was a real source then.
struct Stamp {
  uint32_t wall = 0;
  uint32_t uptime = 0;
  bool wallReal = false;
};

// A stamp is real when it is after the fixed 2024-clock cut-off (and inside the sane range). This is
// the one rule the formatter and the last-hour count share.
constexpr bool stampIsReal(uint32_t epoch) {
  return epoch >= kMinSaneEpoch && epoch < kMaxSaneEpoch;
}

// Writes the time, or the age when the time would be invented. `nowWall` is the current system
// clock, `nowUptime` millis()/1000, `nowReal` timeKeeper().realSinceBoot(). Never writes more than
// cap-1 bytes plus the terminator.
void stampText(char* out, size_t cap, const Stamp& s, uint32_t nowWall, uint32_t nowUptime,
               bool nowReal, StampForm form);

}  // namespace ui
}  // namespace om
