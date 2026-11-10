// SPDX-License-Identifier: MIT
// Host test for EST-343's USB test-command path: the OMCMD1 frame reader (magic, split frames, junk,
// wrong length, header/body timeout) and the command language (quoted names, arguments, refusals).
//
// Both files are plain C++ and are the same code the device runs; ota.cpp only feeds bytes to the
// frame class, and main.cpp only executes what parseCommand() accepted.
#include <stdio.h>
#include <string.h>

#include "usb_cmd_logic.h"
#include "usb_frames.h"

static int g_fail = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++g_fail;                                                           \
    }                                                                     \
  } while (0)

static void feedMagicBytes(om::usb::CommandFrame& f, const char* text, uint32_t nowMs,
                           bool* matched, size_t* matchedAt) {
  if (matched) *matched = false;
  if (matchedAt) *matchedAt = 0;
  for (size_t i = 0; text[i] != 0; ++i) {
    if (f.scan(text[i])) {
      if (matched) *matched = true;
      if (matchedAt) *matchedAt = i;
      f.begin(nowMs);
    }
  }
}

static om::usb::CommandFrame::Event feedHeader(om::usb::CommandFrame& f, const char* text,
                                                uint32_t nowMs) {
  om::usb::CommandFrame::Event ev = om::usb::CommandFrame::Event::None;
  for (size_t i = 0; text[i] != 0; ++i) {
    ev = f.feed(text[i], nowMs);
    if (ev != om::usb::CommandFrame::Event::None) return ev;
  }
  return ev;
}

static void testMagic() {
  om::usb::Magic m{"OMCMD1", 0};
  const char* magic = "OMCMD1";
  for (int i = 0; i < 5; ++i) CHECK(!om::usb::feedMagic(m, magic[i]));
  CHECK(om::usb::feedMagic(m, magic[5]));

  // A miss in the middle restarts at 1 when the byte could begin the magic again, and at 0 when it
  // could not. "OMXOMCMD1" is the recovery the stale-residue bug needed.
  om::usb::Magic r{"OMCMD1", 0};
  const char* junk = "OMXOMCMD1";
  bool done = false;
  for (size_t i = 0; junk[i] != 0; ++i) done = om::usb::feedMagic(r, junk[i]);
  CHECK(done);

  // The OTA, screenshot and command magics are all six bytes; a partial OTA magic must not complete
  // the command matcher.
  om::usb::Magic c{"OMCMD1", 0};
  const char* ota = "OMOTA1";
  done = false;
  for (size_t i = 0; ota[i] != 0; ++i) done = om::usb::feedMagic(c, ota[i]);
  CHECK(!done);
}

static void testFrameHappyPath() {
  om::usb::CommandFrame f;
  bool matched = false;
  size_t at = 0;
  feedMagicBytes(f, "OMCMD1", 1000, &matched, &at);
  CHECK(matched);
  CHECK(at == 5);        // only the last byte completes it
  CHECK(f.busy());       // begin() opened the header

  CHECK(feedHeader(f, " 5\n", 1000) == om::usb::CommandFrame::Event::None);
  CHECK(f.busy());
  om::usb::CommandFrame::Event ev = om::usb::CommandFrame::Event::None;
  const char* body = "hello";
  for (int i = 0; body[i]; ++i) ev = f.feed(body[i], 1000);
  CHECK(ev == om::usb::CommandFrame::Event::Ready);
  CHECK(f.length() == 5);
  CHECK(strcmp(f.text(), "hello") == 0);
  f.consume();
  CHECK(!f.busy());
  CHECK(f.event() == om::usb::CommandFrame::Event::None);
}

