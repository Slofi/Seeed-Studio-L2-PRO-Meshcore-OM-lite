// SPDX-License-Identifier: MIT
// OM Lite radio — SX1262 bring-up on the Wio Tracker L2 (FSPI).
//
// Pins are the board's: NSS 21, DIO1 9, RESET 7, BUSY 8, SCK 4, MISO 5, MOSI 6, DIO2 drives the
// RF switch, TCXO 1.8 V. MeshCore's EU defaults are 869.525 MHz / BW 250 / SF 11 / CR 5.
#pragma once

#include <Arduino.h>

namespace om {

struct RadioInfo {
  bool ok = false;
  int status = 0;        // RadioLib error code (0 = RADIOLIB_ERR_NONE)
  float frequency = 0;
  float bandwidth = 0;
  uint8_t spreading = 0;
  int8_t power = 0;
};

// Powers the radio and configures it for EU868. Safe to call once.
bool radioBegin(RadioInfo& out);

// Short status line for the UI.
const char* radioStatusText();

}  // namespace om
