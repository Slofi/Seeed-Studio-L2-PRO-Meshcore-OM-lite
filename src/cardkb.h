// SPDX-License-Identifier: MIT
// EST-412: the hardware half of the CardKB on the L2's Grove port. The rules (key codes, default,
// polling gate, the unplugged-keyboard limit) are in cardkb_logic.h; this is only the bus, the
// power and the 30 ms read.
#pragma once

#include <stdint.h>

namespace om {
namespace cardkb {

// Powers Grove through expander bit 7, opens a SECOND I2C bus and probes 0x5F. EST-417: the measured
// order on Filip's unit is SDA = GPIO13, SCL = GPIO14, so that is tried first and (14, 13) once more
// as the fallback; never more than those two probes. The 50 ms settle lets the CardKB's ATmega come
// up. True when the keyboard answered.
bool begin();

// Bus down (if it was up), Grove power off. Called for the switch's OFF and for a clean re-enable.
void end();

// True only while the switch is on and the probe found the keyboard. EST-417 rule 8: false again
// after kFailLimit failed reads in a row, until the switch is toggled off and on.
bool found();

// EST-417: true ONCE after the driver marked an unplugged keyboard lost, so the shell can show its
// toast. Taking it clears it; the driver never draws.
bool takeLostNotice();

// One ~30 ms read. Call ONLY when shouldPoll() is true: this function does not know about the switch
// or the screen. Returns -1 when it is not due or the read failed, 0 when nothing is pressed, and the
// key code (> 0) on a new press. A code held down is returned once; 0x00 between presses is what
// re-arms it, so one physical press inserts one character.
int poll(uint32_t nowMs);

}  // namespace cardkb
}  // namespace om
