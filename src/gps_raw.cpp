// SPDX-License-Identifier: MIT
// EST-383: the `gps raw <seconds>` diagnostic. See gps_raw.h for the rule this file must obey.
#include "gps_raw.h"

#include <stdio.h>
#include <string.h>

namespace om {
namespace {

constexpr int kMaxBody = 120;   // one NMEA line's body; the board's line buffer is the same size
constexpr int kMaxFields = 20;  // one more than the longest sentence the L76K sends

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Calls `fn` for each comma field of one complete line. The body is copied, so the caller's buffer is
// not edited. False when there is no '$' or the body is missing/too long.
template <typename Fn>
bool forEachField(const char* sentence, Fn fn) {
  if (sentence == nullptr) return false;
  const char* dollar = strchr(sentence, '$');
  if (dollar == nullptr) return false;
  const char* start = dollar + 1;
  const char* star = strchr(start, '*');
  const size_t len = star != nullptr ? (size_t)(star - start) : strlen(start);
  if (len == 0 || len >= (size_t)kMaxBody) return false;

  char body[kMaxBody];
  memcpy(body, start, len);
  body[len] = 0;

  const char* fields[kMaxFields] = {nullptr};
  int n = 0;
  char* p = body;
  while (p != nullptr && n < kMaxFields) {
    fields[n++] = p;
    char* comma = strchr(p, ',');
    if (comma == nullptr) break;
    *comma = 0;
    p = comma + 1;
  }
  return fn(fields, n);
}

// Copies a field only when every character is a digit (empty text allowed when `allowEmpty`). This is
// what keeps a garbled line from carrying a latitude into a date, quality or satellite column.
bool copyDigits(const char* field, char* out, size_t cap, bool allowEmpty = false) {
  if (field == nullptr || out == nullptr || cap == 0) return false;
  const size_t len = strlen(field);
  if (len == 0) return allowEmpty;
  // Validate the whole field BEFORE writing anything: a refused field must leave the caller's "-"
  // in place, never a truncated number (the first version printed the first digits of a long run).
  if (len + 1 > cap) return false;
  for (size_t i = 0; i < len; ++i) {
    if (!isDigit(field[i])) return false;
  }
  memcpy(out, field, len + 1);
  return true;
}

// NMEA time is exactly hhmmss with an optional fraction: six digits, then "." and one to three
// digits. Anything else (a latitude like 4603.1234 has only four digits before the dot) is refused.
bool copyNmeaTime(const char* field, char* out, size_t cap) {
  if (field == nullptr || out == nullptr || cap < 7) return false;
  int digits = 0;
  const char* p = field;
  while (isDigit(*p)) {
    if (digits >= 6) return false;   // hhmmss is exactly six
    ++p;
    ++digits;
  }
  if (digits != 6) return false;
  if (*p == '.') {
    ++p;
    int frac = 0;
    while (isDigit(*p)) {
      if (++frac > 3) return false;
      ++p;
    }
    if (frac == 0) return false;
  }
  if (*p != 0) return false;
  const size_t len = strlen(field);
  if (len + 1 > cap) return false;
  memcpy(out, field, len + 1);
  return true;
}

char* append(char* at, char* end, const char* text) {
  while (at < end && *text != 0) *at++ = *text++;
  *at = 0;
  return at;
}

}  // namespace

bool gpsRawTypeToken(const char* sentence, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = 0;
  if (sentence == nullptr) return false;
  const char* dollar = strchr(sentence, '$');
  if (dollar == nullptr) return false;
  const char* start = dollar + 1;
  size_t len = 0;
  while (start[len] != 0 && start[len] != ',' && start[len] != '*' && len < 7) ++len;
  if (len == 0) return false;
  for (size_t i = 0; i < len; ++i) {
    const unsigned char c = (unsigned char)start[i];
    out[i] = (c >= 0x21 && c <= 0x7E && c != '"') ? (char)c : '?';
  }
  out[len] = 0;
  return true;
}

bool gpsRawFormat(const char* sentence, char* out, size_t cap) {
  if (out == nullptr || cap < 48) return false;
  out[0] = 0;

  return forEachField(sentence, [&](const char* const* fields, int n) {
    if (n < 2) return false;
    const char* type = fields[0];
    const size_t typeLen = strlen(type);
    if (typeLen < 3) return false;

    // The talker+type token, sanitized and shortened for the line.
    char token[8] = "-";
    size_t tl = typeLen > 7 ? 7 : typeLen;
    for (size_t i = 0; i < tl; ++i) {
      const unsigned char c = (unsigned char)type[i];
      token[i] = (c >= 0x21 && c <= 0x7E && c != '"') ? (char)c : '?';
    }
    token[tl] = 0;

    char time[12] = "-";
    copyNmeaTime(fields[1], time, sizeof(time));
    const char* tail = type + typeLen - 3;

    char body[96];
    if (strcmp(tail, "RMC") == 0) {
      // $..RMC,hhmmss.ss,status,lat,N,lon,E,...  Fields 3..6 are NEVER read here.
      if (n < 10) return false;
      char status[2] = "-";
      if (fields[2][0] == 'A' || fields[2][0] == 'V') {
        status[0] = fields[2][0];
        status[1] = 0;
      }
      char date[8] = "-";
      copyDigits(fields[9], date, sizeof(date));
      snprintf(body, sizeof(body), "type=%s time=%s status=%s date=%s", token, time, status, date);
    } else if (strcmp(tail, "ZDA") == 0) {
      // $..ZDA,hhmmss.ss,dd,mm,yyyy,...
      if (n < 5) return false;
      char dd[4] = "-", mm[4] = "-", yyyy[6] = "-";
      if (!copyDigits(fields[2], dd, sizeof(dd)) || !copyDigits(fields[3], mm, sizeof(mm)) ||
          !copyDigits(fields[4], yyyy, sizeof(yyyy))) {
        snprintf(body, sizeof(body), "type=%s time=%s date=-", token, time);
      } else {
        snprintf(body, sizeof(body), "type=%s time=%s date=%s.%s.%s", token, time, dd, mm, yyyy);
      }
    } else if (strcmp(tail, "GGA") == 0) {
      // $..GGA,hhmmss.ss,lat,N,lon,E,quality,sats,...  Fields 2..5 are NEVER read here.
      if (n < 8) return false;
      char quality[4] = "-", sats[4] = "-";
      copyDigits(fields[6], quality, sizeof(quality));
      copyDigits(fields[7], sats, sizeof(sats));
      snprintf(body, sizeof(body), "type=%s time=%s quality=%s sats=%s", token, time, quality, sats);
    } else if (strcmp(tail, "GSA") == 0) {
      // $..GSA,mode,fixmode,prn,...  GSA carries no time; its fix mode is field 2.
      if (n < 3) return false;
      char fix[2] = "-";
      if (isDigit(fields[2][0]) && fields[2][1] == 0) {
        fix[0] = fields[2][0];
        fix[1] = 0;
      }
      snprintf(body, sizeof(body), "type=%s time=- fixmode=%s", token, fix);
    } else {
      return false;   // not one of the four diagnostic sentences
    }

    // Build the final line again with the caller's cap, so a small buffer truncates safely.
    out[0] = 0;
    append(out, out + cap - 1, body);
    return true;
  });
}

// --- the window ---------------------------------------------------------------------------------

bool GpsRawWindow::start(int seconds, uint32_t nowMs) {
  if (active_) return false;
  if (seconds < kGpsRawMinSeconds || seconds > kGpsRawMaxSeconds) return false;
  active_ = true;
  seconds_ = seconds;
  startMs_ = nowMs;
  endMs_ = nowMs + (uint32_t)seconds * 1000u;
  typeCount_ = 0;
  droppedTypes_ = 0;
  badSum_ = 0;
  for (int i = 0; i < kGpsRawMaxTypes; ++i) {
    types_[i][0] = 0;
    counts_[i] = 0;
  }
  return true;
}

bool GpsRawWindow::finished(uint32_t nowMs) const {
  return active_ && (int32_t)(nowMs - endMs_) >= 0;
}

uint32_t GpsRawWindow::elapsedSec(uint32_t nowMs) const {
  if (!active_) return 0;
  return (uint32_t)(nowMs - startMs_) / 1000u;
}

int GpsRawWindow::secondsLeft(uint32_t nowMs) const {
  if (!active_ || (int32_t)(nowMs - endMs_) >= 0) return 0;
  return (int)((endMs_ - nowMs + 999u) / 1000u);
}

void GpsRawWindow::noteSentence(const char* sentence, bool checksumOk) {
  if (!active_) return;
  if (!checksumOk) ++badSum_;
  char token[8];
  if (!gpsRawTypeToken(sentence, token, sizeof(token))) return;
  for (int i = 0; i < typeCount_; ++i) {
    if (strcmp(types_[i], token) == 0) {
      ++counts_[i];
      return;
    }
  }
  if (typeCount_ >= kGpsRawMaxTypes) {
    ++droppedTypes_;
    return;
  }
  size_t n = strlen(token);
  if (n > 7) n = 7;
  memcpy(types_[typeCount_], token, n);
  types_[typeCount_][n] = 0;
  counts_[typeCount_] = 1;
  ++typeCount_;
}

void GpsRawWindow::stop() {
  active_ = false;
  seconds_ = 0;
  startMs_ = 0;
  endMs_ = 0;
}

const char* GpsRawWindow::typeAt(int i) const {
  if (i < 0 || i >= typeCount_) return "";
  return types_[i];
}

uint32_t GpsRawWindow::countAt(int i) const {
  if (i < 0 || i >= typeCount_) return 0;
  return counts_[i];
}

bool GpsRawWindow::sawZda() const {
  for (int i = 0; i < typeCount_; ++i) {
    const size_t n = strlen(types_[i]);
    if (n >= 3 && strcmp(types_[i] + n - 3, "ZDA") == 0) return true;
  }
  return false;
}

}  // namespace om
