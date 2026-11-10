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
#include "mesh_text.h"
#include "radio_fault.h"
#include "target.h"
#include "timekeeper.h"

// The two upstream limits the rest of the file is sized from. A MeshCore upgrade that changes
// either stops the build here instead of silently cutting a message or a name.
static_assert(om::kMeshTextMax == MAX_TEXT_LEN, "mesh_text.h must follow BaseChatMesh.h MAX_TEXT_LEN");
static_assert(om::kMeshNameMax == sizeof(ContactInfo::name), "mesh_text.h must follow ContactInfo::name");
// EST-341: the global MyMesh was instantiated with BaseChatMesh's 32-contact default (15,312 B)
// while MyMesh.cpp wrote it as the 100-contact layout (28,096 B). `_store` was read from an
// uninitialised offset, the first savePrefs() called through a NULL DataStore and the boot
// panicked at [mc] 3 store + mesh begin. One define must reach every TU, libraries included.
static_assert(MAX_CONTACTS == 100,
              "MAX_CONTACTS must be 100 for every TU: -D MAX_CONTACTS=100 in platformio.ini "
              "(lib/meshcore is compiled without MyMesh.h)");

namespace om {
namespace {

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
uint32_t s_msgSeq = 0;
int s_msgCount = 0;
uint8_t s_unread[8] = {0};
uint32_t s_storeSerial = 0;   // changes on every stored message (channel or DM)

int slotFor(int idx) {
  return (int)((s_msgSeq - (uint32_t)s_msgCount + (uint32_t)idx) % (uint32_t)kMsgRing);
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

// --- direct messages -----------------------------------------------------------------------------
// Stored here (not in MeshCore) because the UI owns the presentation: one thread per contact,
// the same seq / wall / real / hops / SNR rules as channel messages, an unread count per thread.
// The DM chat screen and the sheet's "Direct message" row are the next ticket; this is the data.
struct DmMsg {
  uint8_t thread;
  int8_t hops;
  int8_t snr4;
  uint8_t pathHashSize;
  uint8_t pathCount;
  uint32_t seq;
  uint32_t ts;
  uint32_t wall;
  uint8_t wallReal;
  char name[kMeshNameCap];
  char text[kMeshTextCap];
  uint8_t path[MAX_PATH_SIZE];
};
constexpr int kDmRing = 32;
DmMsg s_dms[kDmRing];
uint32_t s_dmSeq = 0;
int s_dmMsgCount = 0;

struct DmThread {
  uint8_t key[PUB_KEY_SIZE];
  char name[kMeshNameCap];
  uint16_t unread;
  uint32_t lastWall;
  bool used;
};
constexpr int kDmThreads = 16;
DmThread s_dmThreads[kDmThreads];
int s_dmThreadCount = 0;

int findDmThread(const uint8_t* key) {
  for (int i = 0; i < kDmThreads; ++i) {
    if (s_dmThreads[i].used && memcmp(s_dmThreads[i].key, key, PUB_KEY_SIZE) == 0) return i;
  }
  return -1;
}

int ensureDmThread(const ContactInfo& from) {
  int idx = findDmThread(from.id.pub_key);
  if (idx >= 0) return idx;
  for (int i = 0; i < kDmThreads; ++i) {
    if (!s_dmThreads[i].used) {
      s_dmThreads[i].used = true;
      memcpy(s_dmThreads[i].key, from.id.pub_key, PUB_KEY_SIZE);
      snprintf(s_dmThreads[i].name, sizeof(s_dmThreads[i].name), "%s", from.name);
      s_dmThreads[i].unread = 0;
      s_dmThreads[i].lastWall = 0;
      ++s_dmThreadCount;
      return i;
    }
  }
  return -1;   // table full: the message is still counted in the ring, just without a thread
}

void storeDm(const ContactInfo& from, mesh::Packet* pkt, const char* text) {
  const int thread = ensureDmThread(from);
  const int slot = (int)(s_dmSeq % (uint32_t)kDmRing);
  DmMsg& m = s_dms[slot];
  memset(&m, 0, sizeof(m));
  m.thread = (uint8_t)(thread < 0 ? 0 : thread);
  m.seq = ++s_dmSeq;
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
  if (s_dmMsgCount < kDmRing) ++s_dmMsgCount;
  ++s_storeSerial;
  if (thread >= 0) {
    DmThread& t = s_dmThreads[thread];
    if (t.unread < 0xFFFF) ++t.unread;
    t.lastWall = m.wall;
    snprintf(t.name, sizeof(t.name), "%s", from.name);
  }
  Serial.printf("[mc] DM from %s: %s (hops %d snr %.2f)\n", from.name, text ? text : "",
                (int)m.hops, (double)m.snr4 / 4.0);
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

class OmListener : public MyMesh::Listener {
 public:
  void onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type,
                     uint32_t, const char* text) override {
    if (txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_SIGNED_PLAIN) {
      storeDm(from, pkt, text);
    }
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
    const int slot = slotFor(s_msgCount);
    Msg& m = s_msgs[slot];
    memset(&m, 0, sizeof(m));
    m.seq = s_msgSeq + 1;
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
    if (s_msgCount < kMsgRing) ++s_msgCount;
    ++s_msgSeq;
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

  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t, const uint8_t*) override {
    markContactStamp(contact);
    if (is_new) {
      s_newContact = true;
      s_newContactAt = millis();
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
}

bool meshLinkReloadChannels() {
  if (!s_prefsBegun) return false;
  pushChannelsFromNvs(true);
  return true;
}

// --- message accessors ---------------------------------------------------------------------------
int meshMsgCount() { return s_msgCount; }
uint32_t meshMsgSerial() { return s_storeSerial; }

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

// --- direct messages -----------------------------------------------------------------------------
int meshDmCount() { return s_dmThreadCount; }

const char* meshDmName(int idx) {
  if (idx < 0 || idx >= kDmThreads) return "";
  if (!s_dmThreads[idx].used) return "";
  return s_dmThreads[idx].name;
}

int meshDmUnread(int idx) {
  if (idx < 0 || idx >= kDmThreads) return 0;
  if (!s_dmThreads[idx].used) return 0;
  return (int)s_dmThreads[idx].unread;
}

bool meshDmAvailable() { return s_dmThreadCount > 0; }

// --- node DB -------------------------------------------------------------------------------------
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
    for (const char* p = ci.name; *p; ++p) h = (h ^ (uint8_t)*p) * 16777619u;
  }
  return h;
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
  tables.beginOwnSend(s_msgSeq + 1);
  const bool ok = the_mesh.sendGroupMessage(meshNow(), cd.channel, the_mesh.getNodeName(), text,
                                            (int)strlen(text));
  tables.endOwnSend(ok);
  Serial.printf("[mc] sendGroupMessage -> %d\n", (int)ok);
  if (ok) {
    const int slot = slotFor(s_msgCount);
    Msg& m = s_msgs[slot];
    memset(&m, 0, sizeof(m));
    m.seq = s_msgSeq + 1;
    m.ch = (uint8_t)channelIdx;
    m.mine = 1;
    m.ts = millis() / 1000;
    m.wall = timeKeeper().now();
    m.wallReal = timeKeeper().realSinceBoot() ? 1 : 0;
    m.hops = -1;
    snprintf(m.name, sizeof(m.name), "%s", the_mesh.getNodeName());
    snprintf(m.text, sizeof(m.text), "%s", text);
    if (s_msgCount < kMsgRing) ++s_msgCount;
    ++s_msgSeq;
    ++s_storeSerial;
  }
  return ok;
}

}  // namespace om

#endif  // OM_ENABLE_MESH
