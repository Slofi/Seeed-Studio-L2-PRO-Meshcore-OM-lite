// SPDX-License-Identifier: MIT
// EST-361: the synth and the one shared sound library. See sound_synth.h for the model.
//
// A phrase is a few notes; a note is a frequency, a start, a length and a voice. A voice is 2-3 sine
// partials (the bell's are inharmonic, the marimba's fourth-partial ratio, the chime's octaves), one
// ADSR envelope with a 3..8 ms soft attack and an exponential decay, and one gentle one-pole
// low-pass over the whole phrase. Nothing here allocates, and the same code is compiled into the
// firmware and into tools/mksounds.cpp, so the WAV Filip listens to is the waveform the device
// streams.
#include "sound_synth.h"

#include <math.h>
#include <string.h>

namespace om {
namespace sound {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kTailFadeMs = 3;        // final ramp to exactly zero (no click at the end)
constexpr float kLowpassHz = 6500.0f; // gentle: the top partials are still there, the edge is gone
constexpr float kAliasMaxHz = 7200.0f;  // skip a partial above this (Nyquist is 8 kHz)
constexpr int kMaxNotes = 8;          // the longest phrase is six notes (low battery)

enum class Voice : uint8_t { Bell = 0, Marimba, Pluck, Chime, Soft, Count };

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
// nearly harmonic, soft. Chime: octaves. Soft: octaves at a low level. Attack 3..8 ms everywhere.
constexpr VoiceDef kVoices[(int)Voice::Count] = {
    // attack, release, tau, sustain, gain, parts, ratios, amps, upper decay
    {4.0f, 22.0f, 120.0f, 0.00f, 0.62f, 3, {1.00f, 2.00f, 2.76f}, {1.00f, 0.32f, 0.18f}, {1.0f, 1.7f, 2.6f}},
    {3.0f, 16.0f, 45.0f, 0.00f, 0.70f, 3, {1.00f, 3.93f, 7.20f}, {1.00f, 0.24f, 0.06f}, {1.0f, 2.2f, 4.0f}},
    {5.0f, 18.0f, 70.0f, 0.00f, 0.72f, 3, {1.00f, 2.00f, 3.01f}, {1.00f, 0.22f, 0.09f}, {1.0f, 1.6f, 2.4f}},
    {6.0f, 26.0f, 150.0f, 0.00f, 0.62f, 3, {1.00f, 2.00f, 3.00f}, {1.00f, 0.28f, 0.12f}, {1.0f, 1.5f, 2.2f}},
    {8.0f, 30.0f, 110.0f, 0.05f, 0.75f, 3, {1.00f, 2.00f, 3.00f}, {1.00f, 0.16f, 0.05f}, {1.0f, 1.4f, 2.0f}},
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

// The library ids. The array kLibrary below must stay in exactly this order: an id is stored in NVS
// and appending new sounds at the end is what keeps every existing choice stable.
enum SoundId : uint8_t {
  kBootBellTk = 0,
  kBootBellPair,
  kBootChimeUp,
  kBootSoftRise,
  kBootMarimbaUp,
  kBootChimeArp,
  kMsgSoftHi,
  kMsgMarimbaHi,
  kMsgBellHi,
  kMsgSoftHighFifth,
  kMsgMarimbaHighFifth,
  kMsgBellHighFifth,
  kDmSoftWarm,
  kDmSoftDeep,
  kDmSoftBasso,
  kDmSoftLowFifth,
  kDmSoftLowThird,
  kDmSoftDark,
  kMenChimeBright,
  kMenBellHigh,
  kMenChimeGlass,
  kMenChimeTop,
  kMenBellVeryHigh,
  kMenGlassTrio,
  kSentTick,
  kSentBlip,
  kSentTock,
  kSentTickTock,
  kSentTickTick,
  kSentBlipTock,
  kErrLow,
  kErrWarn,
  kErrTriple,
  kErrDeep,
  kErrDouble,
  kErrBellDescend,
  kNewSoftLo,
  kNewMarimbaLo,
  kNewBellLo,
  kNewSoftLowFifth,
  kNewMarimbaLowFifth,
  kNewBellLowFifth,
  kBattLow,
  kBattSigh,
  kBattBell,
  kBattDeep,
  kBattAlarm,
  kBattPulse,
  kTapTick,
  kTapTock,
  kTapClick,
  kTapTik,
  kTapSnap,
  kTapDouble,
};
constexpr int kSoundTotal = (int)kTapDouble + 1;

struct LibrarySound {
  const char* name;
  Family family;
  Event home;
  Recipe recipe;
};

struct EventDef {
  const char* key;
  const char* label;
  const char* tag;
  uint8_t def;        // a library id
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

// The shared library, grouped by family in the picker but stored by id. Every note sequence is
// unique (the host test proves it), so no two choices can sound the same.
//
// Boot keeps Filip's original TK-TK (D7, a short gap, F7) and adds five clearly different motifs.
// Message is the new-node family a fourth (or fifth) UP; new node is the same family a fourth (or
// fifth) DOWN, so the two never blur. Low battery is the long, loud descending figure twice.
constexpr LibrarySound kLibrary[kSoundTotal] = {
    // --- boot home ---------------------------------------------------------------------------
    {"Bell TK-TK", Family::Bells, Event::Boot, R(205, N(0, 60, 2349, Voice::Bell, 200), N(115, 70, 2794, Voice::Bell, 210))},
    {"Bell Pair", Family::Bells, Event::Boot, R(260, N(0, 120, 784, Voice::Bell, 210), N(80, 150, 1047, Voice::Bell, 220))},
    {"Chime Up", Family::Chimes, Event::Boot, R(240, N(0, 110, 1047, Voice::Chime, 210), N(90, 140, 1319, Voice::Chime, 220))},
    {"Soft Rise", Family::Soft, Event::Boot, R(220, N(0, 90, 440, Voice::Soft, 220), N(70, 120, 554, Voice::Soft, 225))},
    {"Marimba Up", Family::Marimba, Event::Boot, R(210, N(0, 100, 880, Voice::Marimba, 220), N(70, 120, 1319, Voice::Marimba, 220))},
    {"Chime Arp", Family::Chimes, Event::Boot, R(260, N(0, 70, 523, Voice::Chime, 210), N(70, 70, 784, Voice::Chime, 215), N(140, 100, 1047, Voice::Chime, 220))},

    // --- message home: the new-node family, a fourth up --------------------------------------
    {"Soft Hi", Family::Soft, Event::Message, R(250, N(0, 100, 880, Voice::Soft, 220), N(90, 130, 698, Voice::Soft, 225))},
    {"Marimba Hi", Family::Marimba, Event::Message, R(230, N(0, 90, 1047, Voice::Marimba, 220), N(80, 120, 784, Voice::Marimba, 220))},
    {"Bell Hi", Family::Bells, Event::Message, R(230, N(0, 90, 1397, Voice::Bell, 210), N(80, 120, 1047, Voice::Bell, 220))},
    {"Soft High Fifth", Family::Soft, Event::Message, R(250, N(0, 100, 988, Voice::Soft, 220), N(90, 130, 784, Voice::Soft, 225))},
    {"Marimba High Fifth", Family::Marimba, Event::Message, R(230, N(0, 90, 1176, Voice::Marimba, 220), N(80, 120, 880, Voice::Marimba, 220))},
    {"Bell High Fifth", Family::Bells, Event::Message, R(240, N(0, 90, 1568, Voice::Bell, 210), N(80, 130, 1176, Voice::Bell, 220))},

    // --- DM home: low and soft ---------------------------------------------------------------
    {"Soft Warm", Family::Soft, Event::Dm, R(300, N(0, 140, 349, Voice::Pluck, 210), N(100, 160, 440, Voice::Pluck, 210))},
    {"Soft Deep", Family::Soft, Event::Dm, R(300, N(0, 150, 294, Voice::Soft, 230), N(110, 170, 349, Voice::Soft, 220))},
    {"Soft Basso", Family::Soft, Event::Dm, R(340, N(0, 160, 196, Voice::Pluck, 220), N(120, 180, 294, Voice::Pluck, 220))},
    {"Soft Low Fifth", Family::Soft, Event::Dm, R(330, N(0, 150, 262, Voice::Soft, 225), N(100, 170, 392, Voice::Soft, 220))},
    {"Soft Low Third", Family::Soft, Event::Dm, R(300, N(0, 140, 220, Voice::Soft, 225), N(100, 160, 262, Voice::Soft, 220))},
    {"Soft Dark", Family::Soft, Event::Dm, R(360, N(0, 170, 165, Voice::Soft, 230), N(120, 180, 220, Voice::Soft, 220))},

    // --- mention home: bright, above a message -----------------------------------------------
    {"Chime Bright", Family::Chimes, Event::Mention, R(220, N(0, 80, 1319, Voice::Chime, 220), N(70, 110, 1760, Voice::Chime, 220))},
    {"Bell High", Family::Bells, Event::Mention, R(240, N(0, 90, 1568, Voice::Bell, 210), N(80, 120, 2093, Voice::Bell, 210))},
    {"Chime Glass", Family::Chimes, Event::Mention, R(200, N(0, 70, 1760, Voice::Chime, 220), N(65, 100, 2637, Voice::Chime, 210))},
    {"Chime Top", Family::Chimes, Event::Mention, R(220, N(0, 80, 2093, Voice::Chime, 215), N(70, 110, 2637, Voice::Chime, 210))},
    {"Bell Very High", Family::Bells, Event::Mention, R(240, N(0, 90, 2093, Voice::Bell, 205), N(80, 120, 2794, Voice::Bell, 210))},
    {"Glass Trio", Family::Chimes, Event::Mention, R(280, N(0, 65, 1568, Voice::Chime, 215), N(65, 65, 2093, Voice::Chime, 215), N(130, 110, 2637, Voice::Chime, 210))},

    // --- sent home: quiet, very short acknowledgements ---------------------------------------
    {"Tick", Family::Ticks, Event::Sent, R(80, N(0, 45, 1319, Voice::Marimba, 95))},
    {"Blip", Family::Ticks, Event::Sent, R(90, N(0, 50, 1568, Voice::Soft, 90))},
    {"Tock", Family::Ticks, Event::Sent, R(95, N(0, 55, 1047, Voice::Pluck, 90))},
    {"Tick Tock", Family::Ticks, Event::Sent, R(120, N(0, 45, 1319, Voice::Marimba, 95), N(50, 55, 1047, Voice::Marimba, 95))},
    {"Tick Tick", Family::Ticks, Event::Sent, R(90, N(0, 35, 1319, Voice::Soft, 90), N(45, 35, 1319, Voice::Soft, 90))},
    {"Blip Tock", Family::Ticks, Event::Sent, R(110, N(0, 40, 1568, Voice::Soft, 90), N(50, 50, 1047, Voice::Pluck, 90))},

    // --- error home: falling, low, slow (one of the two DND-exempt events) -------------------
    {"Error Low", Family::Alerts, Event::Error, R(300, N(0, 120, 440, Voice::Pluck, 220), N(105, 160, 349, Voice::Pluck, 230))},
    {"Error Warn", Family::Alerts, Event::Error, R(310, N(0, 100, 330, Voice::Soft, 240), N(110, 170, 262, Voice::Soft, 240))},
    {"Alert Triple", Family::Alerts, Event::Error, R(300, N(0, 70, 587, Voice::Pluck, 230), N(70, 70, 523, Voice::Pluck, 230), N(140, 130, 440, Voice::Pluck, 235))},
    {"Error Deep", Family::Alerts, Event::Error, R(320, N(0, 110, 262, Voice::Soft, 240), N(100, 180, 196, Voice::Soft, 240))},
    {"Alert Double", Family::Alerts, Event::Error, R(220, N(0, 60, 880, Voice::Pluck, 225), N(90, 60, 880, Voice::Pluck, 225))},
    {"Bell Descend", Family::Bells, Event::Error, R(320, N(0, 110, 392, Voice::Bell, 220), N(100, 160, 294, Voice::Bell, 230))},

    // --- new node home: the same family as message, a fourth down ---------------------------
    {"Soft Lo", Family::Soft, Event::NewNode, R(250, N(0, 100, 494, Voice::Soft, 210), N(90, 130, 392, Voice::Soft, 210))},
    {"Marimba Lo", Family::Marimba, Event::NewNode, R(230, N(0, 90, 587, Voice::Marimba, 210), N(80, 120, 440, Voice::Marimba, 210))},
    {"Bell Lo", Family::Bells, Event::NewNode, R(230, N(0, 90, 784, Voice::Bell, 200), N(80, 120, 587, Voice::Bell, 210))},
    {"Soft Node Low Fifth", Family::Soft, Event::NewNode, R(250, N(0, 100, 415, Voice::Soft, 210), N(90, 130, 330, Voice::Soft, 210))},
    {"Marimba Low Fifth", Family::Marimba, Event::NewNode, R(230, N(0, 90, 523, Voice::Marimba, 210), N(80, 120, 392, Voice::Marimba, 210))},
    {"Bell Low Fifth", Family::Bells, Event::NewNode, R(240, N(0, 90, 698, Voice::Bell, 200), N(80, 130, 523, Voice::Bell, 210))},

    // --- low battery home: 1.25..1.4 s, three descending notes played twice, at the cap ------
    {"Battery Low", Family::Alerts, Event::LowBattery, peak(R(1350, N(0, 300, 440, Voice::Soft, 250), N(250, 300, 349, Voice::Soft, 250), N(500, 350, 262, Voice::Soft, 250), N(700, 300, 440, Voice::Soft, 250), N(950, 300, 349, Voice::Soft, 250), N(1200, 150, 262, Voice::Soft, 250)), 27000)},
    {"Battery Sigh", Family::Alerts, Event::LowBattery, peak(R(1300, N(0, 300, 349, Voice::Soft, 250), N(250, 300, 294, Voice::Soft, 250), N(500, 300, 220, Voice::Soft, 250), N(700, 300, 349, Voice::Soft, 250), N(950, 300, 294, Voice::Soft, 250), N(1200, 100, 220, Voice::Soft, 250)), 27000)},
    {"Battery Bell", Family::Alerts, Event::LowBattery, peak(R(1400, N(0, 320, 392, Voice::Bell, 250), N(270, 320, 330, Voice::Bell, 250), N(540, 380, 262, Voice::Bell, 250), N(750, 320, 392, Voice::Bell, 250), N(1020, 320, 330, Voice::Bell, 250), N(1290, 110, 262, Voice::Bell, 250)), 27000)},
    {"Battery Deep", Family::Alerts, Event::LowBattery, peak(R(1250, N(0, 280, 294, Voice::Soft, 250), N(230, 280, 262, Voice::Soft, 250), N(460, 330, 196, Voice::Soft, 250), N(650, 280, 294, Voice::Soft, 250), N(880, 280, 262, Voice::Soft, 250), N(1110, 140, 196, Voice::Soft, 250)), 27000)},
    {"Battery Alarm", Family::Alerts, Event::LowBattery, peak(R(1300, N(0, 280, 523, Voice::Pluck, 250), N(230, 280, 440, Voice::Pluck, 250), N(460, 330, 349, Voice::Pluck, 250), N(650, 280, 523, Voice::Pluck, 250), N(880, 280, 440, Voice::Pluck, 250), N(1110, 190, 349, Voice::Pluck, 250)), 27000)},
    {"Battery Pulse", Family::Alerts, Event::LowBattery, peak(R(1350, N(0, 300, 440, Voice::Pluck, 250), N(260, 300, 392, Voice::Pluck, 250), N(520, 330, 330, Voice::Pluck, 250), N(720, 300, 440, Voice::Pluck, 250), N(980, 300, 392, Voice::Pluck, 250), N(1240, 110, 330, Voice::Pluck, 250)), 27000)},

    // --- touch home: the optional tap clicks ------------------------------------------------
    {"Tap Tick", Family::Ticks, Event::Touch, R(55, N(0, 30, 1500, Voice::Soft, 85))},
    {"Tap Tock", Family::Ticks, Event::Touch, R(60, N(0, 35, 900, Voice::Soft, 90))},
    {"Tap Click", Family::Ticks, Event::Touch, R(35, N(0, 18, 2200, Voice::Soft, 80))},
    {"Tap Tik", Family::Ticks, Event::Touch, R(50, N(0, 30, 1000, Voice::Soft, 85))},
    {"Tap Snap", Family::Ticks, Event::Touch, R(45, N(0, 20, 1800, Voice::Soft, 80))},
    {"Tap Double", Family::Ticks, Event::Touch, R(80, N(0, 22, 900, Voice::Soft, 90), N(38, 22, 900, Voice::Soft, 90))},
};

const EventDef kEvents[kEventCount] = {
    {"boot", "Boot", "BOOT", kBootBellTk, false},
    {"message", "Message", "MSG", kMsgSoftHi, false},
    {"dm", "Direct message", "DM", kDmSoftWarm, false},
    {"mention", "Mention", "MENT", kMenChimeBright, false},
    {"sent", "Sent", "SENT", kSentTick, false},
    {"error", "Error", "ERR", kErrLow, true},
    {"newnode", "New node", "NODE", kNewSoftLo, false},
    {"battery", "Low battery", "BATT", kBattLow, true},
    {"touch", "Touch click", "TAP", kTapTick, false},
};

// EST-359 stored 0..2 per event (3 or 17 = Off). Map each old index to the closest new sound. Boot's
// three old variants only differed in timbre, so all three land on the original TK-TK.
constexpr uint8_t kLegacy[kEventCount][3] = {
    {kBootBellTk, kBootBellTk, kBootBellTk},
    {kBootChimeUp, kBootBellPair, kBootMarimbaUp},
    {kDmSoftWarm, kDmSoftDeep, kDmSoftBasso},
    {kMenChimeBright, kMenBellHigh, kMenChimeGlass},
    {kSentTick, kSentBlip, kSentTock},
    {kErrLow, kErrWarn, kErrBellDescend},
    {kNewSoftLo, kNewMarimbaLo, kNewBellLo},
    {kBattLow, kBattSigh, kBattBell},
    {kTapTick, kTapTock, kTapClick},
};

const EventDef& defFor(Event ev) {
  const int i = (int)ev;
  return (i >= 0 && i < kEventCount) ? kEvents[i] : kEvents[0];
}

const LibrarySound& soundFor(uint8_t id) {
  return kLibrary[id < kSoundTotal ? id : 0];
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
int renderPass(const Recipe& r, int16_t* out, float scale, RenderInfo* info) {
  const int total = samplesForMs((int)r.ms);
  if (total <= 0) return 0;
  const float lpA = 1.0f - expf(-2.0f * kPi * kLowpassHz / (float)kRate);
  const int fadeN = samplesForMs(kTailFadeMs);
  float lp = 0.0f;
  int peak = 0;
  for (int i = 0; i < total; ++i) {
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
    if (i >= total - fadeN) value *= (float)(total - i) / (float)fadeN;
    int s = (int)lroundf(value * scale * 32767.0f);
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    const int mag = s < 0 ? -s : s;
    if (mag > peak) peak = mag;
    if (out != nullptr) out[i] = (int16_t)s;
  }
  if (info != nullptr) {
    info->samples = total;
    info->peak = peak;
  }
  return total;
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

bool validSound(uint8_t id) { return id < kSoundTotal; }

const char* soundName(uint8_t id) { return validSound(id) ? soundFor(id).name : nullptr; }

Family soundFamily(uint8_t id) { return validSound(id) ? soundFor(id).family : Family::Count; }

const char* familyName(Family family) {
  switch (family) {
    case Family::Bells: return "Bells";
    case Family::Chimes: return "Chimes";
    case Family::Marimba: return "Marimba";
    case Family::Soft: return "Soft";
    case Family::Alerts: return "Alerts";
    case Family::Ticks: return "Ticks";
    default: return "";
  }
}

Event soundHomeEvent(uint8_t id) { return validSound(id) ? soundFor(id).home : Event::Count; }

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
  if (total <= 0 || total > kMaxSamples || total > cap) return 0;

  // An upper bound on the phrase's peak, per instant (see levelBound). Scaling by it means one pass
  // can never overflow 16 bits, whatever a future recipe adds.
  const float bound = levelBound(r);
  const float scale = bound > 1.0f ? (0.90f / bound) : 0.90f;

  RenderInfo local{};
  RenderInfo* measured = info != nullptr ? info : &local;
  const int n = renderPass(r, out, scale, measured);
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

int variantCount(Event ev) {
  const int i = (int)ev;
  if (i < 0 || i >= kEventCount) return 0;
  int count = 0;
  for (int id = 0; id < kSoundTotal; ++id) {
    if ((int)kLibrary[id].home == i) ++count;
  }
  return count;
}

uint8_t variantSoundId(Event ev, uint8_t variant) {
  const int i = (int)ev;
  if (i < 0 || i >= kEventCount) return kSoundOff;
  int seen = 0;
  for (int id = 0; id < kSoundTotal; ++id) {
    if ((int)kLibrary[id].home != i) continue;
    if (seen == (int)variant) return (uint8_t)id;
    ++seen;
  }
  return kSoundOff;
}

const char* variantName(Event ev, uint8_t variant) {
  const uint8_t id = variantSoundId(ev, variant);
  return id == kSoundOff ? nullptr : soundName(id);
}

bool dndExempt(Event ev) { return defFor(ev).dndExempt; }

uint8_t legacyVariantSound(Event ev, uint8_t oldVariant) {
  const int i = (int)ev;
  if (i < 0 || i >= kEventCount || oldVariant >= 3) return kSoundOff;
  return kLegacy[i][oldVariant];
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
