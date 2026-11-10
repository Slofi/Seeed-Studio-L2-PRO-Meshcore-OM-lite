// SPDX-License-Identifier: MIT
// The Arduino FS glue behind history/history_log.h: SD_MMC on /omlite/history/, SPIFFS on /h/.
//
// SD_MMC is mounted by board.cpp (board::sdMount) and SPIFFS by mesh_link.cpp before this runs.
// mount() here only makes the directory and reports whether the volume really answers, so a card
// that was detected but failed to mount falls through to the bounded SPIFFS store.
#include "history_store.h"

#include <FS.h>
#include <esp32-hal-psram.h>
#include <stdlib.h>
#include <string.h>
#include <SD_MMC.h>
#include <SPIFFS.h>

namespace om {
namespace history {
namespace {

class DeviceFile : public HistoryFile {
 public:
  // append=true is mode "a": the file is created when it does not exist, with no error line.
  // A read is only started for a path the volume already said exists (FsVolume::openRead).
  bool begin(fs::FS& fs, const char* path, bool append) {
    f_ = fs.open(path, append ? FILE_APPEND : FILE_READ);
    busy_ = (bool)f_;
    return busy_;
  }

  int read(uint8_t* buf, int n) override { return (int)f_.read(buf, (size_t)n); }
  int write(const uint8_t* buf, int n) override { return (int)f_.write(buf, (size_t)n); }
  bool seek(uint32_t pos) override { return f_.seek(pos); }
  uint32_t size() override { return (uint32_t)f_.size(); }
  void flush() override { f_.flush(); }
  void close() override {
    f_.close();
    busy_ = false;
  }
  bool busy() const { return busy_; }

 private:
  File f_;
  bool busy_ = false;
};

class FsVolume : public HistoryVolume {
 public:
  FsVolume(fs::FS& fs, const char* name, const char* prefix)
      : fs_(fs), name_(name), prefix_(prefix) {}

  const char* name() const override { return name_; }
  const char* prefix() const override { return prefix_; }

  bool exists(const char* path) override { return fs_.exists(path); }

  uint32_t size(const char* path) override {
    // EST-349: probe with exists() first. open(FILE_READ) on a path that is not there logs
    // "does not exist, no permits for creation" (and does an extra SD transaction) for nothing.
    if (!fs_.exists(path)) return 0;
    File f = fs_.open(path, FILE_READ);
    if (!f) return 0;
    const uint32_t n = (uint32_t)f.size();
    f.close();
    return n;
  }

  bool remove(const char* path) override { return fs_.remove(path); }
  bool rename(const char* from, const char* to) override { return fs_.rename(from, to); }

  HistoryFile* openRead(const char* path) override {
    // Never open a missing path in read mode: the Arduino FS logs an error for it. exists() is a
    // stat, it does not log.
    if (!fs_.exists(path)) return nullptr;
    for (int i = 0; i < kFiles; ++i) {
      if (files_[i].busy()) continue;
      return files_[i].begin(fs_, path, false) ? &files_[i] : nullptr;
    }
    return nullptr;
  }

  HistoryFile* openAppend(const char* path) override {
    for (int i = 0; i < kFiles; ++i) {
      if (files_[i].busy()) continue;
      return files_[i].begin(fs_, path, true) ? &files_[i] : nullptr;
    }
    return nullptr;
  }

  void list(ListFn fn, void* ctx) override {
    // The store passes bare names; it already knows its prefix. Some FS builds dislike the
    // trailing slash on a directory open, so drop it here.
    char dirPath[24];
    snprintf(dirPath, sizeof(dirPath), "%s", prefix_);
    const size_t n = strlen(dirPath);
    if (n > 1 && dirPath[n - 1] == '/') dirPath[n - 1] = 0;
    File dir = fs_.open(dirPath);
    if (!dir || !dir.isDirectory()) return;
    for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
      if (!e.isDirectory()) {
        const char* full = e.name();
        const char* base = full;
        for (const char* p = full; p != nullptr && *p != 0; ++p) {
          if (*p == '/') base = p + 1;
        }
        fn(ctx, base, (uint32_t)e.size());
      }
      e.close();
    }
    dir.close();
  }

 protected:
  static constexpr int kFiles = 4;   // one repair reads one and writes another, plus a margin
  fs::FS& fs_;
  DeviceFile files_[kFiles];
  bool dirOk_ = false;

