// SPDX-License-Identifier: MIT
// EST-358: the RPTR radio link, see rptr_link.h. Every call into MeshCore here is a public API:
//   discovery   Mesh::createControlData() + Mesh::sendZeroHop()  (the companion's own
//               CMD_SEND_CONTROL_DATA handler is exactly createControlData(&frame[1], len-1) +
//               sendZeroHop, MyMesh.cpp)
//   login       BaseChatMesh::sendLogin()   (the CMD_SEND_LOGIN handler's own call)
//   status      BaseChatMesh::sendRequest(REQ_TYPE_GET_STATUS)   (the CMD_SEND_STATUS_REQ call)
//   CLI         BaseChatMesh::sendCommandData(..., TXT_TYPE_CLI_DATA, line)   (the CMD_SEND_TXT_MSG
//               path for a CLI text frame)
// No lib/ file is edited. The answers are caught by mesh_link.cpp's listener.
#include "rptr_link.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include <Mesh.h>
#include <helpers/BaseChatMesh.h>

#include "MyMesh.h"   // the_mesh (declared extern there) and ContactInfo
#include "mesh_link.h"   // meshLinkReady()
#include "timekeeper.h"  // the heard stamp a scan row shows

namespace om {

// mesh_link.cpp owns the one MyMesh and keeps it file-local; this is its only door out (defined in
// mesh_link.cpp next to meshLinkBegin).
MyMesh& rptrMesh();

namespace {

using namespace rptr;

// The discovery control-data types (MyMesh.h of the companion and every MeshCore example).
constexpr uint8_t kCtlDiscoverReq = 0x80;
constexpr uint8_t kCtlDiscoverResp = 0x90;
// The request's filter byte: bit N asks nodes of ADV_TYPE_N.
constexpr uint8_t kDiscoverFilterRepeater = (uint8_t)(1u << ADV_TYPE_REPEATER);

FoundList s_found;
LoginMachine s_login;
Status s_status;
uint32_t s_statusSerial = 0;
uint32_t s_statusTag = 0;
bool s_statusPending = false;
uint32_t s_statusSentMs = 0;
uint32_t s_statusDeadlineMs = 0;

char s_pendingPassword[kPasswordMax + 1] = "";   // held only until the answer, never logged
char s_currentName[kNameMax + 1] = "";
uint8_t s_currentKey[PUB_KEY_SIZE] = {};
bool s_haveCurrent = false;

// The reply queue: the radio is asynchronous and the shell draws once per frame, so answers are
// queued and taken by the draw. Four is more than one admin action can produce at once.
constexpr int kReplyQueue = 4;
RptrReply s_replies[kReplyQueue];
int s_replyHead = 0;
int s_replyCount = 0;
uint32_t s_replySerial = 0;
char s_lastCmd[48] = "";
// The CLI reply carries no tag, so the only link to the command is ORDER: every line sent is kept
// here and the answer takes the oldest one. A multi-line action (SI preset, silent, apply) then
// names the right line for each reply instead of always the last one sent.
constexpr int kPendingCmds = 8;
char s_pending[kPendingCmds][48];
int s_pendingHead = 0;
int s_pendingCount = 0;

void pushPending(const char* line) {
  snprintf(s_pending[(s_pendingHead + s_pendingCount) % kPendingCmds], sizeof(s_pending[0]), "%s",
           line);
  if (s_pendingCount < kPendingCmds) {
    ++s_pendingCount;
  } else {
    s_pendingHead = (s_pendingHead + 1) % kPendingCmds;   // drop the oldest
  }
}

bool popPending(char* out, size_t cap) {
  if (s_pendingCount <= 0) return false;
  snprintf(out, cap, "%s", s_pending[s_pendingHead]);
  s_pendingHead = (s_pendingHead + 1) % kPendingCmds;
  --s_pendingCount;
  return true;
}

void queueReply(uint8_t kind, const char* cmd, const char* text, uint32_t nowMs) {
  RptrReply& r = s_replies[(s_replyHead + s_replyCount) % kReplyQueue];
  r.kind = kind;
  snprintf(r.cmd, sizeof(r.cmd), "%s", cmd == nullptr ? "" : cmd);
  snprintf(r.text, sizeof(r.text), "%s", text == nullptr ? "" : text);
  r.atMs = nowMs;
  if (s_replyCount < kReplyQueue) {
    ++s_replyCount;
  } else {
    s_replyHead = (s_replyHead + 1) % kReplyQueue;   // drop the oldest
  }
  ++s_replySerial;
}

// --- the remembered repeaters (NVS) --------------------------------------------------------------
// Four entries, "e0".."e3", each { char name[32]; char password[16]; }. The password is stored
// because auto-login needs it; no log line and no USB reply ever prints it.
constexpr int kRemembered = 4;
constexpr uint32_t kRememberedMagic = 0x52505431;   // "RPT1"
struct Remembered {
  uint32_t magic;
  char name[kNameMax + 1];
  char password[kPasswordMax + 1];
};
Remembered s_mem[kRemembered];
bool s_autoLogin = false;
Preferences s_prefs;
bool s_prefsOk = false;

void loadRemembered() {
  for (int i = 0; i < kRemembered; ++i) {
    s_mem[i] = Remembered();
  }
  if (!s_prefsOk) return;
  for (int i = 0; i < kRemembered; ++i) {
    char key[4] = {'e', (char)('0' + i), 0, 0};
    if (s_prefs.getBytesLength(key) != sizeof(Remembered)) continue;
    Remembered r;
    if (s_prefs.getBytes(key, &r, sizeof(r)) != sizeof(r)) continue;
    if (r.magic != kRememberedMagic || r.name[0] == 0) continue;
    r.name[kNameMax] = 0;
    r.password[kPasswordMax] = 0;
    s_mem[i] = r;
  }
  s_autoLogin = s_prefs.getBool("auto", false);
}

void saveRemembered(int i) {
  if (!s_prefsOk) return;
  char key[4] = {'e', (char)('0' + i), 0, 0};
  s_mem[i].magic = kRememberedMagic;
  s_prefs.putBytes(key, &s_mem[i], sizeof(Remembered));
}

int findRemembered(const char* name) {
  if (name == nullptr || name[0] == 0) return -1;
  for (int i = 0; i < kRemembered; ++i) {
    if (s_mem[i].magic == kRememberedMagic && strcmp(s_mem[i].name, name) == 0) return i;
  }
  return -1;
}

// --- contact lookup ------------------------------------------------------------------------------
bool findRepeater(const char* name, ContactInfo* out) {
  if (name == nullptr || name[0] == 0 || out == nullptr) return false;
  const int total = rptrMesh().getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!rptrMesh().getContactByIdx((uint32_t)idx, ci)) continue;
    if (ci.type == ADV_TYPE_NONE || ci.name[0] == 0) continue;
    if (strcmp(ci.name, name) == 0) {
      *out = ci;
      return true;
    }
  }
  return false;
}

