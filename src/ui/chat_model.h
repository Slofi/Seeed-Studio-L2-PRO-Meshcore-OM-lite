// SPDX-License-Identifier: MIT
// Plain model shared by the Chat screen and the host test. No LovyanGFX and no Arduino, so the
// sequence-number rule that keeps REPLY / INFO / the message menu on the same message can be proven
// with the system g++.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

// MeshCore's ring in mesh_link.cpp keeps 32 messages; the view never has more than that.
constexpr int kMaxChatMsgs = 32;

struct ChatMsgView {
  uint32_t seq = 0;         // mesh_link's monotonic message id; stable when the ring shifts
  const char* name = "";
  const char* text = "";
  const char* chanName = "";
  uint32_t ts = 0;          // uptime seconds when it was stored (monotonic: clock-jump proof)
  int8_t hops = -1;         // packet hops, -1 = local echo / unknown
  int8_t snrq = 0;          // SNR x 4 (MeshCore's unit), 0 = unknown
  bool mine = false;
};

// Index of the message carrying `seq`, or -1 when it has already fallen out of the ring. The UI holds
// the sequence number, never an index, so a new message that shifts the ring cannot move the target.
inline int chatMsgIndexBySeq(const ChatMsgView* msgs, int count, uint32_t seq) {
  if (!msgs || seq == 0) return -1;
  for (int i = 0; i < count; ++i) {
    if (msgs[i].seq == seq) return i;
  }
  return -1;
}

}  // namespace ui
}  // namespace om
