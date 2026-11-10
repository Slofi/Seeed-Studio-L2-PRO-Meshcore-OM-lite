// EST-358 host test: the RPTR tab's logic. Everything proven here runs on the laptop with the system
// g++: the EXACT CLI line every admin control sends (the mock's own strings in
// tests/test_ui_mock.py), the discovery result list, the login state machine and its timeouts, the
// password filter, the status decode, the neighbour and region reply parsers, the two-step confirm,
// the admin row table and the USB `rptr` command.
#include <stdio.h>
#include <string.h>

#include "rptr_model.h"
#include "usb_cmd_logic.h"
#include "ui/rptr_layout.h"

using namespace om::rptr;

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static void checkText(const char* got, const char* want, int line) {
  if (strcmp(got, want) != 0) {
    printf("FAIL %s:%d  got \"%s\", want \"%s\"\n", __FILE__, line, got, want);
    ++g_fail;
  }
}
#define CHECK_TEXT(got, want) checkText(got, want, __LINE__)

// --- the CLI lines the mock's tests assert -------------------------------------------------------
static void testCliLines() {
  RadioValues v;   // the mock's defaults: 869.525 MHz, 62.5 kHz, SF8, CR 4/5, 14 dBm
  char line[64];
  cliRadio(line, sizeof(line), v);
  CHECK_TEXT(line, "set radio 869.525,62.5,8,5");
  cliTx(line, sizeof(line), v);
  CHECK_TEXT(line, "set tx 14");
  cliRadioApply(line, sizeof(line), v);
  CHECK_TEXT(line, "set radio 869.525,62.5,8,5 \xC2\xB7 set tx 14");

  // The mock's steppers, exactly as tests/test_ui_mock.py asserts them.
  radioStep(&v, RadioKey::Bw, +1);
  cliRadio(line, sizeof(line), v);
  CHECK_TEXT(line, "set radio 869.525,125,8,5");
  radioStep(&v, RadioKey::Bw, -1);
  radioStep(&v, RadioKey::Cr, +1);
  cliRadio(line, sizeof(line), v);
  CHECK_TEXT(line, "set radio 869.525,62.5,8,6");
  radioStep(&v, RadioKey::Cr, -1);
  radioStep(&v, RadioKey::Sf, +1);
  cliRadio(line, sizeof(line), v);
  CHECK_TEXT(line, "set radio 869.525,62.5,9,5");
  radioStep(&v, RadioKey::Sf, -1);
  radioStep(&v, RadioKey::Tx, +1);
  cliTx(line, sizeof(line), v);
  CHECK_TEXT(line, "set tx 15");
  radioStep(&v, RadioKey::Tx, -1);
  // Clamps: 1 and 22 dBm, SF 5..12, CR 5..8, freq 150..2500 MHz in 0.025 steps.
  for (int i = 0; i < 40; ++i) radioStep(&v, RadioKey::Tx, -1);
  CHECK(v.tx == 1);
  for (int i = 0; i < 40; ++i) radioStep(&v, RadioKey::Tx, +1);
  CHECK(v.tx == 22);
  for (int i = 0; i < 20; ++i) radioStep(&v, RadioKey::Sf, -1);
  CHECK(v.sf == 5);
  for (int i = 0; i < 20; ++i) radioStep(&v, RadioKey::Sf, +1);
  CHECK(v.sf == 12);
  for (int i = 0; i < 20; ++i) radioStep(&v, RadioKey::Cr, -1);
  CHECK(v.cr == 5);
  // An unknown bandwidth snaps to the ladder's 62.5 kHz (the mock's fallback index).
  RadioValues b;
  b.bw_hz = 12345;
  radioStep(&b, RadioKey::Bw, -1);
  CHECK(b.bw_hz == 41700);
  char bw[16];
  bwText(bw, sizeof(bw), 31250);
  CHECK_TEXT(bw, "31.25");
  bwText(bw, sizeof(bw), 41700);
  CHECK_TEXT(bw, "41.7");
  bwText(bw, sizeof(bw), 125000);
  CHECK_TEXT(bw, "125");
  freqText(bw, sizeof(bw), 869525);
  CHECK_TEXT(bw, "869.525");

  // Regions, exactly the mock's strings.
  cliRegionFlood(line, sizeof(line), "lj", true);
  CHECK_TEXT(line, "region allowf lj");
  cliRegionFlood(line, sizeof(line), "*", false);
  CHECK_TEXT(line, "region denyf *");
  cliRegionHome(line, sizeof(line), "lj");
  CHECK_TEXT(line, "region home lj");
  cliRegionDefault(line, sizeof(line), "si");
  CHECK_TEXT(line, "region default si");
  CHECK_TEXT(kCliRegionSave, "region save");
  CHECK(kSiPresetCount == 7);
  CHECK_TEXT(kSiPreset[0], "region put si");
  CHECK_TEXT(kSiPreset[1], "region allowf si");
  CHECK_TEXT(kSiPreset[2], "region put lj si");
  CHECK_TEXT(kSiPreset[3], "region allowf lj");
  CHECK_TEXT(kSiPreset[4], "region home si");
  CHECK_TEXT(kSiPreset[5], "region default si");
  CHECK_TEXT(kSiPreset[6], "region save");

  // Identity.
  cliName(line, sizeof(line), "rptr");
  CHECK_TEXT(line, "set name rptr");
  cliOwner(line, sizeof(line), "own");
  CHECK_TEXT(line, "set owner.info own");
  cliLat(line, sizeof(line), 46250000);
  CHECK_TEXT(line, "set lat 46.25000");
  cliLon(line, sizeof(line), 14400000);
  CHECK_TEXT(line, "set lon 14.40000");
  cliLat(line, sizeof(line), -12345678);
  CHECK_TEXT(line, "set lat -12.34568");
  // Empty values are refused by the builder, so no trailing-space command can go out.
  line[0] = 'x';
  CHECK(cliName(line, sizeof(line), "") == 0);
  CHECK(line[0] == 0);
  CHECK(cliOwner(line, sizeof(line), "") == 0);

  // Mesh page.
  cliPathHash(line, sizeof(line), 2);
  CHECK_TEXT(line, "set path.hash.mode 2");
  cliLoopDetect(line, sizeof(line), LoopDetect::Strict);
  CHECK_TEXT(line, "set loop.detect strict");
  CHECK_TEXT(loopDetectText(LoopDetect::Off), "off");
  CHECK_TEXT(loopDetectText(LoopDetect::Minimal), "minimal");
  CHECK_TEXT(loopDetectText(LoopDetect::Moderate), "moderate");
  CHECK_TEXT(pathHashText(0), "1B");
  CHECK_TEXT(pathHashText(1), "2B");
  CHECK_TEXT(pathHashText(2), "3B");
  cliRepeat(line, sizeof(line), false);
  CHECK_TEXT(line, "set repeat off");
  cliPowerSaving(line, sizeof(line), true);
  CHECK_TEXT(line, "set powersaving on");
  cliGpsAdvert(line, sizeof(line), true);
  CHECK_TEXT(line, "gps advert prefs");
  cliGpsAdvert(line, sizeof(line), false);
  CHECK_TEXT(line, "gps advert none");

  // Adverts and flood: the mock's ladders and clamps.
  AdminValues a;
  a.reset();
  CHECK(a.advertLocal == 120);
  CHECK(a.advertFlood == 13);
  CHECK(a.floodMax == 8);
  advertLocalStep(&a, -1);
  CHECK(a.advertLocal == 90);
  advertLocalStep(&a, -1);
  CHECK(a.advertLocal == 60);
  advertLocalStep(&a, -1);
  CHECK(a.advertLocal == 0);
  for (int i = 0; i < 10; ++i) advertLocalStep(&a, +1);
  CHECK(a.advertLocal == 240);
  cliAdvertLocal(line, sizeof(line), a.advertLocal);
  CHECK_TEXT(line, "set advert.interval 240");
  a.advertFlood = 3;
  advertFloodStep(&a, -1);
  CHECK(a.advertFlood == 0);          // never 1 or 2 hours
  advertFloodStep(&a, +1);
  CHECK(a.advertFlood == 3);
  for (int i = 0; i < 200; ++i) advertFloodStep(&a, +1);
  CHECK(a.advertFlood == 168);
  cliAdvertFlood(line, sizeof(line), a.advertFlood);
  CHECK_TEXT(line, "set flood.advert.interval 168");
  for (int i = 0; i < 20; ++i) floodMaxStep(&a, -1);
  CHECK(a.floodMax == 0);
  for (int i = 0; i < 20; ++i) floodMaxStep(&a, +1);
  CHECK(a.floodMax == 64);
  a.floodMax = 8;
  cliFloodMax(line, sizeof(line), a.floodMax);
  CHECK_TEXT(line, "set flood.max 8");

  // Silent mode and Resume name all four commands, as the mock's warn box does.
  CHECK_TEXT(kSilent[0], "set repeat off");
  CHECK_TEXT(kSilent[1], "set advert.interval 0");
  CHECK_TEXT(kSilent[2], "set flood.advert.interval 0");
  CHECK_TEXT(kSilent[3], "gps advert none");
  CHECK_TEXT(kResume[0], "set repeat on");
  CHECK_TEXT(kResume[1], "set advert.interval 120");
  CHECK_TEXT(kResume[2], "set flood.advert.interval 13");
  CHECK_TEXT(kResume[3], "gps advert prefs");
  CHECK_TEXT(kCliAdvertLocal, "advert.zerohop");
  CHECK_TEXT(kCliAdvertFlood, "advert");
  CHECK_TEXT(kCliClockSync, "clock sync");
  CHECK_TEXT(kCliNeighboursDiscover, "discover.neighbors");
  CHECK_TEXT(kCliNeighboursRead, "neighbors");
  CHECK_TEXT(kCliReboot, "reboot");
}

