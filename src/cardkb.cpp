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

namespace om {
namespace cardkb {
namespace {

constexpr uint8_t kAddr = 0x5F;          // M5Stack Unit CardKB
constexpr uint32_t kBusHz = 100000;      // the ticket's 100 kHz
constexpr uint32_t kPollMs = 30;         // the loop's cadence while a text field is open
constexpr uint32_t kSettleMs = 50;       // the ATmega8A's boot before a probe; only when ON

bool s_found = false;
bool s_busUp = false;
bool s_haveLast = false;
uint8_t s_lastCode = 0;
uint32_t s_lastPoll = 0;

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

}  // namespace

bool begin() {
  busDown();
  s_found = false;
  s_haveLast = false;
  if (!boardGrovePower(true)) {
    Serial.println("[kb] Grove power FAILED (expander)");
    return false;
  }
  delay(kSettleMs);
  // Seeed names the pins GROVE_DAT_1 = GPIO14 and GROVE_DAT_2 = GPIO13 but no board source says
  // which is SDA. Try the two orders once each, and only these two.
  if (openAndProbe(kPinGroveDat1, kPinGroveDat2)) {
    s_found = true;
    s_busUp = true;
    Serial.printf("[kb] cardkb found (sda=%d scl=%d)\n", kPinGroveDat1, kPinGroveDat2);
    return true;
  }
  busDown();
  if (openAndProbe(kPinGroveDat2, kPinGroveDat1)) {
    s_found = true;
    s_busUp = true;
    Serial.printf("[kb] cardkb found (sda=%d scl=%d)\n", kPinGroveDat2, kPinGroveDat1);
    return true;
  }
  busDown();
  // Review fix (Claude): there is no hot-plug rescan, so a rail left on here would only drain the
  // battery all day when the switch stays on and the keyboard is not carried. Toggling the switch
  // off and on again powers it up and probes once more.
  boardGrovePower(false);
  Serial.println("[kb] cardkb not found, Grove power off");
  return false;
}

void end() {
  const bool wasOn = s_busUp || boardGrovePowerOn();
  busDown();
  s_found = false;
  s_haveLast = false;
  boardGrovePower(false);
  if (wasOn) Serial.println("[kb] off, Grove power off");
}

bool found() { return s_found && s_busUp; }

int poll(uint32_t nowMs) {
  if (!s_found || !s_busUp) return -1;
  if (s_lastPoll != 0 && (uint32_t)(nowMs - s_lastPoll) < kPollMs) return -1;
  s_lastPoll = nowMs;
  if (Wire1.requestFrom((int)kAddr, 1) != 1) return -1;
  const int code = Wire1.read();
  if (code < 0) return -1;
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
