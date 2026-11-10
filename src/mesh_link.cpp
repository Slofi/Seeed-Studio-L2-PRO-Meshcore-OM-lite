// SPDX-License-Identifier: MIT
// MeshCore channel link for OM Lite.
//
// Every object is created lazily inside meshLinkBegin(): nothing may run before setup(), because a
// fault in a global constructor happens before USB CDC exists and is then invisible. A crash during
// init leaves a flag in NVS; the next boot sees it, skips the mesh and stays reachable over OTA.
// Send "MESHON" over serial to clear the flag and retry (the device reboots).
#include "mesh_link.h"

#if defined(OM_ENABLE_MESH)

#include <Preferences.h>
#include <SPIFFS.h>

#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/radiolib/CustomSX1262.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>

#include "audio.h"
#include "channels.h"
#include "contact_store.h"
#include "mc_board.h"
#include "mesh_text.h"
#include "radio_fault.h"
#include "target.h"
#include "timekeeper.h"

namespace om {
namespace {

// The ring and every copy in this file are sized from MeshCore's own limits (mesh_text.h). The two
// static_asserts tie them to the upstream constants, so a MeshCore upgrade that widens MAX_TEXT_LEN or
// the contact name stops the build here instead of silently truncating a message again (EST-334).
static_assert(kMeshTextMax == MAX_TEXT_LEN, "mesh_text.h must follow BaseChatMesh.h MAX_TEXT_LEN");
static_assert(kMeshNameMax == sizeof(ContactInfo::name), "mesh_text.h must follow ContactInfo::name");

char s_lastText[kMeshTextCap];
int s_lastChannel = -1;
bool s_ready = false;

// Path hash size for every packet this node SENDS (MeshCore's sendFlood parameter): 1..3 bytes, 2 by
// default because SLO runs 2-byte hashes and enforces them. NVS-backed ("phs" in "mcnet"); the
// received side needs nothing, MeshCore reads the size from each packet.
uint8_t s_pathHashBytes = 2;
char s_pathHashLabel[4] = "2B";
bool s_prefsBegun = false;

// Contact persistence (see contact_store.h). The scheduler lives here so the callbacks below only
// mark it dirty; the actual SPIFFS write happens at most once per interval, from meshLinkLoop().
// static_assert ties the file format's record capacity to MeshCore's contact table.
static_assert(kMaxContactRecords >= MAX_CONTACTS, "contact file must hold every real contact");
static_assert(kContactKeySize == PUB_KEY_SIZE, "key size");
static_assert(kContactPathSize == MAX_PATH_SIZE, "path size");

constexpr const char* kContactsPath = "/contacts";
constexpr const char* kContactsTmpPath = "/contacts.tmp";
ContactSaveScheduler s_contactSaver;
// Shared by load and save, never both at once: 4.9 KB + 4.9 KB would not fit the loop task stack
// and two copies of each would waste 9.8 KB of static RAM.
uint8_t s_contactFileBuf[kContactMaxFileSize];
StoredContact s_contactRecs[kMaxContactRecords];

struct Msg {
  uint8_t ch;
  uint8_t mine;
  int8_t hops;     // packet path hops, -1 = local echo / unknown
  int8_t snr4;     // SNR x 4 (MeshCore's unit), 0 = unknown
  uint8_t pathHashSize;   // bytes per path hash of the received packet
  uint8_t pathCount;      // repeaters the packet came over
  uint8_t repeats;        // own message: times we heard our packet repeated by others
  uint32_t ts;     // uptime seconds when it was stored (monotonic: a GPS sync cannot disturb ages)
  uint32_t wall;   // system-clock seconds at that moment (0 = unknown)
  uint8_t wallReal;   // the clock was a real source then; the age fallback needs this
  uint32_t seq;    // stable message id across ring shifts, see meshMsgSeq()
  char name[kMeshNameCap];
  char text[kMeshTextCap];
  uint8_t path[MAX_PATH_SIZE];   // the packet's path as it arrived, pathCount * pathHashSize
};
constexpr int kMsgRing = 32;
Msg s_msgs[kMsgRing];
uint32_t s_msgSeq = 0;   // messages ever stored; the next slot is s_msgSeq % kMsgRing
int s_msgCount = 0;      // readable messages, capped at kMsgRing
uint8_t s_unread[8] = {0};

// Per-contact stamp bookkeeping. MeshCore's ContactInfo has lastmod but no room for "was the clock
// real when that was written", and no uptime. This small side table is filled from
// onDiscoveredContact (lastmod is already set there, checked in BaseChatMesh.cpp) and read by
// fillContact(). A contact restored from flash has no entry: its lastmod is judged by the fixed
// 2025-01-01 cut-off in ui/age.h, exactly like the old 2024 clock.
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

// MeshTables with one extra job: counting the times our OWN packet is heard again, repeated by
// someone else. This uses only MeshCore's public virtual interface (markSeen/wasSeen), no library
// edit: MeshCore calls markSeen() on the packet it is about to send, so while our send is in flight
// that hash is remembered as ours; when a copy of it comes back from a repeater, wasSeen() is the
// first thing MeshCore calls, and it lands here before the duplicate is dropped. The repeat is then
// counted on the local echo that carries the same message id.
class OmMeshTables : public SimpleMeshTables {
 public:
  // seq = the message id the local echo will get (s_msgSeq + 1 when the send succeeds).
  void beginOwnSend(uint32_t seq) {
    s_pendingSeq = seq;
    s_pendingSlot = -1;
  }
  void endOwnSend(bool stored) {
    if (!stored && s_pendingSlot >= 0) {
      s_own[s_pendingSlot].seq = 0;   // the send failed, so the echo was not stored
    }
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
        const int slot = (int)(s_own[i].seq % (uint32_t)kMsgRing);
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

// idx 0 is the oldest readable message. Deriving the slot from s_msgSeq (not from the count)
// keeps the order right after the ring wraps; the old version silently reordered at 32.
int slotFor(int idx) {
  return (int)((s_msgSeq - (uint32_t)s_msgCount + (uint32_t)idx) % (uint32_t)kMsgRing);
}

class OmChat : public BaseChatMesh {
public:
  OmChat(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc,
         mesh::PacketManager& mgr, mesh::MeshTables& tables)
      : BaseChatMesh(radio, ms, rng, rtc, mgr, tables) {}

  bool beginStore() {
    Serial.println("[mc] 6a BaseChatMesh::begin");
    BaseChatMesh::begin();
    Serial.println("[mc] 6b identity store");
    IdentityStore store(SPIFFS, "/identity");
    store.begin();   // mkdir /identity
    // Two-argument load: the 4-arg overload writes display_name[n-1] and crashes with a 0-size buffer.
    if (!store.load("_main", self_id)) {
      ((StdRNG*)getRNG())->begin(millis());
      self_id = mesh::LocalIdentity(getRNG());
      int tries = 0;
      while (tries < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) {
        self_id = mesh::LocalIdentity(getRNG());
        ++tries;
      }
      Serial.println("[mc] 6c new identity");
      store.save("_main", self_id);
    }
    Serial.println("[mc] 6e contacts");
    loadContacts();
    return true;
  }

  // --- contact persistence ---------------------------------------------------------------------
  // The table is read and written only through BaseChatMesh's public API (getContactByIdx,
  // addContact, getTotalContactSlots). The file format itself is ours (src/contact_store.h).
  bool loadContacts() {
    if (!SPIFFS.exists(kContactsPath)) {
      Serial.println("[mc] contacts: none stored, starting empty");
      return true;
    }
    File f = SPIFFS.open(kContactsPath, "r");
    if (!f) {
      Serial.println("[mc] contacts: open failed, starting empty");
      return false;
    }
    const size_t len = (size_t)f.size();
    if (len < kContactHeaderSize || len > kContactMaxFileSize) {
      Serial.printf("[mc] contacts: corrupt file (%u bytes), starting empty\n", (unsigned)len);
      f.close();
      return false;
    }
    // File-scope buffer, not stack: 4.9 KB would not fit the loop task's stack.
    const size_t got = (size_t)f.read(s_contactFileBuf, len);
    f.close();
    if (got != len) {
      Serial.printf("[mc] contacts: short read %u of %u, starting empty\n", (unsigned)got,
                    (unsigned)len);
      return false;
    }

    const int n = contactDecode(s_contactFileBuf, len, s_contactRecs, kMaxContactRecords);
    if (n < 0) {
      Serial.println("[mc] contacts: corrupt file (magic/version/size/CRC), starting empty");
      return false;
    }
    const int total = getTotalContactSlots();
    if (total + n > MAX_ANON_CONTACTS + MAX_CONTACTS) {
      Serial.printf("[mc] contacts: %d stored but the table has %d free slots, starting empty\n", n,
                    MAX_ANON_CONTACTS + MAX_CONTACTS - total);
      return false;
    }

    int added = 0;
    for (int i = 0; i < n; ++i) {
      ContactInfo ci{};
      memcpy(ci.id.pub_key, s_contactRecs[i].pub_key, sizeof(ci.id.pub_key));
      snprintf(ci.name, sizeof(ci.name), "%s", s_contactRecs[i].name);
      ci.type = s_contactRecs[i].type;
      ci.flags = s_contactRecs[i].flags;
      ci.out_path_len = s_contactRecs[i].out_path_len;
      memcpy(ci.out_path, s_contactRecs[i].out_path, sizeof(ci.out_path));
      ci.last_advert_timestamp = s_contactRecs[i].last_advert_ts;
      ci.lastmod = s_contactRecs[i].lastmod;
      ci.gps_lat = s_contactRecs[i].gps_lat;
      ci.gps_lon = s_contactRecs[i].gps_lon;
      ci.sync_since = s_contactRecs[i].sync_since;
      if (addContact(ci)) ++added;
    }
    // Do NOT call MeshCore's bootstrapRTCfromContacts(): it sets the clock to latest contact lastmod+1,
    // which is a timestamp from an older boot (2024-era under VolatileRTCClock) and would move the
    // new floor-backed clock backwards. Our clock already starts at max(saved floor, build time).
    Serial.printf("[mc] contacts: loaded %d of %d from flash\n", added, n);
    return added == n;
  }

  bool saveContacts() {
    int n = 0;
    const int total = getTotalContactSlots();
    for (int idx = MAX_ANON_CONTACTS; idx < total && n < kMaxContactRecords; ++idx) {
      ContactInfo ci;
      if (!getContactByIdx((uint32_t)idx, ci)) continue;
      if (ci.type == ADV_TYPE_NONE || ci.name[0] == 0) continue;
      StoredContact& r = s_contactRecs[n++];
      memset(&r, 0, sizeof(r));
      memcpy(r.pub_key, ci.id.pub_key, sizeof(r.pub_key));
      snprintf(r.name, sizeof(r.name), "%s", ci.name);
      r.type = ci.type;
      r.flags = ci.flags;
      r.out_path_len = ci.out_path_len;
      memcpy(r.out_path, ci.out_path, sizeof(r.out_path));
      r.last_advert_ts = ci.last_advert_timestamp;
      r.lastmod = ci.lastmod;
      r.gps_lat = ci.gps_lat;
      r.gps_lon = ci.gps_lon;
      r.sync_since = ci.sync_since;
    }

    const size_t len = contactEncode(s_contactRecs, n, s_contactFileBuf, sizeof(s_contactFileBuf));
    if (len == 0) {
      Serial.println("[mc] contacts: encode failed, not saved");
      return false;
    }
    // Write a temp file and swap it in: a power cut mid-write leaves the previous file intact
    // instead of a half table. A cut between remove and rename leaves no file, and the next boot
    // starts empty (logged), which is the documented fallback.
    File f = SPIFFS.open(kContactsTmpPath, "w");
    if (!f) {
      Serial.println("[mc] contacts: cannot open temp file, not saved");
      return false;
    }
    const size_t put = (size_t)f.write(s_contactFileBuf, len);
    f.close();
    if (put != len) {
      Serial.printf("[mc] contacts: short write %u of %u, not saved\n", (unsigned)put,
                    (unsigned)len);
      SPIFFS.remove(kContactsTmpPath);
      return false;
    }
    if (SPIFFS.exists(kContactsPath)) SPIFFS.remove(kContactsPath);
    if (!SPIFFS.rename(kContactsTmpPath, kContactsPath)) {
      Serial.println("[mc] contacts: rename failed, not saved");
      SPIFFS.remove(kContactsTmpPath);
      return false;
    }
    Serial.printf("[mc] contacts: saved %d (%u bytes)\n", n, (unsigned)len);
    return true;
  }

  void sendAdvert() {
    mesh::Packet* pkt = createAdvert(self_id);
    // A flood with OUR configured path hash size, like upstream's companion does for
    // CMD_SEND_SELF_ADVERT: sendPacket() would leave the route type and the hash size at whatever
    // createAdvert() left them (1-byte hashes, transport-flood route).
    if (pkt) sendFlood(pkt, 0, s_pathHashBytes);
  }

  // MeshCore's own sendFloodScoped() calls sendFlood(pkt, delay) and so always sends 1-byte path
  // hashes, whatever the node is configured for. Both variants are virtual for exactly this: pass
  // the configured size, the way upstream's companion does (_prefs.path_hash_mode + 1). Every
  // channel message, ACK, path reply and direct-message flood goes through these.
  void sendFloodScoped(const ContactInfo&, mesh::Packet* pkt, uint32_t delay_millis) override {
    sendFlood(pkt, delay_millis, s_pathHashBytes);
  }
  void sendFloodScoped(const mesh::GroupChannel&, mesh::Packet* pkt, uint32_t delay_millis) override {
    sendFlood(pkt, delay_millis, s_pathHashBytes);
  }

  // Any advert that lands in the table, and every learned return path, marks it for a (rate
  // limited) save. New contacts, name/type/position updates and path updates all come through
  // onDiscoveredContact; onContactPathUpdated covers a path learned from a later packet.
  void onDiscoveredContact(ContactInfo& contact, bool, uint8_t, const uint8_t*) override {
    markContactStamp(contact);   // lastmod is set just before this call (BaseChatMesh.cpp)
    s_contactSaver.markDirty(millis());
  }
  ContactInfo* processAck(const uint8_t*) override { return nullptr; }
  void onContactPathUpdated(const ContactInfo&) override {
    s_contactSaver.markDirty(millis());
  }
  void onContactOverwrite(const uint8_t*) override { s_contactSaver.markDirty(millis()); }
  void onMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}
  void onCommandDataRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}
  void onCLICommandRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*, char*) override {}
  void onSignedMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t, const uint8_t*,
                           const char*) override {}
  uint32_t calcFloodTimeoutMillisFor(uint32_t) const override { return 6000; }
  uint32_t calcDirectTimeoutMillisFor(uint32_t, uint8_t) const override { return 6000; }
  void onSendTimeout() override {}
  uint8_t onContactRequest(const ContactInfo&, uint32_t, const uint8_t*, uint8_t, uint8_t*) override {
    return 0;
  }
  void onContactResponse(const ContactInfo&, const uint8_t*, uint8_t) override {}

  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t,
                            const char* text) override {
    s_lastChannel = -1;
    for (int i = 0; i < mcChannelCount(); ++i) {
      ChannelDetails cd;
      if (getChannel(i, cd) && memcmp(cd.channel.hash, channel.hash, sizeof(channel.hash)) == 0) {
        s_lastChannel = i;
        break;
      }
    }
    if (s_lastChannel < 0) {   // not one of ours (or a removed channel still in the mesh table)
      Serial.printf("[mc] message for an unknown channel, ignored: %s\n", text ? text : "");
      return;
    }
    // MeshCore dispatches one packet per matching slot, and an emptied slot still has a (zero-key) hash
    // that can collide with a real channel's 1/256 of the time. Drop an immediate repeat.
    {
      static int lastCh = -1;
      static uint32_t lastTs = 0;
      static char lastText[kMeshTextCap];
      const uint32_t now = millis() / 1000;
      if (s_lastChannel == lastCh && (now - lastTs) <= 2 &&
          strncmp(lastText, text ? text : "", kMeshTextMax) == 0) {
        Serial.println("[mc] duplicate dispatch, ignored");
        return;
      }
      lastCh = s_lastChannel;
      lastTs = now;
      snprintf(lastText, sizeof(lastText), "%s", text ? text : "");
    }
    snprintf(s_lastText, sizeof(s_lastText), "%s", text ? text : "");
    const int slot = slotFor(s_msgCount);
    s_msgs[slot].seq = s_msgSeq + 1;
    s_msgs[slot].ch = (uint8_t)(s_lastChannel < 0 ? 0 : s_lastChannel);
    s_msgs[slot].mine = 0;
    s_msgs[slot].ts = millis() / 1000;
    s_msgs[slot].wall = timeKeeper().now();
    s_msgs[slot].wallReal = timeKeeper().realSinceBoot() ? 1 : 0;
    // What the air gave us: hops travelled and SNR at the last hop (Dispatcher sets _snr on RX).
    s_msgs[slot].hops = pkt ? (int8_t)pkt->getPathHashCount() : -1;
    s_msgs[slot].snr4 = pkt ? (int8_t)lroundf(pkt->getSNR() * 4.0f) : 0;
    // The repeaters the packet came over: the hash bytes exactly as they arrived. MeshCore's own
    // size travels in the packet, so the UI names each entry by matching that prefix against a known
    // repeater's public key (the path hash IS the pub_key prefix, Identity.h).
    s_msgs[slot].repeats = 0;
    {
      uint8_t hs = pkt ? pkt->getPathHashSize() : 0;
      uint8_t hc = pkt ? pkt->getPathHashCount() : 0;
      if (pkt == nullptr || hs < 1 || hs > 3 || (int)hc * (int)hs > MAX_PATH_SIZE) {
        hs = 0;
        hc = 0;
      }
      s_msgs[slot].pathHashSize = hs;
      s_msgs[slot].pathCount = hc;
      memset(s_msgs[slot].path, 0, sizeof(s_msgs[slot].path));
      if (hc > 0) memcpy(s_msgs[slot].path, pkt->path, (size_t)hc * hs);
    }
    const char* body = text ? text : "";
    const char* sep = strstr(body, ": ");
    // "<name>: <text>" is one MAX_TEXT_LEN string on the air, so the name can be as long as
    // ContactInfo::name allows; anything longer than that separator is not a name.
    if (sep && (size_t)(sep - body) < sizeof(s_msgs[slot].name)) {
      const size_t n = (size_t)(sep - body);
      memcpy(s_msgs[slot].name, body, n);
      s_msgs[slot].name[n] = 0;
      body = sep + 2;
    } else {
      snprintf(s_msgs[slot].name, sizeof(s_msgs[slot].name), "?");
    }
    snprintf(s_msgs[slot].text, sizeof(s_msgs[slot].text), "%s", body);
    if (s_msgCount < kMsgRing) ++s_msgCount;
    ++s_msgSeq;
    if (s_lastChannel >= 0 && s_lastChannel < 8) ++s_unread[s_lastChannel];
    const bool muted = (s_lastChannel >= 0) && mcChannelMuted(s_lastChannel);
    Serial.printf("[mc] channel %d: %s (hops %d snr %.2f)%s\n", s_lastChannel, s_lastText,
                  (int)s_msgs[slot].hops, (double)s_msgs[slot].snr4 / 4.0,
                  muted ? " [muted]" : "");
    // A muted channel still stores the message and still counts unread: mute is about the chime only.
    if (!muted) {
      audioTone(1318, 70);
      audioTone(1760, 90);
    }
  }
};

