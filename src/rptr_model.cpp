// SPDX-License-Identifier: MIT
// EST-358: the RPTR model's arithmetic and text. See rptr_model.h for the spec and the sources.
#include "rptr_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace om {
namespace rptr {
namespace {

bool samePrefix(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }

int hex2(const char* p, uint8_t* out) {
  if (p == nullptr || out == nullptr) return 0;
  int n = 0;
  for (int i = 0; i < 2; ++i) {
    const char c = p[i];
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else return 0;
    n = n * 16 + v;
  }
  *out = (uint8_t)n;
  return 1;
}

}  // namespace

// ------------------------------------------------------------------------------------------- password
bool passwordChar(char c) {
  const unsigned char u = (unsigned char)c;
  // Printable ASCII. Space is allowed: MeshCore's password is a strcmp'd C string and a space is a
  // character it can hold; the sheet's space key is therefore useful here.
  return u >= 0x20 && u < 0x7F;
}

int passwordFilter(const char* src, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = 0;
  if (src == nullptr) return 0;
  int n = 0;
  for (const char* p = src; *p && n < kPasswordMax && (size_t)n + 1 < cap; ++p) {
    if (!passwordChar(*p)) continue;
    out[n++] = *p;
  }
  out[n] = 0;
  return n;
}

// ------------------------------------------------------------------------------------------ regions
// The character rule itself is constexpr in the header (so the keyboard and every host test share
// it); these are the string forms built on it.
bool regionNameValid(const char* name) {
  if (name == nullptr || name[0] == 0) return false;
  int n = 0;
  for (const char* p = name; *p; ++p, ++n) {
    if (n >= kRegionNameMax) return false;
    if (!regionNameChar(*p)) return false;
  }
  return n > 0;
}

int regionNameFilter(const char* src, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = 0;
  if (src == nullptr) return 0;
  int n = 0;
  for (const char* p = src; *p && n < kRegionNameMax && (size_t)n + 1 < cap; ++p) {
    if (!regionNameChar(*p)) continue;
    out[n++] = *p;
  }
  out[n] = 0;
  return n;
}

// ------------------------------------------------------------------------------------------- found
void FoundList::begin(uint32_t tagSeed, uint32_t nowMs) {
  count = 0;
  listening = true;
  startedMs = nowMs;
  tag = tagSeed;
  ++serial;
}

int FoundList::indexOf(const uint8_t* prefix6) const {
  if (prefix6 == nullptr) return -1;
  for (int i = 0; i < count; ++i) {
    if (samePrefix(rows[i].prefix, prefix6)) return i;
  }
  return -1;
}

int FoundList::addAnswer(const uint8_t* key, int keyLen, const char* name, bool inContacts,
                         bool hasIn, int8_t in4, bool hasOut, int8_t out4, uint8_t hops,
                         uint32_t nowMs) {
  if (key == nullptr || keyLen <= 0) return -1;
  int at = indexOf(key);   // compares the first six bytes, whatever the key length
  if (at < 0) {
    if (count >= kMaxFound) return -1;
    at = count++;
    rows[at] = Found();
    memcpy(rows[at].prefix, key, 6);
  }
  Found& f = rows[at];
  // The name and the contact flag are ours (the answer carries only the key); a later answer for the
  // same key must not clear what we already know.
  if (name != nullptr && name[0]) {
    snprintf(f.name, sizeof(f.name), "%s", name);
    f.inContacts = inContacts;
  }
  // Keep the whole key when the answer carried one: the anonymous regions request needs it for the
  // shared secret, and a prefix-only answer (6 or 8 bytes) cannot be asked.
  if (keyLen >= kPubKeyBytes) {
    memcpy(f.key, key, kPubKeyBytes);
    f.keyLen = (uint8_t)kPubKeyBytes;
  } else if (keyLen > (int)f.keyLen) {
    memcpy(f.key, key, (size_t)keyLen);
    f.keyLen = (uint8_t)keyLen;
  }
  f.hasSnrIn = hasIn;
  if (hasIn) f.snrIn4 = in4;
  f.hasSnrOut = hasOut;
  if (hasOut) f.snrOut4 = out4;
  f.hops = hops;
  f.heardMs = nowMs;
  ++serial;
  return at;
}

bool FoundList::setRegions(int index, const AnonRegions& a) {
  if (index < 0 || index >= count) return false;
  rows[index].anon = a;
  rows[index].anon.valid = true;   // only a parsed answer is ever stored here
  rows[index].regionsAsked = true;
  ++serial;
  return true;
}

void FoundList::tick(uint32_t nowMs) {
  if (listening && (uint32_t)(nowMs - startedMs) >= kScanWindowMs) {
    listening = false;
    ++serial;
  }
}

void FoundList::stateText(char* out, size_t cap) const {
  if (out == nullptr || cap == 0) return;
  if (listening) {
    if (count == 0) snprintf(out, cap, "listening");
    else snprintf(out, cap, "%d found, listening", count);
  } else if (count == 0) {
    snprintf(out, cap, "none answered");
  } else {
    snprintf(out, cap, "%d found", count);
  }
}

// ------------------------------------------------------------------------------------------- login
bool loginParse(const uint8_t* buf, int len, LoginAnswer* out) {
  if (buf == nullptr || out == nullptr || len <= 0) return false;
  *out = LoginAnswer();
  // A legacy repeater answers "OK" (v1 and earlier); MyMesh itself maps that to is_admin = false,
  // but only an admin could log in to those repeaters at all, so admin is the honest reading.
  if (len >= 2 && buf[0] == 'O' && buf[1] == 'K') {
    out->ok = true;
    out->legacy = true;
    out->admin = true;
    return true;
  }
  // Current MeshCore: [0] = 0 (RESP_SERVER_LOGIN_OK), [1] keep-alive/16, [2] is_admin,
  // [3] permissions, [8] FIRMWARE_VER_LEVEL. The repeater's clock is the answer's first four bytes,
  // which arrive here as the tag the caller passes separately; a login answer's tag IS its clock
  // because handleLoginReq prefixes reply_data with getCurrentTimeUnique(). rptr_link stores it.
  if (buf[0] == 0 && len >= 9) {
    out->ok = true;
    out->admin = (buf[2] & 1) != 0;
    out->permissions = buf[3];
    out->fwLevel = buf[8];
    return true;
  }
  // A reply that is neither shape: the repeater said something, and it was not a login.
  out->ok = false;
  return true;
}

const char* loginStateText(LoginState s) {
  switch (s) {
    case LoginState::Idle: return "not logged in";
    case LoginState::Sending: return "sending";
    case LoginState::Admin: return "admin";
    case LoginState::Guest: return "guest";
    case LoginState::Refused: return "refused";
    case LoginState::NoAnswer: return "no answer";
    default: return "";
  }
}

void LoginMachine::begin(uint32_t nowMs, uint32_t estTimeoutMs) {
  state = LoginState::Sending;
  sentMs = nowMs;
  deadlineMs = nowMs + estTimeoutMs + kLoginExtraMs;
  rttMs = 0;
  haveRtt = false;
  answer = LoginAnswer();
}

void LoginMachine::succeed(const LoginAnswer& a, uint32_t nowMs) {
  answer = a;
  if (!a.ok) {
    state = LoginState::Refused;
  } else {
    state = a.admin ? LoginState::Admin : LoginState::Guest;
  }
  rttMs = (uint32_t)(nowMs - sentMs);
  haveRtt = true;
}

void LoginMachine::refuse() {
  state = LoginState::Refused;
  haveRtt = false;
}

bool LoginMachine::tick(uint32_t nowMs) {
  if (state != LoginState::Sending) return false;
  // A signed difference: millis() wraps after 49 days and the deadline must still work.
  if ((int32_t)(nowMs - deadlineMs) < 0) return false;
  state = LoginState::NoAnswer;
  return true;
}

void LoginMachine::reset() {
  state = LoginState::Idle;
  sentMs = deadlineMs = rttMs = 0;
  haveRtt = false;
  answer = LoginAnswer();
}

// ------------------------------------------------------------------------------------------- status
bool statusParse(const uint8_t* buf, int len, Status* out) {
  if (buf == nullptr || out == nullptr) return false;
  // MyMesh::onContactResponse already stripped the 4-byte tag: `buf` starts at batt_milli_volts.
  // EST-360: the old code skipped 4 more, so every field read the one four bytes after it (battery
  // was the noise floor). A newer firmware's longer RepeaterStats is accepted; we read the known head.
  if (len < kRepeaterStatsBytes) return false;
  const uint8_t* p = buf;
  Status s;
  auto u16 = [&](int off) {
    uint16_t v;
    memcpy(&v, p + off, sizeof(v));
    return v;
  };
  auto i16 = [&](int off) {
    int16_t v;
    memcpy(&v, p + off, sizeof(v));
    return v;
  };
  auto u32 = [&](int off) {
    uint32_t v;
    memcpy(&v, p + off, sizeof(v));
    return v;
  };
  s.battMv = u16(0);
  s.txQueue = u16(2);
  s.noiseFloor = i16(4);
  s.lastRssi = i16(6);
  s.rxPackets = u32(8);
  s.txPackets = u32(12);
  s.airtimeSec = u32(16);
  s.uptimeSec = u32(20);
  s.errEvents = u16(40);
  s.lastSnr4 = i16(42);
  s.rxAirtimeSec = u32(48);
  s.rxCrcErrors = u32(52);
  s.valid = true;
  *out = s;
  return true;
}

// ------------------------------------------------------------------------------------- anon regions
bool anonRegionsParse(const uint8_t* buf, int len, AnonRegions* out) {
  if (buf == nullptr || out == nullptr) return false;
  *out = AnonRegions();
  // Clock first, little-endian (the repeater copies its uint32_t straight into the packet).
  if (len < 4) return false;
  memcpy(&out->clock, buf, 4);
  out->clockValid = out->clock >= kClockRealSince;
  const int bodyLen = len - 4;
  if (bodyLen <= 0) {
    out->valid = true;   // a real answer with no names (the repeater has an empty region map)
    return true;
  }
  // The length is the whole list; newer/older notes differ on a NUL terminator, so both work.
  int end = bodyLen;
  for (int i = 0; i < bodyLen; ++i) {
    if (buf[4 + i] == 0) {
      end = i;
      break;
    }
  }
  char joined[sizeof(out->text)] = "";
  size_t used = 0;
  int count = 0;
  int at = 0;
  while (at < end) {
    int comma = at;
    while (comma < end && buf[4 + comma] != ',') ++comma;
    int tokenLen = comma - at;
    // Trim spaces around a token; a name itself can never contain one.
    while (tokenLen > 0 && (buf[4 + at] == ' ' || buf[4 + at] == '\t')) {
      ++at;
      --tokenLen;
    }
    while (tokenLen > 0 && (buf[4 + at + tokenLen - 1] == ' ' ||
                            buf[4 + at + tokenLen - 1] == '\t')) {
      --tokenLen;
    }
    if (tokenLen > 0) {
      char name[kRegionNameMax + 2] = "";
      const bool star = tokenLen == 1 && buf[4 + at] == '*';
      if (star || tokenLen <= kRegionNameMax) {
        // A star or a legal name. Anything else (header residue, a mangled byte) is dropped and the
        // answer is marked not clean, so the caller can log it without showing it as a region.
        bool ok = star;
        if (!ok) {
          ok = true;
          for (int i = 0; i < tokenLen; ++i) {
            if (!regionNameChar((char)buf[4 + at + i])) {
              ok = false;
              break;
            }
          }
        }
        if (ok && count < kMaxRegions) {
          snprintf(name, sizeof(name), "%.*s", tokenLen, (const char*)&buf[4 + at]);
          const int w = snprintf(joined + used, sizeof(joined) - used, "%s%s", count ? ", " : "",
                                 name);
          if (w > 0 && used + (size_t)w < sizeof(joined)) {
            used += (size_t)w;
            ++count;
          } else {
            out->clean = false;   // the display buffer is full; the list was longer
          }
        } else if (ok) {
          out->clean = false;     // more names than the card keeps
        }
        if (!ok) out->clean = false;
      } else {
        out->clean = false;
      }
    }
    at = comma + 1;
  }
  out->count = count;
  snprintf(out->text, sizeof(out->text), "%s", joined);
  out->valid = true;
  return true;
}

// --------------------------------------------------------------------------------------- neighbours
int neighboursParse(const char* text, NeighbourList* out) {
  if (out == nullptr) return 0;
  out->count = 0;
  out->valid = true;
  if (text == nullptr || text[0] == 0) return 0;
  if (strcmp(text, "-none-") == 0) return 0;   // a real answer: the repeater knows nobody
  const char* line = text;
  while (*line && out->count < kMaxNeighbours) {
    const char* end = strchr(line, '\n');
    const int lineLen = end ? (int)(end - line) : (int)strlen(line);
    // "hhhhhhhh:secs:snr4"
    if (lineLen >= 11) {
      char hex[9] = {0};
      memcpy(hex, line, 8);
      uint8_t prefix[4];
      bool ok = true;
      for (int i = 0; i < 4; ++i) {
        if (!hex2(&hex[i * 2], &prefix[i])) ok = false;
      }
      if (ok && line[8] == ':') {
        long secs = 0;
        int sign = 1;
        const char* q = &line[9];
        if (*q == '-') {
          sign = -1;
          ++q;
        }
        while (*q >= '0' && *q <= '9') secs = secs * 10 + (*q++ - '0');
        if (*q == ':') {
          ++q;
          int snr = 0;
          int snrSign = 1;
          if (*q == '-') {
            snrSign = -1;
            ++q;
          }
          while (*q >= '0' && *q <= '9') snr = snr * 10 + (*q++ - '0');
          Neighbour& n = out->rows[out->count++];
          memcpy(n.prefix, prefix, 4);
          n.secsAgo = (int32_t)(sign * secs);
          n.snr4 = (int8_t)(snrSign * snr);
          n.named = false;
          n.name[0] = 0;
        }
      }
    }
    if (end == nullptr) break;
    line = end + 1;
  }
  return out->count;
}

// ------------------------------------------------------------------------------------------ regions
int regionsParse(const char* text, RegionList* out) {
  if (out == nullptr) return 0;
  out->count = 0;
  out->valid = true;
  if (text == nullptr || text[0] == 0) return 0;
  const char* line = text;
  while (*line && out->count < kMaxRegions) {
    const char* end = strchr(line, '\n');
    const int lineLen = end ? (int)(end - line) : (int)strlen(line);
    // Indentation is depth; the first line is the wildcard region (RegionMap's tree root).
    int at = 0;
    while (at < lineLen && line[at] == ' ') ++at;
    if (at < lineLen) {
      Region& r = out->rows[out->count];
      r = Region();
      int n = 0;
      while (at < lineLen && line[at] != ' ' && line[at] != '^' && n < (int)sizeof(r.name) - 1) {
        r.name[n++] = line[at++];
      }
      r.name[n] = 0;
      // " F" at the end means flood is allowed; "^" marks the home region.
      for (int i = at; i < lineLen; ++i) {
        if (line[i] == '^') r.home = true;
      }
      r.allow = (lineLen >= 2) && line[lineLen - 1] == 'F' && line[lineLen - 2] == ' ';
      r.wildcard = (out->count == 0);
      if (r.name[0]) ++out->count;
    }
    if (end == nullptr) break;
    line = end + 1;
  }
  return out->count;
}

// ------------------------------------------------------------------------------------- admin values
const uint32_t kBandwidthLadder[kBandwidthCount] = {7800,  10400, 15600, 20800, 31250,
                                                    41700, 62500, 125000, 250000, 500000};
const uint8_t kAdvertLocalLadder[kAdvertLocalCount] = {0, 60, 90, 120, 150, 180, 210, 240};

void radioStep(RadioValues* v, RadioKey key, int dir) {
  if (v == nullptr || dir == 0) return;
  switch (key) {
    case RadioKey::Freq: {
      int32_t f = (int32_t)v->freq_khz + dir * (int32_t)kRadioFreqStep;
      if (f < (int32_t)kRadioFreqMin) f = kRadioFreqMin;
      if (f > (int32_t)kRadioFreqMax) f = kRadioFreqMax;
      v->freq_khz = (uint32_t)f;
      break;
    }
    case RadioKey::Bw: {
      int i = -1;
      for (int k = 0; k < kBandwidthCount; ++k) {
        if (kBandwidthLadder[k] == v->bw_hz) {
          i = k;
          break;
        }
      }
      if (i < 0) i = 6;   // an unknown rate snaps to 62.5 kHz, like the mock's ladder default
      i += dir;
      if (i < 0) i = 0;
      if (i >= kBandwidthCount) i = kBandwidthCount - 1;
      v->bw_hz = kBandwidthLadder[i];
      break;
    }
    case RadioKey::Sf: {
      int s = (int)v->sf + dir;
      if (s < 5) s = 5;
      if (s > 12) s = 12;
      v->sf = (uint8_t)s;
      break;
    }
    case RadioKey::Cr: {
      int c = (int)v->cr + dir;
      if (c < 5) c = 5;
      if (c > 8) c = 8;
      v->cr = (uint8_t)c;
      break;
    }
    case RadioKey::Tx: {
      int t = (int)v->tx + dir;
      if (t < 1) t = 1;
      if (t > 22) t = 22;
      v->tx = (int8_t)t;
      break;
    }
    default: break;
  }
}

void AdminValues::reset() {
  radio = RadioValues();
  advertLocal = kAdvertLocalDefault;
  advertFlood = kAdvertFloodDefault;
  floodMax = kFloodMaxDefault;
  loop = LoopDetect::Moderate;
  pathHash = 1;
  repeat = true;
  powerSave = false;
  sharePosition = true;
}

void advertLocalStep(AdminValues* v, int dir) {
  if (v == nullptr || dir == 0) return;
  int i = -1;
  for (int k = 0; k < kAdvertLocalCount; ++k) {
    if (kAdvertLocalLadder[k] == v->advertLocal) {
      i = k;
      break;
    }
  }
  if (i < 0) i = 3;   // the mock's 120 min default
  i += dir;
  if (i < 0) i = 0;
  if (i >= kAdvertLocalCount) i = kAdvertLocalCount - 1;
  v->advertLocal = kAdvertLocalLadder[i];
}

void advertFloodStep(AdminValues* v, int dir) {
  if (v == nullptr || dir == 0) return;
  // MeshCore refuses 1 and 2 hours (3-168 or 0), so the ladder hops over them.
  if (dir < 0) {
    if (v->advertFlood == 0) return;
    if (v->advertFlood <= 3) {
      v->advertFlood = 0;
    } else {
      v->advertFlood = (uint8_t)(v->advertFlood - 1);
    }
  } else {
    if (v->advertFlood == 0) v->advertFlood = 3;
    else if (v->advertFlood < 168) ++v->advertFlood;
  }
}

void floodMaxStep(AdminValues* v, int dir) {
  if (v == nullptr || dir == 0) return;
  int h = (int)v->floodMax + dir * (int)kFloodMaxStep;
  if (h < 0) h = 0;
  if (h > 64) h = 64;
  v->floodMax = (uint8_t)h;
}

const char* loopDetectText(LoopDetect v) {
  switch (v) {
    case LoopDetect::Off: return "off";
    case LoopDetect::Minimal: return "minimal";
    case LoopDetect::Moderate: return "moderate";
    case LoopDetect::Strict: return "strict";
    default: return "moderate";
  }
}

const char* pathHashText(uint8_t v) { return v == 0 ? "1B" : (v == 2 ? "3B" : "2B"); }
const char* pathHashCli(uint8_t v) { return v == 0 ? "0" : (v == 2 ? "2" : "1"); }

// -------------------------------------------------------------------------------------- CLI builders
int freqText(char* out, size_t cap, uint32_t khz) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "%u.%03u", (unsigned)(khz / 1000), (unsigned)(khz % 1000));
  return n > 0 ? n : 0;
}

