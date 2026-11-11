// SPDX-License-Identifier: MIT
// EST-376: the two radio A/B knobs (TCXO voltage, boosted RX gain) and the noise-floor sampler.
//
// Everything here is RAM only: a reboot restores target.h's 1.8 V and the build's boosted-gain
// default. Nothing is saved, nothing is a setting, nothing changes what the user sees.
//
// The commands only RECORD a request. The radio work (RadioLib setTCXO / setRxBoostedGainMode, the
// warm-sleep + calibrate RX re-entry, the noise restart) runs from loop() through radioDiagLoop(),
// so the USB command stack never blocks on the radio (the EST-350 rule). The status code is printed
// by the loop's `[radio] apply ...` line and kept for `radio status`.
#pragma once

#include <stdint.h>

namespace om {
namespace rdiag {

// -1 = this change did not ask RadioLib (the value is unchanged, or no apply has run yet).
constexpr int kStatusNone = -1;

// --- the A/B requests (RAM only) ----------------------------------------------------------------
// False for a voltage other than 1.8 / 3.0; the parser refuses those first, this is the second door.
bool requestTcxo(float volts);
bool requestBoost(bool on);
bool pending();   // a request is waiting for the loop

// The voltage the chip was last told at runtime (target.h's 1.8 V until the first apply), and the
// RadioLib status of the last setTCXO / setRxBoostedGainMode.
float tcxoVolts();
int lastTcxoStatus();
int lastBoostStatus();
bool boostOn();   // the loop's read-back of the chip's RX gain register (refreshed every
                  // 5 s and after every apply), never the last request

// --- `radio sample <seconds>` -------------------------------------------------------------------
// One line every 5 s and one summary at the end, all printed by loop(). Nothing is stored to SD.
bool sampleStart(int seconds, uint32_t nowMs);   // false when one is already running
bool sampleActive();
int sampleSecondsLeft(uint32_t nowMs);

// One line per received packet while a sample runs: rssi, snr, and the first payload byte (a
// sender prefix for an advert/response, the channel hash for a group text) as two hex digits.
// Called from recvRaw(), i.e. on the loop task; never prints message text or a name.
void notePacket(const uint8_t* raw, int len, int rssi, int snr4);

// Once per loop pass: applies a pending A/B request, then the 5 s sample lines and the summary.
void loop(uint32_t nowMs);

}  // namespace rdiag
}  // namespace om
