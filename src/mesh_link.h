// SPDX-License-Identifier: MIT
// OM Lite <-> MeshCore channel link.
#pragma once

#include <Arduino.h>

#include "history/history_log.h"
#include "radio_stats.h"
#include "ui/dm_ring.h"   // the DM store itself (plain C++, host-tested): EST-344

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

// --- EST-348: the on-card chat history -------------------------------------------------------------
// Called from setup() after the SD card has been tried. Restores the newest messages of every
// channel and of every DM log into the RAM rings, and the unread counts from the last Meta record.
// `sdOk` false (or a card that fails to mount) selects the bounded SPIFFS fallback.
bool meshHistoryBegin(bool sdOk);
// Older messages than `beforeLogId` for the open chat: a channel (thread < 0) or a DM thread.
// `out` must hold `cap`; the messages are oldest first, at most kScanRing (32).
int meshHistoryOlder(int channelIdx, int thread, uint32_t beforeLogId, history::Msg* out, int cap);
// USB `history <chat> [n]`: channel name or DM contact name / public-key prefix. Returns how many
// [hist] lines were printed, or -1 when no such chat exists.
int meshHistoryDump(const char* chat, int n);

// --- EST-367: a chat's own view, not the shared 32-message arrival ring ---------------------------
// The newest `cap` messages of a chat's own log (a channel, or a DM thread when thread >= 0), oldest
// first. One seek to the end of the file plus one backwards 4 KB window: the log is never scanned.
// Returns how many were written, or -1 when there is no such chat.
int meshHistoryNewest(int channelIdx, int thread, history::Msg* out, int cap);
// That chat's newest stored message (sender, text, clocks), for the selector's preview line. False
// when the chat has stored nothing yet.
bool meshChatSummary(int channelIdx, int thread, history::ChatSummary* out);
// How many messages the chat really has on the volume (.log + .old). Only the `chats` check asks:
// it reads the file once, so the boot and open paths never call it.
int meshHistoryCardMessages(int channelIdx, int thread, bool* complete = nullptr);
// The open chat's seen point moved to the newest message its view actually showed. Returns the
// unread left in the chat (0 when the view reached the newest stored message), and keeps the RAM
// unread counter (the badge, the selector) in step with the store.
int meshHistoryMarkSeen(int channelIdx, int thread, uint32_t throughLogId);

// Recent channel traffic for the UI (oldest first).
int meshMsgCount();
uint32_t meshMsgSerial();   // monotonic: changes on every stored CHANNEL message, even when the ring is full
                            // (DMs have their own serial: meshDmSerial/meshDmRecvSerial)
// EST-359: receive-only sound triggers. meshMsgSoundSerial changes for a received channel message on
// a channel that is not muted; meshMentionSerial changes instead when that text names us (@Name).
// Our own sends and muted channels bump neither.
uint32_t meshMsgSoundSerial();
uint32_t meshMentionSerial();
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
// EST-348: the on-card history id of a stored message (0 = not stored).
uint32_t meshMsgLogId(int idx);
int meshUnread(int channelIdx);
void meshClearUnread(int channelIdx);

// Direct messages, now fed by upstream's companion code: one thread per contact, with the same
// seq / wall / real / hops / SNR rules as channel messages and an unread count. A thread exists as
// soon as a DM arrives OR the user opens one from the Nodes menu, so an empty thread can be written
// to (EST-342).
int meshDmCount();
const char* meshDmName(int idx);
int meshDmUnread(int idx);   // messages received from that contact since it was last cleared
// Whether a DM can actually be opened at all: false while the DM store is empty, so the message
// sheet leaves "Direct message" out instead of offering a row that cannot work.
bool meshDmAvailable();
uint32_t meshDmSerial();               // changes on every DM stored, sent or delivered
// Changes only when a DM ARRIVES: what the DM chime listens to, so our own send stays silent.
uint32_t meshDmRecvSerial();
void meshDmClearUnread(int thread);

// One stored DM, copied out (the ring may shift while the screen draws). It is the ring's own
// message struct (ui/dm_ring.h), so there is exactly one layout and one allocation path. `state` is
// om::ui::DmState as a plain byte; the facade stays free of UI headers.
using MeshDm = ui::DmMsg;
// Oldest first, at most max. Returns how many of this thread's messages are in the ring.
int meshDmList(int thread, MeshDm* out, int max);
// EST-366: the newest RECEIVED DM, for the notification toast ("DM <sender>: ..."). False when the
// ring has no received message (or no ring at all). Our own sends are never returned.
bool meshDmLatest(int* threadOut, char* name, size_t nameCap, char* text, size_t textCap);

