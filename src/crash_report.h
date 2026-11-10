// SPDX-License-Identifier: MIT
// Boot crash reporter (EST-341).
//
// The device has ARDUINO_USB_CDC_ON_BOOT, so a panic prints on UART0 and never reaches the USB
// log. Two things survive a reset and are printed here before anything else can crash:
//   * the stored core dump (the SDK writes it to the `coredump` partition at 0xFF0000),
//   * an RTC_NOINIT checkpoint naming the last `[mc] N ...` init step the previous boot reached.
#pragma once

namespace om {

// Call once at the very start of setup(), after Serial.begin() and before boardBegin().
// Prints the previous boot's ending, its last checkpoint and one core-dump summary line; then
// erases a dump it managed to print, so the block appears exactly once.
void crashReportBegin();

// Records how far init got. Cheap (a short copy into RTC_NOINIT RAM), safe on any boot.
void crashCheckpoint(const char* step);

}  // namespace om
