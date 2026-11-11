// SPDX-License-Identifier: MIT
// EST-376 host test: the diagnostic command surface, the sampler's summary arithmetic and the
// read-only FUSB302 decode.
//
//   1. COMMANDS. `radio tcxo 1.8|3.0`, `radio boost on|off`, `radio sample <10..600>`, `radio
//      status`, `radio packets` and `pmu probe` parse; a bad TCXO value, a bad boost word, a
//      seconds value outside 10..600, a non-number and an unknown sub-command are all refused, and
//      none of them silently defaults (a default would move the radio without being asked).
//   2. SUMMARY. min / median / max / mean over a known list, sorted in place, odd and even counts,
//      the negative truncation rule, the empty list and a full 120-line run.
//   3. FUSB302. The register list never contains 0x3E/0x3F/0x42 (interrupt, clear on read) or
//      0x43 (the FIFO), and the fields come out of fixed register bytes: DEVICE_ID's version and
//      product nibbles, POWER's four bits, STATUS0's VBUSOK (bit 7), BC_LVL (5:4) and COMP (2).
//   4. PAYLOAD OFFSET. The sampler's `hash=` byte is the same first payload byte the reception ring
//      uses (EST-365): the path and the transport codes are skipped, a short frame has none.
// Run by test/host/run.sh (system g++), no Arduino and no device.
#include <stdio.h>
#include <string.h>

#include "diag_logic.h"
#include "radio_stats.h"
#include "usb_cmd_logic.h"

static int g_fail = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++g_fail;                                                           \
    }                                                                     \
  } while (0)

// ------------------------------------------------------------------------------------ 1. commands
static om::usb::Command parse(const char* text) {
  char line[160];
  snprintf(line, sizeof(line), "%s", text);
  return om::usb::parseCommand(line);
}

static void expectRadio(const char* text, const char* target, const char* value, int count) {
  const om::usb::Command c = parse(text);
  CHECK(c.error == nullptr);
  CHECK(c.kind == om::usb::Kind::Radio);
  if (target == nullptr) {
    CHECK(c.target == nullptr);
  } else {
    CHECK(c.target != nullptr && strcmp(c.target, target) == 0);
  }
  if (value == nullptr) {
    CHECK(c.text == nullptr);
  } else {
    CHECK(c.text != nullptr && strcmp(c.text, value) == 0);
  }
  CHECK(c.count == count);
}

static void expectError(const char* text, const char* error) {
  const om::usb::Command c = parse(text);
  CHECK(c.error != nullptr);
  if (c.error != nullptr) CHECK(strcmp(c.error, error) == 0);
}

static void testCommands() {
  // The two TCXO values the A/B test uses, and nothing else.
  expectRadio("radio tcxo 1.8", "tcxo", "1.8", -1);
  expectRadio("radio tcxo 3.0", "tcxo", "3.0", -1);
  // A quoted value is the same word to the tokenizer.
  expectRadio("radio tcxo \"3.0\"", "tcxo", "3.0", -1);
  expectError("radio tcxo 1.7", "value");
  expectError("radio tcxo 2.4", "value");
  expectError("radio tcxo 0", "value");
  expectError("radio tcxo 3", "value");
  expectError("radio tcxo", "args");
  expectError("radio tcxo 3.0 extra", "args");

  expectRadio("radio boost on", "boost", "on", -1);
  expectRadio("radio boost off", "boost", "off", -1);
  expectError("radio boost 1", "value");
  expectError("radio boost true", "value");
  expectError("radio boost", "args");
  expectError("radio boost on off", "args");

  expectRadio("radio sample 10", "sample", nullptr, 10);
  expectRadio("radio sample 60", "sample", nullptr, 60);
  expectRadio("radio sample 600", "sample", nullptr, 600);
  expectError("radio sample 9", "value");
  expectError("radio sample 601", "value");
  expectError("radio sample 0", "value");
  expectError("radio sample 60s", "number");
  expectError("radio sample -5", "number");
  expectError("radio sample", "args");
  expectError("radio sample 60 now", "args");

  // The forms that already existed still parse.
  expectRadio("radio", nullptr, nullptr, -1);
  expectRadio("radio packets", "packets", nullptr, -1);
  expectRadio("radio status", "status", nullptr, -1);
  expectError("radio nope", "args");
  expectError("radio tcxoo 3.0", "args");

  const om::usb::Command pmu = parse("pmu probe");
  CHECK(pmu.error == nullptr);
  CHECK(pmu.kind == om::usb::Kind::Pmu);
  CHECK(pmu.target != nullptr && strcmp(pmu.target, "probe") == 0);
  expectError("pmu", "args");
  expectError("pmu read", "args");
  expectError("pmu probe 1", "args");
}