int bwText(char* out, size_t cap, uint32_t hz) {
  if (out == nullptr || cap == 0) return 0;
  char buf[16];
  snprintf(buf, sizeof(buf), "%.2f", (double)hz / 1000.0);
  // Drop trailing zeros, then a trailing dot: 125.00 -> "125", 41.70 -> "41.7", 31.25 -> "31.25".
  size_t n = strlen(buf);
  while (n > 0 && buf[n - 1] == '0') buf[--n] = 0;
  if (n > 0 && buf[n - 1] == '.') buf[--n] = 0;
  snprintf(out, cap, "%s", buf);
  return (int)strlen(out);
}

int snrText(char* out, size_t cap, int8_t snr4) {
  if (out == nullptr || cap == 0) return 0;
  // x4 -> one decimal, keeping the sign. snr4 / 4 and the remainder are done in integers so no
  // float formatting of a negative number ever prints "-0.0".
  const int v = snr4;
  const int whole = v / 4;
  int frac = (v % 4) * 25;   // .00 .25 .50 .75
  if (frac < 0) frac = -frac;
  const int n = snprintf(out, cap, "%d.%02d dB", whole, frac);
  return n > 0 ? n : 0;
}

int snrBothText(char* out, size_t cap, bool hasIn, int8_t in4, bool hasOut, int8_t out4) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = 0;
  char a[12] = "";
  char b[12] = "";
  if (hasIn) snrText(a, sizeof(a), in4);
  if (hasOut) snrText(b, sizeof(b), out4);
  if (hasIn && hasOut) {
    // "SNR 6.2/7.5 dB": the repeater's hearing first, then ours (the "both ways" the ticket asks).
    // The unit is added once, so it is stripped from the two halves first (16 bytes each: "-123.75 dB"
    // plus the terminator must fit).
    char av[16] = "";
    char bv[16] = "";
    snprintf(av, sizeof(av), "%s", a);
    snprintf(bv, sizeof(bv), "%s", b);
    char* sp = strstr(av, " dB");
    if (sp) *sp = 0;
    sp = strstr(bv, " dB");
    if (sp) *sp = 0;
    snprintf(out, cap, "SNR %s/%s dB", av, bv);
  } else if (hasIn) {
    snprintf(out, cap, "SNR %s", a);
  } else if (hasOut) {
    snprintf(out, cap, "SNR %s", b);
  }
  return (int)strlen(out);
}

