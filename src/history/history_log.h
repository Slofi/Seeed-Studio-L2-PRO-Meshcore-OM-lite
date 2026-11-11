// SPDX-License-Identifier: MIT
// The on-card chat history: record format, CRC, torn-write rule, update records, rotation and the
// page reader.
//
// Plain C++ (no Arduino, no LovyanGFX, no SD): the firmware implements HistoryVolume/HistoryFile for
// SD_MMC and SPIFFS in src/history_store.cpp, and test/host drives the SAME HistoryStore with an
// in-memory volume. That is deliberate: the torn-write scan, the update records and paging are the
// parts that are easy to get wrong, and they are proven with the system g++.
//
// The log is append-only, one file per chat:
//   SD:     /omlite/history/<chat id>.log   (rotation: <chat id>.old)
//   SPIFFS: /h/<chat id>.log                (short prefix: SPIFFS names are at most 31 bytes)
// A chat id is "ch-<hash 2 hex>-<sanitised name>" for a channel and "dm-<pubkey[0..3] 8 hex>" for a
// direct message.
//
// Record: 10-byte header, little-endian, then the payload.
//   [0] magic 0x4F        [1] version 1       [2] type        [3] flags
//   [4..5] payload length [6..9] CRC-32 over bytes [1..5] + payload (so magic and CRC are not in it)
// The length prefix and the CRC make a torn last record identifiable: a reader stops at the first
// record that does not validate and keeps everything before it. Never corrupts the rest.
//
// Types: Chat (identity of the chat), Msg (one message), Update (a small change to a stored
// message: delivery state, ACK time, heard repeats) and Meta (the seen point and the unread count,
// last one wins). Updates and Meta are appended later; on load they are applied in file order.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mesh_text.h"

namespace om {
namespace history {

// --- format ---------------------------------------------------------------------------------------
constexpr uint8_t kMagic = 0x4F;          // 'O'
constexpr uint8_t kVersion = 1;
constexpr int kHeaderBytes = 10;
constexpr int kMaxPayload = 288;
constexpr int kMaxRecord = kHeaderBytes + kMaxPayload;

enum class Type : uint8_t {
  Chat = 1,     // {kind u8, nameLen u8, keyLen u8, reserved u8, name, key[32]}
  Msg = 2,      // see encodeMsg()
  Update = 3,   // {logId u32, kind u8, delivery u8, ackReal u8, reserved u8, ackWall u32, repeats u16}
  Meta = 4,     // {seenLogId u32, unread u16}
};

enum class ChatKind : uint8_t { Channel = 0, Dm = 1 };

// Update kinds: which fields the record carries. A later record wins; two updates for the same
// message are merged by the loader.
enum : uint8_t {
  kUpdateState = 1,     // delivery (+ ackWall, ackReal)
  kUpdateRepeats = 2,   // heard repeats
  kUpdateBoth = 3,
};

// --- CRC ------------------------------------------------------------------------------------------
// The same CRC-32 as the EST-329 contacts file (CRC-32/ISO-HDLC, poly 0xEDB88320).
inline uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

// Two runs, because the header bytes [1..5] and the payload are not contiguous (the CRC field sits
// between them). Used by encodeRecord/decodeRecordAt so one definition covers both.
inline uint32_t crc32Two(const uint8_t* a, size_t alen, const uint8_t* b, size_t blen) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < alen; ++i) {
    crc ^= a[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  for (size_t i = 0; i < blen; ++i) {
    crc ^= b[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

inline void put16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)(v >> 8);
}
inline void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)(v >> 24);
}
inline uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
inline uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Length of a bounded string (never past cap, no libc needed on a device).
inline int boundedLen(const char* s, int cap) {
  if (s == nullptr) return 0;
  int n = 0;
  while (n < cap && s[n] != 0) ++n;
  return n;
}

// --- one message ----------------------------------------------------------------------------------
// The fields the ticket names: seq, wall time + real flag, uptime, sender, text, hops, SNR, path,
// delivery state and ACK time, heard repeats. `logId` is the history's own monotonic id and is what
// Update records name; the RAM rings keep it next to their per-boot seq.
struct Msg {
  uint32_t logId = 0;
  uint32_t seq = 0;       // the RAM ring's per-boot id, kept for the USB dump
  uint32_t uptime = 0;    // seconds since boot when stored
  uint32_t wall = 0;      // system-clock seconds then (0 = unknown)
  uint32_t ackWall = 0;   // DM only: when the ACK arrived
  uint8_t flags = 0;      // bit0 wallReal, bit1 ackReal, bit2 mine, bit3 dm
  uint8_t delivery = 0;   // om::ui::DmState as a byte
  int8_t hops = -1;
  int8_t snr4 = 0;
  uint16_t repeats = 0;
  uint8_t pathHashSize = 1;
  uint8_t pathCount = 0;
  uint8_t nameLen = 0;
  uint8_t textLen = 0;
  uint8_t pathLen = 0;
  char name[kMeshNameCap] = "";
  char text[kMeshTextCap] = "";
  uint8_t path[kMeshPathMax] = {};

