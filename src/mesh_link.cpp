// SPDX-License-Identifier: MIT
// OM Lite <-> MeshCore link, now a facade over upstream's companion code.
//
// The mesh itself is MeshCore's own examples/companion_radio (vendored unmodified in
// lib/meshcore_companion/): MyMesh owns the identity store, channel table, contact table,
// prefs, path hash mode and every phone command. This file only:
//   * gives it a board/radio/clock/sensors through src/target.*,
//   * runs the one-time migration from the old OM Lite storage,
//   * keeps the screens' API (mesh_link.h) working: channels, contacts, the message ring,
//     direct messages, path hash and node name,
//   * keeps the three local facts the UI needs that MyMesh does not model itself: message
//     timestamps, the heard-repeat count and the radio fault mark.
//
// Every object that touches hardware is created at global construction time because MyMesh.h
// declares `extern MyMesh the_mesh`; the old lazy-init guard is kept for the crash flag, but
// the objects themselves are now MeshCore's required globals.
#include "mesh_link.h"

#if defined(OM_ENABLE_MESH)

#include <Arduino.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <math.h>

#include <helpers/ArduinoHelpers.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>

// MyMesh.h comes FIRST among the MeshCore headers: it sets MAX_CONTACTS=100 before it pulls in
// BaseChatMesh.h, and BaseChatMesh sizes its contact table from that value. Everything else follows
// the build flag -D MAX_CONTACTS=100 (platformio.ini), because lib/meshcore/helpers/BaseChatMesh.cpp
// is compiled on its own and never sees MyMesh.h.
#include "MyMesh.h"
#include <helpers/BaseChatMesh.h>

#include "DataStore.h"
#include "audio.h"
#include "channels.h"
#include "crash_report.h"
#include "mesh_migration.h"
#include "mesh_path.h"
#include "mesh_text.h"
#include "seq_ring.h"
#include "radio_fault.h"
#include "target.h"
#include "timekeeper.h"
#include "ui/dm_delivery.h"
#include "ui/dm_ring.h"
#include "ui/new_node.h"
#include "ui/node_menu.h"

// The two upstream limits the rest of the file is sized from. A MeshCore upgrade that changes
// either stops the build here instead of silently cutting a message or a name.
static_assert(om::kMeshTextMax == MAX_TEXT_LEN, "mesh_text.h must follow BaseChatMesh.h MAX_TEXT_LEN");
static_assert(om::kMeshNameMax == sizeof(ContactInfo::name), "mesh_text.h must follow ContactInfo::name");
static_assert(om::kMeshPathMax == MAX_PATH_SIZE, "mesh_text.h must follow MeshCore.h MAX_PATH_SIZE");
static_assert(om::kMeshPubKey == PUB_KEY_SIZE, "mesh_text.h must follow MeshCore.h PUB_KEY_SIZE");
// EST-344: the path decoder's two upstream constants (Packet.h / ContactInfo.h).
static_assert(om::kPathBytesMax == MAX_PATH_SIZE, "mesh_path.h must follow MeshCore.h MAX_PATH_SIZE");
static_assert(om::kOutPathUnknown == OUT_PATH_UNKNOWN, "mesh_path.h must follow ContactInfo.h OUT_PATH_UNKNOWN");
// EST-342: BaseChatMesh::composeMsgPacket() puts a 4-byte timestamp and one attempt byte before the
// DM text; the composer budget (mesh_text.h) must keep counting exactly those bytes.
static_assert(om::kMeshDmFrameBytes == 5, "BaseChatMesh's direct text frame layout changed");
// EST-341: the global MyMesh was instantiated with BaseChatMesh's 32-contact default (15,312 B)
// while MyMesh.cpp wrote it as the 100-contact layout (28,096 B). `_store` was read from an
// uninitialised offset, the first savePrefs() called through a NULL DataStore and the boot
// panicked at [mc] 3 store + mesh begin. One define must reach every TU, libraries included.
static_assert(MAX_CONTACTS == 100,
              "MAX_CONTACTS must be 100 for every TU: -D MAX_CONTACTS=100 in platformio.ini "
              "(lib/meshcore is compiled without MyMesh.h)");

namespace om {
namespace {

// The MeshCore objects (StdRNG, tables, DataStore, MyMesh) are defined further down, because their
// constructors need the radio, the RTC and SPIFFS. A use that is compiled BEFORE that definition
// would bind to MyMesh.h's global `extern MyMesh the_mesh` instead of our file-local object and
// then fail to link, so the object is declared here first (EST-342).
extern MyMesh the_mesh;

char s_lastText[kMeshTextCap];
int s_lastChannel = -1;
bool s_ready = false;
bool s_prefsBegun = false;

// --- message ring --------------------------------------------------------------------------------
struct Msg {
  uint8_t ch;
  uint8_t mine;
  int8_t hops;     // packet path hops, -1 = local echo / unknown
  int8_t snr4;     // SNR x 4 (MeshCore's unit), 0 = unknown
  uint8_t pathHashSize;
  uint8_t pathCount;
  uint8_t repeats;   // own message: times we heard our packet repeated by others
  uint32_t ts;       // uptime seconds when stored (monotonic, clock-jump proof)
  uint32_t wall;     // system-clock seconds at that moment (0 = unknown)
  uint8_t wallReal;  // the clock was a real source then
  uint32_t seq;      // stable message id across ring shifts
  char name[kMeshNameCap];
  char text[kMeshTextCap];
  uint8_t path[MAX_PATH_SIZE];
};
constexpr int kMsgRing = 32;
Msg s_msgs[kMsgRing];
// Message `seq` lives in slot `seq % kMsgRing`; SeqRing is the ONE allocation path (EST-344). The
// old slotFor() was a second rule (newest-1 based) and the own-repeat counter assumed a third, so a
// repeat was looked up in the wrong slot. Reads and writes now share SeqRing::slotOf().
SeqRing<kMsgRing> s_msgRing;
uint8_t s_unread[8] = {0};
uint32_t s_storeSerial = 0;   // changes on every stored message (channel or DM)

// --- favourites (EST-342) ------------------------------------------------------------------------
// MeshCore's ContactInfo has a favourite FLAG but no public setter (addContact() allocates a new
// slot, it does not replace one), so the star is kept in our own NVS namespace "omfav" and matched
// by public key. It sorts the Nodes list and survives a reboot; neither the contact table nor
// /contacts3 is touched. 32 keys of 32 bytes = 1 KB of RAM, no flash writes until a star changes.
constexpr int kFavMax = 32;
uint8_t s_favKeys[kFavMax][PUB_KEY_SIZE];
int s_favCount = 0;
Preferences s_favPrefs;
bool s_favBegun = false;

bool isFavouriteKey(const uint8_t* key, bool* out) {
  if (out) *out = false;
  if (key == nullptr) return false;
  for (int i = 0; i < s_favCount; ++i) {
    if (memcmp(s_favKeys[i], key, PUB_KEY_SIZE) != 0) continue;
    if (out) *out = true;
    return true;
  }
  return false;   // a valid key, just not a favourite
}

void favouritesBegin() {
  if (s_favBegun) return;
  s_favPrefs.begin("omfav", false);
  s_favBegun = true;
  const int count = (int)s_favPrefs.getUChar("n", 0);
  s_favCount = 0;
  for (int i = 0; i < count && i < kFavMax; ++i) {
    char name[8];
    snprintf(name, sizeof(name), "f%d", i);
    if (s_favPrefs.getBytes(name, s_favKeys[s_favCount], PUB_KEY_SIZE) == PUB_KEY_SIZE) {
      ++s_favCount;
    }
  }
  Serial.printf("[mc] favourites: %d from NVS\n", s_favCount);
}

void favouritesSave() {
  s_favPrefs.putUChar("n", (uint8_t)s_favCount);
  for (int i = 0; i < s_favCount; ++i) {
    char name[8];
    snprintf(name, sizeof(name), "f%d", i);
    s_favPrefs.putBytes(name, s_favKeys[i], PUB_KEY_SIZE);
  }
}

bool favouriteSet(const uint8_t* key, bool on) {
  if (key == nullptr) return false;
  int at = -1;
  for (int i = 0; i < s_favCount; ++i) {
    if (memcmp(s_favKeys[i], key, PUB_KEY_SIZE) == 0) {
      at = i;
      break;
    }
  }
  if (on && at < 0) {
    if (s_favCount >= kFavMax) return false;
    memcpy(s_favKeys[s_favCount++], key, PUB_KEY_SIZE);
  } else if (!on && at >= 0) {
    for (int i = at; i < s_favCount - 1; ++i) memcpy(s_favKeys[i], s_favKeys[i + 1], PUB_KEY_SIZE);
    --s_favCount;
  } else {
    return true;   // already in the wanted state
  }
  favouritesSave();
  return true;
}

// Index 0 is the oldest message still in the ring; the slot rule is SeqRing's, not a second copy.
int slotFor(int idx) {
  return SeqRing<kMsgRing>::slotOf(s_msgRing.firstSeq() + (uint32_t)idx);
}

// --- per-contact timestamps ----------------------------------------------------------------------
struct ContactStamp {
  uint8_t key[PUB_KEY_SIZE];
  uint32_t wall;
  uint32_t uptime;
  bool real;
  uint32_t used;
};
constexpr int kContactStamps = 40;
ContactStamp s_contactStamps[kContactStamps] = {};
uint32_t s_contactStampClock = 0;

ContactStamp* findContactStamp(const uint8_t* key) {
  if (key == nullptr) return nullptr;
  for (int i = 0; i < kContactStamps; ++i) {
    if (s_contactStamps[i].used != 0 && memcmp(s_contactStamps[i].key, key, PUB_KEY_SIZE) == 0) {
      return &s_contactStamps[i];
    }
  }
  return nullptr;
}

void markContactStamp(const ContactInfo& ci) {
  ContactStamp* slot = findContactStamp(ci.id.pub_key);
  if (slot == nullptr) {
    for (int i = 0; i < kContactStamps; ++i) {
      if (s_contactStamps[i].used == 0) {
        slot = &s_contactStamps[i];
        break;
      }
      if (slot == nullptr || s_contactStamps[i].used < slot->used) slot = &s_contactStamps[i];
    }
  }
  if (slot == nullptr) return;
  memcpy(slot->key, ci.id.pub_key, PUB_KEY_SIZE);
  slot->wall = ci.lastmod;
  slot->uptime = millis() / 1000;
  slot->real = timeKeeper().realSinceBoot();
  slot->used = ++s_contactStampClock;
}

// --- own-packet repeat counting ------------------------------------------------------------------
// SimpleMeshTables with one extra job: while our own send is in flight, remember its packet hash;
// when a copy comes back from a repeater, wasSeen() is the first thing MeshCore calls, so the
// repeat is counted on the local echo that carries the same message id. Public virtual API only.
class OmMeshTables : public SimpleMeshTables {
 public:
  void beginOwnSend(uint32_t seq) {
    s_pendingSeq = seq;
    s_pendingSlot = -1;
  }
  void endOwnSend(bool stored) {
    if (!stored && s_pendingSlot >= 0) s_own[s_pendingSlot].seq = 0;
    s_pendingSeq = 0;
    s_pendingSlot = -1;
  }

