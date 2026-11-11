// SPDX-License-Identifier: MIT
// EST-382: the chat list's dots mean UNREAD (not "selected"), the ADVERT key's dropdown, the manual
// advert command, and Clear messages one channel at a time.
//
// Everything here is plain C++: the row layout is ui/chat_layout.h (the same function the drawing and
// the hit test use), the selector hit test is ui/chat_selector.h, the history clear runs the REAL
// HistoryStore over an in-memory volume, and the USB parser is the real one. No device, no card.
#include <stdio.h>
#include <string.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "history/history_log.h"
#include "usb_cmd_logic.h"
#include "ui/chat_layout.h"
#include "ui/chat_model.h"
#include "ui/chat_selector.h"

using namespace om;
using namespace om::ui;
using namespace om::history;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// ---------------------------------------------------------------------------------- the row model
static void testRowModel() {
  const Layout& L = l2();
  const Rect list{0, 60, L.width, 150};

  // selected + 0 unread: highlight only, no dot, no number.
  const SelRowLayout a = selRowLayoutFor(list, 2, 1, 0, 0, 0, true);
  CHECK(a.highlight);
  CHECK(!a.hasDot);
  CHECK(!a.hasCount);
  CHECK(a.dot.empty());
  CHECK(a.count.empty());
  CHECK(a.countText[0] == 0);
  CHECK(a.row.h == kSelRowH);

  // not selected + 3: the dot AND "3", no highlight.
  const SelRowLayout b = selRowLayoutFor(list, 2, 1, 1, 0, 3, false);
  CHECK(!b.highlight);
  CHECK(b.hasDot);
  CHECK(b.hasCount);
  CHECK(strcmp(b.countText, "3") == 0);
  CHECK(b.dot.w == kSelDot && b.dot.h == kSelDot);
  CHECK(b.dot.x == list.x + kSelPadX);                       // left side of the row
  CHECK(b.dot.y + b.dot.h / 2 == b.line1Cy);                 // centred on the name line
  CHECK(b.row.contains(b.dot.x, b.dot.y));
  CHECK(b.count.right() == b.row.right() - kSelPadX);        // right side, as before
  CHECK(b.count.w == kSelMonoAdvance);
  CHECK(b.row.contains(b.count.x, b.count.y));
  CHECK(!b.dot.overlaps(b.count));

  // selected + 5: highlight, dot and "5" all at once (the rule is independent of selection).
  const SelRowLayout c = selRowLayoutFor(list, 2, 1, 0, 0, 5, true);
  CHECK(c.highlight);
  CHECK(c.hasDot);
  CHECK(c.hasCount);
  CHECK(strcmp(c.countText, "5") == 0);

  // 150: the existing EST-364 cap ("99+"), still with the dot.
  const SelRowLayout d = selRowLayoutFor(list, 2, 1, 1, 0, 150, false);
  CHECK(d.hasDot);
  CHECK(d.hasCount);
  CHECK(strcmp(d.countText, "99+") == 0);
  CHECK(d.count.w == 3 * kSelMonoAdvance);

  // One source: unread == 0 is the ONLY way the dot and the number are absent; any unread has BOTH.
  for (int n = 0; n <= 200; ++n) {
    const SelRowLayout r =
        selRowLayoutFor(list, 2, 1, 1, 0, (uint16_t)n, n % 2 == 0);
    CHECK(r.hasDot == (n > 0));
    CHECK(r.hasCount == (n > 0));
    CHECK(r.countText[0] != 0 || n == 0);
  }

  // A DM row is the SAME row: same function, same rectangles, same rules. Compare it relative to its
  // own row to the channel row above (a DM is simply entry >= channelCount).
  const SelRowLayout dm = selRowLayoutFor(list, 2, 1, 2, 0, 3, false);
  CHECK(dm.hasDot && dm.hasCount);
  CHECK(strcmp(dm.countText, "3") == 0);
  CHECK(dm.dot.x - dm.row.x == b.dot.x - b.row.x);
  CHECK(dm.dot.y - dm.row.y == b.dot.y - b.row.y);
  CHECK(dm.count.right() == dm.row.right() - kSelPadX);
  CHECK(dm.row.h == b.row.h && dm.row.w == b.row.w);
}