  bool wallReal() const { return (flags & 0x01) != 0; }
  bool ackReal() const { return (flags & 0x02) != 0; }
  bool mine() const { return (flags & 0x04) != 0; }
  bool dm() const { return (flags & 0x08) != 0; }
  void setWallReal(bool on) { flags = (uint8_t)(on ? (flags | 0x01) : (flags & ~0x01)); }
  void setAckReal(bool on) { flags = (uint8_t)(on ? (flags | 0x02) : (flags & ~0x02)); }
  void setMine(bool on) { flags = (uint8_t)(on ? (flags | 0x04) : (flags & ~0x04)); }
  void setDm(bool on) { flags = (uint8_t)(on ? (flags | 0x08) : (flags & ~0x08)); }
};

// Fills the length fields and copies name/text/path from a caller's buffers.
inline void msgSetText(Msg& m, const char* name, const char* text) {
  m.nameLen = (uint8_t)boundedLen(name, kMeshNameCap - 1);
  memcpy(m.name, name != nullptr ? name : "", (size_t)m.nameLen);
  m.name[m.nameLen] = 0;
  m.textLen = (uint8_t)boundedLen(text, kMeshTextCap - 1);
  memcpy(m.text, text != nullptr ? text : "", (size_t)m.textLen);
  m.text[m.textLen] = 0;
}

inline void msgSetPath(Msg& m, const uint8_t* path, uint8_t hashSize, uint8_t count) {
  m.pathHashSize = hashSize;
  m.pathCount = count;
  int bytes = (int)hashSize * (int)count;
  if (path == nullptr || hashSize < 1 || hashSize > 3 || bytes > kMeshPathMax) {
    m.pathHashSize = 0;
    m.pathCount = 0;
    m.pathLen = 0;
    return;
  }
  m.pathLen = (uint8_t)bytes;
  if (bytes > 0) memcpy(m.path, path, (size_t)bytes);
}

// --- record bodies --------------------------------------------------------------------------------
inline int encodeChatPayload(ChatKind kind, const char* name, const uint8_t* key, uint8_t* out,
                            int cap) {
  const int nameLen = boundedLen(name, kMeshNameCap - 1);
  const int keyLen = key != nullptr ? kMeshPubKey : 0;
  const int need = 4 + nameLen + keyLen;
  if (cap < need) return -1;
  out[0] = (uint8_t)kind;
  out[1] = (uint8_t)nameLen;
  out[2] = (uint8_t)keyLen;
  out[3] = 0;
  memcpy(out + 4, name != nullptr ? name : "", (size_t)nameLen);
  if (keyLen) memcpy(out + 4 + nameLen, key, (size_t)keyLen);
  return need;
}

struct ChatBody {
  ChatKind kind = ChatKind::Channel;
  char name[kMeshNameCap] = "";
  uint8_t key[kMeshPubKey] = {};
  bool hasKey = false;
};

inline bool decodeChatPayload(const uint8_t* p, int len, ChatBody* out) {
  if (p == nullptr || out == nullptr || len < 4) return false;
  const int nameLen = p[1];
  const int keyLen = p[2];
  if (nameLen < 0 || nameLen >= kMeshNameCap) return false;
  if (keyLen != 0 && keyLen != kMeshPubKey) return false;
  if (4 + nameLen + keyLen != len) return false;
  out->kind = p[0] == (uint8_t)ChatKind::Dm ? ChatKind::Dm : ChatKind::Channel;
  memcpy(out->name, p + 4, (size_t)nameLen);
  out->name[nameLen] = 0;
  out->hasKey = keyLen == kMeshPubKey;
  if (out->hasKey) memcpy(out->key, p + 4 + nameLen, kMeshPubKey);
  return true;
}

inline int encodeMsgPayload(const Msg& m, uint8_t* out, int cap) {
  const int nameLen = m.nameLen <= kMeshNameCap - 1 ? m.nameLen : kMeshNameCap - 1;
  const int textLen = m.textLen <= kMeshTextCap - 1 ? m.textLen : kMeshTextCap - 1;
  int pathLen = (int)m.pathHashSize * (int)m.pathCount;
  if (m.pathHashSize < 1 || m.pathHashSize > 3 || pathLen > kMeshPathMax) pathLen = 0;
  const int need = 31 + nameLen + textLen + pathLen;
  if (cap < need) return -1;
  put32(out + 0, m.logId);
  put32(out + 4, m.seq);
  put32(out + 8, m.uptime);
  put32(out + 12, m.wall);
  put32(out + 16, m.ackWall);
  out[20] = m.flags;
  out[21] = m.delivery;
  out[22] = (uint8_t)m.hops;
  out[23] = (uint8_t)m.snr4;
  put16(out + 24, m.repeats);
  out[26] = pathLen == 0 ? 0 : m.pathHashSize;
  out[27] = pathLen == 0 ? 0 : m.pathCount;
  out[28] = (uint8_t)nameLen;
  out[29] = (uint8_t)textLen;
  out[30] = (uint8_t)pathLen;
  int at = 31;
  memcpy(out + at, m.name, (size_t)nameLen);
  at += nameLen;
  memcpy(out + at, m.text, (size_t)textLen);
  at += textLen;
  if (pathLen > 0) memcpy(out + at, m.path, (size_t)pathLen);
  return need;
}

inline bool decodeMsgPayload(const uint8_t* p, int len, Msg* out) {
  if (p == nullptr || out == nullptr || len < 31) return false;
  const int nameLen = p[28];
  const int textLen = p[29];
  const int pathLen = p[30];
  if (nameLen >= kMeshNameCap || textLen >= kMeshTextCap) return false;
  if (pathLen > kMeshPathMax) return false;
  if (31 + nameLen + textLen + pathLen != len) return false;
  if (pathLen > 0) {
    const int hs = p[26];
    const int hc = p[27];
    if (hs < 1 || hs > 3 || (int)hs * hc != pathLen) return false;
  } else if (p[26] != 0 || p[27] != 0) {
    return false;
  }
  out->logId = get32(p + 0);
  out->seq = get32(p + 4);
  out->uptime = get32(p + 8);
  out->wall = get32(p + 12);
  out->ackWall = get32(p + 16);
  out->flags = p[20];
  out->delivery = p[21];
  out->hops = (int8_t)p[22];
  out->snr4 = (int8_t)p[23];
  out->repeats = get16(p + 24);
  out->pathHashSize = p[26];
  out->pathCount = p[27];
  out->nameLen = (uint8_t)nameLen;
  out->textLen = (uint8_t)textLen;
  out->pathLen = (uint8_t)pathLen;
  int at = 31;
  memcpy(out->name, p + at, (size_t)nameLen);
  out->name[nameLen] = 0;
  at += nameLen;
  memcpy(out->text, p + at, (size_t)textLen);
  out->text[textLen] = 0;
  at += textLen;
  if (pathLen > 0) memcpy(out->path, p + at, (size_t)pathLen);
  return true;
}

struct Update {
  uint32_t logId = 0;
  uint8_t kind = 0;
  uint8_t delivery = 0;
  bool ackReal = false;
  uint32_t ackWall = 0;
  uint16_t repeats = 0;
};

inline int encodeUpdatePayload(const Update& u, uint8_t* out, int cap) {
  if (cap < 14) return -1;
  put32(out + 0, u.logId);
  out[4] = u.kind;
  out[5] = u.delivery;
  out[6] = u.ackReal ? 1 : 0;
  out[7] = 0;
  put32(out + 8, u.ackWall);
  put16(out + 12, u.repeats);
  return 14;
}

inline bool decodeUpdatePayload(const uint8_t* p, int len, Update* out) {
  if (p == nullptr || out == nullptr || len != 14) return false;
  out->logId = get32(p + 0);
  out->kind = p[4];
  out->delivery = p[5];
  out->ackReal = p[6] != 0;
  out->ackWall = get32(p + 8);
  out->repeats = get16(p + 12);
  return out->kind != 0;
}

// Applies one update. Returns true when the message changed. A delivery update carries the ACK
// time; a repeats update carries the counter; kUpdateBoth carries both (used by the loader when two
// records for one message were merged).
inline bool applyUpdate(Msg& m, const Update& u) {
  bool changed = false;
  if (u.kind == kUpdateState || u.kind == kUpdateBoth) {
    if (m.delivery != u.delivery) {
      m.delivery = u.delivery;
      changed = true;
    }
    if (u.ackWall != 0) {
      m.ackWall = u.ackWall;
      m.setAckReal(u.ackReal);
      changed = true;
    }
  }
  if (u.kind == kUpdateRepeats || u.kind == kUpdateBoth) {
    if (m.repeats != u.repeats) {
      m.repeats = u.repeats;
      changed = true;
    }
  }
  return changed;
}

struct Meta {
  uint32_t seenLogId = 0;
  uint16_t unread = 0;
};

// EST-367: how many messages are still unread once the seen point moved to `seenLogId`. A chat's log
// ids are consecutive (one per stored message, our own sends included), so the distance is the count
// of messages newer than the seen point. The normal open loads the NEWEST page, so seenLogId is the
// newest stored id and this is 0; a load that fell short leaves the messages it did not show unread.
// Never more than what was unread before (an id we did not mark cannot ADD unread).
inline uint16_t unreadAfterSeen(uint16_t unreadBefore, uint32_t maxLogId, uint32_t seenLogId) {
  if (seenLogId >= maxLogId) return 0;
  const uint32_t left = maxLogId - seenLogId;
  const uint16_t capped = left > 0xFFFFu ? 0xFFFFu : (uint16_t)left;
  return capped > unreadBefore ? unreadBefore : capped;
}

// EST-367: a chat's newest stored message, kept in RAM (PSRAM on the device, with the other scratch)
// so the selector's preview and the page's "newest on the card" come from the chat's OWN log instead
// of the 32-message arrival ring, which a busy channel evicts. Filled lazily once from the tail and
// then kept by appendMsg(), so it costs one small read per chat after boot.
struct ChatSummary {
  bool has = false;
  bool wallReal = false;
  bool mine = false;
  uint32_t logId = 0;
  uint32_t wall = 0;
  uint32_t uptime = 0;
  char name[kMeshNameCap] = "";
  char text[kMeshTextCap] = "";
};

inline int encodeMetaPayload(const Meta& m, uint8_t* out, int cap) {
  if (cap < 6) return -1;
  put32(out + 0, m.seenLogId);
  put16(out + 4, m.unread);
  return 6;
}

inline bool decodeMetaPayload(const uint8_t* p, int len, Meta* out) {
  if (p == nullptr || out == nullptr || len != 6) return false;
  out->seenLogId = get32(p + 0);
  out->unread = get16(p + 4);
  return true;
}

// --- one framed record ----------------------------------------------------------------------------
inline int encodeRecord(Type type, const uint8_t* payload, int payloadLen, uint8_t* out, int cap) {
  if (payloadLen < 0 || payloadLen > kMaxPayload) return -1;
  const int total = kHeaderBytes + payloadLen;
  if (cap < total) return -1;
  out[0] = kMagic;
  out[1] = kVersion;
  out[2] = (uint8_t)type;
  out[3] = 0;
  put16(out + 4, (uint16_t)payloadLen);
  if (payloadLen > 0) memcpy(out + kHeaderBytes, payload, (size_t)payloadLen);
  put32(out + 6, crc32Two(out + 1, 5, out + kHeaderBytes, (size_t)payloadLen));
  return total;
}

struct RecordRef {
  Type type = Type::Chat;
  uint16_t len = 0;
  const uint8_t* payload = nullptr;
  uint32_t offset = 0;   // file offset of the record's first byte
};

// One record at buf[0..len). Returns the total record length, or -1 when it does not validate.
inline int decodeRecordAt(const uint8_t* buf, int len, uint32_t baseOffset, RecordRef* out) {
  if (buf == nullptr || len < kHeaderBytes) return -1;
  if (buf[0] != kMagic || buf[1] != kVersion) return -1;
  const int payloadLen = get16(buf + 4);
  if (payloadLen > kMaxPayload) return -1;
  const int total = kHeaderBytes + payloadLen;
  if (len < total) return -1;
  if (crc32Two(buf + 1, 5, buf + kHeaderBytes, (size_t)payloadLen) != get32(buf + 6)) return -1;
  const uint8_t type = buf[2];
  if (type < (uint8_t)Type::Chat || type > (uint8_t)Type::Meta) return -1;
  if (out != nullptr) {
    out->type = (Type)type;
    out->len = (uint16_t)total;
    out->payload = buf + kHeaderBytes;
    out->offset = baseOffset;
  }
  return total;
}

// Walks a window that ends at a known record boundary. Returns the byte position where the walk
// stopped and sets *count. `tornOk` is only for a window that ends at the file's end, where the
// last record may be half-written: then a stop in the last kMaxRecord bytes is accepted.
inline int validateChain(const uint8_t* buf, int len, bool tornOk, int* count) {
  int pos = 0;
  int n = 0;
  while (pos + kHeaderBytes <= len) {
    const int r = decodeRecordAt(buf + pos, len - pos, 0, nullptr);
    if (r < 0) break;
    pos += r;
    ++n;
  }
  if (count != nullptr) *count = n;
  if (n == 0) return -1;
  if (pos == len) return pos;
  if (tornOk && pos >= len - kMaxRecord) return pos;
  return -1;
}

// The first record boundary in a window (windows are read backwards, so a window can start in the
// middle of a record). Tries every magic byte and accepts the first start whose chain reaches the
// window's end (or, at the file's end, a torn tail). Returns the index, or -1.
inline int findWindowStart(const uint8_t* buf, int len, bool tornOk, int* validEnd) {
  for (int p = 0; p + kHeaderBytes <= len; ++p) {
    if (buf[p] != kMagic) continue;
    const int end = validateChain(buf + p, len - p, tornOk, nullptr);
    if (end < 0) continue;
    if (validEnd != nullptr) *validEnd = p + end;
    return p;
  }
  return -1;
}

// --- chat id --------------------------------------------------------------------------------------
// A file name the card and SPIFFS both accept: letters, digits, '-' and '_' only.
inline int sanitizeId(const char* in, char* out, int cap) {
  int n = 0;
  for (const char* p = in; p != nullptr && *p != 0 && n < cap - 1; ++p) {
    const char c = *p;
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '_';
    out[n++] = ok ? c : '_';
  }
  out[n] = 0;
  return n;
}

// "ch-<hash 2 hex>-<name>", at most 20 chars so SPIFFS's 31-byte limit still fits /h/<id>.log.
inline void channelId(uint8_t hash, const char* name, char* out, size_t cap) {
  char clean[15];
  sanitizeId(name != nullptr ? name : "", clean, (int)sizeof(clean));
  snprintf(out, cap, "ch-%02x-%s", (unsigned)hash, clean);
}

// "dm-<pubkey[0..3] 8 hex>". The ticket's id: the contact's public-key prefix.
inline void dmId(const uint8_t* pubKey, char* out, size_t cap) {
  if (pubKey == nullptr) {
    snprintf(out, cap, "dm-unknown");
    return;
  }
  snprintf(out, cap, "dm-%02x%02x%02x%02x", pubKey[0], pubKey[1], pubKey[2], pubKey[3]);
}

// --- volume / file interfaces ---------------------------------------------------------------------
// The device fills these in src/history_store.cpp; the host test fills them with byte vectors.
class HistoryFile {
 public:
  virtual ~HistoryFile() {}
  virtual int read(uint8_t* buf, int n) = 0;
  virtual int write(const uint8_t* buf, int n) = 0;
  virtual bool seek(uint32_t pos) = 0;
  virtual uint32_t size() = 0;
  virtual void flush() = 0;
  virtual void close() = 0;
};

using ListFn = void (*)(void* ctx, const char* name, uint32_t size);

class HistoryVolume {
 public:
  virtual ~HistoryVolume() {}
  virtual const char* name() const = 0;    // "sd" / "spiffs"
  virtual const char* prefix() const = 0;  // directory, with the trailing slash
  virtual bool mount() = 0;
  virtual bool exists(const char* path) = 0;
  virtual uint32_t size(const char* path) = 0;
  virtual bool remove(const char* path) = 0;
  virtual bool rename(const char* from, const char* to) = 0;
  // Returns a reusable file object owned by the volume, or nullptr. The store never holds more than
  // two opens at once (a repair reads one and writes another), so the device adapter keeps four.
  virtual HistoryFile* openRead(const char* path) = 0;
  virtual HistoryFile* openAppend(const char* path) = 0;
  virtual void list(ListFn fn, void* ctx) = 0;
};

// --- the store ------------------------------------------------------------------------------------
constexpr int kMaxChatSlots = 32;      // 8 channels + 16 DM threads + room for orphaned logs
constexpr int kScanRing = 32;          // the one read buffer: newest window / one page
constexpr int kQueueRecords = 16;      // writes waiting for the loop (EST-349: PSRAM, was 8)
constexpr int kPendingUpdates = 16;    // updates met before their message while walking backwards
constexpr int kWindowBytes = 4096;     // one backwards read
constexpr int kWindowRefs = 256;       // the smallest record is 16 B, so 4096/16 = 256
constexpr int kMetaSearchWindows = 4;  // how far back the last Meta is looked for (16 KB)
constexpr uint32_t kCountBudgetBytes = 256UL * 1024UL;   // the `chats` count's cap per file (EST-367)

// One queued write: the slot and the already-encoded record. appendMsg/appendUpdate/markSeen copy
// into this queue and return; only flushStep() (history::loop, loop task) touches the volume. The
// device puts this array in PSRAM (EST-349); the host test gives the store its own array.
struct HistoryQueuedRecord {
  uint8_t slot = 0;
  uint16_t len = 0;
  // EST-380: this record's append failed at least once and it is still waiting for a retry. It stays
  // `true` until the record is written, so hist_write_fail counts RECORDS, not retry attempts.
  bool failed = false;
  uint8_t data[kMaxRecord] = {};
};

constexpr size_t kQueueBytes = sizeof(HistoryQueuedRecord) * (size_t)kQueueRecords;
constexpr size_t kScanBytes = sizeof(Msg) * (size_t)kScanRing;
constexpr size_t kOffsBytes = sizeof(uint16_t) * (size_t)kWindowRefs;
// EST-367: one summary per chat slot (the selector's preview, no card read per draw).
constexpr size_t kSummaryBytes = sizeof(ChatSummary) * (size_t)kMaxChatSlots;

// The store's working memory, supplied by the caller so the firmware can place every large buffer in
// PSRAM and the host test can use the stack. Nothing here is static RAM on the device (EST-349).
struct HistoryScratch {
  HistoryQueuedRecord* queue = nullptr;
  int queueSlots = 0;
  uint8_t* window = nullptr;    // kWindowBytes
  Msg* scan = nullptr;          // kScanRing
  uint16_t* offs = nullptr;     // kWindowRefs
  ChatSummary* summaries = nullptr;   // EST-367: kMaxChatSlots, the selector's preview per chat
  bool complete() const {
    return queue != nullptr && queueSlots >= kQueueRecords && window != nullptr &&
           scan != nullptr && offs != nullptr && summaries != nullptr;
  }
};

// EST-367: what the last backwards read cost, so an open can SAY how fast it was (the ticket's
// "measure the open time with a full 2 MB log"). `windows` is the number of 4 KB backwards reads:
// the newest page of a chat is a seek to the end plus one window, never a scan of the file.
struct ReadStats {
  uint32_t windows = 0;
  uint32_t bytes = 0;
};

struct HistoryConfig {
  uint32_t perChatCap = 2UL * 1024UL * 1024UL;   // 2 MB, then rotate to .old
  uint32_t totalCap = 64UL * 1024UL * 1024UL;    // 64 MB on the card
};

struct HistoryStatus {
  bool ready = false;
  bool sd = false;           // false = the SPIFFS fallback
  bool full = false;         // a cap refused at least one record
  uint32_t bytes = 0;        // all chat logs together
  uint32_t perChatCap = 0;
  uint32_t totalCap = 0;
  uint32_t records = 0;      // records written since boot
  uint32_t worstFlushUs = 0;
  uint32_t dropped = 0;
  // EST-380: records whose append hit a card error at least once (open, write, repair, rotate). The
  // record is NOT dropped; it stays at the head of the RAM queue and is retried. 0 = a clean card.
  uint32_t writeFail = 0;
  int slots = 0;             // chats with a log
};

// EST-380: what the shell must say about a history write failure. One notice per failure episode,
// never one per message; `CardErrorDropped` is a second, once-only warning when the RAM queue
// overflowed during that episode (the messages from then on really are lost, and the pop-up says so).
enum class WriteNotice : uint8_t {
  None = 0,
  CardError = 1,          // a card append failed; the record is kept and retried
  CardErrorDropped = 2,   // ...and the RAM queue was full, so `dropped` records are gone
  QueueFull = 3,          // the queue overflowed while writes worked (a burst); say so too
};

// EST-380: what one flushOne() did with the record at the head of the queue. `Failed` means the
// volume refused it: the record is still there, untouched, and the next pass retries it.
enum class FlushOutcome : uint8_t { Written = 0, Dropped = 1, Failed = 2 };

// EST-382: what clearChat() found and did. `fileGone` means at least one of the chat's files existed
// and is now deleted; `fileFailed` means a delete was refused (then nothing was reset and the queue
// was left alone, so RAM still matches the card copy that remains); `records` is how many of THIS
// chat's records were dropped from the write queue (other chats' records are never touched).
struct ClearResult {
  bool fileGone = false;
  bool fileFailed = false;
  int records = 0;
};

using NowFn = uint32_t (*)();      // microseconds, wrapping
using LogFn = void (*)(const char*);

class HistoryStore {
 public:
  // The real entry point: the caller owns every working buffer (PSRAM on the device, see
  // src/history_store.cpp). begin() reads the directory and each log's tail ONCE, on the loop task
  // during setup, so a slot's next log id is known in RAM before any caller enqueues a record.
  void begin(HistoryVolume* primary, HistoryVolume* fallback, const HistoryConfig& primaryCfg,
             const HistoryConfig& fallbackCfg, const HistoryScratch& scratch, NowFn nowUs,
             LogFn log) {
    scratch_ = scratch;
    queue_ = scratch.queue;
    queueSlots_ = scratch.queueSlots;
    scan_ = scratch.scan;
    window_ = scratch.window;
    offs_ = scratch.offs;
    summaries_ = scratch.summaries;
    primary_ = primary;
    fallback_ = fallback;
    primaryCfg_ = primaryCfg;
    fallbackCfg_ = fallbackCfg;
    nowUs_ = nowUs;
    log_ = log;
    cfg_ = primaryCfg;
    vol_ = nullptr;
    ready_ = false;
    full_ = false;
    dropAll();
    if (!scratch_.complete()) {
      logLine("[hist] no working memory (queue/window/scan): history is off");
      return;
    }
    if (primary_ != nullptr && primary_->mount()) {
      vol_ = primary_;
      cfg_ = primaryCfg_;
      ready_ = true;
    } else if (fallback_ != nullptr && fallback_->mount()) {
      vol_ = fallback_;
      cfg_ = fallbackCfg_;
      ready_ = true;
    }
    if (!ready_) {
      logLine("[hist] no usable volume: history is off");
      return;
    }
    scanDirectory();
    scanAllTails();
    char line[160];
    snprintf(line, sizeof(line), "[hist] store %s, %lu KB/chat, %lu KB total, %d chats, %lu KB used",
             vol_->name(), (unsigned long)(cfg_.perChatCap / 1024UL),
             (unsigned long)(cfg_.totalCap / 1024UL), slotCount_,
             (unsigned long)(totalBytes_ / 1024UL));
    logLine(line);
    if (vol_ == fallback_) {
      logLine("[hist] SD card absent or not mountable: history on SPIFFS (bounded, see the cap above)");
    }
  }

