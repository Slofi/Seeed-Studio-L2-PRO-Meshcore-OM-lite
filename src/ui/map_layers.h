// SPDX-License-Identifier: MIT
// EST-370: the Map's layer switches (Traces and Names), the one place that knows their NVS keys and
// their defaults, and the save/load rule. EST-373 adds the third: the hide/show arrow beside the zoom
// label, remembered with the other two so reopening the map keeps the column as it was left. Plain
// C++ (no Arduino): the shell injects the real Preferences read/write, test/host injects RAM, so the
// saved setting is proven without a device.
//
// Both switches default ON and the hidden column defaults OFF. A missing key takes the default
// WITHOUT writing it; a toggle changes the value and writes it in the same call, so the highlight on
// the key always matches the tap.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

constexpr const char* kMapPrefTraces = "traces";          // EST-368
constexpr const char* kMapPrefNames = "names";            // EST-370
constexpr const char* kMapPrefTabsHidden = "tabsHidden";  // EST-373: the column is 4 px slivers
constexpr bool kMapTracesDefault = true;
constexpr bool kMapNamesDefault = true;
constexpr bool kMapTabsHiddenDefault = false;             // a first boot shows the tabs

class MapLayerPrefs {
 public:
  // read: true when the key exists (`out` holds it); false leaves `fallback`. write: true on success.
  using ReadFn = bool (*)(void* ctx, const char* key, bool fallback, bool& out);
  using WriteFn = bool (*)(void* ctx, const char* key, bool value);

  void begin(ReadFn read, WriteFn write, void* ctx) {
    read_ = read;
    write_ = write;
    ctx_ = ctx;
  }

  // Boot: every key once. A missing key or no reader is the default, not an error.
  void load() {
    traces_ = readOr(kMapPrefTraces, kMapTracesDefault);
    names_ = readOr(kMapPrefNames, kMapNamesDefault);
    tabsHidden_ = readOr(kMapPrefTabsHidden, kMapTabsHiddenDefault);
  }

  bool traces() const { return traces_; }
  bool names() const { return names_; }
  bool tabsHidden() const { return tabsHidden_; }

  // A key tap. The in-memory value flips even when the write fails (the screen must follow the
  // finger); the return value is the write's.
  bool toggleTraces() {
    traces_ = !traces_;
    return store(kMapPrefTraces, traces_);
  }
  bool toggleNames() {
    names_ = !names_;
    return store(kMapPrefNames, names_);
  }
  bool toggleTabsHidden() {
    tabsHidden_ = !tabsHidden_;
    return store(kMapPrefTabsHidden, tabsHidden_);
  }

  // A value set without a flip (boot migration, a USB command later).
  bool setTraces(bool v) {
    traces_ = v;
    return store(kMapPrefTraces, v);
  }
  bool setNames(bool v) {
    names_ = v;
    return store(kMapPrefNames, v);
  }
  bool setTabsHidden(bool v) {
    tabsHidden_ = v;
    return store(kMapPrefTabsHidden, v);
  }

 private:
  bool readOr(const char* key, bool fallback) {
    bool out = fallback;
    if (read_ != nullptr && read_(ctx_, key, fallback, out)) return out;
    return fallback;
  }
  bool store(const char* key, bool value) {
    return write_ != nullptr ? write_(ctx_, key, value) : false;
  }

  ReadFn read_ = nullptr;
  WriteFn write_ = nullptr;
  void* ctx_ = nullptr;
  bool traces_ = kMapTracesDefault;
  bool names_ = kMapNamesDefault;
  bool tabsHidden_ = kMapTabsHiddenDefault;
};

}  // namespace ui
}  // namespace om