// The thread for a contact's exact name, creating an empty one when the contact is known (the Nodes
// menu's "Direct message"). False when there is no such contact, so no thread can exist.
bool meshDmOpenByName(const char* name, int* threadOut);
// Send one DM through BaseChatMesh::sendMessage (the companion's own path, its ACK and its timeout).
// The text is bounded by the DM frame budget; refusing here means nothing went on the air.
bool meshDmSend(int thread, const char* text);

// --- node operations (the Nodes long-press menu) -------------------------------------------------
// Ping: BaseChatMesh::sendRequest(REQ_TYPE_GET_STATUS) and the reply through
// Listener::onUnhandledResponse, so the round-trip is measured, not guessed.
enum class MeshPingState : uint8_t { Idle, Sent, Replied, Timeout, Failed };
bool meshNodePing(const char* name);
MeshPingState meshPingState(uint32_t* rttMs, char* nameOut, size_t nameCap);

// Trace: Mesh::createTrace() + sendDirect() along the contact's path, exactly the packet the
// companion's CMD_SEND_TRACE_PATH builds. The result arrives through Listener::onTraceRecv.
struct MeshTraceHop {
  char name[32];
  int8_t snr4;
  bool known;   // the hash matched a known repeater's public key
};
struct MeshTrace {
  MeshTraceHop hops[8];
  int count;
  int8_t finalSnr4;
  uint32_t rttMs;
  bool haveRtt;
  char node[32];
};
bool meshNodeTrace(const char* name);
uint32_t meshTraceSerial();
bool meshTraceResult(MeshTrace* out);

bool meshNodeResetPath(const char* name);
bool meshNodeFavourite(const char* name, bool on);
bool meshNodeIsFavourite(const char* name);
bool meshNodeRemove(const char* name);
// The RPTR tab exists in this build; the row is offered only for a repeater and only when it does.
bool meshRptrTabExists();

// --- the new-node notice -------------------------------------------------------------------------
// Adverts that ADD a contact queue one event each (name + advert type), for the toast, the dot on
// the Nodes tab and the per-event sound. Known contacts never queue.
uint32_t meshNewNodeSerial();
bool meshNewNodeTake(char* nameOut, size_t nameCap, uint8_t* advTypeOut);

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

// --- the radio prefs the Settings screen edits (EST-347) -----------------------------------------
// NodePrefs is the ONLY store. These calls mirror upstream's CMD_SET_RADIO_PARAMS,
// CMD_SET_RADIO_TX_POWER, CMD_SET_ADVERT_NAME and CMD_SET_OTHER_PARAMS handler bodies: write the
// same fields, MyMesh::savePrefs(), then the same radio apply (radio_driver.setParams / setTxPower)
// the command path performs. No second store exists, so the phone and the screen cannot disagree.
struct MeshRadioPrefs {
  uint32_t freq_khz;
  uint32_t bw_hz;
  uint8_t sf;
  uint8_t cr;
  int8_t tx;
};
bool meshRadioPrefsGet(MeshRadioPrefs* out);
// Upstream's ranges: freq 150000..2500000 kHz, bw 7000..500000 Hz, sf 5..12, cr 5..8. A bad value is
// refused whole (nothing is written, nothing goes on the air).
bool meshRadioPrefsSet(const MeshRadioPrefs& p);
bool meshTxPowerSet(int8_t dbm);          // -9..MAX_LORA_TX_POWER, saved and applied live
bool meshNodeNameSet(const char* name);   // the CMD_SET_ADVERT_NAME path, saved in NodePrefs
bool meshAdvertPosPolicyGet();            // NodePrefs::advert_loc_policy (CMD_SET_OTHER_PARAMS)
bool meshAdvertPosPolicySet(bool share);
// The first 8 hex characters of this node's public key, for Settings > Node.
bool meshPubKeyShort(char* out, size_t cap);