// --- the password filter ------------------------------------------------------------------------
static void testPassword() {
  CHECK(passwordChar('a'));
  CHECK(passwordChar(' '));     // a printable ASCII space is a legal password byte
  CHECK(passwordChar('~'));
  CHECK(!passwordChar(char(0x1F)));
  CHECK(!passwordChar(char(0x7F)));
  CHECK(!passwordChar(char(0x80)));

  char out[20];
  const int n = passwordFilter("s3cret pass!", out, sizeof(out));
  CHECK(n == 12);
  CHECK_TEXT(out, "s3cret pass!");
  // Non-printable bytes never reach the radio.
  CHECK(passwordFilter("a\x01\x7F" "b", out, sizeof(out)) == 2);
  CHECK_TEXT(out, "ab");
  // 15 characters is MeshCore's sendLogin() limit; the 16th is dropped, not truncated mid-way.
  passwordFilter("0123456789abcdefgh", out, sizeof(out));
  CHECK(strlen(out) == 15);
  CHECK_TEXT(out, "0123456789abcde");
  // A buffer smaller than the password still terminates exactly at its end.
  char small[6];
  passwordFilter("abcdefgh", small, sizeof(small));
  CHECK_TEXT(small, "abcde");
}

// --- discovery ----------------------------------------------------------------------------------
static void testFoundList() {
  FoundList f;
  f.stateText(nullptr, 0);   // no crash on a null buffer
  f.begin(0xAABBCCDD, 1000);
  char text[40];
  f.stateText(text, sizeof(text));
  CHECK_TEXT(text, "listening");
  CHECK(f.listening);

  const uint8_t k1[6] = {1, 2, 3, 4, 5, 6};
  const uint8_t k2[6] = {9, 9, 9, 9, 9, 9};
  CHECK(f.addAnswer(k1, "SLO-RPTR-1", true, true, 30, true, -34, 0, 1100) == 0);
  f.stateText(text, sizeof(text));
  CHECK_TEXT(text, "1 found, listening");
  CHECK(f.addAnswer(k2, "", false, true, 10, false, 0, 2, 1200) == 1);
  // A repeater that answers twice stays ONE row, with the newer SNR/time.
  CHECK(f.addAnswer(k1, "SLO-RPTR-1", true, true, 31, true, -35, 0, 1300) == 0);
  CHECK(f.count == 2);
  CHECK(f.rows[0].hasSnrIn && f.rows[0].snrIn4 == 31);
  CHECK(f.rows[0].snrOut4 == -35);
  CHECK(f.rows[0].inContacts);
  CHECK(f.rows[0].heardMs == 1300);
  CHECK(!f.rows[1].inContacts);
  CHECK(f.rows[1].name[0] == 0);
  CHECK(f.rows[1].hops == 2);

  // The window closes after kScanWindowMs, and one serial bump follows.
  const uint32_t before = f.serial;
  f.tick(1000 + kScanWindowMs - 1);
  CHECK(f.listening);
  f.tick(1000 + kScanWindowMs);
  CHECK(!f.listening);
  CHECK(f.serial == before + 1);
  f.stateText(text, sizeof(text));
  CHECK_TEXT(text, "2 found");
  const uint32_t after = f.serial;
  f.tick(60000);
  CHECK(f.serial == after);   // no repeated bumps once it is closed

  // A new scan forgets the rows; a full list refuses the 17th answer rather than overrunning.
  f.begin(7, 2000);
  CHECK(f.count == 0 && f.listening && f.tag == 7);
  uint8_t key[6] = {0, 0, 0, 0, 0, 0};
  for (int i = 0; i < kMaxFound; ++i) {
    key[0] = (uint8_t)i;
    CHECK(f.addAnswer(key, "R", true, true, 0, false, 0, 0, 2000) == i);
  }
  key[0] = 200;
  CHECK(f.addAnswer(key, "R", true, true, 0, false, 0, 0, 2000) == -1);
  CHECK(f.count == kMaxFound);
  f.begin(1, 0);
  f.tick(kScanWindowMs);
  f.stateText(text, sizeof(text));
  CHECK_TEXT(text, "none answered");
}

