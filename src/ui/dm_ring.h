// SPDX-License-Identifier: MIT
// The direct-message store: one thread per contact, one 32-message ring, and THE single allocation
// path for it (EST-344).
//
// Why this is a plain header: the DM bug was pure bookkeeping, not radio or pixels. storeDm() chose
// the slot with `s_dms[s_dmSeq % 32]` and then set `m.seq = ++s_dmSeq`, so a received DM with seq N
// landed in slot (N-1) % 32 while meshDmSend() and meshDmList() used slot N % 32. The reply
// overwrote the message before it and the list never found it. The ring now lives here so
// test/host can drive send, reply, more messages and a wrap with the exact code the firmware runs.
//
// Time and text come in from the caller; this header has no Arduino, no radio and no LovyanGFX.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../mesh_text.h"
#include "../seq_ring.h"

namespace om {
namespace ui {

constexpr int kDmRing = 32;
constexpr int kDmThreads = 16;
constexpr uint8_t kDmNoThread = 0xFF;

// One stored DM, exactly what the facade hands the UI. `state` is om::ui::DmState as a byte so this
// header stays free of the delivery machine (only the value travels with the message).
struct DmMsg {
  uint8_t thread;
  uint8_t mine;
  uint8_t state;     // om::ui::DmState
  uint8_t wallReal;
  uint8_t ackReal;
  int8_t hops;
  int8_t snr4;
  uint8_t pathHashSize;
  uint8_t pathCount;
  uint32_t seq;
  uint32_t ts;       // uptime seconds when stored
  uint32_t wall;     // system-clock seconds then
  uint32_t ackWall;  // system-clock seconds when the ACK arrived
  uint32_t wireTs;   // the timestamp in the packet (resends reuse it)
  char name[kMeshNameCap];
  char text[kMeshTextCap];
  uint8_t path[kMeshPathMax];
};

struct DmThread {
  uint8_t key[kMeshPubKey];
  char name[kMeshNameCap];
  uint16_t unread;
  uint32_t lastWall;
  bool used;
};

class DmRing {
 public:
  void reset() {
    for (int i = 0; i < kDmRing; ++i) msgs_[i] = DmMsg{};
    for (int i = 0; i < kDmThreads; ++i) threads_[i] = DmThread{};
    ring_.reset();
    threadCount_ = 0;
    serial_ = 0;
    recvSerial_ = 0;
  }

  // --- threads -----------------------------------------------------------------------------------
  int threadCount() const { return threadCount_; }

  const DmThread* thread(int idx) const {
    return (idx >= 0 && idx < kDmThreads) ? &threads_[idx] : nullptr;
  }
  DmThread* thread(int idx) { return (idx >= 0 && idx < kDmThreads) ? &threads_[idx] : nullptr; }

  int findThread(const uint8_t* key) const {
    if (key == nullptr) return -1;
    for (int i = 0; i < kDmThreads; ++i) {
      if (threads_[i].used && memcmp(threads_[i].key, key, kMeshPubKey) == 0) return i;
    }
    return -1;
  }

  // The thread for a contact, created when new. Returns -1 when the table is full (the message is
  // still stored in the ring, just without a thread). A new thread bumps the screen serial.
  int ensureThread(const uint8_t* key, const char* name) {
    const int found = findThread(key);
    if (found >= 0) return found;
    if (key == nullptr) return -1;
    for (int i = 0; i < kDmThreads; ++i) {
      if (threads_[i].used) continue;
      threads_[i].used = true;
      memcpy(threads_[i].key, key, kMeshPubKey);
      snprintf(threads_[i].name, sizeof(threads_[i].name), "%s", name ? name : "");
      threads_[i].unread = 0;
      threads_[i].lastWall = 0;
      ++threadCount_;
      ++serial_;
      return i;
    }
    return -1;
  }

  // Bookkeeping for a DM that ARRIVED: unread count, the thread's last time and its latest name.
  void noteReceived(int threadIdx, const char* name, uint32_t wall) {
    DmThread* t = thread(threadIdx);
    if (t == nullptr || !t->used) return;
    if (t->unread < 0xFFFF) ++t->unread;
    t->lastWall = wall;
    if (name != nullptr && name[0] != 0) snprintf(t->name, sizeof(t->name), "%s", name);
  }

