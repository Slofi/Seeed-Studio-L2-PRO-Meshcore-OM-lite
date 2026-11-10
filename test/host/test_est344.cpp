// Host test for EST-344: the DM ring's ONE allocation path (send, reply, two more, wrap), the channel
// ring's shared sequence rule, the toast's overlay state and MeshCore's encoded path length.
//
// The DM part is the regression: the old storeDm() picked slot `s_dmSeq % 32` and THEN set
// `m.seq = ++s_dmSeq`, so a reply landed in the slot of the message before it and the list (which
// reads `seq % 32`) never found it. This test drives the real ring with a send, a reply, two more
// messages and a full wrap.
//
// Plain g++ (no Arduino, no LovyanGFX).
#include <stdio.h>
#include <string.h>

#include "mesh_path.h"
#include "seq_ring.h"
#include "ui/dm_ring.h"
#include "ui/toast.h"

using namespace om;
using namespace om::ui;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// --- the DM ring: one allocation path -------------------------------------------------------------
static void testDmRingSendReplyWrap() {
  DmRing r;
  r.reset();

  uint8_t keyA[kMeshPubKey] = {0};
  uint8_t keyB[kMeshPubKey] = {0};
  keyA[0] = 0x11;
  keyB[0] = 0x22;
  const int a = r.ensureThread(keyA, "ERA-3 (SI)");
  const int b = r.ensureThread(keyB, "Other");
  CHECK(a == 0);
  CHECK(b == 1);
  CHECK(r.threadCount() == 2);
  CHECK(r.ensureThread(keyA, "ignored") == a);   // the same key never makes a second thread

  // 1) We SEND a DM. meshDmSend() allocates seq 1.
  DmMsg* m = r.alloc((uint8_t)a, true);
  CHECK(m != nullptr);
  CHECK(m->seq == 1);
  CHECK(m->thread == (uint8_t)a);
  CHECK(m->mine == 1);
  snprintf(m->name, sizeof(m->name), "%s", "OM Lite");
  snprintf(m->text, sizeof(m->text), "%s", "ping");

  // 2) The REPLY arrives. This is the old bug: it must NOT land in seq 1's slot.
  m = r.alloc((uint8_t)a, false);
  CHECK(m->seq == 2);
  snprintf(m->name, sizeof(m->name), "%s", "ERA-3 (SI)");
  snprintf(m->text, sizeof(m->text), "%s", "pong");

  CHECK(r.find(1) != nullptr);                 // our sent DM is still there
  CHECK(r.find(1)->seq == 1);
  CHECK(strcmp(r.find(1)->text, "ping") == 0);
  CHECK(r.find(2) != nullptr);
  CHECK(strcmp(r.find(2)->text, "pong") == 0);
  CHECK(r.slotAt(1)->seq == 1);                // slot rule: seq N lives in slot N % ring
  CHECK(r.slotAt(2)->seq == 2);

  // 3) Two more arrive.
  m = r.alloc((uint8_t)a, false);
  CHECK(m->seq == 3);
  snprintf(m->name, sizeof(m->name), "%s", "ERA-3 (SI)");
  snprintf(m->text, sizeof(m->text), "%s", "three");
  m = r.alloc((uint8_t)a, false);
  CHECK(m->seq == 4);
  snprintf(m->name, sizeof(m->name), "%s", "ERA-3 (SI)");
  snprintf(m->text, sizeof(m->text), "%s", "four");

  DmMsg out[kDmRing];
  int n = r.list(a, out, kDmRing);
  CHECK(n == 4);
  const char* wantText[4] = {"ping", "pong", "three", "four"};
  const char* wantName[4] = {"OM Lite", "ERA-3 (SI)", "ERA-3 (SI)", "ERA-3 (SI)"};
  const uint8_t wantMine[4] = {1, 0, 0, 0};
  for (int i = 0; i < n; ++i) {
    CHECK(out[i].seq == (uint32_t)(i + 1));
    CHECK(strcmp(out[i].text, wantText[i]) == 0);
    CHECK(strcmp(out[i].name, wantName[i]) == 0);
    CHECK(out[i].mine == wantMine[i]);
  }
  // The other thread is untouched.
  CHECK(r.list(b, out, kDmRing) == 0);

  // 4) The ACK update looks the message up by sequence, through the same slot rule.
  CHECK(r.markDelivered(1, 1234, true));
  CHECK(r.find(1)->state == 1);
  CHECK(r.find(1)->ackWall == 1234);
  CHECK(r.find(1)->ackReal == 1);
  CHECK(!r.setState(1, 1));   // no change, no serial bump
  CHECK(r.setState(3, 2));
  CHECK(r.find(3)->state == 2);

  // 5) WRAP the ring: push it past 32 messages and verify the list is the newest 32, in order,
  // with the right sender on each. This is what the old off-by-one made impossible.
  for (int i = 0; i < 60; ++i) {
    DmMsg* w = r.alloc((uint8_t)a, (i % 2) == 0);
    snprintf(w->name, sizeof(w->name), "%s", w->mine ? "OM Lite" : "ERA-3 (SI)");
    snprintf(w->text, sizeof(w->text), "%lu", (unsigned long)w->seq);
  }
  CHECK(r.count() == kDmRing);
  CHECK(r.newestSeq() == 64);
  n = r.list(a, out, kDmRing);
  CHECK(n == kDmRing);
  for (int i = 0; i < n; ++i) {
    const uint32_t seq = 64u - (uint32_t)(n - 1 - i);
    CHECK(out[i].seq == seq);
    char want[16];
    snprintf(want, sizeof(want), "%lu", (unsigned long)seq);
    CHECK(strcmp(out[i].text, want) == 0);
    // seq 1..4 were ours/replies; from 5 on the parity rule above is the sender.
    const bool mine = seq <= 4 ? (seq == 1) : (((seq - 5) % 2) == 0);
    CHECK(strcmp(out[i].name, mine ? "OM Lite" : "ERA-3 (SI)") == 0);
    CHECK(out[i].mine == (mine ? 1 : 0));
  }
  CHECK(r.find(1) == nullptr);   // seq 1 has fallen out of the ring
  CHECK(r.find(64) != nullptr);
}

