// SPDX-License-Identifier: MIT
// EST-369: the synth and the shared sound library, organised by shape. See sound_synth.h.
//
// A phrase is a few notes; a note is a frequency, a start, a length and a voice. A voice is 1-3 sine
// partials (the bell's are inharmonic, the marimba's fourth-partial ratio, the organ's octave stack,
// the kalimba's tine partial), one ADSR envelope with a 2..14 ms soft attack and an exponential decay,
// and one gentle one-pole low-pass over the whole phrase. Nothing here allocates, and the same code is
// compiled into the firmware and into tools/mksounds.cpp, so the WAV Filip listens to is the waveform
// the device streams.
#include "sound_synth.h"

#include <math.h>
#include <string.h>

namespace om {
namespace sound {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kTailFadeMs = 3;          // final ramp to exactly zero (no click at the end)
constexpr float kLowpassHz = 6500.0f;   // gentle: the top partials are still there, the edge is gone
constexpr float kAliasMaxHz = 7200.0f;  // skip a partial above this (Nyquist is 8 kHz)
constexpr int kMaxNotes = 8;            // the longest phrase is six notes (low battery)

enum class Voice : uint8_t { Bell = 0, Marimba, Pluck, Chime, Soft, Beep, Organ, Kalimba, Count };

struct VoiceDef {
  float attackMs;
  float releaseMs;
  float tauMs;     // ADSR decay time constant
  float sustain;   // the level the exponential decay aims at
  float gain;
  uint8_t parts;
  float ratio[3];
  float amp[3];
  float upperDecay[3];  // p > 0: extra decay (1 = same as the fundamental)
};

// Bell: inharmonic partials (1, 2, 2.76). Marimba: the bar's fourth-partial ratio 3.93. Pluck:
// nearly harmonic, soft. Chime: octaves. Soft: octaves at a low level. Beep: one clean partial that
// holds, then releases. Organ: a mellow octave stack. Kalimba: a tine's 5.4 partial over a fast decay.
constexpr VoiceDef kVoices[(int)Voice::Count] = {
    // attack, release, tau, sustain, gain, parts, ratios, amps, upper decay
    {4.0f, 22.0f, 120.0f, 0.00f, 0.62f, 3, {1.00f, 2.00f, 2.76f}, {1.00f, 0.32f, 0.18f}, {1.0f, 1.7f, 2.6f}},
    {3.0f, 16.0f, 45.0f, 0.00f, 0.70f, 3, {1.00f, 3.93f, 7.20f}, {1.00f, 0.24f, 0.06f}, {1.0f, 2.2f, 4.0f}},
    {5.0f, 18.0f, 70.0f, 0.00f, 0.72f, 3, {1.00f, 2.00f, 3.01f}, {1.00f, 0.22f, 0.09f}, {1.0f, 1.6f, 2.4f}},
    {6.0f, 26.0f, 150.0f, 0.00f, 0.62f, 3, {1.00f, 2.00f, 3.00f}, {1.00f, 0.28f, 0.12f}, {1.0f, 1.5f, 2.2f}},
    {8.0f, 30.0f, 110.0f, 0.05f, 0.75f, 3, {1.00f, 2.00f, 3.00f}, {1.00f, 0.16f, 0.05f}, {1.0f, 1.4f, 2.0f}},
    {2.0f, 12.0f, 220.0f, 0.55f, 0.62f, 1, {1.00f, 0.00f, 0.00f}, {1.00f, 0.00f, 0.00f}, {1.0f, 1.0f, 1.0f}},
    {14.0f, 40.0f, 380.0f, 0.40f, 0.52f, 3, {1.00f, 2.00f, 3.00f}, {1.00f, 0.35f, 0.15f}, {1.0f, 1.2f, 1.8f}},
    {2.0f, 14.0f, 55.0f, 0.00f, 0.72f, 3, {1.00f, 5.40f, 2.00f}, {1.00f, 0.20f, 0.10f}, {1.0f, 2.5f, 2.0f}},
};

struct Note {
  uint16_t atMs;
  uint16_t durMs;
  uint16_t hz;
  Voice voice;
  uint8_t level;   // 0..255, relative loudness inside the phrase
};

struct Recipe {
  uint16_t ms;         // total phrase length, including the release and tail
  uint16_t targetPeak; // 0 = only clamp down to kPeakTarget; >0 = normalize to this peak
  uint8_t count;
  Note notes[kMaxNotes];
};

// The current library starts here. Ids below it are the EST-361 sounds, retired but still accepted
// from NVS (kRedirect). The array kLibrary below must stay in exactly this order: an id is stored in
// NVS and appending new sounds at the end is what keeps every existing choice stable.
constexpr uint8_t kFirstCurrent = 54;

// The current sounds, one block per shape. The names start with the voice so the picker row says
// both ("Bell C6" under SOLO) and INDEX.md carries the same word in its voice column.
enum CurrentId : uint8_t {
  // --- Solo: one note, 80..250 ms ---------------------------------------------------------------
  kBeepClean = kFirstCurrent,  // a clean, plain beep
  kBeepHigh,
  kSoftTick,                   // the quiet acknowledgement (Sent, touch)
  kBellC6,
  kBellLow,
  kGlassPing,
  kMarimbaTock,
  kWoodPluck,
  kOrganMellow,
  kKalimbaTine,
  // --- Duo: two notes (seven kept from EST-361, three new) ---------------------------------------
  kBellTk,                     // Filip's TK-TK boot, kept exactly
  kSoftHi,                     // message default, kept
  kMarimbaHi,
  kBellHi,
  kWoodWarm,                   // DM default, kept
  kChimeBright,                // mention default, kept
  kSoftLo,                     // new-node default, kept (Filip likes this voice)
  kOrganLow,
  kKalimbaRise,
  kWoodDouble,
  // --- Trio: three notes, up to 450 ms ----------------------------------------------------------
  kGlassTrio,                  // kept
  kChimeArp,
  kBellRise,
  kBellFall,
  kMarimbaRoll,
  kWoodMotif,
  kSoftMotif,
  kOrganRise,
  kKalimbaTurn,
  kGlassTop,
  // --- Multi: four or more notes, a short phrase or chord, under 600 ms --------------------------
  kBellChime,
  kChimeRun,
  kMarimbaRun,
  kWoodGroove,
  kSoftChord,
  kOrganChord,
  kKalimbaLick,
  kGlassSparkle,
  kBellRoad,
  kAlertMotif,
  // --- Alert: the one long low-battery warning (kept) --------------------------------------------
  kBatteryLow,
};
constexpr int kSoundTotal = (int)kBatteryLow + 1;

struct LibrarySound {
  const char* name;   // starts with the voice word
  const char* voice;  // the picker / INDEX.md word: bell, chime, glass, soft, beep, ...
  Shape shape;
  Recipe recipe;
};

struct EventDef {
  const char* key;
  const char* label;
  const char* tag;
  uint8_t def;        // a current library id
  bool dndExempt;
};

constexpr Note N(uint16_t atMs, uint16_t durMs, uint16_t hz, Voice voice, uint8_t level) {
  return Note{atMs, durMs, hz, voice, level};
}

// R(...) is the recipe maker; the variadic form keeps each library row readable.
template <typename... Notes>
constexpr Recipe R(uint16_t ms, Notes... notes) {
  Recipe r{};
  r.ms = ms;
  r.targetPeak = 0;
  r.count = (uint8_t)sizeof...(notes);
  const Note list[] = {notes...};
  for (int i = 0; i < (int)r.count; ++i) r.notes[i] = list[i];
  return r;
}

constexpr Recipe peak(Recipe r, uint16_t target) {
  r.targetPeak = target;
  return r;
}

// The current library, in picker order: shape first, then the voice inside the shape. Every note
// sequence is unique (the host test proves it), and inside a shape the ten differ in pitch, voice and
// rhythm, not only in level.
constexpr LibrarySound kLibrary[kSoundTotal - kFirstCurrent] = {
    // --- Solo ---------------------------------------------------------------------------------
    {"Beep Clean", "beep", Shape::Solo, R(110, N(0, 110, 880, Voice::Beep, 200))},
    {"Beep High", "beep", Shape::Solo, R(90, N(0, 90, 1568, Voice::Beep, 190))},
    {"Soft Tick", "soft", Shape::Solo, R(80, N(0, 80, 1175, Voice::Soft, 70))},
    {"Bell C6", "bell", Shape::Solo, R(220, N(0, 220, 1047, Voice::Bell, 210))},
    {"Bell Low", "bell", Shape::Solo, R(250, N(0, 250, 659, Voice::Bell, 205))},
    {"Glass Ping", "glass", Shape::Solo, R(180, N(0, 180, 2093, Voice::Chime, 200))},
    {"Marimba Tock", "marimba", Shape::Solo, R(150, N(0, 150, 784, Voice::Marimba, 210))},
    {"Wood Pluck", "wood", Shape::Solo, R(200, N(0, 200, 392, Voice::Pluck, 210))},
    {"Organ Mellow", "organ", Shape::Solo, R(240, N(0, 240, 330, Voice::Organ, 220))},
    {"Kalimba Tine", "kalimba", Shape::Solo, R(180, N(0, 180, 880, Voice::Kalimba, 210))},

    // --- Duo ----------------------------------------------------------------------------------
    // Bell TK-TK is Filip's boot phrase, unchanged: D7 (2349 Hz), a short gap, F7 (2794 Hz).
    {"Bell TK-TK", "bell", Shape::Duo, R(205, N(0, 60, 2349, Voice::Bell, 200), N(115, 70, 2794, Voice::Bell, 210))},
    {"Soft Hi", "soft", Shape::Duo, R(250, N(0, 100, 880, Voice::Soft, 220), N(90, 130, 698, Voice::Soft, 225))},
    {"Marimba Hi", "marimba", Shape::Duo, R(230, N(0, 90, 1047, Voice::Marimba, 220), N(80, 120, 784, Voice::Marimba, 220))},
    {"Bell Hi", "bell", Shape::Duo, R(230, N(0, 90, 1397, Voice::Bell, 210), N(80, 120, 1047, Voice::Bell, 220))},
    {"Wood Warm", "wood", Shape::Duo, R(300, N(0, 140, 349, Voice::Pluck, 210), N(100, 160, 440, Voice::Pluck, 210))},
    {"Chime Bright", "chime", Shape::Duo, R(220, N(0, 80, 1319, Voice::Chime, 220), N(70, 110, 1760, Voice::Chime, 220))},
    {"Soft Lo", "soft", Shape::Duo, R(250, N(0, 100, 494, Voice::Soft, 210), N(90, 130, 392, Voice::Soft, 210))},
    {"Organ Low", "organ", Shape::Duo, R(340, N(0, 180, 262, Voice::Organ, 220), N(140, 200, 330, Voice::Organ, 220))},
    {"Kalimba Rise", "kalimba", Shape::Duo, R(220, N(0, 100, 1047, Voice::Kalimba, 210), N(90, 130, 1319, Voice::Kalimba, 210))},
    {"Wood Double", "wood", Shape::Duo, R(160, N(0, 70, 587, Voice::Pluck, 220), N(90, 70, 587, Voice::Pluck, 220))},

    // --- Trio ---------------------------------------------------------------------------------
    {"Glass Trio", "glass", Shape::Trio, R(280, N(0, 65, 1568, Voice::Chime, 215), N(65, 65, 2093, Voice::Chime, 215), N(130, 110, 2637, Voice::Chime, 210))},
    {"Chime Arp", "chime", Shape::Trio, R(250, N(0, 70, 1047, Voice::Chime, 215), N(65, 70, 1319, Voice::Chime, 215), N(130, 120, 1568, Voice::Chime, 215))},
    {"Bell Rise", "bell", Shape::Trio, R(300, N(0, 90, 523, Voice::Bell, 210), N(80, 90, 659, Voice::Bell, 210), N(160, 140, 784, Voice::Bell, 215))},
    {"Bell Fall", "bell", Shape::Trio, R(300, N(0, 90, 784, Voice::Bell, 215), N(80, 90, 659, Voice::Bell, 210), N(160, 140, 523, Voice::Bell, 210))},
    {"Marimba Roll", "marimba", Shape::Trio, R(260, N(0, 80, 392, Voice::Marimba, 215), N(70, 80, 523, Voice::Marimba, 215), N(140, 120, 659, Voice::Marimba, 215))},
    {"Wood Motif", "wood", Shape::Trio, R(300, N(0, 100, 220, Voice::Pluck, 220), N(90, 100, 330, Voice::Pluck, 215), N(180, 120, 262, Voice::Pluck, 215))},
    {"Soft Motif", "soft", Shape::Trio, R(310, N(0, 90, 440, Voice::Soft, 215), N(80, 90, 415, Voice::Soft, 215), N(160, 150, 349, Voice::Soft, 220))},
    {"Organ Rise", "organ", Shape::Trio, R(400, N(0, 130, 196, Voice::Organ, 220), N(110, 130, 262, Voice::Organ, 220), N(220, 180, 330, Voice::Organ, 220))},
    {"Kalimba Turn", "kalimba", Shape::Trio, R(290, N(0, 90, 880, Voice::Kalimba, 210), N(80, 90, 1047, Voice::Kalimba, 210), N(160, 130, 880, Voice::Kalimba, 210))},
    {"Glass Top", "glass", Shape::Trio, R(250, N(0, 70, 2093, Voice::Chime, 205), N(65, 70, 2349, Voice::Chime, 205), N(130, 120, 2794, Voice::Chime, 205))},

    // --- Multi --------------------------------------------------------------------------------
    {"Bell Chime", "bell", Shape::Multi, R(390, N(0, 80, 659, Voice::Bell, 210), N(70, 80, 784, Voice::Bell, 210), N(140, 80, 988, Voice::Bell, 210), N(210, 180, 1319, Voice::Bell, 215))},
    {"Chime Run", "chime", Shape::Multi, R(315, N(0, 60, 784, Voice::Chime, 210), N(55, 60, 1047, Voice::Chime, 210), N(110, 60, 1319, Voice::Chime, 210), N(165, 150, 1568, Voice::Chime, 215))},
    {"Marimba Run", "marimba", Shape::Multi, R(300, N(0, 60, 523, Voice::Marimba, 210), N(50, 60, 659, Voice::Marimba, 210), N(100, 60, 784, Voice::Marimba, 210), N(150, 150, 1047, Voice::Marimba, 215))},
    {"Wood Groove", "wood", Shape::Multi, R(360, N(0, 70, 392, Voice::Pluck, 215), N(70, 70, 392, Voice::Pluck, 200), N(140, 70, 523, Voice::Pluck, 215), N(210, 150, 659, Voice::Pluck, 220))},
    {"Soft Chord", "soft", Shape::Multi, R(300, N(0, 180, 262, Voice::Soft, 175), N(0, 180, 330, Voice::Soft, 175), N(0, 180, 392, Voice::Soft, 175), N(120, 180, 523, Voice::Soft, 200))},
    {"Organ Chord", "organ", Shape::Multi, R(260, N(0, 160, 196, Voice::Organ, 190), N(20, 160, 262, Voice::Organ, 190), N(40, 160, 330, Voice::Organ, 190), N(60, 200, 392, Voice::Organ, 200))},
    {"Kalimba Lick", "kalimba", Shape::Multi, R(310, N(0, 70, 880, Voice::Kalimba, 205), N(70, 70, 1047, Voice::Kalimba, 210), N(140, 70, 1319, Voice::Kalimba, 210), N(210, 100, 1047, Voice::Kalimba, 205))},
    {"Glass Sparkle", "glass", Shape::Multi, R(275, N(0, 55, 1568, Voice::Chime, 200), N(55, 55, 2093, Voice::Chime, 200), N(110, 55, 2637, Voice::Chime, 200), N(165, 110, 3136, Voice::Chime, 195))},
    {"Bell Road", "bell", Shape::Multi, R(380, N(0, 80, 392, Voice::Bell, 215), N(70, 80, 330, Voice::Bell, 215), N(140, 80, 262, Voice::Bell, 215), N(210, 170, 196, Voice::Bell, 220))},
    // The error alert as a four-note fall: the old Alert Triple plus its resolving note.
    {"Wood Alert", "wood", Shape::Multi, R(370, N(0, 70, 587, Voice::Pluck, 225), N(70, 70, 523, Voice::Pluck, 225), N(140, 70, 440, Voice::Pluck, 225), N(210, 160, 349, Voice::Pluck, 230))},

    // --- Alert --------------------------------------------------------------------------------
    // 1.35 s, three descending notes played twice, at its own (loud) target peak.
    {"Battery Low", "soft", Shape::Alert, peak(R(1350, N(0, 300, 440, Voice::Soft, 250), N(250, 300, 349, Voice::Soft, 250), N(500, 350, 262, Voice::Soft, 250), N(700, 300, 440, Voice::Soft, 250), N(950, 300, 349, Voice::Soft, 250), N(1200, 150, 262, Voice::Soft, 250)), 27000)},
};

// The retired EST-361 ids 0..53 (in their old order) and the kept sound each one now means. The
// nearest kept sound by shape, then voice, then register; the reasoning is in docs/PROGRESS.md.
constexpr uint8_t kRedirect[kFirstCurrent] = {
    64, 67, 69, 70, 66, 75,   // boot: TK-TK, Bell Pair, Chime Up, Soft Rise, Marimba Up, Chime Arp
    65, 66, 67, 65, 66, 67,   // message: Soft Hi, Marimba Hi, Bell Hi, Soft High Fifth, ...
    68, 70, 68, 70, 70, 68,   // DM: Warm, Deep, Basso, Low Fifth, Low Third, Dark
    69, 67, 69, 69, 64, 74,   // mention: Chime Bright, Bell High, Chime Glass, Chime Top, ...
    60, 55, 61, 60, 60, 56,   // sent ticks: Tick, Blip, Tock, Tick Tock, Tick Tick, Blip Tock
    68, 70, 93, 70, 73, 67,   // error: Low, Warn, Triple, Deep, Double, Bell Descend
    70, 66, 67, 70, 66, 67,   // new node: Soft Lo, Marimba Lo, Bell Lo, ...
    94, 94, 94, 94, 94, 94,   // low battery: all six -> the kept warning
    55, 54, 59, 56, 55, 54,   // touch: Tick, Tock, Click, Tik, Snap, Double
};

const EventDef kEvents[kEventCount] = {
    {"boot", "Boot", "BOOT", kBellTk, false},
    {"message", "Message", "MSG", kSoftHi, false},
    {"dm", "Direct message", "DM", kWoodWarm, false},
    {"mention", "Mention", "MENT", kChimeBright, false},
    {"sent", "Sent", "SENT", kSoftTick, false},
    {"error", "Error", "ERR", kBellFall, true},
    {"newnode", "New node", "NODE", kSoftLo, false},
    {"battery", "Low battery", "BATT", kBatteryLow, true},
    // EST-390 (Filip, 2026-11-11): the optional touch sound is a very short SOFT tick. Soft Tick is
    // the quietest short sound in the library: 80 ms, one 1175 Hz soft-voice note at level 70, where
    // the old Beep Clean was 110 ms at level 200. It is a default, not a lock: the picker still
    // offers every sound. OFF stays the stored default (sounds::touchClicks() defaults false).
    {"touch", "Touch sound", "TAP", kSoftTick, false},
};

// EST-359 stored 0..2 per event (3 or 17 = Off). Map each old index to the EST-361 sound it picked;
// that id is then resolved to the kept sound like any other retired id. Boot's three old variants
// only differed in timbre, so all three land on TK-TK.
constexpr uint8_t kLegacy[kEventCount][3] = {
    {0, 0, 0},   // boot: all three -> Bell TK-TK
    {2, 1, 4},   // message: Chime Up, Bell Pair, Marimba Up
    {12, 13, 14},
    {18, 19, 20},
    {24, 25, 26},
    {30, 31, 35},
    {36, 37, 38},
    {42, 43, 44},
    {48, 49, 50},
};

const EventDef& defFor(Event ev) {
  const int i = (int)ev;
  return (i >= 0 && i < kEventCount) ? kEvents[i] : kEvents[0];
}

const LibrarySound& soundFor(uint8_t id) {
  const uint8_t current = resolveSound(id);
  const int index = current == kSoundOff ? 0 : (int)current - (int)kFirstCurrent;
  return kLibrary[index];
}

int samplesForMs(int ms) {
  if (ms < 0) ms = 0;
  return (int)(((uint64_t)kRate * (uint64_t)ms) / 1000u);
}

// Soft attack (smoothstep, zero slope at both ends), exponential decay toward the voice's sustain,
// linear release over the last releaseMs. Exactly zero at the note's end.
float envelopeAt(const VoiceDef& v, float tSec, float durSec, float releaseSec) {
  if (tSec < 0.0f || tSec >= durSec) return 0.0f;
  const float aSec = v.attackMs / 1000.0f;
  float a = 1.0f;
  if (tSec < aSec) {
    a = tSec / aSec;
    a = a * a * (3.0f - 2.0f * a);
  }
  float d = 1.0f;
  if (tSec > aSec) {
    const float tau = v.tauMs / 1000.0f;
    d = v.sustain + (1.0f - v.sustain) * expf(-(tSec - aSec) / tau);
  }
  const float rem = durSec - tSec;
  const float r = rem < releaseSec ? rem / releaseSec : 1.0f;
  return a * d * r;
}

// One pass over the phrase. With out == nullptr it only measures the peak; with a scale it writes
// the scaled samples. The low-pass state and the tail fade run identically in both passes, so the
// measured peak is the peak of the written buffer.
// EST-397: `cap` is the caller's buffer length. A phrase longer than it is CUT at `cap` samples and
// faded out over the normal tail fade, so a short internal-RAM buffer still plays the attack of a
// long sound instead of returning silence.
int renderPass(const Recipe& r, int16_t* out, int cap, float scale, RenderInfo* info) {
  const int total = samplesForMs((int)r.ms);
  if (total <= 0) return 0;
  const int n = (cap > 0 && cap < total) ? cap : total;
  const float lpA = 1.0f - expf(-2.0f * kPi * kLowpassHz / (float)kRate);
  const int fadeSamples = samplesForMs(kTailFadeMs);
  const int fadeN = fadeSamples < n ? fadeSamples : n;
  float lp = 0.0f;
  int peak = 0;
  for (int i = 0; i < n; ++i) {
    const float tSec = (float)i / (float)kRate;
    float sum = 0.0f;
    for (int k = 0; k < (int)r.count; ++k) {
      const Note& n = r.notes[k];
      const float noteT = tSec - (float)n.atMs / 1000.0f;
      const float noteDur = (float)n.durMs / 1000.0f;
      if (noteT < 0.0f || noteT >= noteDur) continue;
      const VoiceDef& v = kVoices[(int)n.voice];
      const float env = envelopeAt(v, noteT, noteDur, v.releaseMs / 1000.0f);
      if (env <= 0.0f) continue;
      const float level = (float)n.level / 255.0f;
      float note = 0.0f;
      for (int p = 0; p < (int)v.parts; ++p) {
        const float f = (float)n.hz * v.ratio[p];
        if (f <= 0.0f || f >= kAliasMaxHz) continue;   // do not alias on this 16 kHz path
        float decay = 1.0f;
        if (p > 0) decay = expf(-noteT * v.upperDecay[p] / (v.tauMs / 1000.0f));
        note += v.amp[p] * decay * sinf(2.0f * kPi * f * noteT);
      }
      sum += note * env * level * v.gain;
    }
    lp += lpA * (sum - lp);
    float value = lp;
    if (fadeN > 0 && i >= n - fadeN) value *= (float)(n - 1 - i) / (float)fadeN;
    int s = (int)lroundf(value * scale * 32767.0f);
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    const int mag = s < 0 ? -s : s;
    if (mag > peak) peak = mag;
    if (out != nullptr) out[i] = (int16_t)s;
  }
  if (info != nullptr) {
    info->samples = n;
    info->peak = peak;
  }
  return n;
}

// The loudest the waveform can be at any moment: the sum of the notes that are sounding then, each
// at its full level and every partial added in phase. Unlike a sum over the whole phrase this does
// not make a long phrase quiet (important for the battery warning), and it is still an upper bound
// the actual partials never reach.
float levelBound(const Recipe& r) {
  float bound = 0.0f;
  for (int t = 0; t < (int)r.ms; ++t) {
    float sum = 0.0f;
    for (int k = 0; k < (int)r.count; ++k) {
      const Note& n = r.notes[k];
      if (t < (int)n.atMs || t >= (int)n.atMs + (int)n.durMs) continue;
      const VoiceDef& v = kVoices[(int)n.voice];
      float amps = 0.0f;
      for (int p = 0; p < (int)v.parts; ++p) amps += v.amp[p];
      sum += ((float)n.level / 255.0f) * v.gain * amps;
    }
    if (sum > bound) bound = sum;
  }
  return bound;
}

// Scale an already rendered buffer to a new peak. Used to bring the battery warning up to its own
// target; the peak is measured again because rounding moves it by a LSB.
int rescaleTo(int16_t* out, int n, int peak, int target) {
  if (out == nullptr || n <= 0 || peak <= 0 || target <= 0 || target == peak) return peak;
  const float factor = (float)target / (float)peak;
  int measured = 0;
  for (int i = 0; i < n; ++i) {
    int s = (int)lroundf((float)out[i] * factor);
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    out[i] = (int16_t)s;
    const int mag = s < 0 ? -s : s;
    if (mag > measured) measured = mag;
  }
  return measured;
}

}  // namespace

int soundCount() { return kSoundTotal; }

int libraryCount() { return kSoundTotal - (int)kFirstCurrent; }

uint8_t libraryId(int index) {
  if (index < 0 || index >= libraryCount()) return kSoundOff;
  return (uint8_t)((int)kFirstCurrent + index);
}

bool soundIsCurrent(uint8_t id) { return id >= kFirstCurrent && id < kSoundTotal; }

uint8_t resolveSound(uint8_t id) {
  if (id >= kFirstCurrent && id < kSoundTotal) return id;
  if (id < kFirstCurrent) return kRedirect[id];
  return kSoundOff;
}

bool validSound(uint8_t id) { return id < kSoundTotal; }

const char* soundName(uint8_t id) { return validSound(id) ? soundFor(id).name : nullptr; }

Shape soundShape(uint8_t id) { return validSound(id) ? soundFor(id).shape : Shape::Count; }

const char* shapeName(Shape shape) {
  switch (shape) {
    case Shape::Solo: return "Solo";
    case Shape::Duo: return "Duo";
    case Shape::Trio: return "Trio";
    case Shape::Multi: return "Multi";
    case Shape::Alert: return "Alert";
    default: return "";
  }
}

const char* soundVoice(uint8_t id) { return validSound(id) ? soundFor(id).voice : nullptr; }

int soundMs(uint8_t id) { return validSound(id) ? (int)soundFor(id).recipe.ms : 0; }

int soundNoteCount(uint8_t id) {
  return validSound(id) ? (int)soundFor(id).recipe.count : 0;
}

bool soundNote(uint8_t id, int index, NoteInfo* out) {
  if (!validSound(id) || out == nullptr || index < 0) return false;
  const Recipe& r = soundFor(id).recipe;
  if (index >= (int)r.count) return false;
  out->atMs = r.notes[index].atMs;
  out->durMs = r.notes[index].durMs;
  out->hz = r.notes[index].hz;
  return true;
}

int renderSound(uint8_t id, int16_t* out, int cap, RenderInfo* info) {
  if (out == nullptr || cap <= 0 || !validSound(id)) return 0;
  const Recipe& r = soundFor(id).recipe;
  const int total = samplesForMs((int)r.ms);
  if (total <= 0 || total > kMaxSamples) return 0;

  // An upper bound on the phrase's peak, per instant (see levelBound). Scaling by it means one pass
  // can never overflow 16 bits, whatever a future recipe adds.
  const float bound = levelBound(r);
  const float scale = bound > 1.0f ? (0.90f / bound) : 0.90f;

  RenderInfo local{};
  RenderInfo* measured = info != nullptr ? info : &local;
  // EST-397: a cap shorter than the phrase truncates it (with the usual tail fade) instead of
  // returning 0, so the quarter-length internal-RAM buffer still plays something.
  const int n = renderPass(r, out, cap, scale, measured);
  if (n <= 0) return 0;

  // A sound with its own target (the battery warning) is brought exactly to that peak. Everything
  // else is only ever scaled DOWN when it would cross kPeakTarget, so a quiet sound stays quiet.
  int target = 0;
  if (r.targetPeak > 0) {
    target = r.targetPeak > kPeakTarget ? kPeakTarget : (int)r.targetPeak;
  } else if (measured->peak > kPeakTarget) {
    target = kPeakTarget;
  }
  if (target > 0 && measured->peak > 0 && measured->peak != target) {
    measured->peak = rescaleTo(out, n, measured->peak, target);
  }
  return n;
}

const char* eventKey(Event ev) { return defFor(ev).key; }

const char* eventLabel(Event ev) { return defFor(ev).label; }

const char* eventTag(Event ev) { return defFor(ev).tag; }

uint8_t defaultSound(Event ev) { return defFor(ev).def; }

bool dndExempt(Event ev) { return defFor(ev).dndExempt; }

uint8_t legacyVariantSound(Event ev, uint8_t oldVariant) {
  const int i = (int)ev;
  if (i < 0 || i >= kEventCount || oldVariant >= 3) return kSoundOff;
  return resolveSound(kLegacy[i][oldVariant]);
}

bool parseEvent(const char* token, Event* out) {
  if (token == nullptr) return false;
  for (int i = 0; i < kEventCount; ++i) {
    if (strcmp(token, kEvents[i].key) == 0) {
      if (out != nullptr) *out = (Event)i;
      return true;
    }
  }
  return false;
}

}  // namespace sound
}  // namespace om