// --- node DB for the Nodes screen -----------------------------------------------------------------
// The MeshCore contact table, read-only. type is MeshCore's ADV_TYPE_* (1 chat/client, 2 repeater,
// 3 room, 4 sensor). path_hops is ContactInfo::out_path_len DECODED (mesh_path.h): the hop count
// (0 = direct) or -1 for OUT_PATH_UNKNOWN; the raw encoded byte is never handed to the UI.
// This firmware does not get a per-contact RSSI from MeshCore, so the UI must not invent one.
struct MeshContact {
  char name[32];
  uint8_t type;
  int8_t path_hops;
  uint32_t lastmod;      // MeshCore RTC seconds when it was last heard
  uint32_t lastmodUp;    // uptime seconds at that moment (0 = loaded from flash, unknown)
  bool lastmodReal;      // the clock was a real source when lastmod was written
  bool has_pos;
  int32_t lat_e6;
  int32_t lon_e6;
  bool has_key;          // a real contact always has a public key; false only for a bad record
  uint8_t pub_key[32];
  bool favourite;        // kept in our own NVS, favourites sort first
  // The path's bytes are NOT copied into this struct (64 bytes x 64 rows of RAM for a page that is
  // opened rarely): meshContactPathText() formats it on demand (EST-342).
  int8_t adv_snr4;       // SNR x 4 of the last advert; 0 = unknown (see PROGRESS.md)
  bool adv_snr_known;
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
// The path to reach a contact as text: "direct", "--" when unknown, or the named/hex hops joined by
// " > ". Decoded through mesh_path.h (hash size and hop count from the encoded byte), never from a
// raw out_path_len. False when there is no such contact.
bool meshContactPathText(const char* name, char* out, size_t cap);
// Cheap change detector: count, names, types and lastmod. Changes when the list would change.
uint32_t meshContactSignature();
// MeshCore's current clock, for age arithmetic. 0 when the mesh is not up.
uint32_t meshNow();

// --- EST-356: one channel table, the phone's -----------------------------------------------------
// The list is MyMesh's own channel table, read on every call: a channel the phone adds, renames or
// removes is what the screens draw on the next frame. A UI index maps to a MeshCore slot, so an
// empty slot left by a delete is skipped instead of shown as a nameless row.
int meshChannelCount();
int meshChannelSlot(int idx);
const char* meshChannelName(int idx);
bool meshChannelCopy(int idx, char* name, size_t cap, uint8_t secret[16], uint8_t* hash);
bool meshChannelSetName(int idx, const char* name);
int meshChannelAdd(const char* name, const uint8_t secret[16]);
bool meshChannelRemove(int idx);
// FNV-1a over every slot's name and secret. Changes when the channel table changes, ours or the
// phone's; the change watch uses it to tell the two apart.
uint32_t meshChannelSignature();

// --- EST-356: the PIN and the phone's changes ----------------------------------------------------
// NodePrefs::ble_pin is the one field the phone's CMD_SET_DEVICE_PIN writes; these are that path.
uint32_t meshBlePin();
bool meshBlePinSet(uint32_t pin);     // writes the field, savePrefs(), and makes it the active PIN
void meshBlePinActivate();            // boot: NodePrefs::ble_pin -> MyMesh's active PIN

uint32_t meshRadioSignature();        // freq/bw/sf/cr/tx/tuning/path hash/advert position
uint32_t meshNameSignature();         // the advert name
// A changed store that our own screens did NOT write, i.e. the phone. Queued as ble::PhoneChange
// bits (ble_model.h); the shell shows one toast and rebuilds the affected screen.
uint32_t meshPhoneChangeSerial();
bool meshPhoneChangeTake(uint8_t* change);

// The phone's own outgoing messages, tapped from its BLE frames before MyMesh handles them: the
// listener only sees what arrives, so without this a message sent from the phone would be missing
// from our chat. `channelIdx` is MeshCore's slot (what the app sends).
void meshPhoneChannelSent(uint8_t channelIdx, const char* text);
void meshPhoneDmSent(const uint8_t* prefix, uint8_t prefixLen, const char* text);

// --- EST-365: upstream's own radio counters ------------------------------------------------------
// Filled from the RadioLib wrapper (noise floor, packets recv/sent, receive errors, RX boosted gain
// read back from the chip) and MeshCore's Dispatcher (flood/direct counts, airtime) plus the seen
// table's duplicate counters. The model in radio_stats.h adds the history the chip does not keep.
bool meshRadioStats(radio::UpstreamCounters* out);
}
#endif  // OM_ENABLE_MESH
