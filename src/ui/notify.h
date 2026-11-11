// SPDX-License-Identifier: MIT
// EST-366 (Filip ~21:50): every notification event that plays a sound also shows the toast, for the
// same ~5 s as the new-contact notice. This header owns the decisions that must be provable on a
// laptop: the DND exemption (error and low battery only), the open-chat rule (a message already on
// screen does not pop up over itself), and the burst queue (the NEWEST pending one wins after the
// current toast ends; never a stack of ten).
//
// Plain C++ (no Arduino, no LovyanGFX). main.cpp calls noticeWanted() before showing and uses
// NoticeQueue when a toast is already on screen; ui/toast.h still owns the 5 s deadline and the
// consumed-tap dismissal (EST-346), so there is exactly one toast and one dismissal rule.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ui/notice_panel.h"

namespace om {
namespace ui {

enum class NoticeKind : uint8_t {
  Channel = 0,   // a channel message
  Dm,            // a direct message
  Mention,       // a channel message that names us
  NewNode,       // an advert added a contact
  LowBattery,    // the battery crossed the warning line
  Error,         // a radio/mesh fault
  Rptr,          // an RPTR result (Applied, a refusal, no answer)
};

// The one-line toast is ONE line in a 304 px panel: 48 bytes (about 46 characters plus "...") fits
// it and leaves the ellipsis visible. The toast's own buffer is 64, so this is the tighter of the
// two.
constexpr int kNoticeCap = 48;

// EST-368 (Filip ~22:25): a MESSAGE notice is two fields. `title` is the mono dim first line (who
// and where: "TehiLL RAK · #slovenija"), `body` is the message itself, wrapped by the pop-up's own
// layout (ui/notice_panel.h) over up to four rows. One-line notices keep using the old text forms.
constexpr int kNoticeTitleCap = 64;

struct Notice {
  char title[kNoticeTitleCap] = "";
  char body[kNoticeBodyCap] = "";
};

// Copy `src` into `out` at a whole UTF-8 boundary; never splits a sequence. Returns the bytes
// written. A body longer than the panel can show is cut here, and the panel then ends the visible
// text with "...", so the cut is always on a character and always inside the panel.
inline int noticeBodyCopy(char* out, size_t cap, const char* src) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = 0;
  if (src == nullptr) return 0;
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) --n;
  }
  memcpy(out, src, n);
  out[n] = 0;
  return (int)n;
}

// The channel/DM message forms: title only, body as it arrived. The old one-line text builders
// below stay for everything that is not a message (ping, RPTR result, the fault mark).
inline int noticeChannelNotice(Notice* n, const char* sender, const char* chat, const char* text,
                               bool mention) {
  if (n == nullptr) return 0;
  n->title[0] = 0;
  n->body[0] = 0;
  const char* who = (sender != nullptr && sender[0]) ? sender : "?";
  const char* where = (chat != nullptr && chat[0]) ? chat : "?";
  if (mention) {
    snprintf(n->title, sizeof(n->title), "Mention \xC2\xB7 %s \xC2\xB7 %s", who, where);
  } else {
    snprintf(n->title, sizeof(n->title), "%s \xC2\xB7 %s", who, where);
  }
  noticeBodyCopy(n->body, sizeof(n->body), text);
  return (int)strlen(n->title);
}

inline int noticeDmNotice(Notice* n, const char* sender, const char* text) {
  if (n == nullptr) return 0;
  n->title[0] = 0;
  n->body[0] = 0;
  const char* who = (sender != nullptr && sender[0]) ? sender : "?";
  snprintf(n->title, sizeof(n->title), "DM \xC2\xB7 %s", who);
  noticeBodyCopy(n->body, sizeof(n->body), text);
  return (int)strlen(n->title);
}


// DND silences every pop-up except the two urgent ones (the same exemption the sounds use).
inline bool noticeAllowed(NoticeKind kind, bool dnd) {
  if (!dnd) return true;
  return kind == NoticeKind::LowBattery || kind == NoticeKind::Error;
}

// The three notices that say "a message arrived" (the only ones with a chat rule).
inline bool noticeMessageLike(NoticeKind kind) {
  return kind == NoticeKind::Channel || kind == NoticeKind::Dm || kind == NoticeKind::Mention;
}

// `chatOpen` means the chat that received this message is the one drawn on screen right now. The
// message line is already there, so no toast covers it. Only message-like notices have this rule.
inline bool noticeWanted(NoticeKind kind, bool dnd, bool chatOpen) {
  if (!noticeAllowed(kind, dnd)) return false;
  if (chatOpen && noticeMessageLike(kind)) return false;
  return true;
}

// ---------------------------------------------------------------------------------------------
// EST-381: ONE decision and ONE log line per notice. The Map's missing pop-up was invisible in the
// code, so the ticket's test hook has to be able to say, on the device, what happened to a notice:
//   [notice] show kind=channel
//   [notice] queue kind=channel
//   [notice] skip(dnd) kind=channel
// The decision is plain C++ here; screens/toasts.cpp only acts on it and prints the line, so the
// hook, a real channel message, a ping and a DM all go through the same rule and the same words.