// MeshCore's clock is the system clock. It used to be VolatileRTCClock, which restarted at 15 May
// 2024 on every boot (so other nodes dropped our adverts as a replay). OmRTCClock reads and writes
// what timekeeper.h owns; setCurrentTime() is plumbing and never marks a trusted source.
class OmRTCClock : public mesh::RTCClock {
 public:
  uint32_t getCurrentTime() override { return om::timeKeeper().now(); }
  void setCurrentTime(uint32_t time) override { om::timeKeeper().setHardware(time); }
  void tick() override {}
};

// MeshCore's own send path queues a packet and reports success before the radio has transmitted;
// RadioLibWrapper::startSendRaw() is where the SX1262 actually answers. This subclass is the only
// place the status bar's TX fault can be honest:
//   * startSendRaw() false: a RadioLib startTransmit error (no chip answer, bad params, ...) -> fault;
//   * a send that never completes inside kTxTimeoutMs (RadioLib TX timeout) -> fault;
//   * onSendFinished(): the TX completed, so the mark clears.
// Nothing here looks at received traffic: a quiet band can never set the mark.
class OmSX1262Wrapper : public CustomSX1262Wrapper {
 public:
  OmSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board)
      : CustomSX1262Wrapper(radio, board) {}

  bool startSendRaw(const uint8_t* bytes, int len) override {
    const bool ok = CustomSX1262Wrapper::startSendRaw(bytes, len);
    if (!ok) {
      txWaiting_ = false;
      txTimedOut_ = false;
      if (radioFaultState().set(RadioFault::Tx)) {
        Serial.printf("[radio] fault: %s\n", radioFaultToken(RadioFault::Tx));
      }
      return false;
    }
    txWaiting_ = true;
    txTimedOut_ = false;
    txStartMs_ = millis();
    return true;
  }

  bool isSendComplete() override {
    const bool done = CustomSX1262Wrapper::isSendComplete();
    if (done) {
      txWaiting_ = false;
      return true;
    }
    if (txWaiting_ && (uint32_t)(millis() - txStartMs_) > kTxTimeoutMs) {
      txWaiting_ = false;
      txTimedOut_ = true;   // a late completion still clears the mark in onSendFinished()
      if (radioFaultState().set(RadioFault::Tx)) {
        Serial.printf("[radio] fault: %s\n", radioFaultToken(RadioFault::Tx));
      }
    }
    return false;
  }

  void onSendFinished() override {
    CustomSX1262Wrapper::onSendFinished();
    const bool wasSending = txWaiting_ || txTimedOut_;
    txWaiting_ = false;
    txTimedOut_ = false;
    if (wasSending && radioFaultState().onTxOk()) {
      Serial.println("[radio] fault cleared");
    }
  }

 private:
  static constexpr uint32_t kTxTimeoutMs = 15000;   // longer than any legal packet's airtime
  bool txWaiting_ = false;
  bool txTimedOut_ = false;
  uint32_t txStartMs_ = 0;
};

