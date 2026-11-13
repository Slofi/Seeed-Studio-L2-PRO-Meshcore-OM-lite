// SPDX-License-Identifier: MIT
// EST-412: the CardKB's bus and power. The testable rules live in cardkb_logic.h.
//
// Safety notes this file depends on (Filip measures the lines before trusting them):
//   - Grove VCC is its own 5 V boost, enabled by expander bit 7 (Seeed datasheet block diagram,
//     product_pdf/100029766.pdf p.5). The ESP32-S3 pins are not 5 V tolerant.
//   - No source in this tree (src/board.*, lib/, reference/) says whether the Grove data lines carry
//     pull-ups or to which rail. `grove on` therefore touches the power bit ONLY: it starts no bus
//     and sets no pin mode, so a meter on SDA/SCL sees the keyboard's own pull-ups and nothing ours.
//   - When the switch is on, Wire1.begin() enables the SoC's internal pull-ups. Those are tied to
//     the 3.3 V rail (the Arduino core's i2cInit sets sda_pullup_en/scl_pullup_en, GPIO_PULLUP_
//     ENABLE, in esp32-hal-i2c.c); this code cannot select any other rail. They do not protect the
//     pins from a pull-up on the keyboard that goes to 5 V, so the measurement still decides.
#include "cardkb.h"

#include <Arduino.h>
#include <Wire.h>

#include "board.h"
#include "cardkb_logic.h"   // EST-417: the failed-read limit lives with the other tested rules

namespace om {
namespace cardkb {
namespace {

constexpr uint8_t kAddr = 0x5F;          // M5Stack Unit CardKB
constexpr uint32_t kBusHz = 100000;      // the ticket's 100 kHz
constexpr uint32_t kPollMs = 30;         // the loop's cadence while the keyboard is on

bool s_found = false;
bool s_busUp = false;
bool s_haveLast = false;
bool s_lostNotice = false;     // EST-417: the one disconnected toast, taken by the shell once
bool s_notFoundNotice = false; // EST-421: the start window timed out, one toast, taken once
uint8_t s_lastCode = 0;
uint32_t s_lastPoll = 0;
int s_failRun = 0;             // EST-417: consecutive failed reads, reset by any good one

// EST-421: the rail-on time and the probe schedule. The rules (3000 ms, one probe per 100 ms, the
// alternating pin order, cancel and timeout) are cardkb_logic.h's, so test/host proves them with the
// system g++; this file only turns a Probe into a Wire1 transaction. ONE window exists at a time.
StartWindow s_window;

bool probe() {
  Wire1.beginTransmission(kAddr);
  return Wire1.endTransmission() == 0;
}

bool openAndProbe(int sda, int scl) {
  Wire1.begin(sda, scl, kBusHz);
  s_busUp = true;   // the first attempt's failure must be closed before the second begin()
  Wire1.setTimeOut(30);
  return probe();
}

void busDown() {
  if (!s_busUp) return;
  Wire1.end();
  s_busUp = false;
}

// EST-417 rule 8: a read failed. One is noise; kFailLimit in a row means the keyboard was pulled
// out. Mark it lost exactly as the not-found path does (bus closed, Grove 5 V dropped), log ONCE, and
// leave the switch alone: it is still ON, so toggling it off and on probes again. The flag below is
// how the shell learns to show its toast without the driver knowing about screens.
void noteFailedRead() {
  if (!lostAfterFailure(++s_failRun)) return;
  s_failRun = 0;
  Serial.println("[kb] keyboard disconnected");
  busDown();
  s_found = false;
  s_haveLast = false;
  boardGrovePower(false);
  s_lostNotice = true;
}

}  // namespace

bool begin() {
  busDown();
  s_window.cancel();   // a re-enable always starts a fresh window (and probe order 13 first)
  s_found = false;
  s_haveLast = false;
  s_failRun = 0;
  s_lostNotice = false;
  s_notFoundNotice = false;
  if (!boardGrovePower(true)) {
    Serial.println("[kb] Grove power FAILED (expander)");
    return false;
  }
  // EST-421: the rail is on and this returns AT ONCE. The ATmega8A needs longer than the old 50 ms
  // settle, so step() (the loop task) probes 0x5F every ~100 ms until the keyboard answers or the
  // window of kStartWindowMs ends. Nothing waits here: boot and the UI run on, and found() stays
  // false until a keyboard answers, so no key is read early.
  s_window.start(millis());
  return true;
}

void step(uint32_t nowMs) {
  const WindowStep w = s_window.next(nowMs);
  switch (w.action) {
    case WindowAction::Idle:
      return;
    case WindowAction::TimedOut:
      // No hot-plug rescan: a rail left on here would only drain the battery all day when the switch
      // stays on and the keyboard is not carried. Toggling the switch off and on powers it up and
      // opens one more window.
      busDown();
      boardGrovePower(false);
      Serial.printf("[kb] cardkb not found after %lu ms, Grove power off\n",
                    (unsigned long)kStartWindowMs);
      s_notFoundNotice = true;
      return;
    case WindowAction::Probe: {
      // Seeed names the pins GROVE_DAT_1 = GPIO14 and GROVE_DAT_2 = GPIO13 but no board source says
      // which is SDA. EST-417: Filip's probe answered at SDA = GPIO13, SCL = GPIO14, so that order is
      // tried first on every even attempt; the other one on every odd attempt (EST-421 keeps it).
      const int sda = w.sda13First ? kPinGroveDat2 : kPinGroveDat1;
      const int scl = w.sda13First ? kPinGroveDat1 : kPinGroveDat2;
      if (openAndProbe(sda, scl)) {
        s_found = true;          // the bus stays up: the probe helper set s_busUp
        s_window.cancel();
        Serial.printf("[kb] cardkb found (sda=%d scl=%d) after %lu ms\n", sda, scl,
                      (unsigned long)w.elapsedMs);
        return;
      }
      busDown();   // a failed attempt closes its half-open bus before the next one
      return;
    }
  }
}

void end() {
  const bool wasOn = s_busUp || boardGrovePowerOn() || s_window.active();
  s_window.cancel();   // EST-421: switching OFF mid-window cancels it, so no timeout toast follows
  s_notFoundNotice = false;
  busDown();
  s_found = false;
  s_haveLast = false;
  s_failRun = 0;
  s_lostNotice = false;
  boardGrovePower(false);
  if (wasOn) Serial.println("[kb] off, Grove power off");
}

bool found() { return s_found && s_busUp; }

bool takeLostNotice() {
  if (!s_lostNotice) return false;
  s_lostNotice = false;
  return true;
}

bool takeNotFoundNotice() {
  if (!s_notFoundNotice) return false;
  s_notFoundNotice = false;
  return true;
}

int poll(uint32_t nowMs) {
  if (!s_found || !s_busUp) return -1;
  if (s_lastPoll != 0 && (uint32_t)(nowMs - s_lastPoll) < kPollMs) return -1;
  s_lastPoll = nowMs;
  if (Wire1.requestFrom((int)kAddr, 1) != 1) {
    noteFailedRead();
    return -1;
  }
  const int code = Wire1.read();
  if (code < 0) {
    noteFailedRead();
    return -1;
  }
  s_failRun = 0;   // the keyboard answered: the run of failures is over
  if (code == 0) {
    s_haveLast = false;   // the key lifted: the next press is a new one
    return 0;
  }
  if (s_haveLast && code == (int)s_lastCode) return 0;   // one physical press, one character
  s_haveLast = true;
  s_lastCode = (uint8_t)code;
  return code;
}

}  // namespace cardkb
}  // namespace om
