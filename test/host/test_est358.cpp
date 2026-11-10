// EST-358 host test: the RPTR tab's logic (the old admin row table moved to EST-362's page model). Everything proven here runs on the laptop with the system
// g++: the EXACT CLI line every admin control sends (the mock's own strings in
// tests/test_ui_mock.py), the discovery result list, the login state machine and its timeouts, the
// password filter, the status decode, the neighbour and region reply parsers, the two-step confirm,
// the admin row table and the USB `rptr` command.
#include <stdio.h>
#include <stddef.h>
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
  // EST-360: the hard-coded SI preset is gone; every region change is its own word and line. These
  // builders are proved in test_est360.cpp (the whole set), the old preset constants must not exist.

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
  // EST-360: `clock sync` is replaced by `time <epoch>` from our clock (test_est360).
  CHECK_TEXT(kCliRegionHomeRead, "region home");
  CHECK_TEXT(kCliRegionDefaultRead, "region default");
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
  CHECK(f.addAnswer(k1, 6, "SLO-RPTR-1", true, true, 30, true, -34, 0, 1100) == 0);
  f.stateText(text, sizeof(text));
  CHECK_TEXT(text, "1 found, listening");
  CHECK(f.addAnswer(k2, 6, "", false, true, 10, false, 0, 2, 1200) == 1);
  // A repeater that answers twice stays ONE row, with the newer SNR/time.
  CHECK(f.addAnswer(k1, 6, "SLO-RPTR-1", true, true, 31, true, -35, 0, 1300) == 0);
  CHECK(f.count == 2);
  CHECK(f.rows[0].hasSnrIn && f.rows[0].snrIn4 == 31);
  CHECK(f.rows[0].snrOut4 == -35);
  CHECK(f.rows[0].inContacts);
  CHECK(f.rows[0].heardMs == 1300);
  CHECK(f.rows[0].keyLen == 6);   // a 6-byte answer cannot be asked for regions
  CHECK(!f.rows[1].inContacts);
  CHECK(f.rows[1].name[0] == 0);
  CHECK(f.rows[1].hops == 2);

  // EST-360: a full-key answer keeps all 32 bytes (the anonymous regions request needs them), and
  // the row's regions answer lands on the right row with a serial bump.
  uint8_t full[32];
  for (int i = 0; i < 32; ++i) full[i] = (uint8_t)(0x40 + i);
  const uint32_t s0 = f.serial;
  const int fi = f.addAnswer(full, 32, "Argus", true, true, 12, true, 20, 0, 1400);
  CHECK(fi == 2);
  CHECK(f.rows[fi].keyLen == 32);
  CHECK(memcmp(f.rows[fi].key, full, 32) == 0);
  AnonRegions ar;
  ar.clock = 1762790400u;
  ar.clockValid = true;
  ar.count = 2;
  snprintf(ar.text, sizeof(ar.text), "si, lj");
  CHECK(f.setRegions(fi, ar));
  CHECK(f.serial > s0);
  CHECK(f.rows[fi].anon.valid);   // setRegions stores a parsed answer
  CHECK(f.rows[fi].regionsAsked);
  CHECK_TEXT(f.rows[fi].anon.text, "si, lj");
  CHECK(!f.setRegions(99, ar));

  // The window closes after kScanWindowMs, and one serial bump follows.
  const uint32_t before = f.serial;
  f.tick(1000 + kScanWindowMs - 1);
  CHECK(f.listening);
  f.tick(1000 + kScanWindowMs);
  CHECK(!f.listening);
  CHECK(f.serial == before + 1);
  f.stateText(text, sizeof(text));
  CHECK_TEXT(text, "3 found");
  const uint32_t after = f.serial;
  f.tick(60000);
  CHECK(f.serial == after);   // no repeated bumps once it is closed

  // A new scan forgets the rows; a full list refuses the 17th answer rather than overrunning.
  f.begin(7, 2000);
  CHECK(f.count == 0 && f.listening && f.tag == 7);
  uint8_t key[6] = {0, 0, 0, 0, 0, 0};
  for (int i = 0; i < kMaxFound; ++i) {
    key[0] = (uint8_t)i;
    CHECK(f.addAnswer(key, 6, "R", true, true, 0, false, 0, 0, 2000) == i);
  }
  key[0] = 200;
  CHECK(f.addAnswer(key, 6, "R", true, true, 0, false, 0, 0, 2000) == -1);
  CHECK(f.count == kMaxFound);
  f.begin(1, 0);
  f.tick(kScanWindowMs);
  f.stateText(text, sizeof(text));
  CHECK_TEXT(text, "none answered");
}

