// SPDX-License-Identifier: MIT
// OM Lite channels: MeshCore's channel table behind the old API (EST-356).
//
// The five seeded channels below are the *first boot* seed only. Since EST-356 the list the screens
// read is MeshCore's own table (MyMesh, loaded from /channels2 in its DataStore), because the phone
// app changes channels through that table and there must be one truth, not two. This file keeps:
//   * the legacy NVS store ("mcch") that the migration copies into MeshCore once (the seed);
//   * the two flags MeshCore does not model: per-channel enabled and notification mute, saved in NVS
//     and keyed by the channel hash, so a phone-side reorder cannot move a mute;
//   * the key helpers (base64 export, private/random and #hashtag derivation), which now feed
//     MeshCore's setChannel().
#include "channels.h"

#include <Preferences.h>
#include <SHA256.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>

#if defined(OM_ENABLE_MESH)
#include "mesh_link.h"
#endif

namespace om {
namespace {

constexpr McRadioPlan kPlan = {869.618f, 62.5f, 8, 8, 22};

// The public channel's well-known PSK, as MeshCore's own examples use it.
const uint8_t kPublicPsk[16] = {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                                0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};

const McChannel kSeed[] = {
  {"Public",      {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                   0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72}, 0x11},
  {"#slovenija",  {0x81, 0x01, 0x2d, 0xfd, 0xa1, 0x98, 0x9c, 0x27,
                   0x0f, 0xef, 0xa2, 0x9f, 0x2f, 0x66, 0x61, 0xdd}, 0xec},
  {"Don't Panic", {0x5b, 0x98, 0x07, 0xec, 0xb3, 0xe7, 0x71, 0x47,
                   0x73, 0x28, 0xed, 0x3e, 0x60, 0x7a, 0x4b, 0x97}, 0xd7},
  {"#test",       {0x9c, 0xd8, 0xfc, 0xf2, 0x2a, 0x47, 0x33, 0x3b,
                   0x59, 0x1d, 0x96, 0xa2, 0xb8, 0x48, 0xb7, 0x3f}, 0xd9},
  {"#slovenia",   {0xaa, 0x33, 0x5d, 0xf9, 0x34, 0x12, 0xb4, 0x29,
                   0xcb, 0x78, 0xac, 0x28, 0x98, 0xa0, 0xe7, 0xb0}, 0x95},
};

Preferences s_prefs;
bool s_begun = false;
McChannel s_chan[kMaxChannels];        // the LEGACY seed store, not the screens' truth
bool s_enabled[kMaxChannels];
bool s_muted[kMaxChannels];
int s_count = 0;

// The tiny base64 codec, written here on purpose: the vendored base64.hpp is not header-only (its
// functions are non-inline), and MeshCore already compiles one copy - including it twice fails at
// link time. Sixteen bytes always encode to 24 characters with padding.
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// The legacy store is written once (first boot / migration). The names and secrets it holds never
// change after that, so a slot rewrite is all it needs.
void writeSlot(int i) {
  char key[8];
  snprintf(key, sizeof(key), "n%d", i);
  s_prefs.putString(key, s_chan[i].name);
  snprintf(key, sizeof(key), "k%d", i);
  s_prefs.putBytes(key, s_chan[i].secret, sizeof(s_chan[i].secret));
  snprintf(key, sizeof(key), "e%d", i);
  s_prefs.putBool(key, s_enabled[i]);
  // Mute stays ours, keyed by the CHANNEL HASH so removing/reordering slots cannot move a mute to
  // another channel (EST-340). The old m<i> key is still written for one release, so a rollback to
  // the pre-companion firmware keeps the user's mute settings.
  snprintf(key, sizeof(key), "m%d", i);
  s_prefs.putBool(key, s_muted[i]);
  snprintf(key, sizeof(key), "m%02x", s_chan[i].hash);
  s_prefs.putBool(key, s_muted[i]);
}

// The channel hash always comes from the secret, exactly as MeshCore computes it
// (BaseChatMesh::setChannel -> sha256 over the key). The *name* is a local label, so renaming cannot
// break a channel: it only changes what this device calls it.
uint8_t hashFor(const uint8_t secret[16]) {
  SHA256 sha;
  sha.update(secret, 16);
  uint8_t full[32];
  sha.finalize(full, 32);
  return full[0];
}

void ensureBegun() {
  if (s_begun) return;
  s_begun = true;
  s_prefs.begin("mcch", false);
  const int stored = (int)s_prefs.getUChar("cnt", 0);
  if (stored == 0) {                       // first boot on this firmware: seed the SLO channels
    s_count = (int)(sizeof(kSeed) / sizeof(kSeed[0]));
    for (int i = 0; i < s_count; ++i) {
      snprintf(s_chan[i].name, sizeof(s_chan[i].name), "%s", kSeed[i].name);
      memcpy(s_chan[i].secret, kSeed[i].secret, 16);
      s_chan[i].hash = kSeed[i].hash;
      s_enabled[i] = true;
      s_muted[i] = false;
      writeSlot(i);
    }
    s_prefs.putUChar("cnt", (uint8_t)s_count);
    return;
  }
  s_count = stored > kMaxChannels ? kMaxChannels : stored;
  for (int i = 0; i < s_count; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "n%d", i);
    String name = s_prefs.getString(key, "?");
    snprintf(s_chan[i].name, sizeof(s_chan[i].name), "%s", name.c_str());
    snprintf(key, sizeof(key), "k%d", i);
    s_prefs.getBytes(key, s_chan[i].secret, 16);
    snprintf(key, sizeof(key), "e%d", i);
    s_enabled[i] = s_prefs.getBool(key, true);
    s_chan[i].hash = hashFor(s_chan[i].secret);
    char hashKey[8];
    snprintf(hashKey, sizeof(hashKey), "m%02x", s_chan[i].hash);
    snprintf(key, sizeof(key), "m%d", i);
    if (s_prefs.isKey(hashKey)) {
      s_muted[i] = s_prefs.getBool(hashKey, false);
    } else {
      s_muted[i] = s_prefs.getBool(key, false);   // old per-index key, one release
      s_prefs.putBool(hashKey, s_muted[i]);       // remember the hash-keyed value from now on
    }
  }
}

// The enabled/mute flags are ours, so they are saved by channel HASH as well as by index: an index
// means nothing after the phone adds or removes a channel. Only the flag keys are written; the
// legacy names/keys are not touched.
void saveFlags(int idx, uint8_t hash) {
  char key[8];
  snprintf(key, sizeof(key), "e%d", idx);
  s_prefs.putBool(key, s_enabled[idx]);
  snprintf(key, sizeof(key), "m%d", idx);
  s_prefs.putBool(key, s_muted[idx]);
  snprintf(key, sizeof(key), "m%02x", hash);
  s_prefs.putBool(key, s_muted[idx]);
}

// Room for a flag on a channel the legacy store never knew (added from the phone or from Settings
// after the migration). The arrays only hold our two flags for that slot.
void ensureSlotLoaded(int idx) {
  ensureBegun();
  while (idx >= 0 && idx < kMaxChannels && s_count <= idx) {
    memset(&s_chan[s_count], 0, sizeof(s_chan[s_count]));
    s_enabled[s_count] = true;
    s_muted[s_count] = false;
    ++s_count;
  }
  // Grow the stored count with the array, so the flags written for a channel the legacy store never
  // knew survive the next boot. The legacy names/keys for the new slots stay empty; they are not
  // read after the one-time migration.
  if (idx >= 0 && idx < kMaxChannels) s_prefs.putUChar("cnt", (uint8_t)s_count);
}

}  // namespace