  // Host/test convenience: allocate the scratch with malloc (never freed; the device always passes
  // PSRAM through HistoryScratch). Keeps the tests' call sites simple.
  void begin(HistoryVolume* primary, HistoryVolume* fallback, const HistoryConfig& primaryCfg,
             const HistoryConfig& fallbackCfg, NowFn nowUs, LogFn log) {
    HistoryScratch sc;
    sc.queue = (HistoryQueuedRecord*)malloc(kQueueBytes);
    sc.queueSlots = kQueueRecords;
    sc.window = (uint8_t*)malloc((size_t)kWindowBytes);
    sc.scan = (Msg*)malloc(kScanBytes);
    sc.offs = (uint16_t*)malloc(kOffsBytes);
    sc.summaries = (ChatSummary*)malloc(kSummaryBytes);
    begin(primary, fallback, primaryCfg, fallbackCfg, sc, nowUs, log);
  }

  bool ready() const { return ready_; }
  bool onSd() const { return ready_ && vol_ == primary_; }
  int slotCount() const { return slotCount_; }

  const HistoryStatus& status() const {
    status_.ready = ready_;
    status_.sd = onSd();
    status_.full = full_;
    status_.bytes = totalBytes_;
    status_.perChatCap = cfg_.perChatCap;
    status_.totalCap = cfg_.totalCap;
    status_.records = records_;
    status_.worstFlushUs = worstFlushUs_;
    status_.dropped = dropped_;
    status_.writeFail = writeFail_;
    status_.slots = slotCount_;
    return status_;
  }