// Lazily created; the constructor only runs inside meshLinkBegin(), after USB is up.
struct Ctx {
  Ctx() : mod(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi), radio(&mod) {}

  SPIClass spi{FSPI};
  Module mod;
  CustomSX1262 radio;
  OmBoard board;
  StdRNG rng;
  ArduinoMillis ms;
  OmRTCClock rtc;
  StaticPoolPacketManager mgr{16};
  OmMeshTables tables;
  CustomSX1262Wrapper* wrapper = nullptr;
  OmChat* mesh = nullptr;
};

Ctx* s_ctx = nullptr;
Preferences s_netPrefs;

// One place that sets the setting and the label together, so Settings > Radio can never disagree
// with what goes on the air.
void applyPathHashBytes(uint8_t bytes) {
  if (bytes < 1) bytes = 1;
  if (bytes > 3) bytes = 3;
  s_pathHashBytes = bytes;
  snprintf(s_pathHashLabel, sizeof(s_pathHashLabel), "%uB", (unsigned)bytes);
}

}  // namespace

bool meshLinkReady() { return s_ready; }
const char* meshLinkLastMessage() { return s_lastText; }
int meshLinkLastChannel() { return s_lastChannel; }

uint8_t meshPathHashBytes() { return s_pathHashBytes; }
const char* meshPathHashLabel() { return s_pathHashLabel; }

