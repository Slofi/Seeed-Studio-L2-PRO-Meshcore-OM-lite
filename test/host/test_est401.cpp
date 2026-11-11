// SPDX-License-Identifier: MIT
// EST-401 host test: step 1 of the charging bolt, the arithmetic half.
//
//   1. POWER. The three datasheet bits this build sets (PWR0 bandgap/wake, PWR1 receiver and current
//      references, PWR2 measure block), the byte built from a read-back (0x01 -> 0x07, bit 3 kept,
//      0x0F unchanged), the rule that refuses to write back a byte that is not a POWER value, and the
//      read-back rule that decides "the write took".
//   2. STATUS0. VBUSOK is bit 7 and nothing else: 0x80 present, 0x7F absent, and the probe's own
//      decode still agrees with the new helper.
//   3. REGISTER MAP. The address and the two register numbers the ticket names, so a typo cannot
//      silently point the write at another register (the source scan is test_est401.py).
//   4. CSV. The four appended columns, in the ticket's order, with -1 for a failed STATUS0 read and 0
//      for a failed ADC channel: an unreadable cable must never look like "no cable".
// Run by test/host/run.sh (system g++), no Arduino and no device.
#include <stdio.h>
#include <string.h>

#include "diag_logic.h"
#include "power_log_logic.h"

static int g_fail = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++g_fail;                                                           \
    }                                                                     \
  } while (0)

// ------------------------------------------------------------------------------------- 1. POWER
static void testPower() {
  // The ticket's three groups, one bit each, and the mask they make.
  CHECK(om::diag::kPowerBandgapBit == 0x01);
  CHECK(om::diag::kPowerReferencesBit == 0x02);
  CHECK(om::diag::kPowerMeasureBit == 0x04);
  CHECK(om::diag::kPowerMeasureOnMask == 0x07);

  // The measured power-on value (0x01: bandgap and wake only) becomes the three bits on.
  CHECK(om::diag::powerValueToWrite(0x01) == 0x07);
  // A chip that lost everything still gets the three bits.
  CHECK(om::diag::powerValueToWrite(0x00) == 0x07);
  // Something already powered stays as it was (the write is idempotent).
  CHECK(om::diag::powerValueToWrite(0x0F) == 0x0F);
  // A bit this build does not name (bit 3) is PRESERVED by the OR, never cleared and never invented.
  CHECK(om::diag::powerValueToWrite(0x09) == 0x0F);

  // ...but a byte that is not a POWER value at all is never written back: bits 7:4 are not the PWR
  // field, so the attempt fails closed (the glue checks this BEFORE building the byte).
  CHECK(om::diag::powerReadSane(0x01));   // the measured value
  CHECK(om::diag::powerReadSane(0x00));
  CHECK(om::diag::powerReadSane(0x07));
  CHECK(om::diag::powerReadSane(0x09));   // bit 3 set by the hardware: still writable
  CHECK(om::diag::powerReadSane(0x0F));
  CHECK(!om::diag::powerReadSane(0x10));
  CHECK(!om::diag::powerReadSane(0x81));
  CHECK(!om::diag::powerReadSane(0xF0));
  CHECK(!om::diag::powerReadSane(0xFF));  // a bus that answers 0xFF is not written back

  // "The write took" is decided on the read-back, not on the write's return code.
  CHECK(om::diag::powerBitsPresent(0x07));
  CHECK(om::diag::powerBitsPresent(0x0F));
  CHECK(!om::diag::powerBitsPresent(0x01));   // the measured power-on value: not on
  CHECK(!om::diag::powerBitsPresent(0x05));   // the measure block alone is not enough
  CHECK(!om::diag::powerBitsPresent(0x03));   // the references without the measure block
  CHECK(!om::diag::powerBitsPresent(0x00));
}

// ----------------------------------------------------------------------------------- 2. STATUS0
static void testStatus0() {
  CHECK(om::diag::status0VbusOk(0x80));    // bit 7 set: cable power present
  CHECK(om::diag::status0VbusOk(0xFF));
  CHECK(om::diag::status0VbusOk(0x81));    // the measured STATUS0 with VBUSOK forced on
  CHECK(!om::diag::status0VbusOk(0x00));
  CHECK(!om::diag::status0VbusOk(0x01));   // the measured value on USB before the write
  CHECK(!om::diag::status0VbusOk(0x7F));   // every other bit set, VBUSOK clear

  // The probe's own decode uses the same rule (one bit test, not two).
  om::diag::RegisterSet regs;
  regs.valid[om::diag::kRegStatus0] = true;
  regs.value[om::diag::kRegStatus0] = 0xB1;   // VBUSOK 1, BC_LVL 3, COMP 0, low nibble 1
  const om::diag::Fusb302 f = om::diag::fusb302Fields(regs);
  CHECK(f.haveStatus0);
  CHECK(f.vbusOk);
  CHECK(f.vbusOk == om::diag::status0VbusOk(0xB1));
  CHECK(f.bcLvl == 3);
  CHECK(!f.comp);

  regs.value[om::diag::kRegStatus0] = 0x31;   // the same byte with bit 7 cleared
  const om::diag::Fusb302 g = om::diag::fusb302Fields(regs);
  CHECK(!g.vbusOk);
  CHECK(g.bcLvl == 3);
}

