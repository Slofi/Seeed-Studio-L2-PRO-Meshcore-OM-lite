// SPDX-License-Identifier: MIT
// EST-359: the synth and the one event -> sound table. See sound_synth.h for why.
//
// A phrase is a few notes; a note is a frequency, a start, a length and a voice. A voice is 2-3 sine
// partials (the bell's are inharmonic, the marimba's fourth-partial ratio, the chime's octaves), one
// ADSR envelope with a 3..8 ms soft attack and an exponential decay, and one gentle one-pole
// low-pass over the whole phrase. Nothing here allocates, and the same code is compiled into the
// firmware and into tools/mksounds.cpp, so the WAV Claude listens to is the waveform the device
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
  uint16_t ms;     // total phrase length, including the release and tail
  uint8_t count;
  Note notes[4];
};

struct Variant {
  const char* name;
  Recipe recipe;
};

struct EventDef {
  const char* key;
  const char* label;
  const Variant* variants;
  uint8_t count;
  uint8_t def;
  bool dndExempt;
};

constexpr Note N(uint16_t atMs, uint16_t durMs, uint16_t hz, Voice voice, uint8_t level) {
  return Note{atMs, durMs, hz, voice, level};
}

constexpr Variant V(const char* name, uint16_t ms, Note a) {
  Variant v{};
  v.name = name;
  v.recipe.ms = ms;
  v.recipe.count = 1;
  v.recipe.notes[0] = a;
  return v;
}

constexpr Variant V(const char* name, uint16_t ms, Note a, Note b) {
  Variant v{};
  v.name = name;
  v.recipe.ms = ms;
  v.recipe.count = 2;
  v.recipe.notes[0] = a;
  v.recipe.notes[1] = b;
  return v;
}

constexpr Variant V(const char* name, uint16_t ms, Note a, Note b, Note c) {
  Variant v{};
  v.name = name;
  v.recipe.ms = ms;
  v.recipe.count = 3;
  v.recipe.notes[0] = a;
  v.recipe.notes[1] = b;
  v.recipe.notes[2] = c;
  return v;
}

// Boot keeps the "TK-TK" character Filip asked for on 2026-11-10: D7 (2349 Hz), a gap, F7 (2794 Hz).
// The three variants are the same two notes in three voices, so the picker changes the timbre, not
// the phrase. The second note sits 55 ms after the first ends; that gap is the "tk-tk".
const Variant kBootVariants[] = {
    V("Bell TK-TK", 205, N(0, 60, 2349, Voice::Bell, 190), N(115, 70, 2794, Voice::Bell, 200)),
    V("Chime TK-TK", 195, N(0, 55, 2349, Voice::Chime, 190), N(110, 65, 2794, Voice::Chime, 200)),
    V("Soft TK-TK", 205, N(0, 60, 2349, Voice::Soft, 210), N(115, 70, 2794, Voice::Soft, 220)),
};

const Variant kMessageVariants[] = {
    V("Chime", 240, N(0, 110, 1047, Voice::Chime, 210), N(90, 140, 1319, Voice::Chime, 220)),
    V("Bell", 260, N(0, 120, 784, Voice::Bell, 210), N(80, 150, 1047, Voice::Bell, 220)),
    V("Marimba", 210, N(0, 100, 880, Voice::Marimba, 220), N(70, 120, 1319, Voice::Marimba, 220)),
};

// DM is deliberately lower and softer than a channel message: warmer, less "alert".
const Variant kDmVariants[] = {
    V("Warm", 300, N(0, 140, 349, Voice::Pluck, 210), N(100, 160, 440, Voice::Pluck, 210)),
    V("Soft", 300, N(0, 150, 294, Voice::Soft, 230), N(110, 170, 349, Voice::Soft, 220)),
    V("Basso", 340, N(0, 160, 196, Voice::Pluck, 220), N(120, 180, 294, Voice::Pluck, 220)),
};

// Mention is brighter and always two notes, so it stands out from a message in a busy room.
const Variant kMentionVariants[] = {
    V("Bright", 220, N(0, 80, 1319, Voice::Chime, 220), N(70, 110, 1760, Voice::Chime, 220)),
    V("Bell", 240, N(0, 90, 1568, Voice::Bell, 210), N(80, 120, 2093, Voice::Bell, 210)),
    V("Glass", 200, N(0, 70, 1760, Voice::Chime, 220), N(65, 100, 2637, Voice::Chime, 210)),
};

// Sent is a quiet, very short acknowledgement: it must not compete with an incoming sound.
const Variant kSentVariants[] = {
    V("Tick", 80, N(0, 45, 1319, Voice::Marimba, 95)),
    V("Blip", 90, N(0, 50, 1568, Voice::Soft, 90)),
    V("Tock", 95, N(0, 55, 1047, Voice::Pluck, 90)),
};

// Error falls, low and slow. It is one of the two sounds that DND lets through.
const Variant kErrorVariants[] = {
    V("Low", 300, N(0, 120, 440, Voice::Pluck, 220), N(105, 160, 349, Voice::Pluck, 230)),
    V("Warn", 310, N(0, 100, 330, Voice::Soft, 240), N(110, 170, 262, Voice::Soft, 240)),
    V("Bell", 320, N(0, 110, 392, Voice::Bell, 220), N(100, 160, 294, Voice::Bell, 230)),
};