// --- login --------------------------------------------------------------------------------------
static void testLogin() {
  // A current MeshCore answer: OK, keep-alive/16 = 0, admin = 1, permissions 1, fw level 4.
  const uint8_t ok[13] = {0, 0, 1, 1, 0, 0, 0, 0, 4, 0, 0, 0, 0};
  LoginAnswer a;
  CHECK(loginParse(ok, sizeof(ok), &a));
  CHECK(a.ok && a.admin && !a.legacy);
  CHECK(a.permissions == 1);
  CHECK(a.fwLevel == 4);

  // A guest: is_admin 0.
  const uint8_t guest[13] = {0, 0, 0, 2, 0, 0, 0, 0, 4, 0, 0, 0, 0};
  CHECK(loginParse(guest, sizeof(guest), &a));
  CHECK(a.ok && !a.admin);

  // The legacy two-byte "OK".
  const uint8_t legacy[2] = {'O', 'K'};
  CHECK(loginParse(legacy, 2, &a));
  CHECK(a.ok && a.legacy && a.admin);

  // Anything else is a reply, but not a login: refused.
  const uint8_t other[5] = {1, 2, 3, 4, 5};
  CHECK(loginParse(other, sizeof(other), &a));
  CHECK(!a.ok);

  // The state machine: sending, admin with the round trip, then idle again.
  LoginMachine m;
  CHECK(m.state == LoginState::Idle);
  m.begin(1000, 4000);
  CHECK(m.busy());
  CHECK(m.deadlineMs == 1000 + 4000 + kLoginExtraMs);
  CHECK(!m.tick(2000));            // still inside the deadline
  CHECK(m.state == LoginState::Sending);
  CHECK(!m.tick(1000 + 4000 + kLoginExtraMs - 1));
  CHECK(loginParse(ok, sizeof(ok), &a));   // `a` was overwritten by the parse cases above
  m.succeed(a, 2600);
  CHECK(m.state == LoginState::Admin);
  CHECK(m.haveRtt && m.rttMs == 1600);
  CHECK(!m.tick(100000));          // a finished login does not time out

  // No answer: the deadline turns it into NoAnswer exactly once.
  LoginMachine t;
  t.begin(0, 3000);
  CHECK(!t.tick(3000 + kLoginExtraMs - 1));
  CHECK(t.tick(3000 + kLoginExtraMs));
  CHECK(t.state == LoginState::NoAnswer);
  CHECK(!t.tick(999999));
  CHECK_TEXT(loginStateText(LoginState::NoAnswer), "no answer");
  CHECK_TEXT(loginStateText(LoginState::Admin), "admin");
  CHECK_TEXT(loginStateText(LoginState::Guest), "guest");
  CHECK_TEXT(loginStateText(LoginState::Refused), "refused");

  // A refused answer with an ok==false parse.
  LoginMachine r;
  r.begin(0, 1000);
  r.succeed(a, 500);   // (a is reset by loginParse below)
  LoginAnswer bad;
  loginParse(other, sizeof(other), &bad);
  r.begin(0, 1000);
  r.succeed(bad, 700);
  CHECK(r.state == LoginState::Refused);
  CHECK(r.haveRtt && r.rttMs == 700);

  // millis() wrap: a deadline that straddles the wrap still fires once.
  LoginMachine w;
  w.begin(0xFFFFFF00u, 1000);   // deadline wraps to 0x0CAC
  CHECK(!w.tick(0xFFFFFFF0u));
  CHECK(!w.tick(0x00000100u));   // still before the wrapped deadline
  CHECK(w.tick(0x00000D00u));    // past it, after the wrap
}