enum class NoticeDelivery : uint8_t {
  Shown = 0,   // on screen now (the caller must draw one frame)
  Queued,      // one newest pending is waiting for the current toast to end
  Skipped,     // nothing on screen, and `reason` says why
};

inline const char* noticeKindToken(NoticeKind kind) {
  switch (kind) {
    case NoticeKind::Channel: return "channel";
    case NoticeKind::Dm: return "dm";
    case NoticeKind::Mention: return "mention";
    case NoticeKind::NewNode: return "newnode";
    case NoticeKind::LowBattery: return "lowbattery";
    case NoticeKind::Error: return "error";
    case NoticeKind::Rptr: return "rptr";
    default: return "?";
  }
}

inline const char* noticeDeliveryToken(NoticeDelivery d) {
  switch (d) {
    case NoticeDelivery::Shown: return "show";
    case NoticeDelivery::Queued: return "queue";
    default: return "skip";
  }
}

struct NoticeDecision {
  NoticeDelivery delivery = NoticeDelivery::Skipped;
  const char* reason = "empty";   // only read when delivery == Skipped
};

// The whole rule, in one place: empty text is refused, a screen that is off is refused, DND keeps
// its two exemptions, an open chat is not covered by a message about itself, and a toast that is
// already up means this one waits (the newest one wins, see NoticeQueue).
inline NoticeDecision noticeDecide(NoticeKind kind, const char* title, bool screenOn, bool dnd,
                                   bool chatOpen, bool toastVisible) {
  NoticeDecision d;
  if (title == nullptr || title[0] == 0) {
    d.reason = "empty";
    return d;
  }
  if (!screenOn) {
    d.reason = "screen-off";
    return d;
  }
  if (!noticeAllowed(kind, dnd)) {
    d.reason = "dnd";
    return d;
  }
  if (chatOpen && noticeMessageLike(kind)) {
    d.reason = "chat-open";
    return d;
  }
  d.delivery = toastVisible ? NoticeDelivery::Queued : NoticeDelivery::Shown;
  d.reason = nullptr;
  return d;
}

// The one line. `kindToken` is the notice kind, or "text" for the shell's one-line toast door.
inline int noticeLogLine(char* out, size_t cap, const char* kindToken, const NoticeDecision& d) {
  if (out == nullptr || cap == 0) return 0;
  const char* kind = (kindToken != nullptr && kindToken[0] != 0) ? kindToken : "?";
  if (d.delivery == NoticeDelivery::Skipped) {
    const char* reason = (d.reason != nullptr && d.reason[0] != 0) ? d.reason : "?";
    snprintf(out, cap, "[notice] skip(%s) kind=%s", reason, kind);
  } else {
    snprintf(out, cap, "[notice] %s kind=%s", noticeDeliveryToken(d.delivery), kind);
  }
  return (int)strlen(out);
}

// Truncate at a UTF-8 boundary (never inside a continuation byte), then append "...". Returns the
// number of bytes written (without the terminator).
inline int noticeUtf8Cut(char* out, size_t cap, const char* src, size_t len) {
  if (out == nullptr || cap == 0) return 0;
  size_t keep = len;
  if (keep > cap - 1) keep = cap - 1;
  if (keep > 3) {
    // Only a CUT needs an ellipsis; a string that fits is copied whole.
    if (len > cap - 1) {
      keep = cap - 4;   // room for three dots and the terminator
      while (keep > 0 && ((unsigned char)src[keep] & 0xC0) == 0x80) --keep;
    }
  }
  memcpy(out, src, keep);
  out[keep] = 0;
  if (len > cap - 1 && cap >= 4) {
    memcpy(out + keep, "...", 4);   // includes the terminator
  }
  return (int)strlen(out);
}

// One line: CR/LF/TAB and runs of spaces become a single space, trailing space is dropped, and a
// value longer than `cap` ends in "...". This is the "<start of text>" part of a message toast.
inline int noticeOneLine(char* out, size_t cap, const char* text) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = 0;
  if (text == nullptr) return 0;
  char line[kNoticeCap * 2];
  size_t n = 0;
  bool pendingSpace = false;
  for (const char* p = text; *p && n + 1 < sizeof(line); ++p) {
    const char c = *p;
    if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
      pendingSpace = n > 0;
      continue;
    }
    if (pendingSpace) {
      line[n++] = ' ';
      pendingSpace = false;
    }
    line[n++] = c;
  }
  line[n] = 0;
  return noticeUtf8Cut(out, cap, line, n);
}