const McRadioPlan& mcRadioPlan() { return kPlan; }

int mcLegacyChannelCount() {
  ensureBegun();
  return s_count;
}

const McChannel& mcLegacyChannel(int idx) {
  ensureBegun();
  if (idx < 0 || idx >= s_count) return kSeed[0];
  return s_chan[idx];
}

// ------------------------------------------------------------------------------------------------
// The list itself. One lookup per call, no second store: whatever MeshCore's table holds right now
// is what the screens draw, whether it was set here or by the phone a moment ago.
int mcChannelCount() {
#if defined(OM_ENABLE_MESH)
  return om::meshChannelCount();
#else
  ensureBegun();
  return s_count;
#endif
}

const McChannel& mcChannel(int idx) {
#if defined(OM_ENABLE_MESH)
  static McChannel s_view[kMaxChannels];
  if (idx < 0 || idx >= kMaxChannels) return s_view[0];
  McChannel& v = s_view[idx];
  v = McChannel{};
  uint8_t secret[16] = {0};
  om::meshChannelCopy(idx, v.name, sizeof(v.name), secret, &v.hash);
  memcpy(v.secret, secret, sizeof(v.secret));
  return v;
#else
  ensureBegun();
  if (idx < 0 || idx >= s_count) return kSeed[0];
  return s_chan[idx];
#endif
}

