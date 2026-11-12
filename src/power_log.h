// SPDX-License-Identifier: MIT
// EST-377: the battery log's device half. One CSV line per minute on the SD card, and the saved
// on/off switch behind `log power on|off`.
//
// The loop is the ONLY writer (EST-349): the card is touched from plog::loop(), never from the USB
// command stack. `log power tail` reads the day's file in the command handler, which EST-350 runs on
// the loop task after otaPoll() has returned, with a static buffer.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "power_log_logic.h"

namespace om {
namespace plog {

// What the shell gathers for one line. Radio numbers (packets heard, noise floor) are read by the
// logger itself, only when a line is due, so the 5 s mesh stats' SPI read never sits in every pass.
struct State {
  uint16_t battMv = 0;     // cell mV, 0 = the read failed
  bool screenOn = false;
  uint8_t brightness = 0;  // the backlight level actually applied
  bool gpsRailOn = false;  // the GNSS power rail (board::gpsRailOn)
  bool fix = false;
  uint8_t sats = 0;        // the satellites USED in the fix (GGA)
  uint8_t heard = 0;       // EST-418: the satellites HEARD (GSV), the heartbeat's `heard=`
  bool wifiOn = false;
  bool bleOn = false;
  uint32_t uptimeSec = 0;
};

// Boot, after Preferences is open. Loads the saved switch (missing key = ON, not written) and arms
// the minute gate. No SD access here.
void begin(PowerLogPref::ReadFn read, PowerLogPref::WriteFn write, void* ctx);

bool enabled();
// `log power on|off`: flips the RAM value and writes the NVS key. False when the write failed.
bool setEnabled(bool on);

// Once per loop pass, from loop(): the only card writer. Writes when the minute is due (and the
// switch is on) and prints one `[plog] write ...` line with the path, bytes and the line's values.
// EST-401: when a line is due it also reads STATUS0 (VBUSOK) and the ADS1115's AIN1..AIN3, the four
// appended columns; that is ~55 ms of I2C, once a minute, on this task like the radio counters.
void loop(uint32_t nowMs, const State& st);

// EST-380: card errors since boot (the /log directory, the open, a short write). A failed minute is
// no longer only a serial line: `log power tail` prints this number, so a lost battery log is
// visible in the same check as the CSV itself.
uint32_t writeFailures();

// EST-413: the due minutes that wrote no row because the clock was not real since boot (the
// timekeeper's floor is a lower bound, not a time). The first row after a real source is written
// normally; `log power tail` reports this count, so a log that starts late says why.
uint32_t skippedMinutes();

// `log power tail`: prints the last 5 lines of today's file (the header counts as a line). Returns
// the number printed, 0 for an empty file, or -1 when there is no card, no real clock or no file.
// EST-413: a not-real clock never names a file, so a floor date cannot pick another day's run.
int printTail();

}  // namespace plog
}  // namespace om
