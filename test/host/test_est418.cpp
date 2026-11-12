// SPDX-License-Identifier: MIT
// EST-418 host test: the battery log's new LAST column, `heard` (satellites heard, the heartbeat's
// `heard=`), beside `sats` (satellites used in the fix). Plain C++ with the system g++, no Arduino.
//
// Why the ticket exists: `sats` is GGA field 7, the satellites USED in the fix, so a whole night of
// `sats 0` means "no fix", not "no sky". The number that says whether the receiver hears the sky
// (enough for GPS time) is the heard count, and it is now the row's 17th field.
//
//   1. HEADER. The old 16 names, unchanged and in the same order, then `heard` at the END. The
//      header comment and the Sample field say what the two GPS columns mean (`sats` = used,
//      `heard` = heard).
//   2. ROW. The first 16 fields are byte-identical to the pre-EST-418 formatter's line (the arm),
//      and `heard` is the field after `ain3`.
//   3. THE S497 CASE. `sats 0` with `heard 9` is a real line: no fix, sky heard.
//   4. EDGES. A default Sample writes 0 (a count, never an "unknown" mark), the full uint8 range
//      (255) prints whole, and header + line still fit the 256-byte buffer the device hands to
//      File::write().
// Run by test/host/run.sh (system g++), no device.
#include <stdio.h>
#include <string.h>

#include "power_log_logic.h"

static int g_fail = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++g_fail;                                                           \
    }                                                                     \
  } while (0)

// The pre-EST-418 header and row, pinned by hand: the arm that says the new column was APPENDED and
// no old position moved.
static const char* kOldHeader =
    "time,batt_mv,screen,brightness,gps,fix,sats,wifi,ble,pkts,noise,uptime_s,vbus,ain1,ain2,ain3";
static const char* kOldRow =
    "2026-11-11 09:41,4012,1,160,1,1,9,0,0,12,-97,3600,1,3285,576,575";

static om::plog::Sample sampleForTest() {
  om::plog::Sample s;
  s.battMv = 4012;
  s.screenOn = true;
  s.brightness = 160;
  s.gpsRailOn = true;
  s.fix = true;
  s.sats = 9;
  s.heard = 15;
  s.wifiOn = false;
  s.bleOn = false;
  s.packets = 12;
  s.noiseDbm = -97;
  s.uptimeSec = 3600;
  s.vbus = 1;
  s.ain1Mv = 3285;
  s.ain2Mv = 576;
  s.ain3Mv = 575;
  return s;
}

static om::plog::LocalStamp stampForTest() {
  om::plog::LocalStamp t;
  t.year = 2026;
  t.month = 11;
  t.day = 11;
  t.hour = 9;
  t.minute = 41;
  return t;
}

// The line split by hand into `maxFields` pointers; the last one still carries the newline.
static int split(const char* line, const char** fields, int maxFields) {
  const char* p = line;
  int n = 0;
  while (n < maxFields) {
    fields[n++] = p;
    const char* comma = strchr(p, ',');
    if (comma == nullptr) break;
    p = comma + 1;
  }
  return n;
}

// ------------------------------------------------------------------------------------ 1. header
static void testHeader() {
  const char* header = om::plog::kPowerLogHeader;
  // The old 16 names are a PREFIX, byte for byte, then the one new name. Nothing reordered.
  CHECK(strncmp(header, kOldHeader, strlen(kOldHeader)) == 0);
  CHECK(strcmp(header + strlen(kOldHeader), ",heard") == 0);
  // 17 columns: 16 commas.
  int commas = 0;
  for (const char* p = header; *p != 0; ++p) {
    if (*p == ',') ++commas;
  }
  CHECK(commas == 16);
  // The two GPS columns are still `sats` (used) and `fix`, with `heard` appended at the end.
  CHECK(strstr(header, ",fix,sats,") != nullptr);
  CHECK(strcmp(header + strlen(header) - strlen("heard"), "heard") == 0);
}