// --- status -------------------------------------------------------------------------------------
static void testStatus() {
  uint8_t buf[4 + kRepeaterStatsBytes] = {};
  // [tag 4][stats]
  const uint16_t batt = 3950, queue = 2;
  const int16_t noise = -95, rssi = -70, snr4 = 25;
  const uint32_t rx = 1200, tx = 340, air = 4000, up = 90061, rxA = 3900, err = 3;
  memcpy(&buf[4 + 0], &batt, 2);
  memcpy(&buf[4 + 2], &queue, 2);
  memcpy(&buf[4 + 4], &noise, 2);
  memcpy(&buf[4 + 6], &rssi, 2);
  memcpy(&buf[4 + 8], &rx, 4);
  memcpy(&buf[4 + 12], &tx, 4);
  memcpy(&buf[4 + 16], &air, 4);
  memcpy(&buf[4 + 20], &up, 4);
  memcpy(&buf[4 + 42], &snr4, 2);
  memcpy(&buf[4 + 48], &rxA, 4);
  memcpy(&buf[4 + 52], &err, 4);
  Status s;
  CHECK(statusParse(buf, sizeof(buf), &s));
  CHECK(s.valid);
  CHECK(s.battMv == 3950);
  CHECK(s.noiseFloor == -95);
  CHECK(s.lastRssi == -70);
  CHECK(s.rxPackets == 1200);
  CHECK(s.txPackets == 340);
  CHECK(s.airtimeSec == 4000);
  CHECK(s.uptimeSec == 90061);
  CHECK(s.lastSnr4 == 25);
  CHECK(s.rxAirtimeSec == 3900);
  CHECK(s.rxCrcErrors == 3);
  // A short buffer is refused whole: no half-filled card.
  Status s2;
  CHECK(!statusParse(buf, sizeof(buf) - 1, &s2));
  CHECK(!s2.valid);

  char t[24];
  uptimeText(t, sizeof(t), 90061);
  CHECK_TEXT(t, "1d 01:01");
  uptimeText(t, sizeof(t), 3720);
  CHECK_TEXT(t, "1h 02m");
  uptimeText(t, sizeof(t), 750);
  CHECK_TEXT(t, "12m 30s");
  CHECK(batteryPercent(4200) == 100);
  CHECK(batteryPercent(3000) == 0);
  CHECK(batteryPercent(2999) == -1);
  snrText(t, sizeof(t), -34);
  CHECK_TEXT(t, "-8.50 dB");
  snrText(t, sizeof(t), 25);
  CHECK_TEXT(t, "6.25 dB");
  snrBothText(t, sizeof(t), true, 25, true, -34);
  CHECK_TEXT(t, "SNR 6.25/-8.50 dB");
  snrBothText(t, sizeof(t), false, 0, true, 30);
  CHECK_TEXT(t, "SNR 7.50 dB");
  snrBothText(t, sizeof(t), false, 0, false, 0);
  CHECK_TEXT(t, "");
  clockText(t, sizeof(t), 1700000000u);   // 2023-11-14 22:13:20 UTC
  CHECK_TEXT(t, "22:13 UTC");
  clockText(t, sizeof(t), 0);
  CHECK_TEXT(t, "not set");
}

