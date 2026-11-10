// SPDX-License-Identifier: MIT
// EST-367: a chat's own view, not the shared arrival ring.
//
// The data-visibility bug Filip hit on 2026-11-10 22:15: #slovenija had 28 messages on the card and
// 26 unread, but opening it showed nothing, because the chat screen read the ONE 32-message RAM ring
// shared by every channel and a busy channel had evicted it. This test drives the SAME HistoryStore
// the firmware runs over an in-memory volume and proves:
//   1. a quiet chat's newest page loads from its own log even when the ring holds none of it, and the
//      arrival-tail rule adds only what is newer than the newest stored message,
//   2. the selector's preview comes from that chat's own log (the summary), not the ring,
//   3. opening a chat with a full 2 MB log is a seek to the end plus a couple of 4 KB windows, never
//      a scan (windows and bytes are counted, and the host time is printed),
//   4. mark-as-read moves the seen point only through what the view showed, and unread is what stays
//      newer than it,
//   5. the `chats` check says MISMATCH exactly when the view and the log disagree,
//   6. the status card's rows are centred in the card (the first row used to sit on its top line),
//   7. the neighbour discovery: discover, the 60 s window, then `neighbors`; an unknown command
//      falls back to reading the table at once.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "history/history_log.h"
#include "rptr_model.h"
#include "ui/chat_view.h"
#include "ui/rptr_layout.h"
#include "usb_cmd_logic.h"

using namespace om::history;

// --- an in-memory volume (the same interface the device's SD/SPIFFS adapter fills) ------------------
class FakeFile : public HistoryFile {
 public:
  FakeFile(std::string* data, bool append, uint64_t* bytesRead)
      : data_(data), append_(append), pos_(append ? data->size() : 0), bytesRead_(bytesRead) {}

  int read(uint8_t* buf, int n) override {
    if (buf == nullptr || n <= 0 || pos_ >= data_->size()) return 0;
    size_t want = (size_t)n;
    if (want > data_->size() - pos_) want = data_->size() - pos_;
    memcpy(buf, data_->data() + pos_, want);
    pos_ += want;
    if (bytesRead_ != nullptr) *bytesRead_ += want;
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

 private:
  std::string* data_;
  bool append_;
  size_t pos_;
  uint64_t* bytesRead_;
  bool closed_ = false;
};

class FakeVolume : public HistoryVolume {
 public:
  explicit FakeVolume(const char* name, const char* prefix) : name_(name), prefix_(prefix) {}

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
    if (files_.count(to)) files_.erase(to);
    files_[to] = it->second;
    files_.erase(it);
    return true;
  }
  HistoryFile* openRead(const char* path) override {
    auto it = files_.find(path);
    if (it == files_.end()) return nullptr;
    handles_.push_back(std::make_unique<FakeFile>(&it->second, false, &bytesRead));
    return handles_.back().get();
  }
  HistoryFile* openAppend(const char* path) override {
    files_[path];
    handles_.push_back(std::make_unique<FakeFile>(&files_[path], true, &bytesRead));
    return handles_.back().get();
  }
  void list(ListFn fn, void* ctx) override {
    for (auto& e : files_) {
      const char* full = e.first.c_str();
      const char* base = strrchr(full, '/');
      fn(ctx, base != nullptr ? base + 1 : full, (uint32_t)e.second.size());
    }
  }

  uint64_t bytesRead = 0;   // every read this volume served: the "no scan" proof

 private:
  const char* name_;
  const char* prefix_;
  std::map<std::string, std::string> files_;
  std::vector<std::unique_ptr<FakeFile>> handles_;
};

static uint32_t fakeNow() { return 0; }
static void fakeLog(const char*) {}

static Msg makeMsg(uint32_t seq, const char* from, const char* text, bool mine, uint32_t wall) {
  Msg m;
  m.seq = seq;
  m.uptime = 1000 + seq;
  m.wall = wall;
  m.setWallReal(wall != 0);
  m.setMine(mine);
  m.hops = 1;
  m.snr4 = 24;
  msgSetText(m, from, text);
  return m;
}