  void markSeen(const mesh::Packet* pkt) override {
    if (s_pendingSeq != 0 && pkt != nullptr) {
      uint8_t h[MAX_HASH_SIZE];
      pkt->calculatePacketHash(h);
      memcpy(s_own[s_ownNext].hash, h, MAX_HASH_SIZE);
      s_own[s_ownNext].seq = s_pendingSeq;
      s_pendingSlot = s_ownNext;
      s_ownNext = (s_ownNext + 1) % kOwnSlots;
    }
    SimpleMeshTables::markSeen(pkt);
  }

  bool wasSeen(const mesh::Packet* pkt) override {
    if (pkt != nullptr) {
      uint8_t h[MAX_HASH_SIZE];
      pkt->calculatePacketHash(h);
      for (int i = 0; i < kOwnSlots; ++i) {
        if (s_own[i].seq == 0 || memcmp(s_own[i].hash, h, MAX_HASH_SIZE) != 0) continue;
        const int slot = SeqRing<kMsgRing>::slotOf(s_own[i].seq);
        if (s_msgs[slot].seq == s_own[i].seq && s_msgs[slot].repeats < 255) {
          ++s_msgs[slot].repeats;
        }
        break;
      }
    }
    return SimpleMeshTables::wasSeen(pkt);
  }

 private:
  static constexpr int kOwnSlots = 8;
  struct Own {
    uint8_t hash[MAX_HASH_SIZE];
    uint32_t seq;
  };
  Own s_own[kOwnSlots] = {};
  int s_ownNext = 0;
  uint32_t s_pendingSeq = 0;
  int s_pendingSlot = -1;
};

// --- direct messages -----------------------------------------------------------------------------
// The store itself (threads + the 32-message ring + the ONE allocation path) lives in ui/dm_ring.h,
// plain C++ and host-tested: EST-344's overwrite came from storeDm() allocating one slot and writing
// another, which no screen-level test could have caught. This file only adapts it to MeshCore: a
// received packet fills the allocated message, sendMessage()/ACK update it by sequence (EST-342).
using ui::DmMsg;   // the ring's message layout (ui/dm_ring.h), also the facade's MeshDm
om::ui::DmRing s_dmRing;

// The sent DMs waiting for an ACK. The companion's own timeout (est_timeout from sendMessage) is
// what decides "not confirmed", and the same 4-byte hash proves "delivered".
om::ui::DmDeliveryTable s_dmDelivery;

int ensureDmThread(const ContactInfo& from) {
  return s_dmRing.ensureThread(from.id.pub_key, from.name);
}

// The contact behind a thread, so a send can use MeshCore's own ContactInfo.
bool contactForThread(int thread, ContactInfo* out) {
  const om::ui::DmThread* t = s_dmRing.thread(thread);
  if (t == nullptr || !t->used || out == nullptr) return false;
  const int total = the_mesh.getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!the_mesh.getContactByIdx((uint32_t)idx, ci)) continue;
    if (ci.type == ADV_TYPE_NONE || ci.name[0] == 0) continue;
    if (memcmp(ci.id.pub_key, t->key, PUB_KEY_SIZE) != 0) continue;
    *out = ci;
    return true;
  }
  return false;
}

void setDmState(uint32_t seq, om::ui::DmState state) {
  if (!s_dmRing.setState(seq, (uint8_t)state)) return;
  Serial.printf("[mc] DM seq %lu: %s\n", (unsigned long)seq, om::ui::dmStateToken(state));
}

void storeDm(const ContactInfo& from, mesh::Packet* pkt, const char* text) {
  const int thread = ensureDmThread(from);
  // THE allocation, shared with meshDmSend(): seq = newest + 1, slot = seq % ring, committed here.
  DmMsg& m = *s_dmRing.alloc((uint8_t)(thread < 0 ? om::ui::kDmNoThread : thread), false);
  m.state = (uint8_t)om::ui::DmState::Delivered;   // received: nothing to confirm
  m.ts = millis() / 1000;
  m.wall = timeKeeper().now();
  m.wallReal = timeKeeper().realSinceBoot() ? 1 : 0;
  m.hops = pkt ? (int8_t)pkt->getPathHashCount() : -1;
  m.snr4 = pkt ? (int8_t)lroundf(pkt->getSNR() * 4.0f) : 0;
  {
    uint8_t hs = pkt ? pkt->getPathHashSize() : 0;
    uint8_t hc = pkt ? pkt->getPathHashCount() : 0;
    if (pkt == nullptr || hs < 1 || hs > 3 || (int)hc * (int)hs > MAX_PATH_SIZE) {
      hs = 0;
      hc = 0;
    }
    m.pathHashSize = hs;
    m.pathCount = hc;
    if (hc > 0) memcpy(m.path, pkt->path, (size_t)hc * hs);
  }
  snprintf(m.name, sizeof(m.name), "%s", from.name);
  snprintf(m.text, sizeof(m.text), "%s", text ? text : "");
  s_dmRing.bumpRecvSerial();
  if (thread >= 0) s_dmRing.noteReceived(thread, from.name, m.wall);
  Serial.printf("[mc] DM from %s: %s (hops %d snr %.2f)\n", from.name, text ? text : "",
                (int)m.hops, (double)m.snr4 / 4.0);
}

// Resend one DM with the next attempt (the companion app's retry): the same text and the same wire
// timestamp, so the recipient sees one message, and a new frame hash, so the ACK matches that frame.
bool resendDm(uint32_t seq, uint8_t attempt) {
  DmMsg* m = s_dmRing.find(seq);
  if (m == nullptr) return false;
  ContactInfo ci;
  if (!contactForThread(m->thread, &ci)) {
    setDmState(seq, om::ui::DmState::Failed);
    s_dmDelivery.drop(seq);
    return false;
  }
  uint32_t ack = 0;
  uint32_t est = 0;
  const int rc = the_mesh.sendMessage(ci, m->wireTs, attempt, m->text, ack, est);
  if (rc == MSG_SEND_FAILED) {
    setDmState(seq, om::ui::DmState::Failed);
    s_dmDelivery.drop(seq);
    return false;
  }
  s_dmDelivery.rearm(seq, ack, est, millis());
  Serial.printf("[mc] DM retry seq %lu attempt %u -> %s (%lu ms)\n", (unsigned long)seq,
                (unsigned)attempt, rc == MSG_SEND_SENT_FLOOD ? "flood" : "direct",
                (unsigned long)est);
  return true;
}