// The selector's hit test and the drawing read the same row layout: a tap on the dot or the number
// belongs to that row, and a tap on a section label is not a row.
static void testHitUsesTheRow() {
  ChatEntry chats[3];
  snprintf(chats[0].name, sizeof(chats[0].name), "Channel A");
  chats[0].unread = 0;
  snprintf(chats[1].name, sizeof(chats[1].name), "DM B");
  chats[1].unread = 3;
  chats[1].dm = true;
  chats[1].thread = 0;
  snprintf(chats[2].name, sizeof(chats[2].name), "Channel C");
  chats[2].unread = 5;

  SelectorModel m;
  m.chats = chats;
  m.chatCount = 3;
  m.channelCount = 2;
  m.dmCount = 1;
  m.selected = 0;
  const Rect panel{0, 60, 320, 140};
  const Rect list{0, 60, 320, 110};
  const Rect manage{0, 170, 320, kSelManageH};

  const SelRowLayout b = selRowLayoutFor(list, 2, 1, 1, 0, 3, false);
  SelectorHitResult h = selectorHit(m, panel, list, manage, b.dot.x + 2, b.dot.y + 2);
  CHECK(h.kind == SelectorHitKind::Row && h.row == 1);
  h = selectorHit(m, panel, list, manage, b.count.x + 2, b.count.y + 2);
  CHECK(h.kind == SelectorHitKind::Row && h.row == 1);

  // The "Channels" label above the first row is the panel but not a row: Close, never a pick.
  h = selectorHit(m, panel, list, manage, 20, list.y + 3);
  CHECK(h.kind == SelectorHitKind::Close);

  // Selecting a row does not move its dot or number (the layout depends on unread only).
  const SelRowLayout bSel = selRowLayoutFor(list, 2, 1, 1, 0, 3, true);
  CHECK(bSel.dot.x == b.dot.x && bSel.dot.y == b.dot.y);
  CHECK(bSel.count.x == b.count.x && bSel.count.y == b.count.y);
  CHECK(bSel.highlight && !b.highlight);
}

// ----------------------------------------------------------------------------------------- ADVERT
static void testAdvertGeometry() {
  const Layout& L = l2();
  const ChatAdvertLayout a = chatAdvertLayoutFor(L);
  // The ticket's minimum: the DRAWN rect is the tap target, and it is at least 30 x 26.
  CHECK(a.button.w >= 30);
  CHECK(a.button.h >= 26);
  CHECK(a.button.h == kChatHeadH);
  CHECK(a.button.right() == L.content.right() - kHeadPadX);

  // The menu hangs DIRECTLY under the key, right-aligned, two 36 px rows (about 1.3x kSelRowH).
  CHECK(a.menu.y == a.button.bottom());
  CHECK(a.menu.x + a.menu.w == a.button.right());
  CHECK(a.opt[0].h == kAdvertOptH && a.opt[1].h == kAdvertOptH);
  CHECK(a.opt[0].y == a.menu.y + kAdvertMenuPad);
  CHECK(a.opt[1].y == a.opt[0].bottom());
  CHECK(a.menu.contains(a.opt[0].x, a.opt[0].y));
  CHECK(a.menu.contains(a.opt[1].x, a.opt[1].y));

  // A tap on an option sends that one; the panel elsewhere only closes; outside is None (the shell's
  // overlay rule closes it and spends the gesture).
  CHECK(chatAdvertHitAt(a, false, a.button.x + 2, a.button.y + 2) == ChatAdvertHit::Button);
  CHECK(chatAdvertHitAt(a, true, a.opt[0].x + 2, a.opt[0].y + 2) == ChatAdvertHit::Flood);
  CHECK(chatAdvertHitAt(a, true, a.opt[1].x + 2, a.opt[1].y + 2) == ChatAdvertHit::Local);
  CHECK(chatAdvertHitAt(a, true, a.menu.x, a.menu.y) == ChatAdvertHit::MenuOther);
  CHECK(chatAdvertHitAt(a, true, a.menu.x - 10, a.menu.y - 10) == ChatAdvertHit::None);

  // The header keeps a readable room for the selector after the key and the elsewhere dot.
  const ChatLayout g = chatLayoutFor(L, false, false, false, false);
  CHECK(g.advert.x == a.button.x && g.advert.y == a.button.y);
  CHECK(g.advertMenu.w == a.menu.w);
  CHECK(g.head.h == kChatHeadH);
  CHECK(g.headTitle.w >= 180);
  CHECK(!g.headTitle.overlaps(g.advert));
  CHECK(!g.head.overlaps(g.msgs));

  // The advert dropdown is an overlay: a press inside it is not spent, a press outside it is.
  ChatOverlayPress p =
      chatOverlayPress(g, false, Rect{}, a.menu.x + 3, a.menu.y + 3, true, a.menu);
  CHECK(p.overlay == ChatOverlay::AdvertMenu && p.inside);
  p = chatOverlayPress(g, false, Rect{}, 10, 120, true, a.menu);
  CHECK(p.overlay == ChatOverlay::AdvertMenu && !p.inside);
}