static void testSplitMagicAndFrames() {
  // The magic arrives in two writes; the matcher must remember the partial prefix (the S492 rule).
  om::usb::CommandFrame f;
  for (int i = 0; i < 3; ++i) CHECK(!f.scan("OMCMD1"[i]));
  bool matched = false;
  for (int i = 3; i < 6; ++i) {
    if (f.scan("OMCMD1"[i])) matched = true;
  }
  CHECK(matched);
  f.begin(100);
  CHECK(feedHeader(f, "1\n", 100) == om::usb::CommandFrame::Event::None);
  CHECK(f.feed('a', 100) == om::usb::CommandFrame::Event::Ready);
  f.consume();

  // Two complete frames back to back in one stream: after the first is consumed the scanner starts
  // again, so the second magic is found with no gap.
  om::usb::CommandFrame g;
  const char* stream = "OMCMD1 2\nhiOMCMD1 2\nyo";
  int ready = 0;
  for (size_t i = 0; stream[i] != 0; ++i) {
    const char c = stream[i];
    if (g.busy()) {
      if (g.feed(c, 0) == om::usb::CommandFrame::Event::Ready) {
        ++ready;
        if (ready == 1) CHECK(strcmp(g.text(), "hi") == 0);
        if (ready == 2) CHECK(strcmp(g.text(), "yo") == 0);
        g.consume();
      }
      continue;
    }
    if (g.scan(c)) g.begin(0);
  }
  CHECK(ready == 2);
}

static void testJunkBeforeMagic() {
  om::usb::CommandFrame f;
  bool matched = false;
  size_t at = 0;
  // Ordinary log text, a partial magic, a wrong magic, then the real one.
  feedMagicBytes(f, "log text OMCM OMSHO OMOTA1 OMCMD1", 500, &matched, &at);
  CHECK(matched);
  CHECK(f.busy());   // begin() ran; the frame is waiting for its header
  CHECK(feedHeader(f, "6\nstatus", 500) == om::usb::CommandFrame::Event::Ready);
  CHECK(strcmp(f.text(), "status") == 0);
}

static void testWrongLength() {
  om::usb::CommandFrame f;
  bool matched = false;
  size_t at = 0;
  feedMagicBytes(f, "OMCMD1", 10, &matched, &at);
  CHECK(matched);
  CHECK(feedHeader(f, "abc\n", 10) == om::usb::CommandFrame::Event::Failed);
  CHECK(strcmp(f.error(), "length") == 0);
  f.consume();

  for (int i = 0; i < 6; ++i) {
    if (f.scan("OMCMD1"[i])) {
      f.begin(20);
      break;
    }
  }
  CHECK(feedHeader(f, "999999\n", 20) == om::usb::CommandFrame::Event::Failed);
  CHECK(strcmp(f.error(), "length") == 0);
  f.consume();

  for (int i = 0; i < 6; ++i) {
    if (f.scan("OMCMD1"[i])) {
      f.begin(30);
      break;
    }
  }
  CHECK(feedHeader(f, "\n", 30) == om::usb::CommandFrame::Event::Failed);
  CHECK(strcmp(f.error(), "length") == 0);
  f.consume();

  // A zero-length frame is structurally valid; the shell refuses the empty command.
  for (int i = 0; i < 6; ++i) {
    if (f.scan("OMCMD1"[i])) {
      f.begin(40);
      break;
    }
  }
  CHECK(feedHeader(f, "0\n", 40) == om::usb::CommandFrame::Event::Ready);
  CHECK(f.length() == 0);
  CHECK(f.text()[0] == 0);
}