// --- neighbours and regions ---------------------------------------------------------------------
static void testParsers() {
  NeighbourList n;
  CHECK(neighboursParse("a1b2c3d4:12:0\n01020304:3600:-34", &n) == 2);
  CHECK(n.valid && n.count == 2);
  CHECK(n.rows[0].prefix[0] == 0xA1 && n.rows[0].prefix[3] == 0xD4);
  CHECK(n.rows[0].secsAgo == 12 && n.rows[0].snr4 == 0);
  CHECK(n.rows[1].secsAgo == 3600 && n.rows[1].snr4 == -34);
  CHECK(neighboursParse("-none-", &n) == 0);
  CHECK(n.valid && n.count == 0);
  CHECK(neighboursParse("", &n) == 0);
  CHECK(n.valid);

  RegionList r;
  // RegionMap::exportTo(): "<name>[^][ F]" per line, indented by depth, wildcard first.
  CHECK(regionsParse("*\nsi^ F\n lj F\n norway", &r) == 4);
  CHECK(r.valid && r.count == 4);
  CHECK_TEXT(r.rows[0].name, "*");
  CHECK(r.rows[0].wildcard);
  CHECK(!r.rows[0].allow);
  CHECK_TEXT(r.rows[1].name, "si");
  CHECK(r.rows[1].allow && r.rows[1].home);
  CHECK_TEXT(r.rows[2].name, "lj");
  CHECK(r.rows[2].allow && !r.rows[2].home);
  CHECK_TEXT(r.rows[3].name, "norway");
  CHECK(!r.rows[3].allow);
  CHECK(regionsParse("", &r) == 0);
  CHECK(r.valid);
}

