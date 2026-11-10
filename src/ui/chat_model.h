// SPDX-License-Identifier: MIT
// Plain model shared by the Chat screen and the host test. No LovyanGFX and no Arduino, so the
// sequence-number rule that keeps REPLY / INFO / the message menu on the same message can be proven
// with the system g++.
#pragma once

#include <stdint.h>

#include "mesh_text.h"

namespace om {
namespace ui {

// MeshCore's ring in mesh_link.cpp keeps 32 messages; the view never has more than that.
constexpr int kMaxChatMsgs = 32;

// A chat's name: a channel's name is a MeshCore ChannelDetails::name (32 bytes with the terminator),
// a direct message is "DM: " plus the partner's name.
constexpr int kChatNameCap = kMeshNameCap + (int)sizeof("DM: ") - 1;

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

// One row of the chat selector: a channel or a direct message, exactly as the facade hands it over
// (no DM is invented while the DM store is empty). Copies, not pointers into mesh_link's ring, so a
// new packet cannot change the list under the finger.
struct ChatEntry {
  char name[kChatNameCap] = "";        // the channel's name, or "DM: <partner>" for a direct message
  uint16_t unread = 0;
  bool dm = false;
  bool muted = false;
  bool hasLast = false;
  char lastFrom[kMeshNameCap] = "";    // the last message's sender, MeshCore's own name limit
  char lastText[kMeshTextCap] = "";    // its whole text; the preview line ellipsises at draw time
  uint32_t lastTs = 0;                 // uptime seconds when it was stored
};

// Unread messages waiting in the chats other than the open one: the header's gold dot shows this, so
// traffic in a chat the user is not reading is never silent.
inline int chatUnreadElsewhere(const ChatEntry* chats, int count, int open) {
  if (!chats) return 0;
  int sum = 0;
  for (int i = 0; i < count; ++i) {
    if (i == open) continue;
    sum += chats[i].unread;
  }
  return sum;
}

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
