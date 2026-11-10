// SPDX-License-Identifier: MIT
// The new-node notice: an advert that ADDS a contact (MeshCore's onDiscoveredContact(is_new)) is
// worth one toast, one dot on the Nodes tab and one sound. An advert of a contact already in the
// table is not (EST-342). Plain C++ so test/host proves the new/known and DND rules with g++.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "node_menu.h"   // advTypeName: the toast and the sheet name a type the same way

namespace om {
namespace ui {

// The contact table's baseline for "was this contact already known?". Upstream's
// Listener::onDiscoveredContact(is_new) is NOT usable for this: MeshCore v1.18.0 initialises
// is_new = false and never sets it true on the normal add path (it is true only on the early-exit
// paths where the contact is NOT added). This little set of 32-bit key hashes is seeded from the
// table at begin and updated on every advert, so "new" really means "was not in the table".
constexpr int kKnownKeySlots = 128;   // MAX_CONTACTS (100) plus headroom

struct KnownKeySet {
  uint32_t slots[kKnownKeySlots] = {};
  int count = 0;

  static uint32_t keyHash(const uint8_t* key, int len) {
    uint32_t h = 2166136261u;   // FNV-1a
    for (int i = 0; i < len; ++i) {
      h = (h ^ key[i]) * 16777619u;
    }
    return h;
  }

  bool contains(uint32_t h) const {
    for (int i = 0; i < count; ++i) {
      if (slots[i] == h) return true;
    }
    return false;
  }

  // True when the hash was added, false when it was already there or the set is full.
  bool add(uint32_t h) {
    if (contains(h) || count >= kKnownKeySlots) return false;
    slots[count++] = h;
    return true;
  }

  bool remove(uint32_t h) {
    for (int i = 0; i < count; ++i) {
      if (slots[i] != h) continue;
      slots[i] = slots[--count];
      return true;
    }
    return false;
  }

  void clear() { count = 0; }
};

struct NodeNotice {
  bool toast = false;   // "New node: <name> (<type>)" for 3 s
  bool dot = false;     // a dot on the Nodes tab until that screen is opened
  bool sound = false;   // the "New node" per-event sound, silent under DND or Off
};

// A notice is only for a genuinely new contact. EST-366 (Filip ~21:50) puts the toast under the same
// DND rule as every other pop-up ("not while DND is on, except error/low battery"); the DOT on the
// Nodes tab still appears, because it is information on the bar, not a pop-up.
inline NodeNotice newNodeNotice(bool isNew, bool soundOn, bool dnd, bool soundOff) {
  NodeNotice n;
  if (!isNew) return n;
  n.toast = !dnd;
  n.dot = true;
  n.sound = soundOn && !dnd && !soundOff;
  return n;
}

// "New node: <name> (<type>)". Never longer than cap-1 bytes plus the terminator.
inline void newNodeToastText(char* out, size_t cap, const char* name, uint8_t advType) {
  if (out == nullptr || cap == 0) return;
  out[0] = 0;
  snprintf(out, cap, "New node: %s (%s)", (name && name[0]) ? name : "?", advTypeName(advType));
}

}  // namespace ui
}  // namespace om