// --- the two-step confirm -----------------------------------------------------------------------
static void testConfirm() {
  Confirm c;
  CHECK(!c.armed);
  CHECK(!c.take("reboot", 1000));     // nothing armed: one tap never fires
  c.arm("reboot", 1000);
  CHECK(c.armed);
  CHECK(!c.take("silent", 1100));     // another action's confirm is not ours
  CHECK(!c.tick(1000 + kConfirmMs));  // still inside the window
  CHECK(c.take("reboot", 1200));      // the second tap fires
  CHECK(!c.armed);
  CHECK(!c.take("reboot", 1300));     // and it is spent

  c.arm("silent", 0);
  CHECK(!c.tick(kConfirmMs));
  CHECK(c.tick(kConfirmMs + 1));      // the window closed: nothing armed
  CHECK(!c.armed);
  CHECK(!c.take("silent", kConfirmMs + 2));
}

// --- the admin row table ------------------------------------------------------------------------
static void testAdminRows() {
  AdminValues v;
  v.reset();
  Status st;
  AdminRow rows[kAdminRowCap];
  RegionList regions;
  regionsParse("*\nsi^ F\nlj F", &regions);
  NeighbourList neigh;
  neighboursParse("a1b2c3d4:12:-34", &neigh);
  neigh.rows[0].named = true;
  snprintf(neigh.rows[0].name, sizeof(neigh.rows[0].name), "Bohinj-RPTR");

  // Overview before a status read: the refresh row and one honest row, no invented number.
  int n = adminRows(0, v, st, 0, "SLO-RPTR-1", regions, neigh, rows, kAdminRowCap);
  CHECK(n == 2);
  CHECK(rows[0].id == RowId::StatusRefresh && rows[0].kind == RowKind::Action);
  CHECK_TEXT(rows[0].label, "Read all");
  CHECK_TEXT(rows[1].value, "not read yet, tap Read");

  // Overview after it: uptime, battery, airtime, packets, noise, SNR, firmware, clock.
  uint8_t buf[4 + kRepeaterStatsBytes] = {};
  const uint16_t batt = 3950;
  const int16_t noise = -95, snr4 = 25;
  const uint32_t rx = 1200, tx = 340, air = 4000, up = 90061;
  memcpy(&buf[4], &batt, 2);
  memcpy(&buf[4 + 4], &noise, 2);
  memcpy(&buf[4 + 8], &rx, 4);
  memcpy(&buf[4 + 12], &tx, 4);
  memcpy(&buf[4 + 16], &air, 4);
  memcpy(&buf[4 + 20], &up, 4);
  memcpy(&buf[4 + 42], &snr4, 2);
  CHECK(statusParse(buf, sizeof(buf), &st));
  st.clock = 1700000000u;
  st.clockValid = true;
  n = adminRows(0, v, st, 4, "SLO-RPTR-1", regions, neigh, rows, kAdminRowCap);
  CHECK(n == 9);
  CHECK_TEXT(rows[1].label, "uptime");
  CHECK_TEXT(rows[1].value, "1d 01:01");
  CHECK_TEXT(rows[2].label, "battery");
  CHECK_TEXT(rows[2].value, "3.95 V, 79%");
  CHECK_TEXT(rows[3].label, "airtime");
  CHECK_TEXT(rows[4].label, "packets");
  CHECK_TEXT(rows[4].value, "rx 1200, tx 340");
  CHECK_TEXT(rows[5].value, "-95 dBm");
  CHECK_TEXT(rows[6].value, "6.25 dB");
  CHECK_TEXT(rows[7].value, "level 4");
  CHECK_TEXT(rows[8].value, "22:13 UTC");
  for (int i = 1; i < n; ++i) CHECK(rows[i].kind == RowKind::Fact);

  // Radio: five steppers and Apply, in the mock's order.
  n = adminRows(1, v, st, 4, "SLO-RPTR-1", regions, neigh, rows, kAdminRowCap);
  CHECK(n == 6);
  CHECK(rows[0].id == RowId::RadioFreq && rows[0].kind == RowKind::Stepper && rows[0].keys == 2);
  CHECK_TEXT(rows[0].value, "869.525 MHz");
  CHECK_TEXT(rows[1].value, "62.5 kHz");
  CHECK_TEXT(rows[2].value, "8");
  CHECK_TEXT(rows[3].value, "4/5");
  CHECK_TEXT(rows[4].value, "14 dBm");
  CHECK(rows[5].id == RowId::RadioApply && rows[5].kind == RowKind::Action);

  // Regions: Read, one row per scope, Save, SI preset. The region rows carry H/D keys.
  n = adminRows(2, v, st, 4, "SLO-RPTR-1", regions, neigh, rows, kAdminRowCap);
  CHECK(n >= 6);
  CHECK(rows[0].id == RowId::RegionRead);
  CHECK(rows[1].id == RowId::RegionRow);
  CHECK_TEXT(rows[1].label, "*");
  CHECK_TEXT(rows[1].value, "deny");
  CHECK(rows[1].keys == 2);
  CHECK_TEXT(rows[2].label, "si");
  CHECK_TEXT(rows[2].value, "allow, home");
  CHECK(rows[2].on);
  CHECK_TEXT(rows[3].label, "lj");
  CHECK(rows[4].id == RowId::RegionSave);
  CHECK(rows[5].id == RowId::SiPreset);

  // Mesh: the three path-hash values, four loop modes, adverts, flood, toggles and identity.
  n = adminRows(3, v, st, 4, "SLO-RPTR-1", regions, neigh, rows, kAdminRowCap);
  CHECK(n == 12);
  CHECK(rows[0].id == RowId::PathHash);
  CHECK_TEXT(rows[0].value, "2B");
  CHECK_TEXT(rows[1].value, "moderate");
  CHECK_TEXT(rows[2].value, "120 min");
  CHECK_TEXT(rows[3].value, "13 h");
  CHECK_TEXT(rows[4].value, "8");
  CHECK(rows[5].id == RowId::Repeat && rows[5].kind == RowKind::Toggle && rows[5].on);
  CHECK(rows[6].id == RowId::PowerSave && !rows[6].on);
  CHECK(rows[7].id == RowId::SharePos && rows[7].on);
  CHECK(rows[8].id == RowId::EditName && rows[8].kind == RowKind::Edit);
  CHECK_TEXT(rows[8].value, "SLO-RPTR-1");
  CHECK(rows[9].id == RowId::EditOwner);
  CHECK(rows[10].id == RowId::EditPos);
  CHECK(rows[11].id == RowId::UseGpsPos);

  // Neighbours: discover, read, then the rows the reply produced.
  n = adminRows(4, v, st, 4, "SLO-RPTR-1", regions, neigh, rows, kAdminRowCap);
  CHECK(n == 3);
  CHECK(rows[0].id == RowId::NeighDiscover);
  CHECK(rows[1].id == RowId::NeighRead);
  CHECK(rows[2].id == RowId::NeighRow);
  CHECK_TEXT(rows[2].label, "Bohinj-RPTR");
  CHECK_TEXT(rows[2].value, "SNR -8.50 dB");
  NeighbourList none;
  none.valid = true;
  n = adminRows(4, v, st, 4, "SLO-RPTR-1", regions, none, rows, kAdminRowCap);
  CHECK(n == 3);
  CHECK_TEXT(rows[2].value, "none read yet");

  // Actions: the three adverts/clock, silent, resume and the dangerous reboot.
  n = adminRows(5, v, st, 4, "SLO-RPTR-1", regions, neigh, rows, kAdminRowCap);
  CHECK(n == 6);
  CHECK(rows[0].id == RowId::AdvertLocalOnce);
  CHECK_TEXT(rows[0].value, "advert.zerohop");
  CHECK_TEXT(rows[1].value, "advert");
  CHECK_TEXT(rows[2].value, "clock sync");
  CHECK(rows[3].id == RowId::Silent);
  CHECK(rows[4].id == RowId::Resume);
  CHECK(rows[5].id == RowId::Reboot && rows[5].danger);

  CHECK_TEXT(viewName(0), "Overview");
  CHECK_TEXT(viewName(5), "Actions");
}

