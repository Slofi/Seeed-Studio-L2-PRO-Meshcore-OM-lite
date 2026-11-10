// SPDX-License-Identifier: MIT
// The Info page's contents: what the app can honestly say about one contact. The rows are built
// here (plain C++, no LovyanGFX) so test/host proves which ones appear and that the public key is
// shown short and whole, with the system g++.
//
// SNR of the last advert is NOT available: MeshCore's Listener::onDiscoveredContact() receives the
// contact and its path but not the packet, so there is no SNR to show (EST-342). The row is left out
// rather than invented; the field exists for the day the companion API carries it.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "node_menu.h"

namespace om {
namespace ui {

constexpr int kMaxNodeInfoLines = 10;
constexpr int kNodeInfoValueCap = 80;

struct NodeInfoModel {
  char name[32] = "";
  uint8_t advType = 0;
  uint8_t key[32] = {};       // ContactInfo::id.pub_key
  bool hasKey = false;
  bool hasPosition = false;
  int32_t lat_e6 = 0;
  int32_t lon_e6 = 0;
  char lastSeen[24] = "";     // already formatted as a time (ui/age.h), "" = unknown
  char path[96] = "";         // "direct", hop names or hex, "" = unknown
  int8_t snr4 = 0;
  bool hasSnr = false;        // never true today (see the header comment)
};

struct NodeInfoLine {
  const char* label = "";
  char value[kNodeInfoValueCap] = "";
};

// The public key as hex: `bytes` bytes -> 2*bytes digits plus the terminator. Returns the length.
inline int pubKeyHex(char* out, size_t cap, const uint8_t* key, int bytes) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = 0;
  if (key == nullptr || bytes <= 0) return 0;
  size_t n = 0;
  for (int i = 0; i < bytes && n + 2 < cap; ++i) {
    const int w = snprintf(out + n, cap - n, "%02x", key[i]);
    if (w != 2) break;
    n += 2;
  }
  out[n] = 0;
  return (int)n;
}

// Rows: name, type, public key (short, then the whole key split over two mono lines), last advert,
// position and path when known, SNR only when known. Returns 0..kMaxNodeInfoLines.
inline int nodeInfoLines(const NodeInfoModel& m, NodeInfoLine* out, int max) {
  if (out == nullptr || max <= 0) return 0;
  int n = 0;
  auto add = [&](const char* label, const char* value) {
    if (n >= max) return;
    out[n].label = label;
    const char* src = value ? value : "";
    size_t len = strlen(src);
    if (len >= sizeof(out[n].value)) len = sizeof(out[n].value) - 1;
    if (len > 0) memcpy(out[n].value, src, len);
    out[n].value[len] = 0;
    ++n;
  };
  add("Name", m.name[0] ? m.name : "?");
  add("Type", advTypeName(m.advType));
  if (m.hasKey) {
    char hex[72];
    pubKeyHex(hex, sizeof(hex), m.key, 32);
    char shortKey[24];
    snprintf(shortKey, sizeof(shortKey), "%.16s", hex);   // 8 bytes, as the Nodes row shows ids
    add("Key (short)", shortKey);
    // The whole key, split at 32 hex digits so a mono line fits 320 px at the theme's size.
    char part[40];
    snprintf(part, sizeof(part), "%.32s", hex);
    add("Key (1/2)", part);
    snprintf(part, sizeof(part), "%s", hex[32] ? hex + 32 : "");
    add("Key (2/2)", part);
  }
  if (m.lastSeen[0]) add("Last advert", m.lastSeen);
  if (m.hasPosition) {
    char pos[48];
    snprintf(pos, sizeof(pos), "%.6f, %.6f", (double)m.lat_e6 / 1e6, (double)m.lon_e6 / 1e6);
    add("Position", pos);
  }
  if (m.path[0]) add("Path", m.path);
  if (m.hasSnr) {
    char snr[24];
    snprintf(snr, sizeof(snr), "%.1f dB", (double)m.snr4 / 4.0);
    add("Last SNR", snr);
  }
  return n;
}

}  // namespace ui
}  // namespace om
