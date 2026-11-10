// SPDX-License-Identifier: MIT
#include "mc_board.h"

#include <Arduino.h>

#include "board.h"

namespace om {
uint16_t OmBoard::getBattMilliVolts() { return batteryMilliVolts(); }
void OmBoard::reboot() { ESP.restart(); }
}
