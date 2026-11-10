// SPDX-License-Identifier: MIT
// EST-361: the OM Lite shared sound library.
//
// One flat library of synth phrases. Every notification event picks any sound from it, so the picker
// shows the whole library grouped by family instead of a per-event list. A stored choice is a library
// id (stable forever, append new sounds at the end), never an index into a per-event list.
//
// Why a synth instead of WAV files in flash: the phrases are short, the picker needs many of them, and
// every event has to follow the volume setting. The voices are sine partials with a soft ADSR envelope
// and one gentle low-pass, so there is no 8-bit edge and no click at start or end.
//
// This file is plain C++ (no Arduino, no allocation): the host g++ builds it for test_est359.cpp and
// for the WAV renderer, and audio.cpp builds it into the firmware.
#pragma once

#include <stdint.h>

namespace om {
namespace sound {

// The ES8311 path runs at 16 kHz (audio.cpp: MCLK = 256 x fs). The same rate is used for the WAVs.
constexpr uint32_t kRate = 16000;
// The longest phrase is the low-battery warning (1.35 s); every other sound is at most 400 ms.
constexpr int kMaxMs = 1500;
constexpr int kMaxSamples = (int)((kRate * (uint32_t)kMaxMs) / 1000u);

// A stored per-event choice: a library id, or this byte for Off. It is not an index into a list.
constexpr uint8_t kSoundOff = 0xFF;

// One sound per notification event. The event no longer owns a sound list: it owns a choice (a
// library id) and a default.
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

// The picker's sections. `Family::Count` is not a sound family.
enum class Family : uint8_t { Bells = 0, Chimes, Marimba, Soft, Alerts, Ticks, Count };
constexpr int kFamilyCount = (int)Family::Count;

// What render() measured. `peak` is the largest absolute 16-bit value after the low-pass and the
// final fade, so a caller can log or assert headroom.
struct RenderInfo {
  int samples = 0;
  int peak = 0;
};

// What a recipe note carries outside the synth: enough for docs/sounds/INDEX.md.
struct NoteInfo {
  uint16_t atMs = 0;
  uint16_t durMs = 0;
  uint16_t hz = 0;
};

// --- the shared library -------------------------------------------------------------------------
int soundCount();
bool validSound(uint8_t id);
const char* soundName(uint8_t id);
Family soundFamily(uint8_t id);
const char* familyName(Family family);
// The event a sound was written for (where its WAV lives in docs/sounds). Every library sound has
// one; other events can still choose it.
Event soundHomeEvent(uint8_t id);
int soundMs(uint8_t id);
int soundNoteCount(uint8_t id);
bool soundNote(uint8_t id, int index, NoteInfo* out);

// Renders one library sound into out[cap] at 16 kHz, 16-bit mono. Returns the number of samples
// written, or 0 for a bad id, a buffer that is too small, or a recipe over kMaxSamples. A loud sound
// (the battery warning) is normalized to its own target peak; every sound is kept at or below
// kPeakTarget, so a full-volume play cannot clip in the digital domain.
int renderSound(uint8_t id, int16_t* out, int cap, RenderInfo* info);

// --- the event table ----------------------------------------------------------------------------
const char* eventKey(Event ev);     // the stable USB word (`message`, `newnode`, ...)
const char* eventLabel(Event ev);   // what the Settings page shows
const char* eventTag(Event ev);     // the picker's short tag (MSG, DM, MENT, ...)
uint8_t defaultSound(Event ev);     // a library id, not a per-event index

// The event's own variants (its "home" set): at least six per event, in id order. They are shown in
// the same picker as every other sound; these accessors are what the WAV renderer and the migration
// use. variantSoundId returns kSoundOff for an index at or past variantCount.
int variantCount(Event ev);
uint8_t variantSoundId(Event ev, uint8_t variant);
const char* variantName(Event ev, uint8_t variant);

bool dndExempt(Event ev);   // true: still heard under Do not disturb (error, low battery)

// EST-359 stored a per-event variant index (0..2, 3 = Off). Map it to the library id of the closest
// new sound. The migration in the shell uses this; `oldVariant` >= 3 returns kSoundOff.
uint8_t legacyVariantSound(Event ev, uint8_t oldVariant);

// USB `play <event>`: exact key match (`dm`, `newnode`, `battery`, ...). Returns false for unknown.
bool parseEvent(const char* token, Event* out);

// The synthesis headroom: peak of any rendered sound is at most this (16-bit full scale is 32767).
constexpr int kPeakTarget = 28000;

}  // namespace sound
}  // namespace om
