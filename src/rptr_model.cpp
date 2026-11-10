// SPDX-License-Identifier: MIT
// EST-358: the RPTR model's arithmetic and text. See rptr_model.h for the spec and the sources.
#include "rptr_model.h"

#include <stdio.h>
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
TimeVerdict timeSyncCheck(uint32_t ourEpoch, bool ourReal, bool rptrKnown, uint32_t rptrEpoch) {
  if (!ourReal || ourEpoch < kClockRealSince) return TimeVerdict::OurClockNotSet;
  if (rptrKnown && rptrEpoch >= kClockRealSince) {
    const int64_t ahead = (int64_t)ourEpoch - (int64_t)rptrEpoch;
    if (ahead > (int64_t)kTimeMaxAheadSecs) return TimeVerdict::TooFarAhead;
    if (-ahead > (int64_t)kTimeMinBehindSecs) return TimeVerdict::RepeaterAhead;
  }
  return TimeVerdict::Ok;
}

const char* timeVerdictText(TimeVerdict v) {
  switch (v) {
    case TimeVerdict::Ok: return "sync is safe";
    case TimeVerdict::OurClockNotSet: return "our clock is not set";
    case TimeVerdict::TooFarAhead: return "more than a day ahead: refused";
    case TimeVerdict::RepeaterAhead: return "repeater's clock is ahead: refused";
    default: return "";
  }
}

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