bool meshPathHashSet(uint8_t bytes) {
  if (bytes < 1 || bytes > 3) return false;
  applyPathHashBytes(bytes);
  if (s_prefsBegun) s_netPrefs.putUChar("phs", bytes);
  Serial.printf("[mc] path hash set to %s (saved)\n", s_pathHashLabel);
  return true;
}

bool meshLinkBegin() {
  if (s_ready) return true;

  s_netPrefs.begin("mcnet", false);
  s_prefsBegun = true;
  if (s_netPrefs.getBool("crashed", false)) {
    const uint8_t fails = (uint8_t)(s_netPrefs.getUChar("fails", 0) + 1);
    s_netPrefs.putUChar("fails", fails);
    Serial.printf("[mc] previous init crashed (fail %u/3)\n", fails);
    if (fails < 3) {
      // Retry once automatically: clear the flag, give OTA a moment, reboot.
      s_netPrefs.putBool("crashed", false);
      delay(1500);
      ESP.restart();
    }
    s_netPrefs.putBool("crashed", true);   // parked: stays up for OTA
    return false;
  }
  s_netPrefs.putBool("crashed", true);   // cleared only after a full successful init

  // The path hash size goes on the air with every packet, so it is read before the first advert.
  applyPathHashBytes(s_netPrefs.getUChar("phs", 2));
  Serial.printf("[mc] path hash %s (NVS phs=%u)\n", s_pathHashLabel,
                (unsigned)s_netPrefs.getUChar("phs", 2));

  Serial.println("[mc] 1 spiffs");
  if (!SPIFFS.begin(true)) {
    Serial.println("[mc] SPIFFS mount failed");
    return false;
  }

  Serial.println("[mc] 2 objects");
  s_ctx = new Ctx();
  if (!s_ctx) return false;

  Serial.println("[mc] 3 radio std_init");
  s_ctx->spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);
  if (!s_ctx->radio.std_init(&s_ctx->spi)) {
    Serial.println("[mc] radio init failed");
    // The chip itself did not answer, which outranks "MeshCore did not start": the status bar's
    // toast should name the radio, not the layer above it.
    if (radioFaultState().set(RadioFault::Radio)) {
      Serial.printf("[radio] fault: %s\n", radioFaultToken(RadioFault::Radio));
    }
    return false;
  }

  Serial.println("[mc] 4 tx power");
  s_ctx->radio.setOutputPower(LORA_TX_POWER);

  Serial.println("[mc] 5 wrapper");
  s_ctx->wrapper = new OmSX1262Wrapper(s_ctx->radio, s_ctx->board);
  if (!s_ctx->wrapper) return false;

  Serial.println("[mc] 6 chat object");
  s_ctx->mesh = new OmChat(*s_ctx->wrapper, s_ctx->ms, s_ctx->rng, s_ctx->rtc, s_ctx->mgr,
                           s_ctx->tables);
  if (!s_ctx->mesh) return false;

  s_ctx->mesh->beginStore();

  // The reference firmware sets the radio params AFTER mesh.begin(); doing it before left the
  // receive path unconfigured (TX worked, RX never delivered).
  Serial.println("[mc] 6d setParams + tx power (after begin)");
  s_ctx->wrapper->setParams(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR);
  s_ctx->wrapper->setTxPower(LORA_TX_POWER);

  Serial.println("[mc] 7 channels");
  meshLinkReloadChannels();

  // The chip answered std_init and the mesh is built, so a startup mark is resolved here, BEFORE the
  // first advert: an advert TX failure after this must still be able to set the mark.
  if (radioFaultState().onRadioReinit()) Serial.println("[radio] fault cleared");

  s_ready = true;   // before advert/hello, because meshLinkSend() checks it

  Serial.println("[mc] 8 advert");
  s_ctx->mesh->sendAdvert();

  // No automatic "OM Lite online" on boot: it put a message on a real channel on every power cycle
  // (Filip, S492). The advert above still announces the node without occupying a channel.

  s_netPrefs.putBool("crashed", false);
  s_netPrefs.putUChar("fails", 0);
  Serial.printf("[mc] up: %.3f MHz SF%u\n", LORA_FREQ, (unsigned)LORA_SF);
  return true;
}