  // EST-380: the shell's pop-up door. The store keeps ONE pending notice (a second failure replaces
  // nothing: it only escalates to the drop warning), and the caller gets it once. `dropped` is the
  // number to put in the text: the queue overflow of the current failure episode for
  // CardErrorDropped, the cumulative count for QueueFull.
  WriteNotice takeWriteNotice(uint32_t* dropped = nullptr) {
    const WriteNotice n = notice_;
    notice_ = WriteNotice::None;
    if (dropped != nullptr) {
      *dropped = (n == WriteNotice::CardErrorDropped) ? droppedInEpisode_ : dropped_;
    }
    return n;
  }

  // --- slots --------------------------------------------------------------------------------------
  int findSlot(const char* id) const {
    if (id == nullptr) return -1;
    for (int i = 0; i < kMaxChatSlots; ++i) {
      if (slots_[i].used && strcmp(slots_[i].id, id) == 0) return i;
    }
    return -1;
  }

  int ensureChannel(const char* name, uint8_t hash) {
    char id[24];
    channelId(hash, name, id, sizeof(id));
    return ensureSlot(id, ChatKind::Channel, name, nullptr);
  }

  int ensureDm(const uint8_t* pubKey, const char* contactName) {
    char id[24];
    dmId(pubKey, id, sizeof(id));
    return ensureSlot(id, ChatKind::Dm, contactName, pubKey);
  }

  int channelSlotByName(const char* name) const {
    for (int i = 0; i < kMaxChatSlots; ++i) {
      if (slots_[i].used && slots_[i].kind == (uint8_t)ChatKind::Channel &&
          strcmp(slots_[i].name, name != nullptr ? name : "") == 0) {
        return i;
      }
    }
    return -1;
  }

  int dmSlotByNameOrKey(const char* text) const {
    if (text == nullptr) return -1;
    const size_t n = strlen(text);
    for (int i = 0; i < kMaxChatSlots; ++i) {
      if (!slots_[i].used || slots_[i].kind != (uint8_t)ChatKind::Dm) continue;
      if (strcmp(slots_[i].name, text) == 0) return i;
      // The id is dm-<8 hex>; a prefix of the public key is how the USB commands name a contact.
      if (n >= 2 && n <= 8 && strncmp(slots_[i].id + 3, text, n) == 0) return i;
    }
    return -1;
  }

  const char* slotName(int slot) const {
    return (slot >= 0 && slot < kMaxChatSlots) ? slots_[slot].name : "";
  }
  const char* slotId(int slot) const {
    return (slot >= 0 && slot < kMaxChatSlots) ? slots_[slot].id : "";
  }
  bool slotKey(int slot, const uint8_t** key) const {
    if (slot < 0 || slot >= kMaxChatSlots || !slots_[slot].used || !slots_[slot].hasKey) return false;
    if (key != nullptr) *key = slots_[slot].key;
    return true;
  }
  ChatKind slotKind(int slot) const {
    return (slot >= 0 && slot < kMaxChatSlots) ? (ChatKind)slots_[slot].kind : ChatKind::Channel;
  }
  uint32_t newestLogId(int slot) const {
    return (slot >= 0 && slot < kMaxChatSlots) ? slots_[slot].maxLogId : 0;
  }

  int dmSlotCount() const {
    int n = 0;
    for (int i = 0; i < kMaxChatSlots; ++i) {
      if (slots_[i].used && slots_[i].kind == (uint8_t)ChatKind::Dm) ++n;
    }
    return n;
  }
  int dmSlotAt(int index) const {
    int n = 0;
    for (int i = 0; i < kMaxChatSlots; ++i) {
      if (slots_[i].used && slots_[i].kind == (uint8_t)ChatKind::Dm) {
        if (n == index) return i;
        ++n;
      }
    }
    return -1;
  }

  // --- appending ----------------------------------------------------------------------------------
  // Assigns m.logId and queues the Msg record (plus a Meta for a received message). NO VOLUME ACCESS:
  // the slot was scanned at begin() (or is new and empty), so maxLogId is RAM truth. The record is
  // written by flushStep() on the loop task. EST-349: the send path and MeshCore callbacks never
  // touch the card from here.
  uint32_t appendMsg(int slot, Msg m, uint16_t unreadAfter) {
    if (!ready_ || slot < 0 || slot >= kMaxChatSlots || !slots_[slot].used) return 0;
    Slot& s = slots_[slot];
    const uint32_t id = s.maxLogId + 1;
    m.logId = id;
    uint8_t payload[kMaxPayload];
    const int n = encodeMsgPayload(m, payload, (int)sizeof(payload));
    if (n < 0) return 0;
    uint8_t rec[kMaxRecord];
    const int rn = encodeRecord(Type::Msg, payload, n, rec, (int)sizeof(rec));
    if (rn < 0) return 0;
    if (!enqueue(slot, rec, rn)) return 0;
    s.maxLogId = id;
    // EST-367: the preview and "newest on the card" follow the chat's own log, so a message stored
    // here is also what the selector shows before the card is read again.
    if (summaries_ != nullptr) setSummary(summaries_[slot], m);
    if (!m.mine()) {
      Meta meta;
      meta.seenLogId = s.seenLogId;
      meta.unread = unreadAfter;
      uint8_t mp[8];
      const int mn = encodeMetaPayload(meta, mp, (int)sizeof(mp));
      uint8_t mrec[kHeaderBytes + 8];
      const int mr = encodeRecord(Type::Meta, mp, mn, mrec, (int)sizeof(mrec));
      if (mr > 0) enqueue(slot, mrec, mr);
      s.unread = unreadAfter;
    }
    return id;
  }

  // Delivery state / ACK time / heard repeats for an already stored message. Small records; a lost
  // one only loses that update, never a message. RAM only (EST-349).
  bool appendUpdate(int slot, const Update& u) {
    if (!ready_ || slot < 0 || slot >= kMaxChatSlots || !slots_[slot].used) return false;
    if (u.logId == 0) return false;
    uint8_t payload[16];
    const int n = encodeUpdatePayload(u, payload, (int)sizeof(payload));
    if (n < 0) return false;
    uint8_t rec[kHeaderBytes + 16];
    const int rn = encodeRecord(Type::Update, payload, n, rec, (int)sizeof(rec));
    if (rn < 0) return false;
    return enqueue(slot, rec, rn);
  }

  // The user has read the chat: the seen point moves to the newest stored message and unread is 0.
  // RAM only (EST-349): maxLogId is the id the next append will use minus one.
  bool markSeen(int slot) {
    if (!ready_ || slot < 0 || slot >= kMaxChatSlots || !slots_[slot].used) return false;
    Slot& s = slots_[slot];
    if (s.seenLogId == s.maxLogId && s.unread == 0) return true;
    Meta meta;
    meta.seenLogId = s.maxLogId;
    meta.unread = 0;
    uint8_t mp[8];
    const int mn = encodeMetaPayload(meta, mp, (int)sizeof(mp));
    uint8_t rec[kHeaderBytes + 8];
    const int rn = encodeRecord(Type::Meta, mp, mn, rec, (int)sizeof(rec));
    if (rn < 0 || !enqueue(slot, rec, rn)) return false;
    s.seenLogId = s.maxLogId;
    s.unread = 0;
    return true;
  }

  // --- reading ------------------------------------------------------------------------------------
  // The newest `cap` messages (cap is clamped to kScanRing) into scanMsg(). Sets scanUnread() from
  // the last Meta met. Returns how many are in the ring, oldest first.
  int loadNewest(int slot, int cap) {
    if (cap > kScanRing) cap = kScanRing;
    return collect(slot, 0xFFFFFFFFu, cap, true);
  }

  // The newest `cap` messages strictly older than `beforeLogId`, oldest first. This is the paging
  // read: it walks the file backwards in fixed 4 KB windows, so RAM stays the one scan ring.
  int loadOlder(int slot, uint32_t beforeLogId, int cap) {
    if (cap > kScanRing) cap = kScanRing;
    return collect(slot, beforeLogId, cap, false);
  }

  int scanCount() const { return scanCount_; }
  const Msg& scanMsg(int i) const { return scan_[i]; }
  uint16_t scanUnread() const { return scanUnread_; }
  uint32_t scanSeen() const { return scanSeen_; }

