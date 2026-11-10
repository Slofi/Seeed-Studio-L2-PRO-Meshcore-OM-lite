// SPDX-License-Identifier: MIT
// USB framing shared by the three framed commands on the CDC port: OMOTA1 (flash), OMSHOT
// (screenshot) and OMCMD1 (a test command). Plain C++ (no Arduino), so test/host proves the
// matcher, the split-frame rule and both timeouts with the system g++.
//
// The magic matcher is the state machine ota.cpp has always used: advance on a matching byte and,
// on a miss, restart at 1 if this byte could begin the magic again, otherwise at 0. Three
// independent matchers are fed the same byte stream, so adding a command never changes what the
// other two see.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace usb {

// Every magic is six bytes (OMOTA1, OMSHOT, OMCMD1), which is why one matcher is enough.
constexpr int kMagicLen = 6;

// The longest command text accepted. A DM is a 31-byte contact name plus a 155-byte text, and a
// quoted channel name fits as well; 300 bytes leaves room and bounds the frame buffer.
constexpr size_t kMaxCommandBytes = 300;

// The host writes a command in one go. The deadlines are the OTA header's own 1.5 s, here for the
// length line and then for the body, so a host that vanishes cannot leave the reader stuck.
constexpr uint32_t kHeaderTimeoutMs = 1500;
constexpr uint32_t kBodyTimeoutMs = 1500;

struct Magic {
  const char* text;
  uint8_t match;
};

inline bool feedMagic(Magic& m, char c) {
  if (c == m.text[m.match]) {
    if (++m.match < kMagicLen) return false;
    m.match = 0;
    return true;
  }
  m.match = (c == m.text[0]) ? 1 : 0;
  return false;
}

inline void resetMagic(Magic& m) { m.match = 0; }

// One OMCMD1 frame: the magic is found by scan(), then " <len>\n" and <len> UTF-8 bytes arrive
// byte by byte. Nothing here blocks: the caller feeds whatever Serial.available() gives it and
// calls poll() once per loop for the deadlines.
class CommandFrame {
 public:
  enum class Event : uint8_t { None, Ready, Failed };

  // Feed one byte while no frame is open. True means the magic just completed; call begin().
  bool scan(char c) {
    if (state_ != State::Idle) return false;
    return feedMagic(magic_, c);
  }

  // The host's OMCMD1 magic has been consumed; collect the length line from here.
  void begin(uint32_t nowMs) {
    state_ = State::Header;
    headLen_ = 0;
    len_ = 0;
    got_ = 0;
    startMs_ = nowMs;
    event_ = Event::None;
    error_ = "";
    buf_[0] = 0;
  }

  bool busy() const { return state_ == State::Header || state_ == State::Body; }

  // One byte of the header or the body. Returns Ready on the last body byte, Failed on a malformed
  // frame. After Ready or Failed, read text()/length()/error() and call consume().
  Event feed(char c, uint32_t nowMs) {
    if (state_ == State::Header) return feedHeader(c, nowMs);
    if (state_ == State::Body) return feedBody(c);
    return Event::None;
  }

  // Call once per loop while a frame is open: turns a stalled header or body into Failed("timeout").
  Event poll(uint32_t nowMs) {
    if (!busy()) return Event::None;
    const uint32_t limit = state_ == State::Header ? kHeaderTimeoutMs : kBodyTimeoutMs;
    if ((uint32_t)(nowMs - startMs_) > limit) return fail("timeout");
    return Event::None;
  }

  Event event() const { return event_; }
  char* text() { return buf_; }
  const char* text() const { return buf_; }
  size_t length() const { return len_; }
  const char* error() const { return error_; }

  // Back to scanning. The buffer is not cleared here: a caller that needs the text after Ready has
  // it before this call (ota.cpp prints it first).
  void consume() {
    state_ = State::Idle;
    event_ = Event::None;
    error_ = "";
    headLen_ = 0;
    len_ = 0;
    got_ = 0;
    resetMagic(magic_);
  }

  const Magic& magic() const { return magic_; }

 private:
  enum class State : uint8_t { Idle, Header, Body, Ready, Failed };

  Event feedHeader(char c, uint32_t nowMs) {
    if (c == '\r') return Event::None;   // tolerate CRLF from a host that adds it
    if (c == '\n') {
      if (headLen_ == 0) return fail("length");
      head_[headLen_] = 0;
      size_t value = 0;
      size_t i = 0;
      while (head_[i] == ' ') ++i;
      if (head_[i] == 0) return fail("length");
      for (; head_[i] != 0; ++i) {
        if (head_[i] < '0' || head_[i] > '9') return fail("length");
        value = value * 10 + (size_t)(head_[i] - '0');
        if (value > kMaxCommandBytes) return fail("length");
      }
      len_ = value;
      got_ = 0;
      startMs_ = nowMs;
      if (len_ == 0) return ready();
      state_ = State::Body;
      return Event::None;
    }
    if ((size_t)headLen_ + 1 >= sizeof(head_)) return fail("length");
    head_[headLen_++] = c;
    return Event::None;
  }

  Event feedBody(char c) {
    // A command is one line of UTF-8 text: a NUL or a line break inside it would either cut the
    // command or forge an answer line, so the frame is refused instead.
    if (c == 0 || c == '\n' || c == '\r') return fail("byte");
    buf_[got_++] = c;
    if (got_ == len_) return ready();
    return Event::None;
  }

  Event ready() {
    buf_[len_] = 0;
    state_ = State::Ready;
    event_ = Event::Ready;
    return event_;
  }

  Event fail(const char* reason) {
    state_ = State::Failed;
    event_ = Event::Failed;
    error_ = reason;
    return event_;
  }

  Magic magic_{"OMCMD1", 0};
  State state_ = State::Idle;
  char head_[12] = {};
  uint8_t headLen_ = 0;
  char buf_[kMaxCommandBytes + 1] = {};
  size_t len_ = 0;
  size_t got_ = 0;
  uint32_t startMs_ = 0;
  Event event_ = Event::None;
  const char* error_ = "";
};

}  // namespace usb
}  // namespace om
