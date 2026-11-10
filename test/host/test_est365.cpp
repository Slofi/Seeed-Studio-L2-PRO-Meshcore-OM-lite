// EST-365 host test: the badge model, the `msgs` order across a reboot, the radio/loop model, the
// anonymous name answer and the `rptr login` command surface.
//
//   1. TAB BAR. Every tab and every page state carries the same TabBarModel from main.cpp's one
//      currentTabBar() call. The RPTR page used to pass a literal 0 and lost the unread badge; the
//      bar's API no longer takes a count at all, so the model is the only source.
//   2. MSGS ORDER. At 20:38 the dump ended with seq 32 at 19:11 because a restored message carries
//      the uptime of the boot it was recorded in. The order key is the real WALL time (the field the
//      screens print), with this boot's sequence as the tie-break; uptime is never a key.
//   3. RADIO STATS MODEL. The noise window (one sample per 5 s, min/avg over minutes), the last-20
//      reception ring (decoded from MeshCore's raw header), and the loop histogram with per-part
//      attribution.
//   4. UNNAMED REPEATERS. simple_repeater's ANON_REQ 0x02 answer is clock + "<name>\n<owner>";
//      an empty first line is not a name.
//   5. `radio` / `radio packets` and `rptr login <name> <password>` parse; the log form never
//      contains the password.
// Run by test/host/run.sh (system g++).
#include <initializer_list>
#include <stdio.h>
#include <string.h>

#include "msg_order.h"
#include "radio_stats.h"
#include "rptr_model.h"
#include "ui/tab_bar.h"
#include "usb_cmd_logic.h"

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

using namespace om::radio;

// ------------------------------------------------------------------------------------ 1. the tab bar
static void testTabBarModel() {
  const om::ui::Tab tabs[] = {om::ui::Tab::Nodes, om::ui::Tab::Chat, om::ui::Tab::Map,
                              om::ui::Tab::Rptr};
  // The count a screen must show, whatever tab/page/modal/sheet state it is in.
  for (om::ui::Tab t : tabs) {
    for (int unread : {0, 1, 9, 10, 32, 99, 100, 250}) {
      for (bool dot : {false, true}) {
        const om::ui::TabBarModel m = om::ui::tabBarModel(t, unread, dot);
        CHECK(m.active == t);
        CHECK(m.unread == unread);
        CHECK(m.newNodeDot == dot);
      }
    }
  }
  // A negative count (a caller's arithmetic slip) draws nothing, never a broken badge.
  CHECK(om::ui::tabBarModel(om::ui::Tab::Rptr, -3, false).unread == 0);
  // The bug itself: the RPTR page's old call passed a literal 0 for every state. The model keeps
  // the count for the list, a page, a page with a sheet and a confirmation pop-up alike.
  const om::ui::TabBarModel rptr = om::ui::tabBarModel(om::ui::Tab::Rptr, 32, false);
  CHECK(rptr.unread == 32);
  CHECK(rptr.active == om::ui::Tab::Rptr);
}

// ------------------------------------------------------------------------------------ 2. msgs order
struct Item {
  om::MsgOrderKey key;
  const char* who;
};

static void sortItems(Item* items, int n) {
  for (int i = 1; i < n; ++i) {
    const Item x = items[i];
    int j = i - 1;
    while (j >= 0 && om::msgOrderBefore(x.key, items[j].key)) {
      items[j + 1] = items[j];
      --j;
    }
    items[j + 1] = x;
  }
}