// --- the layout the rows are drawn and hit in ---------------------------------------------------
static void testAdminLayout() {
  const om::ui::Layout& L = om::ui::l2();
  const om::ui::RptrAdminLayout a = om::ui::rptrAdminLayoutFor(L);
  CHECK(a.back.y >= L.status.bottom());
  CHECK(a.body.y >= a.chips[0].bottom());
  CHECK(a.body.bottom() <= a.answer.y);
  CHECK(a.answer.bottom() <= L.tabbar.y);
  CHECK(a.chips[om::ui::kRptrViewCount - 1].right() <= L.width);
  for (int i = 1; i < om::ui::kRptrViewCount; ++i) {
    CHECK(a.chips[i].x > a.chips[i - 1].x);
    CHECK(!a.chips[i].overlaps(a.chips[i - 1]));
  }
  CHECK(a.visibleRows >= 2);
  CHECK(om::ui::rptrAdminChipAt(a, a.chips[2].x + 2, a.chips[2].y + 2) == 2);
  CHECK(om::ui::rptrAdminChipAt(a, a.chips[2].x + 2, a.chips[2].y - 4) == -1);

  // A row's two keys answer -/+ (or H/D) and the rest of the row answers as the body.
  const uint8_t keys[4] = {2, 2, 0, 1};
  const om::ui::RptrRowRects r0 = om::ui::rptrRowRects(a, 2, 0, 0);
  CHECK(om::ui::rptrAdminRowAt(a, keys, 4, 0, r0.body.x + 5, r0.body.y + 5).part == 0);
  CHECK(om::ui::rptrAdminRowAt(a, keys, 4, 0, r0.keyA.x + 2, r0.keyA.y + 2).part == 1);
  CHECK(om::ui::rptrAdminRowAt(a, keys, 4, 0, r0.keyB.x + 2, r0.keyB.y + 2).part == 2);
  CHECK(om::ui::rptrAdminRowAt(a, keys, 4, 0, r0.body.x + 5, r0.body.y - 5).row == -1);
  CHECK(om::ui::rptrAdminRowAt(a, keys, 4, 0, r0.body.x + 5, a.answer.y + 5).row == -1);
  // Scrolled: the first row is gone and the second answers.
  const om::ui::RptrRowRects r0s = om::ui::rptrRowRects(a, 2, 0, 40);
  const om::ui::RptrRowRects r1 = om::ui::rptrRowRects(a, 2, 1, 40);
  CHECK(om::ui::rptrAdminRowAt(a, keys, 4, 40, r1.body.x + 5, r1.body.y + 12).row == 1);
  // Row 0's visible rectangle at this scroll is above the body: a point there hits nothing.
  CHECK(r0s.body.bottom() < a.body.y);
  CHECK(om::ui::rptrAdminRowAt(a, keys, 4, 40, r0s.body.x + 5, a.body.y + 2).row == 1);
  CHECK(om::ui::rptrAdminMaxScroll(a, 1) == 0);
  CHECK(om::ui::rptrAdminMaxScroll(a, 20) == 20 * om::ui::kRptrAdminRowH - a.body.h);
}