static void testAdvertCommand() {
  char local[] = "advert local";
  om::usb::Command c = om::usb::parseCommand(local);
  CHECK(c.kind == om::usb::Kind::Advert);
  CHECK(c.error == nullptr);
  CHECK(c.target != nullptr && strcmp(c.target, "local") == 0);

  char flood[] = "advert flood";
  c = om::usb::parseCommand(flood);
  CHECK(c.kind == om::usb::Kind::Advert);
  CHECK(c.error == nullptr);
  CHECK(c.target != nullptr && strcmp(c.target, "flood") == 0);

  // Exactly one of the two words: nothing else can be guessed into a packet on the air.
  char bare[] = "advert";
  c = om::usb::parseCommand(bare);
  CHECK(c.kind == om::usb::Kind::Advert);
  CHECK(c.error != nullptr && strcmp(c.error, "args") == 0);

  char bad[] = "advert both";
  c = om::usb::parseCommand(bad);
  CHECK(c.error != nullptr && strcmp(c.error, "args") == 0);

  char extra[] = "advert local now";
  c = om::usb::parseCommand(extra);
  CHECK(c.error != nullptr && strcmp(c.error, "args") == 0);
}

// ---------------------------------------------------------------------------------- history clear
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

 private:
  std::string* data_;
  bool append_;
  size_t pos_;
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
  bool remove(const char* path) override {
    if (failRemove) return false;
    return files_.erase(path) != 0;
  }
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
    handles_.push_back(std::make_unique<FakeFile>(&it->second, false));
    return handles_.back().get();
  }
  HistoryFile* openAppend(const char* path) override {
    auto it = files_.find(path);
    if (it == files_.end()) it = files_.emplace(path, std::string()).first;
    handles_.push_back(std::make_unique<FakeFile>(&it->second, true));
    return handles_.back().get();
  }
  void list(ListFn fn, void* ctx) override {
    for (const auto& kv : files_) {
      const size_t slash = kv.first.rfind('/');
      const std::string base =
          slash == std::string::npos ? kv.first : kv.first.substr(slash + 1);
      fn(ctx, base.c_str(), (uint32_t)kv.second.size());
    }
  }
  bool has(const std::string& path) const { return files_.count(path) != 0; }
  bool failRemove = false;

 private:
  const char* name_;
  const char* prefix_;
  std::map<std::string, std::string> files_;
  std::vector<std::unique_ptr<FakeFile>> handles_;
};

static uint32_t g_now = 0;
static uint32_t fakeNow() {
  g_now += 7;
  return g_now;
}
static void fakeLog(const char*) {}

static Msg makeMsg(uint32_t seq, const char* name, const char* text, bool mine) {
  Msg m;
  m.seq = seq;
  m.uptime = 100 + seq;
  m.wall = 1762700000u + seq;
  m.setWallReal(true);
  m.setMine(mine);
  msgSetText(m, name, text);
  m.hops = 1;
  return m;
}

