// SPDX-License-Identifier: MIT
// OM Lite audio — ES8311 codec + class-D amp, a 16 kHz mono chime.
//
// The register sequence is the ES8311 datasheet/vendor bring-up for MCLK = 256 x fs in slave mode
// (the ESP32-S3 is the I2S master). Values are hardware facts, cross-checked against the board's
// reference firmware; the code is ours. No files are needed: the chime is synthesised as a sine.
#pragma once

#include <Arduino.h>

namespace om {

// Codec + I2S + amp. Returns false if the codec does not ACK or the I2S driver cannot install.
bool audioBegin();

// 0..100, codec hardware volume.
void audioVolume(uint8_t pct);

// A single tone; blocks for its duration. Safe to call while a chime is playing from loop().
void audioTone(uint16_t freq, uint16_t ms);

// Release the amp and mute the codec (screen off / idle).
void audioStandby(bool standby);

}  // namespace om
