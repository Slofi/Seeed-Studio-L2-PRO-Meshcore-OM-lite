// SPDX-License-Identifier: MIT
// Pure logic for the boot crash reporter (EST-341): why the previous boot ended, the RTC_NOINIT
// checkpoint text and the one-line core-dump summary.
//
// No Arduino, no ESP-IDF, no allocation: test/host/test_crash_report.cpp compiles this with the
// system g++ and proves the rules; crash_report.cpp only maps the SDK's values into these types
// and prints the strings.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace om {

// ------------------------------------------------------------------------------------------------
// How the previous boot ended
// ------------------------------------------------------------------------------------------------
enum class BootEnd : uint8_t {
  Unknown = 0,
  PowerOn,     // ESP_RST_POWERON
  Software,    // ESP_RST_SW: our own ESP.restart() (the guard retry) or an OTA reboot
  Panic,       // ESP_RST_PANIC: exception/abort; a core dump may be stored
  TaskWdt,     // ESP_RST_TASK_WDT
  IntWdt,      // ESP_RST_INT_WDT
  Wdt,         // ESP_RST_WDT (any other watchdog, incl. the RTC WDT)
  Brownout,    // ESP_RST_BROWNOUT
  DeepSleep,   // ESP_RST_DEEPSLEEP
  External,    // ESP_RST_EXT (external pin)
  Other,
};

const char* bootEndName(BootEnd e);

// Only a panic or a watchdog means the previous boot died mid-init. A clean software reset (our
// guard's retry, an OTA) or a power-on must not claim a crash: the NVS `crashed` flag and this
// report have to agree, or a normal reboot reads like a fault.
bool bootEndIsCrash(BootEnd e);

// ------------------------------------------------------------------------------------------------
// RTC_NOINIT checkpoint: the last `[mc] N ...` step the previous boot reached
// ------------------------------------------------------------------------------------------------
constexpr uint32_t kCheckpointMagic = 0x4F4D434Bu;  // 'O','M','C','K'
constexpr size_t kCheckpointStepMax = 32;           // "[mc] 3 store + mesh begin" is 25 + NUL

struct BootCheckpoint {
  uint32_t magic;
  uint32_t seq;        // bumps on every step, so a repeated step is still visible
  uint32_t reserved;   // keep the char array 4-byte aligned
  char step[kCheckpointStepMax];
};

inline bool checkpointValid(const BootCheckpoint& c) {
  return c.magic == kCheckpointMagic && c.step[0] != 0 &&
         memchr(c.step, 0, kCheckpointStepMax) != nullptr;
}

inline void checkpointClear(BootCheckpoint& c) {
  c.magic = 0;
  c.seq = 0;
  c.reserved = 0;
  c.step[0] = 0;
}

// Copies step (never past the buffer, always NUL-terminated) and bumps seq.
inline void checkpointSet(BootCheckpoint& c, const char* step, uint32_t seq) {
  c.magic = kCheckpointMagic;
  c.seq = seq;
  c.reserved = 0;
  c.step[0] = 0;
  if (step == nullptr) return;
  size_t n = 0;
  while (step[n] != 0 && n + 1 < kCheckpointStepMax) {
    c.step[n] = step[n];
    ++n;
  }
  c.step[n] = 0;
}

// `[crash] previous boot ended in panic; last checkpoint "[mc] 4 prefs" (seq 12)`
// A valid checkpoint is always shown; without one the line says so instead of inventing a step.
int formatPreviousBoot(char* out, size_t cap, BootEnd end, const BootCheckpoint& c);

// ------------------------------------------------------------------------------------------------
// Core dump summary
// ------------------------------------------------------------------------------------------------
// Values copied out of esp_core_dump_summary_t; no ESP-IDF type crosses this boundary.
struct CrashSummary {
  char task[17];          // esp_core_dump_summary_t::exc_task is 16 bytes + NUL here
  uint32_t pc;            // exc_pc
  uint32_t cause;         // ex_info.exc_cause
  uint32_t vaddr;         // ex_info.exc_vaddr
  uint32_t backtrace[16]; // exc_bt_info.bt (depth entries are valid)
  int depth;              // 0..16
  bool corrupted;         // exc_bt_info.corrupted
};

// Xtensa EXCCAUSE names, the ones a firmware panic actually produces. Unknown values stay numeric.
const char* excCauseName(uint32_t cause);

// One printable line:
//   `[crash] stored core dump: task loopTask, PC 0x4201460c, exc cause 28 (LoadProhibited),
//    vaddr 0x00000000, backtrace 0x4202a690 0x4201460c ...`
// `backtrace: none` when depth is 0; `(corrupted)` is appended when the device marked it so.
int formatCrashSummary(char* out, size_t cap, const CrashSummary& s);

enum class CoreDumpState : uint8_t { None, Corrupt, Unreadable };

// `[crash] stored core dump: none` / `corrupt (err 0x103)` / `unreadable (err 0x105)`.
int formatCoreDumpState(char* out, size_t cap, CoreDumpState state, int err);

// ------------------------------------------------------------------------------------------------
// What an unsuccessful esp_core_dump_image_check() means
// ------------------------------------------------------------------------------------------------
// ESP-IDF 4.4's esp_core_dump_partition_and_size_get() reads the partition's first four bytes as the
// dump's data_size; an erased partition reads 0xFFFFFFFF and the function returns
// ESP_ERR_INVALID_SIZE. That is "there is no dump", not damage. A missing coredump partition is
// ESP_ERR_NOT_FOUND, also "none". A real CRC/format failure comes back as ESP_ERR_INVALID_CRC
// (0x109) or another read/format error and stays "unreadable".
constexpr int kEspErrInvalidSize = 0x104;
constexpr int kEspErrNotFound = 0x105;

// None when err means there is no dump (NOT_FOUND, or INVALID_SIZE with the partition's first word
// reading as erased); Unreadable for every other failure. `erased` is computed by the device side by
// reading those four bytes; this function is pure so test/host proves the mapping.
CoreDumpState classifyCoreDumpCheck(int err, bool erased);

}  // namespace om