// --- MeshCore objects ----------------------------------------------------------------------------
StdRNG fast_rng;
OmMeshTables tables;
DataStore store(SPIFFS, rtc_clock);
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);

// New contacts are only lazily written by MyMesh when they change later, so schedule one save for
// a new advert too. Updates are already handled by MyMesh's own dirty timer.
bool s_newContact = false;
uint32_t s_newContactAt = 0;
constexpr uint32_t kNewContactSaveMs = 5000;
Preferences s_netPrefs;

// --- new-node notices ----------------------------------------------------------------------------
// One event per advert that ADDS a contact that was not in the table. Upstream's is_new flag cannot
// be used: MeshCore v1.18.0 never sets it true on a normal add (BaseChatMesh.cpp, verified). The
// baseline is a hash set seeded from the table at begin; see ui/new_node.h for the pure rule.
om::ui::KnownKeySet s_knownKeys;

bool contactInTable(const uint8_t* key) {
  const int total = the_mesh.getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!the_mesh.getContactByIdx((uint32_t)idx, ci)) continue;
    if (ci.type == ADV_TYPE_NONE || ci.name[0] == 0) continue;
    if (memcmp(ci.id.pub_key, key, PUB_KEY_SIZE) == 0) return true;
  }
  return false;
}

void seedKnownKeys() {
  s_knownKeys.clear();
  const int total = the_mesh.getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!the_mesh.getContactByIdx((uint32_t)idx, ci)) continue;
    if (ci.type == ADV_TYPE_NONE || ci.name[0] == 0) continue;
    s_knownKeys.add(om::ui::KnownKeySet::keyHash(ci.id.pub_key, PUB_KEY_SIZE));
  }
  Serial.printf("[mc] known contacts: %d for the new-node notice\n", s_knownKeys.count);
}

// A small queue, because two adverts can land between two draws; the shell takes them all and shows
// the last as the toast.
struct NewNodeEvent {
  char name[kMeshNameCap];
  uint8_t type;
};
constexpr int kNewNodeEvents = 4;
NewNodeEvent s_newNodeEvents[kNewNodeEvents];
int s_newNodeHead = 0;
int s_newNodeCount = 0;
uint32_t s_newNodeSerial = 0;

void queueNewNode(const ContactInfo& c) {
  NewNodeEvent& e = s_newNodeEvents[(s_newNodeHead + s_newNodeCount) % kNewNodeEvents];
  snprintf(e.name, sizeof(e.name), "%s", c.name);
  e.type = c.type;
  if (s_newNodeCount < kNewNodeEvents) ++s_newNodeCount;
  else s_newNodeHead = (s_newNodeHead + 1) % kNewNodeEvents;   // drop the oldest
  ++s_newNodeSerial;
  Serial.printf("[mc] new node: %s (%u)\n", e.name, (unsigned)e.type);
}

// --- ping (status request) ----------------------------------------------------------------------
MeshPingState s_pingState = MeshPingState::Idle;
uint32_t s_pingSentMs = 0;
uint32_t s_pingDeadlineMs = 0;
uint32_t s_pingRttMs = 0;
uint8_t s_pingKey[PUB_KEY_SIZE] = {};
char s_pingName[kMeshNameCap] = "";

// --- trace --------------------------------------------------------------------------------------
MeshTrace s_trace;
uint32_t s_traceSerial = 0;
uint32_t s_traceSentMs = 0;
bool s_tracePending = false;
uint32_t s_traceDeadlineMs = 0;

class OmListener : public MyMesh::Listener {
 public:
  void onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type,
                     uint32_t, const char* text) override {
    if (txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_SIGNED_PLAIN) {
      storeDm(from, pkt, text);
    }
  }

  // Every ACK MeshCore receives passes through here (MyMesh::processAck calls it before matching
  // the phone's own table), so a DM's 4-byte hash proves delivery even with no phone attached.
  void onACKRecv(uint32_t ack_crc) override {
    uint32_t seq = 0;
    uint32_t rtt = 0;
    if (!s_dmDelivery.ack(ack_crc, millis(), &seq, &rtt)) return;
    // The ACK update goes through the ring too: same seq -> slot rule as the list (EST-344).
    s_dmRing.markDelivered(seq, timeKeeper().now(), timeKeeper().realSinceBoot());
    Serial.printf("[mc] DM seq %lu delivered in %lu ms\n", (unsigned long)seq, (unsigned long)rtt);
  }

  // The status reply (the ping). sendRequest() does not set MyMesh's own pending_status, so an
  // unmatched response arrives here with its tag; the sender's key is what we match on.
  bool onUnhandledResponse(const ContactInfo& from, uint32_t tag_, const uint8_t* data,
                           uint8_t len) override {
    (void)tag_;
    (void)data;
    (void)len;
    if (s_pingState == MeshPingState::Sent &&
        memcmp(from.id.pub_key, s_pingKey, PUB_KEY_SIZE) == 0) {
      s_pingState = MeshPingState::Replied;
      s_pingRttMs = millis() - s_pingSentMs;
      snprintf(s_pingName, sizeof(s_pingName), "%s", from.name);
      Serial.printf("[mc] ping %s: reply in %lu ms\n", s_pingName, (unsigned long)s_pingRttMs);
      return true;   // handled here; no need for a phone frame
    }
    return false;
  }

  // The trace came back: one SNR per hop, in the order the packet walked them, plus the final SNR
  // at our receiver. Names come from the repeater table (meshPathHopName).
  void onTraceRecv(mesh::Packet* pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                   const uint8_t* path_snrs, const uint8_t* path_hashes,
                   uint8_t path_len) override {
    (void)tag;
    (void)auth_code;
    const uint8_t path_sz = (uint8_t)(flags & 0x03);
    const uint8_t hashBytes = (uint8_t)(1u << path_sz);
    int hops = hashBytes ? (int)path_len / (int)hashBytes : 0;
    if (hops > 8) hops = 8;
    s_trace.count = hops;
    for (int i = 0; i < hops; ++i) {
      MeshTraceHop& h = s_trace.hops[i];
      const uint8_t* hash = path_hashes + (size_t)i * hashBytes;
      char name[kMeshNameCap];
      meshPathHopName(hash, hashBytes, name, sizeof(name));
      h.known = name[0] != 0;
      if (h.known) {
        snprintf(h.name, sizeof(h.name), "%s", name);
      } else {
        size_t n = 0;
        for (uint8_t b = 0; b < hashBytes && n + 2 < sizeof(h.name); ++b) {
          n += (size_t)snprintf(h.name + n, sizeof(h.name) - n, "%02x", hash[b]);
        }
      }
      h.snr4 = (int8_t)path_snrs[i];
    }
    s_trace.finalSnr4 = pkt ? (int8_t)lroundf(pkt->getSNR() * 4.0f) : 0;
    s_trace.rttMs = millis() - s_traceSentMs;
    s_trace.haveRtt = true;
    s_tracePending = false;
    ++s_traceSerial;
    Serial.printf("[mc] trace back: %d hop(s), rtt %lu ms\n", hops,
                  (unsigned long)s_trace.rttMs);
  }

  void onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel_details,
                            const char* text) override {
    int channelIdx = -1;
    for (int i = 0; i < mcChannelCount(); ++i) {
      if (mcChannel(i).hash == channel_details.channel.hash[0]) {
        channelIdx = i;
        break;
      }
    }
    s_lastChannel = channelIdx;
    if (channelIdx < 0) {
      Serial.printf("[mc] message for an unknown channel, ignored: %s\n", text ? text : "");
      return;
    }
    // MeshCore can dispatch one packet to more than one matching slot; drop an immediate repeat
    // (same channel, same text, within 2 s) so the ring and the unread count stay honest.
    {
      static int lastCh = -1;
      static uint32_t lastTs = 0;
      static char lastText[kMeshTextCap];
      const uint32_t now = millis() / 1000;
      if (channelIdx == lastCh && (now - lastTs) <= 2 &&
          strncmp(lastText, text ? text : "", kMeshTextMax) == 0) {
        Serial.println("[mc] duplicate dispatch, ignored");
        return;
      }
      lastCh = channelIdx;
      lastTs = now;
      snprintf(lastText, sizeof(lastText), "%s", text ? text : "");
    }

    snprintf(s_lastText, sizeof(s_lastText), "%s", text ? text : "");
    // The one allocation: seq = newest + 1, slot = seq % ring (EST-344).
    const uint32_t seq = s_msgRing.alloc();
    Msg& m = s_msgs[SeqRing<kMsgRing>::slotOf(seq)];
    memset(&m, 0, sizeof(m));
    m.seq = seq;
    m.ch = (uint8_t)channelIdx;
    m.mine = 0;
    m.ts = millis() / 1000;
    m.wall = timeKeeper().now();
    m.wallReal = timeKeeper().realSinceBoot() ? 1 : 0;
    m.hops = pkt ? (int8_t)pkt->getPathHashCount() : -1;
    m.snr4 = pkt ? (int8_t)lroundf(pkt->getSNR() * 4.0f) : 0;
    {
      uint8_t hs = pkt ? pkt->getPathHashSize() : 0;
      uint8_t hc = pkt ? pkt->getPathHashCount() : 0;
      if (pkt == nullptr || hs < 1 || hs > 3 || (int)hc * (int)hs > MAX_PATH_SIZE) {
        hs = 0;
        hc = 0;
      }
      m.pathHashSize = hs;
      m.pathCount = hc;
      if (hc > 0) memcpy(m.path, pkt->path, (size_t)hc * hs);
    }
    const char* body = text ? text : "";
    const char* sep = strstr(body, ": ");
    if (sep && (size_t)(sep - body) < sizeof(m.name)) {
      const size_t n = (size_t)(sep - body);
      memcpy(m.name, body, n);
      m.name[n] = 0;
      body = sep + 2;
    } else {
      snprintf(m.name, sizeof(m.name), "?");
    }
    snprintf(m.text, sizeof(m.text), "%s", body);
    ++s_storeSerial;
    if (channelIdx < 8) ++s_unread[channelIdx];
    const bool muted = mcChannelMuted(channelIdx);
    Serial.printf("[mc] channel %d: %s (hops %d snr %.2f)%s\n", channelIdx, s_lastText,
                  (int)m.hops, (double)m.snr4 / 4.0, muted ? " [muted]" : "");
    // A muted channel still stores and counts unread; mute is about the chime only.
    if (!muted) {
      audioTone(1318, 70);
      audioTone(1760, 90);
    }
  }

  void onQueueSizeChanged(int msgcount) override { (void)msgcount; }

  void onDiscoveredContact(ContactInfo& contact, bool, uint8_t, const uint8_t*) override {
    markContactStamp(contact);
    // EST-342: a notice only when the advert really ADDED this contact to the table. The key was
    // absent from the baseline AND is in the table now; an advert of a known contact only refreshes
    // its stamp. (is_new is ignored: v1.18.0 never sets it true on a normal add.)
    const uint32_t h = om::ui::KnownKeySet::keyHash(contact.id.pub_key, PUB_KEY_SIZE);
    if (!s_knownKeys.contains(h) && contactInTable(contact.id.pub_key)) {
      s_newContact = true;
      s_newContactAt = millis();
      s_knownKeys.add(h);
      queueNewNode(contact);
    }
  }

};

