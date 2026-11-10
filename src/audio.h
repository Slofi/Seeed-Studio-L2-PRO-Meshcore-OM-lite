// SPDX-License-Identifier: MIT
// OM Lite audio: ES8311 codec + class-D amp, and the EST-359/361 sound player.
//
// The register sequence is the ES8311 datasheet/vendor bring-up for MCLK = 256 x fs in slave mode
// (the ESP32-S3 is the I2S master). Values are hardware facts, cross-checked against the board's
// reference firmware; the code is ours.
//
// A sound is rendered by sound_synth.cpp into a PSRAM buffer first, then streamed by a small task,
// so the loop only enqueues an event and never waits for a phrase or a sine.
#pragma once

#include <Arduino.h>

#include "sound_synth.h"

namespace om {

// Codec + I2S + amp + the sound task and its PSRAM buffer. Returns false if the codec does not ACK
// or the I2S driver cannot install.
bool audioBegin();

// 0..100, codec hardware volume.
void audioVolume(uint8_t pct);

// Queue one library sound for an event. `soundId` is a sound::soundCount() id (the event only labels
// the log line; any sound can be chosen for any event). Returns false when the audio path is not
// open, the id is not real, or the small queue is full (the newest request is dropped rather than
// stacking old chimes). Rendering happens in the audio task, never here.
bool audioPlay(sound::Event ev, uint8_t soundId);

// Release the amp and mute the codec (screen off / idle).
void audioStandby(bool standby);

// Diagnostics for the boot log and the USB `play` reply.
uint32_t audioPlayedCount();
uint32_t audioDroppedCount();

}  // namespace om