// ------------------------------------------------------------------------------------- 2. summary
static void testSummary() {
  {
    int values[] = {-80, -90, -100, -110};
    om::diag::SampleStats st;
    CHECK(om::diag::summarise(values, 4, &st));
    CHECK(st.count == 4);
    CHECK(st.min == -110);
    CHECK(st.max == -80);
    CHECK(st.median == -95);   // (-100 + -90) / 2
    CHECK(st.mean == -95);     // -380 / 4
    // Sorted in place, ascending.
    CHECK(values[0] == -110 && values[1] == -100 && values[2] == -90 && values[3] == -80);
  }
  {
    // Odd count: the middle value, not an average of neighbours.
    int values[] = {5, 1, 3};
    om::diag::SampleStats st;
    CHECK(om::diag::summarise(values, 3, &st));
    CHECK(st.min == 1 && st.max == 5 && st.median == 3 && st.mean == 3);
  }
  {
    // Negative median and mean truncate TOWARD ZERO, as C++ integer division does (a noise floor
    // is printed as a whole dBm, so this rule is visible on the wire).
    int values[] = {-1, -2, -3, -4};
    om::diag::SampleStats st;
    CHECK(om::diag::summarise(values, 4, &st));
    CHECK(st.median == -2);   // (-3 + -2) / 2
    CHECK(st.mean == -2);     // -10 / 4
  }
  {
    // One value: min == median == max == mean.
    int values[] = {-117};
    om::diag::SampleStats st;
    CHECK(om::diag::summarise(values, 1, &st));
    CHECK(st.count == 1 && st.min == -117 && st.median == -117 && st.max == -117 &&
          st.mean == -117);
  }
  {
    // Empty: refused, with a zeroed result (the device prints "-" instead of a fake 0).
    om::diag::SampleStats st;
    st.count = 99;
    CHECK(!om::diag::summarise(nullptr, 0, &st));
    CHECK(st.count == 0 && st.min == 0 && st.median == 0 && st.max == 0 && st.mean == 0);
  }
  {
    // A full 600 s run: 120 values, -60 .. 59. Sum = -60, so the mean is 0; the two middle values
    // are -1 and 0, so the median is 0 too.
    int values[120];
    for (int i = 0; i < 120; ++i) values[i] = i - 60;
    om::diag::SampleStats st;
    CHECK(om::diag::summarise(values, 120, &st));
    CHECK(st.count == 120);
    CHECK(st.min == -60 && st.max == 59);
    CHECK(st.median == 0);
    CHECK(st.mean == 0);
  }
}

// ------------------------------------------------------------------------------------- 3. FUSB302
static void testFusb() {
  // The chip answers at 0x22; the read list is exactly the ticket's twenty registers.
  CHECK(om::diag::kUsbPdAddr == 0x22);
  CHECK(om::diag::kFusbReadCount == 20);

  // The four forbidden registers are refused by name ...
  CHECK(!om::diag::fusbReadable(0x3E));   // interrupt, clear on read
  CHECK(!om::diag::fusbReadable(0x3F));   // interrupt, clear on read
  CHECK(!om::diag::fusbReadable(0x42));   // interrupt, clear on read
  CHECK(!om::diag::fusbReadable(0x43));   // FIFO: a read pops a byte
  // ... and every register in the list is allowed, with no duplicate and no forbidden entry.
  int seen[256] = {};
  for (int i = 0; i < om::diag::kFusbReadCount; ++i) {
    const uint8_t reg = om::diag::kFusbReadRegs[i];
    CHECK(om::diag::fusbReadable(reg));
    CHECK(seen[reg] == 0);
    seen[reg] = 1;
  }
  for (int r = 0x01; r <= 0x10; ++r) CHECK(seen[r] == 1);
  CHECK(seen[0x3C] == 1 && seen[0x3D] == 1 && seen[0x40] == 1 && seen[0x41] == 1);

  // Fixed bytes: a FUSB302 that is powered and sees VBUS at the 500 mA level.
  om::diag::RegisterSet regs;
  regs.value[0x01] = 0x88;   // version 8, product 8
  regs.valid[0x01] = true;
  regs.value[0x0B] = 0x0F;   // PWR3..PWR0 all set
  regs.valid[0x0B] = true;
  regs.value[0x40] = 0x92;   // VBUSOK = 1, BC_LVL = 01, COMP = 0
  regs.valid[0x40] = true;
  om::diag::Fusb302 f = om::diag::fusb302Fields(regs);
  CHECK(f.haveId && f.havePower && f.haveStatus0);
  CHECK(f.deviceId == 0x88);
  CHECK(f.versionId == 0x8);
  CHECK(f.productId == 0x8);
  CHECK(f.power == 0x0F);
  CHECK(f.status0 == 0x92);
  CHECK(f.vbusOk);
  CHECK(f.bcLvl == 1);
  CHECK(!f.comp);

  // A second set: no VBUS, the 1.5 A level and COMP set.
  regs.value[0x40] = 0x64;   // 0110 0100: VBUSOK = 0, BC_LVL = 10, COMP = 1
  f = om::diag::fusb302Fields(regs);
  CHECK(!f.vbusOk);
  CHECK(f.bcLvl == 2);
  CHECK(f.comp);

  // Every bit of STATUS0's VBUSOK is bit 7, not bit 6 or 5.
  for (int bit = 0; bit < 8; ++bit) {
    regs.value[0x40] = (uint8_t)(1U << bit);
    f = om::diag::fusb302Fields(regs);
    CHECK(f.vbusOk == (bit == 7));
  }

  // An unread register is "no field", never a fabricated zero.
  om::diag::RegisterSet empty;
  f = om::diag::fusb302Fields(empty);
  CHECK(!f.haveId && !f.havePower && !f.haveStatus0);
  CHECK(f.deviceId == 0 && f.power == 0 && f.status0 == 0 && !f.vbusOk && f.bcLvl == 0 && !f.comp);

  // The BC_LVL words, so the log's parenthetical cannot drift from the two-bit value.
  CHECK(strcmp(om::diag::bcLevelName(0), "default 100mA") == 0);
  CHECK(strcmp(om::diag::bcLevelName(1), "500mA") == 0);
  CHECK(strcmp(om::diag::bcLevelName(2), "1.5A") == 0);
  CHECK(strcmp(om::diag::bcLevelName(3), "3A") == 0);
  CHECK(strcmp(om::diag::bcLevelName(7), "3A") == 0);   // the mask keeps it two bits
}