  int unread(int threadIdx) const {
    const DmThread* t = thread(threadIdx);
    return (t != nullptr && t->used) ? (int)t->unread : 0;
  }

  void clearUnread(int threadIdx) {
    DmThread* t = thread(threadIdx);
    if (t != nullptr && t->used) t->unread = 0;
  }

  // --- messages ----------------------------------------------------------------------------------
  // THE allocation path. One sequence, one slot, committed in this call:
  //     seq = newest + 1, slot = seq % kDmRing
  // Both storeDm() and meshDmSend() use this and nothing else. Returns a zeroed message whose seq,
  // thread and mine are already set; the caller fills the rest. Every allocation bumps the serial.
  DmMsg* alloc(uint8_t threadIdx, bool mine) {
    const uint32_t seq = ring_.alloc();
    DmMsg& m = msgs_[SeqRing<kDmRing>::slotOf(seq)];
    memset(&m, 0, sizeof(m));
    m.seq = seq;
    m.thread = threadIdx;
    m.mine = mine ? 1 : 0;
    ++serial_;
    return &m;
  }

  DmMsg* find(uint32_t seq) {
    if (seq == 0) return nullptr;
    DmMsg& m = msgs_[SeqRing<kDmRing>::slotOf(seq)];
    return m.seq == seq ? &m : nullptr;
  }
  const DmMsg* find(uint32_t seq) const {
    if (seq == 0) return nullptr;
    const DmMsg& m = msgs_[SeqRing<kDmRing>::slotOf(seq)];
    return m.seq == seq ? &m : nullptr;
  }

  // The physical slot, exposed for the host test (a slot holds the newest message with that index).
  const DmMsg* slotAt(int slot) const {
    return (slot >= 0 && slot < kDmRing) ? &msgs_[slot] : nullptr;
  }

  // One thread's messages, oldest first, copied out so the caller can draw while more arrive.
  int list(int threadIdx, DmMsg* out, int max) const {
    if (out == nullptr || max <= 0 || threadIdx < 0 || threadIdx >= kDmThreads) return 0;
    int n = 0;
    const uint32_t newest = ring_.newest();
    for (uint32_t seq = ring_.firstSeq(); seq <= newest && n < max; ++seq) {
      const DmMsg& m = msgs_[SeqRing<kDmRing>::slotOf(seq)];
      if (m.seq != seq || m.thread != (uint8_t)threadIdx) continue;
      out[n++] = m;
    }
    return n;
  }

  int count() const { return ring_.count(); }
  uint32_t newestSeq() const { return ring_.newest(); }

  // --- delivery state ---------------------------------------------------------------------------
  // The ACK and the timeout both arrive by sequence, so they look the slot up the same way the list
  // does. Returns true only when something changed (the caller logs then, and the screen redraws).
  bool setState(uint32_t seq, uint8_t state) {
    DmMsg* m = find(seq);
    if (m == nullptr || m->state == state) return false;
    m->state = state;
    ++serial_;
    return true;
  }

  bool markDelivered(uint32_t seq, uint32_t wall, bool real) {
    DmMsg* m = find(seq);
    if (m == nullptr) return false;
    m->state = 1;   // om::ui::DmState::Delivered
    m->ackWall = wall;
    m->ackReal = real ? 1 : 0;
    ++serial_;
    return true;
  }

  uint32_t serial() const { return serial_; }
  uint32_t recvSerial() const { return recvSerial_; }
  void bumpSerial() { ++serial_; }
  void bumpRecvSerial() { ++recvSerial_; }

 private:
  DmMsg msgs_[kDmRing] = {};
  SeqRing<kDmRing> ring_;
  DmThread threads_[kDmThreads] = {};
  int threadCount_ = 0;
  uint32_t serial_ = 0;
  uint32_t recvSerial_ = 0;
};

static_assert(kDmRing == 32, "the UI's ChatMsgView buffer is sized from 32 DM messages");
static_assert(kDmThreads == 16, "the thread table size changed");

}  // namespace ui
}  // namespace om
