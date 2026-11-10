// SPDX-License-Identifier: MIT
// One-time migration helpers for EST-340: old OM Lite storage -> MeshCore companion storage.
//
// This header is plain C++ (no Arduino, no SPIFFS): the device code in mesh_link.cpp reads the
// old NVS/file values, these functions decide the bytes, and test/host/test_mesh_migration.cpp
// proves the rules with the system g++.
//
// What moves:
//   * identity: the old firmware used MeshCore's own IdentityStore at /identity/_main, and the
//     companion DataStore uses exactly the same file. Nothing is rewritten; the rule here is
//     that the public key must be identical (and the boot log prints its first 8 bytes).
//   * channels: old NVS slots are 16-byte keys (128-bit); MeshCore's GroupChannel is 32 bytes
//     and treats a zero upper half as a 128-bit key, so the 16 bytes go in [0..15] and
//     [16..31] stays zero. setChannel() then derives the same sha256(secret,16) hash.
//   * contacts: the old /contacts file (EST-329) had a 12-byte header and CRC; /contacts3 has
//     fixed 152-byte records with no header. decodeLegacyContacts() validates and exposes them.
//   * path hash: old NVS "phs" is 1..3 bytes; MeshCore stores mode = bytes - 1 (2 -> 1).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace om {

// --- identity ------------------------------------------------------------------------------------
constexpr int kMigrationKeySize = 32;

inline bool identityPreserved(const uint8_t before[kMigrationKeySize],
                              const uint8_t after[kMigrationKeySize]) {
  return before != nullptr && after != nullptr &&
         memcmp(before, after, kMigrationKeySize) == 0;
}

// First 8 bytes as hex, for the boot proof line `[mc] identity <16 hex chars>`.
inline void identityPrefixHex(const uint8_t key[kMigrationKeySize], char* out, size_t cap) {
  static const char kHex[] = "0123456789abcdef";
  if (out == nullptr || cap == 0) return;
  out[0] = 0;
  if (key == nullptr || cap < 17) return;
  for (int i = 0; i < 8; ++i) {
    out[i * 2] = kHex[key[i] >> 4];
    out[i * 2 + 1] = kHex[key[i] & 0x0F];
  }
  out[16] = 0;
}

// --- channels ------------------------------------------------------------------------------------
struct MigratedChannel {
  char name[32];
  uint8_t secret[32];
};

// Map one old 16-byte channel key into MeshCore's 32-byte group secret. Returns false for a
// missing/empty name, which is not a channel slot worth migrating.
inline bool migrateChannel(const char* name, const uint8_t secret16[16], MigratedChannel& out) {
  if (name == nullptr || name[0] == 0 || secret16 == nullptr) return false;
  memset(&out, 0, sizeof(out));
  size_t n = 0;
  while (name[n] != 0 && n < sizeof(out.name) - 1) {
    out.name[n] = name[n];
    ++n;
  }
  out.name[n] = 0;
  memcpy(out.secret, secret16, 16);
  return true;
}

// NVS "phs" (1..3 bytes) -> NodePrefs::path_hash_mode (0..2). Out-of-range values fall back to
// 2 bytes (mode 1), the SLO default this firmware has run since EST-336.
inline uint8_t pathHashModeFromLegacy(uint8_t phs) {
  if (phs < 1 || phs > 3) return 1;
  return (uint8_t)(phs - 1);
}

// --- legacy contact file (/contacts, EST-329) ----------------------------------------------------
// One file, little endian:
//   header 12 B: magic "OMC1", version u16, record count u16, CRC32 of the records u32
//   records:    fixed 152 B each
constexpr uint32_t kLegacyContactMagic = 0x31434D4Fu;   // 'O','M','C','1' little endian
constexpr uint16_t kLegacyContactVersion = 1;
constexpr int kLegacyMaxContactRecords = 32;
constexpr size_t kLegacyContactKeySize = 32;
constexpr size_t kLegacyContactNameSize = 32;
constexpr size_t kLegacyContactPathSize = 64;
constexpr size_t kLegacyContactHeaderSize = 12;
constexpr size_t kLegacyContactRecordSize = 152;
constexpr size_t kLegacyContactMaxFileSize =
    kLegacyContactHeaderSize + (size_t)kLegacyMaxContactRecords * kLegacyContactRecordSize;

struct LegacyContact {
  uint8_t pub_key[kLegacyContactKeySize];
  char name[kLegacyContactNameSize];
  uint8_t type;
  uint8_t flags;
  uint8_t out_path_len;
  uint8_t out_path[kLegacyContactPathSize];
  uint32_t last_advert_ts;
  uint32_t lastmod;
  int32_t gps_lat;
  int32_t gps_lon;
  uint32_t sync_since;
};

