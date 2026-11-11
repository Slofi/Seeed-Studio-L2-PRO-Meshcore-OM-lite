// SPDX-License-Identifier: MIT
// EST-380 host test: the SD open-file budget, and a failed history append that is retried instead of
// dropped.
//
//   1. BUDGET. The named constants in src/sd_budget.h sum to the mount limit, the limit keeps the
//      two slots of margin, and it is far above the Arduino default of 5 that broke the card.
//   2. RETRY. A refused openAppend leaves the record at the head of the queue, counts
//      hist_write_fail once (not once per retry) and prints [hist] write FAILED at most once a minute.
//   3. RECOVERY. The first successful write drains the backlog and logs exactly one
//      [hist] write recovered, N record(s) flushed; the card file then holds every record.
//   4. SHORT WRITE. A write that returns a short count keeps the record too, marks the tail torn for
//      the repair, and the retry writes it.
//   5. QUEUE FULL. Overflow is the only loss: hist_dropped grows, and the pop-up notice says so.
//   6. The pop-up notice is taken once (no repeat per message / per pass).
// Run by test/host/run.sh (system g++), no Arduino and no device.
#include <stdio.h>
#include <string.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "history/history_log.h"
#include "sd_budget.h"

using namespace om::history;

static int g_fail = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++g_fail;                                                           \
    }                                                                     \
  } while (0)

// --- an in-memory volume that can refuse opens and shorten writes ---------------------------------
class FakeFile : public HistoryFile {
 public:
  FakeFile(std::string* data, bool append, int* shortWrites)
      : data_(data), append_(append) {
    (void)shortWrites;
    pos_ = append ? data->size() : 0;
  }

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
    if (short_) {
      const int cut = n > 5 ? 5 : n;   // a partial record, as a dying card leaves it
      if (append_) pos_ = data_->size();
      data_->append((const char*)buf, (size_t)cut);
      pos_ = data_->size();
      return cut;
    }
    if (append_) pos_ = data_->size();
    if (pos_ > data_->size()) data_->resize(pos_, 0);
    if (pos_ + (size_t)n > data_->size()) data_->resize(pos_ + (size_t)n);
    memcpy(data_->data() + pos_, buf, (size_t)n);
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
  void close() override {}

  bool short_ = false;

 private:
  std::string* data_;
  bool append_;
  size_t pos_ = 0;
};

class FakeVolume : public HistoryVolume {
 public:
  explicit FakeVolume(const char* name = "sd", const char* prefix = "/h/")
      : name_(name), prefix_(prefix) {}

  const char* name() const override { return name_; }
  const char* prefix() const override { return prefix_; }
  bool mount() override { return true; }

  bool exists(const char* path) override { return files_.count(path) != 0; }

  uint32_t size(const char* path) override {
    auto it = files_.find(path);
    return it == files_.end() ? 0 : (uint32_t)it->second.size();
  }

  bool remove(const char* path) override { return files_.erase(path) != 0; }

  bool rename(const char* from, const char* to) override {
    auto it = files_.find(from);
    if (it == files_.end()) return false;
    files_[to] = it->second;
    files_.erase(it);
    return true;
  }

  HistoryFile* openRead(const char* path) override {
    auto it = files_.find(path);
    if (it == files_.end()) return nullptr;
    handles_.push_back(std::make_unique<FakeFile>(&it->second, false, &shortWrites));
    return handles_.back().get();
  }

  HistoryFile* openAppend(const char* path) override {
    if (failAppend) return nullptr;
    auto it = files_.find(path);
    if (it == files_.end()) it = files_.emplace(path, std::string()).first;
    handles_.push_back(std::make_unique<FakeFile>(&it->second, true, &shortWrites));
    if (shortWrites > 0) {
      --shortWrites;
      handles_.back()->short_ = true;
    }
    return handles_.back().get();
  }

  void list(ListFn fn, void* ctx) override {
    for (const auto& kv : files_) {
      const size_t slash = kv.first.rfind('/');
      const std::string base = slash == std::string::npos ? kv.first : kv.first.substr(slash + 1);
      fn(ctx, base.c_str(), (uint32_t)kv.second.size());
    }
  }

  std::string& file(const std::string& path) { return files_[path]; }

  bool failAppend = false;   // every openAppend returns nullptr
  int shortWrites = 0;       // the next N openAppend handles fail every write with 5 bytes

 private:
  const char* name_;
  const char* prefix_;
  std::map<std::string, std::string> files_;
  std::vector<std::unique_ptr<FakeFile>> handles_;
};

// --- clock and log --------------------------------------------------------------------------------
static uint32_t g_nowUs = 0;
static uint32_t fakeNow() { return g_nowUs; }

static std::vector<std::string> g_log;
static void fakeLog(const char* line) {
  if (line != nullptr) g_log.push_back(line);
}

static int countLog(const char* needle) {
  int n = 0;
  for (const std::string& line : g_log) {
    if (line.find(needle) != std::string::npos) ++n;
  }
  return n;
}

