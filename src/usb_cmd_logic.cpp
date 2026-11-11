// SPDX-License-Identifier: MIT
// The OMCMD1 command parser. Plain C++ only; see usb_cmd_logic.h for the rules.
#include "usb_cmd_logic.h"

#include <stdio.h>
#include <string.h>

#include "gps_raw.h"   // EST-383: the `gps raw` window's own 5..120 s limits

namespace om {
namespace usb {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t'; }

// A cursor over the line. next() returns one word, honouring double quotes and the backslash
// escapes for a quote and for a backslash; rest() returns the untouched remainder (used for a
// send/DM text that was not quoted).
class Cursor {
 public:
  explicit Cursor(char* line) : p_(line) {}

  const char* next() {
    if (p_ == nullptr || bad_) return nullptr;
    while (isSpace(*p_)) ++p_;
    if (*p_ == 0) return nullptr;
    char* start = p_;
    if (*p_ == '"') {
      ++p_;
      char* write = start;   // never ahead of the read position, so editing in place is safe
      while (*p_ != 0 && *p_ != '"') {
        char c = *p_++;
        if (c == '\\' && (*p_ == '"' || *p_ == '\\')) c = *p_++;
        *write++ = c;
      }
      if (*p_ != '"') {
        bad_ = true;         // unterminated quote
        return nullptr;
      }
      ++p_;                  // closing quote
      if (*p_ != 0 && !isSpace(*p_)) {
        bad_ = true;         // "word"more is not one token
        return nullptr;
      }
      *write = 0;
      return start;
    }
    while (*p_ != 0 && !isSpace(*p_)) ++p_;
    if (*p_ != 0) {
      *p_ = 0;
      ++p_;
    }
    return start;
  }

  // The next word without consuming it. nullptr at end of line.
  const char* peek() {
    if (p_ == nullptr || bad_) return nullptr;
    while (isSpace(*p_)) ++p_;
    return *p_ == 0 ? nullptr : p_;
  }

  // Everything from the first non-space character, with trailing spaces removed.
  const char* rest() {
    if (p_ == nullptr || bad_) return nullptr;
    while (isSpace(*p_)) ++p_;
    if (*p_ == 0) return nullptr;
    char* start = p_;
    char* end = start + strlen(start);
    while (end > start && isSpace(end[-1])) --end;
    *end = 0;
    return start;
  }

  bool bad() const { return bad_; }

