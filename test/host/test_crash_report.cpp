// SPDX-License-Identifier: MIT
// Host test for the boot crash reporter's pure logic (EST-341): reset-reason classification, the
// RTC_NOINIT checkpoint block and the two one-line formats. The device side only maps SDK values
// into these types, so this is the whole decision surface.
#include <stdio.h>
#include <string.h>

#include "crash_report_logic.h"

static int g_fail = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++g_fail;                                                           \
    }                                                                     \
  } while (0)

// A bounded format must always leave a NUL inside the caller's buffer.
static bool terminated(const char* buf, size_t cap) { return memchr(buf, 0, cap) != nullptr; }

static void testBootEnd() {
  using om::BootEnd;
  CHECK(strcmp(om::bootEndName(BootEnd::Panic), "panic") == 0);
  CHECK(strcmp(om::bootEndName(BootEnd::Software), "software reset") == 0);
  CHECK(strcmp(om::bootEndName(BootEnd::PowerOn), "power-on") == 0);
  CHECK(strcmp(om::bootEndName(BootEnd::Unknown), "unknown") == 0);
  CHECK(strcmp(om::bootEndName(BootEnd::TaskWdt), "task watchdog") == 0);
  CHECK(strcmp(om::bootEndName(BootEnd::IntWdt), "interrupt watchdog") == 0);
  CHECK(strcmp(om::bootEndName(BootEnd::Wdt), "watchdog") == 0);

  // Only a real death counts as a crash; a normal reboot (our guard's retry, an OTA, power-on)
  // must not print a crash line.
  CHECK(om::bootEndIsCrash(BootEnd::Panic));
  CHECK(om::bootEndIsCrash(BootEnd::TaskWdt));
  CHECK(om::bootEndIsCrash(BootEnd::IntWdt));
  CHECK(om::bootEndIsCrash(BootEnd::Wdt));
  CHECK(!om::bootEndIsCrash(BootEnd::PowerOn));
  CHECK(!om::bootEndIsCrash(BootEnd::Software));
  CHECK(!om::bootEndIsCrash(BootEnd::Brownout));
  CHECK(!om::bootEndIsCrash(BootEnd::DeepSleep));
  CHECK(!om::bootEndIsCrash(BootEnd::External));
  CHECK(!om::bootEndIsCrash(BootEnd::Unknown));
  CHECK(!om::bootEndIsCrash(BootEnd::Other));
}

static void testCheckpoint() {
  using om::BootCheckpoint;
  BootCheckpoint c;
  // Random RTC RAM after a power-on fails the magic; a zeroed block fails as well.
  memset(&c, 0xA5, sizeof(c));
  CHECK(!om::checkpointValid(c));
  om::checkpointClear(c);
  CHECK(!om::checkpointValid(c));

  om::checkpointSet(c, "[mc] 3 store + mesh begin", 7);
  CHECK(om::checkpointValid(c));
  CHECK(c.magic == om::kCheckpointMagic);
  CHECK(c.seq == 7);
  CHECK(strcmp(c.step, "[mc] 3 store + mesh begin") == 0);

  // A too-long step is cut inside the buffer and still terminated.
  char longStep[80];
  memset(longStep, 's', sizeof(longStep));
  longStep[sizeof(longStep) - 1] = 0;
  om::checkpointSet(c, longStep, 9);
  CHECK(om::checkpointValid(c));
  CHECK(strlen(c.step) == om::kCheckpointStepMax - 1);

  // Null is a valid (empty) step but not a valid checkpoint.
  om::checkpointSet(c, nullptr, 10);
  CHECK(!om::checkpointValid(c));

  om::checkpointSet(c, "[mc] 4 prefs", 11);
  CHECK(om::checkpointValid(c));
  om::checkpointClear(c);
  CHECK(!om::checkpointValid(c));
  CHECK(c.step[0] == 0);
}

static void testPreviousBoot() {
  using om::BootCheckpoint;
  char line[192];
  BootCheckpoint c;
  om::checkpointSet(c, "[mc] 4 prefs", 12);

  int n = om::formatPreviousBoot(line, sizeof(line), om::BootEnd::Panic, c);
  CHECK(terminated(line, sizeof(line)));
  CHECK(n == (int)strlen(line));
  CHECK(strstr(line, "[crash] previous boot ended in panic") != nullptr);
  CHECK(strstr(line, "[mc] 4 prefs") != nullptr);
  CHECK(strstr(line, "seq 12") != nullptr);

  om::checkpointClear(c);
  om::formatPreviousBoot(line, sizeof(line), om::BootEnd::TaskWdt, c);
  CHECK(strstr(line, "task watchdog") != nullptr);
  CHECK(strstr(line, "no checkpoint recorded") != nullptr);

  // A short buffer stays terminated and never writes past its end.
  char small[24];
  memset(small, 'X', sizeof(small));
  om::formatPreviousBoot(small, sizeof(small), om::BootEnd::Panic, c);
  CHECK(terminated(small, sizeof(small)));
  CHECK(small[sizeof(small) - 1] == 0);

  CHECK(om::formatPreviousBoot(nullptr, 10, om::BootEnd::Panic, c) == 0);
  CHECK(om::formatPreviousBoot(line, 0, om::BootEnd::Panic, c) == 0);
}