// --------------------------------------------------------------------------------------- 2. row
static void testRow() {
  const om::plog::Sample s = sampleForTest();
  const om::plog::LocalStamp t = stampForTest();
  char line[256];
  CHECK(om::plog::formatLine(s, t, line, sizeof(line)) > 0);
  // The arm: every field the old formatter wrote is in the same place, unchanged.
  CHECK(strncmp(line, kOldRow, strlen(kOldRow)) == 0);
  // Then exactly one new field and the newline.
  CHECK(strcmp(line + strlen(kOldRow), ",15\n") == 0);
  CHECK(strlen(line) == strlen(kOldRow) + (size_t)strlen(",15\n"));

  // The same thing read as columns: 17 of them, the last one `heard`.
  const char* f[17] = {};
  CHECK(split(line, f, 17) == 17);
  CHECK(strncmp(f[15], "575,", 4) == 0);          // ain3 keeps its place
  CHECK(strncmp(f[16], "15\n", 3) == 0);          // heard is the one after it
  CHECK(strncmp(f[6], "9,", 2) == 0);             // sats keeps its place (used in the fix)
}

// ------------------------------------------------------------------------- 3. the S497 case
static void testNoFixButHeard() {
  om::plog::Sample s = sampleForTest();
  s.fix = false;
  s.sats = 0;     // no fix: GGA's used count is 0
  s.heard = 9;    // but the receiver hears nine satellites
  const om::plog::LocalStamp t = stampForTest();
  char line[256];
  CHECK(om::plog::formatLine(s, t, line, sizeof(line)) > 0);
  const char* f[17] = {};
  CHECK(split(line, f, 17) == 17);
  CHECK(strncmp(f[5], "0,", 2) == 0);      // fix 0
  CHECK(strncmp(f[6], "0,", 2) == 0);      // sats 0: no fix says nothing about the sky
  CHECK(strncmp(f[16], "9\n", 2) == 0);    // heard 9: the sky is there

  // A dark sky is a real 0 too (both columns 0), not an "unknown" mark.
  om::plog::Sample dark = s;
  dark.heard = 0;
  CHECK(om::plog::formatLine(dark, t, line, sizeof(line)) > 0);
  CHECK(strstr(line, ",0,0,12,-97,3600,1,3285,576,575,0\n") != nullptr);
}

// ------------------------------------------------------------------------------------- 4. edges
static void testEdges() {
  const om::plog::LocalStamp t = stampForTest();
  char line[256];
  // A Sample nobody filled: heard 0, the count's own zero.
  const om::plog::Sample fresh;
  CHECK(fresh.heard == 0);
  CHECK(om::plog::formatLine(fresh, t, line, sizeof(line)) > 0);
  CHECK(strcmp(line + strlen(line) - strlen(",0\n"), ",0\n") == 0);

  // The whole uint8 range prints whole (no truncation, no sign).
  om::plog::Sample full = sampleForTest();
  full.heard = 255;
  CHECK(om::plog::formatLine(full, t, line, sizeof(line)) > 0);
  CHECK(strcmp(line + strlen(kOldRow), ",255\n") == 0);

  // Header + row still fit the 256-byte buffer the device writes from.
  CHECK(strlen(om::plog::kPowerLogHeader) + 1 + strlen(line) < 256);
  char buf[256];
  const int n = om::plog::formatAppend(full, t, true, buf, sizeof(buf));
  CHECK(n > 0);
  CHECK(n == (int)(strlen(om::plog::kPowerLogHeader) + 1 + strlen(line)));
  // A new file's header names the column and its first row carries the value.
  CHECK(strncmp(buf, om::plog::kPowerLogHeader, strlen(om::plog::kPowerLogHeader)) == 0);
  CHECK(buf[strlen(om::plog::kPowerLogHeader)] == '\n');
  CHECK(strcmp(buf + strlen(om::plog::kPowerLogHeader) + 1, line) == 0);
  // An existing file gets the row only (its old header line stays as it is on the card).
  const int m = om::plog::formatAppend(full, t, false, buf, sizeof(buf));
  CHECK(m == (int)strlen(line));
  CHECK(strcmp(buf, line) == 0);
}

int main() {
  testHeader();
  testRow();
  testNoFixButHeard();
  testEdges();
  if (g_fail != 0) {
    printf("FAILED: %d check(s)\n", g_fail);
    return 1;
  }
  printf("EST-418: heard column ok\n");
  return 0;
}