void meshLinkLoop() {
  if (!s_ready || !s_ctx || !s_ctx->mesh) return;
  s_ctx->mesh->loop();

  // Persist the contact table after the mesh has processed this pass, at most once per
  // kContactSaveMinIntervalMs. A failed write stays dirty and is retried after the interval.
  const uint32_t now = millis();
  if (s_contactSaver.due(now)) {
    if (s_ctx->mesh->saveContacts()) s_contactSaver.onSaveOk(now);
    else s_contactSaver.onSaveFailed(now);
  }
}

bool meshLinkReloadChannels() {
  // Deliberately not gated on s_ready: this is also called from meshLinkBegin() while the mesh is
  // being brought up, before s_ready flips (a gate here left the node with no channels at all).
  if (!s_ctx || !s_ctx->mesh) return false;
  const int count = mcChannelCount();
  for (int i = 0; i < kMaxChannels; ++i) {
    ChannelDetails cd;
    memset(&cd, 0, sizeof(cd));
    if (i < count) {
      snprintf(cd.name, sizeof(cd.name), "%s", mcChannelName(i));
      memcpy(cd.channel.secret, mcChannel(i).secret, 16);
    }
    s_ctx->mesh->setChannel(i, cd);
    if (i < count) {
      // Print OUR hash: setChannel() recomputes it inside the mesh, so cd still holds zeros here.
      Serial.printf("[mc] channel %d '%s' hash %02x\n", i, mcChannelName(i), mcChannel(i).hash);
    }
  }
  return true;
}

