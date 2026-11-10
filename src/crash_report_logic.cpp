// SPDX-License-Identifier: MIT
// Pure logic for the boot crash reporter (EST-341). See crash_report_logic.h.
//
// Plain C++ (no Arduino, no allocation), so test/host/test_crash_report.cpp proves the same code
// the device runs.
#include "crash_report_logic.h"

#include <stdarg.h>

namespace om {
namespace {

// Append with snprintf semantics, but never truncate the string in a way that loses the NUL:
// `at` is the current length, the return value is the new length (clamped to cap - 1).
size_t appendf(char* out, size_t cap, size_t at, const char* fmt, ...) {
  if (out == nullptr || cap == 0) return 0;
  if (at >= cap - 1) return cap - 1;
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(out + at, cap - at, fmt, ap);
  va_end(ap);
  if (n < 0) return at;
  const size_t written = (size_t)n;
  return (at + written < cap) ? at + written : cap - 1;
}

// A task name is a fixed 16-byte field that a corrupt dump can fill with anything; keep the line
// printable and never run past the field.
void copyTaskName(char* dest, size_t cap, const char* src) {
  if (dest == nullptr || cap == 0) return;
  dest[0] = 0;
  if (src == nullptr) return;
  size_t n = 0;
  while (n + 1 < cap && src[n] != 0) {
    const unsigned char c = (unsigned char)src[n];
    dest[n] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    ++n;
  }
  dest[n] = 0;
}

}  // namespace

const char* bootEndName(BootEnd e) {
  switch (e) {
    case BootEnd::PowerOn: return "power-on";
    case BootEnd::Software: return "software reset";
    case BootEnd::Panic: return "panic";
    case BootEnd::TaskWdt: return "task watchdog";
    case BootEnd::IntWdt: return "interrupt watchdog";
    case BootEnd::Wdt: return "watchdog";
    case BootEnd::Brownout: return "brownout";
    case BootEnd::DeepSleep: return "deep-sleep wake";
    case BootEnd::External: return "external reset";
    case BootEnd::Other: return "other";
    case BootEnd::Unknown: break;
  }
  return "unknown";
}

bool bootEndIsCrash(BootEnd e) {
  switch (e) {
    case BootEnd::Panic:
    case BootEnd::TaskWdt:
    case BootEnd::IntWdt:
    case BootEnd::Wdt:
      return true;
    default:
      return false;
  }
}

const char* excCauseName(uint32_t cause) {
  // Xtensa EXCCAUSE values a firmware fault actually produces.
  switch (cause) {
    case 0: return "IllegalInstruction";
    case 1: return "Syscall";
    case 2: return "InstructionFetchError";
    case 3: return "LoadStoreError";
    case 4: return "Level1Interrupt";
    case 5: return "Alloca";
    case 6: return "IntegerDivideByZero";
    case 7: return "Speculation";
    case 8: return "Privileged";
    case 9: return "Unaligned";
    case 12: return "InstrPIFDataError";
    case 13: return "LoadStorePIFDataError";
    case 14: return "InstrPIFAddrError";
    case 15: return "LoadStorePIFAddrError";
    case 16: return "InstTLBMiss";
    case 17: return "InstTLBMultiHit";
    case 18: return "InstFetchPrivilege";
    case 20: return "InstFetchProhibited";
    case 24: return "LoadStoreTLBMiss";
    case 25: return "LoadStoreTLBMultiHit";
    case 26: return "LoadStorePrivilege";
    case 28: return "LoadProhibited";
    case 29: return "StoreProhibited";
    default: return "unknown";
  }
}

int formatPreviousBoot(char* out, size_t cap, BootEnd end, const BootCheckpoint& c) {
  if (out == nullptr || cap == 0) return 0;
  size_t at = appendf(out, cap, 0, "[crash] previous boot ended in %s", bootEndName(end));
  if (checkpointValid(c)) {
    char step[kCheckpointStepMax];
    copyTaskName(step, sizeof(step), c.step);
    at = appendf(out, cap, at, "; last checkpoint \"%s\" (seq %lu)", step,
                 (unsigned long)c.seq);
  } else {
    at = appendf(out, cap, at, "; no checkpoint recorded");
  }
  return (int)at;
}

int formatCrashSummary(char* out, size_t cap, const CrashSummary& s) {
  if (out == nullptr || cap == 0) return 0;
  char task[sizeof(s.task)];
  copyTaskName(task, sizeof(task), s.task);
  if (task[0] == 0) snprintf(task, sizeof(task), "?");

  size_t at = appendf(out, cap, 0,
                      "[crash] stored core dump: task %s, PC 0x%08lx, exc cause %lu (%s), "
                      "vaddr 0x%08lx, backtrace",
                      task, (unsigned long)s.pc, (unsigned long)s.cause,
                      excCauseName(s.cause), (unsigned long)s.vaddr);

  int depth = s.depth;
  if (depth < 0) depth = 0;
  if (depth > (int)(sizeof(s.backtrace) / sizeof(s.backtrace[0]))) {
    depth = (int)(sizeof(s.backtrace) / sizeof(s.backtrace[0]));
  }
  if (depth == 0) {
    at = appendf(out, cap, at, " none");
  } else {
    for (int i = 0; i < depth; ++i) {
      at = appendf(out, cap, at, " 0x%08lx", (unsigned long)s.backtrace[i]);
    }
  }
  if (s.corrupted) at = appendf(out, cap, at, " (corrupted)");
  return (int)at;
}

int formatCoreDumpState(char* out, size_t cap, CoreDumpState state, int err) {
  if (out == nullptr || cap == 0) return 0;
  size_t at = 0;
  switch (state) {
    case CoreDumpState::None:
      at = appendf(out, cap, 0, "[crash] stored core dump: none");
      break;
    case CoreDumpState::Corrupt:
      at = appendf(out, cap, 0, "[crash] stored core dump: corrupt (err 0x%x)", err);
      break;
    case CoreDumpState::Unreadable:
      at = appendf(out, cap, 0, "[crash] stored core dump: unreadable (err 0x%x)", err);
      break;
  }
  return (int)at;
}

}  // namespace om
