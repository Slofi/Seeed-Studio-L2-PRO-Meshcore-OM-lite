// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

namespace om {
// Call every loop(): watches the USB CDC stream for an upload request. It only PARSES an OMCMD1 test
// command into a one-slot buffer (EST-350); it never runs the command.
void otaPoll();
bool otaActive();

// EST-350: run the command that otaPoll() copied, if any. loop() calls this AFTER otaPoll() returns
// (and after history::loop()), so the command's SD/FS work runs on the loop task with the reader's
// frames gone, exactly like the screen's own paths.
void otaCommandPoll();

// EST-405: a monotonic count of the commands otaCommandPoll() has EXECUTED, the same pattern the
// message and DM serials use. The screen auto-off timer reads it so one USB command restarts its
// count and opens its 30 s quiet window. The OMSHOT screenshot does not bump it: it is a frame read,
// not a command.
uint32_t otaCommandSerial();
}  // namespace om
