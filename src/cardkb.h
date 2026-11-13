// SPDX-License-Identifier: MIT
// EST-412: the hardware half of the CardKB on the L2's Grove port. The rules (key codes, default,
// polling gate, the unplugged-keyboard limit) are in cardkb_logic.h; this is only the bus, the
// power and the 30 ms read.
#pragma once

#include <stdint.h>

namespace om {
namespace cardkb {

// EST-421: powers Grove through expander bit 7 and arms the non-blocking start window. It does NOT wait
// and does NOT probe: the CardKB's ATmega needs longer than the old 50 ms settle, so step() probes
// from the loop task until the keyboard answers or 3000 ms pass. True when the rail is on (the window
// is running), false only when the expander write failed, so found() is still false when begin()
// returns. EST-417: the measured order on Filip's unit is SDA = GPIO13, SCL = GPIO14, so that is tried
// first and (14, 13) on the alternate attempts.
bool begin();

// EST-421: one pass of the start window. Call once per loop pass while the switch is on. Probes 0x5F
// about every 100 ms, alternating the pin order, and on the answer logs `[kb] cardkb found (sda=.. scl=..)
// after N ms` and stops; at 3000 ms it drops the rail, logs the not-found line and asks for one toast.
// While the window is open found() is false, so nothing polls the keyboard. No delay() in here.
void step(uint32_t nowMs);

// Bus down (if it was up), Grove power off, and an open start window cancelled. Called for the switch's
// OFF and for a clean re-enable.
void end();

// True only while the switch is on and the probe found the keyboard. EST-417 rule 8: false again
// after kFailLimit failed reads in a row, until the switch is toggled off and on.
bool found();

// EST-417: true ONCE after the driver marked an unplugged keyboard lost, so the shell can show its
// toast. Taking it clears it; the driver never draws.
bool takeLostNotice();

// EST-421: true ONCE after the start window ended with no answer (the driver has already dropped the
// rail), so the shell shows "CardKB not found (switch off/on to retry)". Taking it clears it.
bool takeNotFoundNotice();

// One ~30 ms read. Call ONLY when shouldPoll() is true: this function does not know about the switch
// or the screen. Returns -1 when it is not due or the read failed, 0 when nothing is pressed, and the
// key code (> 0) on a new press. A code held down is returned once; 0x00 between presses is what
// re-arms it, so one physical press inserts one character.
int poll(uint32_t nowMs);

}  // namespace cardkb
}  // namespace om