// The name of the contact whose public key starts with these bytes, or "" when none does.
const char* contactNameForKey(const uint8_t* key, int keyLen, bool* inContacts) {
  static char s_name[kNameMax + 1];
  s_name[0] = 0;
  if (inContacts) *inContacts = false;
  if (key == nullptr || keyLen <= 0) return s_name;
  const int total = rptrMesh().getTotalContactSlots();
  for (int idx = MAX_ANON_CONTACTS; idx < total; ++idx) {
    ContactInfo ci;
    if (!rptrMesh().getContactByIdx((uint32_t)idx, ci)) continue;
    if (ci.type == ADV_TYPE_NONE || ci.name[0] == 0) continue;
    if (memcmp(ci.id.pub_key, key, (size_t)keyLen) == 0) {
      snprintf(s_name, sizeof(s_name), "%s", ci.name);
      if (inContacts) *inContacts = true;
      return s_name;
    }
  }
  return s_name;
}

void hexPrefix(const uint8_t* key, int n, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  size_t at = 0;
  out[0] = 0;
  for (int i = 0; i < n && at + 3 < cap; ++i) {
    at += (size_t)snprintf(out + at, cap - at, "%02x", key[i]);
  }
}

// True when this response is one of ours: the sender is the open repeater. A login answer and a
// status reply both come from that contact.
bool fromCurrent(const ContactInfo& from) {
  return s_haveCurrent && memcmp(from.id.pub_key, s_currentKey, PUB_KEY_SIZE) == 0;
}

}  // namespace