// --- login --------------------------------------------------------------------------------------
static void testLogin() {
  // A current MeshCore answer as the Listener receives it: OK, keep-alive/16 = 0, admin = 1,
  // permissions 1, random, fw level 4. Nine bytes: the 4-byte timestamp is gone (it is the `tag`).
  const uint8_t ok[9] = {0, 0, 1, 1, 0, 0, 0, 0, 4};
  LoginAnswer a;
  CHECK(loginParse(ok, sizeof(ok), &a));
  CHECK(a.ok && a.admin && !a.legacy);
  CHECK(a.permissions == 1);
  CHECK(a.fwLevel == 4);
  // EST-360: the listener's buffer has ALREADY lost the 4-byte timestamp. A buffer that still has
  // the tag in front (the old mistake) must not parse as a login.
  const uint8_t tagged[13] = {0x11, 0x22, 0x33, 0x44, 0, 0, 1, 1, 0, 0, 0, 0, 4};
  CHECK(loginParse(tagged, sizeof(tagged), &a));
  CHECK(!a.ok);

  // A guest: is_admin 0.
  const uint8_t guest[9] = {0, 0, 0, 2, 0, 0, 0, 0, 4};
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
// EST-360: exactly the struct simple_repeater sends (examples/simple_repeater/MyMesh.h), so the test
// cannot pass with a shape the receiver never sees. sizeof is asserted and the offsets the parser
// reads are checked below.
struct TestRepeaterStats {
  uint16_t batt_milli_volts;
  uint16_t curr_tx_queue_len;
  int16_t noise_floor;
  int16_t last_rssi;
  uint32_t n_packets_recv;
  uint32_t n_packets_sent;
  uint32_t total_air_time_secs;
  uint32_t total_up_time_secs;
  uint32_t n_sent_flood, n_sent_direct;
  uint32_t n_recv_flood, n_recv_direct;
  uint16_t err_events;
  int16_t last_snr;   // x 4
  uint16_t n_direct_dups, n_flood_dups;
  uint32_t total_rx_air_time_secs;
  uint32_t n_recv_errors;
};
static_assert(sizeof(TestRepeaterStats) == kRepeaterStatsBytes, "RepeaterStats must be 56 B");
static_assert(offsetof(TestRepeaterStats, noise_floor) == 4, "noise floor offset");
static_assert(offsetof(TestRepeaterStats, n_packets_recv) == 8, "packets offset");
static_assert(offsetof(TestRepeaterStats, err_events) == 40, "err_events offset");
static_assert(offsetof(TestRepeaterStats, total_rx_air_time_secs) == 48, "rx airtime offset");
static_assert(offsetof(TestRepeaterStats, n_recv_errors) == 52, "errors offset");

static void testStatus() {
  // The Listener hands statusParse() the RepeaterStats ALONE (MyMesh::onContactResponse already
  // dropped the 4-byte timestamp). These are the ticket's known values.
  TestRepeaterStats st{};
  st.batt_milli_volts = 4120;
  st.curr_tx_queue_len = 2;
  st.noise_floor = -112;
  st.last_rssi = -70;
  st.n_packets_recv = 290;
  st.n_packets_sent = 91;
  st.total_air_time_secs = 4000;
  st.total_up_time_secs = 90061;
  st.n_sent_flood = 40;
  st.n_sent_direct = 51;
  st.n_recv_flood = 200;
  st.n_recv_direct = 90;
  st.err_events = 3;
  st.last_snr = 25;
  st.n_direct_dups = 7;
  st.n_flood_dups = 9;
  st.total_rx_air_time_secs = 3900;
  st.n_recv_errors = 11;

  Status s;
  CHECK(statusParse((const uint8_t*)&st, sizeof(st), &s));
  CHECK(s.valid);
  CHECK(s.battMv == 4120);         // NOT the noise floor: 0xFF90 would have read as 65424
  CHECK(s.txQueue == 2);
  CHECK(s.noiseFloor == -112);
  CHECK(s.lastRssi == -70);
  CHECK(s.rxPackets == 290);
  CHECK(s.txPackets == 91);
  CHECK(s.airtimeSec == 4000);
  CHECK(s.uptimeSec == 90061);
  CHECK(s.lastSnr4 == 25);
  CHECK(s.errEvents == 3);
  CHECK(s.rxAirtimeSec == 3900);
  CHECK(s.rxCrcErrors == 11);

  // A newer firmware's longer reply is accepted; the known fields keep their offsets.
  uint8_t longer[kRepeaterStatsBytes + 8] = {};
  memcpy(longer, &st, sizeof(st));
  Status sl;
  CHECK(statusParse(longer, sizeof(longer), &sl));
  CHECK(sl.battMv == 4120 && sl.rxCrcErrors == 11);

  // A short buffer is refused whole: no half-filled card.
  Status s2;
  CHECK(!statusParse((const uint8_t*)&st, sizeof(st) - 1, &s2));
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
  clockText(t, sizeof(t), 1700000000u);   // 2023-11-14: below the 2025 cut-off
  CHECK_TEXT(t, "not set (2023)");
  clockText(t, sizeof(t), 1715779092u);   // Argus's 2024-05-15 MeshCore restart clock
  CHECK_TEXT(t, "not set (2024)");
  clockText(t, sizeof(t), 1735689600u);   // 2025-01-01 00:00 UTC is real
  CHECK_TEXT(t, "00:00 UTC");
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

  // EST-360: the anonymous regions read, whole scan or one repeater.
  snprintf(line, sizeof(line), "rptr regions");
  c = om::usb::parseCommand(line);
  CHECK(c.kind == om::usb::Kind::Rptr && c.error == nullptr);
  CHECK_TEXT(c.target, "regions");
  CHECK(c.text == nullptr);
  snprintf(line, sizeof(line), "rptr regions Argus");
  c = om::usb::parseCommand(line);
  CHECK(c.kind == om::usb::Kind::Rptr && c.error == nullptr);
  CHECK_TEXT(c.target, "regions");
  CHECK_TEXT(c.text, "Argus");
  snprintf(line, sizeof(line), "rptr regions Argus extra");
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
  testUsb();
  if (g_fail == 0) printf("  OK  est358: CLI lines, discovery, login, status, parsers, USB\n");
  return g_fail == 0 ? 0 : 1;
}
