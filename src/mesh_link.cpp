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
#include "rptr_link.h"   // EST-358: the repeater session's hooks (see rptr_link.h)
#include "channels.h"
#include "crash_report.h"
#include "history_store.h"
#include "ble_model.h"
#include "ble_transport.h"
#include "mesh_migration.h"
#include "mesh_path.h"
#include "mesh_text.h"
#include "mem_stats.h"
#include "psram_alloc.h"
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
// EST-356: the channel writers save through the same DataStore the phone's commands use; its
// definition is further down with the other MeshCore globals.
extern DataStore store;

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
  uint32_t logId;    // EST-348: the on-card history id (0 = not stored yet)
  char name[kMeshNameCap];
  char text[kMeshTextCap];
  uint8_t path[MAX_PATH_SIZE];
};
constexpr int kMsgRing = 32;
// EST-357: 9,216 B of internal .bss, now one PSRAM allocation in meshLinkBegin() (psram_alloc.h).
Msg* s_msgs = nullptr;
// Message `seq` lives in slot `seq % kMsgRing`; SeqRing is the ONE allocation path (EST-344). The
// old slotFor() was a second rule (newest-1 based) and the own-repeat counter assumed a third, so a
// repeat was looked up in the wrong slot. Reads and writes now share SeqRing::slotOf().
SeqRing<kMsgRing> s_msgRing;
// EST-356: unread counts are keyed by the channel HASH, not by the UI index, because the phone can
// add or remove a channel and shift every index after it. MeshCore itself matches a packet to a
// channel by the first byte of sha256(secret), so one byte is the identity it already uses.
struct UnreadEntry {
  uint8_t hash;
  uint8_t count;
  bool used;
};
UnreadEntry s_unread[8] = {};
uint32_t s_storeSerial = 0;   // changes on every stored message (channel or DM)

int unreadEntryFor(uint8_t hash, bool create) {
  for (int i = 0; i < 8; ++i) {
    if (s_unread[i].used && s_unread[i].hash == hash) return i;
  }
  if (!create) return -1;
  for (int i = 0; i < 8; ++i) {
    if (!s_unread[i].used) {
      s_unread[i].used = true;
      s_unread[i].hash = hash;
      s_unread[i].count = 0;
      return i;
    }
  }
  return -1;
}

int unreadForHash(uint8_t hash) {
  const int at = unreadEntryFor(hash, false);
  return at < 0 ? 0 : s_unread[at].count;
}

// --- EST-348: the on-card history ----------------------------------------------------------------
// The RAM rings above are the view; the card is the truth across a reboot. Every stored message
// carries its history id (`logId`), and the update records name that id.
history::Msg histFromChannel(const Msg& m) {
  history::Msg h;
  h.seq = m.seq;
  h.uptime = m.ts;
  h.wall = m.wall;
  h.setWallReal(m.wallReal != 0);
  h.setMine(m.mine != 0);
  h.setDm(false);
  h.delivery = 0;
  h.hops = m.hops;
  h.snr4 = m.snr4;
  h.repeats = m.repeats;
  history::msgSetPath(h, m.path, m.pathHashSize, m.pathCount);
  history::msgSetText(h, m.name, m.text);
  return h;
}

// The history slot of a channel, creating it (no file yet) on first use.
int histChannelSlot(int ch) {
  if (ch < 0 || ch >= mcChannelCount()) return -1;
  return history::store().ensureChannel(mcChannelName(ch), mcChannel(ch).hash);
}

// The unread count a received channel message leaves behind, capped at the RAM field's 8 bits.
uint16_t histNextUnread8(uint8_t hash) {
  const int at = unreadEntryFor(hash, true);
  if (at < 0) return 0;
  return s_unread[at].count < 255 ? (uint16_t)(s_unread[at].count + 1) : 255;
}

// --- EST-356: MeshCore's channel table is the one list the screens read ---------------------------
// A UI index maps to a MeshCore slot. Empty slots (name empty and secret all zero, which is how a
// removed channel is stored) are skipped, so a phone-side delete leaves no nameless row behind.
int s_chanSlot[MAX_GROUP_CHANNELS] = {};
int s_chanCount = 0;
uint32_t s_chanSig = 0;
bool s_chanSigValid = false;
uint32_t s_ownChannelWrites = 0;   // our screens' writes: the change watch must not blame the phone
uint32_t s_ownRadioWrites = 0;