// --- 1: the quiet chat's own page, and the arrival tail --------------------------------------------
static void testQuietChatKeepsItsOwnView() {
  FakeVolume vol("sd", "/h/");
  HistoryStore store;
  store.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);

  const int quiet = store.ensureChannel("slovenija", 0x11);
  const int busy = store.ensureChannel("test", 0x22);
  assert(quiet >= 0 && busy >= 0);

  // 28 messages into the quiet chat (Filip's #slovenija), then 40 into a busy one. The busy chat's
  // traffic is what the ONE 32-message ring would hold, so no quiet message survives there.
  for (int i = 1; i <= 28; ++i) {
    char text[48];
    snprintf(text, sizeof(text), "slovenija %d", i);
    store.appendMsg(quiet, makeMsg((uint32_t)i, "TehiLL RAK", text, false, 1762700000u + (uint32_t)i),
                    (uint16_t)i);
    store.flushStep(10);   // the loop task drains the queue; 28 in one go would overflow its 16 slots
  }
  uint32_t busyIds[40];
  for (int i = 0; i < 40; ++i) {
    char text[48];
    snprintf(text, sizeof(text), "busy %d", i);
    busyIds[i] = store.appendMsg(busy, makeMsg((uint32_t)(100 + i), "Don't Panic", text, false,
                                              1762700100u + (uint32_t)i), 0);
    store.flushStep(10);
  }

  // The arrival ring: the busy chat's last 32 only (the quiet chat was evicted). A chat's log ids are
  // per chat, so the shell filters the ring BY CHANNEL before this rule; the quiet chat's share of the
  // ring is therefore empty, and that empty tail must never be mistaken for "nothing to show".
  uint32_t ring[32];
  for (int i = 0; i < 32; ++i) ring[i] = busyIds[40 - 32 + i];
  uint32_t quietTailIds[32];
  int quietTailCount = 0;
  for (int i = 0; i < 32; ++i) {
    if (i % 2 == 0) continue;   // pretend every other ring entry was the quiet chat: still none
    quietTailIds[quietTailCount++] = ring[i];
  }

  // Opening the quiet chat: its own newest page, from its own log.
  Msg page[om::ui::kChatNewestPage];
  const int n = store.loadNewest(quiet, om::ui::kChatNewestPage);
  assert(n == 28);
  assert(store.scanMsg(27).logId == 28);
  assert(strcmp(store.scanMsg(27).text, "slovenija 28") == 0);
  for (int i = 0; i < n; ++i) page[i] = store.scanMsg(i);

  // The view's arrival tail for THIS chat: the shell passes only that chat's ring entries, and this
  // chat has none (they were evicted). So nothing is added, nothing is lost, and the view still shows
  // the 28 messages its own log holds. chatLiveTail/chatViewFirstKept are the shell's own rules.
  const om::ui::LiveTail none = om::ui::chatLiveTail(nullptr, 0, page[n - 1].logId);
  assert(none.count == 0);
  assert(om::ui::chatViewFirstKept(n, none.count, om::ui::kChatViewMsgs) == 0);
  assert(n == 28);   // the view shows all 28, not zero
  (void)quietTailIds;
  (void)quietTailCount;

  // A message of the quiet chat that is already in the stored page is not shown twice.
  const uint32_t dup[2] = {27, 28};
  const om::ui::LiveTail same = om::ui::chatLiveTail(dup, 2, 28);
  assert(same.count == 0);

  // A NEW arrival (id 29) is the only thing the ring adds to the view.
  const uint32_t fresh[3] = {27, 28, 29};
  const om::ui::LiveTail tail = om::ui::chatLiveTail(fresh, 3, 28);
  assert(tail.first == 2 && tail.count == 1);

  // A message with no history id yet (the record is still queued) is always part of the view.
  const uint32_t pending[2] = {28, 0};
  const om::ui::LiveTail withPending = om::ui::chatLiveTail(pending, 2, 28);
  assert(withPending.first == 1 && withPending.count == 1);

  // Room: the newest stored entries win when the arrivals need space.
  assert(om::ui::chatViewFirstKept(96, 32, om::ui::kChatViewMsgs) == 0);
  // kChatViewMsgs = 96 stored + 32 arrivals: 96 stored with 96 arrivals keeps the newest 64.
  assert(om::ui::chatViewFirstKept(96, 96, om::ui::kChatViewMsgs) == 64);
  assert(om::ui::chatViewFirstKept(96, 200, om::ui::kChatViewMsgs) == 96);

  // The `chats` count is exact while the log is under the 256 KB budget, which is the normal case.
  bool quietComplete = false;
  assert(store.countMessages(quiet, &quietComplete) == 28 && quietComplete);

  // The selector's preview: the chat's OWN newest message, while the ring holds none of it.
  const ChatSummary* sum = store.summary(quiet);
  assert(sum != nullptr && sum->has);
  assert(sum->logId == 28);
  assert(strcmp(sum->name, "TehiLL RAK") == 0);
  assert(strcmp(sum->text, "slovenija 28") == 0);
  assert(store.summary(busy) != nullptr && store.summary(busy)->logId == busyIds[39]);

  // A fresh store over the same volume (the reboot path) fills the same summary from the card.
  HistoryStore re;
  re.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
  const int q2 = re.findSlot("ch-11-slovenija");
  assert(q2 >= 0);
  const ChatSummary* again = re.summary(q2);
  assert(again != nullptr && again->logId == 28);
  assert(strcmp(again->text, "slovenija 28") == 0);

  printf("est367: quiet chat view ok (28 from its own log, ring held 0 of it)\n");
}

