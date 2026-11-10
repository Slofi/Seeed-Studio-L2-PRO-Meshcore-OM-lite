// SPDX-License-Identifier: MIT
// The composer's byte budget, shared by the device and the host test.
//
// MeshCore's BaseChatMesh::sendGroupMessage() writes "<our name>: " in front of every channel text and
// the prefix counts against MAX_TEXT_LEN, and this firmware prefixes a reply with "@Name ". The
// composer therefore has to count those bytes itself: if it did not, MeshCore would cut the text on
// send and the user would never know. Plain C++ (no Arduino, no LovyanGFX) so test/host can prove it
// with the system g++.
#pragma once

#include <stddef.h>
#include <string.h>

#include "mesh_text.h"
#include "ui/text_wrap.h"

namespace om {
namespace ui {

// Bytes the draft may hold: MAX_TEXT_LEN, minus our own "<name>: " prefix, minus "@<reply name> ".
// A negative reply-name length would be nonsense, so the answer never goes below 0.
inline int composeAllowance(bool replying, const char* replyName) {
  int allow = kMeshOwnTextMax;
  if (replying && replyName != nullptr && replyName[0] != 0) {
    allow -= 1 + (int)strlen(replyName) + 1;
  }
  return allow < 0 ? 0 : allow;
}

// Bytes still free in a draft of `draftLen` bytes.
inline int composeRemaining(int allow, int draftLen) {
  const int left = allow - draftLen;
  return left < 0 ? 0 : left;
}

// Append one whole key or code point. Refused as a unit when it would not fit, so a multi-byte
// character is never half-written and the message is never cut on send.
inline bool composeAppend(char* draft, int cap, const char* add, int allow) {
  if (draft == nullptr || add == nullptr || cap <= 1) return false;
  const int have = (int)strlen(draft);
  const int n = (int)strlen(add);
  if (n <= 0 || have + n > allow || have + n >= cap) return false;
  memcpy(draft + have, add, (size_t)n + 1);   // copies the terminator too
  return true;
}

// Remove the last code point (a byte walk back to the sequence's lead byte), so a backspace on an
// emoji deletes the whole emoji instead of leaving broken bytes in the draft.
inline void composeBackspace(char* draft) {
  if (draft == nullptr) return;
  const int n = (int)strlen(draft);
  if (n <= 0) return;
  int cut = n - 1;
  while (cut > 0 && ((uint8_t)draft[cut] & 0xC0) == 0x80) --cut;
  draft[cut] = 0;
}

// COPY: take a received message into the draft. When it is longer than the budget the cut falls on a
// code point boundary and an ellipsis says so, exactly like a preview line.
inline void composeCopy(char* out, int cap, const char* src, int allow) {
  if (out == nullptr || cap <= 0) return;
  out[0] = 0;
  if (src == nullptr || cap <= 1) return;
  const int len = (int)strlen(src);
  if (len <= allow && len < cap) {
    memcpy(out, src, (size_t)len + 1);
    return;
  }
  const int room = allow - 3;   // the three dots of the ellipsis
  if (room <= 0) return;
  int cut = 0;
  for (int i = 0; src[i] != 0 && cut < room;) {
    int at = i;
    utf8Decode(src, &at);
    if (at - i > room - cut) break;   // this whole code point does not fit
    cut = at;
    i = at;
  }
  if (cut <= 0) return;
  memcpy(out, src, (size_t)cut);
  memcpy(out + cut, "...", 4);   // the terminator comes with it
}

}  // namespace ui
}  // namespace om
