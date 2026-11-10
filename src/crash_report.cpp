// SPDX-License-Identifier: MIT
// Boot crash reporter (EST-341): the device side.
//
// The formatting rules live in crash_report_logic.h/.cpp (plain C++, host-tested). This file only
// talks to the SDK: esp_reset_reason(), esp_core_dump_image_check()/get_summary()/erase() and the
// RTC_NOINIT checkpoint block.
#include "crash_report.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <esp_system.h>

#include "crash_report_logic.h"

namespace om {
namespace {

// Preserved across a panic, a watchdog and esp_restart(); random after a power-on, which the magic
// rejects. It is written on every checkpoint, so it always names the step that was running.
RTC_NOINIT_ATTR BootCheckpoint s_ckpt;
uint32_t s_ckptSeq = 0;

// True when the coredump partition's first four bytes (the dump's data_size) read as erased flash.
// ESP-IDF 4.4's check returns ESP_ERR_INVALID_SIZE for exactly that, so this separates "empty" from
// a real format failure (EST-343). A missing partition or a failed read is not "erased".
bool coreDumpPartitionErased() {
  const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
  if (part == nullptr) return false;
  uint32_t first = 0;
  if (esp_partition_read(part, 0, &first, sizeof(first)) != ESP_OK) return false;
  return first == 0xFFFFFFFFu;
}

BootEnd mapResetReason(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return BootEnd::PowerOn;
    case ESP_RST_SW: return BootEnd::Software;
    case ESP_RST_PANIC: return BootEnd::Panic;
    case ESP_RST_TASK_WDT: return BootEnd::TaskWdt;
    case ESP_RST_INT_WDT: return BootEnd::IntWdt;
    case ESP_RST_WDT: return BootEnd::Wdt;
    case ESP_RST_BROWNOUT: return BootEnd::Brownout;
    case ESP_RST_DEEPSLEEP: return BootEnd::DeepSleep;
    case ESP_RST_EXT: return BootEnd::External;
    default: return BootEnd::Other;
  }
}

const char* resetReasonToken(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_INT_WDT: return "int-wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_EXT: return "external";
    default: return "other";
  }
}

}  // namespace

void crashCheckpoint(const char* step) {
  checkpointSet(s_ckpt, step, ++s_ckptSeq);
}

void crashReportBegin() {
  const esp_reset_reason_t reason = esp_reset_reason();
  const BootEnd end = mapResetReason(reason);
  char line[384];   // the core-dump line with a full 16-frame backtrace is the longest one

  // 1. Why the previous boot ended and how far it got. Only panic/watchdog count as a crash: the
  //    guard's own ESP.restart() and an OTA reboot are software resets.
  if (bootEndIsCrash(end)) {
    formatPreviousBoot(line, sizeof(line), end, s_ckpt);
    Serial.println(line);
  }
  // From here on, a checkpoint belongs to THIS boot.
  checkpointClear(s_ckpt);
  s_ckptSeq = 0;
  Serial.printf("[crash] reset reason: %d (%s)\n", (int)reason, resetReasonToken(reason));

  // 2. A core dump from the crash, if the SDK stored one. Printed once, then erased: an OTA of the
  //    next image must not inherit a stale dump.
  const esp_err_t err = esp_core_dump_image_check();
  if (err == ESP_OK) {
    // Static, not stack: the boot stack is healthy here, but this also keeps the frame small.
    static esp_core_dump_summary_t s_summary;
    const esp_err_t serr = esp_core_dump_get_summary(&s_summary);
    if (serr == ESP_OK) {
      CrashSummary cs = {};
      memcpy(cs.task, s_summary.exc_task, sizeof(s_summary.exc_task));
      cs.task[16] = 0;
      cs.pc = s_summary.exc_pc;
      cs.cause = s_summary.ex_info.exc_cause;
      cs.vaddr = s_summary.ex_info.exc_vaddr;
      cs.depth = (int)s_summary.exc_bt_info.depth;
      cs.corrupted = s_summary.exc_bt_info.corrupted;
      const int depth = (int)(sizeof(cs.backtrace) / sizeof(cs.backtrace[0]));
      for (int i = 0; i < depth; ++i) cs.backtrace[i] = s_summary.exc_bt_info.bt[i];
      formatCrashSummary(line, sizeof(line), cs);
      Serial.println(line);
      // The app that crashed; Claude matches it to the saved EST-340 ELF before addr2line.
      Serial.printf("[crash] core dump app sha256: %s\n", (const char*)s_summary.app_elf_sha256);
      esp_core_dump_image_erase();
      Serial.println("[crash] core dump erased");
    } else {
      // Keep the dump: it was readable as a blob but the summary failed, and erasing it would
      // throw away the only evidence.
      formatCoreDumpState(line, sizeof(line), CoreDumpState::Corrupt, (int)serr);
      Serial.println(line);
    }
  } else {
    // ESP_ERR_NOT_FOUND means there is no coredump partition; INVALID_SIZE means the partition's
    // first word is erased (see coreDumpPartitionErased()). Both are "no dump", worth a line only
    // when a dump was expected, exactly like NOT_FOUND has always been. A real CRC or format failure
    // (e.g. ESP_ERR_INVALID_CRC 0x109) stays "unreadable".
    const bool erased = (err == ESP_ERR_INVALID_SIZE) && coreDumpPartitionErased();
    if (classifyCoreDumpCheck((int)err, erased) == CoreDumpState::None) {
      if (bootEndIsCrash(end)) {
        formatCoreDumpState(line, sizeof(line), CoreDumpState::None, (int)err);
        Serial.println(line);
      }
    } else {
      formatCoreDumpState(line, sizeof(line), CoreDumpState::Unreadable, (int)err);
      Serial.println(line);
    }
  }

  // 3. This boot is now checkpointed from the first mesh step on.
  crashCheckpoint("[om] boot");
}

}  // namespace om
