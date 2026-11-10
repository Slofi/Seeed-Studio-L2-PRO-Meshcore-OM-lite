// SPDX-License-Identifier: MIT
// Host test for src/mesh_migration.h (EST-340): identity bytes, channel mapping (16-byte key ->
// MeshCore's 32-byte group secret), path-hash mode and the old /contacts file decode.
// Plain g++, no Arduino, so the exact bytes can be checked without a device.
#include <stdio.h>
#include <string.h>

#include <stdint.h>

#include "mesh_migration.h"

static int g_fail = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++g_fail;                                                           \
    }                                                                     \
  } while (0)

static void fillPattern(uint8_t* p, size_t n, uint8_t start = 1) {
  for (size_t i = 0; i < n; ++i) p[i] = (uint8_t)(start + i);
}

static void testIdentity() {
  uint8_t a[om::kMigrationKeySize], b[om::kMigrationKeySize];
  fillPattern(a, sizeof(a), 0x10);
  memcpy(b, a, sizeof(b));
  CHECK(om::identityPreserved(a, b));
  b[31] ^= 0x01;
  CHECK(!om::identityPreserved(a, b));
  memset(b, 0, sizeof(b));
  CHECK(!om::identityPreserved(a, b));
  CHECK(!om::identityPreserved(nullptr, b));

  char hex[17];
  om::identityPrefixHex(a, hex, sizeof(hex));
  CHECK(strcmp(hex, "1011121314151617") == 0);
  om::identityPrefixHex(a, hex, 8);   // too small: never writes a partial/half string
  CHECK(hex[0] == 0);
}

static void testChannels() {
  uint8_t key[16];
  fillPattern(key, sizeof(key), 0xA0);
  om::MigratedChannel out;
  CHECK(om::migrateChannel("Public", key, out));
  CHECK(strcmp(out.name, "Public") == 0);
  CHECK(memcmp(out.secret, key, 16) == 0);
  for (int i = 16; i < 32; ++i) CHECK(out.secret[i] == 0);

  // A 32-byte name does not fit; the result stays terminated inside the field.
  char longName[40];
  memset(longName, 'x', sizeof(longName) - 1);
  longName[sizeof(longName) - 1] = 0;
  CHECK(om::migrateChannel(longName, key, out));
  CHECK(strlen(out.name) == sizeof(out.name) - 1);
  CHECK(out.name[sizeof(out.name) - 1] == 0);

  CHECK(!om::migrateChannel("", key, out));
  CHECK(!om::migrateChannel(nullptr, key, out));
  CHECK(!om::migrateChannel("Public", nullptr, out));
}

static void testPathHashMode() {
  CHECK(om::pathHashModeFromLegacy(1) == 0);   // 1 byte
  CHECK(om::pathHashModeFromLegacy(2) == 1);   // 2 bytes: SLO stays on 2B
  CHECK(om::pathHashModeFromLegacy(3) == 2);   // 3 bytes
  CHECK(om::pathHashModeFromLegacy(0) == 1);   // never seen: the firmware default 2B
  CHECK(om::pathHashModeFromLegacy(4) == 1);
  CHECK(om::pathHashModeFromLegacy(255) == 1);
}

static void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static void put16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