bool rptrLinkBegin() {
  s_prefsOk = s_prefs.begin("rptr", false);
  if (!s_prefsOk) {
    Serial.println("[rptr] NVS not available: the remembered repeaters will not survive a reboot");
  }
  loadRemembered();
  int n = 0;
  for (int i = 0; i < kRemembered; ++i) {
    if (s_mem[i].magic == kRememberedMagic) ++n;
  }
  Serial.printf("[rptr] ready: %d remembered repeater(s), auto-login %s\n", n,
                s_autoLogin ? "on" : "off");
  return true;
}

void rptrLinkLoop() {
  const uint32_t now = millis();
  s_found.tick(now);
  if (s_login.tick(now)) {
    Serial.printf("[rptr] login to %s: no answer within %lu ms\n", s_currentName,
                  (unsigned long)(now - s_login.sentMs));
    queueReply((uint8_t)RptrReplyKind::Error, "login",
               "no answer (wrong password, or out of range)", now);
  }
  if (s_statusPending && (int32_t)(now - s_statusDeadlineMs) >= 0) {
    s_statusPending = false;
    queueReply((uint8_t)RptrReplyKind::Error, "status", "no answer", now);
    Serial.printf("[rptr] status from %s: no answer\n", s_currentName);
  }
}

// ----------------------------------------------------------------------------------------------- scan
bool rptrScanStart() {
  if (!meshLinkReady()) return false;
  uint32_t tag = 0;
  rptrMesh().getRNG()->random((uint8_t*)&tag, sizeof(tag));
  uint8_t data[10];
  data[0] = kCtlDiscoverReq;   // prefix_only = 0: ask for the whole public key
  data[1] = kDiscoverFilterRepeater;
  memcpy(&data[2], &tag, 4);
  uint32_t since = 0;
  memcpy(&data[6], &since, 4);
  auto pkt = rptrMesh().createControlData(data, sizeof(data));
  if (pkt == nullptr) {
    Serial.println("[rptr] scan: no packet (pool empty)");
    return false;
  }
  rptrMesh().sendZeroHop(pkt);
  s_found.begin(tag, millis());
  Serial.printf("[rptr] scan: discover request tag %08lx, listening %lu ms\n", (unsigned long)tag,
                (unsigned long)kScanWindowMs);
  return true;
}

const FoundList& rptrScan() { return s_found; }
uint32_t rptrScanSerial() { return s_found.serial; }

bool rptrOnControlData(const mesh::Packet* pkt) {
  if (pkt == nullptr || pkt->payload_len < 6) return false;
  const uint8_t* p = pkt->payload;
  if ((p[0] & 0xF0) != kCtlDiscoverResp) return false;
  const uint8_t nodeType = (uint8_t)(p[0] & 0x0F);
  if (nodeType != ADV_TYPE_REPEATER) return false;
  uint32_t tag = 0;
  memcpy(&tag, &p[2], 4);
  if (!s_found.listening || tag != s_found.tag) return false;   // another scan's answer
  const int keyLen = pkt->payload_len - 6;
  if (keyLen < 6) return false;
  const uint8_t* key = &p[6];
  const int usable = keyLen >= 6 ? 6 : keyLen;   // the row is keyed on six bytes
  bool inContacts = false;
  const char* name = contactNameForKey(key, keyLen, &inContacts);
  const bool hasIn = true;                      // the response always carries it (x4)
  const int8_t snrIn4 = (int8_t)p[1];
  const bool hasOut = true;
  const int8_t snrOut4 = (int8_t)lroundf(pkt->getSNR() * 4.0f);
  uint8_t hops = 0;
  if (pkt->getPathHashCount() > 0) hops = pkt->getPathHashCount();
  const int at = s_found.addAnswer(key, name, inContacts, hasIn, snrIn4, hasOut, snrOut4, hops,
                                   millis());
  if (at >= 0) {
    s_found.rows[at].heardWall = timeKeeper().now();
    s_found.rows[at].heardReal = timeKeeper().realSinceBoot();
  }
  char hex[16];
  hexPrefix(key, usable, hex, sizeof(hex));
  if (at >= 0) {
    Serial.printf("[rptr] answer %s (%s): snr in %d/4 out %d/4, %d found\n",
                  name[0] ? name : hex, hex, (int)snrIn4, (int)snrOut4, s_found.count);
  } else {
    Serial.printf("[rptr] answer %s dropped: the list is full\n", name[0] ? name : hex);
  }
  return true;
}

