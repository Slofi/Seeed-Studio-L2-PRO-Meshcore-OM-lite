// SPDX-License-Identifier: MIT
// EST-357: one place for the buffers that moved out of internal RAM into PSRAM.
//
// Why: Bluedroid makes many small allocations that CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096 keeps in
// internal RAM. With Wi-Fi up the internal heap was 53,948 B and `ble on` panicked inside hash_map_set,
// so the big static scratch arrays below stopped competing with the radio stacks for that heap.
//
// These are allocated once at boot (or on the one path that needs them) and never freed, except the
// one-time migration scratch, which its owner frees when the migration ran. The SDK on this core has
// no EXT_RAM_BSS_ATTR (CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY is off), so the memory has to be
// taken at runtime. A board whose PSRAM did not come up falls back to internal RAM and says so.
#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <new>

#include <esp32-hal-psram.h>

namespace om {

// One zeroed array of n T objects in PSRAM, every element constructed. ps_calloc already zeroes;
// the placement new is what makes the objects real (ChatEntry and friends carry default member
// initialisers). Returns nullptr only when both PSRAM and internal RAM refused.
template <typename T>
T* psramArray(size_t n) {
  if (n == 0) return nullptr;
  void* raw = ps_calloc(n, sizeof(T));
  if (raw == nullptr) raw = calloc(n, sizeof(T));
  if (raw == nullptr) return nullptr;
  T* p = static_cast<T*>(raw);
  for (size_t i = 0; i < n; ++i) new (static_cast<void*>(p + i)) T();
  return p;
}

// One object in PSRAM (the JPEGDEC decoder, the DM ring). Same fallback.
template <typename T>
T* psramObject() {
  void* raw = ps_malloc(sizeof(T));
  if (raw == nullptr) raw = malloc(sizeof(T));
  if (raw == nullptr) return nullptr;
  return new (raw) T();
}

}  // namespace om