// New node stays soft: an advert is information, not an alarm.
const Variant kNewNodeVariants[] = {
    V("Soft", 250, N(0, 100, 659, Voice::Soft, 210), N(90, 130, 523, Voice::Soft, 210)),
    V("Marimba", 230, N(0, 90, 784, Voice::Marimba, 210), N(80, 120, 587, Voice::Marimba, 210)),
    V("Bell", 220, N(0, 80, 1047, Voice::Bell, 200), N(70, 120, 784, Voice::Bell, 210)),
};

// Low battery is a low two-note sigh; the other DND-exempt sound.
const Variant kLowBatteryVariants[] = {
    V("Low", 380, N(0, 160, 220, Voice::Soft, 240), N(140, 200, 175, Voice::Soft, 240)),
    V("Alert", 380, N(0, 90, 330, Voice::Soft, 230), N(100, 90, 262, Voice::Soft, 230),
      N(200, 150, 220, Voice::Soft, 240)),
    V("Bell", 360, N(0, 140, 294, Voice::Bell, 220), N(130, 180, 220, Voice::Bell, 230)),
};

// The optional touch click: 15..35 ms, quiet. Off by default in the settings.
const Variant kTouchVariants[] = {
    V("Tick", 35, N(0, 18, 1500, Voice::Soft, 85)),
    V("Tock", 40, N(0, 24, 900, Voice::Soft, 90)),
    V("Click", 30, N(0, 15, 2200, Voice::Soft, 80)),
};

#define OM_SOUND_COUNT(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

const EventDef kEvents[kEventCount] = {
    {"boot", "Boot", kBootVariants, OM_SOUND_COUNT(kBootVariants), 0, false},
    {"message", "Message", kMessageVariants, OM_SOUND_COUNT(kMessageVariants), 0, false},
    {"dm", "Direct message", kDmVariants, OM_SOUND_COUNT(kDmVariants), 0, false},
    {"mention", "Mention", kMentionVariants, OM_SOUND_COUNT(kMentionVariants), 0, false},
    {"sent", "Sent", kSentVariants, OM_SOUND_COUNT(kSentVariants), 0, false},
    {"error", "Error", kErrorVariants, OM_SOUND_COUNT(kErrorVariants), 0, true},
    {"newnode", "New node", kNewNodeVariants, OM_SOUND_COUNT(kNewNodeVariants), 0, false},
    {"battery", "Low battery", kLowBatteryVariants, OM_SOUND_COUNT(kLowBatteryVariants), 0, true},
    {"touch", "Touch click", kTouchVariants, OM_SOUND_COUNT(kTouchVariants), 0, false},
};

#undef OM_SOUND_COUNT

const EventDef& defFor(Event ev) {
  const int i = (int)ev;
  return (i >= 0 && i < kEventCount) ? kEvents[i] : kEvents[0];
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

}  // namespace

int render(Event ev, uint8_t variant, int16_t* out, int cap, RenderInfo* info) {
  if (out == nullptr || cap <= 0) return 0;
  if (!validVariant(ev, variant)) return 0;
  const Variant& var = defFor(ev).variants[variant];
  const int total = samplesForMs((int)var.recipe.ms);
  if (total <= 0 || total > kMaxSamples || total > cap) return 0;

  // A cheap upper bound on the phrase's peak: every note at its full level, every partial added in
  // phase (the parts are never all in phase, so the real peak is lower). Scaling by the bound means
  // one pass can never overflow 16 bits, whatever a future recipe adds. The measured peak then only
  // ever scales the buffer DOWN to kPeakTarget, never up, so a quiet sound stays quiet.
  float bound = 0.0f;
  for (int k = 0; k < (int)var.recipe.count; ++k) {
    const Note& n = var.recipe.notes[k];
    const VoiceDef& v = kVoices[(int)n.voice];
    float amps = 0.0f;
    for (int p = 0; p < (int)v.parts; ++p) amps += v.amp[p];
    bound += ((float)n.level / 255.0f) * v.gain * amps;
  }
  float scale = bound > 1.0f ? (0.90f / bound) : 0.90f;
  const int n = renderPass(var.recipe, out, scale, info);
  if (n <= 0) return 0;
  if (info != nullptr && info->peak > kPeakTarget) {
    const float down = (float)kPeakTarget / (float)info->peak;
    int peak = 0;
    for (int i = 0; i < n; ++i) {
      int s = (int)lroundf((float)out[i] * down);
      if (s > 32767) s = 32767;
      if (s < -32768) s = -32768;
      out[i] = (int16_t)s;
      const int mag = s < 0 ? -s : s;
      if (mag > peak) peak = mag;
    }
    info->peak = peak;
  }
  return n;
}

const char* eventKey(Event ev) { return defFor(ev).key; }

const char* eventLabel(Event ev) { return defFor(ev).label; }

int variantCount(Event ev) { return (int)defFor(ev).count; }

const char* variantName(Event ev, uint8_t variant) {
  if (!validVariant(ev, variant)) return nullptr;
  return defFor(ev).variants[variant].name;
}

uint8_t defaultVariant(Event ev) { return defFor(ev).def; }

bool validVariant(Event ev, uint8_t variant) {
  const int i = (int)ev;
  if (i < 0 || i >= kEventCount) return false;
  return variant < defFor(ev).count;
}

int eventMs(Event ev, uint8_t variant) {
  if (!validVariant(ev, variant)) return 0;
  return (int)defFor(ev).variants[variant].recipe.ms;
}

bool dndExempt(Event ev) { return defFor(ev).dndExempt; }

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
