// SPDX-License-Identifier: MIT
// EST-358: the RPTR radio link. Everything that touches MeshCore lives here: upstream's own
// discovery (the control-data DISCOVER request), its login (BaseChatMesh::sendLogin), its status
// request (sendRequest REQ_TYPE_GET_STATUS) and its repeater CLI path (sendCommandData with
// TXT_TYPE_CLI_DATA). No lib/ file is edited: all four are public MeshCore calls, and the answers
// are caught in mesh_link.cpp's listener (the four hooks at the bottom of this header).
//
// The shell (main.cpp) never formats a packet and never parses one; it reads this facade and draws.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "rptr_model.h"

#if defined(OM_ENABLE_MESH)
namespace mesh {
class Packet;
}
class ContactInfo;

namespace om {

// One queued answer from the radio, in the order it arrived. `kind` is a rptr::ReplyKind-like byte
// so the shell can tell a CLI answer from a status reply; `cmd` is the line the shell sent (copied
// when it was sent, so an answer always names its own control).
struct RptrReply {
  uint8_t kind = 0;
  char cmd[48] = "";
  char text[160] = "";
  uint32_t atMs = 0;
};

enum class RptrReplyKind : uint8_t { Cli = 1, Status = 2, Login = 3, Error = 4 };

bool rptrLinkBegin();   // NVS: the remembered repeaters and the auto-login switch
void rptrLinkLoop();    // the scan window and the login deadline

// --- scan (the companion's control-data DISCOVER request) ----------------------------------------
// Sends one zero-hop request with a random tag; every repeater in range answers once. Listening ends
// after rptr::kScanWindowMs. False when the mesh is not up or the packet pool is empty (nothing went
// on the air, and found().listening stays false).
bool rptrScanStart();
const rptr::FoundList& rptrScan();
uint32_t rptrScanSerial();

// --- the open repeater session -------------------------------------------------------------------
// `rptrLoginStart` looks the name up in MeshCore's contact table and sends upstream's login. On a
// successful answer the password is remembered for that repeater (NVS), so the next visit can log in
// by itself; the password is never logged and never handed to a reply line.
bool rptrLoginStart(const char* name, const char* password);
void rptrLogout();
bool rptrLoggedIn();
const char* rptrCurrentName();
const rptr::LoginMachine& rptrLogin();
// The login sheet's one-line result: "admin · 1.2 s", "no answer (wrong password or out of range)".
void rptrLoginResultText(char* out, size_t cap);

// --- status after login -------------------------------------------------------------------------
bool rptrStatusRequest();
const rptr::Status& rptrStatus();
uint32_t rptrStatusSerial();

// --- the repeater CLI ---------------------------------------------------------------------------
// One line, sent through upstream's CLI path (sendCommandData, TXT_TYPE_CLI_DATA). `tag` comes back
// in the answer, so the shell can route a `neighbors` reply to the neighbour list and a `region`
// reply to the region list. False when nothing went on the air.
bool rptrCliSend(const char* line, uint8_t tag);
const char* rptrLastCmd();
uint32_t rptrReplySerial();
// Oldest queued answer, or false when the queue is empty.
bool rptrReplyTake(RptrReply* out);

// --- remembered repeaters (NVS) ------------------------------------------------------------------
// Up to four, each {name, password}. `rptrPasswordFor` is the ONLY way to read one back and only the
// link and the auto-login use it; no caller prints it.
int rptrRememberedCount();
const char* rptrRememberedName(int i);
const char* rptrLastRemembered();
bool rptrPasswordFor(const char* name, char* out, size_t cap);
bool rptrForget(const char* name);
bool rptrAutoLogin();
void rptrSetAutoLogin(bool on);

// --- listener hooks, called from mesh_link.cpp ---------------------------------------------------
// A control-data packet (the discovery answer). Returns true when it was ours (matched the tag), so
// the phone's frame path can leave it alone.
bool rptrOnControlData(const mesh::Packet* pkt);
// An unmatched response: the login answer, the status reply, or something else. `handled` is set
// true only for our own answers.
void rptrOnResponse(const ContactInfo& from, uint32_t tag, const uint8_t* data, uint8_t len,
                    bool* handled);
// A TXT_TYPE_CLI_DATA message: the repeater's CLI answer.
void rptrOnCliReply(const char* fromName, const char* text);

}  // namespace om
#endif  // OM_ENABLE_MESH
