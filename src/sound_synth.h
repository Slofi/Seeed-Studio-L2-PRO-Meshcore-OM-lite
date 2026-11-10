// SPDX-License-Identifier: MIT
// EST-359: the OM Lite sound bank, synthesized on the host and on the device from the same table.
//
// Why a synth instead of WAV files in flash: the picker needs several alternatives per event, the
// phrases are short (about 0.1..0.4 s), and every event has to follow the volume setting. A table of
// notes costs a few hundred bytes of flash and is the same code the host test and tools/mksounds.cpp
// exercise; samples would cost about 200 KB of flash for the same set, without the per-event
// alternatives. The voices are sine partials with a soft ADSR envelope and one gentle low-pass, so
// there is no 8-bit edge and no click at start or end.
//
// This file is plain C++ (no Arduino, no allocation): the host g++ builds it for test_est359.cpp and
// for the WAV renderer, and audio.cpp builds it into the firmware.
#pragma once

#include <stdint.h>

namespace om {
namespace sound {

// The ES8311 path runs at 16 kHz (audio.cpp: MCLK = 256 x fs). The same rate is used for the WAVs.
constexpr uint32_t kRate = 16000;
constexpr int kMaxMs = 400;
constexpr int kMaxSamples = (int)((kRate * (uint32_t)kMaxMs) / 1000u);

// One sound per event. `Touch` is the optional tap click, off by default.
enum class Event : uint8_t {
  Boot = 0,
  Message,
  Dm,
  Mention,
  Sent,
  Error,
  NewNode,
  LowBattery,
  Touch,
  Count,
};
constexpr int kEventCount = (int)Event::Count;

// What render() measured. `peak` is the largest absolute 16-bit value after the low-pass and the
// final fade, so a caller can log or assert headroom.
struct RenderInfo {
  int samples = 0;
  int peak = 0;
};

// Renders one event/variant into out[cap] at 16 kHz, 16-bit mono. Returns the number of samples
// written, or 0 for a bad event/variant, a buffer that is too small, or a recipe over kMaxSamples.
// The whole phrase is normalized down (never up) when it would exceed kPeakTarget, so a full-volume
// play cannot clip in the digital domain.
int render(Event ev, uint8_t variant, int16_t* out, int cap, RenderInfo* info);

// The table. `eventKey` is the stable USB word (`message`, `newnode`, ...), `eventLabel` is what the
// Settings page shows. variantName returns nullptr for an index at or past variantCount (the picker's
// "Off" row).
const char* eventKey(Event ev);
const char* eventLabel(Event ev);
int variantCount(Event ev);
const char* variantName(Event ev, uint8_t variant);
uint8_t defaultVariant(Event ev);
bool validVariant(Event ev, uint8_t variant);
int eventMs(Event ev, uint8_t variant);
bool dndExempt(Event ev);   // true: still heard under Do not disturb (error, low battery)

// USB `play <event>`: exact key match (`dm`, `newnode`, `battery`, ...). Returns false for unknown.
bool parseEvent(const char* token, Event* out);

// The synthesis headroom: peak of any rendered sound is at most this (16-bit full scale is 32767).
constexpr int kPeakTarget = 28000;

}  // namespace sound
}  // namespace om
