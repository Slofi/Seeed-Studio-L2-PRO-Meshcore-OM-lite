// SPDX-License-Identifier: MIT
// ONE rule for every sequence-numbered ring in the firmware.
//
// A ring that keeps the newest N messages numbers them 1, 2, 3... for the life of the boot and
// stores message `seq` in slot `seq % N`. Readers walk the live sequence range, so a slot is never
// "the same message" after a wrap. This small template is that rule in one place, because EST-344
// found the DM ring allocating one slot and writing another: storeDm() picked slot `s_dmSeq % N`
// and THEN set `seq = ++s_dmSeq`, so every received DM landed one slot behind, a reply overwrote
// the message before it, and the list (which expects `seq % N`) never found it.
//
// Plain C++ (no Arduino), so test/host drives the exact allocation the firmware uses.
#pragma once

#include <stdint.h>

namespace om {

template <int N>
class SeqRing {
 public:
  static_assert(N > 0, "a ring needs at least one slot");

  // Where message `seq` lives. The low bits are the only index the ring ever needs.
  static constexpr int slotOf(uint32_t seq) { return (int)(seq % (uint32_t)N); }

  // The next message's number. Allocate BEFORE filling the slot; one call, one sequence, no way
  // for two code paths to disagree about which slot they mean.
  uint32_t alloc() {
    ++newest_;
    if (count_ < N) ++count_;
    return newest_;
  }

  uint32_t newest() const { return newest_; }
  int count() const { return count_; }
  uint32_t nextSeq() const { return newest_ + 1; }

  // The oldest sequence still in the ring. 1 while the ring has never wrapped; when empty the
  // caller's `for (seq = first; seq <= newest; ++seq)` simply does not run.
  uint32_t firstSeq() const { return count_ >= N ? newest_ - (uint32_t)N + 1 : 1; }

  void reset() {
    newest_ = 0;
    count_ = 0;
  }

 private:
  uint32_t newest_ = 0;
  int count_ = 0;
};

}  // namespace om