int meshMsgCount() { return s_msgCount; }
uint32_t meshMsgSerial() { return s_msgSeq; }

const char* meshMsgText(int idx) {
  if (idx < 0 || idx >= s_msgCount) return "";
  return s_msgs[slotFor(idx)].text;
}

int meshMsgChannel(int idx) {
  if (idx < 0 || idx >= s_msgCount) return -1;
  return s_msgs[slotFor(idx)].ch;
}

const char* meshMsgName(int idx) {
  if (idx < 0 || idx >= s_msgCount) return "";
  return s_msgs[slotFor(idx)].name;
}

bool meshMsgMine(int idx) {
  if (idx < 0 || idx >= s_msgCount) return false;
  return s_msgs[slotFor(idx)].mine != 0;
}

uint32_t meshMsgTs(int idx) {
  if (idx < 0 || idx >= s_msgCount) return 0;
  return s_msgs[slotFor(idx)].ts;
}

uint32_t meshMsgWallTs(int idx) {
  if (idx < 0 || idx >= s_msgCount) return 0;
  return s_msgs[slotFor(idx)].wall;
}

bool meshMsgWallReal(int idx) {
  if (idx < 0 || idx >= s_msgCount) return false;
  return s_msgs[slotFor(idx)].wallReal != 0;
}