// --- the USB command -----------------------------------------------------------------------------
static void testUsb() {
  char line[64];
  snprintf(line, sizeof(line), "rptr scan");
  om::usb::Command c = om::usb::parseCommand(line);
  CHECK(c.kind == om::usb::Kind::Rptr);
  CHECK(c.error == nullptr);
  CHECK_TEXT(c.target, "scan");

  snprintf(line, sizeof(line), "rptr status SLO-RPTR-1");
  c = om::usb::parseCommand(line);
  CHECK(c.kind == om::usb::Kind::Rptr);
  CHECK(c.error == nullptr);
  CHECK_TEXT(c.target, "status");
  CHECK_TEXT(c.text, "SLO-RPTR-1");

  snprintf(line, sizeof(line), "rptr status");
  c = om::usb::parseCommand(line);
  CHECK(c.kind == om::usb::Kind::Rptr && c.error != nullptr);   // a name is required

  snprintf(line, sizeof(line), "rptr");
  c = om::usb::parseCommand(line);
  CHECK(c.kind == om::usb::Kind::Rptr && c.error != nullptr);

  snprintf(line, sizeof(line), "rptr password SLO-RPTR-1");
  c = om::usb::parseCommand(line);
  CHECK(c.kind == om::usb::Kind::Rptr && c.error != nullptr);   // no word reads a secret back

  snprintf(line, sizeof(line), "rptr scan extra");
  c = om::usb::parseCommand(line);
  CHECK(c.kind == om::usb::Kind::Rptr && c.error != nullptr);

  // The old surface still parses.
  snprintf(line, sizeof(line), "wifi status");
  c = om::usb::parseCommand(line);
  CHECK(c.kind == om::usb::Kind::Wifi && c.error == nullptr);
}

int main() {
  testCliLines();
  testPassword();
  testFoundList();
  testLogin();
  testStatus();
  testParsers();
  testConfirm();
  testAdminRows();
  testAdminLayout();
  testUsb();
  if (g_fail == 0) printf("  OK  est358: CLI lines, discovery, login, status, parsers, rows, USB\n");
  return g_fail == 0 ? 0 : 1;
}