static Msg makeMsg(uint32_t seq, const char* text) {
  Msg m;
  m.seq = seq;
  m.uptime = 1000 + seq;
  m.wall = 1762700000u + seq;
  m.setWallReal(true);
  // OUR OWN message: appendMsg adds a Meta record for a received one, and this test wants exactly one
  // queued record per message so the queue arithmetic is the ticket's.
  m.setMine(true);
  m.delivery = 0;
  msgSetText(m, "Alice", text);
  return m;
}

static HistoryConfig bigCaps() {
  HistoryConfig c;
  c.perChatCap = 2UL * 1024UL * 1024UL;
  c.totalCap = 64UL * 1024UL * 1024UL;
  return c;
}

// --- 1. budget ------------------------------------------------------------------------------------
static void testBudget() {
  namespace b = om::sd_budget;
  const int sum = b::kMapMainPack + b::kMapOverlayFiles + b::kHistoryPoolFiles + b::kPowerLogFiles +
                  b::kTransientFiles + b::kMargin;
  CHECK(sum == b::kMaxOpenFiles);
  CHECK(b::kMaxOpenFiles == b::kRequiredOpenFiles + b::kMargin);
  CHECK(b::kRequiredOpenFiles == b::kMapMainPack + b::kMapOverlayFiles + b::kHistoryPoolFiles +
                                     b::kPowerLogFiles + b::kTransientFiles);
  CHECK(b::kMapOverlayFiles == b::kMapOverlayLevels * 2);   // one .idx + one .pack per level
  CHECK(b::kTransientFiles == b::kExistsProbeFiles + b::kSizeProbeFiles + b::kMapIndexReadFiles +
                                  b::kTailReaderFiles + b::kDirectoryListFiles + b::kScreenshotFiles);
  CHECK(b::kHistoryPoolFiles == 4);
  CHECK(b::kMargin >= 2);
  CHECK(b::kMaxOpenFiles > 5);   // the old Arduino default is what broke the card
  CHECK(b::kMaxOpenFiles <= 255);   // SD_MMC::begin takes a uint8_t
  CHECK(b::kBeginMaxOpenFiles == b::kMaxOpenFiles);
  printf("est380: budget %d = map %d + overlays %d + history %d + power log %d + transient %d + "
         "margin %d\n",
         b::kMaxOpenFiles, b::kMapMainPack, b::kMapOverlayFiles, b::kHistoryPoolFiles,
         b::kPowerLogFiles, b::kTransientFiles, b::kMargin);
}

// --- 2/3. retry, one FAILED line a minute, recovery -----------------------------------------------
static void testRetryAndRecovery() {
  FakeVolume vol;
  HistoryStore store;
  store.begin(&vol, nullptr, bigCaps(), HistoryConfig{}, fakeNow, fakeLog);
  CHECK(store.ready());
  const int slot = store.ensureChannel("Public", 0x3f);
  CHECK(slot >= 0);

  g_log.clear();
  g_nowUs = 0;
  CHECK(store.appendMsg(slot, makeMsg(1, "hello"), 0) == 1);

  vol.failAppend = true;
  store.flushStep(1);
  CHECK(store.queued() == 1);                 // kept, not dropped
  CHECK(store.status().dropped == 0);
  CHECK(store.status().writeFail == 1);       // one record, once
  CHECK(countLog("[hist] write FAILED") == 1);
  CHECK(countLog("[hist] write FAILED open") == 1);

  // A retry within the same minute: still kept, still ONE failed record, no second line.
  g_nowUs += 1000000;
  store.flushStep(1);
  CHECK(store.queued() == 1);
  CHECK(store.status().dropped == 0);
  CHECK(store.status().writeFail == 1);
  CHECK(countLog("[hist] write FAILED") == 1);

  // Past a minute, the throttled line is printed again (still no drop, still one record counted).
  g_nowUs += 61000000;
  store.flushStep(1);
  CHECK(countLog("[hist] write FAILED") == 2);
  CHECK(store.status().writeFail == 1);
  CHECK(store.status().dropped == 0);

  // The pop-up: one CardError, taken once.
  uint32_t dropped = 99;
  CHECK(store.takeWriteNotice(&dropped) == WriteNotice::CardError);
  CHECK(dropped == 0);
  CHECK(store.takeWriteNotice(&dropped) == WriteNotice::None);

  // The card answers: the pass drains the backlog and logs the recovery once.
  vol.failAppend = false;
  g_log.clear();
  store.flushStep(1);
  CHECK(store.queued() == 0);
  CHECK(store.status().records == 1);
  CHECK(store.status().dropped == 0);
  CHECK(countLog("[hist] write recovered, 1 record(s) flushed") == 1);

  // A second message while healthy writes normally; the notice stays silent.
  CHECK(store.appendMsg(slot, makeMsg(2, "again"), 0) == 2);
  store.flushStep(1);
  CHECK(store.queued() == 0);
  CHECK(store.takeWriteNotice(&dropped) == WriteNotice::None);
  // The file really holds both records after the recovery.
  const std::string& bytes = vol.file("/h/ch-3f-Public.log");
  CHECK(bytes.find("hello") != std::string::npos);
  CHECK(bytes.find("again") != std::string::npos);

  // A NEW episode logs its first failure at once (the throttle was re-armed by the recovery).
  g_log.clear();
  vol.failAppend = true;
  CHECK(store.appendMsg(slot, makeMsg(3, "third"), 0) == 3);
  store.flushStep(1);
  CHECK(countLog("[hist] write FAILED") == 1);
  CHECK(store.status().writeFail == 2);
}

