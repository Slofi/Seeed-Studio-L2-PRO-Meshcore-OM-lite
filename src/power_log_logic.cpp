// SPDX-License-Identifier: MIT
// EST-377: the CSV line, the file name and the append buffer. See power_log_logic.h.
#include "power_log_logic.h"

#include <stdio.h>

#include "timekeeper.h"

namespace om {
namespace plog {

// EST-413: one gate for every reader of the log's clock. The floor is a lower bound, not a time.
bool logLocalStamp(uint32_t epochUtc, bool clockRealSinceBoot, LocalStamp& out) {
  if (!clockRealSinceBoot) return false;
  int year = 0, month = 0, day = 0, hour = 0, minute = 0;
  if (!om::localCivil(epochUtc, year, month, day, hour, minute)) return false;
  out.year = year;
  out.month = month;
  out.day = day;
  out.hour = hour;
  out.minute = minute;
  return true;
}

int formatLine(const Sample& s, const LocalStamp& t, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return -1;
  // EST-418: `heard` is the LAST field, after ain3; every earlier field keeps its position.
  const int n = snprintf(out, cap,
                         "%04d-%02d-%02d %02d:%02d,%u,%d,%u,%d,%d,%u,%d,%d,%lu,%d,%lu,%d,%u,%u,%u,%u\n",
                         t.year, t.month, t.day, t.hour, t.minute, (unsigned)s.battMv,
                         s.screenOn ? 1 : 0, (unsigned)s.brightness, s.gpsRailOn ? 1 : 0,
                         s.fix ? 1 : 0, (unsigned)s.sats, s.wifiOn ? 1 : 0, s.bleOn ? 1 : 0,
                         (unsigned long)s.packets, s.noiseDbm, (unsigned long)s.uptimeSec,
                         (int)s.vbus, (unsigned)s.ain1Mv, (unsigned)s.ain2Mv, (unsigned)s.ain3Mv,
                         (unsigned)s.heard);
  if (n < 0 || (size_t)n >= cap) return -1;
  return n;
}

int formatFileName(const LocalStamp& t, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return -1;
  const int n = snprintf(out, cap, "/log/power-%04d%02d%02d.csv", t.year, t.month, t.day);
  if (n < 0 || (size_t)n >= cap) return -1;
  return n;
}

int formatAppend(const Sample& s, const LocalStamp& t, bool newFile, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return -1;
  int at = 0;
  if (newFile) {
    const int n = snprintf(out, cap, "%s\n", kPowerLogHeader);
    if (n < 0 || (size_t)n >= cap) return -1;
    at = n;
  }
  const int n = formatLine(s, t, out + at, cap - (size_t)at);
  if (n < 0) return -1;
  return at + n;
}

}  // namespace plog
}  // namespace om