int uptimeText(char* out, size_t cap, uint32_t seconds) {
  if (out == nullptr || cap == 0) return 0;
  const uint32_t d = seconds / 86400u;
  const uint32_t h = (seconds / 3600u) % 24u;
  const uint32_t m = (seconds / 60u) % 60u;
  const uint32_t s = seconds % 60u;
  int n;
  if (d > 0) n = snprintf(out, cap, "%ud %02u:%02u", (unsigned)d, (unsigned)h, (unsigned)m);
  else if (h > 0) n = snprintf(out, cap, "%uh %02um", (unsigned)h, (unsigned)m);
  else n = snprintf(out, cap, "%um %02us", (unsigned)m, (unsigned)s);
  return n > 0 ? n : 0;
}

int batteryPercent(uint16_t milliVolts) {
  // A single Li-ion cell: 3.0 V empty, 4.2 V full, linear (the same rough reading the status bar
  // uses; the repeater's own percentage is not in the status struct).
  if (milliVolts < 3000 || milliVolts > 4300) return -1;
  int pct = (int)(((int)milliVolts - 3000) * 100 / 1200);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

int cliRadio(char* out, size_t cap, const RadioValues& v) {
  if (out == nullptr || cap == 0) return 0;
  char f[16], bw[16];
  freqText(f, sizeof(f), v.freq_khz);
  bwText(bw, sizeof(bw), v.bw_hz);
  const int n = snprintf(out, cap, "set radio %s,%s,%u,%u", f, bw, (unsigned)v.sf, (unsigned)v.cr);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliTx(char* out, size_t cap, const RadioValues& v) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "set tx %d", (int)v.tx);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliRadioApply(char* out, size_t cap, const RadioValues& v) {
  if (out == nullptr || cap == 0) return 0;
  char a[32], b[16];
  cliRadio(a, sizeof(a), v);
  cliTx(b, sizeof(b), v);
  const int n = snprintf(out, cap, "%s \xC2\xB7 %s", a, b);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliName(char* out, size_t cap, const char* name) {
  if (out == nullptr || cap == 0) return 0;
  if (name == nullptr || name[0] == 0) {
    out[0] = 0;
    return 0;
  }
  const int n = snprintf(out, cap, "set name %s", name);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliOwner(char* out, size_t cap, const char* owner) {
  if (out == nullptr || cap == 0) return 0;
  if (owner == nullptr || owner[0] == 0) {
    out[0] = 0;
    return 0;
  }
  // A trailing space would set an empty owner on the repeater, so an empty value is refused by the
  // builder (the mock's "owner cannot be empty").
  const int n = snprintf(out, cap, "set owner.info %s", owner);
  return n > 0 && (size_t)n < cap ? n : 0;
}

// Micro-degrees -> "46.25000". Rounded to the nearest 1e-5 without floating point.
int degreesText(char* out, size_t cap, int32_t e6) {
  if (out == nullptr || cap == 0) return 0;
  const bool neg = e6 < 0;
  int64_t v = neg ? -(int64_t)e6 : (int64_t)e6;
  const int64_t hundredThousandths = (v + 5) / 10;   // e6 -> 1e-5 units, rounded
  const int64_t whole = hundredThousandths / 100000;
  const int64_t frac = hundredThousandths % 100000;
  const int n = snprintf(out, cap, "%s%lld.%05lld", neg ? "-" : "", (long long)whole,
                         (long long)frac);
  return n > 0 ? n : 0;
}

int cliLat(char* out, size_t cap, int32_t latE6) {
  if (out == nullptr || cap == 0) return 0;
  char d[20];
  degreesText(d, sizeof(d), latE6);
  const int n = snprintf(out, cap, "set lat %s", d);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliLon(char* out, size_t cap, int32_t lonE6) {
  if (out == nullptr || cap == 0) return 0;
  char d[20];
  degreesText(d, sizeof(d), lonE6);
  const int n = snprintf(out, cap, "set lon %s", d);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliRegionFlood(char* out, size_t cap, const char* name, bool allow) {
  if (out == nullptr || cap == 0) return 0;
  if (name == nullptr || name[0] == 0) {
    out[0] = 0;
    return 0;
  }
  const int n = snprintf(out, cap, "region %s %s", allow ? "allowf" : "denyf", name);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliRegionHome(char* out, size_t cap, const char* name) {
  if (out == nullptr || cap == 0) return 0;
  if (name == nullptr || name[0] == 0) {
    out[0] = 0;
    return 0;
  }
  const int n = snprintf(out, cap, "region home %s", name);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliRegionDefault(char* out, size_t cap, const char* name) {
  if (out == nullptr || cap == 0) return 0;
  if (name == nullptr || name[0] == 0) {
    out[0] = 0;
    return 0;
  }
  const int n = snprintf(out, cap, "region default %s", name);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliRegionPut(char* out, size_t cap, const char* name, const char* parent) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = 0;
  if (!regionNameValid(name)) return 0;
  int n;
  if (parent != nullptr && parent[0]) {
    // A parent must be a legal name too; "*" is the wildcard and needs no parent word.
    if (!regionNameValid(parent) && strcmp(parent, "*") != 0) return 0;
    n = snprintf(out, cap, "region put %s %s", name, parent);
  } else {
    n = snprintf(out, cap, "region put %s", name);
  }
  if (n <= 0 || (size_t)n >= cap) {
    out[0] = 0;
    return 0;
  }
  return n;
}

int cliRegionRemove(char* out, size_t cap, const char* name) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = 0;
  if (name == nullptr || name[0] == 0) return 0;
  const int n = snprintf(out, cap, "region remove %s", name);
  if (n <= 0 || (size_t)n >= cap) {
    out[0] = 0;
    return 0;
  }
  return n;
}

int cliRegionHomeNone(char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  // CommonCLI: `region home *` -> RegionMap::findByNamePrefix("*") is the wildcard, whose id is 0,
  // so setHomeRegion() stores home_id = 0 and `region home` then answers "*". This IS the clean
  // "no home" (home = the unscoped wildcard), checked in CommonCLI.cpp and RegionMap.cpp.
  const int n = snprintf(out, cap, "region home *");
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliRegionDefaultNone(char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  // CommonCLI accepts the literal <null> and clears the default scope in one atomic save.
  const int n = snprintf(out, cap, "region default <null>");
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliTime(char* out, size_t cap, uint32_t epoch) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "time %lu", (unsigned long)epoch);
  return n > 0 && (size_t)n < cap ? n : 0;
}

// ----------------------------------------------------------------------------------- reply parsing
bool regionHomeReplyParse(const char* text, char* name, size_t cap) {
  if (name == nullptr || cap == 0) return false;
  name[0] = 0;
  if (text == nullptr) return false;
  const char* p = strstr(text, "home is ");
  if (p == nullptr) return false;
  p += 8;
  while (*p == ' ') ++p;
  size_t n = strlen(p);
  while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == '\n' || p[n - 1] == ' ')) --n;
  if (n == 0) return false;
  if (n == 1 && p[0] == '*') {
    snprintf(name, cap, "*");
    return true;
  }
  if (n > (size_t)kRegionNameMax) return false;
  for (size_t i = 0; i < n; ++i) {
    if (!regionNameChar(p[i])) return false;
  }
  snprintf(name, cap, "%.*s", (int)n, p);
  return true;
}

bool regionDefaultReplyParse(const char* text, char* name, size_t cap) {
  if (name == nullptr || cap == 0) return false;
  name[0] = 0;
  if (text == nullptr) return false;
  const char* p = strstr(text, "default scope is ");
  if (p == nullptr) return false;
  p += 17;
  while (*p == ' ') ++p;
  size_t n = strlen(p);
  while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == '\n' || p[n - 1] == ' ')) --n;
  if (n == 0) return false;
  if (n == 6 && memcmp(p, "<null>", 6) == 0) {
    name[0] = 0;   // a real answer: no default scope
    return true;
  }
  if (n == 1 && p[0] == '*') {
    snprintf(name, cap, "*");
    return true;
  }
  if (n > (size_t)kRegionNameMax) return false;
  for (size_t i = 0; i < n; ++i) {
    if (!regionNameChar(p[i])) return false;
  }
  snprintf(name, cap, "%.*s", (int)n, p);
  return true;
}

// ---------------------------------------------------------------------------------------- clock sync
int clockAgoText(char* out, size_t cap, int32_t deltaSecs) {
  if (out == nullptr || cap == 0) return 0;
  int32_t d = deltaSecs;
  if (d < 0) d = -d;
  if (d < 2) {
    snprintf(out, cap, "in sync");
    return (int)strlen(out);
  }
  char span[20];
  const uint32_t secs = (uint32_t)d;
  if (secs >= 86400u) {
    snprintf(span, sizeof(span), "%lud %02luh", (unsigned long)(secs / 86400u),
             (unsigned long)((secs / 3600u) % 24u));
  } else if (secs >= 3600u) {
    snprintf(span, sizeof(span), "%luh %02lum", (unsigned long)(secs / 3600u),
             (unsigned long)((secs / 60u) % 60u));
  } else if (secs >= 60u) {
    snprintf(span, sizeof(span), "%lum %02lus", (unsigned long)(secs / 60u),
             (unsigned long)(secs % 60u));
  } else {
    snprintf(span, sizeof(span), "%lus", (unsigned long)secs);
  }
  snprintf(out, cap, "%s %s", span, deltaSecs > 0 ? "behind" : "ahead");
  return (int)strlen(out);
}

namespace {
// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm), UTC only.
int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2 ? 1 : 0;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

// The UTC year of an epoch. Used only for the "not set (2024)" label, so a small walk after a mean
// year estimate is enough (and exact for every value the repeaters send).
int utcYear(uint32_t epoch) {
  int year = 1970 + (int)(epoch / 31556952u);
  while (daysFromCivil(year + 1, 1, 1) * 86400 <= (int64_t)epoch) ++year;
  while (daysFromCivil(year, 1, 1) * 86400 > (int64_t)epoch) --year;
  return year;
}
}  // namespace

bool clockReplyParse(const char* text, uint32_t* epoch) {
  if (text == nullptr || epoch == nullptr) return false;
  // CommonCLI's `clock`: "%02d:%02d - %d/%d/%d UTC" (UTC, so no zone work).
  int hh = 0, mm = 0, dd = 0, mo = 0, yy = 0;
  char tail[8] = "";
  if (sscanf(text, "%d:%d - %d/%d/%d %7s", &hh, &mm, &dd, &mo, &yy, tail) != 6) return false;
  if (strcmp(tail, "UTC") != 0) return false;
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || dd < 1 || dd > 31 || mo < 1 || mo > 12 ||
      yy < 2020 || yy > 2100) {
    return false;
  }
  const int64_t days = daysFromCivil(yy, mo, dd);
  *epoch = (uint32_t)(days * 86400 + hh * 3600 + mm * 60);
  return true;
}

// ----------------------------------------------------------------------------------------- battery
bool batteryPlausible(uint16_t milliVolts) {
  return milliVolts >= kBatteryMinMv && milliVolts <= kBatteryMaxMv;
}

int batteryVoltsText(char* out, size_t cap, uint16_t milliVolts) {
  if (out == nullptr || cap == 0) return 0;
  if (!batteryPlausible(milliVolts)) {
    snprintf(out, cap, "n/a");
    return (int)strlen(out);
  }
  const int n = snprintf(out, cap, "%u.%02u V", (unsigned)(milliVolts / 1000),
                         (unsigned)((milliVolts % 1000) / 10));
  return n > 0 ? n : 0;
}

int batteryRawText(char* out, size_t cap, uint16_t milliVolts) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "%u mV", (unsigned)milliVolts);
  return n > 0 ? n : 0;
}

