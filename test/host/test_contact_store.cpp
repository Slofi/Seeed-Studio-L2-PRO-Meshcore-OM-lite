// Host test for src/contact_store.h: the on-flash contact format (round trip, truncation,
// corruption, impossible fields) and the write rate limit that protects the flash.
// Plain g++, no Arduino: contact_store.h must stay device-free.
#include <stdio.h>
#include <string.h>

#include "contact_store.h"

using namespace om;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static StoredContact makeRec(int i) {
  StoredContact r;
  memset(&r, 0, sizeof(r));
  for (int k = 0; k < 32; ++k) r.pub_key[k] = (uint8_t)(i * 7 + k);
  snprintf(r.name, sizeof(r.name), "NODE-%02d", i);   // includes a long name on purpose
  r.type = (uint8_t)(1 + i % 4);
  r.flags = (uint8_t)(i & 0x1F);
  r.out_path_len = (uint8_t)(i % 3 == 0 ? 0xFF : i % 4);
  for (int k = 0; k < 64; ++k) r.out_path[k] = (uint8_t)(i + k);
  r.last_advert_ts = 1700000000u + (uint32_t)i;
  r.lastmod = 1700000100u + (uint32_t)i;
  r.gps_lat = (i % 2) ? -46239100 : 46239100;   // both signs must survive
  r.gps_lon = 14356200 + i;
  r.sync_since = (uint32_t)i * 3u;
  return r;
}

static bool sameRec(const StoredContact& a, const StoredContact& b) {
  return memcmp(a.pub_key, b.pub_key, 32) == 0 && memcmp(a.name, b.name, 32) == 0 &&
         a.type == b.type && a.flags == b.flags && a.out_path_len == b.out_path_len &&
         memcmp(a.out_path, b.out_path, 64) == 0 && a.last_advert_ts == b.last_advert_ts &&
         a.lastmod == b.lastmod && a.gps_lat == b.gps_lat && a.gps_lon == b.gps_lon &&
         a.sync_since == b.sync_since;
}

static void testRoundTrip() {
  static StoredContact in[kMaxContactRecords];
  static StoredContact out[kMaxContactRecords];
  static uint8_t buf[kContactMaxFileSize];

  // 0 records: a valid (empty) file, header only.
  size_t len = contactEncode(in, 0, buf, sizeof(buf));
  CHECK(len == kContactHeaderSize);
  CHECK(contactDecode(buf, len, out, kMaxContactRecords) == 0);

  // 1 record.
  in[0] = makeRec(0);
  len = contactEncode(in, 1, buf, sizeof(buf));
  CHECK(len == kContactHeaderSize + kContactRecordSize);
  CHECK(contactDecode(buf, len, out, kMaxContactRecords) == 1);
  CHECK(sameRec(in[0], out[0]));

  // A full table: every field of every record must come back byte for byte.
  for (int i = 0; i < kMaxContactRecords; ++i) in[i] = makeRec(i);
  len = contactEncode(in, kMaxContactRecords, buf, sizeof(buf));
  CHECK(len == kContactMaxFileSize);
  const int n = contactDecode(buf, len, out, kMaxContactRecords);
  CHECK(n == kMaxContactRecords);
  for (int i = 0; i < n && i < kMaxContactRecords; ++i) CHECK(sameRec(in[i], out[i]));

  // Header magic is the documented "OMC1" in file order.
  CHECK(buf[0] == 'O' && buf[1] == 'M' && buf[2] == 'C' && buf[3] == '1');
  CHECK(contactGet16(buf + 4) == kContactVersion);
  CHECK(contactGet16(buf + 6) == kMaxContactRecords);

  // Encoding refuses what cannot be written instead of truncating.
  CHECK(contactEncode(in, kMaxContactRecords + 1, buf, sizeof(buf)) == 0);
  CHECK(contactEncode(in, -1, buf, sizeof(buf)) == 0);
  CHECK(contactEncode(in, 2, buf, kContactHeaderSize + 2 * kContactRecordSize - 1) == 0);
  CHECK(contactEncode(nullptr, 1, buf, sizeof(buf)) == 0);
}

