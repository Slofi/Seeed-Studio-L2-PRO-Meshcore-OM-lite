// SPDX-License-Identifier: MIT
// The OMCMD1 command parser. Plain C++ only; see usb_cmd_logic.h for the rules.
#include "usb_cmd_logic.h"

#include <string.h>

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
      if (!sound::validSound((uint8_t)v)) {
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

  // Everything else, including anything that tries to read a key or a secret, is refused here and
  // never reaches a handler.
  c.error = "unknown";
  return c;
}

}  // namespace usb
}  // namespace om