const char* const kSilent[kSilentCount] = {
    "set repeat off", "set advert.interval 0", "set flood.advert.interval 0", "gps advert none",
};
const char* const kResume[kSilentCount] = {
    "set repeat on", "set advert.interval 120", "set flood.advert.interval 13", "gps advert prefs",
};

int cliAdvertLocal(char* out, size_t cap, uint8_t minutes) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "set advert.interval %u", (unsigned)minutes);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliAdvertFlood(char* out, size_t cap, uint8_t hours) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "set flood.advert.interval %u", (unsigned)hours);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliFloodMax(char* out, size_t cap, uint8_t hops) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "set flood.max %u", (unsigned)hops);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliPathHash(char* out, size_t cap, uint8_t mode) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "set path.hash.mode %s", pathHashCli(mode));
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliLoopDetect(char* out, size_t cap, LoopDetect v) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "set loop.detect %s", loopDetectText(v));
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliRepeat(char* out, size_t cap, bool on) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "set repeat %s", on ? "on" : "off");
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliPowerSaving(char* out, size_t cap, bool on) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "set powersaving %s", on ? "on" : "off");
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliGpsAdvert(char* out, size_t cap, bool share) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "gps advert %s", share ? "prefs" : "none");
  return n > 0 && (size_t)n < cap ? n : 0;
}