// ------------------------------------------------------------------------------ 3. REGISTER MAP
static void testRegisterMap() {
  CHECK(om::diag::kUsbPdAddr == 0x22);
  CHECK(om::diag::kRegPower == 0x0B);
  CHECK(om::diag::kRegStatus0 == 0x40);
  // POWER and the read-only STATUS0 are not each other, and neither is SWITCHES0.
  CHECK(om::diag::kRegPower != om::diag::kRegStatus0);
  CHECK(om::diag::kRegPower != 0x02);
  // STATUS0 stays on the probe's safe list (a write must never be needed to read it).
  CHECK(om::diag::fusbReadable(om::diag::kRegStatus0));
  CHECK(om::diag::fusbReadable(om::diag::kRegPower));
}

// -------------------------------------------------------------------------------------- 4. CSV
static om::plog::Sample sampleForTest() {
  om::plog::Sample s;
  s.battMv = 4012;
  s.screenOn = true;
  s.brightness = 160;
  s.gpsRailOn = true;
  s.fix = true;
  s.sats = 9;
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
  t.hour = 16;
  t.minute = 49;
  return t;
}

// The four new fields, split off the line by hand: a reader that only knows the old 12 columns must
// keep working (they are untouched), and the new ones come last in the ticket's order.
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

static void testCsv() {
  const om::plog::Sample s = sampleForTest();
  const om::plog::LocalStamp t = stampForTest();
  char line[256];
  CHECK(om::plog::formatLine(s, t, line, sizeof(line)) > 0);
  CHECK(strcmp(line,
               "2026-11-11 16:49,4012,1,160,1,1,9,0,0,12,-97,3600,1,3285,576,575\n") == 0);

  // 16 columns: 15 commas, and the header names the four new ones last, in this order.
  int commas = 0;
  for (const char* p = line; *p; ++p) {
    if (*p == ',') ++commas;
  }
  CHECK(commas == 15);
  const char* header = om::plog::kPowerLogHeader;
  const char* tail = strstr(header, ",vbus,ain1,ain2,ain3");
  CHECK(tail != nullptr);
  if (tail != nullptr) CHECK(strcmp(tail, ",vbus,ain1,ain2,ain3") == 0);
  // The old columns keep their positions: the first 12 names are the 12 known ones.
  CHECK(strncmp(header, "time,batt_mv,screen,brightness,gps,fix,sats,wifi,ble,pkts,noise,uptime_s",
                strlen("time,batt_mv,screen,brightness,gps,fix,sats,wifi,ble,pkts,noise,uptime_s")) ==
        0);

  const char* f[16] = {};
  CHECK(split(line, f, 16) == 16);
  // The four new fields are the last four and carry the sample's numbers.
  CHECK(strncmp(f[12], "1,", 2) == 0);
  CHECK(strncmp(f[13], "3285,", 5) == 0);
  CHECK(strncmp(f[14], "576,", 4) == 0);
  CHECK(strncmp(f[15], "575\n", 4) == 0);

  // A cable that is not there: vbus 0, and the ADC numbers are whatever they are.
  om::plog::Sample absent = s;
  absent.vbus = 0;
  CHECK(om::plog::formatLine(absent, t, line, sizeof(line)) > 0);
  CHECK(strstr(line, ",3600,0,3285,576,575\n") != nullptr);

  // A STATUS0 read that failed is -1, NOT 0: "unknown" must be distinguishable from "no cable".
  om::plog::Sample unknown = s;
  unknown.vbus = -1;
  CHECK(om::plog::formatLine(unknown, t, line, sizeof(line)) > 0);
  CHECK(strstr(line, ",3600,-1,3285,576,575\n") != nullptr);

  // The default Sample is "unknown" + 0 on the three channels (the value a caller that fills nothing
  // gets), so an unfilled line can never claim a cable.
  const om::plog::Sample fresh;
  CHECK(fresh.vbus == -1);
  CHECK(fresh.ain1Mv == 0 && fresh.ain2Mv == 0 && fresh.ain3Mv == 0);
  CHECK(om::plog::formatLine(fresh, t, line, sizeof(line)) > 0);
  CHECK(strstr(line, ",0,-1,0,0,0\n") != nullptr);   // a Sample nobody filled: uptime 0

  // The append buffer still fits header + line in the 256-byte buffer the device hands to write().
  CHECK(om::plog::formatLine(s, t, line, sizeof(line)) > 0);
  char buf[256];
  const int n = om::plog::formatAppend(s, t, true, buf, sizeof(buf));
  CHECK(n > 0);
  CHECK(n == (int)(strlen(om::plog::kPowerLogHeader) + 1 + strlen(line)));
}

int main() {
  testPower();
  testStatus0();
  testRegisterMap();
  testCsv();
  if (g_fail == 0) {
    printf("est401: POWER bits, STATUS0 decode, register map and the four CSV columns OK\n");
    return 0;
  }
  printf("est401: %d failure(s)\n", g_fail);
  return 1;
}