OmListener s_listener;

// --- one-time migration --------------------------------------------------------------------------
// The `mig` flag may only be set after every output below has been written AND read back. A failed
// step logs why, leaves the flag unset and is retried on the next boot.
constexpr const char* kOldContactsPath = "/contacts";
constexpr const char* kNewContactsPath = "/contacts3";
constexpr const char* kContactsTmpPath = "/contacts3.tmp";
constexpr const char* kPrefsPath = "/prefs.json";
constexpr const char* kChannelsPath = "/channels2";

enum class MigrateResult : uint8_t { Ok, Skipped, Failed };

// Reopens path and compares it with the image just written: size first, then every byte. This is
// the check EST-340 lacked: it asked File::size() before flush/close, ESP-IDF's SPIFFS stat then
// reported 0 for a file whose data was still in the stdio buffer, and the log said "0 bytes".
bool readBackExact(const char* path, const uint8_t* expected, size_t expectedLen) {
  if (expected == nullptr && expectedLen != 0) return false;
  if (!SPIFFS.exists(path)) return false;
  File f = SPIFFS.open(path, "r");
  if (!f) return false;
  bool ok = (size_t)f.size() == expectedLen;
  uint8_t chunk[kLegacyContactRecordSize];
  size_t off = 0;
  while (ok && off < expectedLen) {
    const size_t want = (expectedLen - off) < sizeof(chunk) ? (expectedLen - off) : sizeof(chunk);
    if ((size_t)f.read(chunk, want) != want || memcmp(chunk, expected + off, want) != 0) {
      ok = false;
    } else {
      off += want;
    }
  }
  f.close();
  return ok;
}

// /contacts -> /contacts3. Idempotent: a structurally valid /contacts3 is never rewritten, because
// after the first migration MeshCore owns it and updates it from adverts. The bad states are a size
// that is not a whole number of 152-byte records (a torn write) and 0 bytes while /contacts still
// holds contacts (exactly what the EST-340 crash could have left behind).
MigrateResult copyLegacyContactsIfNeeded() {
  if (!SPIFFS.exists(kOldContactsPath)) {
    Serial.printf("[mc] migrate /contacts: no old file (/contacts3 %s)\n",
                  SPIFFS.exists(kNewContactsPath) ? "kept" : "absent");
    return MigrateResult::Skipped;
  }
  File in = SPIFFS.open(kOldContactsPath, "r");
  if (!in) {
    Serial.println("[mc] migrate /contacts: open failed, left for the next boot");
    return MigrateResult::Failed;
  }
  const size_t len = (size_t)in.size();
  if (len < kLegacyContactHeaderSize || len > kLegacyContactMaxFileSize) {
    Serial.printf("[mc] migrate /contacts: bad size %u, not migrated\n", (unsigned)len);
    in.close();
    return MigrateResult::Failed;
  }
  static uint8_t s_legacyBuf[kLegacyContactMaxFileSize];
  const size_t got = (size_t)in.read(s_legacyBuf, len);
  in.close();
  if (got != len) {
    Serial.printf("[mc] migrate /contacts: short read %u of %u, not migrated\n", (unsigned)got,
                  (unsigned)len);
    return MigrateResult::Failed;
  }

  static LegacyContact recs[kLegacyMaxContactRecords];
  const int n = decodeLegacyContacts(s_legacyBuf, len, recs, kLegacyMaxContactRecords);
  if (n < 0) {
    Serial.println("[mc] migrate /contacts: corrupt (magic/version/CRC), not migrated");
    return MigrateResult::Failed;
  }
  if (n == 0) {
    Serial.println("[mc] migrate /contacts: 0 contacts, no /contacts3 needed");
    return MigrateResult::Ok;
  }

  static uint8_t s_image[kLegacyMaxContactRecords * kLegacyContactRecordSize];
  size_t imageLen = 0;
  if (!encodeContacts3(recs, n, s_image, sizeof(s_image), &imageLen)) {
    Serial.println("[mc] migrate /contacts: cannot build the /contacts3 image");
    return MigrateResult::Failed;
  }

  if (SPIFFS.exists(kNewContactsPath)) {
    size_t existing = 0;
    File cur = SPIFFS.open(kNewContactsPath, "r");
    if (cur) {
      existing = (size_t)cur.size();
      cur.close();
    }
    const bool wholeRecords = (existing % kLegacyContactRecordSize) == 0;
    const bool emptyButData = (existing == 0);
    if (wholeRecords && !emptyButData) {
      Serial.printf("[mc] migrate /contacts: /contacts3 already valid (%u B, %d in /contacts), kept\n",
                    (unsigned)existing, n);
      return MigrateResult::Ok;
    }
    Serial.printf("[mc] migrate /contacts: /contacts3 not usable (%u B, %d decoded), rebuilding\n",
                  (unsigned)existing, n);
  }

  File out = SPIFFS.open(kContactsTmpPath, "w");
  if (!out) {
    Serial.println("[mc] migrate /contacts: cannot open /contacts3.tmp");
    return MigrateResult::Failed;
  }
  const size_t wrote = (size_t)out.write(s_image, imageLen);
  out.flush();   // push the stdio buffer into SPIFFS before anyone asks for the file's size
  out.close();
  if (wrote != imageLen) {
    SPIFFS.remove(kContactsTmpPath);
    Serial.printf("[mc] migrate /contacts: short write %u of %u, not migrated\n", (unsigned)wrote,
                  (unsigned)imageLen);
    return MigrateResult::Failed;
  }
  if (!readBackExact(kContactsTmpPath, s_image, imageLen)) {
    SPIFFS.remove(kContactsTmpPath);
    Serial.println("[mc] migrate /contacts: /contacts3.tmp did not read back, not migrated");
    return MigrateResult::Failed;
  }
  if (SPIFFS.exists(kNewContactsPath)) SPIFFS.remove(kNewContactsPath);
  if (!SPIFFS.rename(kContactsTmpPath, kNewContactsPath)) {
    SPIFFS.remove(kContactsTmpPath);
    Serial.println("[mc] migrate /contacts: rename failed, not migrated");
    return MigrateResult::Failed;
  }
  if (!readBackExact(kNewContactsPath, s_image, imageLen)) {
    Serial.println("[mc] migrate /contacts: /contacts3 did not read back after rename");
    return MigrateResult::Failed;
  }
  // /contacts stays in place for one release, so a rollback still finds it.
  Serial.printf("[mc] migrate /contacts -> /contacts3: %d contacts, %u bytes written and read back "
                "(old file kept)\n",
                n, (unsigned)imageLen);
  return MigrateResult::Ok;
}

