// SPDX-License-Identifier: MIT
// EST-409: one fixed-size snapshot of a source list, copied only when the source's change signature
// moves. The Map uses it for the MeshCore contact table: meshContactList() walks every slot and
// insertion-sorts up to 32 records, and build() ran it on every animation frame (up to 30 a second)
// and twice per tap, although the table only changes when an advert arrives.
//
// Plain C++ (no Arduino, no LovyanGFX) so test/host proves the rebuild rule with the system g++.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

template <typename T, int N>
class SignatureCache {
 public:
  // True when the source's signature moved (or nothing has been copied yet): fill rows(), then call
  // commit(). False means the cached rows are still the source's list and must not be rebuilt.
  bool begin(uint32_t signature) {
    if (valid_ && signature == signature_) return false;
    signature_ = signature;
    valid_ = false;
    return true;
  }

  T* rows() { return rows_; }
  const T* rows() const { return rows_; }
  int count() const { return count_; }
  static constexpr int capacity() { return N; }

  void commit(int count) {
    count_ = count < 0 ? 0 : (count > N ? N : count);
    valid_ = true;
  }

  bool valid() const { return valid_; }
  uint32_t signature() const { return signature_; }

 private:
  T rows_[N] = {};
  int count_ = 0;
  uint32_t signature_ = 0;
  bool valid_ = false;
};

}  // namespace ui
}  // namespace om