static void testMsgOrder() {
  // The 20:38 run: a restored ring (uptime from the previous boot, ~5 h) and new arrivals (uptime
  // ~2 min). The old sort by uptime put the restored 19:11 entry last.
  Item m[4];
  m[0].who = "restored 19:11";   // wall 19:11, uptime 18000
  m[0].key.wall = 19 * 3600 + 11 * 60;
  m[0].key.wallReal = true;
  m[0].key.uptime = 18000;
  m[0].key.seq = 30;
  m[1].who = "restored 19:12";
  m[1].key.wall = 19 * 3600 + 12 * 60;
  m[1].key.wallReal = true;
  m[1].key.uptime = 18060;
  m[1].key.seq = 31;
  m[2].who = "new 20:37";
  m[2].key.wall = 20 * 3600 + 37 * 60;
  m[2].key.wallReal = true;
  m[2].key.uptime = 120;
  m[2].key.seq = 32;
  m[3].who = "new 20:38";
  m[3].key.wall = 20 * 3600 + 38 * 60;
  m[3].key.wallReal = true;
  m[3].key.uptime = 130;
  m[3].key.seq = 33;
  sortItems(m, 4);
  CHECK(strcmp(m[0].who, "restored 19:11") == 0);
  CHECK(strcmp(m[1].who, "restored 19:12") == 0);
  CHECK(strcmp(m[2].who, "new 20:37") == 0);
  CHECK(strcmp(m[3].who, "new 20:38") == 0);
  // The newest is last (what `msgs` prints last).
  CHECK(!om::msgOrderBefore(m[3].key, m[2].key));

  // Both clocks unknown: this boot's arrival (seq allocated after restore) still follows the
  // restored ones, so the dump stays chronological even before the first clock sync.
  Item u[2];
  u[0].who = "restored, no clock";
  u[0].key.seq = 7;
  u[0].key.wall = 0;
  u[0].key.wallReal = false;
  u[1].who = "new, no clock";
  u[1].key.seq = 8;
  u[1].key.wall = 0;
  u[1].key.wallReal = false;
  sortItems(u, 2);
  CHECK(strcmp(u[0].who, "restored, no clock") == 0);
  CHECK(strcmp(u[1].who, "new, no clock") == 0);

  // A fake 2024 stamp is not real: it sorts with the unknown-clock entries, never after 2026.
  Item f[2];
  f[0].who = "fake 2024";
  f[0].key.wall = 1700000000;
  f[0].key.wallReal = false;
  f[0].key.seq = 2;
  f[1].who = "real 2026";
  f[1].key.wall = 1762800000;
  f[1].key.wallReal = true;
  f[1].key.seq = 3;
  sortItems(f, 2);
  CHECK(strcmp(f[0].who, "fake 2024") == 0);
  CHECK(strcmp(f[1].who, "real 2026") == 0);

  // Equal wall stamps (two chats, same second): the per-boot sequence decides, never uptime.
  om::MsgOrderKey a;
  a.wall = 1762800000;
  a.wallReal = true;
  a.uptime = 9000;   // restored
  a.seq = 4;
  om::MsgOrderKey b;
  b.wall = 1762800000;
  b.wallReal = true;
  b.uptime = 10;     // new
  b.seq = 5;
  CHECK(om::msgOrderBefore(a, b));
  CHECK(!om::msgOrderBefore(b, a));
}

// ------------------------------------------------------------------------------- 3. radio stats model
static void testNoiseWindow() {
  begin(0);
  Snapshot s = snapshot();
  CHECK(s.noise.samples == 0);
  CHECK(s.packetsHeard == 0);

  noteNoise(-117, 0);
  noteNoise(-90, 1000);   // inside the 5 s period: must not enter the window
  s = snapshot();
  CHECK(s.noise.samples == 1);
  CHECK(s.noise.last == -117);
  CHECK(s.noise.min == -117);
  CHECK(s.noise.avg == -117);

  noteNoise(-119, 6000);
  noteNoise(-115, 11000);
  s = snapshot();
  CHECK(s.noise.samples == 3);
  CHECK(s.noise.last == -115);
  CHECK(s.noise.min == -119);
  CHECK(s.noise.avg == -117);   // (-117 -119 -115)/3 = -117

  // A spike is visible in the average and the last value; a quieter sample moves the min.
  noteNoise(-100, 16000);
  noteNoise(-121, 21000);
  s = snapshot();
  CHECK(s.noise.samples == 5);
  CHECK(s.noise.min == -121);
  CHECK(s.noise.last == -121);

  // The window is bounded: 36 samples, then the oldest falls out and the min can rise again.
  begin(0);
  for (int i = 0; i < kNoiseSamples; ++i) noteNoise(-120 - (i == 0 ? 20 : 0), (uint32_t)i * 5000);
  s = snapshot();
  CHECK(s.noise.samples == kNoiseSamples);
  CHECK(s.noise.min == -140);   // sample 0 is still inside
  noteNoise(-110, (uint32_t)kNoiseSamples * 5000);
  s = snapshot();
  CHECK(s.noise.samples == kNoiseSamples);
  CHECK(s.noise.min == -120);   // the -140 fell out; the rest are -120
  CHECK(s.noise.last == -110);
}