// Copy a finished line into the caller's buffer, bounded (the toast's own cap may be smaller than
// the composed text). Never splits a UTF-8 continuation byte.
inline int noticeCopy(char* out, size_t cap, const char* src) {
  if (out == nullptr || cap == 0) return 0;
  size_t n = src == nullptr ? 0 : strlen(src);
  if (n >= cap) {
    // A cut line ends in "...", on a whole UTF-8 character: the toast is one line, never a
    // half-drawn letter at its edge.
    if (cap >= 4) n = cap - 4; else n = cap - 1;
    while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) --n;
    if (n > 0) memcpy(out, src, n);
    out[n] = 0;
    if (cap >= 4) memcpy(out + n, "...", 4);
    return (int)strlen(out);
  }
  if (n > 0) memcpy(out, src, n);
  out[n] = 0;
  return (int)n;
}

// "<sender> in <chat>: <start>" and the mention-marked form, or "DM <sender>: <start>". The sender
// and chat fall back to "?" so a toast is never a bare colon.
inline int noticeChannelText(char* out, size_t cap, const char* sender, const char* chat,
                             const char* text, bool mention) {
  if (out == nullptr || cap == 0) return 0;
  char body[kNoticeCap];
  noticeOneLine(body, sizeof(body), text);
  const char* who = (sender != nullptr && sender[0]) ? sender : "?";
  const char* where = (chat != nullptr && chat[0]) ? chat : "?";
  char line[kNoticeCap * 3];
  const int n = mention ? snprintf(line, sizeof(line), "Mention: %s in %s: %s", who, where, body)
                        : snprintf(line, sizeof(line), "%s in %s: %s", who, where, body);
  if (n <= 0) {
    out[0] = 0;
    return 0;
  }
  return noticeCopy(out, cap, line);
}

inline int noticeDmText(char* out, size_t cap, const char* sender, const char* text) {
  if (out == nullptr || cap == 0) return 0;
  char body[kNoticeCap];
  noticeOneLine(body, sizeof(body), text);
  const char* who = (sender != nullptr && sender[0]) ? sender : "?";
  char line[kNoticeCap * 2];
  const int n = snprintf(line, sizeof(line), "DM %s: %s", who, body);
  if (n <= 0) {
    out[0] = 0;
    return 0;
  }
  return noticeCopy(out, cap, line);
}

inline int noticeBatteryText(char* out, size_t cap, int percent) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "Low battery: %d %%", percent);
  return n > 0 ? n : 0;
}

// The burst queue: ONE pending slot. A push always replaces what was waiting, so ten messages in a
// burst end with the tenth on screen after the current toast, never a queue of ten.
struct NoticeQueue {
  char pending[kNoticeTitleCap] = "";
  // EST-368: a queued message keeps its whole text, so the pop-up that appears after the current
  // one is still readable, not a cut line.
  char pendingBody[kNoticeBodyCap] = "";
  // EST-381: and its kind, so the `[notice] show kind=..` line names the notice that really waited
  // instead of guessing Channel.
  NoticeKind kind = NoticeKind::Channel;
  bool hasPending = false;

  // A bounded copy (never a truncated format warning, never a byte past the caller's buffer).
  static void boundedCopy(char* out, size_t cap, const char* src) {
    if (out == nullptr || cap == 0) return;
    size_t n = (src == nullptr) ? 0 : strlen(src);
    if (n > cap - 1) n = cap - 1;
    if (n > 0) memcpy(out, src, n);
    out[n] = 0;
  }
  bool push(const char* text) {
    if (text == nullptr || text[0] == 0) return false;
    boundedCopy(pending, sizeof(pending), text);
    pendingBody[0] = 0;
    kind = NoticeKind::Channel;
    hasPending = true;
    return true;
  }
  bool pushNotice(const Notice& n) { return pushNotice(NoticeKind::Channel, n); }
  bool pushNotice(NoticeKind k, const Notice& n) {
    if (n.title[0] == 0) return false;
    boundedCopy(pending, sizeof(pending), n.title);
    boundedCopy(pendingBody, sizeof(pendingBody), n.body);
    kind = k;
    hasPending = true;
    return true;
  }
  bool take(char* out, size_t cap) {
    if (!hasPending || out == nullptr || cap == 0) return false;
    boundedCopy(out, cap, pending);
    pending[0] = 0;
    pendingBody[0] = 0;
    kind = NoticeKind::Channel;
    hasPending = false;
    return true;
  }
  // EST-381: `kindOut` (optional) reports which kind the taken notice was, for its own log line.
  bool takeNotice(Notice* n, NoticeKind* kindOut = nullptr) {
    if (!hasPending || n == nullptr) return false;
    boundedCopy(n->title, sizeof(n->title), pending);
    boundedCopy(n->body, sizeof(n->body), pendingBody);
    if (kindOut != nullptr) *kindOut = kind;
    pending[0] = 0;
    pendingBody[0] = 0;
    kind = NoticeKind::Channel;
    hasPending = false;
    return true;
  }
  void clear() {
    pending[0] = 0;
    pendingBody[0] = 0;
    kind = NoticeKind::Channel;
    hasPending = false;
  }
};

}  // namespace ui
}  // namespace om