  // EST-367: the last backwards read (windows / bytes), for the measured open time.
  const ReadStats& readStats() const { return readStats_; }

  // EST-367: this chat's newest stored message, from its own log. A summary is filled on first use
  // (one backwards window from the END of the file, never a scan), then kept current by appendMsg().
  // nullptr when the chat has no stored message (or history is off).
  const ChatSummary* summary(int slot) {
    if (!ready_ || slot < 0 || slot >= kMaxChatSlots || !slots_[slot].used) return nullptr;
    if (summaries_ == nullptr) return nullptr;
    ChatSummary& c = summaries_[slot];
    if (!c.has) fillSummary(slot, c);
    return c.has ? &c : nullptr;
  }

  uint16_t slotUnread(int slot) const {
    return (slot >= 0 && slot < kMaxChatSlots) ? slots_[slot].unread : 0;
  }
  uint32_t slotSeen(int slot) const {
    return (slot >= 0 && slot < kMaxChatSlots) ? slots_[slot].seenLogId : 0;
  }

  // EST-367: the user saw this chat up to `logId` (the newest message the view actually loaded). The
  // messages newer than that stay unread; the normal open shows the newest page, so it is 0. Returns
  // true when the seen point moved (a Meta record is queued; nothing is written here).
  bool markSeenThrough(int slot, uint32_t logId) {
    if (!ready_ || slot < 0 || slot >= kMaxChatSlots || !slots_[slot].used || logId == 0) return false;
    Slot& s = slots_[slot];
    if (logId > s.maxLogId) logId = s.maxLogId;
    if (logId <= s.seenLogId) return true;   // nothing newer was shown
    const uint16_t left = unreadAfterSeen(s.unread, s.maxLogId, logId);
    Meta meta;
    meta.seenLogId = logId;
    meta.unread = left;
    uint8_t mp[8];
    const int mn = encodeMetaPayload(meta, mp, (int)sizeof(mp));
    uint8_t rec[kHeaderBytes + 8];
    const int rn = encodeRecord(Type::Meta, mp, mn, rec, (int)sizeof(rec));
    if (rn < 0 || !enqueue(slot, rec, rn)) return false;
    s.seenLogId = logId;
    s.unread = left;
    return true;
  }

  // EST-367 rule 6 (the `chats` USB check): how many Msg records this chat really has on the volume,
  // .log plus .old. ONE forward read of each file, on the loop task, only when the check asks: the
  // boot and open paths never call it. A torn tail ends the count, exactly like the readers stop there.
  // The scan stops after kCountBudgetBytes per file so the check can never freeze the loop on a 2 MB
  // log; `complete` false means the answer is a lower bound (the line prints it with a '+').
  int countMessages(int slot, bool* complete = nullptr) {
    if (complete != nullptr) *complete = true;
    if (!ready_ || slot < 0 || slot >= kMaxChatSlots || !slots_[slot].used) return 0;
    bool done = true;
    const int n = countFile(slots_[slot], false, &done) + countFile(slots_[slot], true, &done);
    if (complete != nullptr) *complete = done;
    return n;
  }

  // EST-382: delete one chat's card copy and its queued records. The .log and the .old (a rotation
  // holds the same chat's older messages) are both removed; on success the slot's ids, seen point,
  // unread and preview are reset, so the store recreates the file with a Chat header on the next
  // message and the chat looks empty. On a refused delete nothing is reset and no queue record is
  // dropped: the card still holds the messages, and RAM must not claim otherwise. Runs on the loop
  // task (the caller is the UI/USB shell), never from a packet callback.
  ClearResult clearChat(int slot) {
    ClearResult r;
    if (!ready_ || slot < 0 || slot >= kMaxChatSlots || !slots_[slot].used) return r;
    Slot& s = slots_[slot];
    if (vol_ == nullptr) return r;
    char logPath[64], oldPath[64];
    buildPath(s, ".log", logPath, sizeof(logPath));
    buildPath(s, ".old", oldPath, sizeof(oldPath));
    const bool hadLog = vol_->exists(logPath);
    const bool hadOld = vol_->exists(oldPath);
    bool logOk = true, oldOk = true;
    if (hadLog) logOk = vol_->remove(logPath);
    if (hadOld) oldOk = vol_->remove(oldPath);
    const uint32_t logNow = vol_->size(logPath);
    const uint32_t oldNow = vol_->size(oldPath);
    if (!logOk || !oldOk) {
      // Re-read the sizes so the store's accounting follows what is really on the card. Ids, seen,
      // unread and the summary stay: the messages that remain are still readable.
      const uint32_t before = s.fileBytes + s.oldBytes;
      totalBytes_ = totalBytes_ >= before ? totalBytes_ - before : 0;
      s.fileBytes = logNow;
      s.oldBytes = oldNow;
      totalBytes_ += logNow + oldNow;
      s.headerWritten = s.fileBytes > 0;
      s.needsRepair = false;
      s.repairBytes = 0;
      r.fileFailed = true;
      return r;
    }
    r.fileGone = hadLog || hadOld;
    const uint32_t before = s.fileBytes + s.oldBytes;
    totalBytes_ = totalBytes_ >= before ? totalBytes_ - before : 0;
    s.fileBytes = 0;
    s.oldBytes = 0;
    s.maxLogId = 0;
    s.seenLogId = 0;
    s.unread = 0;
    s.headerWritten = false;
    s.needsRepair = false;
    s.repairBytes = 0;
    s.scanned = true;
    if (summaries_ != nullptr) summaries_[slot] = ChatSummary{};
    r.records = dropQueuedForSlot(slot);
    return r;
  }

  // --- flushing -----------------------------------------------------------------------------------
  // Writes at most `maxRecords` queued records. Called from the mesh loop, so the worst single call
  // is one open + one write + flush + close; status().worstFlushUs measures it.
  //
  // EST-380: a failed append never drops the record. The record stays at the head of the queue and
  // the next pass retries it; while an episode lasts, the pass retries and then drains the whole
  // backlog in one go, and the first success logs one `[hist] write recovered, N record(s) flushed`.
  void flushStep(int maxRecords = 1) {
    if (episode_) maxRecords = queueSlots_ > 0 ? queueSlots_ : 1;
    int written = 0;
    for (int i = 0; i < maxRecords && queueCount_ > 0; ++i) {
      const FlushOutcome r = flushOne();
      if (r == FlushOutcome::Failed) return;   // the head stays; the next loop pass tries again
      if (r == FlushOutcome::Written) ++written;
    }
    if (episode_ && written > 0 && queueCount_ == 0) {
      // The card answers again and the backlog is on it. One line, with the count the ticket asks
      // for; the next outage logs `[hist] write FAILED` at once (failLogArmed_ starts false).
      episode_ = false;
      failLogArmed_ = false;
      char line[80];
      snprintf(line, sizeof(line), "[hist] write recovered, %d record(s) flushed", written);
      logLine(line);
    }
  }
  int queued() const { return queueCount_; }

 private:
  struct Slot {
    bool used = false;
    uint8_t kind = (uint8_t)ChatKind::Channel;
    char id[24] = "";
    char name[kMeshNameCap] = "";
    uint8_t key[kMeshPubKey] = {};
    bool hasKey = false;
    uint32_t fileBytes = 0;   // the .log
    uint32_t oldBytes = 0;    // the .old
    uint32_t maxLogId = 0;
    uint32_t seenLogId = 0;
    uint16_t unread = 0;
    bool scanned = false;
    bool headerWritten = false;
    bool needsRepair = false;
    uint32_t repairBytes = 0;
  };

  struct PendingUpdate {
    bool used = false;
    Update u;
  };

  void logLine(const char* text) const {
    if (log_ != nullptr && text != nullptr) log_(text);
  }

  // EST-367: the summary's fields come from the message itself (bounded copies, no allocation).
  void setSummary(ChatSummary& c, const Msg& m) {
    c = ChatSummary{};
    c.has = true;
    c.logId = m.logId;
    c.wall = m.wall;
    c.wallReal = m.wallReal();
    c.uptime = m.uptime;
    c.mine = m.mine();
    const int nameLen = boundedLen(m.name, kMeshNameCap - 1);
    memcpy(c.name, m.name, (size_t)nameLen);
    c.name[nameLen] = 0;
    const int textLen = boundedLen(m.text, kMeshTextCap - 1);
    memcpy(c.text, m.text, (size_t)textLen);
    c.text[textLen] = 0;
  }

  // One backwards window over the tail, through the same collect() the open uses: no scan.
  void fillSummary(int slot, ChatSummary& c) {
    if (collect(slot, 0xFFFFFFFFu, 1, false) <= 0) return;
    setSummary(c, scan_[0]);
  }

  // Forward read of one file, counting Msg records. Only the diagnostics (`chats`) call this.
  int countFile(const Slot& s, bool old, bool* complete) {
    char path[64];
    buildPath(s, old ? ".old" : ".log", path, sizeof(path));
    HistoryFile* f = vol_->openRead(path);
    if (f == nullptr) return 0;
    int counted = 0;
    uint32_t base = 0;      // the file offset of window_[0]
    uint32_t readBytes = 0; // this file's share of the budget
    int carry = 0;          // bytes of a record that crossed the window's end
    bool stop = false;
    while (!stop) {
      if (readBytes >= kCountBudgetBytes) {
        if (complete != nullptr) *complete = false;   // a lower bound: the '+' in the `chats` line
        break;
      }
      const int got = f->read(window_ + carry, kWindowBytes - carry);
      if (got <= 0) break;   // EOF: whatever is left in `carry` is the torn tail
      readBytes += (uint32_t)got;
      const int have = carry + got;
      int at = 0;
      while (at + kHeaderBytes <= have) {
        RecordRef ref;
        if (decodeRecordAt(window_ + at, have - at, base + (uint32_t)at, &ref) < 0) {
          // A record may simply CROSS the window's end: fewer bytes left here than the longest
          // record can be is an incomplete record, not a torn one. Anything longer that still does
          // not decode is the torn tail and ends the count, exactly like the readers.
          if (have - at >= kMaxRecord) stop = true;
          break;
        }
        if (ref.type == Type::Msg) ++counted;
        at += (int)ref.len;
      }
      base += (uint32_t)at;
      carry = have - at;
      if (carry > 0) memmove(window_, window_ + at, (size_t)carry);
      if (carry >= kWindowBytes) break;   // cannot happen: kMaxRecord < kWindowBytes
    }
    f->close();
    return counted;
  }