void pushChannelsFromNvs(bool save) {
  const int count = mcChannelCount();
  for (int i = 0; i < kMaxChannels; ++i) {
    ChannelDetails cd;
    memset(&cd, 0, sizeof(cd));
    if (i < count) {
      snprintf(cd.name, sizeof(cd.name), "%s", mcChannelName(i));
      memcpy(cd.channel.secret, mcChannel(i).secret, 16);
    }
    the_mesh.setChannel(i, cd);
  }
  if (save) store.saveChannels(&the_mesh);
  Serial.printf("[mc] channels: %d from NVS%s\n", count, save ? " (saved to /channels2)" : "");
}

// /prefs.json exists, is not empty and parses with MeshCore's own reader. A scratch NodePrefs, so
// the live one the mesh already loaded is never disturbed by the check.
bool prefsFileParses() {
  if (!SPIFFS.exists(kPrefsPath)) return false;
  File f = SPIFFS.open(kPrefsPath, "r");
  if (!f) return false;
  const size_t size = (size_t)f.size();
  static NodePrefs s_scratch;   // NodePrefs is large: keep it out of the loop task's stack
  const bool parsed = size > 0 && s_scratch.loadSerial(f);
  f.close();
  return parsed;
}

// /channels2 holds exactly kMaxChannels 68-byte entries and every name/secret matches the channel
// table that was just written. That is the read-back for the channel step.
bool channelsFileMatchesMesh() {
  if (!SPIFFS.exists(kChannelsPath)) return false;
  File f = SPIFFS.open(kChannelsPath, "r");
  if (!f) return false;
  bool ok = channelsFileComplete((size_t)f.size(), kMaxChannels);
  for (int i = 0; ok && i < kMaxChannels; ++i) {
    uint8_t unused[4];
    char name[sizeof(ChannelDetails::name)];
    uint8_t secret[sizeof(ChannelDetails::channel.secret)];
    ChannelDetails want;
    if (f.read(unused, sizeof(unused)) != sizeof(unused) ||
        f.read((uint8_t*)name, sizeof(name)) != sizeof(name) ||
        f.read(secret, sizeof(secret)) != sizeof(secret) || !the_mesh.getChannel(i, want) ||
        memcmp(name, want.name, sizeof(name)) != 0 ||
        memcmp(secret, want.channel.secret, sizeof(secret)) != 0) {
      ok = false;
    }
  }
  f.close();
  return ok;
}

void logFileSize(const char* path) {
  if (!SPIFFS.exists(path)) {
    Serial.printf("[mc] store: %s missing\n", path);
    return;
  }
  File f = SPIFFS.open(path, "r");
  if (!f) {
    Serial.printf("[mc] store: %s unreadable\n", path);
    return;
  }
  Serial.printf("[mc] store: %s %u B\n", path, (unsigned)f.size());
  f.close();
}

}  // namespace

// ------------------------------------------------------------------------------------------------
bool meshLinkReady() { return s_ready; }
const char* meshLinkLastMessage() { return s_lastText; }
int meshLinkLastChannel() { return s_lastChannel; }
const char* meshNodeName() { return the_mesh.getNodeName(); }

uint8_t meshPathHashBytes() {
  const uint8_t mode = the_mesh.getNodePrefs()->path_hash_mode;
  return (uint8_t)(mode > 2 ? 3 : mode + 1);
}

const char* meshPathHashLabel() {
  static char label[4];
  snprintf(label, sizeof(label), "%uB", (unsigned)meshPathHashBytes());
  return label;
}

bool meshPathHashSet(uint8_t bytes) {
  if (bytes < 1 || bytes > 3) return false;
  the_mesh.getNodePrefs()->path_hash_mode = (uint8_t)(bytes - 1);
  the_mesh.savePrefs();
  if (s_prefsBegun) s_netPrefs.putUChar("phs", bytes);   // old key kept for rollback
  Serial.printf("[mc] path hash set to %s (saved in prefs.json)\n", meshPathHashLabel());
  return true;
}

bool meshLinkBegin() {
  if (s_ready) return true;

  s_netPrefs.begin("mcnet", false);
  s_prefsBegun = true;
  favouritesBegin();   // the Nodes stars, from our own NVS namespace (never the contact file)
  crashCheckpoint("[mc] 0 guard");
  // A newly installed firmware gets a fresh start: the crash guard belongs to the image that
  // crashed, and a parked guard is otherwise permanent (S494 rollback: the old image stayed parked
  // too, because `crashed`/`fails` live in NVS and nothing cleared them). A new image has a new
  // __DATE__ " " __TIME__, so its first boot clears the guard; a reboot of the same image keeps it.
  {
    const char* build = __DATE__ " " __TIME__;
    if (s_netPrefs.getString("gbuild", "") != build) {
      s_netPrefs.putBool("crashed", false);
      s_netPrefs.putUChar("fails", 0);
      s_netPrefs.putString("gbuild", build);
      Serial.printf("[mc] new firmware image (%s): crash guard reset\n", build);
    }
  }
  if (s_netPrefs.getBool("crashed", false)) {
    const uint8_t fails = (uint8_t)(s_netPrefs.getUChar("fails", 0) + 1);
    s_netPrefs.putUChar("fails", fails);
    Serial.printf("[mc] previous init crashed (fail %u/3)\n", fails);
    if (fails < 3) {
      s_netPrefs.putBool("crashed", false);
      delay(1500);
      ESP.restart();
    }
    s_netPrefs.putBool("crashed", true);   // parked: stays up for OTA
    return false;
  }
  s_netPrefs.putBool("crashed", true);   // cleared only after a full successful init

  crashCheckpoint("[mc] 1 spiffs");
  Serial.println("[mc] 1 spiffs");
  if (!SPIFFS.begin(true)) {
    Serial.println("[mc] SPIFFS mount failed");
    return false;
  }

  // The old identity file is MeshCore's own IdentityStore format at the same path. Read it
  // BEFORE begin() so the boot log can prove the key did not change.
  crashCheckpoint("[mc] 1b identity");
  mesh::LocalIdentity oldIdentity;
  bool hadOldIdentity = false;
  {
    IdentityStore ids(SPIFFS, "/identity");
    ids.begin();
    hadOldIdentity = ids.load("_main", oldIdentity);
  }

  // /contacts (EST-329) -> /contacts3 must happen before begin() loads contacts. The result is part
  // of the `mig` gate below: a failed contact migration must not mark the move complete.
  crashCheckpoint("[mc] 1c contacts");
  const MigrateResult contactsMig = copyLegacyContactsIfNeeded();
  const bool hadChannelsFile = SPIFFS.exists(kChannelsPath);

  crashCheckpoint("[mc] 2 radio");
  Serial.println("[mc] 2 radio");
  if (!radio_init()) {
    Serial.println("[mc] radio init failed");
    if (radioFaultState().set(RadioFault::Radio)) {
      Serial.printf("[radio] fault: %s\n", radioFaultToken(RadioFault::Radio));
    }
    return false;
  }
  fast_rng.begin(radio_get_rng_seed());

  crashCheckpoint("[mc] 3 store + mesh begin");
  Serial.println("[mc] 3 store + mesh begin");
  crashCheckpoint("[mc] 3a store.begin");
  store.begin();
  // EST-341 crash window: the_mesh is a global whose layout must agree with MyMesh.cpp (see the
  // MAX_CONTACTS static_assert at the top of this file).
  crashCheckpoint("[mc] 3b the_mesh.begin");
  the_mesh.begin();
  seedKnownKeys();   // the new-node baseline: contacts loaded from /contacts3 are not "new"
  crashCheckpoint("[mc] 3c interface");
  target_mesh_begun = true;   // from here on setCurrentTime() is the phone source
  the_mesh.setListener(&s_listener);
  the_mesh.setBLEPin(0);      // Bluetooth stays OFF in this ticket
  the_mesh.startInterface(null_serial);

  // Identity proof for the flash check: exactly `[mc] identity <first 8 bytes hex>`.
  {
    char hex[17];
    identityPrefixHex(the_mesh.self_id.pub_key, hex, sizeof(hex));
    Serial.printf("[mc] identity %s\n", hex);
    if (hadOldIdentity) {
      Serial.printf("[mc] identity %s\n",
                    identityPreserved(oldIdentity.pub_key, the_mesh.self_id.pub_key)
                        ? "same as before"
                        : "CHANGED");
    } else {
      Serial.println("[mc] identity: no previous /identity/_main (new node)");
    }
  }

  // One-time migration: node name, path hash mode, channels. Old NVS keys are left untouched for
  // a rollback; the old /contacts file likewise stays next to /contacts3 for one release.
  // `mig` is set ONLY when every output below has been written and read back; a failure retries on
  // the next boot (EST-341).
  const bool migrated = s_netPrefs.getBool("mig", false);
  bool migrateOk = true;

  crashCheckpoint("[mc] 4 prefs");
  // A missing/empty/unparsable file is rewritten (retry), not just reported: otherwise one bad boot
  // would leave the node without preferences forever. The scratch parse in prefsFileParses() does
  // not disturb the prefs the mesh already loaded.
  const bool prefsOk = prefsFileParses();
  if (!migrated || !prefsOk) {
    NodePrefs* prefs = the_mesh.getNodePrefs();
    snprintf(prefs->node_name, sizeof(prefs->node_name), "%s", kMeshOwnName);
    const uint8_t phs = s_netPrefs.getUChar("phs", 2);
    prefs->path_hash_mode = pathHashModeFromLegacy(phs);
    the_mesh.savePrefs();
    if (!prefsFileParses()) {
      Serial.println("[mc] migration: /prefs.json missing, empty or did not parse, mig not set");
      migrateOk = false;
    } else {
      Serial.printf("[mc] migration: name \"%s\", path hash %uB (NVS phs=%u), prefs read back\n",
                    prefs->node_name, (unsigned)meshPathHashBytes(), (unsigned)phs);
    }
  }

  // Only the first boot after the move seeds from our NVS. Later boots let MeshCore load
  // /channels2, so a channel added by the phone (next ticket) is not thrown away. A malformed file
  // is rewritten and checked again.
  crashCheckpoint("[mc] 5 channels");
  if (!migrated || !hadChannelsFile || !channelsFileMatchesMesh()) {
    pushChannelsFromNvs(true);
    if (!channelsFileMatchesMesh()) {
      Serial.println("[mc] migration: /channels2 size or entries wrong, mig not set");
      migrateOk = false;
    }
  }

  if (contactsMig == MigrateResult::Failed) {
    Serial.println("[mc] migration: /contacts3 incomplete, mig not set");
    migrateOk = false;
  }

  crashCheckpoint("[mc] 5b mig gate");
  if (migrateOk) {
    s_netPrefs.putBool("mig", true);
    Serial.println("[mc] migration: complete (prefs, channels and contacts read back)");
  } else {
    Serial.println("[mc] migration: INCOMPLETE, retrying next boot (mig not set)");
  }

  logFileSize("/identity/_main.id");
  logFileSize("/prefs.json");
  logFileSize("/channels2");
  logFileSize("/contacts3");
  logFileSize("/contacts");   // old EST-329 file, kept for one release

  // The chip answered and the mesh is built: a startup mark is resolved here, before the advert,
  // so an advert TX failure after this still sets the mark.
  if (radioFaultState().onRadioReinit()) Serial.println("[radio] fault cleared");

  s_ready = true;

  // One flood advert at boot (the old firmware did this; upstream's companion UI sends it from
  // its own advert action). The path hash size is whatever the prefs hold, so SLO stays on 2B.
  crashCheckpoint("[mc] 6 advert");
  mesh::Packet* pkt = nullptr;
  if (the_mesh.getNodePrefs()->advert_loc_policy != 0 &&
      (sensors.node_lat != 0 || sensors.node_lon != 0)) {
    pkt = the_mesh.createSelfAdvert(the_mesh.getNodeName(), sensors.node_lat, sensors.node_lon);
  } else {
    pkt = the_mesh.createSelfAdvert(the_mesh.getNodeName());
  }
  if (pkt) the_mesh.sendFlood(pkt, 0, meshPathHashBytes());

  s_netPrefs.putBool("crashed", false);
  s_netPrefs.putUChar("fails", 0);
  crashCheckpoint("[mc] up");
  Serial.printf("[mc] up: %.1f MHz SF%u, path hash %s, name \"%s\"\n",
                (double)the_mesh.getNodePrefs()->freq, (unsigned)the_mesh.getNodePrefs()->sf,
                meshPathHashLabel(), the_mesh.getNodeName());
  return true;
}

