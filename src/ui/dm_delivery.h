// SPDX-License-Identifier: MIT
// The delivery state of the DMs we sent, from MeshCore's own ACK.
//
// BaseChatMesh::composeMsgPacket() hashes the frame (timestamp + attempt + text + our public key)
// into the 4-byte ACK the recipient sends back, and sendMessage() tells us how long the companion's
// own logic waits for it (est_timeout). MyMesh hands every received ACK to the Listener
// (onACKRecv), so the state is proven here, not guessed from "the radio said the packet went out":
//   Sending      the frame is on the air, the ACK has not arrived yet
//   Delivered    the matching ACK arrived (the time is kept for the screen)
//   NotConfirmed no ACK after the last attempt's timeout (NOT a radio fault)
//   Failed       MeshCore refused to compose/send at all (packet pool/tables), also not a fault
//
// The retry rule matches the companion app: attempt 0 first, then one resend per timeout, up to
// attempt 3 (composeMsgPacket keeps the attempt in 2 bits and changes the frame from attempt 4 on).
// Plain C++ (no Arduino, no LovyanGFX), so test/host proves the ACK and timeout rules with g++.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

enum class DmState : uint8_t {
  Sending = 0,
  Delivered = 1,
  NotConfirmed = 2,
  Failed = 3,
};

inline const char* dmStateToken(DmState s) {
  switch (s) {
    case DmState::Sending: return "sending";
    case DmState::Delivered: return "delivered";
    case DmState::NotConfirmed: return "not confirmed";
    case DmState::Failed: return "not sent";
    default: return "";
  }
}

// One sent DM waiting for its ACK.
class DmDeliveryTable {
 public:
  static constexpr int kSlots = 8;
  static constexpr uint8_t kMaxAttempts = 4;   // attempt 0, 1, 2, 3 (BaseChatMesh's 2-bit attempt)

  void reset() {
    for (int i = 0; i < kSlots; ++i) slots_[i] = Entry{};
  }

  // The first frame went out: remember its ACK and the companion's timeout. When every slot is in
  // use the oldest entry is dropped, so a new send is never lost.
  bool begin(uint32_t seq, uint32_t ack, uint32_t estTimeoutMs, uint32_t nowMs) {
    int at = freeSlot();
    if (at < 0) {
      at = 0;
      for (int i = 1; i < kSlots; ++i) {
        if (slots_[i].sentMs < slots_[at].sentMs) at = i;
      }
    }
    slots_[at].used = true;
    slots_[at].seq = seq;
    slots_[at].ack = ack;
    slots_[at].timeout = estTimeoutMs;
    slots_[at].sentMs = nowMs;
    slots_[at].deadline = nowMs + estTimeoutMs;
    slots_[at].attempt = 0;
    return true;
  }

  // The matching ACK arrived. Fills the message id and the round-trip time and clears the slot.
  bool ack(uint32_t ackCrc, uint32_t nowMs, uint32_t* seqOut, uint32_t* rttMs) {
    if (ackCrc == 0) return false;
    for (int i = 0; i < kSlots; ++i) {
      if (!slots_[i].used || slots_[i].ack != ackCrc) continue;
      if (seqOut) *seqOut = slots_[i].seq;
      if (rttMs) *rttMs = nowMs - slots_[i].sentMs;
      slots_[i] = Entry{};
      return true;
    }
    return false;
  }

  // Time has passed. Returns 0 for nothing, 1 when the next attempt is due (seqOut/attemptOut are
  // filled; the caller resends and calls rearm()) and 2 when every attempt is spent (NotConfirmed).
  int poll(uint32_t nowMs, uint32_t* seqOut, uint8_t* attemptOut) {
    for (int i = 0; i < kSlots; ++i) {
      Entry& e = slots_[i];
      if (!e.used) continue;
      if ((int32_t)(nowMs - e.deadline) < 0) continue;
      if (e.attempt + 1 < kMaxAttempts) {
        ++e.attempt;
        if (seqOut) *seqOut = e.seq;
        if (attemptOut) *attemptOut = e.attempt;
        // rearm() sets the real deadline from the new frame's timeout; this only stops a second
        // poll from firing again before the caller gets there.
        e.deadline = nowMs + e.timeout;
        return 1;
      }
      if (seqOut) *seqOut = e.seq;
      slots_[i] = Entry{};
      return 2;
    }
    return 0;
  }

  // A resend's ACK differs (the frame's attempt byte is part of the hash): the caller computed the
  // new expected ACK and the new timeout, and the slot follows it.
  bool rearm(uint32_t seq, uint32_t ack, uint32_t estTimeoutMs, uint32_t nowMs) {
    for (int i = 0; i < kSlots; ++i) {
      if (!slots_[i].used || slots_[i].seq != seq) continue;
      slots_[i].ack = ack;
      slots_[i].timeout = estTimeoutMs;
      slots_[i].sentMs = nowMs;
      slots_[i].deadline = nowMs + estTimeoutMs;
      return true;
    }
    return false;
  }

  bool pending(uint32_t seq) const {
    for (int i = 0; i < kSlots; ++i) {
      if (slots_[i].used && slots_[i].seq == seq) return true;
    }
    return false;
  }

  void drop(uint32_t seq) {
    for (int i = 0; i < kSlots; ++i) {
      if (slots_[i].used && slots_[i].seq == seq) slots_[i] = Entry{};
    }
  }

  int pendingCount() const {
    int n = 0;
    for (int i = 0; i < kSlots; ++i) {
      if (slots_[i].used) ++n;
    }
    return n;
  }

 private:
  struct Entry {
    bool used = false;
    uint32_t seq = 0;
    uint32_t ack = 0;
    uint32_t timeout = 0;
    uint32_t sentMs = 0;
    uint32_t deadline = 0;
    uint8_t attempt = 0;
  };

  int freeSlot() const {
    for (int i = 0; i < kSlots; ++i) {
      if (!slots_[i].used) return i;
    }
    return -1;
  }

  Entry slots_[kSlots] = {};
};

}  // namespace ui
}  // namespace om