int clockText(char* out, size_t cap, uint32_t epoch) {
  if (out == nullptr || cap == 0) return 0;
  if (epoch < kClockRealSince) {
    // The MeshCore restart clock reads 2024: say the year so "Sync clock" is the obvious next step.
    if (epoch >= 1600000000u) snprintf(out, cap, "not set (%d)", utcYear(epoch));
    else snprintf(out, cap, "not set");
    return (int)strlen(out);
  }
  const uint32_t secs = epoch % 86400u;
  const int n = snprintf(out, cap, "%02u:%02u UTC", (unsigned)(secs / 3600u),
                         (unsigned)((secs / 60u) % 60u));
  return n > 0 ? n : 0;
}

// -------------------------------------------------------------------------------------- clock guard
ClockVerdict clockGuard(uint32_t ourEpoch, bool ourReal, bool rptrKnown, uint32_t rptrEpoch) {
  if (!ourReal || ourEpoch < kClockRealSince) return ClockVerdict::OurClockNotSet;
  // No read yet: treat the repeater as unset (a jump of decades, so the confirm shows "not set").
  const uint32_t r = rptrKnown ? rptrEpoch : 0;
  if (r >= kClockRealSince) {
    if (r > ourEpoch) {
      // The repeater is ahead: a minute of drift is ignored, more is the one refusal.
      return (r - ourEpoch) > kTimeMinBehindSecs ? ClockVerdict::RepeaterAhead
                                                 : ClockVerdict::Ok;
    }
    return (ourEpoch - r) <= kTimeMaxAheadSecs ? ClockVerdict::Ok : ClockVerdict::NeedConfirm;
  }
  // An unset clock (or the 2024 restart clock) is a one-way jump of more than a day.
  return ClockVerdict::NeedConfirm;
}