// Build the old EST-329 /contacts image: header (magic, version, count, CRC) + 152-byte records.
static size_t makeLegacyFile(uint8_t* buf, size_t cap, int count, bool corruptCrc = false) {
  const size_t len = om::kLegacyContactHeaderSize + (size_t)count * om::kLegacyContactRecordSize;
  if (cap < len) return 0;
  memset(buf, 0, len);
  put32(buf, om::kLegacyContactMagic);
  put16(buf + 4, om::kLegacyContactVersion);
  put16(buf + 6, (uint16_t)count);
  for (int i = 0; i < count; ++i) {
    uint8_t* p = buf + om::kLegacyContactHeaderSize + (size_t)i * om::kLegacyContactRecordSize;
    fillPattern(p, 32, (uint8_t)(0x40 + i));                  // pub_key
    snprintf((char*)p + 32, 32, "node%d", i);
    p[64] = 2;                                                // repeater
    p[65] = 0x03;                                             // flags
    p[66] = 2;                                                // out_path_len
    p[67] = 0;                                                // reserved
    p[68] = 0xAA;                                             // path
    put32(p + 132, 100 + (uint32_t)i);                        // last_advert_ts
    put32(p + 136, 200 + (uint32_t)i);                        // lastmod
    put32(p + 140, (uint32_t)(46000000 + i));                 // lat
    put32(p + 144, (uint32_t)(14000000 + i));                 // lon
    put32(p + 148, 7);                                        // sync_since
  }
  uint32_t crc = om::legacyCrc32(buf + om::kLegacyContactHeaderSize,
                                 (size_t)count * om::kLegacyContactRecordSize);
  if (corruptCrc) crc ^= 0xFFFFFFFFu;
  put32(buf + 8, crc);
  return len;
}

static void testLegacyContacts() {
  static uint8_t buf[om::kLegacyContactMaxFileSize];
  const size_t len = makeLegacyFile(buf, sizeof(buf), 2);
  CHECK(len > 0);

  om::LegacyContact recs[4];
  CHECK(om::decodeLegacyContacts(buf, len, recs, 4) == 2);
  CHECK(strcmp(recs[0].name, "node0") == 0);
  CHECK(strcmp(recs[1].name, "node1") == 0);
  CHECK(recs[0].type == 2);
  CHECK(recs[0].flags == 0x03);
  CHECK(recs[0].out_path_len == 2);
  CHECK(recs[0].last_advert_ts == 100);
  CHECK(recs[0].lastmod == 200);
  CHECK(recs[0].gps_lat == 46000000);
  CHECK(recs[0].gps_lon == 14000000);
  CHECK(recs[0].sync_since == 7);
  CHECK(recs[0].pub_key[0] == 0x40);
  CHECK(recs[0].pub_key[31] == 0x5F);

  // Empty but valid.
  const size_t emptyLen = makeLegacyFile(buf, sizeof(buf), 0);
  CHECK(om::decodeLegacyContacts(buf, emptyLen, recs, 4) == 0);

  // A bad CRC is rejected as a whole (the caller then starts empty and logs it).
  makeLegacyFile(buf, sizeof(buf), 1, true);
  const size_t badLen = om::kLegacyContactHeaderSize + om::kLegacyContactRecordSize;
  CHECK(om::decodeLegacyContacts(buf, badLen, recs, 4) == -1);

  // Truncated / padded.
  const size_t goodLen = makeLegacyFile(buf, sizeof(buf), 1);
  CHECK(om::decodeLegacyContacts(buf, goodLen - 1, recs, 4) == -1);
  CHECK(om::decodeLegacyContacts(buf, goodLen + 1, recs, 4) == -1);

  // Fields a written file can never contain.
  makeLegacyFile(buf, sizeof(buf), 1);
  uint8_t* p = buf + om::kLegacyContactHeaderSize;
  p[67] = 1;   // reserved
  put32(buf + 8, om::legacyCrc32(p, om::kLegacyContactRecordSize));
  CHECK(om::decodeLegacyContacts(buf, goodLen, recs, 4) == -1);

  makeLegacyFile(buf, sizeof(buf), 1);
  p = buf + om::kLegacyContactHeaderSize;
  p[64] = 0;   // ADV_TYPE_NONE is never persisted
  put32(buf + 8, om::legacyCrc32(p, om::kLegacyContactRecordSize));
  CHECK(om::decodeLegacyContacts(buf, goodLen, recs, 4) == -1);

  makeLegacyFile(buf, sizeof(buf), 1);
  p = buf + om::kLegacyContactHeaderSize;
  memset(p + 32, 'n', 32);   // no terminator in the name
  put32(buf + 8, om::legacyCrc32(p, om::kLegacyContactRecordSize));
  CHECK(om::decodeLegacyContacts(buf, goodLen, recs, 4) == -1);

  // Wrong magic / version.
  makeLegacyFile(buf, sizeof(buf), 1);
  buf[0] ^= 0xFF;
  CHECK(om::decodeLegacyContacts(buf, goodLen, recs, 4) == -1);
  makeLegacyFile(buf, sizeof(buf), 1);
  put16(buf + 4, 99);
  CHECK(om::decodeLegacyContacts(buf, goodLen, recs, 4) == -1);
}

