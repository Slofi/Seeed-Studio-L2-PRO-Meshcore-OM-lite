// SPDX-License-Identifier: MIT
// EST-367: one chat's own view, built from that chat's log instead of the shared arrival ring.
//
// The 32-message RAM ring in mesh_link is an ARRIVAL buffer shared by every chat. A busy channel
// evicts a quiet one: #slovenija's 28 messages lived only on the card while the ring held Don't
// Panic's traffic, so opening the quiet chat showed nothing while its unread count (from the card)
// said 26. A chat's view is now:
//
//   1. the NEWEST page of that chat's own log (kChatNewestPage), loaded when the chat opens with a
//      seek to the end of the file and one backwards 4 KB window (never a scan of the log),
//   2. older pages prepended in front of it while the finger scrolls up (kChatOlderPage each),
//   3. the arrival ring's messages for that chat that are NEWER than the newest stored message the
//      view already holds (or that have no log id yet), so a just-received message shows at once.
//
// The three rules below are plain C++ (no Arduino, no LovyanGFX) and test/host proves them: which
// arrival entries belong to the view, how many stored entries the view may keep once the arrivals
// need room, and when the `chats` check must say MISMATCH.
//
// Unread stays what it always was: the number of messages newer than the chat's seen point. Opening
// a chat moves the seen point to the newest message the view actually loaded; the normal open loads
// the newest page, which IS the newest of the chat, so unread goes to 0 exactly as before. A load
// that fell short (no card, a torn window) marks only what it showed and leaves the rest unread.
#pragma once

#include <stdint.h>

#include "ui/chat_model.h"   // kMaxChatMsgs: the shared arrival ring the live tail comes from

namespace om {
namespace ui {

constexpr int kChatNewestPage = 32;   // loaded from the chat's own log when it opens
constexpr int kChatOlderPage = 16;    // one scroll-up step
constexpr int kChatViewCap = 96;      // newest page + up to four older pages
// What the message band can draw and hit at once: the stored view plus the arrival tail.
constexpr int kChatViewMsgs = kChatViewCap + kMaxChatMsgs;
static_assert(kChatViewMsgs >= kChatViewCap + kMaxChatMsgs, "the view buffer must hold both");

// One run of the arrival ring that belongs to the open chat's view. `first` is the index of the
// ring's oldest message that is NOT already in the stored view; `count` is how many follow it (the
// ring is oldest first, so they are contiguous: a stored message's id only ever grows).
struct LiveTail {
  int first = 0;
  int count = 0;
};

// `logIds` are the arrival ring's per-message history ids for THIS chat, oldest first. A message is
// part of the view when it has no id yet (0: still queued, or history is off) or when its id is
// newer than the newest stored message the view already shows.
inline LiveTail chatLiveTail(const uint32_t* logIds, int count, uint32_t newestShownLogId) {
  LiveTail t;
  if (logIds == nullptr || count <= 0) return t;
  int first = count;
  for (int i = 0; i < count; ++i) {
    if (logIds[i] == 0 || logIds[i] > newestShownLogId) {
      first = i;
      break;
    }
  }
  t.first = first;
  t.count = count - first;
  return t;
}

// How many of the stored view's entries stay when `liveCount` arrivals need room in `cap`. The
// NEWEST stored entries win: the bottom of the chat is what the reader is looking at, and the ones
// dropped here are paged back by scrolling up.
inline int chatViewFirstKept(int viewCount, int liveCount, int cap) {
  if (viewCount <= 0) return 0;
  int keep = cap - liveCount;
  if (keep < 0) keep = 0;
  int first = viewCount - keep;
  return first < 0 ? 0 : first;
}

// The `chats` check (EST-367 rule 6): a chat whose view and log disagree. `shown` is the number of
// messages the per-view buffer held when the chat was opened (-1: never opened since boot).
inline bool chatViewMismatch(int shown, int unread, uint32_t cardNewestLogId,
                             uint32_t shownNewestLogId) {
  if (shown == 0 && unread > 0) return true;
  if (shown > 0 && shownNewestLogId != 0 && cardNewestLogId > shownNewestLogId) return true;
  return false;
}

}  // namespace ui
}  // namespace om