void meshLinkLoop() {
  if (!s_ready) return;
  the_mesh.loop();

  const uint32_t now = millis();
  if (s_newContact && (uint32_t)(now - s_newContactAt) >= kNewContactSaveMs) {
    store.saveContacts(&the_mesh);
    Serial.println("[mc] contacts: saved a new advert to /contacts3");
    s_newContact = false;
  }

  // The DM retry/timeout rule, from the companion's own est_timeout: one resend per expired attempt,
  // then "not confirmed". No radio fault is set here: an unanswered DM is not a broken radio.
  uint32_t seq = 0;
  uint8_t attempt = 0;
  const int ev = s_dmDelivery.poll(now, &seq, &attempt);
  if (ev == 1) {
    resendDm(seq, attempt);
  } else if (ev == 2) {
    setDmState(seq, om::ui::DmState::NotConfirmed);
    Serial.printf("[mc] DM seq %lu: no ACK after the last attempt\n", (unsigned long)seq);
  }

  // A ping has the same one-shot deadline; a trace waits for its packet to walk the hops back.
  if (s_pingState == MeshPingState::Sent && (int32_t)(now - s_pingDeadlineMs) >= 0) {
    s_pingState = MeshPingState::Timeout;
    Serial.printf("[mc] ping %s: no reply\n", s_pingName);
  }
  if (s_tracePending && (int32_t)(now - s_traceDeadlineMs) >= 0) {
    s_tracePending = false;
    ++s_traceSerial;
    Serial.printf("[mc] trace %s: no data back\n", s_trace.node);
  }
}

bool meshLinkReloadChannels() {
  if (!s_prefsBegun) return false;
  pushChannelsFromNvs(true);
  return true;
}

// --- message accessors ---------------------------------------------------------------------------
int meshMsgCount() { return s_msgRing.count(); }
uint32_t meshMsgSerial() { return s_storeSerial; }

const char* meshMsgText(int idx) {
  if (idx < 0 || idx >= s_msgRing.count()) return "";
  return s_msgs[slotFor(idx)].text;
}

int meshMsgChannel(int idx) {
  if (idx < 0 || idx >= s_msgRing.count()) return -1;
  return s_msgs[slotFor(idx)].ch;
}

const char* meshMsgName(int idx) {
  if (idx < 0 || idx >= s_msgRing.count()) return "";
  return s_msgs[slotFor(idx)].name;
}

bool meshMsgMine(int idx) {
  if (idx < 0 || idx >= s_msgRing.count()) return false;
  return s_msgs[slotFor(idx)].mine != 0;
}

uint32_t meshMsgTs(int idx) {
  if (idx < 0 || idx >= s_msgRing.count()) return 0;
  return s_msgs[slotFor(idx)].ts;
}

uint32_t meshMsgWallTs(int idx) {
  if (idx < 0 || idx >= s_msgRing.count()) return 0;
  return s_msgs[slotFor(idx)].wall;
}

bool meshMsgWallReal(int idx) {
  if (idx < 0 || idx >= s_msgRing.count()) return false;
  return s_msgs[slotFor(idx)].wallReal != 0;
}

uint32_t meshMsgSeq(int idx) {
  if (idx < 0 || idx >= s_msgRing.count()) return 0;
  return s_msgs[slotFor(idx)].seq;
}

int meshMsgHops(int idx) {
  if (idx < 0 || idx >= s_msgRing.count()) return -1;
  return s_msgs[slotFor(idx)].hops;
}

int meshMsgSnrQ(int idx) {
  if (idx < 0 || idx >= s_msgRing.count()) return 0;
  return s_msgs[slotFor(idx)].snr4;
}

const uint8_t* meshMsgPath(int idx, uint8_t* hashSize, uint8_t* count) {
  if (hashSize) *hashSize = 1;
  if (count) *count = 0;
  if (idx < 0 || idx >= s_msgRing.count()) return nullptr;
  const Msg& m = s_msgs[slotFor(idx)];
  if (m.pathCount == 0) return nullptr;
  if (hashSize) *hashSize = m.pathHashSize;
  if (count) *count = m.pathCount;
  return m.path;
}

int meshMsgHeardRepeats(int idx) {
  if (idx < 0 || idx >= s_msgRing.count()) return 0;
  return s_msgs[slotFor(idx)].repeats;
}

int meshUnread(int channelIdx) {
  if (channelIdx < 0 || channelIdx >= 8) return 0;
  return s_unread[channelIdx];
}

void meshClearUnread(int channelIdx) {
  if (channelIdx >= 0 && channelIdx < 8) s_unread[channelIdx] = 0;
}

// --- direct messages -----------------------------------------------------------------------------
int meshDmCount() { return s_dmRing.threadCount(); }

const char* meshDmName(int idx) {
  const om::ui::DmThread* t = s_dmRing.thread(idx);
  return (t != nullptr && t->used) ? t->name : "";
}

int meshDmUnread(int idx) { return s_dmRing.unread(idx); }

bool meshDmAvailable() { return s_dmRing.threadCount() > 0; }

uint32_t meshDmSerial() { return s_dmRing.serial(); }

