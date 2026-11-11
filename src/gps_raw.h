// SPDX-License-Identifier: MIT
// EST-383: the `gps raw <seconds>` diagnostic window.
//
// It answers one question the app could not answer from the outside: what does THIS L76K actually
// send while it hears satellites but has no position fix? It prints the SAFE fields of every RMC,
// ZDA, GGA and GSA sentence, and at the end one line naming every talker+type that arrived (so
// "does the module send ZDA by default?" has a measured answer).
//
// Rule 9 (the repo is public): a latitude or longitude must never reach a log. gpsRawFormat() never
// reads RMC fields 3..6 or GGA fields 2..5; it copies only the type, the time field, the RMC
// status/date, the GGA quality/satellites and the GSA fix mode, and each copied field must have the
// shape NMEA gives it (the time field must be exactly hhmmss[.sss]). A field that does not match
// prints "-", so a garbled line cannot smuggle a position into another column.
//
// Plain C++ (no Arduino), so test/host proves the stripping, the tally and the window with g++.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {

// The USB command's allowed window (the ticket's 5..120 s).
constexpr int kGpsRawMinSeconds = 5;
constexpr int kGpsRawMaxSeconds = 120;

// How many distinct talker+type tokens one window remembers. The L76K sends five or six; the spare
// slots keep the summary honest if a firmware ever adds more.
constexpr int kGpsRawMaxTypes = 12;

// Copies the talker+type token (the first comma field of a line, e.g. "GNRMC") into `out`, printable
// ASCII and at most 7 characters. False when the line has no '$' or the first field is empty.
bool gpsRawTypeToken(const char* sentence, char* out, size_t cap);

// Formats one RMC/ZDA/GGA/GSA sentence into its safe diagnostic fields, e.g.
//   "type=GNRMC time=123456.00 status=A date=101126"
// Returns false for every other sentence type, an unreadable line, or a very small cap. The result
// never contains a latitude or a longitude, whatever the line actually carried.
bool gpsRawFormat(const char* sentence, char* out, size_t cap);

// One diagnostic window: when it ends, and every talker+type seen in it with its count. The board
// feeds every complete line to noteSentence(); gpsPoll() prints the summary once when finished().
class GpsRawWindow {
 public:
  bool start(int seconds, uint32_t nowMs);   // false for a second window or an out-of-range length
  bool active() const { return active_; }
  bool finished(uint32_t nowMs) const;       // a running window whose time is up
  int seconds() const { return seconds_; }
  uint32_t elapsedSec(uint32_t nowMs) const;
  int secondsLeft(uint32_t nowMs) const;     // 0 when not running or already over

  // One complete NMEA line (checksum not needed here). The type is tallied whether the checksum is
  // good or not: a garbled line still says which sentence the module tried to send.
  void noteSentence(const char* sentence, bool checksumOk);
  void stop();

  int typeCount() const { return typeCount_; }
  const char* typeAt(int i) const;
  uint32_t countAt(int i) const;
  bool sawZda() const;                       // any ZDA talker appeared in this window
  uint32_t badSum() const { return badSum_; }
  int droppedTypes() const { return droppedTypes_; }   // types past the table's capacity

 private:
  bool active_ = false;
  int seconds_ = 0;
  uint32_t startMs_ = 0;
  uint32_t endMs_ = 0;
  int typeCount_ = 0;
  int droppedTypes_ = 0;
  uint32_t badSum_ = 0;
  char types_[kGpsRawMaxTypes][8] = {};
  uint32_t counts_[kGpsRawMaxTypes] = {};
};

}  // namespace om
