// SPDX-License-Identifier: MIT
// EST-412: the hardware half of the CardKB on the L2's Grove port. The rules (key codes, default,
// polling gate) are in cardkb_logic.h; this is only the bus, the power and the 30 ms read.
#pragma once

#include <stdint.h>

namespace om {
namespace cardkb {

// Powers Grove through expander bit 7, opens a SECOND I2C bus on GPIO14/GPIO13 and probes 0x5F.
// No source in this tree says which of the two is SDA, so the driver tries both orders ONCE each
// and logs the one that answered; never more than those two probes. The 50 ms settle lets the
// CardKB's ATmega come up. True when the keyboard answered.
bool begin();

// Bus down (if it was up), Grove power off. Called for the switch's OFF and for a clean re-enable.
void end();

// True only while the switch is on and the probe found the keyboard.
bool found();

// One ~30 ms read. Call ONLY when shouldPoll() is true: this function does not know about text
// fields or the switch. Returns -1 when it is not due or the read failed, 0 when nothing is pressed,
// and the key code (> 0) on a new press. A code held down is returned once; 0x00 between presses is
// what re-arms it, so one physical press inserts one character.
int poll(uint32_t nowMs);

}  // namespace cardkb
}  // namespace om