uint32_t meshDmRecvSerial() { return s_dmRing.recvSerial(); }

void meshDmClearUnread(int thread) { s_dmRing.clearUnread(thread); }

int meshDmList(int thread, MeshDm* out, int max) {
  // The ring walks its own live sequence range (firstSeq..newest) and reads slot seq % ring, so the
  // list and the allocator can never disagree (EST-344). MeshDm is DmMsg itself.
  return s_dmRing.list(thread, out, max);
}

bool meshDmOpenByName(const char* name, int* threadOut) {
  if (threadOut) *threadOut = -1;
  if (name == nullptr || name[0] == 0) return false;
  const int total = the_mesh.getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!the_mesh.getContactByIdx((uint32_t)idx, ci)) continue;
    if (ci.type == ADV_TYPE_NONE || ci.name[0] == 0) continue;
    if (strcmp(ci.name, name) != 0) continue;
    const int t = ensureDmThread(ci);
    if (t < 0) return false;   // thread table full
    if (threadOut) *threadOut = t;
    Serial.printf("[mc] DM open with %s (thread %d)\n", ci.name, t);
    return true;
  }
  return false;
}

bool meshDmSend(int thread, const char* text) {
  if (!s_ready || text == nullptr || text[0] == 0) return false;
  const int len = (int)strlen(text);
  if (len > kMeshDmTextMax) {
    Serial.printf("[mc] DM send: %d bytes > %d budget, refused\n", len, kMeshDmTextMax);
    return false;
  }
  ContactInfo ci;
  if (!contactForThread(thread, &ci)) {
    Serial.println("[mc] DM send: no contact for that thread");
    return false;
  }
  const uint32_t wireTs = the_mesh.getRTCClock()->getCurrentTime();
  // THE same allocation storeDm() uses: seq = newest + 1, slot = seq % ring, committed here. The
  // reply that arrives next gets the following sequence, so it cannot overwrite this message.
  DmMsg& m = *s_dmRing.alloc((uint8_t)thread, true);
  const uint32_t seq = m.seq;
  m.state = (uint8_t)om::ui::DmState::Sending;
  m.ts = millis() / 1000;
  m.wall = timeKeeper().now();
  m.wallReal = timeKeeper().realSinceBoot() ? 1 : 0;
  m.wireTs = wireTs;
  m.hops = -1;
  snprintf(m.name, sizeof(m.name), "%s", the_mesh.getNodeName());
  snprintf(m.text, sizeof(m.text), "%s", text);

  uint32_t ack = 0;
  uint32_t est = 0;
  const int rc = the_mesh.sendMessage(ci, wireTs, 0, text, ack, est);
  if (rc == MSG_SEND_FAILED) {
    // Not a radio fault: MeshCore refused to compose (packet pool / tables). An unanswered DM is
    // also not a fault; only the radio's own failures set the mark.
    m.state = (uint8_t)om::ui::DmState::Failed;
    Serial.println("[mc] DM send: sendMessage failed");
    return false;
  }
  s_dmDelivery.begin(seq, ack, est, millis());
  s_dmRing.clearUnread(thread);
  Serial.printf("[mc] DM to %s (%d B) -> %s, ack %08lx, timeout %lu ms\n", ci.name, len,
                rc == MSG_SEND_SENT_FLOOD ? "flood" : "direct", (unsigned long)ack,
                (unsigned long)est);
  return true;
}

// --- node DB -------------------------------------------------------------------------------------
namespace {
bool validContact(const ContactInfo& c) {
  return c.type != ADV_TYPE_NONE && c.name[0] != 0;
}

void fillContact(const ContactInfo& ci, MeshContact& mc) {
  mc = MeshContact{};
  snprintf(mc.name, sizeof(mc.name), "%s", ci.name);
  mc.type = ci.type;
  // Out_path_len is MeshCore's ENCODED byte (hash size in the top 2 bits, count in the low 6); the
  // UI gets the decoded hop count, never the raw value (EST-344, mesh_path.h).
  mc.path_hops = (int8_t)om::pathHopCount(ci.out_path_len);
  mc.lastmod = ci.lastmod;
  mc.has_key = true;
  memcpy(mc.pub_key, ci.id.pub_key, PUB_KEY_SIZE);
  bool fav = false;
  isFavouriteKey(ci.id.pub_key, &fav);
  mc.favourite = fav;
  // The companion's Listener does not carry the advert packet, so there is no advert SNR: the Info
  // page leaves the row out instead of inventing one (EST-342).
  mc.adv_snr4 = 0;
  mc.adv_snr_known = false;
  const ContactStamp* mark = findContactStamp(ci.id.pub_key);
  if (mark != nullptr) {
    mc.lastmodUp = mark->uptime;
    mc.lastmodReal = mark->real;
  } else {
    mc.lastmodUp = 0;
    mc.lastmodReal = saneEpoch(ci.lastmod);
  }
  mc.has_pos = (ci.gps_lat != 0 || ci.gps_lon != 0);
  mc.lat_e6 = ci.gps_lat;
  mc.lon_e6 = ci.gps_lon;
}
}  // namespace

int meshContactList(MeshContact* out, int max) {
  if (!out || max <= 0) return 0;
  const int total = the_mesh.getTotalContactSlots();
  int n = 0;
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!the_mesh.getContactByIdx((uint32_t)idx, ci) || !validContact(ci)) continue;
    MeshContact mc;
    fillContact(ci, mc);
    // Favourites first, then the newest advert first (ui/node_menu.h holds the one rule).
    int pos = n;
    while (pos > 0 &&
           om::ui::nodeComesBefore(mc.favourite, mc.lastmod, out[pos - 1].favourite,
                                   out[pos - 1].lastmod)) {
      --pos;
    }
    if (pos >= max) continue;
    if (n < max) ++n;
    for (int j = n - 1; j > pos; --j) out[j] = out[j - 1];
    out[pos] = mc;
  }
  return n;
}

bool meshContactPathText(const char* name, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = 0;
  if (name == nullptr || name[0] == 0) return false;
  ContactInfo ci;
  bool found = false;
  const int total = the_mesh.getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    if (!the_mesh.getContactByIdx((uint32_t)idx, ci) || !validContact(ci)) continue;
    if (strcmp(ci.name, name) != 0) continue;
    found = true;
    break;
  }
  if (!found) return false;
  // One decoder for the encoded out_path_len (EST-344). The hash size is per-contact (what the path
  // actually arrived with), not our current prefs; the raw byte is never divided or printed.
  const PathSpec spec = decodeOutPathLen(ci.out_path_len);
  if (!spec.known) {
    snprintf(out, cap, "--");
    return true;
  }
  if (spec.count == 0) {
    snprintf(out, cap, "direct");
    return true;
  }
  const int hashSize = (int)spec.hashSize;
  const int count = (int)spec.count;
  size_t n = 0;
  for (int i = 0; i < count; ++i) {
    if (i > 0 && n + 3 < cap) {
      memcpy(out + n, " > ", 3);
      n += 3;
    }
    char hop[kMeshNameMax];
    meshPathHopName(ci.out_path + (size_t)i * hashSize, (uint8_t)hashSize, hop, sizeof(hop));
    char hex[8] = "";
    if (hop[0] == 0) {
      size_t h = 0;
      for (int b = 0; b < hashSize && h + 2 < sizeof(hex); ++b) {
        h += (size_t)snprintf(hex + h, sizeof(hex) - h, "%02x", ci.out_path[(size_t)i * hashSize + b]);
      }
      snprintf(hop, sizeof(hop), "%s", hex);
    }
    const size_t len = strlen(hop);
    if (n + len >= cap) break;
    memcpy(out + n, hop, len);
    n += len;
  }
  out[n] = 0;
  return true;
}

bool meshContactByName(const char* name, MeshContact* out) {
  if (name == nullptr || name[0] == 0 || out == nullptr) return false;
  const int total = the_mesh.getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!the_mesh.getContactByIdx((uint32_t)idx, ci) || !validContact(ci)) continue;
    if (strcmp(ci.name, name) != 0) continue;
    fillContact(ci, *out);
    return true;
  }
  return false;
}

const char* meshPathHopName(const uint8_t* hash, uint8_t len, char* buf, size_t cap) {
  if (buf == nullptr || cap == 0) return "";
  buf[0] = 0;
  if (hash == nullptr || len < 1 || len > MAX_HASH_SIZE) return buf;
  const int total = the_mesh.getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!the_mesh.getContactByIdx((uint32_t)idx, ci) || !validContact(ci)) continue;
    if (ci.type != ADV_TYPE_REPEATER) continue;
    if (memcmp(ci.id.pub_key, hash, len) != 0) continue;
    snprintf(buf, cap, "%s", ci.name);
    return buf;
  }
  return buf;
}