static void testTimeoutAndBadBody() {
  om::usb::CommandFrame f;
  for (int i = 0; i < 6; ++i) {
    if (f.scan("OMCMD1"[i])) {
      f.begin(1000);
      break;
    }
  }
  CHECK(feedHeader(f, " 3", 1000) == om::usb::CommandFrame::Event::None);
  CHECK(f.poll(1000 + om::usb::kHeaderTimeoutMs) == om::usb::CommandFrame::Event::None);
  CHECK(f.poll(1001 + om::usb::kHeaderTimeoutMs) == om::usb::CommandFrame::Event::Failed);
  CHECK(strcmp(f.error(), "timeout") == 0);
  f.consume();

  for (int i = 0; i < 6; ++i) {
    if (f.scan("OMCMD1"[i])) {
      f.begin(2000);
      break;
    }
  }
  CHECK(feedHeader(f, " 5\n", 2000) == om::usb::CommandFrame::Event::None);
  CHECK(f.feed('a', 2000) == om::usb::CommandFrame::Event::None);
  CHECK(f.poll(2000 + om::usb::kBodyTimeoutMs) == om::usb::CommandFrame::Event::None);
  CHECK(f.poll(2001 + om::usb::kBodyTimeoutMs) == om::usb::CommandFrame::Event::Failed);
  CHECK(strcmp(f.error(), "timeout") == 0);
  f.consume();

  // A NUL or a line break inside the command would cut the line or forge an answer.
  for (int i = 0; i < 6; ++i) {
    if (f.scan("OMCMD1"[i])) {
      f.begin(3000);
      break;
    }
  }
  CHECK(feedHeader(f, " 2\n", 3000) == om::usb::CommandFrame::Event::None);
  CHECK(f.feed('a', 3000) == om::usb::CommandFrame::Event::None);
  CHECK(f.feed('\n', 3000) == om::usb::CommandFrame::Event::Failed);
  CHECK(strcmp(f.error(), "byte") == 0);
}

static void testTokenizer() {
  char line1[] = "send \"Don't Panic\" hello world";
  const char* words[8];
  CHECK(om::usb::tokenize(line1, words, 8) == 4);
  CHECK(strcmp(words[0], "send") == 0);
  CHECK(strcmp(words[1], "Don't Panic") == 0);
  CHECK(strcmp(words[2], "hello") == 0);
  CHECK(strcmp(words[3], "world") == 0);

  char line2[] = "  a\t\"b  c\"   d  ";
  CHECK(om::usb::tokenize(line2, words, 8) == 3);
  CHECK(strcmp(words[0], "a") == 0);
  CHECK(strcmp(words[1], "b  c") == 0);
  CHECK(strcmp(words[2], "d") == 0);

  // Escapes: \" and \\ only.
  char line3[] = "x \"a\\\"b\" \"c\\\\d\" z";
  CHECK(om::usb::tokenize(line3, words, 8) == 4);
  CHECK(strcmp(words[1], "a\"b") == 0);
  CHECK(strcmp(words[2], "c\\d") == 0);

  char bad[] = "send \"unterminated";
  CHECK(om::usb::tokenize(bad, words, 8) == -1);
  char glued[] = "\"a\"b";
  CHECK(om::usb::tokenize(glued, words, 8) == -1);
  char tooMany[] = "a b c";
  CHECK(om::usb::tokenize(tooMany, words, 2) == -1);
}

static om::usb::Command parse(char* line) { return om::usb::parseCommand(line); }

static void testParseSendDm() {
  char a[] = "send \"Don't Panic\" hello world";
  om::usb::Command c = parse(a);
  CHECK(c.error == nullptr);
  CHECK(c.kind == om::usb::Kind::Send);
  CHECK(strcmp(c.target, "Don't Panic") == 0);
  CHECK(strcmp(c.text, "hello world") == 0);

  // tools/cmd.py quotes every argument, including the text; the quotes must not reach the air.
  char b[] = "send \"Don't Panic\" \"hello there\"";
  c = parse(b);
  CHECK(c.error == nullptr);
  CHECK(strcmp(c.target, "Don't Panic") == 0);
  CHECK(strcmp(c.text, "hello there") == 0);

  char d[] = "dm \"ERA-3 (SI)\" ping";
  c = parse(d);
  CHECK(c.error == nullptr);
  CHECK(c.kind == om::usb::Kind::Dm);
  CHECK(strcmp(c.target, "ERA-3 (SI)") == 0);
  CHECK(strcmp(c.text, "ping") == 0);

  char e[] = "dm a1b2c3d4 hi";
  c = parse(e);
  CHECK(c.error == nullptr);
  CHECK(c.kind == om::usb::Kind::Dm);
  CHECK(strcmp(c.target, "a1b2c3d4") == 0);

  // Empty text and a missing target are refused before anything can be sent.
  char f[] = "send \"Don't Panic\"";
  CHECK(parse(f).error != nullptr);
  char g[] = "send \"Don't Panic\" \"\"";
  CHECK(parse(g).error != nullptr);
  char h[] = "send";
  CHECK(parse(h).error != nullptr);
  char i[] = "send \"Don't Panic\"hello world";
  c = parse(i);
  CHECK(c.error != nullptr);
  char j[] = "send \"\" hello";
  CHECK(parse(j).error != nullptr);
  char k[] = "dm \"\" hello";
  CHECK(parse(k).error != nullptr);
}