// One MeshCore raw frame: header, [transport], path_len, path, payload.
static int buildRaw(uint8_t* out, uint8_t type, uint8_t route, int pathCount,
                    const uint8_t* payload, int payloadLen) {
  int at = 0;
  out[at++] = (uint8_t)((type << 2) | route);
  if (route == 0x00 || route == 0x03) {
    out[at++] = 0;
    out[at++] = 0;
    out[at++] = 0;
    out[at++] = 0;
  }
  out[at++] = (uint8_t)pathCount;   // hash size 1, count pathCount
  for (int i = 0; i < pathCount; ++i) out[at++] = (uint8_t)(0x10 + i);
  for (int i = 0; i < payloadLen; ++i) out[at++] = payload[i];
  return at;
}

static void testPacketRing() {
  begin(0);
  uint8_t payload[8] = {0xA1, 0xB2, 0xC3, 0xD4, 0x01, 0x02, 0x03, 0x04};
  uint8_t raw[64];
  for (int i = 0; i < 25; ++i) {
    payload[0] = (uint8_t)i;
    const int len = buildRaw(raw, 0x05, 0x01, 2, payload, 8);   // GRP_TXT, flood, 2 hashes
    noteRawPacket(raw, len, -90 - i, -40 + i, 1762800000u + (uint32_t)i, true,
                  (uint32_t)i * 1000);
  }
  Snapshot s = snapshot();
  CHECK(s.packetsHeard == 25);
  CHECK(s.packetSamples == kPacketSamples);
  CHECK(packetCount() == kPacketSamples);
  PacketSample p;
  CHECK(packetAt(0, &p));           // newest
  CHECK(p.hashValid);
  CHECK(p.hash[0] == 24);           // payload[0] of the 25th packet
  CHECK(p.type == 0x05);
  CHECK(p.route == 0x01);
  CHECK(p.pathCount == 2);
  CHECK(p.pathHashSize == 1);
  CHECK(p.rssi == -114);
  CHECK(p.snr4 == -16);
  CHECK(p.len == 1 + 1 + 2 + 8);
  CHECK(packetAt(19, &p));          // oldest kept
  CHECK(p.hash[0] == 5);            // 25 - 20 = 5
  CHECK(!packetAt(20, &p));
  CHECK(!packetAt(-1, &p));
  CHECK(strcmp(payloadTypeName(0x05), "GRP_TXT") == 0);
  CHECK(strcmp(routeName(0x01), "flood") == 0);

  // A transport-route packet has four transport bytes before path_len; the hash and path survive.
  begin(0);
  const int len = buildRaw(raw, 0x04, 0x00, 1, payload, 4);   // ADVERT, transport flood
  noteRawPacket(raw, len, -80, 24, 1762800100u, true, 5000);
  CHECK(packetAt(0, &p));
  CHECK(p.type == 0x04);
  CHECK(p.route == 0x00);
  CHECK(p.pathCount == 1);
  CHECK(p.hash[0] == payload[0]);

  // A frame too short to hold a payload still counts as heard and never reads outside the buffer.
  begin(0);
  const uint8_t tiny[2] = {0x15, 0x00};
  noteRawPacket(tiny, 2, -100, 0, 0, false, 1000);
  CHECK(snapshot().packetsHeard == 1);
  CHECK(packetAt(0, &p));
  CHECK(!p.hashValid);
}