// EST-341: the /contacts3 image DataStore::loadContacts() reads (152-byte records, no header) and
// the "is this file complete?" rules the `mig` gate uses.
//
// This decoder mirrors DataStore::loadContacts() field by field, in its own read order, so the test
// does not trust our offset constants: a drift in either side fails the round trip.
static uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int decodeContacts3LikeDataStore(const uint8_t* buf, size_t len, om::LegacyContact* out,
                                        int max) {
  int n = 0;
  size_t off = 0;
  while (off + om::kLegacyContactRecordSize <= len && n < max) {
    const uint8_t* p = buf + off;
    om::LegacyContact r;
    memset(&r, 0, sizeof(r));
    memcpy(r.pub_key, p, 32);
    memcpy(r.name, p + 32, 32);
    r.type = p[64];
    r.flags = p[65];
    r.out_path_len = p[71];
    memcpy(r.out_path, p + 76, 64);
    r.last_advert_ts = get32(p + 72);
    r.lastmod = get32(p + 140);
    r.gps_lat = (int32_t)get32(p + 144);
    r.gps_lon = (int32_t)get32(p + 148);
    r.sync_since = get32(p + 67);
    out[n++] = r;
    off += om::kLegacyContactRecordSize;
  }
  return (off == len) ? n : -1;
}