// --- the channel ring's one rule ------------------------------------------------------------------
static void testSeqRing() {
  SeqRing<32> ring;
  CHECK(ring.count() == 0);
  CHECK(ring.newest() == 0);
  CHECK(ring.nextSeq() == 1);
  for (uint32_t s = 1; s <= 40; ++s) CHECK(ring.alloc() == s);
  CHECK(ring.newest() == 40);
  CHECK(ring.count() == 32);
  CHECK(ring.firstSeq() == 9);          // 40 - 32 + 1; seq 1..8 fell out
  // Live seqs map to 32 distinct slots, and the slot rule is seq % 32 for every one.
  bool seen[32] = {false};
  for (uint32_t s = ring.firstSeq(); s <= ring.newest(); ++s) {
    const int slot = SeqRing<32>::slotOf(s);
    CHECK(slot >= 0 && slot < 32);
    CHECK(!seen[slot]);
    seen[slot] = true;
    CHECK(slot == (int)(s % 32));
  }
  // The slot of seq 8 now holds seq 40 (one slot, newest message), which is exactly why a reader
  // must compare seq and never trust the slot alone.
  CHECK(SeqRing<32>::slotOf(8) == 8);
  CHECK(SeqRing<32>::slotOf(40) == 8);
  ring.reset();
  CHECK(ring.count() == 0);
  CHECK(ring.firstSeq() == 1);
}

