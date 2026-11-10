// SPDX-License-Identifier: MIT
// OM Lite <-> MeshCore channel link.
#pragma once

#include <Arduino.h>

#if defined(OM_ENABLE_MESH)
namespace om {
bool meshLinkBegin();
void meshLinkLoop();
bool meshLinkReady();
const char* meshLinkLastMessage();
int meshLinkLastChannel();
bool meshLinkSend(int channelIdx, const char* text);
// Re-apply the channel table to the running mesh (after an add, remove or rename). Safe to call any
// time after meshLinkBegin(); unused slots are emptied so a removed channel stops matching.
bool meshLinkReloadChannels();

// Recent channel traffic for the UI (oldest first).
int meshMsgCount();
uint32_t meshMsgSerial();   // monotonic: changes on every stored message, even when the ring is full
const char* meshMsgText(int idx);
int meshMsgChannel(int idx);
const char* meshMsgName(int idx);
bool meshMsgMine(int idx);
uint32_t meshMsgTs(int idx);   // uptime seconds when it was stored (monotonic, clock-jump proof)
uint32_t meshMsgWallTs(int idx);  // system-clock seconds at the same moment (0 = unknown)
bool meshMsgWallReal(int idx);    // the clock was a real source at that moment
int meshMsgHops(int idx);      // path hops the packet carried, -1 = local echo / unknown
int meshMsgSnrQ(int idx);      // SNR x 4 as int8 (MeshCore's own unit), 0 = unknown
// The packet's path as it arrived (the repeaters it came over), valid while the message is in the
// ring: `count` hashes of `hashSize` bytes each, 0 count = heard directly. nullptr for a bad index.
const uint8_t* meshMsgPath(int idx, uint8_t* hashSize, uint8_t* count);
// Own messages only: how many times we heard our own packet repeated by others. 0 for received
// messages and before the first repeat.
int meshMsgHeardRepeats(int idx);
// Monotonic sequence number of a stored message (1, 2, 3...). Stable across ring shifts: the UI holds
// this instead of an index, so REPLY / INFO / an open menu cannot move to another message.
uint32_t meshMsgSeq(int idx);
int meshUnread(int channelIdx);
void meshClearUnread(int channelIdx);

// Direct messages, now fed by upstream's companion code: one thread per contact, with the same
// seq / wall / real / hops / SNR rules as channel messages and an unread count. The DM chat screen
// and the sheet's "Direct message" row are the next ticket; the data is here.
int meshDmCount();
const char* meshDmName(int idx);
int meshDmUnread(int idx);   // messages received from that contact since it was last cleared
// Whether a DM can actually be opened at all: false while the DM store is empty, so the message
// sheet leaves "Direct message" out instead of offering a row that cannot work.
bool meshDmAvailable();

// The name this node sends with, from MeshCore's NodePrefs (the phone can change it later; the
// composer budget must read this, not a constant).
const char* meshNodeName();

// --- path hash size (every packet we SEND) ---------------------------------------------------------
// MeshCore's NodePrefs::path_hash_mode (0 = 1B, 1 = 2B, 2 = 3B) is the single source now; it is
// seeded from our old NVS "phs" at the migration and every MyMesh send reads it. The old NVS key is
// kept in sync for a rollback. The received side needs nothing: MeshCore reads the size per packet.
uint8_t meshPathHashBytes();
bool meshPathHashSet(uint8_t bytes);   // 1..3; saved in NodePrefs, false for a bad value
const char* meshPathHashLabel();       // "1B" / "2B" / "3B", for Settings > Radio

// --- node DB for the Nodes screen -----------------------------------------------------------------
// The MeshCore contact table, read-only. type is MeshCore's ADV_TYPE_* (1 chat/client, 2 repeater,
// 3 room, 4 sensor). path_len is ContactInfo::out_path_len in bytes (0 = direct, 0xFF = unknown);
// this firmware does not get a per-contact RSSI from MeshCore, so the UI must not invent one.
struct MeshContact {
  char name[32];
  uint8_t type;
  uint8_t path_len;
  uint32_t lastmod;      // MeshCore RTC seconds when it was last heard
  uint32_t lastmodUp;    // uptime seconds at that moment (0 = loaded from flash, unknown)
  bool lastmodReal;      // the clock was a real source when lastmod was written
  bool has_pos;
  int32_t lat_e6;
  int32_t lon_e6;
};

// Newest first, at most max. Returns how many valid contacts were written (0 is a real answer:
// nothing has been heard yet).
int meshContactList(MeshContact* out, int max);
// One contact by its exact name (a message's sender is a contact's own name). False when there is no
// such contact, so "Node details" is only offered for a sender that really exists.
bool meshContactByName(const char* name, MeshContact* out);
// Name one packet-path hash: the repeater whose public key starts with these bytes, copied into buf.
// "" when no known repeater matches (MeshCore's path hash is the first 1..3 bytes of the pub_key).
const char* meshPathHopName(const uint8_t* hash, uint8_t len, char* buf, size_t cap);
// Cheap change detector: count, names, types and lastmod. Changes when the list would change.
uint32_t meshContactSignature();
// MeshCore's current clock, for age arithmetic. 0 when the mesh is not up.
uint32_t meshNow();
}
#endif  // OM_ENABLE_MESH