uint32_t meshContactSignature() {
  uint32_t h = 2166136261u;
  const int total = the_mesh.getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!the_mesh.getContactByIdx((uint32_t)idx, ci) || !validContact(ci)) continue;
    h = (h ^ ci.lastmod) * 16777619u;
    h = (h ^ ci.type) * 16777619u;
    bool fav = false;
    isFavouriteKey(ci.id.pub_key, &fav);
    h = (h ^ (fav ? 1u : 0u)) * 16777619u;   // a star changes the order, so it changes the list
    for (const char* p = ci.name; *p; ++p) h = (h ^ (uint8_t)*p) * 16777619u;
  }
  return h;
}

// --- node operations -----------------------------------------------------------------------------
namespace {

bool findContactByName(const char* name, ContactInfo* out) {
  if (name == nullptr || name[0] == 0 || out == nullptr) return false;
  const int total = the_mesh.getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!the_mesh.getContactByIdx((uint32_t)idx, ci) || !validContact(ci)) continue;
    if (strcmp(ci.name, name) != 0) continue;
    *out = ci;
    return true;
  }
  return false;
}

}  // namespace

bool meshNodePing(const char* name) {
  ContactInfo ci;
  if (!s_ready || !findContactByName(name, &ci)) {
    s_pingState = MeshPingState::Failed;
    return false;
  }
  uint32_t tag = 0;
  uint32_t est = 0;
  const int rc = the_mesh.sendRequest(ci, REQ_TYPE_GET_STATUS, tag, est);
  if (rc == MSG_SEND_FAILED) {
    s_pingState = MeshPingState::Failed;
    Serial.printf("[mc] ping %s: sendRequest failed\n", ci.name);
    return false;
  }
  s_pingState = MeshPingState::Sent;
  s_pingSentMs = millis();
  s_pingDeadlineMs = s_pingSentMs + est;
  s_pingRttMs = 0;
  memcpy(s_pingKey, ci.id.pub_key, PUB_KEY_SIZE);
  snprintf(s_pingName, sizeof(s_pingName), "%s", ci.name);
  Serial.printf("[mc] ping %s: status request sent (tag %08lx, timeout %lu ms)\n", ci.name,
                (unsigned long)tag, (unsigned long)est);
  return true;
}

MeshPingState meshPingState(uint32_t* rttMs, char* nameOut, size_t nameCap) {
  if (rttMs) *rttMs = s_pingRttMs;
  if (nameOut && nameCap) snprintf(nameOut, nameCap, "%s", s_pingName);
  return s_pingState;
}

bool meshNodeTrace(const char* name) {
  ContactInfo ci;
  if (!s_ready || !findContactByName(name, &ci)) return false;
  // The trace takes an explicit path; sendDirect() for TRACE appends path_len BYTES to the payload
  // (not MeshCore's encoded byte), so decode the contact's path first. The old code passed the
  // encoded out_path_len straight in, which for a 2B one-hop path (0x41) appended 65 bytes.
  const PathSpec spec = decodeOutPathLen(ci.out_path_len);
  if (!spec.known) {
    // A trace is a directed packet; upstream's CMD_SEND_TRACE_PATH also takes an explicit path, so
    // there is nothing to aim at until a path is known.
    Serial.printf("[mc] trace %s: no path known\n", ci.name);
    return false;
  }
  const uint32_t tag = the_mesh.getRTCClock()->getCurrentTimeUnique();
  const uint8_t flags = (uint8_t)((spec.hashSize - 1) & 0x03);
  mesh::Packet* pkt = the_mesh.createTrace(tag, 0, flags);
  if (pkt == nullptr) {
    Serial.println("[mc] trace: no packet");
    return false;
  }
  memset(&s_trace, 0, sizeof(s_trace));
  snprintf(s_trace.node, sizeof(s_trace.node), "%s", ci.name);
  s_traceSentMs = millis();
  s_traceDeadlineMs = s_traceSentMs + 12000;   // a trace must come back through every hop
  s_tracePending = true;
  ++s_traceSerial;
  const uint8_t pathBytes = (uint8_t)(spec.count * spec.hashSize);
  the_mesh.sendDirect(pkt, ci.out_path, pathBytes);
  Serial.printf("[mc] trace %s: %u byte(s), %u hop(s), tag %08lx\n", ci.name, (unsigned)pathBytes,
                (unsigned)spec.count, (unsigned long)tag);
  return true;
}

uint32_t meshTraceSerial() { return s_traceSerial; }

bool meshTraceResult(MeshTrace* out) {
  if (out == nullptr) return false;
  *out = s_trace;
  return s_trace.count > 0 || s_trace.haveRtt;
}

bool meshNodeResetPath(const char* name) {
  ContactInfo ci;
  if (!findContactByName(name, &ci)) return false;
  // resetPathTo() edits the object it is given. A COPY would be forgotten immediately, so the
  // table's own entry is looked up (the public lookup returns a pointer into it).
  ContactInfo* live = the_mesh.lookupContactByPubKey(ci.id.pub_key, PUB_KEY_SIZE);
  if (live == nullptr) return false;
  the_mesh.resetPathTo(*live);
  store.saveContacts(&the_mesh);
  Serial.printf("[mc] path to %s reset (will flood next time)\n", ci.name);
  return true;
}

bool meshNodeFavourite(const char* name, bool on) {
  ContactInfo ci;
  if (!findContactByName(name, &ci)) return false;
  if (!favouriteSet(ci.id.pub_key, on)) return false;
  Serial.printf("[mc] %s %s favourite\n", ci.name, on ? "is" : "is not");
  return true;
}

bool meshNodeIsFavourite(const char* name) {
  ContactInfo ci;
  if (!findContactByName(name, &ci)) return false;
  bool fav = false;
  isFavouriteKey(ci.id.pub_key, &fav);
  return fav;
}

bool meshNodeRemove(const char* name) {
  ContactInfo ci;
  if (!findContactByName(name, &ci)) return false;
  if (!the_mesh.removeContact(ci)) return false;
  store.saveContacts(&the_mesh);
  favouriteSet(ci.id.pub_key, false);
  s_knownKeys.remove(om::ui::KnownKeySet::keyHash(ci.id.pub_key, PUB_KEY_SIZE));
  Serial.printf("[mc] contact %s removed\n", ci.name);
  return true;
}

bool meshRptrTabExists() { return true; }

uint32_t meshNewNodeSerial() { return s_newNodeSerial; }

bool meshNewNodeTake(char* nameOut, size_t nameCap, uint8_t* advTypeOut) {
  if (s_newNodeCount <= 0) return false;
  const NewNodeEvent& e = s_newNodeEvents[s_newNodeHead];
  if (nameOut && nameCap) snprintf(nameOut, nameCap, "%s", e.name);
  if (advTypeOut) *advTypeOut = e.type;
  s_newNodeHead = (s_newNodeHead + 1) % kNewNodeEvents;
  --s_newNodeCount;
  return true;
}

uint32_t meshNow() { return the_mesh.getRTCClock()->getCurrentTime(); }

bool meshLinkSend(int channelIdx, const char* text) {
  Serial.printf("[mc] send ch%d ready=%d\n", channelIdx, (int)s_ready);
  if (!s_ready || text == nullptr || *text == 0) return false;
  const int prefix = (int)strlen(the_mesh.getNodeName()) + 2;   // "<name>: "
  if ((int)strlen(text) + prefix > kMeshTextMax) {
    Serial.printf("[mc] send: %u bytes + %d prefix > %d, refused\n", (unsigned)strlen(text),
                  prefix, kMeshTextMax);
    return false;
  }
  ChannelDetails cd;
  if (!the_mesh.getChannel(channelIdx, cd)) {
    Serial.println("[mc] send: no such channel");
    return false;
  }
  // The own-send marker and the stored message must share one sequence: nextSeq() is what alloc()
  // will return (EST-344).
  tables.beginOwnSend(s_msgRing.nextSeq());
  const bool ok = the_mesh.sendGroupMessage(meshNow(), cd.channel, the_mesh.getNodeName(), text,
                                            (int)strlen(text));
  tables.endOwnSend(ok);
  Serial.printf("[mc] sendGroupMessage -> %d\n", (int)ok);
  if (ok) {
    const uint32_t seq = s_msgRing.alloc();
    Msg& m = s_msgs[SeqRing<kMsgRing>::slotOf(seq)];
    memset(&m, 0, sizeof(m));
    m.seq = seq;
    m.ch = (uint8_t)channelIdx;
    m.mine = 1;
    m.ts = millis() / 1000;
    m.wall = timeKeeper().now();
    m.wallReal = timeKeeper().realSinceBoot() ? 1 : 0;
    m.hops = -1;
    snprintf(m.name, sizeof(m.name), "%s", the_mesh.getNodeName());
    snprintf(m.text, sizeof(m.text), "%s", text);
    ++s_storeSerial;
  }
  return ok;
}

}  // namespace om

#endif  // OM_ENABLE_MESH
