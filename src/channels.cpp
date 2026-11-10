// SPDX-License-Identifier: MIT
// OM Lite — the MeshCore settings and channels read from ERA-3 (SI) on 2026-11-09.
//
// The five channels below are the *seed*: written to NVS on first boot, after which the table lives in
// NVS and can be added to, renamed or removed on the device itself. Secrets stay in internal flash
// (NVS), never on the SD card — the same rule as every other credential on this board.
#include "channels.h"

#include <Preferences.h>
#include <SHA256.h>
#include <esp_system.h>

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
McChannel s_chan[kMaxChannels];
bool s_enabled[kMaxChannels];
bool s_muted[kMaxChannels];
int s_count = 0;

void writeSlot(int i) {
  char key[8];
  snprintf(key, sizeof(key), "n%d", i);
  s_prefs.putString(key, s_chan[i].name);
  snprintf(key, sizeof(key), "k%d", i);
  s_prefs.putBytes(key, s_chan[i].secret, sizeof(s_chan[i].secret));
  snprintf(key, sizeof(key), "e%d", i);
  s_prefs.putBool(key, s_enabled[i]);
  snprintf(key, sizeof(key), "m%d", i);
  s_prefs.putBool(key, s_muted[i]);
}

// The channel hash always comes from the secret, exactly as MeshCore computes it
// (BaseChatMesh::setChannel -> sha256 over the key). The *name* is a local label, so renaming cannot
// break a channel — it only changes what this device calls it.
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
    snprintf(key, sizeof(key), "m%d", i);
    s_muted[i] = s_prefs.getBool(key, false);
    s_chan[i].hash = hashFor(s_chan[i].secret);
  }
}

}  // namespace

const McRadioPlan& mcRadioPlan() { return kPlan; }

int mcChannelCount() {
  ensureBegun();
  return s_count;
}

const McChannel& mcChannel(int idx) {
  ensureBegun();
  if (idx < 0 || idx >= s_count) return kSeed[0];
  return s_chan[idx];
}

void channelsBegin() { ensureBegun(); }

const char* mcChannelName(int idx) {
  ensureBegun();
  if (idx < 0 || idx >= s_count) return "";
  return s_chan[idx].name;
}

void mcChannelSetName(int idx, const char* name) {
  ensureBegun();
  if (idx < 0 || idx >= s_count) return;
  snprintf(s_chan[idx].name, sizeof(s_chan[idx].name), "%s", name ? name : "");
  writeSlot(idx);
}

bool mcChannelEnabled(int idx) {
  ensureBegun();
  if (idx < 0 || idx >= s_count) return false;
  return s_enabled[idx];
}

void mcChannelSetEnabled(int idx, bool on) {
  ensureBegun();
  if (idx < 0 || idx >= s_count) return;
  s_enabled[idx] = on;
  writeSlot(idx);
}

bool mcChannelMuted(int idx) {
  ensureBegun();
  if (idx < 0 || idx >= s_count) return false;
  return s_muted[idx];
}

void mcChannelSetMuted(int idx, bool on) {
  ensureBegun();
  if (idx < 0 || idx >= s_count) return;
  s_muted[idx] = on;
  writeSlot(idx);
}

const uint8_t* mcPublicPsk() { return kPublicPsk; }

int mcChannelAdd(const char* name, const uint8_t secret[16]) {
  ensureBegun();
  if (s_count >= kMaxChannels || !name || !*name) return -1;
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
}

bool mcChannelRemove(int idx) {
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
}

// A tiny base64 codec for 16-byte keys, written here on purpose: the vendored base64.hpp is not
// header-only (its functions are non-inline), and MeshCore already compiles one copy - including it
// twice fails at link time. Sixteen bytes always encode to 24 characters with padding.
namespace {
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int b64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
}  // namespace

void mcChannelKeyB64(int idx, char* out, size_t cap) {
  ensureBegun();
  if (!out || cap < 25 || idx < 0 || idx >= s_count) { if (out && cap) out[0] = 0; return; }
  const uint8_t* in = s_chan[idx].secret;
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
  ensureBegun();
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
  ensureBegun();
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
  ensureBegun();
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
  ensureBegun();
  if (idx < 0 || idx >= s_count) return false;
  return s_chan[idx].name[0] == '#';
}

}  // namespace om
