// SPDX-License-Identifier: MIT
// EST-348: the on-card history, proven without a card.
//
// The SAME HistoryStore the firmware runs is driven here over an in-memory volume: record
// encode/decode and CRC, a torn last record (and its repair), update records applied on load,
// paging backwards, rotation to .old, unread/seen across a reload, the SPIFFS fallback and the
// chat ids. The device glue (SD_MMC / SPIFFS) is the only untested part, by design.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "history/history_log.h"

using namespace om::history;

// --- an in-memory volume ---------------------------------------------------------------------------
class FakeFile : public HistoryFile {
 public:
  FakeFile(std::string* data, bool append)
      : data_(data), append_(append), pos_(append ? data->size() : 0) {}

  int read(uint8_t* buf, int n) override {
    if (buf == nullptr || n <= 0 || pos_ >= data_->size()) return 0;
    size_t want = (size_t)n;
    if (want > data_->size() - pos_) want = data_->size() - pos_;
    memcpy(buf, data_->data() + pos_, want);
    pos_ += want;
    return (int)want;
  }

  int write(const uint8_t* buf, int n) override {
    if (buf == nullptr || n <= 0) return 0;
    if (append_) pos_ = data_->size();
    if (pos_ > data_->size()) data_->resize(pos_, 0);
    if (pos_ + (size_t)n > data_->size()) data_->resize(pos_ + (size_t)n);
    memcpy(&(*data_)[pos_], buf, (size_t)n);
    pos_ += (size_t)n;
    return n;
  }

  bool seek(uint32_t pos) override {
    if (pos > data_->size()) return false;
    pos_ = pos;
    return true;
  }

  uint32_t size() override { return (uint32_t)data_->size(); }

  void flush() override {}

  void close() override { closed_ = true; }

  bool closed() const { return closed_; }

 private:
  std::string* data_;
  bool append_;
  size_t pos_;
  bool closed_ = false;
};

class FakeVolume : public HistoryVolume {
 public:
  explicit FakeVolume(const char* name, const char* prefix, bool mountable = true)
      : name_(name), prefix_(prefix), mountable_(mountable) {}

  const char* name() const override { return name_; }
  const char* prefix() const override { return prefix_; }

  bool mount() override {
    ++mounts;
    return mountable_;
  }

  // EST-349: every volume entry point counts as one "SD op". The enqueue-path test sets poison so a
  // call from appendMsg/appendUpdate/markSeen is both counted and visible.
  bool exists(const char* path) override {
    ++ops;
    if (poison) return false;
    return files_.count(path) != 0;
  }

  uint32_t size(const char* path) override {
    ++ops;
    if (poison) return 0;
    auto it = files_.find(path);
    return it == files_.end() ? 0 : (uint32_t)it->second.size();
  }

  bool remove(const char* path) override {
    ++ops;
    if (poison) return false;
    return files_.erase(path) != 0;
  }

  bool rename(const char* from, const char* to) override {
    ++ops;
    if (poison) return false;
    auto it = files_.find(from);
    if (it == files_.end()) return false;
    if (files_.count(to)) files_.erase(to);
    files_[to] = it->second;
    files_.erase(it);
    return true;
  }

  HistoryFile* openRead(const char* path) override {
    ++ops;
    if (poison) return nullptr;
    auto it = files_.find(path);
    if (it == files_.end()) return nullptr;
    handles_.push_back(std::make_unique<FakeFile>(&it->second, false));
    return handles_.back().get();
  }

  HistoryFile* openAppend(const char* path) override {
    ++ops;
    if (poison) return nullptr;
    auto it = files_.find(path);
    if (it == files_.end()) it = files_.emplace(path, std::string()).first;
    handles_.push_back(std::make_unique<FakeFile>(&it->second, true));
    return handles_.back().get();
  }

  void list(ListFn fn, void* ctx) override {
    ++ops;
    if (poison) return;
    for (const auto& kv : files_) {
      const std::string& path = kv.first;
      const size_t slash = path.rfind('/');
      const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
      fn(ctx, base.c_str(), (uint32_t)kv.second.size());
    }
  }

  std::string& file(const std::string& path) { return files_[path]; }
  const std::map<std::string, std::string>& files() const { return files_; }
  int mounts = 0;
  int ops = 0;          // EST-349: every volume call; 0 proves the enqueue path never touches SD
  bool poison = false;  // any call from the enqueue path fails loudly

