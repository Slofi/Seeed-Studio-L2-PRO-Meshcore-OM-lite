// SPDX-License-Identifier: MIT
// Contact persistence for OM Lite: the on-flash file format and the write rate limit.
//
// Why this file exists: MeshCore's BaseChatMesh keeps the contact table in RAM only; the companion
// firmware persists it in its own storage layer. We do the same through the public contact API
// (getContactByIdx / addContact) and a small, explicit file of our own, so a corrupt or half-written
// file can be detected and discarded instead of trusted. Nothing here is MeshCore code; the field
// names follow ContactInfo.h (MIT) so the mapping is obvious.
//
// This header is plain C++ (no Arduino, no LovyanGFX) so test/host can prove the format and the
// save policy with the system g++.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace om {

// --- file format ---------------------------------------------------------------------------------
// One file, little endian:
//   header 12 B: magic "OMC1", format version u16, record count u16, CRC32 of the records u32
//   records:    fixed 152 B each, in the order they are written
// A file whose size, magic, version, count, record fields or CRC disagree with the header is
// rejected as a whole; the caller then starts with an empty contact table (and logs it).
constexpr uint32_t kContactMagic = 0x31434D4Fu;   // bytes 'O','M','C','1' read little endian
constexpr uint16_t kContactVersion = 1;

// BaseChatMesh has MAX_CONTACTS (32) real slots plus MAX_ANON_CONTACTS (8) transient ones; only the
// real ones are worth persisting. mesh_link.cpp static_asserts that this is large enough.
constexpr int kMaxContactRecords = 32;

constexpr size_t kContactKeySize = 32;    // PUB_KEY_SIZE
constexpr size_t kContactNameSize = 32;   // ContactInfo::name
constexpr size_t kContactPathSize = 64;   // MAX_PATH_SIZE

constexpr size_t kContactHeaderSize = 12;
constexpr size_t kContactRecordSize = 152;
constexpr size_t kContactMaxFileSize =
    kContactHeaderSize + (size_t)kMaxContactRecords * kContactRecordSize;

// One contact as stored. Field widths match ContactInfo so the conversion is a copy, not a judgement
// call. out_path_len 0 = direct, 0xFF = unknown (MeshCore's OUT_PATH_UNKNOWN).
struct StoredContact {
  uint8_t pub_key[kContactKeySize];
  char name[kContactNameSize];
  uint8_t type;         // MeshCore ADV_TYPE_*; 0 (NONE) is never stored
  uint8_t flags;        // favourite / telemetry / remote CLI bits
  uint8_t out_path_len;
  uint8_t reserved;     // keeps the record 4-byte aligned; always 0
  uint8_t out_path[kContactPathSize];
  uint32_t last_advert_ts;   // by THEIR clock
  uint32_t lastmod;          // by OUR clock
  int32_t gps_lat;           // 1e-6 degrees
  int32_t gps_lon;
  uint32_t sync_since;       // room login bookkeeping
};