static void testLoopHistogram() {
  begin(0);
  // Pass 1: 500 us total, 400 us in GPS. The remainder is charged to Other.
  beginPass(0);
  addPart(Part::Gps, 400);
  endPass(500, 0);
  Snapshot s = snapshot();
  CHECK(s.lastPassUs == 500);
  CHECK(s.worstUs == 500);
  CHECK(s.worstPart == Part::Gps);
  CHECK(passPartUs(Part::Other) == 100);
  CHECK(histogramBucket(0) == 1);
  CHECK(histogramLastPassTotal() == 1);

  // Pass 2: 120 ms, 60 ms pushing the frame and 50 ms drawing it. The push wins.
  beginPass(1000000);
  addPart(Part::FramePush, 60000);
  addPart(Part::Draw, 50000);
  addPart(Part::History, 10000);
  endPass(1000000 + 120000, 5000);
  s = snapshot();
  CHECK(s.lastPassUs == 120000);
  CHECK(s.worstUs == 120000);
  CHECK(s.worstPart == Part::FramePush);
  CHECK(histogramBucket(7) == 1);   // <200 ms
  CHECK(histogramLastPassTotal() == 2);

  // A 120 ms pass is below one packet's airtime (SF8/BW62.5), so it is not the dangerous case; the
  // threshold the report uses is one packet.
  CHECK(s.worstUs < kOnePacketUs);
  CHECK(kOnePacketUs == 300000);

  // Pass 3: a short pass. The since-boot worst stays the 120 ms push.
  beginPass(2000000);
  addPart(Part::Usb, 300);
  endPass(2000000 + 1000, 20000);
  s = snapshot();
  CHECK(s.worstUs == 120000);
  CHECK(s.worstPart == Part::FramePush);
  CHECK(s.lastPassUs == 1000);

  // The per-minute window: a pass after the first 60 s reports the previous minute's worst and
  // starts a new window.
  begin(0);
  beginPass(0);
  addPart(Part::Mesh, 7000);
  endPass(7000, 1000);
  beginPass(10000000);
  addPart(Part::Rptr, 2000);
  endPass(10002000, 61000);
  s = snapshot();
  CHECK(s.worstMinuteUs == 7000);
  CHECK(s.worstMinutePart == Part::Mesh);
  CHECK(s.worstUs == 7000);
  // ...and the new minute starts from this pass.
  beginPass(20000000);
  addPart(Part::Gps, 900);
  endPass(20000900, 62000);
  s = snapshot();
  CHECK(s.worstMinuteUs == 7000);   // not reported until the next boundary
  CHECK(s.lastPassUs == 900);

  // Every block has a name; the USB line prints partName().
  CHECK(strcmp(partName(Part::History), "history") == 0);
  CHECK(strcmp(partName(Part::MapDecode), "map") == 0);
  CHECK(strcmp(partName(Part::FramePush), "push") == 0);
  CHECK(strcmp(partName(Part::Other), "other") == 0);
  begin(0);
}

// ---------------------------------------------------------------------------- 4. the anonymous name
static void testAnonOwner() {
  uint8_t buf[64];
  const uint32_t clock = 1762800000;
  memcpy(buf, &clock, 4);
  const char* body = "Argus mobile RPTR\nFilip";
  memcpy(buf + 4, body, strlen(body));
  char name[32];
  char owner[32];
  CHECK(om::rptr::anonOwnerParse(buf, 4 + (int)strlen(body), name, sizeof(name), owner,
                                sizeof(owner)));
  CHECK(strcmp(name, "Argus mobile RPTR") == 0);
  CHECK(strcmp(owner, "Filip") == 0);

  // No owner line: the name alone is a valid answer and owner stays empty.
  const char* just = "db744b";
  memcpy(buf + 4, just, strlen(just));
  CHECK(om::rptr::anonOwnerParse(buf, 4 + (int)strlen(just), name, sizeof(name), owner,
                                sizeof(owner)));
  CHECK(strcmp(name, "db744b") == 0);
  CHECK(owner[0] == 0);

  // Clock only / empty first line: not a name.
  CHECK(!om::rptr::anonOwnerParse(buf, 4, name, sizeof(name), owner, sizeof(owner)));
  buf[4] = '\n';
  buf[5] = 'x';
  CHECK(!om::rptr::anonOwnerParse(buf, 6, name, sizeof(name), owner, sizeof(owner)));

  // Longer than the caller's buffer: truncated, still NUL-terminated.
  char small[6];
  memcpy(buf + 4, "abcdefghij\n", 11);
  CHECK(om::rptr::anonOwnerParse(buf, 15, small, sizeof(small), nullptr, 0));
  CHECK(strcmp(small, "abcde") == 0);
}