// --- 4. short write, repair, retry ----------------------------------------------------------------
static void testShortWriteRepair() {
  FakeVolume vol;
  HistoryStore store;
  store.begin(&vol, nullptr, bigCaps(), HistoryConfig{}, fakeNow, fakeLog);
  const int slot = store.ensureChannel("Public", 0x3f);
  g_log.clear();
  g_nowUs = 0;

  CHECK(store.appendMsg(slot, makeMsg(1, "short one"), 0) == 1);
  vol.shortWrites = 1;   // the header/record write returns 5 bytes
  store.flushStep(1);
  CHECK(store.queued() == 1);
  CHECK(store.status().dropped == 0);
  CHECK(store.status().writeFail == 1);
  CHECK(countLog("[hist] write FAILED write") == 1);

  // The retry repairs the torn tail, then writes the record: the file validates and holds the text.
  store.flushStep(1);
  CHECK(store.queued() == 0);
  CHECK(store.status().records == 1);
  CHECK(countLog("[hist] write recovered, 1 record(s) flushed") == 1);
  const std::string& bytes = vol.file("/h/ch-3f-Public.log");
  CHECK(bytes.size() > kHeaderBytes);

  // A fresh store reading the same volume sees the record, i.e. the file is not left torn.
  HistoryStore re;
  re.begin(&vol, nullptr, bigCaps(), HistoryConfig{}, fakeNow, fakeLog);
  const int slot2 = re.findSlot("ch-3f-Public");
  CHECK(slot2 >= 0);
  CHECK(re.loadNewest(slot2, kScanRing) == 1);
  CHECK(strcmp(re.scanMsg(0).text, "short one") == 0);
}

// --- 5/6. queue full: drops counted, notice says so ------------------------------------------------
static void testQueueFull() {
  FakeVolume vol;
  HistoryStore store;
  store.begin(&vol, nullptr, bigCaps(), HistoryConfig{}, fakeNow, fakeLog);
  const int slot = store.ensureChannel("Public", 0x3f);
  g_log.clear();
  g_nowUs = 0;

  vol.failAppend = true;
  for (int i = 1; i <= kQueueRecords; ++i) {
    CHECK(store.appendMsg(slot, makeMsg((uint32_t)i, "burst"), 0) == (uint32_t)i);
  }
  CHECK(store.queued() == kQueueRecords);
  store.flushStep(1);
  CHECK(store.queued() == kQueueRecords);     // the head stays, nothing dropped
  CHECK(store.status().dropped == 0);
  CHECK(store.status().writeFail == 1);       // only the head has been tried
  CHECK(store.takeWriteNotice(nullptr) == WriteNotice::CardError);

  // One more message while the queue is full: THIS is the loss, and it is counted and said.
  CHECK(store.appendMsg(slot, makeMsg(99, "overflow"), 0) == 0);
  CHECK(store.status().dropped == 1);
  CHECK(store.queued() == kQueueRecords);
  uint32_t dropped = 0;
  CHECK(store.takeWriteNotice(&dropped) == WriteNotice::CardErrorDropped);
  CHECK(dropped == 1);
  CHECK(store.takeWriteNotice(nullptr) == WriteNotice::None);   // one pop-up, not per message

  // Recovery drains all sixteen in the one pass the loop gives it.
  vol.failAppend = false;
  g_log.clear();
  store.flushStep(1);
  CHECK(store.queued() == 0);
  CHECK(store.status().records == (uint32_t)kQueueRecords);
  CHECK(store.status().dropped == 1);   // the overflow stays counted
  CHECK(countLog("[hist] write recovered, 16 record(s) flushed") == 1);

  // A burst that overflows while writes are healthy still counts the drop and gets its own notice.
  HistoryStore busy;
  busy.begin(&vol, nullptr, bigCaps(), HistoryConfig{}, fakeNow, fakeLog);
  const int slot2 = busy.ensureChannel("Public", 0x3f);
  for (int i = 1; i <= kQueueRecords; ++i) {
    busy.appendMsg(slot2, makeMsg((uint32_t)i, "queued"), 0);
  }
  CHECK(busy.appendMsg(slot2, makeMsg(99, "one too many"), 0) == 0);
  CHECK(busy.status().dropped == 1);
  dropped = 0;
  CHECK(busy.takeWriteNotice(&dropped) == WriteNotice::QueueFull);
  CHECK(dropped >= 1);
}

int main() {
  testBudget();
  testRetryAndRecovery();
  testShortWriteRepair();
  testQueueFull();
  if (g_fail != 0) {
    printf("est380: %d check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("est380: budget + retry/recover/drop ok\n");
  return 0;
}
