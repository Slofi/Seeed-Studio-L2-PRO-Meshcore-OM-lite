// SPDX-License-Identifier: MIT
// EST-369: the OM Lite shared sound library, organised by SHAPE first (Solo, Duo, Trio, Multi) and
// voice second. EST-361 built one flat library grouped by timbre; this round asks for about ten
// clearly different sounds per style, from clean single beeps up to short multi-note phrases, and
// reduces the near-duplicates of the old set.
//
// One library of synth phrases. Every notification event picks any sound from it, so the picker
// shows the whole library grouped by shape instead of a per-event list. A stored choice is a library
// id (stable forever, append new sounds at the end), never an index into a per-event list.
//
// Ids 0..53 are the EST-361 library. Those sounds are retired but their ids stay accepted: a stored
// choice of a retired id resolves to the nearest kept sound (kRedirect), so nothing in NVS breaks.
// The current sounds are 54 and up, in picker order.
//
// Why a synth instead of WAV files in flash: the phrases are short, the picker needs many of them, and
// every event has to follow the volume setting. The voices are sine partials with a soft ADSR envelope
// and one gentle low-pass, so there is no 8-bit edge and no click at start or end.
//
// This file is plain C++ (no Arduino, no allocation): the host g++ builds it for test_est369.cpp and
// for the WAV renderer, and audio.cpp builds it into the firmware.
#pragma once

#include <stdint.h>

namespace om {
namespace sound {

// The ES8311 path runs at 16 kHz (audio.cpp: MCLK = 256 x fs). The same rate is used for the WAVs.
constexpr uint32_t kRate = 16000;
// The longest phrase is the low-battery warning (1.35 s); a Multi phrase is under 600 ms.
constexpr int kMaxMs = 1500;
constexpr int kMaxSamples = (int)((kRate * (uint32_t)kMaxMs) / 1000u);

// A stored per-event choice: a library id, or this byte for Off. It is not an index into a list.
constexpr uint8_t kSoundOff = 0xFF;

// One sound per notification event. The event owns a choice (a library id) and a default.
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

// The picker's sections, shape first (EST-369). Alert holds the one long low-battery warning.
enum class Shape : uint8_t { Solo = 0, Duo, Trio, Multi, Alert, Count };
constexpr int kShapeCount = (int)Shape::Count;

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
// Every id the firmware accepts: the retired EST-361 ids 0..53 plus the current sounds from 54 up.
int soundCount();
// The sounds the picker and the WAV renderer list, in picker order (shape, then voice).
int libraryCount();
uint8_t libraryId(int index);
bool soundIsCurrent(uint8_t id);
// A stored id as the sound it means now: the id itself when current, the kept replacement for a
// retired EST-361 id, kSoundOff for anything else.
uint8_t resolveSound(uint8_t id);

bool validSound(uint8_t id);
const char* soundName(uint8_t id);
Shape soundShape(uint8_t id);
const char* shapeName(Shape shape);
// The voice in the picker and in INDEX.md ("bell", "soft", "organ", ...). The name starts with it.
const char* soundVoice(uint8_t id);
int soundMs(uint8_t id);
int soundNoteCount(uint8_t id);
bool soundNote(uint8_t id, int index, NoteInfo* out);

// Renders one library sound into out[cap] at 16 kHz, 16-bit mono. Returns the number of samples
// written, or 0 for a bad id or a recipe over kMaxSamples. EST-397: a `cap` shorter than the phrase
// writes `cap` samples, cut with the usual tail fade, instead of returning 0 (the quarter-length
// internal-RAM buffer depends on that). A loud sound (the battery warning) is normalized to its own
// target peak; every sound is kept at or below kPeakTarget, so a full-volume play cannot clip in the
// digital domain.
int renderSound(uint8_t id, int16_t* out, int cap, RenderInfo* info);

// --- the event table ----------------------------------------------------------------------------
const char* eventKey(Event ev);     // the stable USB word (`message`, `newnode`, ...)
const char* eventLabel(Event ev);   // what the Settings page shows
const char* eventTag(Event ev);     // the picker's short tag (MSG, DM, MENT, ...)
uint8_t defaultSound(Event ev);     // a current library id

bool dndExempt(Event ev);   // true: still heard under Do not disturb (error, low battery)

// EST-359 stored a per-event variant index (0..2, 3 = Off). Map it to the kept sound it means now.
// The migration in the shell uses this; `oldVariant` >= 3 returns kSoundOff.
uint8_t legacyVariantSound(Event ev, uint8_t oldVariant);

// USB `play <event>`: exact key match (`dm`, `newnode`, `battery`, ...). Returns false for unknown.
bool parseEvent(const char* token, Event* out);

// The synthesis headroom: peak of any rendered sound is at most this (16-bit full scale is 32767).
constexpr int kPeakTarget = 28000;

}  // namespace sound
}  // namespace om
