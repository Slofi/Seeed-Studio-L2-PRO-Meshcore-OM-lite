// SPDX-License-Identifier: MIT
// EST-358: the RPTR model's arithmetic and text. See rptr_model.h for the spec and the sources.
#include "rptr_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The one Europe/Ljubljana local-time rule (EST-364): om::localCivil() below is the same function
// the status bar and ui/age.cpp use, so the RPTR section cannot invent its own zone.
#include "timekeeper.h"

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

// ---------------------------------------------------------------------------------------- anon owner
bool anonOwnerParse(const uint8_t* buf, int len, char* name, size_t nameCap, char* owner,
                    size_t ownerCap) {
  if (name != nullptr && nameCap > 0) name[0] = 0;
  if (owner != nullptr && ownerCap > 0) owner[0] = 0;
  if (buf == nullptr || name == nullptr || nameCap == 0) return false;
  if (len <= 4) return false;   // clock only, no name line
  const int bodyLen = len - 4;
  int end = bodyLen;
  for (int i = 0; i < bodyLen; ++i) {
    if (buf[4 + i] == 0) {
      end = i;
      break;
    }
  }
  int lineEnd = 0;
  while (lineEnd < end && buf[4 + lineEnd] != '\n') ++lineEnd;
  int start = 0;
  while (start < lineEnd && (buf[4 + start] == ' ' || buf[4 + start] == '\t')) ++start;
  int stop = lineEnd;
  while (stop > start && (buf[4 + stop - 1] == ' ' || buf[4 + stop - 1] == '\t' ||
                          buf[4 + stop - 1] == '\r')) {
    --stop;
  }
  if (stop <= start) return false;
  size_t at = 0;
  for (int i = start; i < stop && at + 1 < nameCap; ++i) {
    const unsigned char c = buf[4 + i];
    name[at++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
  }
  name[at] = 0;
  if (owner != nullptr && ownerCap > 0 && lineEnd < end) {
    int os = lineEnd + 1;
    int oe = end;
    while (os < oe && (buf[4 + os] == ' ' || buf[4 + os] == '\t')) ++os;
    while (oe > os && (buf[4 + oe - 1] == ' ' || buf[4 + oe - 1] == '\t' ||
                       buf[4 + oe - 1] == '\r')) {
      --oe;
    }
    size_t oa = 0;
    for (int i = os; i < oe && oa + 1 < ownerCap; ++i) {
      const unsigned char c = buf[4 + i];
      owner[oa++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    owner[oa] = 0;
  }
  return name[0] != 0;
}

// --------------------------------------------------------------------------------------- neighbours
int neighboursParse(const char* text, NeighbourList* out) {
  if (out == nullptr) return 0;
  out->count = 0;
  out->valid = false;   // only a reply that LOOKS like the table may become a real empty answer
  for (int i = 0; i < kMaxNeighbours; ++i) out->rows[i] = Neighbour();
  if (text == nullptr) return 0;
  const char* p = text;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
  if (p[0] == '>' && p[1] == ' ') p += 2;   // CommonCLI's prompt, when a caller kept it
  while (*p == ' ' || *p == '\t') ++p;
  if (*p == 0) {
    out->valid = true;   // an empty answer to `neighbors` is still an answer: nobody heard yet
    return 0;
  }
  // A real answer with no rows. `-none-` is CommonCLI's own word; "none" is accepted so a newer
  // format cannot turn a real empty table into "not read".
  if (strcmp(p, "-none-") == 0 || strcmp(p, "none") == 0) {
    out->valid = true;
    return 0;
  }
  // Anything else that is not at least one "hhhhhhhh:secs:snr4" line is NOT a neighbour answer
  // (for example "Err - unknown command"): the page must not claim the table is empty.
  const char* line = p;
  while (*line && out->count < kMaxNeighbours) {
    const char* end = strchr(line, '\n');
    int lineLen = end ? (int)(end - line) : (int)strlen(line);
    while (lineLen > 0 && (line[lineLen - 1] == '\r' || line[lineLen - 1] == ' ')) --lineLen;
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
  // At least one parsed "prefix:secs:snr" line proves this is the table. A reply with no such line
  // (an error, an unknown command) stays valid=false, so the page says "not read", never "empty".
  out->valid = out->count > 0;
  return out->count;
}

// ------------------------------------------------------------------------------------------ regions
int regionsParse(const char* text, RegionList* out) {
  if (out == nullptr) return 0;
  // Start from an empty list EVERY time: a shorter answer (Filip removed "lj") must not leave the
  // old tail behind. This was the shape of the stale list the EST-366 review found.
  out->count = 0;
  for (int i = 0; i < kMaxRegions; ++i) out->rows[i] = Region();
  out->valid = false;   // only a list-shaped answer may be shown as the repeater's regions
  if (text == nullptr || text[0] == 0) {
    out->valid = true;   // an empty answer to `region` is a real empty map
    return 0;
  }
  // CommonCLI prefixes a refusal with "Err - " / "Error"; that is not a region called "Err".
  const char* first = text;
  while (*first == ' ' || *first == '\t' || *first == '\r' || *first == '\n') ++first;
  if (first[0] == '>' && first[1] == ' ') first += 2;
  while (*first == ' ' || *first == '\t') ++first;
  if (strncmp(first, "Err - ", 6) == 0 || strncmp(first, "Error", 5) == 0 ||
      strcmp(first, "Unknown command") == 0) {
    return 0;
  }
  out->valid = true;
  const char* line = first;
  while (*line && out->count < kMaxRegions) {
    const char* end = strchr(line, '\n');
    int lineLen = end ? (int)(end - line) : (int)strlen(line);
    // CommonCLI's exportTo() ends every line with '\n'; a CLI carried over a packet may show '\r\n'.
    // Trailing CR/space is formatting, never part of a region name or its flags.
    while (lineLen > 0 && (line[lineLen - 1] == '\r' || line[lineLen - 1] == ' ' ||
                           line[lineLen - 1] == '\t')) {
      --lineLen;
    }
    // Indentation is the tree depth; the first line of a complete answer is the wildcard region.
    int at = 0;
    while (at < lineLen && (line[at] == ' ' || line[at] == '\t')) ++at;
    if (at < lineLen) {
      Region& r = out->rows[out->count];
      r = Region();
      int n = 0;
      while (at < lineLen && line[at] != ' ' && line[at] != '^' && line[at] != '\t' &&
             n < (int)sizeof(r.name) - 1) {
        r.name[n++] = line[at++];
      }
      r.name[n] = 0;
      // RegionMap::printChildRegions() writes "<name>[^][ F]\n": '^' is the home region, " F" means
      // flood allowed (no F = denied). Both markers may come after the name in either order in a
      // future format, so the whole tail is scanned rather than only its last two bytes.
      for (int i = at; i < lineLen; ++i) {
        if (line[i] == '^') r.home = true;
      }
      for (int i = at; i < lineLen; ++i) {
        if (line[i] == 'F' && i > 0 && (line[i - 1] == ' ' || line[i - 1] == '\t')) r.allow = true;
      }
      r.wildcard = r.name[0] == '*' || out->count == 0;
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
// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm). Used to turn the repeater's
// own UTC clock text into an epoch; the DISPLAY never prints UTC (EST-364).
int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2 ? 1 : 0;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

// The LOCAL year of an epoch, for the "not set (2024)" label. Everything this section shows is local
// (EST-364), and around New Year local and UTC are not even in the same year.
int localYear(uint32_t epoch) {
  int y = 0, mo = 0, d = 0, hh = 0, mi = 0;
  if (om::localCivil(epoch, y, mo, d, hh, mi)) return y;
  return 1970 + (int)(epoch / 31556952u);
}

// The same instant in the repeater's own order, but local and dotted: "20:33 - 10.11.2026". The
// console log keeps the answer's shape, only the time and the date change.
void stampClockAnswer(char* out, size_t cap, uint32_t epoch) {
  if (out == nullptr || cap == 0) return;
  if (epoch < 1600000000u) {
    snprintf(out, cap, "not set");
    return;
  }
  int y = 0, mo = 0, d = 0, hh = 0, mi = 0;
  if (!om::localCivil(epoch, y, mo, d, hh, mi)) {
    snprintf(out, cap, "not set");
    return;
  }
  snprintf(out, cap, "%02d:%02d - %02d.%02d.%04d", hh, mi, d, mo, y);
}

// The same instant as Filip writes a date: "10.11.2026 20:33" (the Set clock pop-up's own wording).
void stampDotted(char* out, size_t cap, uint32_t epoch) {
  if (out == nullptr || cap == 0) return;
  if (epoch < 1600000000u) {
    snprintf(out, cap, "not set");
    return;
  }
  int y = 0, mo = 0, d = 0, hh = 0, mi = 0;
  if (!om::localCivil(epoch, y, mo, d, hh, mi)) {
    snprintf(out, cap, "not set");
    return;
  }
  snprintf(out, cap, "%02d.%02d.%04d %02d:%02d", d, mo, y, hh, mi);
}

// The repeater's own clock text, "HH:MM - DD/MM/YYYY UTC" (CommonCLI prints UTC), at the start of
// `text`. The `clock` read and the `OK - clock set: ...` confirmation carry the same shape, so both
// parsers below share this one. Returns a pointer just past the parsed text, or nullptr.
const char* parseRepeaterClock(const char* text, uint32_t* epoch) {
  if (text == nullptr || epoch == nullptr) return nullptr;
  int hh = 0, mm = 0, dd = 0, mo = 0, yy = 0, used = 0;
  char tail[8] = "";
  if (sscanf(text, "%d:%d - %d/%d/%d %7s%n", &hh, &mm, &dd, &mo, &yy, tail, &used) != 6) {
    return nullptr;
  }
  if (strcmp(tail, "UTC") != 0) return nullptr;
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || dd < 1 || dd > 31 || mo < 1 || mo > 12 ||
      yy < 2020 || yy > 2100) {
    return nullptr;
  }
  *epoch = (uint32_t)(daysFromCivil(yy, mo, dd) * 86400 + hh * 3600 + mm * 60);
  return text + used;
}
}  // namespace

bool clockReplyParse(const char* text, uint32_t* epoch) {
  // CommonCLI's `clock`: exactly "HH:MM - DD/MM/YYYY UTC", nothing after it.
  const char* end = parseRepeaterClock(text, epoch);
  if (end == nullptr) return false;
  while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
  return *end == 0;
}

bool clockSetReplyParse(const char* text, uint32_t* epoch) {
  // "OK - clock set: 19:33 - 10/11/2026 UTC" is the repeater's confirmation that it took our `time`.
  // The prefix is its own wording, so it is found rather than assumed; the clock text after it is
  // parsed by the same code as the `clock` read.
  if (text == nullptr || epoch == nullptr) return false;
  const char* at = strstr(text, "clock set:");
  if (at == nullptr) return false;
  return parseRepeaterClock(at + 10, epoch) != nullptr;
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
    if (epoch >= 1600000000u) snprintf(out, cap, "not set (%d)", localYear(epoch));
    else snprintf(out, cap, "not set");
    return (int)strlen(out);
  }
  // LOCAL time (EST-364): the repeater answers UTC, Filip reads Ljubljana.
  int y = 0, mo = 0, d = 0, hh = 0, mi = 0;
  if (!om::localCivil(epoch, y, mo, d, hh, mi)) {
    snprintf(out, cap, "not set");
    return (int)strlen(out);
  }
  const int n = snprintf(out, cap, "%02d:%02d", hh, mi);
  return n > 0 ? n : 0;
}

int localClockAnswer(const char* text, char* out, size_t cap) {
  // The console log is raw CLI text, but a clock answer is still a time Filip reads: rewrite it in
  // local time and drop the repeater's "UTC" label (EST-364). "OK - clock set: 19:33 - 10/11/2026
  // UTC" becomes "OK - clock set: 20:33 - 10.11.2026"; anything that is not a clock answer is
  // copied through untouched.
  if (out == nullptr || cap == 0) return 0;
  out[0] = 0;
  if (text == nullptr) return 0;
  // Where the clock text starts: right after "clock set: " when the answer confirms a set, else at
  // the start of the answer (the `clock` read).
  const char* start = nullptr;
  const char* marker = strstr(text, "clock set:");
  if (marker != nullptr && marker[10] == ' ') {
    start = marker + 11;
  } else {
    const char* p = text;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '>' && p[1] == ' ') p += 2;
    start = p;
  }
  const uint32_t at = (uint32_t)(start - text);
  uint32_t epoch = 0;
  const char* end = parseRepeaterClock(start, &epoch);
  if (end == nullptr) {
    snprintf(out, cap, "%s", text);
    return (int)strlen(out);
  }
  char local[24];
  stampClockAnswer(local, sizeof(local), epoch);
  const size_t prefix = at < cap ? at : cap - 1;
  memcpy(out, text, prefix);
  out[prefix] = 0;
  char tail[16] = "";
  while (*end == ' ' || *end == '\t') ++end;
  snprintf(tail, sizeof(tail), "%s", end);
  const int n = snprintf(out + prefix, cap - prefix, "%s%s%s", local, tail[0] ? " " : "", tail);
  return n > 0 ? (int)(prefix + (size_t)n) : (int)prefix;
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

// The LOCAL stamp of an epoch, "2026-11-10 20:05". EST-364: the zone comes from om::localCivil(),
// the same Europe/Ljubljana rule the status bar and ui/age.cpp use, and no line says "UTC".
void stampShort(char* out, size_t cap, uint32_t epoch) {
  if (out == nullptr || cap == 0) return;
  // A 2020..2024 stamp is MeshCore's restart clock: it is still a date, and a jump confirmation
  // must show it as one. Only an unset (or nonsense) clock has no date at all.
  if (epoch < 1600000000u) {
    snprintf(out, cap, "not set");
    return;
  }
  int y = 0, mo = 0, d = 0, hh = 0, mi = 0;
  if (!om::localCivil(epoch, y, mo, d, hh, mi)) {
    snprintf(out, cap, "not set");
    return;
  }
  snprintf(out, cap, "%04d-%02d-%02d %02d:%02d", y, mo, d, hh, mi);
}

}  // namespace

int clockStampText(char* out, size_t cap, uint32_t epoch) {
  if (out == nullptr || cap == 0) return 0;
  if (epoch < kClockRealSince) {
    if (epoch >= 1600000000u) {
      int y = 0, mo = 0, d = 0, hh = 0, mi = 0;
      if (!om::localCivil(epoch, y, mo, d, hh, mi)) {
        snprintf(out, cap, "not set");
        return (int)strlen(out);
      }
      snprintf(out, cap, "not set (%04d-%02d-%02d)", y, mo, d);
    } else {
      snprintf(out, cap, "not set");
    }
    return (int)strlen(out);
  }
  char s[32];
  stampShort(s, sizeof(s), epoch);
  const int n = snprintf(out, cap, "%s", s);
  return n > 0 ? n : 0;
}

int clockJumpText(char* out, size_t cap, uint32_t fromEpoch, uint32_t toEpoch) {
  if (out == nullptr || cap == 0) return 0;
  char from[32];
  char to[32];
  stampDotted(from, sizeof(from), fromEpoch);
  stampDotted(to, sizeof(to), toEpoch);
  const int n = snprintf(out, cap, "Set the repeater's clock from %s to %s?", from, to);
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

bool coordReplyParse(const char* text, int32_t* e6, CoordAxis axis) {
  if (e6 == nullptr) return false;
  char v[24];
  replyValue(text, v, sizeof(v));
  if (v[0] == 0) return false;
  char* end = nullptr;
  const double deg = strtod(v, &end);
  if (end == v || end == nullptr) return false;
  while (*end == ' ') ++end;
  if (*end != 0) return false;
  // Per-field limits (EST-364): a latitude above 90 degrees is not a latitude, whatever else it may
  // be. These catch a wrong field whenever the swapped value is outside the range (a longitude of
  // 120 cannot become a latitude); the pair rule below is what catches the ones inside it.
  const double limit = axis == CoordAxis::Lat ? 90.0 : (axis == CoordAxis::Lon ? 180.0 : 180.0);
  if (!(deg >= -limit && deg <= limit)) return false;
  const int32_t micro = (int32_t)lround(deg * 1000000.0);
  if (axis == CoordAxis::Lat && (micro < -kCoordLatLimitE6 || micro > kCoordLatLimitE6)) return false;
  if (axis == CoordAxis::Lon && (micro < -kCoordLonLimitE6 || micro > kCoordLonLimitE6)) return false;
  *e6 = micro;
  return true;
}

// ------------------------------------------------------------------------------- the coordinate pair
void CoordRun::reset() { *this = CoordRun(); }

void CoordRun::takeLat(int32_t e6) {
  // A latitude after a completed pair starts a NEW pair: the old longitude belonged to the old read.
  if (lon) reset();
  latE6 = e6;
  lat = true;
}

void CoordRun::takeLon(int32_t e6) {
  // A longitude with no latitude before it means the order was lost: nothing from this run is shown.
  if (!lat) {
    abandon();
    return;
  }
  lonE6 = e6;
  lon = true;
}

void CoordRun::abandon() {
  // The line before this answer never came back, so the answers that follow cannot be paired by
  // order any more. Forgetting the whole run is the honest thing: nothing is shown from it.
  clean = false;
  lat = false;
  lon = false;
  latE6 = 0;
  lonE6 = 0;
}

bool positionNotShared(int32_t latE6, int32_t lonE6) { return latE6 == 0 && lonE6 == 0; }

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

// EST-368: CommonCLI's `gps advert` read-back (v1.18.0, helpers/CommonCLI.cpp): "> none", "> prefs"
// or "> share". Anything else (an error, "unknown command") is false, so the page knows the repeater
// has no such setting and falls back to lat/lon 0.
bool gpsAdvertParse(const char* text, uint8_t* policy) {
  if (policy == nullptr) return false;
  char v[12];
  replyValue(text, v, sizeof(v));
  if (strcmp(v, "none") == 0) { *policy = kAdvertLocNone; return true; }
  if (strcmp(v, "share") == 0) { *policy = kAdvertLocShare; return true; }
  if (strcmp(v, "prefs") == 0) { *policy = kAdvertLocPrefs; return true; }
  return false;
}

// ------------------------------------------------------------------------------- reply/command fit
bool replyFitsCmd(const char* cmd, const char* text) {
  // A CLI answer carries no command name, so the link names it by the order it sent the commands, and
  // the shell can only trust that name as far as the answer's SHAPE agrees with it (EST-364). This
  // is the backstop: an answer that cannot be this command's answer is never parsed as one. An
  // implementation note: every check below is the parser the caller is about to run, so the two can
  // never disagree.
  if (cmd == nullptr || cmd[0] == 0) return false;   // no name at all: never a read-back value
  if (text == nullptr) return false;
  const char* p = text;
  while (*p == ' ' || *p == '\t') ++p;
  // An ERROR is a legal answer to any line (a read can fail too): the caller's parser decides.
  if (strncmp(p, "Err", 3) == 0 || strncmp(p, "Error", 5) == 0) return true;
  // An OK is only a write's answer. A read returns a value, so "OK - clock set: ..." can never be
  // the latitude, the radio or the clock of a read line.
  if (strncmp(p, "OK", 2) == 0) {
    const bool write = strncmp(cmd, "set ", 4) == 0 || strncmp(cmd, "time ", 5) == 0 ||
                       strcmp(cmd, "reboot") == 0 || strncmp(cmd, "region ", 7) == 0 ||
                       strncmp(cmd, "discover.", 9) == 0 || strcmp(cmd, "advert") == 0 ||
                       strcmp(cmd, "advert.zerohop") == 0 || strncmp(cmd, "gps advert", 10) == 0;
    return write;
  }
  int32_t number = 0;
  RadioValues radio;
  bool on = false;
  LoopDetect loop = LoopDetect::Moderate;
  uint32_t epoch = 0;
  if (strcmp(cmd, "get lat") == 0) return coordReplyParse(text, &number, CoordAxis::Lat);
  if (strcmp(cmd, "get lon") == 0) return coordReplyParse(text, &number, CoordAxis::Lon);
  if (strcmp(cmd, "get radio") == 0) return radioReplyParse(text, &radio);
  if (strcmp(cmd, "get repeat") == 0) return switchReplyParse(text, &on);
  if (strcmp(cmd, "get loop.detect") == 0) return loopReplyParse(text, &loop);
  if (strcmp(cmd, "clock") == 0) return clockReplyParse(text, &epoch);
  if (strcmp(cmd, "get tx") == 0 || strcmp(cmd, "get flood.max") == 0 ||
      strcmp(cmd, "get advert.interval") == 0 || strcmp(cmd, "get flood.advert.interval") == 0 ||
      strcmp(cmd, "get path.hash.mode") == 0) {
    return intReplyParse(text, &number);
  }
  // EST-368: the advert location policy's own shape ("> none" / "> prefs" / "> share"). A repeater
  // without the command answers "unknown command", which is NOT an answer to this read-back.
  if (strcmp(cmd, "gps advert") == 0) {
    uint8_t policy = 0;
    return gpsAdvertParse(text, &policy);
  }
  // The remaining read-backs are text of their own shape: a name is one bare line, the region list is
  // one short name per line, and `neighbors` is "prefix:secs:snr" lines (or "-none-").
  if (strcmp(cmd, "neighbors") == 0) {
    return strstr(p, ":") != nullptr || strstr(p, "-none-") != nullptr;
  }
  // `region`, `region home`, `region default`, `get name`, `get owner.info` and every console line:
  // no shape of their own that a different answer could not also have, so nothing is refused here.
  return true;
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