uint32_t meshMsgSeq(int idx) {
  if (idx < 0 || idx >= s_msgCount) return 0;
  return s_msgs[slotFor(idx)].seq;
}

int meshMsgHops(int idx) {
  if (idx < 0 || idx >= s_msgCount) return -1;
  return s_msgs[slotFor(idx)].hops;
}

int meshMsgSnrQ(int idx) {
  if (idx < 0 || idx >= s_msgCount) return 0;
  return s_msgs[slotFor(idx)].snr4;
}

const uint8_t* meshMsgPath(int idx, uint8_t* hashSize, uint8_t* count) {
  if (hashSize) *hashSize = 1;
  if (count) *count = 0;
  if (idx < 0 || idx >= s_msgCount) return nullptr;
  const Msg& m = s_msgs[slotFor(idx)];
  if (m.pathCount == 0) return nullptr;
  if (hashSize) *hashSize = m.pathHashSize;
  if (count) *count = m.pathCount;
  return m.path;
}

int meshMsgHeardRepeats(int idx) {
  if (idx < 0 || idx >= s_msgCount) return 0;
  return s_msgs[slotFor(idx)].repeats;
}

int meshUnread(int channelIdx) {
  if (channelIdx < 0 || channelIdx >= 8) return 0;
  return s_unread[channelIdx];
}

void meshClearUnread(int channelIdx) {
  if (channelIdx >= 0 && channelIdx < 8) s_unread[channelIdx] = 0;
}

// --- direct messages -------------------------------------------------------------------------------
// The DM store arrives with upstream's companion code (work order item 5: onMessageRecv is still an
// empty override). Until then there is no DM, and the chat selector says "No direct messages yet"
// instead of inventing a list. The UI asks this facade, never a placeholder list.
int meshDmCount() { return 0; }

const char* meshDmName(int idx) {
  (void)idx;
  return "";
}

// A DM row in the message sheet needs a real DM store behind it; false today, and the row is left
// out of the sheet rather than shown greyed (EST-336).
bool meshDmAvailable() { return meshDmCount() > 0; }

// --- node DB ---------------------------------------------------------------------------------------
// The UI gets a copy, never a pointer into contacts[]: the mesh may overwrite a slot on the next
// advert while the screen is being built.
namespace {
bool validContact(const ContactInfo& c) {
  return c.type != ADV_TYPE_NONE && c.name[0] != 0;
}

void fillContact(const ContactInfo& ci, MeshContact& mc) {
  mc = MeshContact{};
  snprintf(mc.name, sizeof(mc.name), "%s", ci.name);
  mc.type = ci.type;
  mc.path_len = ci.out_path_len;
  mc.lastmod = ci.lastmod;
  const ContactStamp* mark = findContactStamp(ci.id.pub_key);
  if (mark != nullptr) {
    mc.lastmodUp = mark->uptime;
    mc.lastmodReal = mark->real;
  } else {
    // Restored from flash: no uptime and no "was it real" bit survived. The fixed 2025-01-01 cut-off
    // is the fallback: a stamp inside the sane range is treated as real (the EST-338 rule), a 2024
    // stamp from the old VolatileRTCClock is not and prints "--".
    mc.lastmodUp = 0;
    mc.lastmodReal = saneEpoch(ci.lastmod);
  }
  mc.has_pos = (ci.gps_lat != 0 || ci.gps_lon != 0);
  mc.lat_e6 = ci.gps_lat;
  mc.lon_e6 = ci.gps_lon;
}
}  // namespace

int meshContactList(MeshContact* out, int max) {
  if (!out || max <= 0 || !s_ctx || !s_ctx->mesh) return 0;
  const int total = s_ctx->mesh->getTotalContactSlots();
  int n = 0;
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!s_ctx->mesh->getContactByIdx((uint32_t)idx, ci) || !validContact(ci)) continue;
    MeshContact mc;
    fillContact(ci, mc);
    // Insert newest first; the newest max contacts win when the table is larger than the view.
    int pos = n;
    while (pos > 0 && out[pos - 1].lastmod < mc.lastmod) --pos;
    if (pos >= max) continue;
    if (n < max) ++n;
    for (int j = n - 1; j > pos; --j) out[j] = out[j - 1];
    out[pos] = mc;
  }
  return n;
}

bool meshContactByName(const char* name, MeshContact* out) {
  if (name == nullptr || name[0] == 0 || out == nullptr || !s_ctx || !s_ctx->mesh) return false;
  const int total = s_ctx->mesh->getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!s_ctx->mesh->getContactByIdx((uint32_t)idx, ci) || !validContact(ci)) continue;
    if (strcmp(ci.name, name) != 0) continue;
    fillContact(ci, *out);
    return true;
  }
  return false;
}