// ---------------------------------------------------------------------------------------------- login
bool rptrLoginStart(const char* name, const char* password) {
  if (!meshLinkReady()) return false;
  ContactInfo ci;
  if (!findRepeater(name, &ci)) {
    queueReply((uint8_t)RptrReplyKind::Error, "login", "no such repeater contact", millis());
    return false;
  }
  if (password == nullptr || password[0] == 0) {
    queueReply((uint8_t)RptrReplyKind::Error, "login", "password required", millis());
    return false;
  }
  uint32_t est = 0;
  const int rc = rptrMesh().sendLogin(ci, password, est);
  if (rc == MSG_SEND_FAILED) {
    queueReply((uint8_t)RptrReplyKind::Error, "login", "send failed (packet pool full)", millis());
    return false;
  }
  snprintf(s_currentName, sizeof(s_currentName), "%s", ci.name);
  snprintf(s_pendingPassword, sizeof(s_pendingPassword), "%s", password);
  memcpy(s_currentKey, ci.id.pub_key, PUB_KEY_SIZE);
  s_haveCurrent = true;
  s_login.begin(millis(), est);
  s_status = Status();
  s_statusPending = false;
  s_pendingHead = 0;
  s_pendingCount = 0;
  Serial.printf("[rptr] login sent to %s (%s), deadline %lu ms\n", s_currentName,
                rc == MSG_SEND_SENT_FLOOD ? "flood" : "direct", (unsigned long)est);
  return true;
}

void rptrLogout() {
  s_login.reset();
  s_pendingPassword[0] = 0;
  s_haveCurrent = false;
  s_currentName[0] = 0;
  memset(s_currentKey, 0, sizeof(s_currentKey));
  s_statusPending = false;
  s_status = Status();
  s_pendingHead = 0;
  s_pendingCount = 0;
  s_lastCmd[0] = 0;
  Serial.println("[rptr] session closed");
}

bool rptrLoggedIn() { return s_haveCurrent && s_login.loggedIn(); }
const char* rptrCurrentName() { return s_currentName; }
const LoginMachine& rptrLogin() { return s_login; }

void rptrLoginResultText(char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = 0;
  switch (s_login.state) {
    case LoginState::Idle:
      snprintf(out, cap, "not logged in");
      break;
    case LoginState::Sending:
      snprintf(out, cap, "sending...");
      break;
    case LoginState::Admin:
    case LoginState::Guest: {
      char rtt[24] = "";
      if (s_login.haveRtt) snprintf(rtt, sizeof(rtt), " \\xC2\\xB7 %lu.%01lu s",
                                    (unsigned long)(s_login.rttMs / 1000),
                                    (unsigned long)((s_login.rttMs % 1000) / 100));
      snprintf(out, cap, "%s%s", loginStateText(s_login.state), rtt);
      break;
    }
    case LoginState::Refused:
      snprintf(out, cap, "refused by the repeater");
      break;
    case LoginState::NoAnswer:
      snprintf(out, cap, "no answer (wrong password, or out of range)");
      break;
    default:
      break;
  }
}