// --- 2: the open with a full 2 MB log is a seek, not a scan ----------------------------------------
static void testOpenWithFullLogIsCheap() {
  FakeVolume vol("sd", "/h/");
  HistoryStore store;
  store.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
  const int slot = store.ensureChannel("slovenija", 0x11);
  // ~2 MB: MeshCore caps one text at 160 B, so a message record is ~211 B plus its 16 B Meta: 9000
  // of them fill the 2 MB per-chat log without rotating.
  char text[om::kMeshTextCap];
  memset(text, 'x', sizeof(text) - 1);
  text[sizeof(text) - 1] = 0;
  for (int i = 1; i <= 9000; ++i) {
    char num[16];
    snprintf(num, sizeof(num), "m%d ", i);
    memcpy(text, num, strlen(num));   // a long text with a recognisable start
    store.appendMsg(slot, makeMsg((uint32_t)i, "TehiLL RAK", text, false, 1762700000u + (uint32_t)i),
                    0);
    store.flushStep(1000);
  }
  const uint32_t fileSize = vol.size("/h/ch-11-slovenija.log");
  assert(fileSize > 1800000u);   // a full 2 MB test log
  // The `chats` count is exact for a normal log (the quiet chat below) and a LOWER BOUND once the
  // 256 KB per-file budget is hit, so the check can never freeze the loop on a full log; the line
  // prints the '+' in that case.
  bool complete = true;
  const int cardCount = store.countMessages(slot, &complete);
  assert(!complete);
  assert(cardCount > 500);   // 256 KB of ~227 B records, not zero and not the whole log
  assert(cardCount < 9000);

  vol.bytesRead = 0;
  const uint32_t t0 = (uint32_t)(clock() / (CLOCKS_PER_SEC / 1000));
  const int n = store.loadNewest(slot, om::ui::kChatNewestPage);
  const uint32_t hostMs = (uint32_t)(clock() / (CLOCKS_PER_SEC / 1000)) - t0;
  const ReadStats rs = store.readStats();
  assert(n == om::ui::kChatNewestPage);
  assert(store.scanMsg(n - 1).logId == 9000);
  // The newest page is a seek to the end plus a couple of 4 KB windows: never the 2 MB file.
  assert(rs.windows <= 4);
  assert(rs.bytes <= 4u * (uint32_t)kWindowBytes);
  assert(vol.bytesRead <= 4u * (uint32_t)kWindowBytes);
  assert(vol.bytesRead * 20u < fileSize);
  printf("est367: open of a %lu B log: %d msg(s), %lu window(s), %lu B read, %lu ms (host)\n",
         (unsigned long)fileSize, n, (unsigned long)rs.windows, (unsigned long)rs.bytes,
         (unsigned long)hostMs);

  // The same open on a tiny log uses the same number of windows: the cost does not follow the file.
  FakeVolume small("sd", "/h/");
  HistoryStore tiny;
  tiny.begin(&small, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
  const int ts = tiny.ensureChannel("slovenija", 0x11);
  for (int i = 1; i <= 5; ++i) {
    tiny.appendMsg(ts, makeMsg((uint32_t)i, "A", "hello", false, 1762700000u + (uint32_t)i), 0);
    tiny.flushStep(10);
  }
  assert(tiny.loadNewest(ts, om::ui::kChatNewestPage) == 5);
  assert(tiny.readStats().windows == store.readStats().windows ||
         tiny.readStats().windows <= store.readStats().windows);
  printf("est367: open time model ok\n");
}

// --- 3: unread vs shown ----------------------------------------------------------------------------
static void testUnreadFollowsWhatWasShown() {
  // The pure rule first.
  assert(unreadAfterSeen(26, 28, 28) == 0);      // the whole newest page was shown
  assert(unreadAfterSeen(40, 68, 40) == 28);     // only part of it was shown: 28 stay unread
  assert(unreadAfterSeen(26, 68, 40) == 26);     // ... but a mark can never GROW the count
  assert(unreadAfterSeen(3, 68, 40) == 3);       // never MORE than what was unread
  assert(unreadAfterSeen(26, 28, 0) == 26);      // nothing marked

  FakeVolume vol("sd", "/h/");
  HistoryStore store;
  store.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
  const int slot = store.ensureChannel("slovenija", 0x11);
  for (int i = 1; i <= 28; ++i) {
    store.appendMsg(slot, makeMsg((uint32_t)i, "TehiLL RAK", "hi", false, 1762700000u + (uint32_t)i),
                    (uint16_t)i);
    store.flushStep(10);
  }
  assert(store.slotUnread(slot) == 28);
  // Opening the chat loads the newest page (28 messages) and marks exactly those read.
  assert(store.loadNewest(slot, om::ui::kChatNewestPage) == 28);
  assert(store.markSeenThrough(slot, store.scanMsg(27).logId));
  assert(store.slotUnread(slot) == 0);
  assert(store.slotSeen(slot) == 28);
  // Nothing is written by the mark; the loop's flush writes the one Meta record.
  store.flushStep(1000);

  // A partial load: 40 more messages arrive, but only up to id 40 is ever shown.
  for (int i = 29; i <= 68; ++i) {
    store.appendMsg(slot, makeMsg((uint32_t)i, "Marko", "new", false, 1762700000u + (uint32_t)i),
                    (uint16_t)(i - 28));
    store.flushStep(10);
  }
  assert(store.slotUnread(slot) == 40);
  assert(store.markSeenThrough(slot, 40));
  assert(store.slotUnread(slot) == 28);   // 68 - 40: the rest is still unseen
  store.flushStep(1000);

  // ...and after a reload the seen point and the remaining unread are still there.
  HistoryStore re;
  re.begin(&vol, nullptr, HistoryConfig{}, HistoryConfig{}, fakeNow, fakeLog);
  const int s2 = re.findSlot("ch-11-slovenija");
  assert(s2 >= 0);
  assert(re.slotSeen(s2) == 40);
  assert(re.slotUnread(s2) == 28);
  re.loadNewest(s2, om::ui::kChatNewestPage);
  assert(re.scanUnread() == 28);
  assert(re.scanSeen() == 40);

  // The `chats` check: MISMATCH only when the view and the log disagree.
  assert(om::ui::chatViewMismatch(28, 26, 28, 28) == false);   // shown everything, unread stale badge
  assert(om::ui::chatViewMismatch(0, 26, 28, 0) == true);      // nothing shown, 26 unread: THE bug
  assert(om::ui::chatViewMismatch(32, 0, 30, 30) == false);    // the view reached the card
  assert(om::ui::chatViewMismatch(32, 0, 31, 30) == true);     // the card has one newer than the view
  assert(om::ui::chatViewMismatch(-1, 5, 9, 0) == false);      // never opened: no claim
  printf("est367: unread vs shown ok\n");
}

// --- 4: the status card's rows sit inside the card -------------------------------------------------
static void testCardPadding() {
  const om::ui::Layout& L = om::ui::l2();
  const om::ui::RptrPageLayout a = om::ui::rptrPageLayoutFor(L, true, false, false, false, false);
  assert(!a.card.empty());
  assert(om::ui::kRptrCardPad * 2 + 3 * om::ui::kRptrCardRowH == om::ui::kRptrCardH);
  const om::ui::RptrCardCell first = om::ui::rptrCardCellAt(a, 0, 0);
  const om::ui::RptrCardCell last = om::ui::rptrCardCellAt(a, 1, 2);
  // An 11 px glyph around the centre line must stay inside the card's border.
  assert(first.cy - 6 >= a.card.y);
  assert(last.cy + 6 <= a.card.bottom());
  // The padding above row 0 equals the padding below row 2: that was the report.
  assert(first.cy - a.card.y == a.card.bottom() - last.cy);
  assert(first.cy > a.card.y + 3);   // the old drawing put row 0 at card.y + 3
  // The two columns keep the refresh gutter out of the values (EST-364 stays true).
  const om::ui::RptrCardCell right = om::ui::rptrCardCellAt(a, 1, 0);
  assert(right.valueRight <= a.refreshTap.x);
  assert(om::ui::rptrCardCellAt(a, 2, 0).x == 0);   // no third column
  printf("est367: card padding ok (top %d == bottom %d)\n", first.cy - a.card.y,
         a.card.bottom() - last.cy);
}

// --- 5: the neighbour discovery --------------------------------------------------------------------
static void testNeighbourScan() {
  using namespace om::rptr;
  NeighbourScan scan;
  assert(scan.phase == NbrScanPhase::Idle);
  assert(!scan.running());

  scan.start(1000);
  assert(scan.running());
  assert(scan.progressPercent(1000) == 0);
  assert(scan.progressPercent(31000) == 50);
  assert(scan.secondsLeft(1000) == 60);
  assert(scan.secondsLeft(31000) == 30);

  bool readNow = false;
  // The repeater's own OK keeps the window running.
  assert(scan.onDiscoverAnswer("OK - Discover sent", 1200, &readNow));
  assert(!readNow && scan.running());
  assert(scan.phase == NbrScanPhase::Discovering);

  // One second short of the window: no read.
  assert(!scan.tick(60999, &readNow));
  assert(!readNow && scan.running());
  // The window is over: ask for the table exactly once.
  assert(scan.tick(61000, &readNow));
  assert(readNow && scan.phase == NbrScanPhase::Reading);
  assert(!scan.tick(120000, &readNow));
  assert(!readNow);
  scan.onTable(true);
  assert(scan.phase == NbrScanPhase::Done);
  assert(!scan.running());
  assert(strcmp(scan.note(), "") == 0);

  // A repeater that does not know the command: read the table at once, and say why.
  NeighbourScan old;
  old.start(0);
  assert(!old.onDiscoverAnswer("Err - unknown command", 500, &readNow));
  assert(readNow);
  assert(old.phase == NbrScanPhase::Unsupported);
  assert(old.fallback);
  assert(strstr(old.note(), "does not know discover.neighbors") != nullptr);
  // The table read now still lands, and the note stays (it is why the table is what it is).
  old.onTable(true);
  assert(old.phase == NbrScanPhase::Done);
  assert(old.fallback);
  assert(strstr(old.note(), "does not know") != nullptr);

  // No answer at all to the discover: the same fallback, not a 60 s wait.
  NeighbourScan silent;
  silent.start(0);
  bool now2 = false;
  silent.onDiscoverTimeout(&now2);
  assert(now2 && silent.phase == NbrScanPhase::Unsupported);

  // The answer shapes.
  assert(NeighbourScan::isDiscoverOk("OK - Discover sent"));
  assert(NeighbourScan::isDiscoverOk("> OK - Discover sent\r\n"));
  assert(!NeighbourScan::isDiscoverOk("Err - unknown command"));
  assert(NeighbourScan::isUnknownCommand("Err - unknown command"));
  assert(!NeighbourScan::isUnknownCommand("OK - Discover sent"));
  printf("est367: neighbour discovery ok\n");
}

// --- 6: the `chats` command is part of the command language ---------------------------------------
static void testChatsCommand() {
  char line1[] = "chats";
  const om::usb::Command c = om::usb::parseCommand(line1);
  assert(c.error == nullptr);
  assert(c.kind == om::usb::Kind::Chats);
  char line2[] = "chats now";
  const om::usb::Command bad = om::usb::parseCommand(line2);
  assert(bad.error != nullptr);
  printf("est367: chats command ok\n");
}

int main() {
  testQuietChatKeepsItsOwnView();
  testOpenWithFullLogIsCheap();
  testUnreadFollowsWhatWasShown();
  testCardPadding();
  testNeighbourScan();
  testChatsCommand();
  printf("est367: per-chat view, open time, unread vs shown, card padding, neighbour discovery ok\n");
  return 0;
}