 private:
  const char* name_;
  const char* prefix_;
};

class SdVolume : public FsVolume {
 public:
  SdVolume() : FsVolume(SD_MMC, "sd", "/omlite/history/") {}
  // EST-349: create the directory tree first and check the result. mkdir on an existing directory
  // returns false, so probe with exists() (a stat, no error line) before creating.
  bool mount() override {
    if (SD_MMC.totalBytes() == 0) return false;
    bool ok = true;
    if (!SD_MMC.exists("/omlite")) ok = SD_MMC.mkdir("/omlite") && ok;
    if (!SD_MMC.exists("/omlite/history")) ok = SD_MMC.mkdir("/omlite/history") && ok;
    dirOk_ = ok && SD_MMC.exists("/omlite") && SD_MMC.exists("/omlite/history");
    if (!dirOk_) Serial.println("[hist] could not create /omlite/history on the card");
    return dirOk_;
  }
};

class SpiffsVolume : public FsVolume {
 public:
  SpiffsVolume() : FsVolume(SPIFFS, "spiffs", "/h/") {}
  bool mount() override {
    if (SPIFFS.totalBytes() == 0) return false;
    bool ok = true;
    if (!SPIFFS.exists("/h")) ok = SPIFFS.mkdir("/h") && ok;
    dirOk_ = ok && SPIFFS.exists("/h");
    if (!dirOk_) Serial.println("[hist] could not create /h on SPIFFS");
    return dirOk_;
  }
};

SdVolume s_sd;
SpiffsVolume s_spiffs;
HistoryStore s_store;
bool s_begun = false;

// EST-349: every large store buffer lives in PSRAM (the card must never be the reason loop RAM dies).
// One allocation each, on the same 4-byte alignment ps_malloc gives; a board without PSRAM falls back
// to malloc and says so.
HistoryScratch s_scratch;

bool allocScratch() {
  s_scratch.queue = (HistoryQueuedRecord*)ps_malloc(kQueueBytes);
  s_scratch.queueSlots = kQueueRecords;
  s_scratch.window = (uint8_t*)ps_malloc((size_t)kWindowBytes);
  s_scratch.scan = (Msg*)ps_malloc(kScanBytes);
  s_scratch.offs = (uint16_t*)ps_malloc(kOffsBytes);
  const bool allPsram = s_scratch.queue != nullptr && s_scratch.window != nullptr &&
                        s_scratch.scan != nullptr && s_scratch.offs != nullptr;
  if (!allPsram) {
    // A board whose PSRAM did not come up: internal heap is better than no history at all.
    if (s_scratch.queue == nullptr) s_scratch.queue = (HistoryQueuedRecord*)malloc(kQueueBytes);
    if (s_scratch.window == nullptr) s_scratch.window = (uint8_t*)malloc((size_t)kWindowBytes);
    if (s_scratch.scan == nullptr) s_scratch.scan = (Msg*)malloc(kScanBytes);
    if (s_scratch.offs == nullptr) s_scratch.offs = (uint16_t*)malloc(kOffsBytes);
  }
  Serial.printf("[hist] working memory %u B in %s (queue %u, window %u, scan %u, offs %u)\n",
                (unsigned)(kQueueBytes + (size_t)kWindowBytes + kScanBytes + kOffsBytes),
                allPsram ? "PSRAM" : "PSRAM + internal RAM", (unsigned)kQueueBytes,
                (unsigned)kWindowBytes, (unsigned)kScanBytes, (unsigned)kOffsBytes);
  return s_scratch.complete();
}

uint32_t deviceNowUs() { return (uint32_t)micros(); }
void deviceLog(const char* line) { Serial.println(line); }

}  // namespace

bool begin(bool sdMounted) {
  if (s_begun) return s_store.ready();
  s_begun = true;
  HistoryConfig sdCfg;   // the ticket's limits
  sdCfg.perChatCap = 2UL * 1024UL * 1024UL;
  sdCfg.totalCap = 64UL * 1024UL * 1024UL;
  HistoryConfig spiffsCfg;   // bounded: SPIFFS is 3.5 MB and also holds prefs, contacts, identity
  spiffsCfg.perChatCap = 48UL * 1024UL;
  spiffsCfg.totalCap = 512UL * 1024UL;
  allocScratch();
  s_store.begin(sdMounted ? &s_sd : nullptr, &s_spiffs, sdCfg, spiffsCfg, s_scratch, deviceNowUs,
                deviceLog);
  return s_store.ready();
}

// Called at the top of loop(), on the loop task only. Two records per pass drains the queue under a
// burst without a card write sitting in the packet path (worst case ~600 B).
void loop() {
  s_store.flushStep(s_store.queued() > 4 ? 2 : 1);
}

const HistoryStatus& status() { return s_store.status(); }

HistoryStore& store() { return s_store; }

}  // namespace history
}  // namespace om