// --------------------------------------------------------------------------------------------- status
bool rptrStatusRequest() {
  if (!s_haveCurrent || !rptrLoggedIn()) return false;
  ContactInfo ci;
  if (!findRepeater(s_currentName, &ci)) return false;
  uint32_t tag = 0;
  uint32_t est = 0;
  const int rc = rptrMesh().sendRequest(ci, REQ_TYPE_GET_STATUS, tag, est);
  if (rc == MSG_SEND_FAILED) return false;
  s_statusTag = tag;
  s_statusPending = true;
  s_statusSentMs = millis();
  s_statusDeadlineMs = s_statusSentMs + est + kLoginExtraMs;
  Serial.printf("[rptr] status request to %s, deadline %lu ms\n", s_currentName,
                (unsigned long)est);
  return true;
}

const Status& rptrStatus() { return s_status; }
uint32_t rptrStatusSerial() { return s_statusSerial; }

// ------------------------------------------------------------------------------------------------ CLI
bool rptrCliSend(const char* line, uint8_t tag) {
  if (!s_haveCurrent || !rptrLoggedIn()) return false;
  if (line == nullptr || line[0] == 0) return false;
  ContactInfo ci;
  if (!findRepeater(s_currentName, &ci)) return false;
  uint32_t est = 0;
  // MeshCore's own CLI timestamp rule (MyMesh's CMD_SEND_TXT_MSG for a CLI type): the node's RTC,
  // unique, so the repeater's replay guard accepts each command.
  const uint32_t ts = rptrMesh().getRTCClock()->getCurrentTimeUnique();
  const int rc = rptrMesh().sendCommandData(ci, ts, 0, TXT_TYPE_CLI_DATA, line, est);
  if (rc == MSG_SEND_FAILED) return false;
  snprintf(s_lastCmd, sizeof(s_lastCmd), "%s", line);
  pushPending(line);
  (void)tag;   // the CLI frame carries no tag; the pending ring above is how a reply names it
  Serial.printf("[rptr] cli -> %s: %s\n", s_currentName, line);
  return true;
}

const char* rptrLastCmd() { return s_lastCmd; }
uint32_t rptrReplySerial() { return s_replySerial; }

bool rptrReplyTake(RptrReply* out) {
  if (out == nullptr || s_replyCount <= 0) return false;
  *out = s_replies[s_replyHead];
  s_replyHead = (s_replyHead + 1) % kReplyQueue;
  --s_replyCount;
  return true;
}

// ----------------------------------------------------------------------------------- listener hooks
void rptrOnResponse(const ContactInfo& from, uint32_t tag, const uint8_t* data, uint8_t len,
                    bool* handled) {
  if (handled) *handled = false;
  if (data == nullptr || len == 0) return;

  // The login answer. sendLogin() does not set MyMesh's own pending_login (that path belongs to the
  // phone's CMD_SEND_LOGIN), so the answer arrives here. Its first four bytes are the repeater's
  // clock, which the login sheet shows.
  if (s_login.busy() && fromCurrent(from)) {
    LoginAnswer a;
    if (loginParse(data, (int)len, &a)) {
      a.clock = tag;
      a.clockValid = true;
      s_login.succeed(a, millis());
      Serial.printf("[rptr] login %s: %s (%lu ms), fw level %u, permissions %u\n", s_currentName,
                    loginStateText(s_login.state), (unsigned long)s_login.rttMs,
                    (unsigned)a.fwLevel, (unsigned)a.permissions);
      queueReply((uint8_t)RptrReplyKind::Login, "login", loginStateText(s_login.state), millis());
      if (handled) *handled = true;
      if (s_login.loggedIn() && s_pendingPassword[0]) {
        // Remember this repeater and its password: the next visit logs in by itself. The password
        // is written to NVS and never printed anywhere.
        int slot = findRemembered(s_currentName);
        if (slot < 0) {
          slot = 0;   // all four used: the first slot is replaced
          for (int i = 0; i < kRemembered; ++i) {
            if (s_mem[i].magic != kRememberedMagic) {
              slot = i;
              break;
            }
          }
        }
        s_mem[slot] = Remembered();
        snprintf(s_mem[slot].name, sizeof(s_mem[slot].name), "%s", s_currentName);
        snprintf(s_mem[slot].password, sizeof(s_mem[slot].password), "%s", s_pendingPassword);
        saveRemembered(slot);
        Serial.printf("[rptr] remembered %s (password saved, never printed)\n", s_currentName);
      }
      s_pendingPassword[0] = 0;
      return;
    }
  }

  // The status reply. sendRequest() gives us a tag; a response whose first four bytes are that
  // timestamp and whose payload is a RepeaterStats is ours.
  if (s_statusPending && tag == s_statusTag && fromCurrent(from) && len >= 4 + kRepeaterStatsBytes) {
    Status st;
    if (statusParse(data, (int)len, &st)) {
      st.atMs = millis();
      st.rttMs = st.atMs - s_statusSentMs;
      st.haveRtt = true;
      st.fwLevel = s_login.answer.fwLevel;
      st.clock = s_login.answer.clock;
      st.clockValid = s_login.answer.clockValid;
      s_status = st;
      s_statusPending = false;
      ++s_statusSerial;
      char text[64];
      snprintf(text, sizeof(text), "up %lu s, %u mV", (unsigned long)st.uptimeSec,
               (unsigned)st.battMv);
      queueReply((uint8_t)RptrReplyKind::Status, "status", text, st.atMs);
      Serial.printf("[rptr] status %s: up %lu s, batt %u mV, air %lu s, rx %lu tx %lu, noise %d\n",
                    s_currentName, (unsigned long)st.uptimeSec, (unsigned)st.battMv,
                    (unsigned long)st.airtimeSec, (unsigned long)st.rxPackets,
                    (unsigned long)st.txPackets, (int)st.noiseFloor);
      if (handled) *handled = true;
      return;
    }
  }
}