static void testContacts3Image() {
  static uint8_t legacy[om::kLegacyContactMaxFileSize];
  const size_t legacyLen = makeLegacyFile(legacy, sizeof(legacy), 2);
  om::LegacyContact recs[4];
  CHECK(om::decodeLegacyContacts(legacy, legacyLen, recs, 4) == 2);

  uint8_t out[2 * om::kLegacyContactRecordSize];
  size_t outLen = 12345;
  CHECK(om::encodeContacts3(recs, 2, out, sizeof(out), &outLen));
  CHECK(outLen == sizeof(out));

  // The wire offsets DataStore::loadContacts() uses, written down independently of the header's
  // constants so a change there has to be deliberate here too.
  CHECK(om::kNewContactTypeOffset == 64);
  CHECK(om::kNewContactFlagsOffset == 65);
  CHECK(om::kNewContactSyncSinceOffset == 67);
  CHECK(om::kNewContactPathLenOffset == 71);
  CHECK(om::kNewContactLastAdvertOffset == 72);
  CHECK(om::kNewContactPathOffset == 76);
  CHECK(om::kNewContactLastModOffset == 140);
  CHECK(om::kNewContactLatOffset == 144);
  CHECK(om::kNewContactLonOffset == 148);
  CHECK(om::kNewContactLonOffset + 4 == om::kLegacyContactRecordSize);

  // Byte-level spot checks on the second record.
  const uint8_t* p = out + om::kLegacyContactRecordSize;
  CHECK(memcmp(p, recs[1].pub_key, 32) == 0);
  CHECK(strcmp((const char*)p + 32, "node1") == 0);
  CHECK(p[om::kNewContactTypeOffset] == 2);
  CHECK(p[om::kNewContactFlagsOffset] == 0x03);
  CHECK(p[66] == 0);   // the unused byte
  CHECK(p[om::kNewContactPathLenOffset] == 2);
  CHECK(p[om::kNewContactPathOffset] == 0xAA);
  CHECK(p[om::kNewContactLastAdvertOffset] == 101);
  CHECK(p[om::kNewContactLastAdvertOffset + 1] == 0);
  CHECK(p[om::kNewContactLastModOffset] == 201);
  CHECK(p[om::kNewContactLatOffset] == (uint8_t)(46000000 + 1));
  CHECK(p[om::kNewContactLonOffset] == (uint8_t)(14000000 + 1));
  CHECK(p[om::kNewContactSyncSinceOffset] == 7);
  CHECK(p[om::kNewContactSyncSinceOffset + 3] == 0);

  // Full round trip through the DataStore read order: every field of every record survives.
  om::LegacyContact back[4];
  CHECK(decodeContacts3LikeDataStore(out, outLen, back, 4) == 2);
  for (int i = 0; i < 2; ++i) {
    CHECK(memcmp(back[i].pub_key, recs[i].pub_key, 32) == 0);
    CHECK(strcmp(back[i].name, recs[i].name) == 0);
    CHECK(back[i].type == recs[i].type);
    CHECK(back[i].flags == recs[i].flags);
    CHECK(back[i].out_path_len == recs[i].out_path_len);
    CHECK(memcmp(back[i].out_path, recs[i].out_path, 64) == 0);
    CHECK(back[i].last_advert_ts == recs[i].last_advert_ts);
    CHECK(back[i].lastmod == recs[i].lastmod);
    CHECK(back[i].gps_lat == recs[i].gps_lat);
    CHECK(back[i].gps_lon == recs[i].gps_lon);
    CHECK(back[i].sync_since == recs[i].sync_since);
  }
  // A torn trailing record is rejected, not silently decoded.
  CHECK(decodeContacts3LikeDataStore(out, outLen - 1, back, 4) == -1);

  // Bounds and empty input.
  outLen = 999;
  CHECK(!om::encodeContacts3(recs, 2, out, sizeof(out) - 1, &outLen));
  CHECK(outLen == 0);
  CHECK(!om::encodeContacts3(nullptr, 1, out, sizeof(out), &outLen));
  CHECK(!om::encodeContacts3(recs, -1, out, sizeof(out), &outLen));
  CHECK(!om::encodeContacts3(recs, om::kLegacyMaxContactRecords + 1, out, sizeof(out), &outLen));
  CHECK(om::encodeContacts3(nullptr, 0, out, sizeof(out), &outLen));
  CHECK(outLen == 0);

  // The file-size rule the migration gate uses: exactly one record per decoded contact.
  CHECK(om::contactsFileComplete(0, 0));
  CHECK(!om::contactsFileComplete(0, 2));   // the EST-340 "0 bytes while 10 decoded" case
  CHECK(om::contactsFileComplete(2 * om::kLegacyContactRecordSize, 2));
  CHECK(!om::contactsFileComplete(2 * om::kLegacyContactRecordSize + 1, 2));
  CHECK(!om::contactsFileComplete(om::kLegacyContactRecordSize, 2));
  CHECK(!om::contactsFileComplete(0, -1));
  CHECK(!om::contactsFileComplete(0, om::kLegacyMaxContactRecords + 1));

  // /channels2: 68 bytes per slot, all kMaxChannels slots written.
  CHECK(om::kChannelRecordSize == 68);
  CHECK(om::channelsFileComplete(8 * 68, 8));
  CHECK(!om::channelsFileComplete(7 * 68, 8));
  CHECK(!om::channelsFileComplete(8 * 68 + 1, 8));
  CHECK(!om::channelsFileComplete(8 * 68, -1));
}

int main() {
  testIdentity();
  testChannels();
  testPathHashMode();
  testLegacyContacts();
  testContacts3Image();
  if (g_fail) {
    printf("%d mesh_migration check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("mesh_migration: all checks passed\n");
  return 0;
}