uint32_t fnvBytes(uint32_t h, const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data;
  for (size_t i = 0; i < len; ++i) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

bool channelSlotEmpty(const ChannelDetails& cd) {
  if (cd.name[0] != 0) return false;
  for (size_t i = 0; i < sizeof(cd.channel.secret); ++i) {
    if (cd.channel.secret[i] != 0) return false;
  }
  return true;
}

uint32_t channelSignatureNow() {
  uint32_t h = 2166136261u;
  for (int slot = 0; slot < MAX_GROUP_CHANNELS; ++slot) {
    ChannelDetails cd;
    if (!the_mesh.getChannel(slot, cd)) continue;
    h = fnvBytes(h, &slot, sizeof(slot));
    h = fnvBytes(h, cd.name, sizeof(cd.name));
    h = fnvBytes(h, cd.channel.secret, sizeof(cd.channel.secret));
  }
  return h;
}

void ensureChannelMap() {
  const uint32_t sig = channelSignatureNow();
  if (s_chanSigValid && sig == s_chanSig) return;
  s_chanSig = sig;
  s_chanSigValid = true;
  s_chanCount = 0;
  for (int slot = 0; slot < MAX_GROUP_CHANNELS; ++slot) {
    ChannelDetails cd;
    if (!the_mesh.getChannel(slot, cd)) continue;
    if (channelSlotEmpty(cd)) continue;
    s_chanSlot[s_chanCount++] = slot;
  }
}

int uiIndexForSlot(int slot) {
  ensureChannelMap();
  for (int i = 0; i < s_chanCount; ++i) {
    if (s_chanSlot[i] == slot) return i;
  }
  return -1;
}

// One store write + one DataStore save, exactly what the phone's CMD_SET_CHANNEL does.
bool writeChannelSlot(int slot, const ChannelDetails& cd) {
  if (!the_mesh.setChannel(slot, cd)) return false;
  store.saveChannels(&the_mesh);
  ++s_ownChannelWrites;
  s_chanSigValid = false;
  ensureChannelMap();
  return true;
}

// --- EST-356: the phone's changes, one event queue ------------------------------------------------
constexpr int kPhoneEvents = 4;
uint8_t s_phoneEvents[kPhoneEvents] = {};
int s_phoneEventCount = 0;
uint32_t s_phoneSerial = 0;

void queuePhoneChange(uint8_t change) {
  if (change == ble::kChangeNone) return;
  if (s_phoneEventCount >= kPhoneEvents) {   // drop the oldest, never the newest
    for (int i = 1; i < kPhoneEvents; ++i) s_phoneEvents[i - 1] = s_phoneEvents[i];
    --s_phoneEventCount;
  }
  s_phoneEvents[s_phoneEventCount++] = change;
  ++s_phoneSerial;
  if (s_phoneSerial == 0) s_phoneSerial = 1;
}


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
          // EST-348: the repeat count is one more small update record, applied on the next load.
          if (s_msgs[slot].logId != 0) {
            history::Update u;
            u.logId = s_msgs[slot].logId;
            u.kind = history::kUpdateRepeats;
            u.repeats = s_msgs[slot].repeats;
            const int ch = (int)s_msgs[slot].ch;
            history::store().appendUpdate(histChannelSlot(ch), u);
          }
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
// EST-357: 10,708 B of internal .bss moved to a PSRAM allocation in meshLinkBegin().
om::ui::DmRing* s_dmRing = nullptr;

// The sent DMs waiting for an ACK. The companion's own timeout (est_timeout from sendMessage) is
// what decides "not confirmed", and the same 4-byte hash proves "delivered".
om::ui::DmDeliveryTable s_dmDelivery;

// --- EST-348: a DM's history record ---------------------------------------------------------------
history::Msg histFromDm(const DmMsg& m) {
  history::Msg h;
  h.seq = m.seq;
  h.uptime = m.ts;
  h.wall = m.wall;
  h.setWallReal(m.wallReal != 0);
  h.setAckReal(m.ackReal != 0);
  h.setMine(m.mine != 0);
  h.setDm(true);
  h.delivery = m.state;
  h.ackWall = m.ackWall;
  h.hops = m.hops;
  h.snr4 = m.snr4;
  history::msgSetPath(h, m.path, m.pathHashSize, m.pathCount);
  history::msgSetText(h, m.name, m.text);
  return h;
}

// The history slot of a DM, by the contact's public key (the ticket's chat id).
int histDmSlot(int thread) {
  const om::ui::DmThread* t = s_dmRing->thread(thread);
  if (t == nullptr || !t->used) return -1;
  return history::store().ensureDm(t->key, t->name);
}

// Unread after one more received DM (the thread field is 16-bit).
uint16_t histNextUnreadDm(int thread) {
  const int n = s_dmRing->unread(thread);
  return n < 0xFFFF ? (uint16_t)(n + 1) : 0xFFFF;
}

int ensureDmThread(const ContactInfo& from) {
  return s_dmRing->ensureThread(from.id.pub_key, from.name);
}

// The contact behind a thread, so a send can use MeshCore's own ContactInfo.
bool contactForThread(int thread, ContactInfo* out) {
  const om::ui::DmThread* t = s_dmRing->thread(thread);
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
  DmMsg* m = s_dmRing->find(seq);
  if (!s_dmRing->setState(seq, (uint8_t)state)) return;
  Serial.printf("[mc] DM seq %lu: %s\n", (unsigned long)seq, om::ui::dmStateToken(state));
  // EST-348: the delivery state outlives the reboot as a small update record.
  if (m != nullptr && m->logId != 0) {
    history::Update u;
    u.logId = m->logId;
    u.kind = history::kUpdateState;
    u.delivery = (uint8_t)state;
    history::store().appendUpdate(histDmSlot(m->thread), u);
  }
}

void storeDm(const ContactInfo& from, mesh::Packet* pkt, const char* text) {
  const int thread = ensureDmThread(from);
  // THE allocation, shared with meshDmSend(): seq = newest + 1, slot = seq % ring, committed here.
  DmMsg& m = *s_dmRing->alloc((uint8_t)(thread < 0 ? om::ui::kDmNoThread : thread), false);
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
  // EST-348: store it before the unread count changes, so the Meta record carries the new count.
  m.logId = history::store().appendMsg(histDmSlot(thread), histFromDm(m),
                                       thread >= 0 ? histNextUnreadDm(thread) : 0);
  s_dmRing->bumpRecvSerial();
  if (thread >= 0) s_dmRing->noteReceived(thread, from.name, m.wall);
  Serial.printf("[mc] DM from %s: %s (hops %d snr %.2f)\n", from.name, text ? text : "",
                (int)m.hops, (double)m.snr4 / 4.0);
}

// Resend one DM with the next attempt (the companion app's retry): the same text and the same wire
// timestamp, so the recipient sees one message, and a new frame hash, so the ACK matches that frame.
bool resendDm(uint32_t seq, uint8_t attempt) {
  DmMsg* m = s_dmRing->find(seq);
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
    } else if (txt_type == TXT_TYPE_CLI_DATA) {
      // EST-358: a repeater's CLI answer to our remote admin. Not a chat message: it never enters
      // the DM ring or the unread count, it goes to the RPTR screen.
      rptrOnCliReply(from.name, text);
    }
  }

  // EST-358: the discovery answer (a control-data DISCOVER_RESP). MyMesh hands every control-data
  // packet to the listener after queueing the phone's own frame.
  void onControlDataRecv(const mesh::Packet* pkt) override { rptrOnControlData(pkt); }

  // Every ACK MeshCore receives passes through here (MyMesh::processAck calls it before matching
  // the phone's own table), so a DM's 4-byte hash proves delivery even with no phone attached.
  void onACKRecv(uint32_t ack_crc) override {
    uint32_t seq = 0;
    uint32_t rtt = 0;
    if (!s_dmDelivery.ack(ack_crc, millis(), &seq, &rtt)) return;
    // The ACK update goes through the ring too: same seq -> slot rule as the list (EST-344).
    const uint32_t ackWall = timeKeeper().now();
    const bool ackReal = timeKeeper().realSinceBoot();
    s_dmRing->markDelivered(seq, ackWall, ackReal);
    // EST-348: append the ACK as an update record so the thread still says "delivered" after a
    // reboot, with the same wall clock.
    DmMsg* dm = s_dmRing->find(seq);
    if (dm != nullptr && dm->logId != 0) {
      history::Update u;
      u.logId = dm->logId;
      u.kind = history::kUpdateState;
      u.delivery = (uint8_t)om::ui::DmState::Delivered;
      u.ackWall = ackWall;
      u.ackReal = ackReal;
      history::store().appendUpdate(histDmSlot(dm->thread), u);
    }
    Serial.printf("[mc] DM seq %lu delivered in %lu ms\n", (unsigned long)seq, (unsigned long)rtt);
  }

  // The status reply (the ping). sendRequest() does not set MyMesh's own pending_status, so an
  // unmatched response arrives here with its tag; the sender's key is what we match on.
  bool onUnhandledResponse(const ContactInfo& from, uint32_t tag_, const uint8_t* data,
                           uint8_t len) override {
    (void)len;
    if (s_pingState == MeshPingState::Sent &&
        memcmp(from.id.pub_key, s_pingKey, PUB_KEY_SIZE) == 0) {
      s_pingState = MeshPingState::Replied;
      s_pingRttMs = millis() - s_pingSentMs;
      snprintf(s_pingName, sizeof(s_pingName), "%s", from.name);
      Serial.printf("[mc] ping %s: reply in %lu ms\n", s_pingName, (unsigned long)s_pingRttMs);
      return true;   // handled here; no need for a phone frame
    }
    // EST-358: the repeater session's own answers (the login result and the status card). Only our
    // own answers are consumed; anything else still reaches the phone as upstream sends it.
    bool rptrHandled = false;
    rptrOnResponse(from, tag_, data, len, &rptrHandled);
    return rptrHandled;
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
    // EST-356: match the packet's secret against MeshCore's table, then map the slot to the UI
    // index. The table (not a copy) is what the screens draw, so a phone-side edit is visible here
    // on the very next packet.
    int channelIdx = -1;
    for (int slot = 0; slot < MAX_GROUP_CHANNELS; ++slot) {
      ChannelDetails cd;
      if (!the_mesh.getChannel(slot, cd)) continue;
      if (memcmp(cd.channel.secret, channel_details.channel.secret, 16) == 0) {
        channelIdx = uiIndexForSlot(slot);
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
    // EST-348: the message goes to the card before the RAM unread count changes, so the Meta
    // record written with it already carries the new count.
    const uint8_t msgHash = channel_details.channel.hash[0];
    m.logId = history::store().appendMsg(histChannelSlot(channelIdx), histFromChannel(m),
                                         histNextUnread8(msgHash));
    ++s_storeSerial;
    const int unreadAt = unreadEntryFor(msgHash, true);
    if (unreadAt >= 0 && s_unread[unreadAt].count < 255) ++s_unread[unreadAt].count;
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
  // EST-357: the one-time migration's 14,592 B of scratch (legacy file, decoded records, /contacts3
  // image) is allocated only when the migration really runs and freed on every exit. As statics it
  // lived in internal .bss for the whole boot, competing with the radio stacks for the heap.
  struct Scratch {
    uint8_t* legacy = (uint8_t*)ps_malloc(kLegacyContactMaxFileSize);
    LegacyContact* recs = (LegacyContact*)ps_malloc(sizeof(LegacyContact) * kLegacyMaxContactRecords);
    uint8_t* image =
        (uint8_t*)ps_malloc((size_t)kLegacyMaxContactRecords * kLegacyContactRecordSize);
    Scratch() {
      if (legacy == nullptr) legacy = (uint8_t*)malloc(kLegacyContactMaxFileSize);
      if (recs == nullptr) recs = (LegacyContact*)malloc(sizeof(LegacyContact) * kLegacyMaxContactRecords);
      if (image == nullptr) {
        image = (uint8_t*)malloc((size_t)kLegacyMaxContactRecords * kLegacyContactRecordSize);
      }
    }
    ~Scratch() {
      free(legacy);
      free(recs);
      free(image);
    }
    bool ok() const { return legacy != nullptr && recs != nullptr && image != nullptr; }
  } scratch;
  if (!scratch.ok()) {
    in.close();
    Serial.println("[mc] migrate /contacts: out of memory, left for the next boot");
    return MigrateResult::Failed;
  }
  const size_t got = (size_t)in.read(scratch.legacy, len);
  in.close();
  if (got != len) {
    Serial.printf("[mc] migrate /contacts: short read %u of %u, not migrated\n", (unsigned)got,
                  (unsigned)len);
    return MigrateResult::Failed;
  }

  const int n = decodeLegacyContacts(scratch.legacy, len, scratch.recs, kLegacyMaxContactRecords);
  if (n < 0) {
    Serial.println("[mc] migrate /contacts: corrupt (magic/version/CRC), not migrated");
    return MigrateResult::Failed;
  }
  if (n == 0) {
    Serial.println("[mc] migrate /contacts: 0 contacts, no /contacts3 needed");
    return MigrateResult::Ok;
  }

  size_t imageLen = 0;
  if (!encodeContacts3(scratch.recs, n, scratch.image,
                       (size_t)kLegacyMaxContactRecords * kLegacyContactRecordSize, &imageLen)) {
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
  const size_t wrote = (size_t)out.write(scratch.image, imageLen);
  out.flush();   // push the stdio buffer into SPIFFS before anyone asks for the file's size
  out.close();
  if (wrote != imageLen) {
    SPIFFS.remove(kContactsTmpPath);
    Serial.printf("[mc] migrate /contacts: short write %u of %u, not migrated\n", (unsigned)wrote,
                  (unsigned)imageLen);
    return MigrateResult::Failed;
  }
  if (!readBackExact(kContactsTmpPath, scratch.image, imageLen)) {
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
  if (!readBackExact(kNewContactsPath, scratch.image, imageLen)) {
    Serial.println("[mc] migrate /contacts: /contacts3 did not read back after rename");
    return MigrateResult::Failed;
  }
  // /contacts stays in place for one release, so a rollback still finds it.
  Serial.printf("[mc] migrate /contacts -> /contacts3: %d contacts, %u bytes written and read back "
                "(old file kept)\n",
                n, (unsigned)imageLen);
  return MigrateResult::Ok;
}

// EST-356: the ONE-TIME seed from the legacy NVS store into MeshCore's table (first boot after the
// move, or a /channels2 that does not read back). The legacy accessors are used on purpose: the
// screen-facing mcChannel* now reads MeshCore's table, so seeding from it would copy the table onto
// itself and lose the NVS channels.
void pushChannelsFromNvs(bool save) {
  const int count = mcLegacyChannelCount();
  for (int i = 0; i < kMaxChannels; ++i) {
    ChannelDetails cd;
    memset(&cd, 0, sizeof(cd));
    if (i < count) {
      snprintf(cd.name, sizeof(cd.name), "%s", mcLegacyChannel(i).name);
      memcpy(cd.channel.secret, mcLegacyChannel(i).secret, 16);
    }
    the_mesh.setChannel(i, cd);
  }
  if (save) store.saveChannels(&the_mesh);
  s_chanSigValid = false;
  Serial.printf("[mc] channels: %d from the legacy NVS store%s\n", count,
                save ? " (saved to /channels2)" : "");
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
  ++s_ownRadioWrites;
  if (s_prefsBegun) s_netPrefs.putUChar("phs", bytes);   // old key kept for rollback
  Serial.printf("[mc] path hash set to %s (saved in prefs.json)\n", meshPathHashLabel());
  return true;
}

// --- radio prefs used by Settings (EST-347) -------------------------------------------------------
// Same fields, same save, same radio apply as MyMesh's CMD_SET_RADIO_PARAMS / _TX_POWER handlers.
bool meshRadioPrefsGet(MeshRadioPrefs* out) {
  if (out == nullptr) return false;
  const NodePrefs* p = the_mesh.getNodePrefs();
  out->freq_khz = (uint32_t)(p->freq * 1000.0f + 0.5f);
  out->bw_hz = (uint32_t)(p->bw * 1000.0f + 0.5f);
  out->sf = p->sf;
  out->cr = p->cr;
  out->tx = p->tx_power_dbm;
  return true;
}

bool meshRadioPrefsSet(const MeshRadioPrefs& p) {
  // Upstream's CMD_SET_RADIO_PARAMS test, verbatim in meaning (bw is Hz there, kHz here).
  if (p.freq_khz < 150000 || p.freq_khz > 2500000 || p.sf < 5 || p.sf > 12 || p.cr < 5 ||
      p.cr > 8 || p.bw_hz < 7000 || p.bw_hz > 500000) {
    return false;
  }
  NodePrefs* prefs = the_mesh.getNodePrefs();
  prefs->sf = p.sf;
  prefs->cr = p.cr;
  prefs->freq = (float)p.freq_khz / 1000.0f;
  prefs->bw = (float)p.bw_hz / 1000.0f;
  prefs->tx_power_dbm = (p.tx < -9) ? -9 : (p.tx > MAX_LORA_TX_POWER ? MAX_LORA_TX_POWER : p.tx);
  the_mesh.savePrefs();
  ++s_ownRadioWrites;
  radio_driver.setParams(prefs->freq, prefs->bw, prefs->sf, prefs->cr);
  radio_driver.setTxPower(prefs->tx_power_dbm);
  Serial.printf("[mc] radio set: %.3f MHz bw %.1f sf %u cr %u tx %d dBm (saved in prefs.json)\n",
                (double)prefs->freq, (double)prefs->bw, (unsigned)prefs->sf, (unsigned)prefs->cr,
                (int)prefs->tx_power_dbm);
  return true;
}

bool meshTxPowerSet(int8_t dbm) {
  if (dbm < -9 || dbm > MAX_LORA_TX_POWER) return false;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  prefs->tx_power_dbm = dbm;
  the_mesh.savePrefs();
  ++s_ownRadioWrites;
  radio_driver.setTxPower(dbm);
  Serial.printf("[mc] tx power %d dBm (saved in prefs.json)\n", (int)dbm);
  return true;
}

bool meshNodeNameSet(const char* name) {
  if (name == nullptr) return false;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  snprintf(prefs->node_name, sizeof(prefs->node_name), "%s", name);
  the_mesh.savePrefs();   // the CMD_SET_ADVERT_NAME path
  ++s_ownRadioWrites;
  Serial.printf("[mc] node name \"%s\" (saved in prefs.json)\n", prefs->node_name);
  return true;
}

bool meshAdvertPosPolicyGet() {
  return the_mesh.getNodePrefs()->advert_loc_policy != ADVERT_LOC_NONE;
}

bool meshAdvertPosPolicySet(bool share) {
  // CMD_SET_OTHER_PARAMS writes exactly this field, then saves.
  the_mesh.getNodePrefs()->advert_loc_policy = share ? ADVERT_LOC_SHARE : ADVERT_LOC_NONE;
  the_mesh.savePrefs();
  ++s_ownRadioWrites;
  Serial.printf("[mc] advert position %s (saved in prefs.json)\n", share ? "shared" : "off");
  return true;
}

bool meshPubKeyShort(char* out, size_t cap) {
  if (out == nullptr || cap < 9) return false;
  snprintf(out, cap, "%02X%02X%02X%02X", the_mesh.self_id.pub_key[0], the_mesh.self_id.pub_key[1],
           the_mesh.self_id.pub_key[2], the_mesh.self_id.pub_key[3]);
  return true;
}


// EST-358: rptr_link.cpp needs the one MyMesh (it is file-local to this file on purpose). This is
// the only door, and it hands back the same object every facade call already drives.
MyMesh& rptrMesh() { return the_mesh; }

bool meshLinkBegin() {
  if (s_ready) return true;

  // EST-357: the two message rings moved out of internal .bss and into PSRAM, allocated once here
  // (before anything can store a message). Without PSRAM the helper falls back to internal RAM.
  if (s_msgs == nullptr) s_msgs = psramArray<Msg>(kMsgRing);
  if (s_dmRing == nullptr) s_dmRing = psramObject<om::ui::DmRing>();
  if (s_msgs == nullptr || s_dmRing == nullptr) {
    Serial.println("[mc] message rings: out of memory, mesh not started");
    return false;
  }
  s_dmRing->reset();
  Serial.printf("[mc] message rings: channel %u B, DM %u B in PSRAM\n",
                (unsigned)(sizeof(Msg) * kMsgRing), (unsigned)sizeof(om::ui::DmRing));

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
  // EST-356: Bluetooth is a switch, off by default. MyMesh keeps the transport pointer from here on;
  // BleSerial::enable() is a no-op until the switch is on, so the BLE stack is not allocated at boot.
  the_mesh.setBLEPin(the_mesh.getNodePrefs()->ble_pin);
  the_mesh.startInterface(bleTransport());

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

  // EST-347: the radio settings now live in NodePrefs too. A store that is missing or invalid is
  // seeded from the build values (the same ones the radio is already running), so the first boot
  // changes nothing on the air; then the same setParams/setTxPower the phone command path calls.
  {
    NodePrefs* prefs = the_mesh.getNodePrefs();
    const bool ok = prefs->freq >= 150.0f && prefs->freq <= 2500.0f && prefs->bw >= 7.0f &&
                    prefs->bw <= 500.0f && prefs->sf >= 5 && prefs->sf <= 12 && prefs->cr >= 5 &&
                    prefs->cr <= 8 && prefs->tx_power_dbm >= -9 &&
                    prefs->tx_power_dbm <= MAX_LORA_TX_POWER;
    if (!ok) {
      prefs->freq = LORA_FREQ;
      prefs->bw = LORA_BW;
      prefs->sf = LORA_SF;
      prefs->cr = LORA_CR;
      prefs->tx_power_dbm = LORA_TX_POWER;
      the_mesh.savePrefs();
      radio_driver.setParams(prefs->freq, prefs->bw, prefs->sf, prefs->cr);
      radio_driver.setTxPower(prefs->tx_power_dbm);
      Serial.printf("[mc] radio prefs seeded from build: %.3f MHz bw %.1f sf %u cr %u tx %d dBm\n",
                    (double)prefs->freq, (double)prefs->bw, (unsigned)prefs->sf,
                    (unsigned)prefs->cr, (int)prefs->tx_power_dbm);
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
  rptrLinkBegin();   // EST-358: the remembered repeaters and the auto-login switch (NVS)

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
  // The sizes the Bluetooth/Wi-Fi heap ticket needs for a PSRAM proposal: the MeshCore object (the
  // 100-contact table dominates it) and our two message rings, all in internal DRAM today.
  Serial.printf("[mc] sizes: MyMesh=%u B, channel ring=%u B, DM ring=%u B, DM threads=%u B\n",
                (unsigned)sizeof(the_mesh), (unsigned)(sizeof(Msg) * kMsgRing),
                (unsigned)(sizeof(om::ui::DmMsg) * om::ui::kDmRing),
                (unsigned)(sizeof(om::ui::DmThread) * om::ui::kDmThreads));
  Serial.printf("[mc] up: %.1f MHz SF%u, path hash %s, name \"%s\"\n",
                (double)the_mesh.getNodePrefs()->freq, (unsigned)the_mesh.getNodePrefs()->sf,
                meshPathHashLabel(), the_mesh.getNodeName());
  return true;
}

void meshLinkLoop() {
  // EST-349: history flushing moved to loop() in main.cpp, before otaPoll(): ONE owner, on the loop
  // task, outside the mesh and USB call paths and outside any lock. This function never touches SD.
  if (!s_ready) return;
  the_mesh.loop();

  // EST-356: the phone's writes land in MyMesh's stores with no callback; poll the two store
  // signatures once per pass. Our own setters bump the generation counters, so a value the glass
  // just wrote is not reported as "from the phone".
  {
    static ble::ChangeWatch watch;
    const uint8_t changes = watch.poll(meshChannelSignature(), meshRadioSignature(),
                                       meshNameSignature(), s_ownChannelWrites, s_ownRadioWrites);
    if (changes != ble::kChangeNone) {
      queuePhoneChange(changes);
      Serial.printf("[ble] phone changed the stores (0x%02x)\n", (unsigned)changes);
    }
  }

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

  // EST-358: the discovery window and the login/status deadlines.
  rptrLinkLoop();
}

bool meshLinkReloadChannels() {
  if (!s_prefsBegun) return false;
  // EST-356: our own channel edits now write MeshCore's table directly (meshChannelSetName/Add/
  // Remove), so there is nothing to copy. The map's signature is refreshed on the next read.
  s_chanSigValid = false;
  ensureChannelMap();
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

uint32_t meshMsgLogId(int idx) {
  if (idx < 0 || idx >= s_msgRing.count()) return 0;
  return s_msgs[slotFor(idx)].logId;
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
  if (channelIdx < 0 || channelIdx >= mcChannelCount()) return 0;
  return unreadForHash(mcChannel(channelIdx).hash);
}

void meshClearUnread(int channelIdx) {
  if (channelIdx < 0 || channelIdx >= mcChannelCount()) return;
  const int at = unreadEntryFor(mcChannel(channelIdx).hash, false);
  if (at >= 0) s_unread[at].count = 0;
  // EST-348: the seen point moves with it, so a reboot does not resurrect the badge.
  history::store().markSeen(histChannelSlot(channelIdx));
}

// --- direct messages -----------------------------------------------------------------------------
// EST-357: the ring is a PSRAM pointer now, so the facade's read side answers empty if the allocation
// never happened (meshLinkBegin refused). Nothing here may dereference a null on a draw path.
int meshDmCount() { return s_dmRing != nullptr ? s_dmRing->threadCount() : 0; }

const char* meshDmName(int idx) {
  if (s_dmRing == nullptr) return "";
  const om::ui::DmThread* t = s_dmRing->thread(idx);
  return (t != nullptr && t->used) ? t->name : "";
}

int meshDmUnread(int idx) { return s_dmRing != nullptr ? s_dmRing->unread(idx) : 0; }

bool meshDmAvailable() { return s_dmRing != nullptr && s_dmRing->threadCount() > 0; }

uint32_t meshDmSerial() { return s_dmRing != nullptr ? s_dmRing->serial() : 0; }

uint32_t meshDmRecvSerial() { return s_dmRing != nullptr ? s_dmRing->recvSerial() : 0; }

void meshDmClearUnread(int thread) {
  if (s_dmRing == nullptr) return;
  s_dmRing->clearUnread(thread);
  history::store().markSeen(histDmSlot(thread));
}

int meshDmList(int thread, MeshDm* out, int max) {
  // The ring walks its own live sequence range (firstSeq..newest) and reads slot seq % ring, so the
  // list and the allocator can never disagree (EST-344). MeshDm is DmMsg itself.
  if (s_dmRing == nullptr) return 0;
  return s_dmRing->list(thread, out, max);
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
  DmMsg& m = *s_dmRing->alloc((uint8_t)thread, true);
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
    m.logId = history::store().appendMsg(histDmSlot(thread), histFromDm(m), 0);
    Serial.println("[mc] DM send: sendMessage failed");
    return false;
  }
  // EST-348: the sent DM is on the card as "sending"; the ACK later appends its update record.
  m.logId = history::store().appendMsg(histDmSlot(thread), histFromDm(m), 0);
  s_dmDelivery.begin(seq, ack, est, millis());
  s_dmRing->clearUnread(thread);
  Serial.printf("[mc] DM to %s (%d B) -> %s, ack %08lx, timeout %lu ms\n", ci.name, len,
                rc == MSG_SEND_SENT_FLOOD ? "flood" : "direct", (unsigned long)ack,
                (unsigned long)est);
  return true;
}

// --- EST-348: boot restore, paging and the USB dump ----------------------------------------------
// A DM log stores the contact's name as it was when the thread started. The contact table is the
// current truth, so a renamed contact gets its new name back at boot.
bool contactNameByKey(const uint8_t* key, char* out, size_t cap) {
  if (key == nullptr || out == nullptr || cap == 0) return false;
  const int total = the_mesh.getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!the_mesh.getContactByIdx((uint32_t)idx, ci)) continue;
    if (ci.type == ADV_TYPE_NONE || ci.name[0] == 0) continue;
    if (memcmp(ci.id.pub_key, key, PUB_KEY_SIZE) != 0) continue;
    snprintf(out, cap, "%s", ci.name);
    return true;
  }
  return false;
}

bool meshHistoryBegin(bool sdOk) {
  if (!history::begin(sdOk)) return false;
  history::HistoryStore& hs = history::store();
  int channelMsgs = 0;
  int dmMsgs = 0;

  for (int i = 0; i < mcChannelCount() && i < 8; ++i) {
    const int slot = histChannelSlot(i);
    if (slot < 0) continue;
    const int n = hs.loadNewest(slot, kMsgRing);
    for (int j = 0; j < n; ++j) {
      const history::Msg& h = hs.scanMsg(j);
      const uint32_t seq = s_msgRing.alloc();
      Msg& m = s_msgs[SeqRing<kMsgRing>::slotOf(seq)];
      memset(&m, 0, sizeof(m));
      m.seq = seq;
      m.logId = h.logId;
      m.ch = (uint8_t)i;
      m.mine = h.mine() ? 1 : 0;
      m.ts = h.uptime;
      m.wall = h.wall;
      m.wallReal = h.wallReal() ? 1 : 0;
      m.hops = h.hops;
      m.snr4 = h.snr4;
      m.repeats = h.repeats;
      m.pathHashSize = h.pathHashSize;
      m.pathCount = h.pathCount;
      if (h.pathLen > 0) memcpy(m.path, h.path, h.pathLen);
      snprintf(m.name, sizeof(m.name), "%s", h.name);
      snprintf(m.text, sizeof(m.text), "%s", h.text);
      ++channelMsgs;
    }
    const int uat = unreadEntryFor(mcChannel(i).hash, true);
    if (uat >= 0) {
      const uint16_t unread = hs.scanUnread();
      s_unread[uat].count = unread > 255 ? 255 : (uint8_t)unread;
    }
  }

  // Every dm-*.log on the card becomes a thread again, with its contact key and name from the Chat
  // record (the file name only carries the public-key prefix, as the ticket specifies).
  for (int k = 0; k < hs.dmSlotCount(); ++k) {
    const int slot = hs.dmSlotAt(k);
    const uint8_t* key = nullptr;
    if (slot < 0 || !hs.slotKey(slot, &key)) continue;
    char threadName[kMeshNameCap];
    snprintf(threadName, sizeof(threadName), "%s", hs.slotName(slot));
    contactNameByKey(key, threadName, sizeof(threadName));
    const int thread = s_dmRing->ensureThread(key, threadName);
    if (thread < 0) continue;
    const int n = hs.loadNewest(slot, om::ui::kDmRing);
    uint32_t newestWall = 0;
    for (int j = 0; j < n; ++j) {
      const history::Msg& h = hs.scanMsg(j);
      const bool mine = h.mine();
      DmMsg& m = *s_dmRing->alloc((uint8_t)thread, mine);
      m.logId = h.logId;
      m.ts = h.uptime;
      m.wall = h.wall;
      m.wallReal = h.wallReal() ? 1 : 0;
      m.ackWall = h.ackWall;
      m.ackReal = h.ackReal() ? 1 : 0;
      m.state = h.delivery;
      m.hops = h.hops;
      m.snr4 = h.snr4;
      m.pathHashSize = h.pathHashSize;
      m.pathCount = h.pathCount;
      if (h.pathLen > 0) memcpy(m.path, h.path, h.pathLen);
      snprintf(m.name, sizeof(m.name), "%s", h.name);
      snprintf(m.text, sizeof(m.text), "%s", h.text);
      newestWall = h.wall;
      ++dmMsgs;
    }
    om::ui::DmThread* t = s_dmRing->thread(thread);
    if (t != nullptr) {
      t->unread = hs.scanUnread();
      if (newestWall != 0) t->lastWall = newestWall;
    }
  }

  ++s_storeSerial;
  s_dmRing->bumpSerial();
  Serial.printf("[hist] restored %d channel message(s), %d DM message(s) from %s\n", channelMsgs,
                dmMsgs, history::status().sd ? "the card" : "SPIFFS");
  return true;
}

int meshHistoryOlder(int channelIdx, int thread, uint32_t beforeLogId, history::Msg* out, int cap) {
  if (out == nullptr || cap <= 0) return -1;
  history::HistoryStore& hs = history::store();
  int slot = -1;
  if (thread >= 0) slot = histDmSlot(thread);
  else if (channelIdx >= 0) slot = histChannelSlot(channelIdx);
  if (slot < 0) return -1;
  const int n = hs.loadOlder(slot, beforeLogId, cap);
  for (int i = 0; i < n; ++i) out[i] = hs.scanMsg(i);
  return n;
}

int meshHistoryDump(const char* chat, int n) {
  if (chat == nullptr || *chat == 0) return -1;
  history::HistoryStore& hs = history::store();
  int slot = -1;
  history::ChatKind kind = history::ChatKind::Channel;
  for (int i = 0; i < mcChannelCount(); ++i) {
    if (strcmp(mcChannelName(i), chat) != 0) continue;
    slot = histChannelSlot(i);
    kind = history::ChatKind::Channel;
    break;
  }
  if (slot < 0) {
    slot = hs.dmSlotByNameOrKey(chat);
    kind = history::ChatKind::Dm;
  }
  if (slot < 0) return -1;
  if (n < 1) n = 20;
  if (n > history::kScanRing) n = history::kScanRing;
  const int count = hs.loadNewest(slot, n);
  for (int i = 0; i < count; ++i) {
    const history::Msg& m = hs.scanMsg(i);
    char from[64];
    char text[192];
    int w = 0;
    for (const char* p = m.name; *p != 0 && w < (int)sizeof(from) - 1; ++p) {
      from[w++] = *p == '"' ? '\'' : *p;
    }
    from[w] = 0;
    w = 0;
    for (const char* p = m.text; *p != 0 && w < (int)sizeof(text) - 1; ++p) {
      text[w++] = *p == '"' ? '\'' : *p;
    }
    text[w] = 0;
    const char* state = "recv";
    if (m.mine()) {
      state = kind == history::ChatKind::Dm ? om::ui::dmStateToken((om::ui::DmState)m.delivery) : "sent";
    }
    char when[24];
    if (m.wallReal() && m.wall != 0) snprintf(when, sizeof(when), "%lu", (unsigned long)m.wall);
    else snprintf(when, sizeof(when), "-");
    Serial.printf(
        "[hist] id=%lu seq=%lu up=%lu wall=%s real=%u from=\"%s\" hops=%d snr=%.2f state=%s "
        "repeats=%u text=\"%s\"\n",
        (unsigned long)m.logId, (unsigned long)m.seq, (unsigned long)m.uptime, when,
        m.wallReal() ? 1u : 0u, from, (int)m.hops, (double)m.snr4 / 4.0, state,
        (unsigned)m.repeats, text);
  }
  return count;
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

// --- EST-356: the channel table, the PIN and the phone's own messages -------------------------------
int meshChannelCount() {
  ensureChannelMap();
  return s_chanCount;
}

int meshChannelSlot(int idx) {
  ensureChannelMap();
  return (idx >= 0 && idx < s_chanCount) ? s_chanSlot[idx] : -1;
}

const char* meshChannelName(int idx) {
  static char s_names[MAX_GROUP_CHANNELS][kMeshNameMax];
  ensureChannelMap();
  if (idx < 0 || idx >= s_chanCount) return "";
  ChannelDetails cd;
  if (!the_mesh.getChannel(s_chanSlot[idx], cd)) return "";
  snprintf(s_names[idx], sizeof(s_names[idx]), "%s", cd.name);
  return s_names[idx];
}

bool meshChannelCopy(int idx, char* name, size_t cap, uint8_t secret[16], uint8_t* hash) {
  if (name == nullptr || secret == nullptr || hash == nullptr || cap == 0) return false;
  ensureChannelMap();
  if (idx < 0 || idx >= s_chanCount) return false;
  ChannelDetails cd;
  if (!the_mesh.getChannel(s_chanSlot[idx], cd)) return false;
  snprintf(name, cap, "%s", cd.name);
  memcpy(secret, cd.channel.secret, 16);
  *hash = cd.channel.hash[0];
  return true;
}

bool meshChannelSetName(int idx, const char* name) {
  if (!s_ready || name == nullptr) return false;
  ensureChannelMap();
  if (idx < 0 || idx >= s_chanCount) return false;
  const int slot = s_chanSlot[idx];
  ChannelDetails cd;
  if (!the_mesh.getChannel(slot, cd)) return false;
  StrHelper::strncpy(cd.name, name, sizeof(cd.name));
  if (!writeChannelSlot(slot, cd)) return false;
  Serial.printf("[mc] channel slot %d renamed to \"%s\" (saved to /channels2)\n", slot, name);
  return true;
}

int meshChannelAdd(const char* name, const uint8_t secret[16]) {
  if (!s_ready || name == nullptr || name[0] == 0 || secret == nullptr) return -1;
  ensureChannelMap();
  // The same key twice is almost certainly a mistake (or a channel already joined): return the slot
  // that already holds it instead of creating a second entry receiving the same traffic twice.
  for (int slot = 0; slot < MAX_GROUP_CHANNELS; ++slot) {
    ChannelDetails cd;
    if (!the_mesh.getChannel(slot, cd)) continue;
    if (channelSlotEmpty(cd)) continue;
    if (memcmp(cd.channel.secret, secret, 16) == 0) return uiIndexForSlot(slot);
  }
  int slot = -1;
  for (int i = 0; i < MAX_GROUP_CHANNELS; ++i) {
    ChannelDetails cd;
    if (!the_mesh.getChannel(i, cd)) continue;
    if (channelSlotEmpty(cd)) {
      slot = i;
      break;
    }
  }
  if (slot < 0) return -1;
  ChannelDetails cd;
  memset(&cd, 0, sizeof(cd));
  StrHelper::strncpy(cd.name, name, sizeof(cd.name));
  memcpy(cd.channel.secret, secret, 16);
  if (!writeChannelSlot(slot, cd)) return -1;
  const int ui = uiIndexForSlot(slot);
  Serial.printf("[mc] channel added at slot %d: \"%s\" (saved to /channels2)\n", slot, name);
  return ui;
}

bool meshChannelRemove(int idx) {
  if (!s_ready) return false;
  ensureChannelMap();
  if (idx < 0 || idx >= s_chanCount) return false;
  const int slot = s_chanSlot[idx];
  ChannelDetails empty;
  memset(&empty, 0, sizeof(empty));
  if (!writeChannelSlot(slot, empty)) return false;
  Serial.printf("[mc] channel slot %d removed (saved to /channels2)\n", slot);
  return true;
}

uint32_t meshChannelSignature() {
  ensureChannelMap();
  return s_chanSig;
}

uint32_t meshRadioSignature() {
  NodePrefs* p = the_mesh.getNodePrefs();
  if (p == nullptr) return 0;
  uint32_t h = 2166136261u;
  h = fnvBytes(h, &p->freq, sizeof(p->freq));
  h = fnvBytes(h, &p->bw, sizeof(p->bw));
  h = fnvBytes(h, &p->sf, sizeof(p->sf));
  h = fnvBytes(h, &p->cr, sizeof(p->cr));
  h = fnvBytes(h, &p->tx_power_dbm, sizeof(p->tx_power_dbm));
  h = fnvBytes(h, &p->path_hash_mode, sizeof(p->path_hash_mode));
  h = fnvBytes(h, &p->advert_loc_policy, sizeof(p->advert_loc_policy));
  h = fnvBytes(h, &p->multi_acks, sizeof(p->multi_acks));
  h = fnvBytes(h, &p->rx_delay_base, sizeof(p->rx_delay_base));
  h = fnvBytes(h, &p->airtime_factor, sizeof(p->airtime_factor));
  h = fnvBytes(h, &p->tx_delay_factor, sizeof(p->tx_delay_factor));
  h = fnvBytes(h, &p->direct_tx_delay_factor, sizeof(p->direct_tx_delay_factor));
  return h;
}

uint32_t meshNameSignature() {
  const char* name = the_mesh.getNodeName();
  return fnvBytes(2166136261u, name, strlen(name));
}

uint32_t meshBlePin() { return the_mesh.getNodePrefs()->ble_pin; }

bool meshBlePinSet(uint32_t pin) {
  // CMD_SET_DEVICE_PIN's own validation: zero, or a six-digit PIN. The generator never makes
  // 123456; a PIN the user typed on the phone is theirs to choose.
  if (pin != 0 && (pin < 100000 || pin > 999999)) return false;
  the_mesh.getNodePrefs()->ble_pin = pin;
  the_mesh.savePrefs();          // the same save the phone's command performs
  the_mesh.setBLEPin(pin);       // and the active one, so the next enable uses it
  return true;
}

void meshBlePinActivate() { the_mesh.setBLEPin(the_mesh.getNodePrefs()->ble_pin); }

uint32_t meshPhoneChangeSerial() { return s_phoneSerial; }

bool meshPhoneChangeTake(uint8_t* change) {
  if (s_phoneEventCount <= 0) return false;
  if (change != nullptr) *change = s_phoneEvents[0];
  for (int i = 1; i < s_phoneEventCount; ++i) s_phoneEvents[i - 1] = s_phoneEvents[i];
  --s_phoneEventCount;
  return true;
}

void meshPhoneChannelSent(uint8_t channelIdx, const char* text) {
  if (!s_ready || text == nullptr || text[0] == 0) return;
  const int ui = uiIndexForSlot(channelIdx);
  if (ui < 0) {
    Serial.printf("[ble] phone sent to slot %u, not in our table, not shown\n", (unsigned)channelIdx);
    return;
  }
  const uint32_t seq = s_msgRing.alloc();
  Msg& m = s_msgs[SeqRing<kMsgRing>::slotOf(seq)];
  memset(&m, 0, sizeof(m));
  m.seq = seq;
  m.ch = (uint8_t)ui;
  m.mine = 1;
  m.ts = millis() / 1000;
  m.wall = timeKeeper().now();
  m.wallReal = timeKeeper().realSinceBoot() ? 1 : 0;
  m.hops = -1;
  snprintf(m.name, sizeof(m.name), "%s", the_mesh.getNodeName());
  snprintf(m.text, sizeof(m.text), "%s", text);
  // EST-348: our own message is stored (mine, no unread Meta), exactly like a send from the glass.
  m.logId = history::store().appendMsg(histChannelSlot(ui), histFromChannel(m), 0);
  ++s_storeSerial;
  Serial.printf("[ble] phone sent ch%d: %s\n", ui, m.text);
}

void meshPhoneDmSent(const uint8_t* prefix, uint8_t prefixLen, const char* text) {
  if (!s_ready || prefix == nullptr || text == nullptr || text[0] == 0) return;
  ContactInfo* ci = the_mesh.lookupContactByPubKey(prefix, prefixLen);
  if (ci == nullptr) {
    Serial.println("[ble] phone sent a DM to an unknown contact, not shown");
    return;
  }
  const int thread = ensureDmThread(*ci);
  DmMsg& m = *s_dmRing->alloc((uint8_t)(thread < 0 ? om::ui::kDmNoThread : thread), true);
  // We did not compose the frame, so we cannot know the ACK it expects; "sending" is the honest
  // state (MyMesh confirms it on the phone side, and our ACK table does not track this frame).
  m.state = (uint8_t)om::ui::DmState::Sending;
  m.ts = millis() / 1000;
  m.wall = timeKeeper().now();
  m.wallReal = timeKeeper().realSinceBoot() ? 1 : 0;
  m.hops = -1;
  snprintf(m.name, sizeof(m.name), "%s", the_mesh.getNodeName());
  snprintf(m.text, sizeof(m.text), "%s", text);
  m.logId = history::store().appendMsg(histDmSlot(thread), histFromDm(m), 0);
  if (thread >= 0) s_dmRing->clearUnread(thread);
  Serial.printf("[ble] phone sent a DM to %s: %s\n", ci->name, m.text);
}

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
  const int slot = meshChannelSlot(channelIdx);
  if (slot < 0 || !the_mesh.getChannel(slot, cd)) {
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
    // EST-348: our own message is stored too (mine, no unread Meta).
    m.logId = history::store().appendMsg(histChannelSlot(channelIdx), histFromChannel(m), 0);
    ++s_storeSerial;
  }
  return ok;
}

}  // namespace om

#endif  // OM_ENABLE_MESH