const char* clockVerdictText(ClockVerdict v) {
  switch (v) {
    case ClockVerdict::Ok: return "the clock can be set";
    case ClockVerdict::NeedConfirm: return "this is a jump of more than a day";
    case ClockVerdict::OurClockNotSet: return "our clock is not set";
    case ClockVerdict::RepeaterAhead: return "the repeater's clock is ahead of ours";
    default: return "";
  }
}

bool clockWrong(bool rptrKnown, uint32_t rptrEpoch, uint32_t ourEpoch, bool ourReal) {
  // Not read, unset, or the 2024 restart clock: definitely wrong, whatever our clock says.
  if (!rptrKnown || rptrEpoch < kClockRealSince) return true;
  if (!ourReal || ourEpoch < kClockRealSince) return false;   // nothing to compare against
  const uint32_t diff = rptrEpoch > ourEpoch ? rptrEpoch - ourEpoch : ourEpoch - rptrEpoch;
  return diff > kTimeMinBehindSecs;
}

namespace {

// Days since 1970-01-01 -> civil date (Howard Hinnant's inverse, UTC only).
void civilFromDays(int64_t z, int* outY, int* outM, int* outD) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = (int64_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  *outY = (int)(y + (m <= 2 ? 1 : 0));
  *outM = (int)m;
  *outD = (int)d;
}

// "2026-11-10 19:05" (no zone), or "not set" for the restart clock.
void stampShort(char* out, size_t cap, uint32_t epoch) {
  if (out == nullptr || cap == 0) return;
  // A 2020..2024 stamp is MeshCore's restart clock: it is still a date, and a jump confirmation
  // must show it as one. Only an unset (or nonsense) clock has no date at all.
  if (epoch < 1600000000u) {
    snprintf(out, cap, "not set");
    return;
  }
  int y = 0, mo = 0, d = 0;
  civilFromDays((int64_t)(epoch / 86400u), &y, &mo, &d);
  const uint32_t secs = epoch % 86400u;
  snprintf(out, cap, "%04d-%02d-%02d %02u:%02u", y, mo, d, (unsigned)(secs / 3600u),
           (unsigned)((secs / 60u) % 60u));
}

}  // namespace

