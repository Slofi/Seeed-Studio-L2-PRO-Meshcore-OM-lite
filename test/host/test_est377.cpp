// SPDX-License-Identifier: MIT
// EST-377 host test: the battery log's line/header/gate/switch and the boost choice that must survive
// a later resetAGC().
//
//   1. COMMANDS. `log power on|off|tail` parse; a missing word, an extra word, an unknown power word
//      and an unknown sub-command are refused. The parser accepts quoted values exactly like the
//      other commands.
//   2. CSV. The exact line for a known sample and local minute, the local file name, and the header
//      that precedes the first line of a new file (and only then).
//   3. GATE. The first call writes, then exactly 60 s of monotonic time per line, including the
//      millis() wrap.
//   4. SWITCH. A missing key is ON and is not written; a stored OFF loads; a set writes the key; a
//      failed write still changes the RAM value but reports the failure.
//   5. BOOST. The model of `radio boost on` -> `radio tcxo 3.0` (which runs resetAGC and drops the
//      gain) -> read back/apply keeps boost on. "none set" must never invent a value.
// Run by test/host/run.sh (system g++), no Arduino and no device.
#include <stdio.h>
#include <string.h>

#include "diag_logic.h"
#include "power_log_logic.h"
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

static void expectLog(const char* text, const char* value) {
  const om::usb::Command c = parse(text);
  CHECK(c.error == nullptr);
  CHECK(c.kind == om::usb::Kind::Log);
  CHECK(c.target != nullptr && strcmp(c.target, "power") == 0);
  CHECK(c.text != nullptr && strcmp(c.text, value) == 0);
}

static void expectError(const char* text, const char* error) {
  const om::usb::Command c = parse(text);
  CHECK(c.error != nullptr);
  if (c.error != nullptr) CHECK(strcmp(c.error, error) == 0);
}

static void testCommands() {
  expectLog("log power on", "on");
  expectLog("log power off", "off");
  expectLog("log power tail", "tail");
  expectLog("log power \"on\"", "on");
  expectError("log power", "args");
  expectError("log power on extra", "args");
  expectError("log power maybe", "value");
  expectError("log power 1", "value");
  expectError("log", "args");
  expectError("log other on", "args");
  expectError("log POWER on", "args");
  // The neighbouring commands still parse (the new verb did not take their names).
  const om::usb::Command h = parse("history \"Alice\" 5");
  CHECK(h.error == nullptr && h.kind == om::usb::Kind::History);
  const om::usb::Command r = parse("radio status");
  CHECK(r.error == nullptr && r.kind == om::usb::Kind::Radio);
}

// ---------------------------------------------------------------------------------------- 2. CSV
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

static int countCommas(const char* s) {
  int n = 0;
  for (; *s != 0; ++s) {
    if (*s == ',') ++n;
  }
  return n;
}

static void testCsv() {
  const om::plog::Sample s = sampleForTest();
  const om::plog::LocalStamp t = stampForTest();
  const char* kExpected =
      "2026-11-11 09:41,4012,1,160,1,1,9,0,0,12,-97,3600\n";

  char line[256];
  CHECK(om::plog::formatLine(s, t, line, sizeof(line)) == (int)strlen(kExpected));
  CHECK(strcmp(line, kExpected) == 0);
  // 12 columns: 11 commas in the header and in every line.
  CHECK(countCommas(om::plog::kPowerLogHeader) == 11);
  CHECK(countCommas(line) == 11);

  // A screen-off minute writes 0 brightness even if the caller's brightness field is stale.
  om::plog::Sample off = s;
  off.screenOn = false;
  off.brightness = 0;
  off.fix = false;
  off.sats = 0;
  off.wifiOn = true;
  off.bleOn = true;
  off.packets = 0;
  off.noiseDbm = 0;
  off.uptimeSec = 61;
  CHECK(om::plog::formatLine(off, t, line, sizeof(line)) > 0);
  CHECK(strcmp(line, "2026-11-11 09:41,4012,0,0,1,0,0,1,1,0,0,61\n") == 0);

  char name[48];
  CHECK(om::plog::formatFileName(t, name, sizeof(name)) == (int)strlen("/log/power-20261111.csv"));
  CHECK(strcmp(name, "/log/power-20261111.csv") == 0);

  // A buffer that is one byte too small for the line is refused, never truncated.
  char tight[16];
  CHECK(om::plog::formatLine(s, t, tight, sizeof(tight)) == -1);
}

static void testHeader() {
  const om::plog::Sample s = sampleForTest();
  const om::plog::LocalStamp t = stampForTest();
  char buf[512];
  const char* line = "2026-11-11 09:41,4012,1,160,1,1,9,0,0,12,-97,3600\n";

  const int fresh = om::plog::formatAppend(s, t, true, buf, sizeof(buf));
  CHECK(fresh > 0);
  CHECK(strncmp(buf, om::plog::kPowerLogHeader, strlen(om::plog::kPowerLogHeader)) == 0);
  CHECK(buf[strlen(om::plog::kPowerLogHeader)] == '\n');
  CHECK(strcmp(buf + strlen(om::plog::kPowerLogHeader) + 1, line) == 0);
  CHECK(fresh == (int)(strlen(om::plog::kPowerLogHeader) + 1 + strlen(line)));

  // An existing file gets the data line only: no second header in the same day's file.
  const int append = om::plog::formatAppend(s, t, false, buf, sizeof(buf));
  CHECK(append == (int)strlen(line));
  CHECK(strcmp(buf, line) == 0);

  // A buffer that cannot hold header + line is refused.
  char tight[64];
  CHECK(om::plog::formatAppend(s, t, true, tight, sizeof(tight)) == -1);
}