// ------------------------------------------------------------------------------------- the admin rows
const char* viewName(int view) {
  switch (view) {
    case 0: return "Overview";
    case 1: return "Radio";
    case 2: return "Regions";
    case 3: return "Mesh";
    case 4: return "Neigh.";
    case 5: return "Actions";
    case 6: return "Region";
    default: return "";
  }
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

namespace {

// A bounded copy: never a truncation warning, never a missing terminator.
void copyInto(char* dst, size_t cap, const char* src) {
  if (dst == nullptr || cap == 0) return;
  if (src == nullptr) src = "";
  size_t n = strlen(src);
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = 0;
}

AdminRow makeRow(RowId id, const char* label, const char* value, RowKind kind) {
  AdminRow r;
  r.id = id;
  copyInto(r.label, sizeof(r.label), label);
  copyInto(r.value, sizeof(r.value), value);
  r.kind = kind;
  return r;
}

}  // namespace

int adminRows(int view, const AdminValues& v, const Status& status, uint8_t fwLevel,
              const char* repeaterName, const RegionList& regions, const NeighbourList& neighbours,
              const char* home, const char* defaultScope, int regionSel, const char* pendingLine,
              AdminRow* out, int cap) {
  if (out == nullptr || cap <= 0) return 0;
  int n = 0;
  char buf[96];
  auto push = [&](const AdminRow& r) {
    if (n < cap) out[n++] = r;
  };

  // EST-360: the one-step confirm. Every CLI line that changes the repeater is armed first; these
  // two rows show the EXACT line and let the finger send it (or cancel) with one more tap.
  if (pendingLine != nullptr && pendingLine[0]) {
    AdminRow send = makeRow(RowId::PendingSend, "Send now", pendingLine, RowKind::Action);
    push(send);
    push(makeRow(RowId::PendingCancel, "Cancel", "", RowKind::Action));
  }

  switch (view) {
    case 0: {   // Overview: what the status reply says, plus the login answer's firmware and clock
      push(makeRow(RowId::StatusRefresh, "Read all", "REQ_TYPE_GET_STATUS", RowKind::Action));
      if (!status.valid) {
        AdminRow r = makeRow(RowId::None, "status", "not read yet, tap Read", RowKind::Fact);
        push(r);
        return n;
      }
      uptimeText(buf, sizeof(buf), status.uptimeSec);
      push(makeRow(RowId::StatusUptime, "uptime", buf, RowKind::Fact));
      {
        // EST-360: volts with two decimals. Outside 2.5..30 V (or 0) the primary value is "n/a"; the
        // raw mV is on this row's detail line only, so a wrong decode cannot look like a voltage.
        char volts[16];
        batteryVoltsText(volts, sizeof(volts), status.battMv);
        const int pct = batteryPercent(status.battMv);
        if (!batteryPlausible(status.battMv)) {
          snprintf(buf, sizeof(buf), "%s (%u mV)", volts, (unsigned)status.battMv);
        } else if (pct >= 0) {
          snprintf(buf, sizeof(buf), "%s, %d%%", volts, pct);
        } else {
          snprintf(buf, sizeof(buf), "%s", volts);
        }
        push(makeRow(RowId::StatusBattery, "battery", buf, RowKind::Fact));
      }
      uptimeText(buf, sizeof(buf), status.airtimeSec);
      push(makeRow(RowId::StatusAirtime, "airtime", buf, RowKind::Fact));
      snprintf(buf, sizeof(buf), "rx %lu, tx %lu", (unsigned long)status.rxPackets,
               (unsigned long)status.txPackets);
      push(makeRow(RowId::StatusPackets, "packets", buf, RowKind::Fact));
      snprintf(buf, sizeof(buf), "%d dBm", (int)status.noiseFloor);
      push(makeRow(RowId::StatusNoise, "noise floor", buf, RowKind::Fact));
      snrText(buf, sizeof(buf), status.lastSnr4);
      push(makeRow(RowId::StatusSnr, "last SNR", buf, RowKind::Fact));
      snprintf(buf, sizeof(buf), "level %u", (unsigned)fwLevel);
      push(makeRow(RowId::StatusFirmware, "firmware", buf, RowKind::Fact));
      clockText(buf, sizeof(buf), status.clock);
      push(makeRow(RowId::StatusClock, "clock", buf, RowKind::Fact));
      return n;
    }
    case 1: {   // Radio: the mock's +/- steppers, and Apply sends the two lines
      char f[16];
      freqText(f, sizeof(f), v.radio.freq_khz);
      snprintf(buf, sizeof(buf), "%s MHz", f);
      AdminRow freq = makeRow(RowId::RadioFreq, "Freq", buf, RowKind::Stepper);
      freq.keys = 2;
      push(freq);
      char bw[16];
      bwText(bw, sizeof(bw), v.radio.bw_hz);
      snprintf(buf, sizeof(buf), "%s kHz", bw);
      AdminRow bwRow = makeRow(RowId::RadioBw, "BW", buf, RowKind::Stepper);
      bwRow.keys = 2;
      push(bwRow);
      snprintf(buf, sizeof(buf), "%u", (unsigned)v.radio.sf);
      AdminRow sf = makeRow(RowId::RadioSf, "SF", buf, RowKind::Stepper);
      sf.keys = 2;
      push(sf);
      snprintf(buf, sizeof(buf), "4/%u", (unsigned)v.radio.cr);
      AdminRow cr = makeRow(RowId::RadioCr, "CR", buf, RowKind::Stepper);
      cr.keys = 2;
      push(cr);
      snprintf(buf, sizeof(buf), "%d dBm", (int)v.radio.tx);
      AdminRow tx = makeRow(RowId::RadioTx, "TX power", buf, RowKind::Stepper);
      tx.keys = 2;
      push(tx);
      push(makeRow(RowId::RadioApply, "Apply to repeater", "reboot", RowKind::Action));
      return n;
    }
    case 2: {   // Regions: read, add, save, home and default as words, then one row per region
      push(makeRow(RowId::RegionRead, "Read regions", kCliRegionList, RowKind::Action));
      push(makeRow(RowId::RegionAdd, "Add region", "region put", RowKind::Action));
      push(makeRow(RowId::RegionSave, "Save to repeater", "makes changes permanent",
                   RowKind::Action));
      // The repeater's own answers, not what this device remembers.
      if (home == nullptr) snprintf(buf, sizeof(buf), "not read");
      else if (strcmp(home, "*") == 0) snprintf(buf, sizeof(buf), "* (no region)");
      else snprintf(buf, sizeof(buf), "%s", home);
      push(makeRow(RowId::RegionHomeFact, "Home", buf, RowKind::Fact));
      if (defaultScope == nullptr) snprintf(buf, sizeof(buf), "not read");
      else if (defaultScope[0] == 0) snprintf(buf, sizeof(buf), "none");
      else snprintf(buf, sizeof(buf), "%s", defaultScope);
      push(makeRow(RowId::RegionDefaultFact, "Default scope", buf, RowKind::Fact));
      push(makeRow(RowId::RegionSetHomeNone, "Home: * (no region)", "region home *",
                   RowKind::Action));
      push(makeRow(RowId::RegionDefaultNone, "Default scope: none", "region default <null>",
                   RowKind::Action));
      if (!regions.valid || regions.count == 0) {
        push(makeRow(RowId::None, "regions", "none read yet", RowKind::Fact));
      } else {
        for (int i = 0; i < regions.count && n < cap; ++i) {
          const Region& rg = regions.rows[i];
          AdminRow r = makeRow(RowId::RegionRow, rg.name[0] ? rg.name : "*", "", RowKind::Open);
          snprintf(r.value, sizeof(r.value), "%s%s", rg.home ? "^ home, " : "",
                   rg.allow ? "allow" : "deny");
          r.keys = 1;
          r.on = rg.allow;
          r.index = (uint8_t)i;
          push(r);
        }
      }
      return n;
    }
    case 3: {   // Mesh: path hash, loop detect, adverts, flood, repeat, power save, position, identity
      {
        AdminRow r = makeRow(RowId::PathHash, "path hash", pathHashText(v.pathHash),
                             RowKind::Stepper);
        r.keys = 2;
        push(r);
      }
      {
        AdminRow r = makeRow(RowId::LoopDetect, "loop detect", loopDetectText(v.loop),
                             RowKind::Stepper);
        r.keys = 2;
        push(r);
      }
      {
        snprintf(buf, sizeof(buf), "%u min", (unsigned)v.advertLocal);
        AdminRow r = makeRow(RowId::AdvertLocal, "adverts local", buf, RowKind::Stepper);
        r.keys = 2;
        push(r);
      }
      {
        snprintf(buf, sizeof(buf), "%u h", (unsigned)v.advertFlood);
        AdminRow r = makeRow(RowId::AdvertFlood, "adverts flood", buf, RowKind::Stepper);
        r.keys = 2;
        push(r);
      }
      {
        snprintf(buf, sizeof(buf), "%u", (unsigned)v.floodMax);
        AdminRow r = makeRow(RowId::FloodMax, "flood max", buf, RowKind::Stepper);
        r.keys = 2;
        push(r);
      }
      {
        AdminRow r = makeRow(RowId::Repeat, "repeat", v.repeat ? "on" : "off", RowKind::Toggle);
        r.on = v.repeat;
        push(r);
      }
      {
        AdminRow r = makeRow(RowId::PowerSave, "power save", v.powerSave ? "on" : "off",
                             RowKind::Toggle);
        r.on = v.powerSave;
        push(r);
      }
      {
        AdminRow r = makeRow(RowId::SharePos, "share position", v.sharePosition ? "on" : "off",
                             RowKind::Toggle);
        r.on = v.sharePosition;
        push(r);
      }
      push(makeRow(RowId::EditName, "name", repeaterName == nullptr ? "" : repeaterName,
                   RowKind::Edit));
      push(makeRow(RowId::EditOwner, "owner", "tap to set", RowKind::Edit));
      push(makeRow(RowId::EditPos, "position", "tap to set", RowKind::Edit));
      push(makeRow(RowId::UseGpsPos, "use my GPS position", "send", RowKind::Action));
      return n;
    }
    case 4: {   // Neighbours
      push(makeRow(RowId::NeighDiscover, "Discover neighbours", "discover.neighbors",
                   RowKind::Action));
      push(makeRow(RowId::NeighRead, "Read stored list", "neighbors", RowKind::Action));
      if (!neighbours.valid || neighbours.count == 0) {
        push(makeRow(RowId::None, "neighbours", "none read yet", RowKind::Fact));
      } else {
        for (int i = 0; i < neighbours.count && n < cap; ++i) {
          const Neighbour& nb = neighbours.rows[i];
          char snr[12];
          snrText(snr, sizeof(snr), nb.snr4);
          snprintf(buf, sizeof(buf), "SNR %s", snr);
          AdminRow r = makeRow(RowId::NeighRow, nb.name[0] ? nb.name : "?", buf, RowKind::Fact);
          r.index = (uint8_t)i;
          push(r);
        }
      }
      return n;
    }
    case 5: {   // Actions
      push(makeRow(RowId::AdvertLocalOnce, "Local advert", "advert.zerohop", RowKind::Action));
      push(makeRow(RowId::AdvertFloodOnce, "Flood advert", "advert", RowKind::Action));
      // The clock rows' values are filled by the shell from timeKeeper() and the last `clock` reply
      // (this table is pure text and has no clock).
      push(makeRow(RowId::ClockSync, "Sync clock", "", RowKind::Action));
      push(makeRow(RowId::ClockBefore, "clock before", "", RowKind::Fact));
      push(makeRow(RowId::ClockAfter, "clock after", "", RowKind::Fact));
      push(makeRow(RowId::Silent, "Silent mode", "4 commands", RowKind::Action));
      push(makeRow(RowId::Resume, "Resume", "4 commands", RowKind::Action));
      {
        AdminRow r = makeRow(RowId::Reboot, "Reboot", "tap twice", RowKind::Action);
        r.danger = true;
        push(r);
      }
      return n;
    }
    case 6: {   // One region's page: every action is a word with the exact CLI line beside it
      if (regionSel < 0 || regionSel >= regions.count) {
        push(makeRow(RowId::None, "region", "none selected", RowKind::Fact));
        return n;
      }
      const Region& rg = regions.rows[regionSel];
      const char* name = rg.name[0] ? rg.name : "*";
      const bool wildcard = rg.wildcard || strcmp(name, "*") == 0;
      if (wildcard) snprintf(buf, sizeof(buf), "* (no region)%s", rg.home ? ", the home" : "");
      else snprintf(buf, sizeof(buf), "%s%s", name, rg.home ? ", the home region" : "");
      push(makeRow(RowId::RegionNameFact, "Region", buf, RowKind::Fact));
      if (!wildcard) {
        snprintf(buf, sizeof(buf), "region put <new> %s", name);
        push(makeRow(RowId::RegionAddChild, "Add child region", buf, RowKind::Action));
      } else {
        push(makeRow(RowId::RegionAddChild, "Add region", "region put", RowKind::Action));
      }
      cliRegionFlood(buf, sizeof(buf), name, true);
      push(makeRow(RowId::RegionAllow, "Allow flood", buf, RowKind::Action));
      cliRegionFlood(buf, sizeof(buf), name, false);
      push(makeRow(RowId::RegionDeny, "Deny flood", buf, RowKind::Action));
      if (wildcard) {
        push(makeRow(RowId::RegionHome, "Set as home: none", "region home *", RowKind::Action));
      } else {
        cliRegionHome(buf, sizeof(buf), name);
        push(makeRow(RowId::RegionHome, "Set as home", buf, RowKind::Action));
      }
      if (!wildcard) {
        cliRegionDefault(buf, sizeof(buf), name);
        push(makeRow(RowId::RegionDefault, "Set as default scope", buf, RowKind::Action));
        cliRegionRemove(buf, sizeof(buf), name);
        AdminRow r = makeRow(RowId::RegionRemove, "Remove region", buf, RowKind::Action);
        r.danger = true;
        push(r);
      }
      return n;
    }
    default:
      return 0;
  }
}

// ------------------------------------------------------------------------------------------ confirm
void Confirm::arm(const char* what, uint32_t nowMs) {
  key = what;
  armedMs = nowMs;
  armed = true;
}

bool Confirm::take(const char* what, uint32_t nowMs) {
  if (!armed || key == nullptr || what == nullptr) return false;
  if (key != what && strcmp(key, what) != 0) return false;
  if ((uint32_t)(nowMs - armedMs) > kConfirmMs) {
    armed = false;
    return false;
  }
  armed = false;
  key = nullptr;
  return true;
}

bool Confirm::tick(uint32_t nowMs) {
  if (!armed) return false;
  if ((uint32_t)(nowMs - armedMs) <= kConfirmMs) return false;
  armed = false;
  key = nullptr;
  return true;
}

void Confirm::clear() {
  armed = false;
  key = nullptr;
  armedMs = 0;
}

}  // namespace rptr
}  // namespace om