void rptrOnCliReply(const char* fromName, const char* text) {
  if (!s_haveCurrent) return;
  if (fromName != nullptr && fromName[0] && strcmp(fromName, s_currentName) != 0) {
    // A CLI answer from another node (a DM with a repeater we are not managing): not ours.
    return;
  }
  char cmd[48] = "";
  if (!popPending(cmd, sizeof(cmd))) snprintf(cmd, sizeof(cmd), "%s", s_lastCmd);
  queueReply((uint8_t)RptrReplyKind::Cli, cmd, text, millis());
  Serial.printf("[rptr] cli <- %s: %s\n", s_currentName, text == nullptr ? "" : text);
}

// ---------------------------------------------------------------------------------- remembered list
int rptrRememberedCount() {
  int n = 0;
  for (int i = 0; i < kRemembered; ++i) {
    if (s_mem[i].magic == kRememberedMagic && s_mem[i].name[0]) ++n;
  }
  return n;
}

const char* rptrRememberedName(int i) {
  int at = 0;
  for (int k = 0; k < kRemembered; ++k) {
    if (s_mem[k].magic != kRememberedMagic || s_mem[k].name[0] == 0) continue;
    if (at == i) return s_mem[k].name;
    ++at;
  }
  return "";
}

const char* rptrLastRemembered() {
  const int n = rptrRememberedCount();
  return n > 0 ? rptrRememberedName(0) : "";
}

bool rptrPasswordFor(const char* name, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = 0;
  const int at = findRemembered(name);
  if (at < 0 || s_mem[at].password[0] == 0) return false;
  snprintf(out, cap, "%s", s_mem[at].password);
  return true;
}

bool rptrForget(const char* name) {
  const int at = findRemembered(name);
  if (at < 0) return false;
  s_mem[at] = Remembered();
  if (s_prefsOk) {
    char key[4] = {'e', (char)('0' + at), 0, 0};
    s_prefs.remove(key);
  }
  Serial.printf("[rptr] forgot %s\n", name);
  return true;
}

bool rptrAutoLogin() { return s_autoLogin; }

void rptrSetAutoLogin(bool on) {
  s_autoLogin = on;
  if (s_prefsOk) s_prefs.putBool("auto", on);
  Serial.printf("[rptr] auto-login %s\n", on ? "on" : "off");
}

}  // namespace om