// --------------------------------------------------------------------------------------- 3. gate
static void testGate() {
  om::plog::MinuteGate gate;
  CHECK(!gate.started());
  CHECK(gate.due(1000));            // a fresh log writes at once
  CHECK(gate.started());
  CHECK(!gate.due(60999));          // 59.999 s later: not yet
  CHECK(gate.due(61000));           // exactly one minute
  CHECK(!gate.due(120999));
  CHECK(gate.due(121000));
  // A reset re-arms the first line (the USB switch calls it).
  gate.reset();
  CHECK(!gate.started());
  CHECK(gate.due(500));

  // millis() wrap: the difference is what counts, not the absolute value.
  om::plog::MinuteGate wrap;
  CHECK(wrap.due(0xFFFFFF00u));
  CHECK(!wrap.due(0x00000100u));    // 512 ms later
  CHECK(wrap.due(0x0000EBC0u));     // exactly 60000 ms after 0xFFFFFF00
}

// ------------------------------------------------------------------------------------- 4. switch
struct FakePrefs {
  bool has = false;
  bool value = false;
  bool failWrite = false;
  int reads = 0;
  int writes = 0;
  char lastKey[24] = "";
};

static bool fakeRead(void* ctx, const char* key, bool fallback, bool& out) {
  (void)fallback;   // the fake stores only the value; the missing-key default is the caller's
  FakePrefs* f = (FakePrefs*)ctx;
  ++f->reads;
  snprintf(f->lastKey, sizeof(f->lastKey), "%s", key);
  if (!f->has) return false;
  out = f->value;
  return true;
}

static bool fakeWrite(void* ctx, const char* key, bool value) {
  FakePrefs* f = (FakePrefs*)ctx;
  ++f->writes;
  snprintf(f->lastKey, sizeof(f->lastKey), "%s", key);
  if (f->failWrite) return false;
  f->has = true;
  f->value = value;
  return true;
}

static void testSwitch() {
  // A first boot: no key, the default ON, and no write for taking the default.
  FakePrefs f;
  om::plog::PowerLogPref p;
  p.begin(fakeRead, fakeWrite, &f);
  p.load();
  CHECK(p.on());
  CHECK(f.writes == 0);
  CHECK(f.reads == 1 && strcmp(f.lastKey, "plogen") == 0);
  CHECK(om::plog::kPowerLogDefaultOn);

  // A stored OFF is honoured.
  FakePrefs g;
  g.has = true;
  g.value = false;
  om::plog::PowerLogPref q;
  q.begin(fakeRead, fakeWrite, &g);
  q.load();
  CHECK(!q.on());

  // A switch change writes the key in the same call.
  CHECK(p.setOn(false));
  CHECK(!p.on());
  CHECK(g.writes == 0);   // the other fake was untouched
  CHECK(f.writes == 1 && f.has && !f.value);
  CHECK(strcmp(f.lastKey, "plogen") == 0);

  // A failed write still flips RAM (the caller reports the failure) but returns false.
  FakePrefs h;
  h.failWrite = true;
  om::plog::PowerLogPref r;
  r.begin(fakeRead, fakeWrite, &h);
  r.load();   // default ON
  CHECK(!r.setOn(false));
  CHECK(!r.on());
  CHECK(h.writes == 1);

  // The packet delta is per minute and wraps with the counter.
  CHECK(om::plog::packetDelta(42, 0, false) == 0);
  CHECK(om::plog::packetDelta(42, 40, true) == 2);
  CHECK(om::plog::packetDelta(1, 0xFFFFFFFFu, true) == 2);
}

// -------------------------------------------------------------------------------------- 5. boost
static void testBoostRecall() {
  // "none set" since boot: a reset must not invent a boost value, whatever the chip reads back.
  om::diag::BoostChoice choice;
  CHECK(!choice.known());
  CHECK(!choice.needsReapply(false));
  CHECK(!choice.needsReapply(true));

  // The ticket's sequence: boost on -> the chip takes it -> a TCXO change runs resetAGC, which in
  // this build can drop the gain -> the read-back says off -> set it again -> it is on.
  bool chipBoosted = false;
  choice.note(true);
  CHECK(choice.known() && choice.on());
  CHECK(choice.needsReapply(chipBoosted));
  if (choice.needsReapply(chipBoosted)) chipBoosted = choice.on();   // `radio boost on`
  CHECK(chipBoosted);
  CHECK(!choice.needsReapply(chipBoosted));   // the reset kept it: nothing to do

  chipBoosted = false;                        // resetAGC lost it during `radio tcxo 3.0`
  CHECK(choice.needsReapply(chipBoosted));
  if (choice.needsReapply(chipBoosted)) chipBoosted = choice.on();   // the EST-377 recall
  CHECK(chipBoosted);
  CHECK(!choice.needsReapply(chipBoosted));

  // `radio boost off` after a reset that left the chip on: put it back off.
  choice.note(false);
  CHECK(choice.known() && !choice.on());
  CHECK(choice.needsReapply(true));
  if (choice.needsReapply(true)) chipBoosted = choice.on();
  CHECK(!chipBoosted);
  CHECK(!choice.needsReapply(false));
}

int main() {
  testCommands();
  testCsv();
  testHeader();
  testGate();
  testSwitch();
  testBoostRecall();
  if (g_fail != 0) {
    printf("%d check(s) failed\n", g_fail);
    return 1;
  }
  printf("est377: all checks passed\n");
  return 0;
}