int clockStampText(char* out, size_t cap, uint32_t epoch) {
  if (out == nullptr || cap == 0) return 0;
  if (epoch < kClockRealSince) {
    if (epoch >= 1600000000u) {
      int y = 0, mo = 0, d = 0;
      civilFromDays((int64_t)(epoch / 86400u), &y, &mo, &d);
      snprintf(out, cap, "not set (%04d-%02d-%02d)", y, mo, d);
    } else {
      snprintf(out, cap, "not set");
    }
    return (int)strlen(out);
  }
  char s[32];
  stampShort(s, sizeof(s), epoch);
  const int n = snprintf(out, cap, "%s UTC", s);
  return n > 0 ? n : 0;
}

int clockJumpText(char* out, size_t cap, uint32_t fromEpoch, uint32_t toEpoch) {
  if (out == nullptr || cap == 0) return 0;
  char from[32];
  char to[32];
  stampShort(from, sizeof(from), fromEpoch);
  stampShort(to, sizeof(to), toEpoch);
  const int n = snprintf(out, cap, "Set the repeater's clock from %s to %s UTC?", from, to);
  return n > 0 ? n : 0;
}

// ---------------------------------------------------------------------------------------- read-backs
namespace {

// CommonCLI answers start with "> "; a multi-line answer may carry trailing CR/LF. Trim both.
void replyValue(const char* text, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = 0;
  if (text == nullptr) return;
  const char* p = text;
  while (*p == ' ' || *p == '\t') ++p;
  if (p[0] == '>' && p[1] == ' ') p += 2;
  size_t n = strlen(p);
  while (n > 0 && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' ')) --n;
  if (n >= cap) n = cap - 1;
  memcpy(out, p, n);
  out[n] = 0;
}

}  // namespace

