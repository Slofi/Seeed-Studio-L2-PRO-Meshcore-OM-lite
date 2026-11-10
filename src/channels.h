// SPDX-License-Identifier: MIT
// OM Lite — MeshCore settings and channels, mirrored from ERA-3 (SI) over the companion protocol.
//
// Read on 2026-11-09 with the meshcore Python client against Gandalf's OM:
//   radio 869.618 MHz / BW 62.5 / SF 8 / CR 8 / tx 22 dBm, name "ERA-3 (SI)"
//   ch0 Public, ch1 #slovenija, ch2 Don't Panic, ch3 #test, ch4 #slovenia
// The secrets are the node's own channel keys; they are needed to join those channels, so they live
// here for the mesh layer to use. Treat this file as secret.
#pragma once

#include <Arduino.h>

namespace om {

// Up to eight, matching MAX_GROUP_CHANNELS in the mesh build.
constexpr int kMaxChannels = 8;

struct McChannel {
  char name[24];
  uint8_t secret[16];   // the channel key: stays in NVS, never on the card
  uint8_t hash;         // sha256(secret)[0] - derived, never stored independently
};

// Radio plan shared by the mesh (matches ERA-3).
struct McRadioPlan {
  float freq;
  float bw;
  uint8_t sf;
  uint8_t cr;
  int8_t txPower;
};

const McRadioPlan& mcRadioPlan();

int mcChannelCount();
const McChannel& mcChannel(int idx);

// Names, the enabled flag and the per-channel notification mute are editable and persisted in NVS
// ("mcch"). Mute is about *sound* only: a muted channel still receives, still counts unread, and still
// shows in the list - it just does not chime.
void channelsBegin();
const char* mcChannelName(int idx);
void mcChannelSetName(int idx, const char* name);
bool mcChannelEnabled(int idx);
void mcChannelSetEnabled(int idx, bool on);
bool mcChannelMuted(int idx);
void mcChannelSetMuted(int idx, bool on);

// Add by key (refuses a key that is already present and returns that slot), remove by index.
int mcChannelAdd(const char* name, const uint8_t secret[16]);
bool mcChannelRemove(int idx);
bool mcChannelIsHashtag(int idx);          // name starts with '#'
const uint8_t* mcPublicPsk();              // the well-known public-channel key

// Key handling belongs with the store, not the UI: base64 is how keys are shared between nodes, and
// the derivation rules (random for "create", sha256 of the name for a #tag) are part of the model.
int mcChannelAddFromB64(const char* name, const char* keyB64);   // -1 = bad key or table full
int mcChannelCreatePrivate(const char* name);                    // random 16-byte key
int mcChannelAddHashtag(const char* name);                       // key = sha256("#name")
void mcChannelKeyB64(int idx, char* out, size_t cap);            // the key as text, for sharing

}  // namespace om