// ------------------------------------------------------------------------------ 5. the command surface
static void testCommands() {
  char line[128];
  snprintf(line, sizeof(line), "radio");
  om::usb::Command c = om::usb::parseCommand(line);
  CHECK(c.error == nullptr);
  CHECK(c.kind == om::usb::Kind::Radio);
  CHECK(c.target == nullptr);

  snprintf(line, sizeof(line), "radio packets");
  c = om::usb::parseCommand(line);
  CHECK(c.error == nullptr);
  CHECK(c.kind == om::usb::Kind::Radio);
  CHECK(c.target != nullptr && strcmp(c.target, "packets") == 0);

  snprintf(line, sizeof(line), "radio nope");
  c = om::usb::parseCommand(line);
  CHECK(c.error != nullptr);
  CHECK(strcmp(c.error, "args") == 0);

  // rptr login: a quoted name with spaces and a password (spaces allowed in a quoted password).
  snprintf(line, sizeof(line), "rptr login \"Argus mobile RPTR\" \"my secret\"");
  c = om::usb::parseCommand(line);
  CHECK(c.error == nullptr);
  CHECK(c.kind == om::usb::Kind::Rptr);
  CHECK(c.target != nullptr && strcmp(c.target, "login") == 0);
  CHECK(c.text != nullptr && strcmp(c.text, "Argus mobile RPTR") == 0);
  CHECK(c.text2 != nullptr && strcmp(c.text2, "my secret") == 0);

  // A missing password is an argument error, not a login with an empty password.
  snprintf(line, sizeof(line), "rptr login Argus");
  c = om::usb::parseCommand(line);
  CHECK(c.error != nullptr);
  CHECK(strcmp(c.error, "args") == 0);

  // The log form never contains the password, for every spelling.
  char log[128];
  snprintf(line, sizeof(line), "rptr login Argus hunter2");
  om::usb::redactCommand(line, log, sizeof(log));
  CHECK(strstr(log, "hunter2") == nullptr);
  CHECK(strstr(log, "password hidden") != nullptr);
  CHECK(strstr(log, "Argus") != nullptr);

  snprintf(line, sizeof(line), "rptr login \"Argus mobile RPTR\" \"secret with spaces\"");
  om::usb::redactCommand(line, log, sizeof(log));
  CHECK(strstr(log, "secret") == nullptr);
  CHECK(strstr(log, "Argus mobile RPTR") != nullptr);

  // A malformed or over-long form is still redacted: only the command and the name survive.
  snprintf(line, sizeof(line), "rptr login Argus secret extra");
  om::usb::redactCommand(line, log, sizeof(log));
  CHECK(strstr(log, "secret") == nullptr);
  CHECK(strstr(log, "extra") == nullptr);

  // Ordinary commands are logged as before.
  snprintf(line, sizeof(line), "send ch2 hello there");
  om::usb::redactCommand(line, log, sizeof(log));
  CHECK(strcmp(log, "send ch2 hello there") == 0);
}

int main() {
  testTabBarModel();
  testMsgOrder();
  testNoiseWindow();
  testPacketRing();
  testLoopHistogram();
  testAnonOwner();
  testCommands();
  if (g_fail == 0) {
    printf("est365: all checks passed\n");
    return 0;
  }
  printf("est365: %d check(s) failed\n", g_fail);
  return 1;
}