void channelsBegin() { ensureBegun(); }

const char* mcChannelName(int idx) {
#if defined(OM_ENABLE_MESH)
  static char s_name[kMaxChannels][sizeof(McChannel::name)];
  if (idx < 0 || idx >= kMaxChannels) return "";
  uint8_t secret[16] = {0};
  uint8_t hash = 0;
  if (!om::meshChannelCopy(idx, s_name[idx], sizeof(s_name[idx]), secret, &hash)) return "";
  return s_name[idx];
#else
  ensureBegun();
  if (idx < 0 || idx >= s_count) return "";
  return s_chan[idx].name;
#endif
}

void mcChannelSetName(int idx, const char* name) {
#if defined(OM_ENABLE_MESH)
  om::meshChannelSetName(idx, name != nullptr ? name : "");
#else
  ensureBegun();
  if (idx < 0 || idx >= s_count) return;
  snprintf(s_chan[idx].name, sizeof(s_chan[idx].name), "%s", name ? name : "");
  writeSlot(idx);
#endif
}

bool mcChannelEnabled(int idx) {
  ensureBegun();
  if (idx < 0 || idx >= kMaxChannels) return false;
  if (idx >= s_count) return true;   // a channel the phone added is not ours to have disabled
  return s_enabled[idx];
}

void mcChannelSetEnabled(int idx, bool on) {
  if (idx < 0 || idx >= kMaxChannels) return;
  ensureSlotLoaded(idx);
  s_enabled[idx] = on;
  saveFlags(idx, mcChannel(idx).hash);
}

bool mcChannelMuted(int idx) {
  ensureBegun();
  if (idx < 0 || idx >= kMaxChannels) return false;
  if (idx >= s_count) return false;
  return s_muted[idx];
}

void mcChannelSetMuted(int idx, bool on) {
  if (idx < 0 || idx >= kMaxChannels) return;
  ensureSlotLoaded(idx);
  s_muted[idx] = on;
  saveFlags(idx, mcChannel(idx).hash);
}

const uint8_t* mcPublicPsk() { return kPublicPsk; }

int mcChannelAdd(const char* name, const uint8_t secret[16]) {
  if (secret == nullptr || name == nullptr || name[0] == 0) return -1;
#if defined(OM_ENABLE_MESH)
  return om::meshChannelAdd(name, secret);
#else
  ensureBegun();
  if (s_count >= kMaxChannels) return -1;
  // The same key twice is almost certainly a mistake (or a channel already joined): return the slot
  // that already holds it instead of creating a second entry receiving the same traffic twice.
  for (int i = 0; i < s_count; ++i) {
    if (memcmp(s_chan[i].secret, secret, 16) == 0) return i;
  }
  const int slot = s_count;
  snprintf(s_chan[slot].name, sizeof(s_chan[slot].name), "%s", name);
  memcpy(s_chan[slot].secret, secret, 16);
  s_chan[slot].hash = hashFor(secret);
  s_enabled[slot] = true;
  s_muted[slot] = false;
  ++s_count;
  s_prefs.putUChar("cnt", (uint8_t)s_count);
  writeSlot(slot);
  return slot;
#endif
}