static void testParseOthers() {
  char a[] = "status";
  om::usb::Command c = parse(a);
  CHECK(c.error == nullptr && c.kind == om::usb::Kind::Status);

  char b[] = "nodes";
  c = parse(b);
  CHECK(c.error == nullptr && c.kind == om::usb::Kind::Nodes);

  char d[] = "msgs";
  c = parse(d);
  CHECK(c.error == nullptr && c.kind == om::usb::Kind::Msgs && c.count == -1);
  char e[] = "msgs 5";
  c = parse(e);
  CHECK(c.error == nullptr && c.count == 5);
  char f[] = "msgs 0";
  c = parse(f);
  CHECK(c.error == nullptr && c.count == 0);
  char g[] = "msgs x";
  c = parse(g);
  CHECK(c.error != nullptr && strcmp(c.error, "number") == 0);

  char h[] = "tap 12 34";
  c = parse(h);
  CHECK(c.error == nullptr && c.kind == om::usb::Kind::Tap && c.numCount == 2);
  CHECK(c.nums[0] == 12 && c.nums[1] == 34);
  char i[] = "tap 12";
  CHECK(parse(i).error != nullptr);
  char j[] = "tap 12 x";
  c = parse(j);
  CHECK(c.error != nullptr && strcmp(c.error, "number") == 0);

  char k[] = "drag 10 20 100 120 300";
  c = parse(k);
  CHECK(c.error == nullptr && c.kind == om::usb::Kind::Drag && c.numCount == 5);
  CHECK(c.nums[0] == 10 && c.nums[4] == 300);
  char l[] = "drag 10 20 100 120";
  CHECK(parse(l).error != nullptr);

  char m[] = "key wake";
  c = parse(m);
  CHECK(c.error == nullptr && c.kind == om::usb::Kind::KeyWake);
  char n[] = "key sleep";
  c = parse(n);
  CHECK(c.error != nullptr && strcmp(c.error, "args") == 0);
  char o[] = "key wake now";
  CHECK(parse(o).error != nullptr);
}

static void testRefusals() {
  // Everything not on the list is refused here and never reaches a handler. In particular nothing
  // that names a key or a secret exists: these all come back as "unknown", not as a command.
  const char* refused[] = {
      "secret", "keys", "show key", "key", "channel secret", "status extra", "nodes all",
      "reboot", "erase", "ota", "shutdown", "passwords", "private key", "print secrets",
  };
  for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
    char line[64];
    snprintf(line, sizeof(line), "%s", refused[i]);
    const om::usb::Command c = parse(line);
    CHECK(c.error != nullptr);
    if (c.error != nullptr) CHECK(strcmp(c.error, "unknown") == 0 || strcmp(c.error, "args") == 0);
  }

  char empty[] = "";
  CHECK(parse(empty).error != nullptr);
  char spaces[] = "   \t ";
  CHECK(parse(spaces).error != nullptr);
  char quote[] = "\"unterminated";
  CHECK(parse(quote).error != nullptr);
}

int main() {
  testMagic();
  testFrameHappyPath();
  testSplitMagicAndFrames();
  testJunkBeforeMagic();
  testWrongLength();
  testTimeoutAndBadBody();
  testTokenizer();
  testParseSendDm();
  testParseOthers();
  testRefusals();
  if (g_fail) {
    printf("%d usb_cmd check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("usb_cmd: all checks passed\n");
  return 0;
}