inline uint16_t legacyGet16(const uint8_t* p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

inline uint32_t legacyGet32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

inline uint32_t legacyCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

// Returns the record count (0 = valid empty file) or -1 for a truncated/padded image, another
// version, a CRC mismatch or a field a written file can never contain.
inline int decodeLegacyContacts(const uint8_t* buf, size_t len, LegacyContact* out, int max) {
  if (buf == nullptr || out == nullptr || max < 0) return -1;
  if (len < kLegacyContactHeaderSize) return -1;
  if (legacyGet32(buf) != kLegacyContactMagic) return -1;
  if (legacyGet16(buf + 4) != kLegacyContactVersion) return -1;
  const int count = (int)legacyGet16(buf + 6);
  if (count > kLegacyMaxContactRecords || count > max) return -1;

  const size_t payload = (size_t)count * kLegacyContactRecordSize;
  if (len != kLegacyContactHeaderSize + payload) return -1;
  if (legacyCrc32(buf + kLegacyContactHeaderSize, payload) != legacyGet32(buf + 8)) return -1;

  for (int i = 0; i < count; ++i) {
    const uint8_t* p = buf + kLegacyContactHeaderSize + (size_t)i * kLegacyContactRecordSize;
    LegacyContact r;
    memset(&r, 0, sizeof(r));
    memcpy(r.pub_key, p, kLegacyContactKeySize);
    memcpy(r.name, p + 32, kLegacyContactNameSize);
    r.type = p[64];
    r.flags = p[65];
    r.out_path_len = p[66];
    memcpy(r.out_path, p + 68, kLegacyContactPathSize);
    r.last_advert_ts = legacyGet32(p + 132);
    r.lastmod = legacyGet32(p + 136);
    r.gps_lat = (int32_t)legacyGet32(p + 140);
    r.gps_lon = (int32_t)legacyGet32(p + 144);
    r.sync_since = legacyGet32(p + 148);

    if (p[67] != 0) return -1;                     // reserved byte must stay zero
    if (r.type == 0) return -1;                    // ADV_TYPE_NONE is never persisted
    if (r.name[0] == 0) return -1;
    if (memchr(r.name, 0, kLegacyContactNameSize) == nullptr) return -1;
    if (r.out_path_len != 0xFF && r.out_path_len > kLegacyContactPathSize) return -1;
    out[i] = r;
  }
  return count;
}

// --- /contacts3 (MeshCore companion) image ------------------------------------------------------
// DataStore::loadContacts() reads fixed 152-byte records with no header, in this order:
//   pub_key 32, name 32, type 1, flags 1, unused 1, sync_since 4, out_path_len 1,
//   last_advert_ts 4, out_path 64, lastmod 4, gps_lat 4, gps_lon 4
// The migration builds the whole image once and writes it in one call, so the byte order exists in
// exactly one place (and one host test).
//   pub 0..31 | name 32..63 | type 64 | flags 65 | unused 66 | sync_since 67..70 |
//   path_len 71 | last_advert 72..75 | path 76..139 | lastmod 140..143 | lat 144..147 |
//   lon 148..151                                                          = 152 bytes
constexpr size_t kNewContactTypeOffset = 64;
constexpr size_t kNewContactFlagsOffset = 65;
constexpr size_t kNewContactSyncSinceOffset = 67;
constexpr size_t kNewContactPathLenOffset = 71;
constexpr size_t kNewContactLastAdvertOffset = 72;
constexpr size_t kNewContactPathOffset = 76;
constexpr size_t kNewContactLastModOffset = 140;
constexpr size_t kNewContactLatOffset = 144;
constexpr size_t kNewContactLonOffset = 148;

inline void legacyPut32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

// Serialises n legacy records as the /contacts3 image. Returns false for a bad count or when the
// caller's buffer is too small; an empty file (n == 0) is a valid image of 0 bytes.
inline bool encodeContacts3(const LegacyContact* recs, int n, uint8_t* out, size_t cap,
                            size_t* outLen) {
  if (outLen != nullptr) *outLen = 0;
  if (n < 0 || n > kLegacyMaxContactRecords || (n > 0 && (recs == nullptr || out == nullptr))) {
    return false;
  }
  const size_t need = (size_t)n * kLegacyContactRecordSize;
  if (need > cap) return false;
  for (int i = 0; i < n; ++i) {
    const LegacyContact& r = recs[i];
    uint8_t* p = out + (size_t)i * kLegacyContactRecordSize;
    memset(p, 0, kLegacyContactRecordSize);
    memcpy(p, r.pub_key, kLegacyContactKeySize);
    memcpy(p + 32, r.name, kLegacyContactNameSize);
    p[kNewContactTypeOffset] = r.type;
    p[kNewContactFlagsOffset] = r.flags;
    p[66] = 0;   // the "unused" byte, always zero (loadContacts skips it)
    legacyPut32(p + kNewContactSyncSinceOffset, r.sync_since);
    p[kNewContactPathLenOffset] = r.out_path_len;
    legacyPut32(p + kNewContactLastAdvertOffset, r.last_advert_ts);
    memcpy(p + kNewContactPathOffset, r.out_path, kLegacyContactPathSize);
    legacyPut32(p + kNewContactLastModOffset, r.lastmod);
    legacyPut32(p + kNewContactLatOffset, (uint32_t)r.gps_lat);
    legacyPut32(p + kNewContactLonOffset, (uint32_t)r.gps_lon);
  }
  if (outLen != nullptr) *outLen = need;
  return true;
}

// A /contacts3 file is complete when its size is exactly one record per decoded contact. 0 bytes
// is complete only when the old file decoded to zero contacts.
inline bool contactsFileComplete(size_t fileSize, int decodedCount) {
  if (decodedCount < 0 || decodedCount > kLegacyMaxContactRecords) return false;
  return fileSize == (size_t)decodedCount * kLegacyContactRecordSize;
}

// --- /channels2 (MeshCore companion) image ------------------------------------------------------
// DataStore::saveChannels() writes fixed 68-byte entries (4 unused + name 32 + secret 32), one per
// slot, up to MAX_GROUP_CHANNELS.
constexpr size_t kChannelRecordSize = 68;

inline bool channelsFileComplete(size_t fileSize, int slots) {
  if (slots < 0) return false;
  return fileSize == (size_t)slots * kChannelRecordSize;
}

}  // namespace om