 private:
  char* p_ = nullptr;
  bool bad_ = false;
};

// Non-negative decimal integer. Returns false for a sign, a non-digit, an overflow, or a value
// above the small range these arguments live in.
bool toInt(const char* s, int* out) {
  if (s == nullptr || *s == 0) return false;
  int value = 0;
  for (const char* p = s; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    value = value * 10 + (*p - '0');
    if (value > 60000) return false;
  }
  if (out) *out = value;
  return true;
}

// send/dm: the target word, then the rest as text. A quoted rest is one word (so a text with
// spaces can be quoted by tools/cmd.py); an unquoted rest is the whole remainder, spaces and all.
const char* textAfterTarget(Cursor& cur, const char** error) {
  const char* r = cur.peek();
  if (r == nullptr) {
    *error = "args";
    return nullptr;
  }
  if (*r == '"') {
    const char* text = cur.next();
    if (cur.bad() || text == nullptr) {
      *error = "quote";
      return nullptr;
    }
    if (cur.next() != nullptr) {
      *error = "args";
      return nullptr;
    }
    if (text[0] == 0) {
      *error = "args";
      return nullptr;
    }
    return text;
  }
  const char* text = cur.rest();
  if (text == nullptr || *text == 0) {
    *error = "args";
    return nullptr;
  }
  return text;
}

// A fixed number of non-negative integer words and nothing else.
bool numberList(Cursor& cur, int* out, int count, const char** error) {
  for (int i = 0; i < count; ++i) {
    const char* word = cur.next();
    if (word == nullptr) {
      *error = cur.bad() ? "quote" : "args";
      return false;
    }
    if (!toInt(word, &out[i])) {
      *error = "number";
      return false;
    }
  }
  if (cur.next() != nullptr) {
    *error = cur.bad() ? "quote" : "args";
    return false;
  }
  return true;
}

}  // namespace

int tokenize(char* line, const char** out, int maxOut) {
  if (line == nullptr || out == nullptr || maxOut <= 0) return -1;
  Cursor cur(line);
  int n = 0;
  const char* word = nullptr;
  while ((word = cur.next()) != nullptr) {
    if (n >= maxOut) return -1;
    out[n++] = word;
  }
  if (cur.bad()) return -1;
  return n;
}

Command parseCommand(char* line) {
  Command c;
  if (line == nullptr) {
    c.error = "unknown";
    return c;
  }
  Cursor cur(line);
  const char* verb = cur.next();
  if (verb == nullptr) {
    c.error = cur.bad() ? "quote" : "unknown";
    return c;
  }

  if (strcmp(verb, "send") == 0) {
    c.kind = Kind::Send;
    c.target = cur.next();
    if (cur.bad() || c.target == nullptr || c.target[0] == 0) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    const char* error = nullptr;
    c.text = textAfterTarget(cur, &error);
    c.error = error;
    return c;
  }
  if (strcmp(verb, "dm") == 0) {
    c.kind = Kind::Dm;
    c.target = cur.next();
    if (cur.bad() || c.target == nullptr || c.target[0] == 0) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    const char* error = nullptr;
    c.text = textAfterTarget(cur, &error);
    c.error = error;
    return c;
  }
  if (strcmp(verb, "status") == 0 || strcmp(verb, "nodes") == 0) {
    c.kind = strcmp(verb, "status") == 0 ? Kind::Status : Kind::Nodes;
    if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
    return c;
  }
  // EST-367: the per-chat visibility check (`chats`). No arguments: it reports every chat.
  if (strcmp(verb, "chats") == 0) {
    c.kind = Kind::Chats;
    if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
    return c;
  }
  if (strcmp(verb, "msgs") == 0) {
    c.kind = Kind::Msgs;
    const char* word = cur.next();
    if (word != nullptr) {
      if (!toInt(word, &c.count)) {
        c.error = "number";
        return c;
      }
      if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
    } else if (cur.bad()) {
      c.error = "quote";
    }
    return c;
  }
  // EST-365: `radio` or `radio packets`. The data line(s) are printed first and the one answer
  // line terminates them, exactly like `nodes` and `msgs`.
  //
  // EST-376 adds the diagnostic forms. Their argument lists are closed so nothing but the named
  // values can reach the radio: `tcxo` takes exactly 1.8 or 3.0 (RadioLib's allowed DIO3 values,
  // and the two the ticket A/B tests), `boost` exactly on or off, `sample` a whole number of
  // seconds in 10..600. A wrong value is "value"; a missing or extra word is "args".
  if (strcmp(verb, "radio") == 0) {
    c.kind = Kind::Radio;
    const char* sub = cur.next();
    if (sub == nullptr) {
      if (cur.bad()) c.error = "quote";
      return c;   // plain `radio`: today's full status line
    }
    if (strcmp(sub, "packets") == 0 || strcmp(sub, "status") == 0) {
      c.target = sub;
      if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    if (strcmp(sub, "tcxo") == 0) {
      c.target = sub;
      const char* word = cur.next();
      if (word == nullptr) {
        c.error = cur.bad() ? "quote" : "args";
        return c;
      }
      if (strcmp(word, "1.8") != 0 && strcmp(word, "3.0") != 0) {
        c.error = "value";
        return c;
      }
      c.text = word;
      if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    if (strcmp(sub, "boost") == 0) {
      c.target = sub;
      const char* word = cur.next();
      if (word == nullptr) {
        c.error = cur.bad() ? "quote" : "args";
        return c;
      }
      if (strcmp(word, "on") != 0 && strcmp(word, "off") != 0) {
        c.error = "value";
        return c;
      }
      c.text = word;
      if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    if (strcmp(sub, "sample") == 0) {
      c.target = sub;
      const char* word = cur.next();
      if (word == nullptr) {
        c.error = cur.bad() ? "quote" : "args";
        return c;
      }
      int seconds = 0;
      if (!toInt(word, &seconds)) {
        c.error = "number";
        return c;
      }
      if (seconds < 10 || seconds > 600) {
        c.error = "value";
        return c;
      }
      c.count = seconds;
      if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    c.error = cur.bad() ? "quote" : "args";   // an unknown sub-command
    return c;
  }
  // EST-383: `gps raw <seconds>`, the L76K diagnostic window. One form only, 5..120 s; the same
  // closed-argument rule as `radio sample`, so nothing but a window length reaches the GNSS code.
  if (strcmp(verb, "gps") == 0) {
    c.kind = Kind::Gps;
    const char* sub = cur.next();
    if (sub == nullptr || strcmp(sub, "raw") != 0) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    c.target = sub;
    const char* word = cur.next();
    if (word == nullptr) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    int seconds = 0;
    if (!toInt(word, &seconds)) {
      c.error = "number";
      return c;
    }
    if (seconds < kGpsRawMinSeconds || seconds > kGpsRawMaxSeconds) {
      c.error = "value";
      return c;
    }
    c.count = seconds;
    if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
    return c;
  }
  // EST-384: `pos gps` and `pos fixed-here`, the node's own position source. One word, exactly one
  // of the two; nothing here carries a coordinate.
  if (strcmp(verb, "pos") == 0) {
    c.kind = Kind::Pos;
    const char* sub = cur.next();
    if (sub == nullptr) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    if (strcmp(sub, "gps") != 0 && strcmp(sub, "fixed-here") != 0) {
      c.error = "value";
      return c;
    }
    c.target = sub;
    if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
    return c;
  }
  // EST-376: the read-only charge probe. One form only: `pmu probe`.
  if (strcmp(verb, "pmu") == 0) {
    c.kind = Kind::Pmu;
    const char* sub = cur.next();
    if (sub == nullptr) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    if (strcmp(sub, "probe") != 0) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    c.target = sub;
    if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
    return c;
  }
  // EST-377: the battery log. One form only: `log power on|off|tail`. No path, no file content and
  // no date can be named from the host; the device owns the file name (/log/power-YYYYMMDD.csv).
  if (strcmp(verb, "log") == 0) {
    c.kind = Kind::Log;
    const char* sub = cur.next();
    if (sub == nullptr) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    if (strcmp(sub, "power") != 0) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    c.target = sub;
    const char* word = cur.next();
    if (word == nullptr) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    if (strcmp(word, "on") != 0 && strcmp(word, "off") != 0 && strcmp(word, "tail") != 0) {
      c.error = "value";
      return c;
    }
    c.text = word;
    if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
    return c;
  }
  if (strcmp(verb, "history") == 0) {
    c.kind = Kind::History;
    c.target = cur.next();
    if (cur.bad() || c.target == nullptr || c.target[0] == 0) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    const char* word = cur.next();
    if (word != nullptr) {
      if (!toInt(word, &c.count)) {
        c.error = "number";
        return c;
      }
      if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
    } else if (cur.bad()) {
      c.error = "quote";
    }
    return c;
  }
  if (strcmp(verb, "tap") == 0) {
    c.kind = Kind::Tap;
    const char* error = nullptr;
    if (numberList(cur, c.nums, 2, &error)) c.numCount = 2;
    c.error = error;
    return c;
  }
  if (strcmp(verb, "drag") == 0) {
    c.kind = Kind::Drag;
    const char* error = nullptr;
    if (numberList(cur, c.nums, 5, &error)) c.numCount = 5;
    c.error = error;
    return c;
  }
  if (strcmp(verb, "key") == 0) {
    const char* sub = cur.next();
    if (sub != nullptr && strcmp(sub, "wake") == 0 && cur.next() == nullptr && !cur.bad()) {
      c.kind = Kind::KeyWake;
      return c;
    }
    c.error = cur.bad() ? "quote" : "args";
    return c;
  }
  // EST-354: Wi-Fi is read and switched from the host, never configured: there is deliberately no
  // word that carries an SSID or a password, so a stored secret cannot be printed back.
  if (strcmp(verb, "wifi") == 0) {
    c.kind = Kind::Wifi;
    const char* sub = cur.next();
    if (sub == nullptr) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    const bool known = strcmp(sub, "status") == 0 || strcmp(sub, "scan") == 0 ||
                       strcmp(sub, "on") == 0 || strcmp(sub, "off") == 0;
    if (!known) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    c.target = sub;
    if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
    return c;
  }

  // EST-385: the Wi-Fi clock's own two verbs. `ntp now` starts one attempt through the same path the
  // schedule uses; `ntp status` reports the last attempt, its server and result, and the next attempt.
  // Neither takes an argument, and neither can name a network or an address.
  if (strcmp(verb, "ntp") == 0) {
    c.kind = Kind::Ntp;
    const char* sub = cur.next();
    if (sub == nullptr) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    if (strcmp(sub, "now") != 0 && strcmp(sub, "status") != 0) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    c.target = sub;
    if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
    return c;
  }

  // EST-388: `https probe`, one closed form. The target (host, path, port) is firmware constants;
  // no word here can name another server, another path or a query string. The measurement runs from
  // the loop and prints its one report line when it is over.
  if (strcmp(verb, "https") == 0) {
    c.kind = Kind::Https;
    const char* sub = cur.next();
    if (sub == nullptr) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    if (strcmp(sub, "probe") != 0) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    c.target = sub;
    if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
    return c;
  }

  // EST-356: Bluetooth is read and switched from the host. Like `wifi`, the subcommand list is the
  // whole surface: there is deliberately no word that reads the pairing PIN back.
  if (strcmp(verb, "ble") == 0) {
    c.kind = Kind::Ble;
    const char* sub = cur.next();
    if (sub == nullptr) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    const bool known = strcmp(sub, "status") == 0 || strcmp(sub, "on") == 0 ||
                       strcmp(sub, "off") == 0;
    if (!known) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    c.target = sub;
    if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
    return c;
  }

  // EST-358: the repeater tab from the host. EST-360 adds the anonymous nearby-regions read. The
  // forms are deliberately narrow, and no word can read a stored repeater password back.
  if (strcmp(verb, "rptr") == 0) {
    c.kind = Kind::Rptr;
    const char* sub = cur.next();
    if (sub == nullptr) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    if (strcmp(sub, "scan") == 0) {
      c.target = sub;
      if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    // EST-365: the reviewer's door to the logged-in RPTR pages. `rptr login <name> <password>`
    // goes through rptrLoginStart(), the screen's own path; the reply and the log never carry the
    // password (main.cpp logs the redactedCommand() form).
    if (strcmp(sub, "login") == 0) {
      c.target = sub;
      c.text = cur.next();    // the repeater's contact name
      c.text2 = cur.next();   // the password, not echoed or stored by the parser
      if (cur.bad() || c.text == nullptr || c.text[0] == 0 || c.text2 == nullptr ||
          c.text2[0] == 0) {
        c.error = cur.bad() ? "quote" : "args";
        return c;
      }
      if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    if (strcmp(sub, "status") == 0) {
      c.target = sub;
      c.text = cur.next();
      if (cur.bad() || c.text == nullptr || c.text[0] == 0) {
        c.error = cur.bad() ? "quote" : "args";
        return c;
      }
      if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    if (strcmp(sub, "regions") == 0) {
      // `rptr regions` asks every direct responder of the last scan; `rptr regions <name>` asks one
      // and prints the stored answer when it has one.
      c.target = sub;
      c.text = cur.next();
      if (c.text != nullptr && c.text[0] == 0) c.text = nullptr;
      if (cur.bad() || cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    c.error = cur.bad() ? "quote" : "args";
    return c;
  }

  // EST-359/361: hear the device's own sound library from the host. The event word is the table's
  // key (boot, message, dm, mention, sent, error, newnode, battery, touch); the optional number is a
  // library sound id (any sound can play as any event). A bad word or id is an argument error, never
  // a silent default.
  if (strcmp(verb, "play") == 0) {
    c.kind = Kind::Play;
    const char* word = cur.next();
    if (word == nullptr || word[0] == 0) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    sound::Event ev;
    if (!sound::parseEvent(word, &ev)) {
      c.error = "args";
      return c;
    }
    c.target = word;
    c.count = (int)sound::defaultSound(ev);
    const char* soundId = cur.next();
    if (soundId != nullptr) {
      int v = 0;
      if (!toInt(soundId, &v)) {
        c.error = "number";
        return c;
      }
      if (v < 0 || v > 255 || !sound::validSound((uint8_t)v)) {
        c.error = "args";
        return c;
      }
      c.count = v;
    } else if (cur.bad()) {
      c.error = "quote";
      return c;
    }
    if (cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
    return c;
  }

  // EST-382: the manual advert. Exactly two words: `advert local` (zero-hop) or `advert flood`.
  // Anything else is an argument error, so no word can be guessed into a packet on the air.
  if (strcmp(verb, "advert") == 0) {
    c.kind = Kind::Advert;
    const char* word = cur.next();
    if (word == nullptr || (strcmp(word, "local") != 0 && strcmp(word, "flood") != 0)) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    c.target = word;
    if (cur.bad() || cur.next() != nullptr) c.error = cur.bad() ? "quote" : "args";
    return c;
  }

  // EST-381: the pop-up without radio traffic. `notify test "<title>" "<body>"` goes through the
  // SAME door a real channel message takes (toasts::notifyNotice, kind Channel, never the open
  // chat), so the glass can be tested on any screen with no packet in the air. The title becomes the
  // pop-up's first line and the body is the message, exactly as a channel notice builds them. Both
  // words are required: nothing is invented from a missing one, and the body may be quoted or be the
  // rest of the line.
  if (strcmp(verb, "notify") == 0) {
    c.kind = Kind::Notify;
    const char* sub = cur.next();
    if (sub == nullptr || strcmp(sub, "test") != 0) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    c.target = cur.next();   // the title line
    if (cur.bad() || c.target == nullptr || c.target[0] == 0) {
      c.error = cur.bad() ? "quote" : "args";
      return c;
    }
    const char* error = nullptr;
    c.text = textAfterTarget(cur, &error);
    c.error = error;
    return c;
  }

  // Everything else, including anything that tries to read a key or a secret, is refused here and
  // never reaches a handler.
  c.error = "unknown";
  return c;
}

// EST-365: the log-safe form. Deliberately built from the TOKENS, not by hiding a slice of the raw
// line: whatever follows the name (an extra word, a malformed quote, a too-long line) is discarded
// with the password, so no spelling of `rptr login` can print it.
void redactCommand(const char* line, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = 0;
  if (line == nullptr) return;
  // Find the first two words by hand, without tokenize()'s output limit: a line with many extra
  // words must still be recognised as `rptr login` and lose everything after the name.
  const char* p = line;
  while (*p == ' ' || *p == '\t') ++p;
  const char* w1 = p;
  while (*p != 0 && *p != ' ' && *p != '\t') ++p;
  const size_t w1Len = (size_t)(p - w1);
  while (*p == ' ' || *p == '\t') ++p;
  const char* w2 = p;
  while (*p != 0 && *p != ' ' && *p != '\t') ++p;
  const size_t w2Len = (size_t)(p - w2);
  const bool isLogin = w1Len == 4 && w2Len == 5 && strncmp(w1, "rptr", 4) == 0 &&
                       strncmp(w2, "login", 5) == 0;
  if (isLogin) {
    while (*p == ' ' || *p == '\t') ++p;
    const char* name = p;
    size_t nameLen = 0;
    if (*p == '"') {
      ++name;
      while (name[nameLen] != 0 && name[nameLen] != '"') ++nameLen;
    } else {
      while (p[nameLen] != 0 && p[nameLen] != ' ' && p[nameLen] != '\t') ++nameLen;
    }
    char safe[80];
    size_t at = 0;
    for (size_t i = 0; i < nameLen && at + 1 < sizeof(safe); ++i) {
      const unsigned char ch = (unsigned char)name[i];
      safe[at++] = (ch < 0x20 || ch == 0x7F || ch == '"') ? '\'' : (char)ch;
    }
    safe[at] = 0;
    snprintf(out, cap, "rptr login name=\"%s\" (password hidden)", safe);
    return;
  }
  size_t at = 0;
  for (const char* q = line; *q != 0 && at + 1 < cap; ++q) {
    const unsigned char ch = (unsigned char)*q;
    out[at++] = (ch < 0x20 || ch == 0x7F || ch == '"') ? '\'' : (char)ch;
  }
  out[at] = 0;
}

}  // namespace usb
}  // namespace om
