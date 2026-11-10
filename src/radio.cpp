// SPDX-License-Identifier: MIT
// OM Lite radio — SX1262 bring-up on the Wio Tracker L2.
//
// The radio sits on FSPI (SPI2) with the board's own pins, DIO2 as the RF switch and a 1.8 V TCXO.
// This file only proves the chip answers and can be configured; the MeshCore stack will take the
// same Module once it is wired in.
#include "radio.h"

#include <RadioLib.h>
#include <SPI.h>

#include "board.h"
#include "channels.h"

namespace om {
namespace {

constexpr int kPinNss = 21;
constexpr int kPinDio1 = 9;
constexpr int kPinReset = 7;
constexpr int kPinBusy = 8;

SPIClass s_spi(FSPI);
Module s_module(kPinNss, kPinDio1, kPinReset, kPinBusy, s_spi);
SX1262 s_radio(&s_module);

RadioInfo s_info;

}  // namespace

bool radioBegin(RadioInfo& out) {
  if (s_info.ok) {
    out = s_info;
    return true;
  }

  // The mesh's own plan, mirrored from ERA-3 (SI) so OM Lite sits on the same airtime.
  const McRadioPlan& plan = mcRadioPlan();
  s_spi.begin(4, 5, 6, kPinNss);
  const int16_t status = s_radio.begin(plan.freq, plan.bw, plan.sf, plan.cr, 0x12, plan.txPower, 8, 1.8f);
  s_info.status = status;
  Serial.printf("[radio] begin: %d (%s)\n", status, status == RADIOLIB_ERR_NONE ? "ok" : "FAILED");
  if (status == RADIOLIB_ERR_NONE) {
    s_radio.setDio2AsRfSwitch(true);
    s_info.ok = true;
    // The SX126x driver does not expose getters for these; they are what begin() was given.
    s_info.frequency = plan.freq;
    s_info.bandwidth = plan.bw;
    s_info.spreading = plan.sf;
    s_info.power = plan.txPower;
    Serial.printf("[radio] %.3f MHz bw %.0f sf %u cr %u pwr %d dBm\n", s_info.frequency,
                  s_info.bandwidth, s_info.spreading, (unsigned)plan.cr,
                  (int)s_info.power);
  }
  out = s_info;
  return s_info.ok;
}

const char* radioStatusText() {
  if (!s_info.ok) return s_info.status ? "radio FAIL" : "radio --";
  return "radio ok";
}

}  // namespace om
