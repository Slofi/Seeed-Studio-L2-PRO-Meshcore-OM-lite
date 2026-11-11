// SPDX-License-Identifier: MIT
// The OMCMD1 command language: one line of UTF-8 text, split on spaces, with double quotes for a
// word that contains spaces (a channel or contact name such as "Don't Panic").
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host proves the tokenizer, the quoted names, the
// argument errors and the refusal of everything that is not one of the listed commands. The shell
// in main.cpp only executes what this parser accepted.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sound_synth.h"   // EST-359/361: `play <event>` validates the same library the device uses

namespace om {
namespace usb {

enum class Kind : uint8_t {
  Unknown = 0,
  Send,     // send <channel name> <text>
  Dm,       // dm <contact name or pubkey prefix> <text>
  Status,   // status
  Nodes,    // nodes
  Msgs,     // msgs [n]
  Chats,    // chats   (EST-367: unread vs what each chat's own view shows vs the card)
  History,  // history <chat> [n]   (EST-348: the stored log on the card)
  Tap,      // tap <x> <y>
  Drag,     // drag <x0> <y0> <x1> <y1> <ms>
  KeyWake,  // key wake
  Wifi,     // wifi status | scan | on | off   (EST-354; never a password)
  Ble,      // ble status | on | off   (EST-356; never the pairing PIN)
  Rptr,     // rptr scan | rptr status <name> | rptr regions [name] | rptr login <name> <pw>
  Radio,    // radio | radio packets | radio status
            // radio tcxo 1.8|3.0 | radio boost on|off | radio sample <10..600>
            // (EST-365: what the L2 hears and the loop histogram; EST-376: the diagnostics)
  Pmu,      // pmu probe   (EST-376: read-only FUSB302 at 0x22 + the four ADS1115 inputs)
  Play,     // play <event> [sound-id]   (EST-359/361: hear the device's shared library)
};

// A parsed command. `target` and `text` point into the line given to parseCommand(), which is
// edited in place (quotes are removed and each word is NUL-terminated).
struct Command {
  Kind kind = Kind::Unknown;
  const char* target = nullptr;   // channel name, contact name or public-key prefix
  const char* text = nullptr;     // the send/DM text, already unquoted when it was quoted
  const char* text2 = nullptr;    // EST-365: `rptr login <name> <password>`, the second word
  int nums[5] = {0, 0, 0, 0, 0};
  int numCount = 0;
  int count = -1;                 // msgs [n]: -1 when n was not given
  const char* error = nullptr;    // nullptr = accepted; otherwise "unknown"/"args"/"quote"/"number"
};

// Splits `line` in place into at most maxOut words. A double-quoted word may contain spaces, and
// \" / \\ are unescaped. Returns the number of words, or -1 when a quote is not closed or a quoted
// word is followed directly by more text. Exposed for the host test; parseCommand() uses it.
int tokenize(char* line, const char** out, int maxOut);

// Parses one command line in place. Never reads past the terminator and never allocates. An empty
// line, an unknown verb and a malformed argument list all come back with `error` set, so a host
// gets one honest answer and nothing goes on the air.
Command parseCommand(char* line);

// EST-365: what the serial may log for a command. `rptr login <name> <password>` becomes
// `rptr login name="<name>" (password hidden)`; nothing else is changed except that control bytes
// and the double quote that would break the field become apostrophes. The password can never reach
// the log, a reply or an error line.
void redactCommand(const char* line, char* out, size_t cap);

}  // namespace usb
}  // namespace om