// MeshCore's path hash is the first 1..3 bytes of the node's public key (Identity.h copyHashTo), so a
// path entry names a repeater when its prefix equals that repeater's pub_key prefix. Repeaters only:
// a room server or a client in the path stays hex rather than being named as something it is not.
const char* meshPathHopName(const uint8_t* hash, uint8_t len, char* buf, size_t cap) {
  if (buf == nullptr || cap == 0) return "";
  buf[0] = 0;
  if (hash == nullptr || len < 1 || len > MAX_HASH_SIZE || !s_ctx || !s_ctx->mesh) return buf;
  const int total = s_ctx->mesh->getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!s_ctx->mesh->getContactByIdx((uint32_t)idx, ci) || !validContact(ci)) continue;
    if (ci.type != ADV_TYPE_REPEATER) continue;
    if (memcmp(ci.id.pub_key, hash, len) != 0) continue;
    snprintf(buf, cap, "%s", ci.name);
    return buf;
  }
  return buf;
}

uint32_t meshContactSignature() {
  uint32_t h = 2166136261u;
  if (!s_ctx || !s_ctx->mesh) return h;
  const int total = s_ctx->mesh->getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!s_ctx->mesh->getContactByIdx((uint32_t)idx, ci) || !validContact(ci)) continue;
    h = (h ^ ci.lastmod) * 16777619u;
    h = (h ^ ci.type) * 16777619u;
    for (const char* p = ci.name; *p; ++p) h = (h ^ (uint8_t)*p) * 16777619u;
  }
  return h;
}

uint32_t meshNow() { return (s_ctx && s_ctx->mesh) ? s_ctx->mesh->getRTCClock()->getCurrentTime() : 0; }

bool meshLinkSend(int channelIdx, const char* text) {
  Serial.printf("[mc] send ch%d ready=%d ctx=%d mesh=%d\n", channelIdx, (int)s_ready,
                s_ctx != nullptr, s_ctx && s_ctx->mesh != nullptr);
  if (!s_ready || !s_ctx || !s_ctx->mesh || !text || !*text) return false;
  // MeshCore's sendGroupMessage() would quietly cut the text to fit MAX_TEXT_LEN. Refuse instead: the
  // composer already counts these bytes, so a cut here would only hide a counting bug (EST-334).
  if ((int)strlen(text) + kMeshOwnPrefix > kMeshTextMax) {
    Serial.printf("[mc] send: %u bytes + %d prefix > %d, refused\n", (unsigned)strlen(text),
                  kMeshOwnPrefix, kMeshTextMax);
    return false;
  }
  ChannelDetails cd;
  if (!s_ctx->mesh->getChannel(channelIdx, cd)) {
    Serial.println("[mc] send: no such channel");
    return false;
  }
  Serial.printf("[mc] send: hash %02x secret %02x textlen %d\n", cd.channel.hash[0],
                cd.channel.secret[0], (int)strlen(text));
  // Let the tables remember this packet's hash as OURS, so a copy repeated back by someone else can
  // be counted on the local echo below (MeshCore calls markSeen() on the outgoing packet).
  s_ctx->tables.beginOwnSend(s_msgSeq + 1);
  const bool ok = s_ctx->mesh->sendGroupMessage(meshNow(), cd.channel, kMeshOwnName, text,
                                                (int)strlen(text));
  s_ctx->tables.endOwnSend(ok);
  Serial.printf("[mc] sendGroupMessage -> %d\n", (int)ok);
  // The TX fault is NOT read from this return value: MeshCore queues the packet and returns true
  // before the radio transmits. OmSX1262Wrapper's startSendRaw()/isSendComplete()/onSendFinished()
  // are the real RadioLib result and own the mark (see the class above).
  if (ok) {
    // Locally echo what we sent, so the channel view shows the outgoing message too.
    const int slot = slotFor(s_msgCount);
    s_msgs[slot].seq = s_msgSeq + 1;
    s_msgs[slot].ch = (uint8_t)channelIdx;
    s_msgs[slot].mine = 1;
    s_msgs[slot].ts = millis() / 1000;
    s_msgs[slot].wall = timeKeeper().now();
    s_msgs[slot].wallReal = timeKeeper().realSinceBoot() ? 1 : 0;
    s_msgs[slot].hops = -1;   // we know it is local; the air has not told us anything yet
    s_msgs[slot].snr4 = 0;
    s_msgs[slot].pathHashSize = 0;
    s_msgs[slot].pathCount = 0;
    s_msgs[slot].repeats = 0;
    memset(s_msgs[slot].path, 0, sizeof(s_msgs[slot].path));
    snprintf(s_msgs[slot].name, sizeof(s_msgs[slot].name), "%s", kMeshOwnName);
    snprintf(s_msgs[slot].text, sizeof(s_msgs[slot].text), "%s", text);
    if (s_msgCount < kMsgRing) ++s_msgCount;
    ++s_msgSeq;
  }
  return ok;
}

}  // namespace om

#endif  // OM_ENABLE_MESH