// ------------------------------------------------------------------------------- 4. payload offset
static void testPayloadOffset() {
  // Flood route (0x01), no transport codes, path byte 0 (direct), payload at 2.
  const uint8_t flood[6] = {0x05, 0x00, 0xAA, 0xBB, 0xCC, 0xDD};
  uint8_t count = 9, size = 9;
  CHECK(om::radio::payloadOffset(flood, sizeof(flood), &count, &size) == 2);
  CHECK(count == 0 && size == 1);

  // Transport-coded route (0x00) skips four transport bytes: 1 + 4 + 1 = 6.
  const uint8_t coded[8] = {0x00, 1, 2, 3, 4, 0x00, 0xAA, 0xBB};
  CHECK(om::radio::payloadOffset(coded, sizeof(coded), nullptr, nullptr) == 6);

  // Two one-byte path hashes: 1 + 1 + 2 = 4, and the path count is decoded.
  const uint8_t paths[6] = {0x02, 0x02, 0x11, 0x22, 0x33, 0x44};
  count = 9;
  CHECK(om::radio::payloadOffset(paths, sizeof(paths), &count, &size) == 4);
  CHECK(count == 2 && size == 1);

  // Two two-byte path hashes: 1 + 1 + 4 = 6, and 0b01 in the path byte's top bits is two bytes.
  const uint8_t wide[8] = {0x02, 0x42, 0x11, 0x11, 0x22, 0x22, 0x33, 0x44};
  CHECK(om::radio::payloadOffset(wide, sizeof(wide), &count, &size) == 6);
  CHECK(count == 2 && size == 2);

  // 0b10 in the top bits means three-byte hashes: 1 + 1 + 6 = 8, i.e. no payload in this frame.
  const uint8_t widest[8] = {0x02, 0x82, 0x11, 0x11, 0x11, 0x22, 0x22, 0x22};
  CHECK(om::radio::payloadOffset(widest, sizeof(widest), &count, &size) == -1);
  CHECK(count == 2 && size == 3);

  // A frame that ends before its payload has no first payload byte.
  const uint8_t headerOnly[1] = {0x01};
  CHECK(om::radio::payloadOffset(headerOnly, 1, nullptr, nullptr) == -1);
  const uint8_t noPayload[2] = {0x01, 0x00};
  CHECK(om::radio::payloadOffset(noPayload, 2, nullptr, nullptr) == -1);
  CHECK(om::radio::payloadOffset(nullptr, 0, nullptr, nullptr) == -1);
}

int main() {
  testCommands();
  testSummary();
  testFusb();
  testPayloadOffset();
  if (g_fail != 0) {
    printf("est376: %d check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("est376: all checks passed\n");
  return 0;
}