  void dropAll() {
    for (int i = 0; i < kMaxChatSlots; ++i) slots_[i] = Slot{};
    if (summaries_ != nullptr) {
      for (int i = 0; i < kMaxChatSlots; ++i) summaries_[i] = ChatSummary{};
    }
    slotCount_ = 0;
    totalBytes_ = 0;
    if (queue_ != nullptr) {
      for (int i = 0; i < queueSlots_; ++i) queue_[i] = HistoryQueuedRecord{};
    }
    queueHead_ = 0;
    queueCount_ = 0;
    scanCount_ = 0;
    scanUnread_ = 0;
    scanSeen_ = 0;
    records_ = 0;
    worstFlushUs_ = 0;
    dropped_ = 0;
    droppedInEpisode_ = 0;
    writeFail_ = 0;
    episode_ = false;
    overflowNoticed_ = false;
    queueFullNoticed_ = false;
    failLogArmed_ = false;
    failLogUs_ = 0;
    notice_ = WriteNotice::None;
    slotsFullLogged_ = false;
  }

  void buildPath(const Slot& s, const char* suffix, char* out, size_t cap) const {
    snprintf(out, cap, "%s%s%s", vol_->prefix(), s.id, suffix);
  }

  int findOrCreateSlot(const char* id, ChatKind kind, const char* name, const uint8_t* key) {
    int at = findSlot(id);
    if (at >= 0) return at;
    for (int i = 0; i < kMaxChatSlots; ++i) {
      if (slots_[i].used) continue;
      Slot& s = slots_[i];
      s = Slot{};
      s.used = true;
      s.kind = (uint8_t)kind;
      // scanDirectory() ran at begin() and listed every existing log, so a slot created later has no
      // file and its first record is id 1. Mark it scanned: nothing here may touch the card.
      s.scanned = true;
      // EST-367: a reused slot starts with no summary, so a stale preview cannot survive the slot.
      if (summaries_ != nullptr) summaries_[i] = ChatSummary{};
      snprintf(s.id, sizeof(s.id), "%s", id);
      snprintf(s.name, sizeof(s.name), "%s", name != nullptr ? name : "");
      if (key != nullptr) {
        memcpy(s.key, key, kMeshPubKey);
        s.hasKey = true;
      }
      ++slotCount_;
      return i;
    }
    return -1;
  }

  int ensureSlot(const char* id, ChatKind kind, const char* name, const uint8_t* key) {
    if (!ready_) return -1;
    int at = findOrCreateSlot(id, kind, name, key);
    if (at < 0) {
      full_ = true;
      if (!slotsFullLogged_) {
        slotsFullLogged_ = true;
        logLine("[hist] chat table full (32 slots): a new chat is not stored");
      }
      return -1;
    }
    Slot& s = slots_[at];
    // A contact or channel can be renamed: the RAM name wins, the stored Chat header stays as it was
    // written (it only names the thread on the next boot).
    if (name != nullptr && name[0] != 0) snprintf(s.name, sizeof(s.name), "%s", name);
    if (!s.hasKey && key != nullptr) {
      memcpy(s.key, key, kMeshPubKey);
      s.hasKey = true;
    }
    // EST-349: no volume call here. Every read needed to keep a log id coherent happens at boot
    // (scanDirectory + scanAllTails) or inside flushStep on the loop task.
    return at;
  }

  // Reads the first record of .log (or .old) and fills the slot's identity. Cheap: one small read.
  void readChatHeader(Slot& s) {
    char path[64];
    buildPath(s, ".log", path, sizeof(path));
    if (!vol_->exists(path)) buildPath(s, ".old", path, sizeof(path));
    if (!vol_->exists(path)) return;
    HistoryFile* f = vol_->openRead(path);
    if (f == nullptr) return;
    uint8_t buf[kHeaderBytes + kMaxPayload];
    const int n = f->read(buf, (int)sizeof(buf));
    f->close();
    const int r = decodeRecordAt(buf, n, 0, nullptr);
    if (r < 0) return;
    RecordRef ref;
    decodeRecordAt(buf, n, 0, &ref);
    if (ref.type != Type::Chat) return;
    ChatBody body;
    if (!decodeChatPayload(ref.payload, (int)ref.len - kHeaderBytes, &body)) return;
    snprintf(s.name, sizeof(s.name), "%s", body.name);
    s.kind = (uint8_t)body.kind;
    if (body.hasKey) {
      memcpy(s.key, body.key, kMeshPubKey);
      s.hasKey = true;
    }
  }

  void scanDirectory() {
    if (vol_ == nullptr) return;
    vol_->list(&listTrampoline, this);
  }

  // Boot only (setup, loop task). One tail read per known log makes maxLogId/seen/unread RAM truth,
  // so appendMsg() never has to look at the card. A torn tail is repaired later, in flushStep().
  void scanAllTails() {
    for (int i = 0; i < kMaxChatSlots; ++i) {
      if (slots_[i].used) scanTail(slots_[i]);
    }
  }

  static void listTrampoline(void* ctx, const char* name, uint32_t size) {
    static_cast<HistoryStore*>(ctx)->onListed(name, size);
  }

  void onListed(const char* name, uint32_t size) {
    if (name == nullptr) return;
    // Skip directories and anything that is not ours.
    const char* dot = strrchr(name, '.');
    if (dot == nullptr) return;
    const bool isLog = strcmp(dot, ".log") == 0;
    const bool isOld = strcmp(dot, ".old") == 0;
    const bool isTmp = strcmp(dot, ".tmp") == 0;
    char id[24];
    const int idLen = (int)(dot - name);
    if (idLen <= 0 || idLen >= (int)sizeof(id)) return;
    memcpy(id, name, (size_t)idLen);
    id[idLen] = 0;
    if (isTmp) {
      // A repair that was interrupted: the .log is still the truth and the next repair removes the
      // partial copy before it starts. Do NOT remove it while the directory is being listed.
      return;
    }
    if (!isLog && !isOld) return;
    ChatKind kind = strncmp(id, "dm-", 3) == 0 ? ChatKind::Dm : ChatKind::Channel;
    int at = findOrCreateSlot(id, kind, "", nullptr);
    if (at < 0) return;
    Slot& s = slots_[at];
    if (isLog) s.fileBytes = size; else s.oldBytes = size;
    totalBytes_ += size;
    if (!s.hasKey && s.name[0] == 0) readChatHeader(s);
  }

  // One backwards window. Returns the first record's index in `buf` (or -1) and the valid end.
  int readWindow(const Slot& s, bool old, uint32_t end, uint32_t* startOut, int* lenOut,
                 bool* tornOkOut) {
    char path[64];
    buildPath(s, old ? ".old" : ".log", path, sizeof(path));
    const uint32_t fileSize = old ? s.oldBytes : s.fileBytes;
    if (end == 0 || end > fileSize) return -1;
    const uint32_t start = end > (uint32_t)kWindowBytes ? end - (uint32_t)kWindowBytes : 0;
    const int want = (int)(end - start);
    if (want <= 0) return -1;
    HistoryFile* f = vol_->openRead(path);
    if (f == nullptr) return -1;
    if (!f->seek(start)) {
      f->close();
      return -1;
    }
    const int got = f->read(window_, want);
    f->close();
    if (got <= 0) return -1;
    *startOut = start;
    *lenOut = got;
    *tornOkOut = (start == 0 && end == fileSize);
    return 0;
  }

