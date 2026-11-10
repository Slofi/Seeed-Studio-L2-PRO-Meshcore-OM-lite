// SPDX-License-Identifier: MIT
#pragma once

namespace om {
// Call every loop(): watches the USB CDC stream for an upload request. It only PARSES an OMCMD1 test
// command into a one-slot buffer (EST-350); it never runs the command.
void otaPoll();
bool otaActive();

// EST-350: run the command that otaPoll() copied, if any. loop() calls this AFTER otaPoll() returns
// (and after history::loop()), so the command's SD/FS work runs on the loop task with the reader's
// frames gone, exactly like the screen's own paths.
void otaCommandPoll();
}  // namespace om
