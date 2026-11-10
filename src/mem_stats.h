// SPDX-License-Identifier: MIT
// EST-357: the one number the Bluetooth/Wi-Fi work is judged by, the INTERNAL free heap.
//
// ESP.getFreeHeap() answers the allocator's default caps and mixes PSRAM into the picture on a board
// with SPIRAM_USE_MALLOC, so it cannot say whether Bluedroid has room. These helpers ask for
// MALLOC_CAP_INTERNAL explicitly: free bytes and the largest single internal block. Print them at
// boot, on every Wi-Fi phase change and every BLE start, and in `status` / `ble status` / `wifi status`.
#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>

namespace om {

constexpr uint32_t kInternalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

inline uint32_t internalFreeBytes() { return heap_caps_get_free_size(kInternalCaps); }
inline uint32_t internalLargestBlock() {
  return heap_caps_get_largest_free_block(kInternalCaps);
}

// One line, same fields everywhere: "[mem] <what>: internal=N largest=M psram=P heap=H".
inline void printMemLine(const char* what) {
  Serial.printf("[mem] %s: internal=%u largest=%u psram=%u heap=%u\n", what, (unsigned)internalFreeBytes(),
                (unsigned)internalLargestBlock(), (unsigned)ESP.getFreePsram(),
                (unsigned)ESP.getFreeHeap());
}

}  // namespace om