  int collect(int slot, uint32_t before, int cap, bool wantMeta) {
    scanCount_ = 0;
    scanUnread_ = 0;
    scanSeen_ = 0;
    readStats_ = ReadStats{};
    if (!ready_ || slot < 0 || slot >= kMaxChatSlots || !slots_[slot].used) return 0;
    Slot& s = slots_[slot];
    if (!s.scanned && !scanTail(s)) return 0;
    int count = 0;
    PendingUpdate pending[kPendingUpdates];
    for (int i = 0; i < kPendingUpdates; ++i) pending[i] = PendingUpdate{};
    bool metaFound = false;
    int windows = 0;
    bool old = false;
    uint32_t end = s.fileBytes;
    if (end == 0 && s.oldBytes > 0) {
      old = true;
      end = s.oldBytes;
    }
    while (end > 0) {
      const bool stillNeed = count < cap;
      const bool metaNeed = wantMeta && !metaFound && windows < kMetaSearchWindows;
      if (!stillNeed && !metaNeed) break;
      uint32_t start = 0;
      int len = 0;
      bool tornOk = false;
      if (readWindow(s, old, end, &start, &len, &tornOk) < 0) break;
      ++windows;
      ++readStats_.windows;
      readStats_.bytes += (uint32_t)len;
      int validEnd = 0;
      const int first = findWindowStart(window_, len, tornOk, &validEnd);
      if (first < 0) break;
      // One forward pass stores each record's offset (two bytes, not a 24-byte ref), then the loop
      // consumes them backwards so updates are met before their message.
      int refCount = 0;
      int pos = first;
      const int stop = validEnd;
      while (pos + kHeaderBytes <= stop && refCount < kWindowRefs) {
        RecordRef ref;
        const int r = decodeRecordAt(window_ + pos, stop - pos, 0, &ref);
        if (r < 0) break;
        offs_[refCount++] = (uint16_t)pos;
        pos += r;
      }
      for (int i = refCount - 1; i >= 0; --i) {
        RecordRef ref;
        const int at = offs_[i];
        if (decodeRecordAt(window_ + at, stop - at, start + (uint32_t)at, &ref) < 0) continue;
        const int bodyLen = (int)ref.len - kHeaderBytes;
        if (ref.type == Type::Msg) {
          Msg m;
          if (!decodeMsgPayload(ref.payload, bodyLen, &m)) continue;
          for (int k = 0; k < kPendingUpdates; ++k) {
            if (pending[k].used && pending[k].u.logId == m.logId) {
              applyUpdate(m, pending[k].u);
              pending[k].used = false;
            }
          }
          if (count < cap && m.logId < before) scan_[count++] = m;
        } else if (ref.type == Type::Update) {
          Update u;
          if (!decodeUpdatePayload(ref.payload, bodyLen, &u)) continue;
          addPendingUpdate(pending, u);
        } else if (ref.type == Type::Meta && wantMeta && !metaFound && !old) {
          // Only the current .log is trusted for unread: after a rotation the older .old may hold a
          // Meta from before the user last read the chat.
          Meta meta;
          if (decodeMetaPayload(ref.payload, bodyLen, &meta)) {
            scanUnread_ = meta.unread;
            scanSeen_ = meta.seenLogId;
            metaFound = true;
          }
        }
      }
      const uint32_t consumedTo = start + (uint32_t)first;
      if (consumedTo == 0) {
        if (!old && s.oldBytes > 0) {
          old = true;
          end = s.oldBytes;
        } else {
          break;
        }
      } else {
        end = consumedTo;
      }
    }
    // The messages were collected newest first; the readers want oldest first.
    for (int i = 0; i < count / 2; ++i) {
      const Msg t = scan_[i];
      scan_[i] = scan_[count - 1 - i];
      scan_[count - 1 - i] = t;
    }
    scanCount_ = count;
    if (!metaFound) scanUnread_ = s.unread;
    return scanCount_;
  }

  static void addPendingUpdate(PendingUpdate* pending, const Update& u) {
    int freeSlot = -1;
    for (int i = 0; i < kPendingUpdates; ++i) {
      if (pending[i].used && pending[i].u.logId == u.logId) {
        mergeUpdate(pending[i].u, u);
        return;
      }
      if (!pending[i].used && freeSlot < 0) freeSlot = i;
    }
    if (freeSlot < 0) return;   // more than 16 distinct updated messages in one window: drop
    pending[freeSlot].used = true;
    pending[freeSlot].u = u;
  }

  static void mergeUpdate(Update& into, const Update& u) {
    if (u.kind == kUpdateRepeats) {
      into.repeats = u.repeats;
      if (into.kind == kUpdateState) into.kind = kUpdateBoth;
    } else {
      into.delivery = u.delivery;
      into.ackWall = u.ackWall;
      into.ackReal = u.ackReal;
      if (u.kind == kUpdateBoth) into.repeats = u.repeats;
      into.kind = into.kind == kUpdateRepeats ? (uint8_t)kUpdateBoth : into.kind;
    }
  }

  // Reads the tail of .log (and .old when .log has no message yet) to learn maxLogId, the seen
  // point, the unread count and whether the last record is torn.
  bool scanTail(Slot& s) {
    s.scanned = true;
    s.needsRepair = false;
    s.repairBytes = 0;
    bool gotMsg = false;
    if (s.fileBytes > 0) {
      gotMsg = scanTailFile(s, false);
      // The file already starts with a Chat header; never write a second one.
      s.headerWritten = s.fileBytes > 0;
    }
    if (!gotMsg && s.oldBytes > 0) scanTailFile(s, true);
    return true;
  }

  bool scanTailFile(Slot& s, bool old) {
    const uint32_t fileSize = old ? s.oldBytes : s.fileBytes;
    if (fileSize == 0) return false;
    char path[64];
    buildPath(s, old ? ".old" : ".log", path, sizeof(path));
    HistoryFile* f = vol_->openRead(path);
    if (f == nullptr) return false;
    const uint32_t start = fileSize > (uint32_t)kWindowBytes ? fileSize - (uint32_t)kWindowBytes : 0;
    const int want = (int)(fileSize - start);
    if (!f->seek(start)) {
      f->close();
      return false;
    }
    const int got = f->read(window_, want);
    f->close();
    if (got <= 0) return false;
    int validEnd = 0;
    const int first = findWindowStart(window_, got, true, &validEnd);
    if (first < 0) {
      if (!old) {
        s.needsRepair = true;
        s.repairBytes = 0;
      }
      return false;
    }
    if (!old && start + (uint32_t)validEnd < fileSize) {
      s.needsRepair = true;
      s.repairBytes = start + (uint32_t)validEnd;
    }
    bool sawMsg = false;
    int pos = first;
    while (pos + kHeaderBytes <= got) {
      RecordRef ref;
      const int r = decodeRecordAt(window_ + pos, got - pos, start + (uint32_t)pos, &ref);
      if (r < 0) break;
      const int bodyLen = (int)ref.len - kHeaderBytes;
      if (ref.type == Type::Msg) {
        Msg m;
        if (decodeMsgPayload(ref.payload, bodyLen, &m)) {
          sawMsg = true;
          if (m.logId > s.maxLogId) s.maxLogId = m.logId;
        }
      } else if (ref.type == Type::Meta) {
        Meta meta;
        if (decodeMetaPayload(ref.payload, bodyLen, &meta)) {
          if (meta.seenLogId > s.seenLogId) s.seenLogId = meta.seenLogId;
          if (!old) s.unread = meta.unread;
        }
      }
      pos += r;
    }
    return sawMsg;
  }

  // Rewrites the .log without its torn tail (copy the valid prefix to .tmp, then rename).
  bool repairTail(Slot& s) {
    char logPath[64], tmpPath[64];
    buildPath(s, ".log", logPath, sizeof(logPath));
    buildPath(s, ".tmp", tmpPath, sizeof(tmpPath));
    if (vol_->exists(tmpPath)) vol_->remove(tmpPath);
    if (s.repairBytes > 0) {
      HistoryFile* in = vol_->openRead(logPath);
      HistoryFile* out = vol_->openAppend(tmpPath);
      if (in == nullptr || out == nullptr) {
        if (in != nullptr) in->close();
        if (out != nullptr) out->close();
        return false;
      }
      uint8_t chunk[512];
      uint32_t left = s.repairBytes;
      bool ok = true;
      while (left > 0) {
        const int want = left > sizeof(chunk) ? (int)sizeof(chunk) : (int)left;
        const int got = in->read(chunk, want);
        if (got <= 0) {
          ok = false;
          break;
        }
        if (out->write(chunk, got) != got) {
          ok = false;
          break;
        }
        left -= (uint32_t)got;
      }
      in->close();
      out->flush();
      out->close();
      if (!ok) {
        vol_->remove(tmpPath);
        return false;
      }
      const uint32_t oldSize = vol_->size(logPath);
      vol_->remove(logPath);
      if (!vol_->rename(tmpPath, logPath)) return false;
      totalBytes_ = totalBytes_ >= oldSize ? totalBytes_ - oldSize : 0;
      totalBytes_ += s.repairBytes;
      s.fileBytes = s.repairBytes;
    } else {
      const uint32_t oldSize = vol_->size(logPath);
      vol_->remove(logPath);
      totalBytes_ = totalBytes_ >= oldSize ? totalBytes_ - oldSize : 0;
      s.fileBytes = 0;
      s.headerWritten = false;
    }
    s.needsRepair = false;
    s.repairBytes = 0;
    s.headerWritten = s.fileBytes > 0;
    logLine("[hist] repaired a torn last record: the rest of the log is intact");
    return true;
  }

  bool enqueue(int slot, const uint8_t* data, int len) {
    if (!ready_ || queue_ == nullptr || queueSlots_ <= 0) return false;
    if (len <= 0 || len > kMaxRecord) return false;
    // EST-349: NEVER write from here. This runs on the send path and inside MeshCore callbacks; the
    // card is touched only by flushStep() on the loop task.
    // EST-380: a full queue is now the ONLY way a message is lost, so it is counted and said out
    // loud: during a card-error episode the pop-up learns that records are really gone, and a burst
    // that overflows while writes work gets its own QueueFull notice.
    if (queueCount_ >= queueSlots_) {
      ++dropped_;
      if (episode_) {
        ++droppedInEpisode_;
        if (!overflowNoticed_) {
          overflowNoticed_ = true;
          notice_ = WriteNotice::CardErrorDropped;
        }
      } else if (!queueFullNoticed_) {
        queueFullNoticed_ = true;
        notice_ = WriteNotice::QueueFull;
      }
      return false;
    }
    HistoryQueuedRecord& p = queue_[(queueHead_ + queueCount_) % queueSlots_];
    p.slot = (uint8_t)slot;
    p.len = (uint16_t)len;
    p.failed = false;   // a recycled slot must not inherit the previous record's failure
    memcpy(p.data, data, (size_t)len);
    ++queueCount_;
    return true;
  }