bool mcChannelRemove(int idx) {
#if defined(OM_ENABLE_MESH)
  return om::meshChannelRemove(idx);
#else
  ensureBegun();
  if (idx < 0 || idx >= s_count) return false;
  for (int i = idx; i + 1 < s_count; ++i) {   // shift down, then rewrite every slot
    s_chan[i] = s_chan[i + 1];
    s_enabled[i] = s_enabled[i + 1];
    s_muted[i] = s_muted[i + 1];
  }
  --s_count;
  s_prefs.putUChar("cnt", (uint8_t)s_count);
  for (int i = 0; i < s_count; ++i) writeSlot(i);
  return true;
#endif
}

void mcChannelKeyB64(int idx, char* out, size_t cap) {
  if (!out || cap < 25) { if (out && cap) out[0] = 0; return; }
  if (idx < 0 || idx >= mcChannelCount()) { out[0] = 0; return; }
  const McChannel& ch = mcChannel(idx);
  const uint8_t* in = ch.secret;
  int o = 0;
  for (int i = 0; i < 15; i += 3) {   // 5 whole groups = 15 bytes, then the last byte with padding
    const uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
    out[o++] = kB64[(v >> 18) & 63];
    out[o++] = kB64[(v >> 12) & 63];
    out[o++] = kB64[(v >> 6) & 63];
    out[o++] = kB64[v & 63];
  }
  const uint32_t v = ((uint32_t)in[15] << 16);
  out[o++] = kB64[(v >> 18) & 63];
  out[o++] = kB64[(v >> 12) & 63];
  out[o++] = '=';
  out[o++] = '=';
  out[o] = 0;
}

int mcChannelAddFromB64(const char* name, const char* keyB64) {
  if (!keyB64) return -1;
  uint8_t secret[16];
  memset(secret, 0, sizeof(secret));
  int bits = 0, total = 0, o = 0;
  for (const char* p = keyB64; *p; ++p) {
    if (*p == '=') break;
    const int v = b64Value(*p);
    if (v < 0) { if (*p == ' ' || *p == '\t') continue; return -1; }
    bits = (bits << 6) | v;
    total += 6;
    if (total >= 8) {
      total -= 8;
      if (o < 16) secret[o++] = (uint8_t)((bits >> total) & 0xFF);
    }
  }
  if (o != 16) return -1;             // only 16-byte (128-bit) keys are supported here
  return mcChannelAdd(name, secret);
}

int mcChannelCreatePrivate(const char* name) {
  uint8_t secret[16];
  for (int i = 0; i < 16; i += 4) {           // esp_random() is a hardware RNG
    const uint32_t r = esp_random();
    secret[i] = (uint8_t)(r & 0xFF);
    secret[i + 1] = (uint8_t)((r >> 8) & 0xFF);
    secret[i + 2] = (uint8_t)((r >> 16) & 0xFF);
    secret[i + 3] = (uint8_t)((r >> 24) & 0xFF);
  }
  return mcChannelAdd(name && *name ? name : "Private", secret);
}

int mcChannelAddHashtag(const char* name) {
  if (!name || !*name) return -1;
  char tag[32];
  int n = 0;
  tag[n++] = '#';
  for (const char* p = name; *p && n < (int)sizeof(tag) - 1; ++p) {
    char c = *p;
    if (c == '#' || c == ' ') continue;             // normalise: one '#', no spaces
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);   // MeshCore derives the key from the lowercased tag
    tag[n++] = c;
  }
  tag[n] = 0;
  if (n < 2) return -1;
  uint8_t secret[16];
  SHA256 sha;
  sha.update((const uint8_t*)tag, n);
  sha.finalize(secret, 16);
  return mcChannelAdd(tag, secret);
}

bool mcChannelIsHashtag(int idx) {
  const char* name = mcChannelName(idx);
  return name != nullptr && name[0] == '#';
}

}  // namespace om