 private:
  const char* name_;
  const char* prefix_;
  bool mountable_;
  std::map<std::string, std::string> files_;
  std::vector<std::unique_ptr<FakeFile>> handles_;
};

// A clock that advances 7 us per read, so the flush timing code is exercised.
static uint32_t g_now = 0;
static uint32_t fakeNow() {
  g_now += 7;
  return g_now;
}

static std::vector<std::string> g_log;
static void fakeLog(const char* line) {
  if (line != nullptr) g_log.push_back(line);
}

static HistoryConfig smallCaps(uint32_t perChat, uint32_t total) {
  HistoryConfig c;
  c.perChatCap = perChat;
  c.totalCap = total;
  return c;
}

static Msg makeMsg(uint32_t seq, const char* name, const char* text, bool mine, uint32_t wall) {
  Msg m;
  m.seq = seq;
  m.uptime = 1000 + seq;
  m.wall = wall;
  m.setWallReal(wall != 0);
  m.setMine(mine);
  m.delivery = mine ? 0 : 1;
  m.hops = (int8_t)(seq % 4);
  m.snr4 = (int8_t)(-8 + (int)seq);
  msgSetText(m, name, text);
  return m;
}

// --- tests ----------------------------------------------------------------------------------------
static void testRecordRoundTrip() {
  Msg in = makeMsg(7, "ERA-3 (SI)", "hello \xC4\x8D \xF0\x9F\x98\x80", false, 1762700000u);
  in.ackWall = 1762700010u;
  in.setAckReal(true);
  in.delivery = 1;
  in.repeats = 3;
  const uint8_t path[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  msgSetPath(in, path, 2, 3);

  uint8_t payload[kMaxPayload];
  const int n = encodeMsgPayload(in, payload, (int)sizeof(payload));
  assert(n > 31);

  Msg out;
  assert(decodeMsgPayload(payload, n, &out));
  assert(out.logId == in.logId);
  assert(out.seq == in.seq);
  assert(out.uptime == in.uptime);
  assert(out.wall == in.wall);
  assert(out.ackWall == in.ackWall);
  assert(out.wallReal() && out.ackReal());
  assert(!out.mine() && !out.dm());
  assert(out.hops == in.hops);
  assert(out.snr4 == in.snr4);
  assert(out.repeats == 3);
  assert(out.delivery == 1);
  assert(out.pathHashSize == 2 && out.pathCount == 3 && out.pathLen == 6);
  assert(memcmp(out.path, path, sizeof(path)) == 0);
  assert(strcmp(out.name, in.name) == 0);
  assert(strcmp(out.text, in.text) == 0);

  // A truncated payload is refused whole; a length byte that disagrees with the real payload too.
  assert(!decodeMsgPayload(payload, n - 1, &out));
  payload[30] = 200;
  assert(!decodeMsgPayload(payload, n, &out));

  // The framed record carries a CRC over the header bytes and the payload.
  uint8_t rec[kMaxRecord];
  const int rn = encodeRecord(Type::Msg, payload, n, rec, (int)sizeof(rec));
  assert(rn == kHeaderBytes + n);
  RecordRef ref;
  assert(decodeRecordAt(rec, rn, 0, &ref) == rn);
  rec[kHeaderBytes + 3] ^= 0x01;
  assert(decodeRecordAt(rec, rn, 0, &ref) < 0);
}

static void testChatId() {
  char id[24];
  channelId(0x3f, "Public", id, sizeof(id));
  assert(strcmp(id, "ch-3f-Public") == 0);
  channelId(0xa1, "Don't Panic", id, sizeof(id));
  assert(strcmp(id, "ch-a1-Don_t_Panic") == 0);
  channelId(0x02, "SI-Ivančna Gorica #test extra long name", id, sizeof(id));
  assert(strncmp(id, "ch-02-SI-Ivan", 13) == 0);
  assert(strlen(id) <= 20);

  const uint8_t key[32] = {0xde, 0xad, 0xbe, 0xef};
  dmId(key, id, sizeof(id));
  assert(strcmp(id, "dm-deadbeef") == 0);
}

static void testTornLastRecord() {
  FakeVolume vol("sd", "/h/");
  HistoryStore store;
  store.begin(&vol, nullptr, smallCaps(2 * 1024 * 1024, 64 * 1024 * 1024), HistoryConfig{},
              fakeNow, fakeLog);
  assert(store.ready());
  const int slot = store.ensureChannel("Public", 0x3f);
  assert(slot >= 0);

  for (int i = 1; i <= 3; ++i) {
    Msg m = makeMsg((uint32_t)i, "ERA-3", "message", false, 1762700000u + (uint32_t)i);
    assert(store.appendMsg(slot, m, (uint16_t)i) == (uint32_t)i);
  }
  store.flushStep(100);
  assert(store.queued() == 0);

  const std::string path = "/h/ch-3f-Public.log";
  const std::string clean = vol.file(path);
  assert(clean.size() > kHeaderBytes);

  // Append half a record: the length prefix promises more than the card got.
  Msg extra = makeMsg(4, "ERA-3", "torn", false, 1762700100u);
  uint8_t payload[kMaxPayload];
  const int pn = encodeMsgPayload(extra, payload, (int)sizeof(payload));
  uint8_t rec[kMaxRecord];
  const int rn = encodeRecord(Type::Msg, payload, pn, rec, (int)sizeof(rec));
  vol.file(path).append(std::string((const char*)rec, (size_t)(rn / 2)));

  HistoryStore re;
  re.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
  const int slot2 = re.findSlot("ch-3f-Public");
  assert(slot2 >= 0);
  assert(re.loadNewest(slot2, kScanRing) == 3);
  assert(re.scanMsg(0).logId == 1);
  assert(re.scanMsg(2).logId == 3);
  assert(strcmp(re.scanMsg(2).text, "message") == 0);

  // The next append repairs the tail first: the torn bytes are gone, the three records survive.
  Msg next = makeMsg(4, "ERA-3", "after repair", false, 1762700200u);
  assert(re.appendMsg(slot2, next, 4) == 4);
  re.flushStep(100);
  assert(vol.file(path).size() > clean.size());
  const std::string repaired = vol.file(path);
  int cnt = 0;
  const int valid = validateChain((const uint8_t*)repaired.data(), (int)repaired.size(), false, &cnt);
  assert(valid == (int)repaired.size());
  assert(cnt == 1 + 3 * 2 + 2);
  assert(re.loadNewest(slot2, kScanRing) == 4);
  assert(strcmp(re.scanMsg(3).text, "after repair") == 0);
}

static void testUpdatesAppliedOnLoad() {
  FakeVolume vol("sd", "/h/");
  HistoryStore store;
  store.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
  uint8_t pub[om::kMeshPubKey] = {0xde, 0xad, 0xbe, 0xef};
  const int slot = store.ensureDm(pub, "ERA-3 (SI)");
  assert(slot >= 0);

  Msg mine = makeMsg(1, "OM Lite", "ping", true, 1762700000u);
  mine.setDm(true);
  mine.delivery = 0;
  const uint32_t id = store.appendMsg(slot, mine, 0);
  assert(id == 1);

  Update u;
  u.logId = id;
  u.kind = kUpdateState;
  u.delivery = 1;   // Delivered
  u.ackWall = 1762700005u;
  u.ackReal = true;
  assert(store.appendUpdate(slot, u));

  Update r;
  r.logId = id;
  r.kind = kUpdateRepeats;
  r.repeats = 2;
  assert(store.appendUpdate(slot, r));

  Msg got = makeMsg(2, "ERA-3 (SI)", "pong", false, 1762700010u);
  got.setDm(true);
  assert(store.appendMsg(slot, got, 1) == 2);
  store.flushStep(100);

  HistoryStore re;
  re.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
  const int s2 = re.findSlot("dm-deadbeef");
  assert(s2 >= 0);
  assert(strcmp(re.slotName(s2), "ERA-3 (SI)") == 0);
  const uint8_t* key = nullptr;
  assert(re.slotKey(s2, &key));
  assert(key[0] == 0xde && key[3] == 0xef);
  assert(re.loadNewest(s2, kScanRing) == 2);
  assert(re.scanMsg(0).delivery == 1);
  assert(re.scanMsg(0).ackWall == 1762700005u);
  assert(re.scanMsg(0).ackReal());
  assert(re.scanMsg(0).repeats == 2);
  assert(re.scanUnread() == 1);
}

static void testPaging() {
  FakeVolume vol("sd", "/h/");
  HistoryStore store;
  store.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
  const int slot = store.ensureChannel("Public", 0x3f);
  for (int i = 1; i <= 100; ++i) {
    char text[32];
    snprintf(text, sizeof(text), "msg %d", i);
    Msg m = makeMsg((uint32_t)i, "ERA-3", text, false, 1762700000u + (uint32_t)i);
    assert(store.appendMsg(slot, m, 0) == (uint32_t)i);
    // EST-349: the caller only enqueues; the loop task drains. The test plays the loop between
    // messages, exactly as the firmware does.
    store.flushStep(1000);
  }

  assert(store.loadNewest(slot, 32) == 32);
  assert(store.scanMsg(0).logId == 69);
  assert(store.scanMsg(31).logId == 100);

  // Three pages of 16 walk back to 21, then the last page has 20 left.
  uint32_t before = store.scanMsg(0).logId;
  int got = store.loadOlder(slot, before, 16);
  assert(got == 16 && store.scanMsg(0).logId == 53 && store.scanMsg(15).logId == 68);
  before = store.scanMsg(0).logId;
  got = store.loadOlder(slot, before, 16);
  assert(got == 16 && store.scanMsg(0).logId == 37 && store.scanMsg(15).logId == 52);
  before = store.scanMsg(0).logId;
  got = store.loadOlder(slot, before, 16);
  assert(got == 16 && store.scanMsg(0).logId == 21 && store.scanMsg(15).logId == 36);
  before = store.scanMsg(0).logId;
  got = store.loadOlder(slot, before, 16);
  assert(got == 16 && store.scanMsg(0).logId == 5 && store.scanMsg(15).logId == 20);
  before = store.scanMsg(0).logId;
  got = store.loadOlder(slot, before, 16);
  assert(got == 4 && store.scanMsg(0).logId == 1 && store.scanMsg(3).logId == 4);
  before = store.scanMsg(0).logId;
  assert(store.loadOlder(slot, before, 16) == 0);

  // Paging past the newest message returns nothing.
  assert(store.loadOlder(slot, 1, 16) == 0);
}

static void testRotation() {
  FakeVolume vol("sd", "/h/");
  HistoryStore store;
  store.begin(&vol, nullptr, smallCaps(2048, 64 * 1024 * 1024), HistoryConfig{}, fakeNow, fakeLog);
  const int slot = store.ensureChannel("Public", 0x3f);
  for (int i = 1; i <= 40; ++i) {
    char text[64];
    snprintf(text, sizeof(text), "rotation message number %d", i);
    Msg m = makeMsg((uint32_t)i, "ERA-3", text, false, 1762700000u + (uint32_t)i);
    assert(store.appendMsg(slot, m, 0) == (uint32_t)i);
    store.flushStep(1000);   // EST-349: the loop drains the queue, not the caller
  }
  assert(vol.exists("/h/ch-3f-Public.old"));
  assert(vol.size("/h/ch-3f-Public.log") <= 2048);
  assert(store.status().bytes <= 40 * 100 + 2048);   // .log + .old, never more than two files

  // The newest page comes from .log; paging far enough crosses into .old with no gap.
  int seen[41] = {0};
  int got = store.loadNewest(slot, 32);
  assert(got == 32);
  assert(store.scanMsg(31).logId == 40);
  for (int i = 0; i < got; ++i) seen[store.scanMsg(i).logId] = 1;
  const uint32_t before = store.scanMsg(0).logId;
  const int n = store.loadOlder(slot, before, 16);
  assert(n == 8);
  assert(store.scanMsg(n - 1).logId + 1 == before);
  for (int i = 0; i < n; ++i) seen[store.scanMsg(i).logId] = 1;
  // Every id 1..40 is still reachable across the two files.
  for (int i = 1; i <= 40; ++i) assert(seen[i] == 1);

  // Unread/seen after a rotation: the Meta in the current .log is the one that counts, and the seen
  // point clears it across another reload.
  Msg last = makeMsg(41, "ERA-3", "after rotation", false, 1762700200u);
  assert(store.appendMsg(slot, last, 1) == 41);
  store.flushStep(10);
  {
    HistoryStore re;
    re.begin(&vol, nullptr, smallCaps(2048, 64 * 1024 * 1024), HistoryConfig{}, fakeNow, fakeLog);
    const int s2 = re.findSlot("ch-3f-Public");
    assert(re.loadNewest(s2, kScanRing) == 32);
    assert(re.scanMsg(31).logId == 41);
    assert(re.scanUnread() == 1);
    assert(re.markSeen(s2));
    re.flushStep(10);
  }
  {
    HistoryStore re;
    re.begin(&vol, nullptr, smallCaps(2048, 64 * 1024 * 1024), HistoryConfig{}, fakeNow, fakeLog);
    const int s2 = re.findSlot("ch-3f-Public");
    assert(re.loadNewest(s2, kScanRing) == 32);
    assert(re.scanUnread() == 0);
    assert(re.scanSeen() == 41);
  }
}

static void testSeenAndUnreadAcrossReload() {
  FakeVolume vol("sd", "/h/");
  {
    HistoryStore store;
    store.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
    const int slot = store.ensureChannel("Public", 0x3f);
    for (int i = 1; i <= 3; ++i) {
      Msg m = makeMsg((uint32_t)i, "ERA-3", "new", false, 1762700000u + (uint32_t)i);
      store.appendMsg(slot, m, (uint16_t)i);
    }
    store.flushStep(100);
  }
  {
    HistoryStore re;
    re.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
    const int slot = re.findSlot("ch-3f-Public");
    assert(re.loadNewest(slot, 32) == 3);
    assert(re.scanUnread() == 3);
    assert(re.markSeen(slot));
    re.flushStep(100);
  }
  {
    HistoryStore re;
    re.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
    const int slot = re.findSlot("ch-3f-Public");
    assert(re.loadNewest(slot, 32) == 3);
    assert(re.scanUnread() == 0);
    assert(re.scanSeen() == 3);
    // A new received message after the seen point counts as one unread.
    Msg m = makeMsg(4, "ERA-3", "after", false, 1762700100u);
    re.appendMsg(slot, m, 1);
    re.flushStep(100);
  }
  {
    HistoryStore re;
    re.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
    const int slot = re.findSlot("ch-3f-Public");
    assert(re.loadNewest(slot, 32) == 4);
    assert(re.scanUnread() == 1);
  }
}

static void testFallbackAndStatus() {
  FakeVolume sd("sd", "/omlite/history/", false);   // no card
  FakeVolume spiffs("spiffs", "/h/", true);
  HistoryStore store;
  store.begin(&sd, &spiffs, smallCaps(2 * 1024 * 1024, 64 * 1024 * 1024),
              smallCaps(32 * 1024, 256 * 1024), fakeNow, fakeLog);
  assert(store.ready());
  assert(!store.onSd());
  assert(store.status().perChatCap == 32 * 1024);
  const int slot = store.ensureChannel("Public", 0x3f);
  Msg m = makeMsg(1, "ERA-3", "on spiffs", false, 1762700000u);
  assert(store.appendMsg(slot, m, 1) == 1);
  store.flushStep(10);
  assert(spiffs.exists("/h/ch-3f-Public.log"));
  assert(store.status().records >= 1);
  assert(store.status().worstFlushUs > 0);

  // The fallback is bounded: with a tiny total cap a message is refused and flagged, not written.
  FakeVolume sd2("sd", "/omlite/history/", true);
  HistoryStore tight;
  tight.begin(&sd2, nullptr, smallCaps(512, 600), HistoryConfig{}, fakeNow, fakeLog);
  const int s2 = tight.ensureChannel("Public", 0x3f);
  int accepted = 0;
  for (int i = 0; i < 10; ++i) {
    Msg x = makeMsg((uint32_t)i, "ERA-3", "tight", false, 1762700000u);
    if (tight.appendMsg(s2, x, 0) != 0) ++accepted;
  }
  tight.flushStep(1000);
  assert(tight.status().full || tight.status().dropped > 0);
  assert(tight.status().bytes <= 600);
}

static void testTinyRecordStress() {
  // Many small Meta records per message exercise the 256-offset window and the backwards walk.
  FakeVolume vol("sd", "/h/");
  HistoryStore store;
  store.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
  const int slot = store.ensureChannel("Public", 0x3f);
  for (int i = 1; i <= 60; ++i) {
    Msg m = makeMsg((uint32_t)i, "ERA-3", "x", false, 1762700000u + (uint32_t)i);
    store.appendMsg(slot, m, (uint16_t)i);
    // Four extra Meta records between messages (seen-point churn) = 5 records per message. The loop
    // drains between messages (EST-349: append/markSeen never write).
    for (int k = 0; k < 4; ++k) store.markSeen(slot);
    store.flushStep(1000);
  }
  store.flushStep(1000);
  assert(store.loadNewest(slot, 32) == 32);
  assert(store.scanMsg(31).logId == 60);
  assert(store.scanMsg(0).logId == 29);
}

static void testEnqueueNeverTouchesTheVolume() {
  // EST-349: the send path (usbCommandExecute -> meshLinkSend -> appendMsg) and every MeshCore
  // callback may only copy into the RAM queue. This test poisons the volume after boot: any SD call
  // made by the enqueue path fails and is counted.
  FakeVolume vol("sd", "/h/");
  HistoryStore store;
  store.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
  const int slot = store.ensureChannel("Public", 0x3f);
  Msg first = makeMsg(1, "OM Lite", "first", true, 1762700000u);
  assert(store.appendMsg(slot, first, 0) == 1);
  store.flushStep(1000);
  assert(store.queued() == 0);

  vol.poison = true;
  vol.ops = 0;
  // A burst exactly like the first send after boot: new slot (RAM only), messages, an update and a
  // seen mark. Nothing here may call the volume.
  const int fresh = store.ensureChannel("Brand new", 0x11);
  assert(fresh >= 0);
  Msg n = makeMsg(1, "OM Lite", "brand new chat", false, 1762700001u);
  assert(store.appendMsg(fresh, n, 0) == 1);
  for (int i = 2; i <= 6; ++i) {
    Msg m = makeMsg((uint32_t)i, "OM Lite", "burst", true, 1762700000u + (uint32_t)i);
    assert(store.appendMsg(slot, m, 0) == (uint32_t)i);
  }
  Update u;
  u.logId = 1;
  u.kind = kUpdateState;
  u.delivery = 1;
  assert(store.appendUpdate(slot, u));
  assert(store.markSeen(slot));
  // Past the queue size: the record is dropped, never written from the caller.
  int accepted = 0;
  for (int i = 0; i < kQueueRecords * 2; ++i) {
    Msg m = makeMsg((uint32_t)(10 + i), "OM Lite", "overflow", true, 1762700000u);
    if (store.appendMsg(slot, m, 0) != 0) ++accepted;
  }
  assert(accepted > 0);
  assert(store.status().dropped > 0);
  assert(vol.ops == 0);   // THE assertion: the enqueue path made zero SD calls

  // The loop owns the card: with the volume healthy again, one flush pass writes the queue.
  vol.poison = false;
  vol.ops = 0;
  store.flushStep(1000);
  assert(vol.ops > 0);
  assert(store.queued() == 0);
  assert(store.status().dropped > 0);
}

static void testBootDoesNotProbeMissingFiles() {
  // EST-349 boot contract: one directory listing, then nothing. A chat that does not have a file yet
  // is never opened just to find out (that open is what printed the vfs error and crashed in EST-348).
  FakeVolume vol("sd", "/h/");
  HistoryStore store;
  vol.ops = 0;
  store.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
  assert(store.ready());
  assert(vol.ops == 1);   // list() only: no size/open/exists for the 32 empty slots
  const int slot = store.ensureChannel("Not On Card", 0x22);
  assert(slot >= 0);
  Msg m = makeMsg(1, "OM Lite", "creates it on flush", true, 1762700000u);
  assert(store.appendMsg(slot, m, 0) == 1);
  assert(vol.ops == 1);   // still RAM only
  store.flushStep(1000);
  assert(vol.ops > 1);    // the loop wrote the header with mode "a"
}

int main() {
  testRecordRoundTrip();
  testChatId();
  testTornLastRecord();
  testUpdatesAppliedOnLoad();
  testPaging();
  testRotation();
  testSeenAndUnreadAcrossReload();
  testFallbackAndStatus();
  testTinyRecordStress();
  testEnqueueNeverTouchesTheVolume();
  testBootDoesNotProbeMissingFiles();
  printf("test_history: all checks passed (%zu log lines)\n", g_log.size());
  return 0;
}