// --- the toast overlay state ----------------------------------------------------------------------
static void testToastState() {
  ToastState t;
  CHECK(!t.active());
  CHECK(!t.visible(0));
  CHECK(!t.expire(0));

  CHECK(t.show("New node: X (repeater)", 1000, 3000));
  CHECK(t.active());
  CHECK(t.visible(1000));
  CHECK(t.visible(3999));
  CHECK(!t.visible(4000));                  // at the deadline the toast is over
  CHECK(!t.expire(3999));
  CHECK(t.visible(3999));
  CHECK(t.expire(4000));                    // exactly one expiry event
  CHECK(!t.active());
  CHECK(!t.expire(4001));
  CHECK(!t.visible(4001));
  CHECK(strcmp(t.text, "") == 0);

  // Empty text and zero duration cannot create an invisible active state.
  CHECK(!t.show("", 0, 1000));
  CHECK(!t.show(nullptr, 0, 1000));
  CHECK(!t.show("x", 0, 0));
  CHECK(!t.active());

  // A newer toast replaces the older one, deadline included.
  CHECK(t.show("Ping A: 120 ms", 1000, 5000));
  CHECK(t.show("Ping B: 90 ms", 1500, 2000));
  CHECK(strcmp(t.text, "Ping B: 90 ms") == 0);
  CHECK(t.visible(3499));
  CHECK(!t.visible(3500));
  CHECK(t.expire(3500));
  CHECK(!t.active());

  // clear() also ends it immediately.
  CHECK(t.show("Fault", 0, 2500));
  t.clear();
  CHECK(!t.active());
  CHECK(!t.visible(1));

  // millis() wraps after ~49 days; the deadline arithmetic must survive the wrap.
  ToastState w;
  CHECK(w.show("wrap", 0xFFFFFF00u, 1000));   // until = 0x000002E8
  CHECK(w.visible(0xFFFFFFF0u));
  CHECK(!w.visible(0x000002E8u));
  CHECK(w.expire(0x000002E8u));
}

// --- MeshCore's encoded path length ---------------------------------------------------------------
static void testPathDecode() {
  // 1B, 1 hop: (1-1)<<6 | 1
  PathSpec p = decodeOutPathLen(0x01);
  CHECK(p.known);
  CHECK(p.hashSize == 1);
  CHECK(p.count == 1);
  CHECK(pathHopCount(0x01) == 1);
  char text[24];
  pathHopsText(text, sizeof(text), pathHopCount(0x01));
  CHECK(strcmp(text, "1 hop") == 0);

  // 1B, 3 hops
  p = decodeOutPathLen(0x03);
  CHECK(p.known && p.hashSize == 1 && p.count == 3);
  pathHopsText(text, sizeof(text), pathHopCount(0x03));
  CHECK(strcmp(text, "3 hops") == 0);

  // 2B, 1 hop: 0x41. This is the ERA-3 case Claude saw as "hops=32" (65 / 2, our old arithmetic);
  // the encoded byte itself must decode to one hop of two bytes.
  p = decodeOutPathLen(0x41);
  CHECK(p.known);
  CHECK(p.hashSize == 2);
  CHECK(p.count == 1);
  CHECK(pathHopCount(0x41) == 1);

  // 2B, 2 hops
  p = decodeOutPathLen(0x42);
  CHECK(p.known && p.hashSize == 2 && p.count == 2);

  // 3B, 2 hops: (2)<<6 | 2 = 0x82
  p = decodeOutPathLen(0x82);
  CHECK(p.known);
  CHECK(p.hashSize == 3);
  CHECK(p.count == 2);

  // Direct (0 hops) is known, not unknown.
  p = decodeOutPathLen(0x00);
  CHECK(p.known && p.count == 0);
  pathHopsText(text, sizeof(text), pathHopCount(0x00));
  CHECK(strcmp(text, "direct") == 0);

  // 0xFF is OUT_PATH_UNKNOWN, not "hash size 4, 63 hops".
  p = decodeOutPathLen(kOutPathUnknown);
  CHECK(!p.known);
  CHECK(pathHopCount(kOutPathUnknown) == -1);
  pathHopsText(text, sizeof(text), pathHopCount(kOutPathUnknown));
  CHECK(strcmp(text, "--") == 0);

  // Hash size 4 is reserved by MeshCore's Packet::isValidPathLen.
  CHECK(!decodeOutPathLen(0xC0).known);
  // count * size must fit MAX_PATH_SIZE (64): 40 two-byte hashes do not.
  CHECK(!decodeOutPathLen((uint8_t)((1 << 6) | 40)).known);
  // 63 one-byte hashes do fit.
  CHECK(decodeOutPathLen(63).known);
  CHECK(decodeOutPathLen(63).count == 63);
}

int main() {
  testDmRingSendReplyWrap();
  testSeqRing();
  testToastState();
  testPathDecode();
  if (g_fail != 0) {
    printf("%d check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("ok: DM ring send/reply/wrap, channel ring rule, toast state, path decoding\n");
  return 0;
}