static std::string pathOf(const char* prefix, uint8_t hash, const char* name, const char* suffix) {
  char id[24];
  channelId(hash, name, id, sizeof(id));
  char p[64];
  snprintf(p, sizeof(p), "%s%s%s", prefix, id, suffix);
  return p;
}

static void testClearOneChat() {
  FakeVolume vol("mem", "/mem/");
  HistoryConfig cfg;
  cfg.perChatCap = 1UL << 20;
  cfg.totalCap = 4UL << 20;
  HistoryStore hs;
  hs.begin(&vol, nullptr, cfg, cfg, fakeNow, fakeLog);
  CHECK(hs.ready());

  const int a = hs.ensureChannel("Channel A", 0x11);
  const int b = hs.ensureChannel("Channel B", 0x22);
  CHECK(a >= 0 && b >= 0);

  hs.appendMsg(a, makeMsg(1, "Alice", "a1", false), 1);
  hs.appendMsg(b, makeMsg(2, "Bob", "b1", false), 1);
  hs.flushStep(10);
  CHECK(hs.queued() == 0);

  // A second, NOT yet written message for each chat: the clear must drop exactly A's two records
  // (message + its Meta) from the EST-380 queue and leave B's two alone.
  hs.appendMsg(a, makeMsg(3, "Alice", "a2", false), 2);
  hs.appendMsg(b, makeMsg(4, "Bob", "b2", false), 4);
  CHECK(hs.queued() == 4);

  const std::string aLog = pathOf("/mem/", 0x11, "Channel A", ".log");
  const std::string bLog = pathOf("/mem/", 0x22, "Channel B", ".log");
  CHECK(vol.has(aLog));
  CHECK(vol.has(bLog));

  const ClearResult r = hs.clearChat(a);
  CHECK(!r.fileFailed);
  CHECK(r.fileGone);
  CHECK(r.records == 2);          // exactly A's message + Meta
  CHECK(hs.queued() == 2);        // B's two are still waiting
  CHECK(!vol.has(aLog));          // A's card copy is gone
  CHECK(vol.has(bLog));           // B's is untouched
  CHECK(hs.newestLogId(a) == 0);
  CHECK(hs.slotUnread(a) == 0);
  CHECK(hs.summary(a) == nullptr);
  CHECK(hs.newestLogId(b) > 0);
  CHECK(hs.slotUnread(b) == 4);   // B's unread is the one it had
  CHECK(hs.summary(b) != nullptr);

  // B's queued records still flush into B's file (nobody else's were dropped).
  hs.flushStep(10);
  CHECK(hs.queued() == 0);
  CHECK(!vol.has(aLog));
  CHECK(hs.countMessages(b) == 2);

  // A second clear of the now-empty A is a clean "missing", not an error.
  const ClearResult again = hs.clearChat(a);
  CHECK(!again.fileFailed);
  CHECK(!again.fileGone);
  CHECK(again.records == 0);

  // A refused delete changes nothing: RAM keeps the chat's unread and the file stays.
  FakeVolume vol2("mem", "/mem/");
  HistoryStore hs2;
  hs2.begin(&vol2, nullptr, cfg, cfg, fakeNow, fakeLog);
  const int c = hs2.ensureChannel("Channel C", 0x33);
  hs2.appendMsg(c, makeMsg(5, "Alice", "c1", false), 7);
  hs2.flushStep(10);
  const std::string cLog = pathOf("/mem/", 0x33, "Channel C", ".log");
  CHECK(vol2.has(cLog));
  vol2.failRemove = true;
  const ClearResult failed = hs2.clearChat(c);
  CHECK(failed.fileFailed);
  CHECK(!failed.fileGone);
  CHECK(failed.records == 0);
  CHECK(vol2.has(cLog));
  CHECK(hs2.slotUnread(c) == 7);
  CHECK(hs2.newestLogId(c) > 0);
}

int main() {
  testRowModel();
  testHitUsesTheRow();
  testAdvertGeometry();
  testAdvertCommand();
  testClearOneChat();
  if (g_fail != 0) {
    printf("%d failure(s)\n", g_fail);
    return 1;
  }
  printf("EST-382: row dots, advert geometry and command, clear-one-chat: all pass\n");
  return 0;
}