static void testRejects() {
  static StoredContact in[kMaxContactRecords];
  static StoredContact out[kMaxContactRecords];
  static uint8_t good[kContactMaxFileSize];
  static uint8_t buf[kContactMaxFileSize];

  for (int i = 0; i < 3; ++i) in[i] = makeRec(i);
  const size_t len = contactEncode(in, 3, good, sizeof(good));
  CHECK(len > 0);

  // Truncated anywhere: a short header, a missing record tail, one byte short of the CRC.
  for (size_t cut = 0; cut < len; cut += 17) {
    memcpy(buf, good, cut);
    CHECK(contactDecode(buf, cut, out, kMaxContactRecords) == -1);
  }

  // Trailing junk after the last record is not a file we wrote.
  memcpy(buf, good, len);
  buf[len] = 0;
  CHECK(contactDecode(buf, len + 1, out, kMaxContactRecords) == -1);

  // Wrong magic.
  memcpy(buf, good, len);
  buf[0] = 'X';
  CHECK(contactDecode(buf, len, out, kMaxContactRecords) == -1);

  // Unknown future format version.
  memcpy(buf, good, len);
  contactPut16(buf + 4, (uint32_t)kContactVersion + 1);
  CHECK(contactDecode(buf, len, out, kMaxContactRecords) == -1);

  // Count larger than the table (and larger than the buffer really holds).
  memcpy(buf, good, len);
  contactPut16(buf + 6, kMaxContactRecords + 1);
  CHECK(contactDecode(buf, len, out, kMaxContactRecords) == -1);

  // A decoder with fewer output slots than records refuses rather than overflowing.
  CHECK(contactDecode(good, len, out, 2) == -1);

  // One flipped bit in a record breaks the CRC (partial write / worn flash).
  memcpy(buf, good, len);
  buf[kContactHeaderSize + 40] ^= 0x10;
  CHECK(contactDecode(buf, len, out, kMaxContactRecords) == -1);

  // Fields a real save can never produce are rejected even with a correct CRC: re-seal after each
  // edit so the CRC check cannot mask the field check.
  struct Case { int off; uint8_t value; const char* what; };
  const Case cases[] = {
      {64, 0, "type ADV_TYPE_NONE"},          // type
      {67, 1, "reserved byte"},               // reserved
      {66, 65, "out_path_len > 64"},          // out_path_len
  };
  for (const Case& c : cases) {
    memcpy(buf, good, len);
    buf[kContactHeaderSize + c.off] = c.value;
    contactPut32(buf + 8, contactCrc32(buf + kContactHeaderSize,
                                       (size_t)3 * kContactRecordSize));
    if (contactDecode(buf, len, out, kMaxContactRecords) != -1) {
      printf("FAIL %s:%d  accepted %s\n", __FILE__, __LINE__, c.what);
      ++g_fail;
    }
  }

  // Empty name, and a name that fills all 32 bytes without a terminator.
  memcpy(buf, good, len);
  buf[kContactHeaderSize + 32] = 0;
  contactPut32(buf + 8, contactCrc32(buf + kContactHeaderSize, (size_t)3 * kContactRecordSize));
  CHECK(contactDecode(buf, len, out, kMaxContactRecords) == -1);

  memcpy(buf, good, len);
  memset(buf + kContactHeaderSize + 32, 'A', 32);
  contactPut32(buf + 8, contactCrc32(buf + kContactHeaderSize, (size_t)3 * kContactRecordSize));
  CHECK(contactDecode(buf, len, out, kMaxContactRecords) == -1);

  // A valid file still decodes after all of that (the rejects did not poison the data).
  CHECK(contactDecode(good, len, out, kMaxContactRecords) == 3);
}

static void testScheduler() {
  ContactSaveScheduler s;   // default interval
  CHECK(kContactSaveMinIntervalMs == 60000);

  // Nothing changed: never due, however long we wait.
  CHECK(!s.due(0));
  CHECK(!s.due(1000000));
  CHECK(!s.dirty());

  // First change after boot is written at once: a quick power cycle must not lose it.
  s.markDirty(500);
  CHECK(s.dirty());
  CHECK(s.due(500));
  s.onSaveOk(500);
  CHECK(!s.dirty());
  CHECK(!s.due(500));

  // Another change one second later waits for the interval, not for the next packet.
  s.markDirty(1500);
  CHECK(!s.due(1500));
  CHECK(!s.due(500 + kContactSaveMinIntervalMs - 1));
  CHECK(s.due(500 + kContactSaveMinIntervalMs));

  // A failed save stays dirty but does not retry in a tight loop.
  s.onSaveFailed(60500);
  CHECK(s.dirty());
  CHECK(!s.due(60501));
  CHECK(!s.due(60500 + kContactSaveMinIntervalMs - 1));
  CHECK(s.due(60500 + kContactSaveMinIntervalMs));
  s.onSaveOk(60500 + kContactSaveMinIntervalMs);
  CHECK(!s.dirty());

  // markDirty while already dirty does not restart the timer (repeated adverts cannot postpone a
  // save forever).
  s.markDirty(200000);
  CHECK(s.due(200000));
  const uint32_t base = 200000;
  for (uint32_t t = base; t < base + kContactSaveMinIntervalMs; t += 1000) s.markDirty(t);
  CHECK(s.due(base + kContactSaveMinIntervalMs));

  // millis() wraps after ~49 days: the interval comparison must use unsigned difference.
  ContactSaveScheduler w;
  w.markDirty(0xFFFFFF00u);
  CHECK(w.due(0xFFFFFF00u));
  w.onSaveOk(0xFFFFFF00u);
  w.markDirty(0xFFFFFF00u);
  CHECK(!w.due(0x00000100u));                    // 0x200 ms elapsed, far below the interval
  CHECK(w.due(0xFFFFFF00u + kContactSaveMinIntervalMs));

  // A zero interval is "save on every change", used only by tests.
  ContactSaveScheduler always(0);
  always.markDirty(10);
  CHECK(always.due(10));
  always.onSaveOk(10);
  always.markDirty(10);
  CHECK(always.due(10));
}

int main() {
  testRoundTrip();
  testRejects();
  testScheduler();

  if (g_fail) {
    printf("%d contact_store check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("contact_store: all checks passed\n");
  return 0;
}
