// SPDX-License-Identifier: MIT
// MeshCore's encoded path length, decoded in ONE place (EST-344).
//
// ContactInfo::out_path_len is not a byte count: MeshCore packs it, exactly as mesh::Packet keeps
// its own path_len (lib/meshcore/Packet.h):
//
//   hash size = (raw >> 6) + 1      // 1, 2 or 3 bytes per hop (4 is reserved by isValidPathLen)
//   hop count =  raw & 63           // how many repeaters the path holds
//   OUT_PATH_UNKNOWN = 0xFF         // ContactInfo.h: no path known for this contact
//
// The old UI divided the raw byte by our CURRENT path-hash preference, which is why a 2-byte,
// one-hop path (raw 0x41) was printed as "hops=32" and the Nodes list said "path 65B".
//
// Plain C++ (no Arduino), so test/host proves 1B/2B/3B encodings and the unknown sentinel with the
// system g++.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace om {

// MeshCore.h: MAX_PATH_SIZE 64 (bytes of path hashes); ContactInfo.h: OUT_PATH_UNKNOWN 0xFF.
// src/mesh_link.cpp static_asserts both against upstream.
constexpr uint8_t kPathBytesMax = 64;
constexpr uint8_t kOutPathUnknown = 0xFF;

struct PathSpec {
  uint8_t hashSize = 0;   // 1..3 bytes per hop, 0 when unknown
  uint8_t count = 0;      // number of hops (0 = heard directly)
  bool known = false;     // false for OUT_PATH_UNKNOWN and a bad encoding
};

// The one decoder. `raw` is ContactInfo::out_path_len (or a packet's path_len).
inline PathSpec decodeOutPathLen(uint8_t raw) {
  if (raw == kOutPathUnknown) return PathSpec{};
  const uint8_t hashSize = (uint8_t)((raw >> 6) + 1);
  const uint8_t count = (uint8_t)(raw & 0x3F);
  if (hashSize == 4) return PathSpec{};   // Packet::isValidPathLen: reserved for the future
  if ((int)count * (int)hashSize > (int)kPathBytesMax) return PathSpec{};
  return PathSpec{hashSize, count, true};
}

// Hop count for display: 0 = direct, >0 = repeaters, -1 = unknown.
inline int pathHopCount(uint8_t raw) {
  const PathSpec p = decodeOutPathLen(raw);
  return p.known ? (int)p.count : -1;
}

// The compact text every surface uses: "direct", "2 hops" or "--" for unknown.
inline void pathHopsText(char* out, size_t cap, int hops) {
  if (out == nullptr || cap == 0) return;
  if (hops < 0) {
    snprintf(out, cap, "--");
  } else if (hops == 0) {
    snprintf(out, cap, "direct");
  } else {
    snprintf(out, cap, "%d hop%s", hops, hops == 1 ? "" : "s");
  }
}

}  // namespace om
