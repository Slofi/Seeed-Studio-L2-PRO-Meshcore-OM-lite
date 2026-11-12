// SPDX-License-Identifier: MIT
// EST-377: the battery log's card half. See power_log.h.
//
// The card is mounted once at boot (board::sdMount) and its rail stays up, exactly as the history
// store and the map use it; there is no second mount and no power cycling here. What this file adds
// is the EST-349 rule: the ONE append per minute happens from plog::loop(), called by the main loop,
// never from a USB handler or a callback.
#include "power_log.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <string.h>

#include "mesh_link.h"
#include "radio_stats.h"
#include "board.h"        // EST-401: usbPdStatus0(), adcAuxProbe()
#include "diag_logic.h"   // EST-401: status0VbusOk()
#include "timekeeper.h"

namespace om {
namespace plog {
namespace {

PowerLogPref s_pref;
MinuteGate s_gate;
// EST-413: the clock side of the gate. It stamps only once the clock is real since boot and counts
// the due minutes it skipped before that, so `log power tail` can report them.
MinuteClock s_clock;
bool s_havePackets = false;
uint32_t s_lastPackets = 0;
bool s_dirOk = false;
bool s_noCardLogged = false;
// EST-380: card errors since boot, so `log power tail` can answer with a number instead of a line
// that only ever appeared in the serial log (which nobody reads after a night on battery).
uint32_t s_writeFail = 0;

// The tail read's buffer. Static on purpose: it keeps the command handler's stack where EST-350
// left it (the history command's depth was the panic), and only the loop task touches it.
constexpr size_t kTailBytes = 1024;
char s_tail[kTailBytes];

bool localNow(LocalStamp& out) {
  // EST-413: the one rule lives in power_log_logic.cpp (and is host-tested): the timekeeper's floor
  // is NOT a real time, so it must never become a CSV row.
  return logLocalStamp(om::timeKeeper().now(), om::timeKeeper().realSinceBoot(), out);
}

// /log is made once, lazily, on the same loop pass that writes the first line. A failed mkdir is
// retried on the next due minute (the card may be re-seated), not every pass.
bool ensureDir() {
  if (s_dirOk) return true;
  if (SD_MMC.exists("/log")) {
    s_dirOk = true;
    return true;
  }
  if (SD_MMC.mkdir("/log")) {
    s_dirOk = SD_MMC.exists("/log");
    return s_dirOk;
  }
  return false;
}

}  // namespace

void begin(PowerLogPref::ReadFn read, PowerLogPref::WriteFn write, void* ctx) {
  s_pref.begin(read, write, ctx);
  s_pref.load();
  s_gate.reset();
  s_clock.reset();
  s_havePackets = false;
  s_lastPackets = 0;
  s_dirOk = false;
  s_noCardLogged = false;
  Serial.printf("[plog] battery log %s (key %s, default %s)\n", s_pref.on() ? "on" : "off",
                kPowerLogPrefKey, kPowerLogDefaultOn ? "on" : "off");
}

bool enabled() { return s_pref.on(); }

bool setEnabled(bool on) {
  const bool saved = s_pref.setOn(on);
  s_gate.reset();   // a fresh enable writes its first line at once, so the switch proves itself
  Serial.printf("[plog] switch %s saved=%d\n", on ? "on" : "off", saved ? 1 : 0);
  return saved;
}

uint32_t writeFailures() { return s_writeFail; }

uint32_t skippedMinutes() { return s_clock.skipped(); }

void loop(uint32_t nowMs, const State& st) {
  if (!s_pref.on()) return;
  if (!s_gate.due(nowMs)) return;

  if (SD_MMC.totalBytes() == 0) {
    if (!s_noCardLogged) {
      s_noCardLogged = true;
      Serial.println("[plog] no SD card, battery log waits");
    }
    return;
  }
  s_noCardLogged = false;

  LocalStamp t;
  const bool clockReal = om::timeKeeper().realSinceBoot();
  if (!s_clock.stamp(om::timeKeeper().now(), clockReal, t)) {
    // EST-413: a due minute before a real source writes NO row (the floor is not a time, and the
    // previous boot's rows may share this file); it is counted instead. The first minute after the
    // clock becomes real stamps and writes normally.
    if (!clockReal) {
      Serial.printf("[plog] no local time yet, line skipped (%lu skipped since boot, not real)\n",
                    (unsigned long)s_clock.skipped());
    } else {
      Serial.println("[plog] no local time yet, line skipped");
    }
    return;
  }
  char path[40];
  if (formatFileName(t, path, sizeof(path)) < 0) return;
  if (!ensureDir()) {
    ++s_writeFail;   // EST-380: a card error is counted, not only printed
    Serial.println("[plog] no /log directory on the card");
    return;
  }

  // The radio numbers are read ONLY here, once per minute: meshRadioStats reads the SX1262's boosted
  // gain register over SPI (it is not a cached value), so it must not run on every pass.
  om::radio::UpstreamCounters c;
  const bool up = om::meshRadioStats(&c);
  const om::radio::Snapshot snap = om::radio::snapshot();
  const int noise = snap.noise.samples > 0 ? snap.noise.last : (up ? c.noiseFloor : 0);

  // EST-401: the charging-bolt measurement, read ONLY here for the same reason (each ADS1115 channel
  // is about 18 ms of I2C): STATUS0's VBUSOK and the three spare ADC inputs. A failed read is not a
  // zero: `vbus` stays -1 and the line says "unknown" rather than "no cable".
  uint8_t status0 = 0;
  const bool haveStatus0 = om::usbPdStatus0(&status0);
  om::AdcAux aux;
  om::adcAuxProbe(aux);

  Sample s;
  s.battMv = st.battMv;
  s.screenOn = st.screenOn;
  s.brightness = st.brightness;
  s.gpsRailOn = st.gpsRailOn;
  s.fix = st.fix;
  s.sats = st.sats;
  s.wifiOn = st.wifiOn;
  s.bleOn = st.bleOn;
  s.noiseDbm = noise;
  s.uptimeSec = st.uptimeSec;
  s.vbus = haveStatus0 ? (om::diag::status0VbusOk(status0) ? 1 : 0) : -1;
  s.ain1Mv = aux.mv[0];
  s.ain2Mv = aux.mv[1];
  s.ain3Mv = aux.mv[2];
  const uint32_t total = up ? c.rxPackets : 0;
  s.packets = packetDelta(total, s_lastPackets, s_havePackets && up);
  if (up) {
    s_lastPackets = total;
    s_havePackets = true;
  }

  // "a" creates the file when it is absent (no error line, the EST-349 finding) and appends when it
  // exists. The exists() probe is what decides the header; it is a stat on the loop task, like the
  // history store's own probes.
  const bool newFile = !SD_MMC.exists(path);
  File f = SD_MMC.open(path, FILE_APPEND);
  if (!f) {
    ++s_writeFail;   // EST-380: counted so `log power tail` can report it
    Serial.printf("[plog] write failed %s (write_fail=%lu)\n", path, (unsigned long)s_writeFail);
    return;
  }
  char buf[256];
  const int n = formatAppend(s, t, newFile, buf, sizeof(buf));
  if (n <= 0) {
    f.close();
    Serial.println("[plog] line did not fit the buffer");
    return;
  }
  const size_t wrote = f.write((const uint8_t*)buf, (size_t)n);
  f.flush();
  f.close();
  if (wrote != (size_t)n) {
    ++s_writeFail;   // a short write is a lost minute, not a clean line
    Serial.printf("[plog] write short %s %u/%d (write_fail=%lu)\n", path, (unsigned)wrote, n,
                  (unsigned long)s_writeFail);
    return;
  }

  Serial.printf("[plog] write %s new=%d bytes=%u/%d batt=%umV screen=%d bright=%u gps=%d fix=%d "
                "sats=%u wifi=%d ble=%d pkts=%lu noise=%d uptime=%lus vbus=%d ain1=%u ain2=%u "
                "ain3=%u\n",
                path, newFile ? 1 : 0, (unsigned)wrote, n, (unsigned)s.battMv,
                s.screenOn ? 1 : 0, (unsigned)s.brightness, s.gpsRailOn ? 1 : 0, s.fix ? 1 : 0,
                (unsigned)s.sats, s.wifiOn ? 1 : 0, s.bleOn ? 1 : 0, (unsigned long)s.packets,
                s.noiseDbm, (unsigned long)s.uptimeSec, (int)s.vbus, (unsigned)s.ain1Mv,
                (unsigned)s.ain2Mv, (unsigned)s.ain3Mv);
}

int printTail() {
  if (SD_MMC.totalBytes() == 0) return -1;
  // EST-413: never name a day's file from a floor epoch. Before a real source the floor can be a
  // different date (max(saved NVS, build time) plus the off time), so the tail would show another
  // run's file as if it were today's.
  if (!om::timeKeeper().realSinceBoot()) return -1;
  LocalStamp t;
  if (!localNow(t)) return -1;
  char path[40];
  if (formatFileName(t, path, sizeof(path)) < 0) return -1;
  if (!SD_MMC.exists(path)) return -1;

  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return -1;
  const uint32_t size = (uint32_t)f.size();
  if (size == 0) {
    f.close();
    return 0;
  }
  const uint32_t start = size > (uint32_t)(kTailBytes - 1) ? size - (uint32_t)(kTailBytes - 1) : 0;
  if (!f.seek(start)) {
    f.close();
    return -1;
  }
  const int got = (int)f.read((uint8_t*)s_tail, kTailBytes - 1);
  f.close();
  if (got <= 0) return 0;
  s_tail[got] = 0;

  // A seek into the middle of a line starts with a fragment; drop it. A file that does not end with
  // a newline (a torn append) leaves its last partial line unprinted.
  char* p = s_tail;
  if (start > 0) {
    char* nl = strchr(p, '\n');
    if (nl == nullptr) return 0;
    p = nl + 1;
  }
  const char* starts[5] = {};
  int lens[5] = {};
  int count = 0;
  while (*p != 0) {
    char* nl = strchr(p, '\n');
    if (nl == nullptr) break;
    const int len = (int)(nl - p);
    if (len > 0) {
      const int slot = count % 5;
      starts[slot] = p;
      lens[slot] = len;
      ++count;
    }
    p = nl + 1;
  }
  const int shown = count < 5 ? count : 5;
  for (int i = count - shown; i < count; ++i) {
    const int slot = i % 5;
    Serial.write((const uint8_t*)starts[slot], (size_t)lens[slot]);
    Serial.println();
  }
  return shown;
}

}  // namespace plog
}  // namespace om