inline void contactPut16(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

inline void contactPut32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

inline uint16_t contactGet16(const uint8_t* p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

inline uint32_t contactGet32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

// CRC-32 (IEEE 802.3, reflected, init/final 0xFFFFFFFF) without a 1 KB table: this runs once per
// boot and once per save, on at most 4.9 KB, so a bitwise loop is plenty and costs no RAM.
inline uint32_t contactCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

// Serialise count records into out. Returns the number of bytes written, or 0 when count or cap is
// wrong (0 is never a valid size: the header alone is 12).
inline size_t contactEncode(const StoredContact* recs, int count, uint8_t* out, size_t cap) {
  if (!recs || !out || count < 0 || count > kMaxContactRecords) return 0;
  const size_t payload = (size_t)count * kContactRecordSize;
  const size_t need = kContactHeaderSize + payload;
  if (cap < need) return 0;

  contactPut32(out, kContactMagic);
  contactPut16(out + 4, kContactVersion);
  contactPut16(out + 6, (uint32_t)count);

  for (int i = 0; i < count; ++i) {
    const StoredContact& r = recs[i];
    uint8_t* p = out + kContactHeaderSize + (size_t)i * kContactRecordSize;
    memcpy(p, r.pub_key, kContactKeySize);
    memcpy(p + 32, r.name, kContactNameSize);
    p[64] = r.type;
    p[65] = r.flags;
    p[66] = r.out_path_len;
    p[67] = 0;
    memcpy(p + 68, r.out_path, kContactPathSize);
    contactPut32(p + 132, r.last_advert_ts);
    contactPut32(p + 136, r.lastmod);
    contactPut32(p + 140, (uint32_t)r.gps_lat);
    contactPut32(p + 144, (uint32_t)r.gps_lon);
    contactPut32(p + 148, r.sync_since);
  }
  contactPut32(out + 8, contactCrc32(out + kContactHeaderSize, payload));
  return need;
}

// Parse a complete file image. Returns the record count (0 is a valid, empty file) or -1 when the
// image is truncated, padded, of another version, fails the CRC, or holds a field that a written
// file can never contain. On -1 nothing in out may be used.
inline int contactDecode(const uint8_t* buf, size_t len, StoredContact* out, int max) {
  if (!buf || !out || max < 0) return -1;
  if (len < kContactHeaderSize) return -1;
  if (contactGet32(buf) != kContactMagic) return -1;
  if (contactGet16(buf + 4) != kContactVersion) return -1;
  const int count = (int)contactGet16(buf + 6);
  if (count > kMaxContactRecords || count > max) return -1;

  const size_t payload = (size_t)count * kContactRecordSize;
  if (len != kContactHeaderSize + payload) return -1;
  if (contactCrc32(buf + kContactHeaderSize, payload) != contactGet32(buf + 8)) return -1;

  for (int i = 0; i < count; ++i) {
    const uint8_t* p = buf + kContactHeaderSize + (size_t)i * kContactRecordSize;
    StoredContact r;
    memset(&r, 0, sizeof(r));
    memcpy(r.pub_key, p, kContactKeySize);
    memcpy(r.name, p + 32, kContactNameSize);
    r.type = p[64];
    r.flags = p[65];
    r.out_path_len = p[66];
    r.reserved = 0;
    memcpy(r.out_path, p + 68, kContactPathSize);
    r.last_advert_ts = contactGet32(p + 132);
    r.lastmod = contactGet32(p + 136);
    r.gps_lat = (int32_t)contactGet32(p + 140);
    r.gps_lon = (int32_t)contactGet32(p + 144);
    r.sync_since = contactGet32(p + 148);

    if (p[67] != 0) return -1;                       // reserved byte must stay zero
    if (r.type == 0) return -1;                      // ADV_TYPE_NONE is never persisted
    if (r.name[0] == 0) return -1;
    if (memchr(r.name, 0, kContactNameSize) == NULL) return -1;
    // 0xFF is MeshCore's OUT_PATH_UNKNOWN, not a length.
    if (r.out_path_len != 0xFF && r.out_path_len > kContactPathSize) return -1;
    out[i] = r;
  }
  return count;
}

// --- save policy ---------------------------------------------------------------------------------
// Flash wears out and adverts arrive in bursts, so a change marks the table dirty and the writer
// saves at most once per interval. The first change after boot is written immediately (it is the
// one a quick power cycle would otherwise lose); after that the limit applies to both directions:
// every later write waits at least the interval, successful or failed.
constexpr uint32_t kContactSaveMinIntervalMs = 60000;

class ContactSaveScheduler {
 public:
  explicit constexpr ContactSaveScheduler(uint32_t minIntervalMs = kContactSaveMinIntervalMs)
      : minIntervalMs_(minIntervalMs) {}

  // A contact was added, updated or had its path changed. Does not restart a running interval.
  void markDirty(uint32_t nowMs) {
    (void)nowMs;
    dirty_ = true;
  }

  bool due(uint32_t nowMs) const {
    if (!dirty_) return false;
    if (!attempted_) return true;
    return (uint32_t)(nowMs - lastAttemptMs_) >= minIntervalMs_;
  }

  void onSaveOk(uint32_t nowMs) {
    dirty_ = false;
    attempted_ = true;
    lastAttemptMs_ = nowMs;
  }

  void onSaveFailed(uint32_t nowMs) {
    attempted_ = true;   // stays dirty; retried after the interval, not in a tight loop
    lastAttemptMs_ = nowMs;
  }

  bool dirty() const { return dirty_; }

 private:
  uint32_t minIntervalMs_;
  uint32_t lastAttemptMs_ = 0;
  bool attempted_ = false;
  bool dirty_ = false;
};

// The record layout above is written by hand; keep it honest.
static_assert(kContactRecordSize == 32 + 32 + 4 + 64 + 4 + 4 + 4 + 4 + 4, "record size");
static_assert(kContactMaxFileSize == 12 + 32 * 152, "file size");

}  // namespace om