static void testCrashSummary() {
  using om::CrashSummary;
  CrashSummary s = {};
  snprintf(s.task, sizeof(s.task), "loopTask");
  s.pc = 0x4201460c;
  s.cause = 28;
  s.vaddr = 0x00000000;
  s.depth = 3;
  s.backtrace[0] = 0x4202a690;
  s.backtrace[1] = 0x4201460c;
  s.backtrace[2] = 0x42002b1c;

  char line[320];
  om::formatCrashSummary(line, sizeof(line), s);
  CHECK(terminated(line, sizeof(line)));
  CHECK(strstr(line, "[crash] stored core dump: task loopTask") != nullptr);
  CHECK(strstr(line, "PC 0x4201460c") != nullptr);
  CHECK(strstr(line, "exc cause 28 (LoadProhibited)") != nullptr);
  CHECK(strstr(line, "vaddr 0x00000000") != nullptr);
  CHECK(strstr(line, "backtrace 0x4202a690 0x4201460c 0x42002b1c") != nullptr);
  CHECK(strstr(line, "(corrupted)") == nullptr);

  // No backtrace at all.
  s.depth = 0;
  om::formatCrashSummary(line, sizeof(line), s);
  CHECK(strstr(line, "backtrace none") != nullptr);

  // A negative depth is clamped to none, a too-large one to the 16-entry array.
  s.depth = -5;
  om::formatCrashSummary(line, sizeof(line), s);
  CHECK(strstr(line, "backtrace none") != nullptr);
  for (int i = 0; i < 16; ++i) s.backtrace[i] = (uint32_t)(0x1000 + i);
  s.depth = 99;
  om::formatCrashSummary(line, sizeof(line), s);
  CHECK(strstr(line, "0x0000100f") != nullptr);
  CHECK(strstr(line, "0x00001010") == nullptr);

  // A corrupt task name is made printable and a missing name becomes "?".
  CrashSummary bad = {};
  bad.task[0] = (char)0x01;
  bad.task[1] = (char)0xFF;
  bad.task[2] = 'a';
  bad.task[3] = 0;
  bad.depth = 0;
  om::formatCrashSummary(line, sizeof(line), bad);
  CHECK(strstr(line, "task ??a") != nullptr);
  CrashSummary empty = {};
  empty.depth = 0;
  empty.cause = 99;
  om::formatCrashSummary(line, sizeof(line), empty);
  CHECK(strstr(line, "task ?,") != nullptr);
  CHECK(strstr(line, "(unknown)") != nullptr);

  // The corrupted flag is reported.
  s.depth = 1;
  s.corrupted = true;
  om::formatCrashSummary(line, sizeof(line), s);
  CHECK(strstr(line, "(corrupted)") != nullptr);

  // A short buffer stays terminated.
  char small[32];
  memset(small, 'X', sizeof(small));
  om::formatCrashSummary(small, sizeof(small), s);
  CHECK(terminated(small, sizeof(small)));
  CHECK(small[sizeof(small) - 1] == 0);
}

static void testCoreDumpState() {
  using om::CoreDumpState;
  char line[96];
  om::formatCoreDumpState(line, sizeof(line), CoreDumpState::None, 0x105);
  CHECK(strcmp(line, "[crash] stored core dump: none") == 0);
  om::formatCoreDumpState(line, sizeof(line), CoreDumpState::Corrupt, 0x103);
  CHECK(strcmp(line, "[crash] stored core dump: corrupt (err 0x103)") == 0);
  om::formatCoreDumpState(line, sizeof(line), CoreDumpState::Unreadable, 0x10a);
  CHECK(strcmp(line, "[crash] stored core dump: unreadable (err 0x10a)") == 0);
  char small[16];
  memset(small, 'X', sizeof(small));
  om::formatCoreDumpState(small, sizeof(small), CoreDumpState::Corrupt, 0x103);
  CHECK(terminated(small, sizeof(small)));
}

static void testCoreDumpCheck() {
  using om::CoreDumpState;
  // No coredump partition: "none", whether or not a dump was expected (the caller decides whether
  // the line is printed).
  CHECK(om::classifyCoreDumpCheck(om::kEspErrNotFound, false) == CoreDumpState::None);
  CHECK(om::classifyCoreDumpCheck(om::kEspErrNotFound, true) == CoreDumpState::None);
  // ESP_ERR_INVALID_SIZE on an erased partition is what ESP-IDF 4.4 returns for an empty dump
  // (data_size reads 0xFFFFFFFF); it is "none", not damage. The same error with a real size field
  // is a format failure and stays "unreadable".
  CHECK(om::classifyCoreDumpCheck(om::kEspErrInvalidSize, true) == CoreDumpState::None);
  CHECK(om::classifyCoreDumpCheck(om::kEspErrInvalidSize, false) == CoreDumpState::Unreadable);
  // A checksum mismatch is the real corruption case.
  CHECK(om::classifyCoreDumpCheck(0x109, false) == CoreDumpState::Unreadable);   // INVALID_CRC
  CHECK(om::classifyCoreDumpCheck(0x109, true) == CoreDumpState::Unreadable);
  CHECK(om::classifyCoreDumpCheck(0x102, false) == CoreDumpState::Unreadable);   // INVALID_ARG
}

static void testCauseNames() {
  CHECK(strcmp(om::excCauseName(0), "IllegalInstruction") == 0);
  CHECK(strcmp(om::excCauseName(20), "InstFetchProhibited") == 0);   // a call through NULL
  CHECK(strcmp(om::excCauseName(28), "LoadProhibited") == 0);        // a read through NULL
  CHECK(strcmp(om::excCauseName(29), "StoreProhibited") == 0);
  CHECK(strcmp(om::excCauseName(99), "unknown") == 0);
}

int main() {
  testBootEnd();
  testCheckpoint();
  testPreviousBoot();
  testCrashSummary();
  testCoreDumpState();
  testCoreDumpCheck();
  testCauseNames();
  if (g_fail) {
    printf("%d crash_report check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("crash_report: all checks passed\n");
  return 0;
}