  // `partial` (EST-380): the header write returned a short count, so bytes are on the card but
  // headerWritten stays false; the caller must repair the tail before the retry.
  bool writeHeader(Slot& s, HistoryFile* f, bool* partial = nullptr) {
    char name[kMeshNameCap];
    snprintf(name, sizeof(name), "%s", s.name);
    uint8_t payload[kMaxPayload];
    const int n = encodeChatPayload((ChatKind)s.kind, name, s.hasKey ? s.key : nullptr, payload,
                                    (int)sizeof(payload));
    if (n < 0) return false;
    uint8_t rec[kMaxRecord];
    const int rn = encodeRecord(Type::Chat, payload, n, rec, (int)sizeof(rec));
    if (rn < 0) return false;
    const int written = f->write(rec, rn);
    if (written != rn) {
      if (partial != nullptr && written > 0) *partial = true;
      return false;
    }
    s.fileBytes += (uint32_t)rn;
    totalBytes_ += (uint32_t)rn;
    s.headerWritten = true;
    return true;
  }

  bool rotate(Slot& s) {
    char logPath[64], oldPath[64];
    buildPath(s, ".log", logPath, sizeof(logPath));
    buildPath(s, ".old", oldPath, sizeof(oldPath));
    if (vol_->exists(oldPath)) {
      const uint32_t n = vol_->size(oldPath);
      if (vol_->remove(oldPath)) totalBytes_ = totalBytes_ >= n ? totalBytes_ - n : 0;
      s.oldBytes = 0;
    }
    if (vol_->exists(logPath)) {
      if (!vol_->rename(logPath, oldPath)) return false;
      s.oldBytes = s.fileBytes;
    }
    s.fileBytes = 0;
    s.headerWritten = false;
    logLine("[hist] chat log reached its cap, rotated to .old");
    return true;
  }

  // The ONE place that touches the volume. Runs from history::loop() on the loop task, never from a
  // caller, an ISR, a MeshCore callback or a critical section (EST-349).
  //
  // EST-380: a volume error keeps the record. `Failed` leaves the head of the queue exactly as it
  // was, so the next loop pass retries it; a card error never drops a message. Only two things drop
  // on purpose: a cap (`full_`) and an id collision after a late scan, both counted in `dropped_`.
  FlushOutcome flushOne() {
    if (!ready_ || queue_ == nullptr || queueCount_ == 0) return FlushOutcome::Dropped;
    HistoryQueuedRecord& p = queue_[queueHead_];
    Slot& s = slots_[p.slot];
    // Safety net: every slot is scanned at begin(), so this only fires for a slot that appeared
    // after boot. It is still the loop task doing it, before the first write of that slot.
    const bool wasScanned = s.scanned;
    if (!wasScanned) scanTail(s);
    if (s.needsRepair && !repairTail(s)) return failWrite("repair");
    if (!wasScanned) {
      // A late scan found log ids this boot had not seen: never overwrite them. Drop the record
      // rather than write a colliding id.
      uint32_t recId = 0;
      RecordRef ref;
      if (decodeRecordAt(p.data, (int)p.len, 0, &ref) == (int)p.len) {
        if (ref.type == Type::Msg) {
          Msg m;
          if (decodeMsgPayload(ref.payload, (int)ref.len - kHeaderBytes, &m)) recId = m.logId;
        } else if (ref.type == Type::Update) {
          Update u;
          if (decodeUpdatePayload(ref.payload, (int)ref.len - kHeaderBytes, &u)) recId = u.logId;
        }
      }
      if (recId != 0 && recId <= s.maxLogId) {
        ++dropped_;
        popQueue();
        return FlushOutcome::Dropped;
      }
    }
    const uint32_t t0 = nowUs_ != nullptr ? nowUs_() : 0;
    // Rotate FIRST: replacing .old frees room, so a store at the total cap can still move on.
    if (s.fileBytes > 0 && s.fileBytes + p.len + (s.headerWritten ? 0 : (uint32_t)kMaxRecord) >
                              cfg_.perChatCap) {
      if (!rotate(s)) return failWrite("rotate");
    }
    if (totalBytes_ + p.len > cfg_.totalCap) {
      full_ = true;
      ++dropped_;
      popQueue();
      return FlushOutcome::Dropped;
    }
    char path[64];
    buildPath(s, ".log", path, sizeof(path));
    HistoryFile* f = vol_->openAppend(path);
    if (f == nullptr) return failWrite("open");
    bool ok = true;
    bool partial = false;
    if (!s.headerWritten && !writeHeader(s, f, &partial)) ok = false;
    if (ok) {
      const int wn = f->write(p.data, p.len);
      if (wn != p.len) {
        ok = false;
        if (wn > 0) partial = true;   // a short write left bytes after the last valid record
      }
    }
    if (ok) {
      s.fileBytes += p.len;
      totalBytes_ += p.len;
      ++records_;
    }
    f->flush();
    f->close();
    const uint32_t t1 = nowUs_ != nullptr ? nowUs_() : 0;
    const uint32_t dt = t1 - t0;
    if (dt > worstFlushUs_) worstFlushUs_ = dt;
    if (!ok) {
      // A partial write leaves a torn record; the retry repairs the tail first, otherwise every
      // later record would sit behind it and read as lost. A zero-byte failure needs no repair.
      if (partial) {
        s.needsRepair = true;
        s.repairBytes = s.fileBytes;
      }
      return failWrite("write");
    }
    popQueue();
    return FlushOutcome::Written;
  }

  // EST-380: one card error, said at most once a minute, and the record is marked as failed ONCE
  // (hist_write_fail counts records, not the retries). The first failure of an episode also arms the
  // shell's one pop-up.
  FlushOutcome failWrite(const char* why) {
    HistoryQueuedRecord& p = queue_[queueHead_];
    if (!p.failed) {
      p.failed = true;
      ++writeFail_;
    }
    if (!episode_) {
      episode_ = true;
      droppedInEpisode_ = 0;
      overflowNoticed_ = false;
      notice_ = WriteNotice::CardError;
    }
    const uint32_t now = nowUs_ != nullptr ? nowUs_() : 0;
    if (!failLogArmed_ || (uint32_t)(now - failLogUs_) >= 60000000u) {
      failLogArmed_ = true;
      failLogUs_ = now;
      char line[72];
      snprintf(line, sizeof(line), "[hist] write FAILED %s", why);
      logLine(line);
    }
    return FlushOutcome::Failed;
  }

  // EST-382: removes exactly this slot's records from the RAM queue, keeping the order of everyone
  // else's. The head is re-based to 0 when the queue empties; no volume access happens here.
  int dropQueuedForSlot(int slot) {
    if (queue_ == nullptr || queueSlots_ <= 0 || queueCount_ <= 0) return 0;
    int kept = 0;
    int dropped = 0;
    for (int i = 0; i < queueCount_; ++i) {
      const int at = (queueHead_ + i) % queueSlots_;
      if ((int)queue_[at].slot == slot) {
        ++dropped;
        continue;
      }
      if (kept != i) queue_[(queueHead_ + kept) % queueSlots_] = queue_[at];
      ++kept;
    }
    queueCount_ = kept;
    if (kept == 0) queueHead_ = 0;
    if (queueCount_ < queueSlots_) queueFullNoticed_ = false;
    return dropped;
  }

  void popQueue() {
    if (queueSlots_ <= 0) {
      queueHead_ = 0;
      queueCount_ = 0;
      return;
    }
    queueHead_ = (queueHead_ + 1) % queueSlots_;
    if (queueCount_ > 0) --queueCount_;
    if (queueCount_ < queueSlots_) queueFullNoticed_ = false;
  }

  // EST-380: there is deliberately no runtime switch to the SPIFFS fallback any more. The EST-349
  // path moved the store to SPIFFS on the first failed write and DROPPED the whole queue (its ids
  // were assigned against the card's counters): exactly the silent loss this ticket removes. A card
  // write error now keeps every record in RAM and retries it (flushOne -> failWrite); the fallback is
  // still used when the card cannot be mounted at all, chosen once in begin().

  HistoryVolume* primary_ = nullptr;
  HistoryVolume* fallback_ = nullptr;
  HistoryVolume* vol_ = nullptr;
  HistoryConfig primaryCfg_{};
  HistoryConfig fallbackCfg_{};
  HistoryConfig cfg_{};
  NowFn nowUs_ = nullptr;
  LogFn log_ = nullptr;

  Slot slots_[kMaxChatSlots] = {};
  int slotCount_ = 0;
  uint32_t totalBytes_ = 0;
  bool ready_ = false;
  bool full_ = false;

  // EST-349: every large buffer is caller-owned (PSRAM on the device), so none of the store's
  // working memory is static RAM.
  HistoryScratch scratch_{};
  HistoryQueuedRecord* queue_ = nullptr;
  int queueSlots_ = 0;
  int queueHead_ = 0;
  int queueCount_ = 0;

  Msg* scan_ = nullptr;
  int scanCount_ = 0;
  uint16_t scanUnread_ = 0;
  uint32_t scanSeen_ = 0;

  uint8_t* window_ = nullptr;
  uint16_t* offs_ = nullptr;
  ChatSummary* summaries_ = nullptr;   // EST-367: the selector's per-chat preview
  ReadStats readStats_{};              // EST-367: the last backwards read's windows / bytes
  uint32_t records_ = 0;
  uint32_t worstFlushUs_ = 0;
  uint32_t dropped_ = 0;
  // EST-380: the retry state. `episode_` is true from the first failed append until the backlog is
  // on the card again; `writeFail_` counts records that failed at least once; `notice_` is the one
  // pop-up the shell has not taken yet; `failLogArmed_`/`failLogUs_` throttle `[hist] write FAILED`.
  uint32_t writeFail_ = 0;
  uint32_t droppedInEpisode_ = 0;
  bool episode_ = false;
  bool overflowNoticed_ = false;
  bool queueFullNoticed_ = false;
  bool failLogArmed_ = false;
  uint32_t failLogUs_ = 0;
  WriteNotice notice_ = WriteNotice::None;
  bool slotsFullLogged_ = false;
  mutable HistoryStatus status_{};
};

}  // namespace history
}  // namespace om