bool valueReplyParse(const char* text, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  replyValue(text, out, cap);
  return out[0] != 0 && strcmp(out, "Error") != 0 && strncmp(out, "Err", 3) != 0;
}

bool intReplyParse(const char* text, int32_t* out) {
  if (out == nullptr) return false;
  char v[24];
  replyValue(text, v, sizeof(v));
  if (v[0] == 0) return false;
  char* end = nullptr;
  const long n = strtol(v, &end, 10);
  if (end == v || end == nullptr) return false;
  while (*end == ' ') ++end;
  if (*end != 0) return false;
  *out = (int32_t)n;
  return true;
}

bool radioReplyParse(const char* text, RadioValues* out) {
  if (out == nullptr) return false;
  char v[40];
  replyValue(text, v, sizeof(v));
  float freq = 0, bw = 0;
  int sf = 0, cr = 0;
  char tail = 0;
  if (sscanf(v, "%f,%f,%d,%d%c", &freq, &bw, &sf, &cr, &tail) != 4) return false;
  if (freq < 150.0f || freq > 2500.0f || bw < 7.0f || bw > 500.0f) return false;
  if (sf < 5 || sf > 12 || cr < 5 || cr > 8) return false;
  out->freq_khz = (uint32_t)(freq * 1000.0f + 0.5f);
  out->bw_hz = (uint32_t)(bw * 1000.0f + 0.5f);
  out->sf = (uint8_t)sf;
  out->cr = (uint8_t)cr;
  return true;
}

bool coordReplyParse(const char* text, int32_t* e6) {
  if (e6 == nullptr) return false;
  char v[24];
  replyValue(text, v, sizeof(v));
  if (v[0] == 0) return false;
  char* end = nullptr;
  const double deg = strtod(v, &end);
  if (end == v || end == nullptr) return false;
  while (*end == ' ') ++end;
  if (*end != 0) return false;
  if (deg < -180.0 || deg > 180.0) return false;
  *e6 = (int32_t)lround(deg * 1000000.0);
  return true;
}

bool switchReplyParse(const char* text, bool* on) {
  if (on == nullptr) return false;
  char v[12];
  replyValue(text, v, sizeof(v));
  if (strcmp(v, "on") == 0) { *on = true; return true; }
  if (strcmp(v, "off") == 0) { *on = false; return true; }
  return false;
}

bool loopReplyParse(const char* text, LoopDetect* out) {
  if (out == nullptr) return false;
  char v[12];
  replyValue(text, v, sizeof(v));
  if (strcmp(v, "off") == 0) { *out = LoopDetect::Off; return true; }
  if (strcmp(v, "minimal") == 0) { *out = LoopDetect::Minimal; return true; }
  if (strcmp(v, "moderate") == 0) { *out = LoopDetect::Moderate; return true; }
  if (strcmp(v, "strict") == 0) { *out = LoopDetect::Strict; return true; }
  return false;
}

bool answerOk(const char* text) {
  if (text == nullptr) return false;
  const char* p = text;
  while (*p == ' ' || *p == '\t') ++p;
  if (strncmp(p, "OK", 2) == 0) return true;
  // region home / region default answer " home is now ..." / " default scope is now ...".
  if (strstr(p, " is now ") != nullptr) return true;
  return false;
}

int answerPlainText(const char* text, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  if (text == nullptr || text[0] == 0) {
    snprintf(out, cap, "No answer");
    return (int)strlen(out);
  }
  char v[120];
  replyValue(text, v, sizeof(v));
  struct Map { const char* raw; const char* plain; };
  static const Map kMap[] = {
      {"OK - reboot to apply", "Applied. Restart the repeater to use it."},
      {"OK - (flood allowed)", "Applied"},
      {"OK - repeat is now OFF", "Applied. Repeat is off."},
      {"OK - repeat is now ON", "Applied. Repeat is on."},
      {"Err - unknown region", "The repeater does not know that region."},
      {"Err - unknown parent", "The repeater does not know that parent region."},
      {"Err - not empty", "That region still has child regions."},
      {"Err - not found", "That region is not there."},
      {"Err - unable to put", "The repeater could not add that region."},
      {"Err - save failed", "The repeater could not save its regions."},
      {"Err - region table full", "The repeater's region table is full."},
      {"Err - ??", "The repeater did not understand that."},
      {"Error, invalid radio params", "Those radio settings were refused."},
      {"Error, bad chars", "The repeater refused that name."},
      {"Error, max 64", "The maximum is 64."},
      {"Error: interval range is 3-168 hours", "The flood interval must be 3 to 168 hours."},
      {"ERR: clock cannot go backwards", "The repeater's clock cannot go backwards."},
      {"(ERR: clock cannot go backwards)", "The repeater's clock cannot go backwards."},
  };
  for (const Map& m : kMap) {
    if (strcmp(v, m.raw) == 0) {
      snprintf(out, cap, "%s", m.plain);
      return (int)strlen(out);
    }
  }
  if (answerOk(v)) {
    snprintf(out, cap, "Applied");
    return (int)strlen(out);
  }
  // An error we have no sentence for: say it was refused, then the repeater's own words.
  const char* rest = v;
  if (strncmp(rest, "Error, ", 7) == 0) rest += 7;
  else if (strncmp(rest, "Error: ", 7) == 0) rest += 7;
  else if (strncmp(rest, "Err - ", 6) == 0) rest += 6;
  else if (strncmp(rest, "Error", 5) == 0) rest += 5;
  while (*rest == ' ' || *rest == '-') ++rest;
  const int n = snprintf(out, cap, "Refused: %s", rest);
  return n > 0 ? n : 0;
}

}  // namespace rptr
}  // namespace om
