// SPDX-License-Identifier: MIT
// The L76K constellation mode: ONE `$PCAS04,7` (GPS + BeiDou + GLONASS), written after the module
// has proved it is up (the first checksum-valid NMEA sentence).
//
// Why it waits: the old code wrote the command 250 ms after boot, i.e. while the L76K was still
// starting (the boot log had binary bytes before the first NMEA line) and the module ignored it.
// EST-338's $PCAS03 GSV off/on probe then proved that our TX path (GPIO17) does reach the module
// (GSV stopped for 5 s and came back), so the diagnostic itself is gone (EST-339, Filip: "so far the
// location and time works, so hold on this"). No probe at boot, no mode 5, no mode 3 restore.
//
// What is NOT claimed: that the module ACCEPTS mode 7. EST-338's "GLONASS seen" detector counted an
// empty GLONASS GSA slot (NMEA 4.1 system id 2, no PRN) that was already in the boot log BEFORE any
// command was sent, so it proves nothing. Only a heard GLONASS satellite settles it, which the
// heartbeat's per-constellation expected/heard counts show (`l` above zero).
//
// The module's flash is never written ($PCAS00 is never sent), so this survives a factory reset.
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host compiles it with the system g++.
#pragma once

namespace om {

class GpsModeCommand {
 public:
  // One complete NMEA line whose checksum is valid. Returns true exactly once per module start: the
  // caller then writes `$PCAS04,7` and logs `[gps] mode 7 sent`, once.
  bool onValidSentence(const char* line) {
    if (sent_ || line == nullptr || line[0] != '$') return false;
    sent_ = true;
    return true;
  }

  // gpsBegin() re-opened the UART, which can follow a module restart: the mode may be gone, so arm
  // the command again and wait for the module to talk.
  void onModuleReboot() { sent_ = false; }

  // EST-387: this boot did NOT restart the receiver (a software/OTA/panic/watchdog restart of the
  // ESP32 alone), so its constellation mode is still in force. Mark the command as done for this
  // boot: nothing is sent and nothing is armed, which is the point of keeping the receiver running.
  void onModuleWarmStart() { sent_ = true; }

  bool sent() const { return sent_; }

 private:
  bool sent_ = false;
};

}  // namespace om
