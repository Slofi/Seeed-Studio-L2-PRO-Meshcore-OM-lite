// SPDX-License-Identifier: MIT
// MeshCore's own text limits, in ONE place: the message ring and every copy the UI makes are sized
// from these, so a long bot reply can never be cut to a handful of characters again (EST-334).
//
// Where the numbers come from (lib/meshcore is upstream v1.18.0, never edited):
//   text: helpers/BaseChatMesh.h   MAX_TEXT_LEN = 10 * CIPHER_BLOCK_SIZE = 160 bytes
//         BaseChatMesh::sendGroupMessage() writes "<sender>: " in front of the text and the prefix is
//         part of that 160, so a group text (prefix included) never exceeds it.
//   name: helpers/ContactInfo.h    char name[32], terminator included (ChannelDetails.h is 32 too).
// src/mesh_link.cpp static_asserts both values against those upstream constants, so a MeshCore
// upgrade that changes either stops the build instead of silently cutting a message.
//
// This header is plain C++ (no Arduino, no LovyanGFX) because src/ui/chat_model.h is compiled by
// test/host with the system g++.
#pragma once

namespace om {

constexpr int kMeshTextMax = 160;   // MAX_TEXT_LEN: bytes of one channel text, sender prefix included
constexpr int kMeshTextCap = kMeshTextMax + 1;   // + the terminator
constexpr int kMeshNameMax = 32;    // sizeof(ContactInfo::name), terminator included
constexpr int kMeshNameCap = kMeshNameMax;       // the field already holds its own terminator

// The name this node sends with: MeshCore writes "<this>: " in front of every channel text.
constexpr const char* kMeshOwnName = "OM Lite";

constexpr int cstrLen(const char* s) {
  int n = 0;
  while (s != nullptr && s[n] != 0) ++n;
  return n;
}

// Bytes BaseChatMesh::sendGroupMessage() puts in front of our text: "<name>" plus ": ".
constexpr int kMeshOwnPrefix = cstrLen(kMeshOwnName) + 2;

// The longest text the air will carry from us: MAX_TEXT_LEN minus our own prefix.
constexpr int kMeshOwnTextMax = kMeshTextMax - kMeshOwnPrefix;

static_assert(kMeshOwnTextMax > 0, "our name must leave room for a message");

}  // namespace om
