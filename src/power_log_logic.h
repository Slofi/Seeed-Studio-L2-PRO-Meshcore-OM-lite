// SPDX-License-Identifier: MIT
// EST-377: the plain-C++ half of the battery log — the CSV line, the file name, the once-per-minute
// gate and the saved on/off switch.
//
// No Arduino, no SD, no time functions: test/host builds this file with the system g++, so the line
// the device writes is the line the test checks. The shell supplies the local civil time (from
// timekeeper.cpp) and the device state; src/power_log.cpp owns the card.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace plog {

// The one NVS key, and the default the first boot takes WITHOUT writing it (the same rule as the
// map layers: a missing key is the default, a toggle writes it).
constexpr const char* kPowerLogPrefKey = "plogen";
constexpr bool kPowerLogDefaultOn = true;

// One line per minute. The gate is monotonic (millis()), so a wall-clock jump cannot produce a burst
// and does not postpone a line; the first call after the logger starts is due at once.
constexpr uint32_t kPowerLogPeriodMs = 60000;

// The local civil minute a line belongs to. timekeeper.cpp's localCivil() fills this (Europe/
// Ljubljana); the line carries minutes, which is the logger's own resolution.
struct LocalStamp {
  int year = 1970;
  int month = 1;
  int day = 1;
  int hour = 0;
  int minute = 0;
};

// Everything one CSV line reports. `packets` is the number heard IN THE MINUTE (the caller keeps
// the previous minute's counter); the rest is the state at the moment the line is written.
struct Sample {
  uint16_t battMv = 0;      // cell mV (board::batteryMilliVolts, 0 = the read failed)
  bool screenOn = false;    // the panel/backlight state the loop uses
  uint8_t brightness = 0;   // the backlight level actually applied, 0..255
  bool gpsRailOn = false;   // the GNSS power rail (expander bit 13)
  bool fix = false;         // GGA fix quality > 0
  uint8_t sats = 0;         // satellites USED in the fix
  bool wifiOn = false;      // the saved Wi-Fi switch
  bool bleOn = false;       // the saved Bluetooth switch
  uint32_t packets = 0;     // packets heard in this minute
  int noiseDbm = 0;         // MeshCore's noise floor, 0 = still converging
  uint32_t uptimeSec = 0;   // millis()/1000
  // EST-401: the charging-bolt measurement, step 1. Nothing in the UI reads these yet; they exist so
  // the device can prove whether USB power has a real signal (cable on vs off) before a bolt is drawn.
  int8_t vbus = -1;         // STATUS0 bit 7 VBUSOK: 1 present, 0 absent, -1 the read failed
  uint16_t ain1Mv = 0;      // ADS1115 AIN1..AIN3 at the pin, 0 = the read failed
  uint16_t ain2Mv = 0;
  uint16_t ain3Mv = 0;
};

// The header line, WITHOUT a newline. A new file gets this line first. The four EST-401 columns are
// APPENDED, so every position a reader already used keeps its meaning.
constexpr const char* kPowerLogHeader =
    "time,batt_mv,screen,brightness,gps,fix,sats,wifi,ble,pkts,noise,uptime_s,vbus,ain1,ain2,ain3";

// "YYYY-MM-DD HH:MM,<15 more fields>\n". Returns the length written (without the NUL) or -1 when
// the buffer is too small.
int formatLine(const Sample& s, const LocalStamp& t, char* out, size_t cap);

// "/log/power-YYYYMMDD.csv" (local date). Returns the length or -1.
int formatFileName(const LocalStamp& t, char* out, size_t cap);

// The bytes one card append writes: the header first when `newFile` (the file was just created or
// was empty), then the data line. This is the exact buffer the device hands to File::write().
// Returns the length or -1.
int formatAppend(const Sample& s, const LocalStamp& t, bool newFile, char* out, size_t cap);

// One line per 60 s of monotonic time. The first call returns true (a fresh log shows a line at
// once, so a wrong switch or a full card is visible in the first minute, not after one).
class MinuteGate {
 public:
  bool due(uint32_t nowMs) {
    if (!started_) {
      started_ = true;
      lastMs_ = nowMs;
      return true;
    }
    if ((uint32_t)(nowMs - lastMs_) < kPowerLogPeriodMs) return false;
    lastMs_ = nowMs;
    return true;
  }
  void reset() {
    started_ = false;
    lastMs_ = 0;
  }
  bool started() const { return started_; }

 private:
  bool started_ = false;
  uint32_t lastMs_ = 0;
};

// The saved switch: load once, set with one write. The backend is the firmware's Preferences (the
// shell injects it) and RAM in the host test, exactly like ui/map_layers.h.
class PowerLogPref {
 public:
  // read: true when the key exists (`out` holds it); false leaves the fallback. write: true on
  // success.
  using ReadFn = bool (*)(void* ctx, const char* key, bool fallback, bool& out);
  using WriteFn = bool (*)(void* ctx, const char* key, bool value);

  void begin(ReadFn read, WriteFn write, void* ctx) {
    read_ = read;
    write_ = write;
    ctx_ = ctx;
  }

  // A missing key or no reader is the default ON, and nothing is written for it.
  void load() { on_ = readOr(kPowerLogDefaultOn); }
  bool on() const { return on_; }

  // A switch change: the RAM value follows the request even when the write fails (the caller can
  // report the failure); the return value is the write's.
  bool setOn(bool on) {
    on_ = on;
    return write_ != nullptr ? write_(ctx_, kPowerLogPrefKey, on_) : false;
  }

 private:
  bool readOr(bool fallback) {
    bool out = fallback;
    if (read_ != nullptr && read_(ctx_, kPowerLogPrefKey, fallback, out)) return out;
    return fallback;
  }

  ReadFn read_ = nullptr;
  WriteFn write_ = nullptr;
  void* ctx_ = nullptr;
  bool on_ = kPowerLogDefaultOn;
};

// How many new packets a one-minute line reports. `total` and `previous` are uint32 counters that
// wrap; `havePrevious` false is the first line (0, there was no minute before it).
inline uint32_t packetDelta(uint32_t total, uint32_t previous, bool havePrevious) {
  return havePrevious ? (uint32_t)(total - previous) : 0;
}

}  // namespace plog
}  // namespace om
