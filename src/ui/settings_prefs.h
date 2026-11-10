// SPDX-License-Identifier: MIT
// The radio settings the Settings screen edits, and the ONE store that owns them (plain C++).
//
// MeshCore's companion code owns the real values: NodePrefs in /prefs.json, saved by
// MyMesh::savePrefs(), and applied to the radio by the same setParams/setTxPower calls
// CMD_SET_RADIO_PARAMS and CMD_SET_RADIO_TX_POWER make. This header has no second store: the shell
// injects the same NodePrefs read + savePrefs + radio apply sequence as its backend, and the host
// test injects a fake one to prove the arithmetic and the round trip.
//
// Ranges are upstream's own (lib/meshcore_companion/MyMesh.cpp, CMD_SET_RADIO_PARAMS and
// CMD_SET_RADIO_TX_POWER): freq 150,000..2,500,000 kHz, bw 7,000..500,000 Hz, sf 5..12, cr 5..8,
// tx -9..MAX_LORA_TX_POWER (22 here, the build's LORA_TX_POWER).
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace om {
namespace ui {

constexpr uint32_t kRadioFreqMin = 150000;    // kHz
constexpr uint32_t kRadioFreqMax = 2500000;   // kHz
constexpr uint32_t kRadioBwMin = 7000;        // Hz
constexpr uint32_t kRadioBwMax = 500000;      // Hz
constexpr uint8_t kRadioSfMin = 5;
constexpr uint8_t kRadioSfMax = 12;
constexpr uint8_t kRadioCrMin = 5;
constexpr uint8_t kRadioCrMax = 8;
constexpr int8_t kRadioTxMin = -9;
constexpr int8_t kRadioTxMax = 22;            // LORA_TX_POWER in platformio.ini, the SX1262's max

struct RadioPrefs {
  uint32_t freqKhz = 869618;   // 869.618 MHz, the SLO preset
  uint32_t bwHz = 62500;       // 62.5 kHz
  uint8_t sf = 8;
  uint8_t cr = 8;
  int8_t tx = 22;
};

// What the build is configured with (platformio.ini -D LORA_*). First boot seeds the store from
// this, which is exactly what the radio already runs, so seeding changes nothing on the air.
constexpr RadioPrefs seedRadioPrefs() { return RadioPrefs{869618, 62500, 8, 8, 22}; }

constexpr bool radioPrefsValid(const RadioPrefs& p) {
  return p.freqKhz >= kRadioFreqMin && p.freqKhz <= kRadioFreqMax && p.bwHz >= kRadioBwMin &&
         p.bwHz <= kRadioBwMax && p.sf >= kRadioSfMin && p.sf <= kRadioSfMax && p.cr >= kRadioCrMin &&
         p.cr <= kRadioCrMax && p.tx >= kRadioTxMin && p.tx <= kRadioTxMax;
}

constexpr int8_t clampTxPower(int v) {
  if (v < (int)kRadioTxMin) return kRadioTxMin;
  if (v > (int)kRadioTxMax) return kRadioTxMax;
  return (int8_t)v;
}

// The mock's preset list, with the current SLO values as the first entry (the mock's EU868 row
// listed SF7/CR6, but the node is on SF8/CR8: the first entry must be what we actually run).
struct RadioPreset {
  const char* name;
  const char* desc;
  RadioPrefs prefs;
};

constexpr int kRadioPresetCount = 4;
constexpr int kRadioCustomPreset = kRadioPresetCount;   // the Custom row

constexpr RadioPreset radioPresetAt(int i) {
  switch (i) {
    case 0: return {"EU 868 (SLO)", "869.618 MHz / SF8 / BW62.5 / CR8", RadioPrefs{869618, 62500, 8, 8, 22}};
    case 1: return {"EU 868", "869.618 MHz / SF7 / BW62.5 / CR6", RadioPrefs{869618, 62500, 7, 6, 22}};
    case 2: return {"New Zealand", "917.375 MHz / SF7 / BW62.5 / CR5", RadioPrefs{917375, 62500, 7, 5, 22}};
    default: return {"Australia", "916.575 MHz / SF7 / BW62.5 / CR7", RadioPrefs{916575, 62500, 7, 7, 22}};
  }
}

// Which preset row the current values are, or kRadioCustomPreset.
constexpr int radioPresetIndex(const RadioPrefs& p) {
  for (int i = 0; i < kRadioPresetCount; ++i) {
    const RadioPrefs q = radioPresetAt(i).prefs;
    if (p.freqKhz == q.freqKhz && p.bwHz == q.bwHz && p.sf == q.sf && p.cr == q.cr) return i;
  }
  return kRadioCustomPreset;
}

// "869.618" / "62.5", for the steppers and the summary rows. Callers pass a buffer of 16.
inline void radioFreqText(const RadioPrefs& p, char* out, int cap) {
  if (out == nullptr || cap <= 0) return;
  const uint32_t whole = p.freqKhz / 1000;
  const uint32_t frac = p.freqKhz % 1000;
  if (frac == 0) {
    snprintf(out, (size_t)cap, "%lu", (unsigned long)whole);
  } else {
    snprintf(out, (size_t)cap, "%lu.%03lu", (unsigned long)whole, (unsigned long)frac);
  }
}

inline void radioBwText(const RadioPrefs& p, char* out, int cap) {
  if (out == nullptr || cap <= 0) return;
  const uint32_t whole = p.bwHz / 1000;
  const uint32_t frac = p.bwHz % 1000;
  if (frac == 0) {
    snprintf(out, (size_t)cap, "%lu", (unsigned long)whole);
  } else if (frac % 100 == 0) {
    snprintf(out, (size_t)cap, "%lu.%lu", (unsigned long)whole, (unsigned long)(frac / 100));
  } else {
    snprintf(out, (size_t)cap, "%lu.%03lu", (unsigned long)whole, (unsigned long)frac);
  }
}

// --------------------------------------------------------------------------------------------- store
// The page never writes a second store. It calls this class, whose backend is (in the firmware) the
// NodePrefs fields + MyMesh::savePrefs() + radio_driver.setParams()/setTxPower(); the host test
// supplies plain RAM functions and checks the round trip.
class RadioPrefsStore {
 public:
  // read fills `out` from the real prefs; write persists and applies them to the radio.
  using ReadFn = bool (*)(void* ctx, RadioPrefs& out);
  using WriteFn = bool (*)(void* ctx, const RadioPrefs& p);

  void begin(ReadFn read, WriteFn write, void* ctx) {
    read_ = read;
    write_ = write;
    ctx_ = ctx;
  }

  // Loads the store. A failed or invalid read seeds the build values and writes them once, so the
  // first boot cannot leave the radio on zeros. Returns true when it seeded (worth one log line).
  bool load() {
    RadioPrefs p;
    if (read_ != nullptr && read_(ctx_, p) && radioPrefsValid(p)) {
      prefs_ = p;
      return false;
    }
    prefs_ = seedRadioPrefs();
    if (write_ != nullptr) write_(ctx_, prefs_);
    return true;
  }

  bool save() { return write_ != nullptr ? write_(ctx_, prefs_) : false; }

  const RadioPrefs& prefs() const { return prefs_; }

  // The two edits the screen makes. Both refuse out-of-range values wholesale, like upstream's
  // commands (no silent clamp of a frequency; tx power IS clamped to -9..22, as the +/- keys are).
  bool setRadio(uint32_t freqKhz, uint32_t bwHz, uint8_t sf, uint8_t cr) {
    RadioPrefs next = prefs_;
    next.freqKhz = freqKhz;
    next.bwHz = bwHz;
    next.sf = sf;
    next.cr = cr;
    if (!radioPrefsValid(next)) return false;
    prefs_ = next;
    return save();
  }

  bool setTx(int8_t dbm) {
    prefs_.tx = clampTxPower(dbm);
    return save();
  }

  bool setPreset(int index) {
    if (index < 0 || index >= kRadioPresetCount) return false;
    RadioPrefs next = radioPresetAt(index).prefs;
    next.tx = prefs_.tx;   // TX power is its own row: a preset never resets it
    if (!radioPrefsValid(next)) return false;
    prefs_ = next;
    return save();
  }

 private:
  RadioPrefs prefs_ = seedRadioPrefs();
  ReadFn read_ = nullptr;
  WriteFn write_ = nullptr;
  void* ctx_ = nullptr;
};

}  // namespace ui
}  // namespace om
