// SPDX-License-Identifier: MIT
// OM Lite <-> MeshCore channel link.
#pragma once

#include <Arduino.h>

#if defined(OM_ENABLE_MESH)
namespace om {
bool meshLinkBegin();
void meshLinkLoop();
bool meshLinkReady();
const char* meshLinkLastMessage();
int meshLinkLastChannel();
bool meshLinkSend(int channelIdx, const char* text);
// Re-apply the channel table to the running mesh (after an add, remove or rename). Safe to call any
// time after meshLinkBegin(); unused slots are emptied so a removed channel stops matching.
bool meshLinkReloadChannels();

// Recent channel traffic for the UI (oldest first).
int meshMsgCount();
uint32_t meshMsgSerial();   // monotonic: changes on every stored message, even when the ring is full
const char* meshMsgText(int idx);
int meshMsgChannel(int idx);
const char* meshMsgName(int idx);
bool meshMsgMine(int idx);
uint32_t meshMsgTs(int idx);   // uptime seconds when it was stored (there is no RTC on this board)
int meshMsgHops(int idx);      // path hops the packet carried, -1 = local echo / unknown
int meshMsgSnrQ(int idx);      // SNR x 4 as int8 (MeshCore's own unit), 0 = unknown
int meshUnread(int channelIdx);
void meshClearUnread(int channelIdx);

// --- node DB for the Nodes screen -----------------------------------------------------------------
// The MeshCore contact table, read-only. type is MeshCore's ADV_TYPE_* (1 chat/client, 2 repeater,
// 3 room, 4 sensor). path_len is ContactInfo::out_path_len in bytes (0 = direct, 0xFF = unknown);
// this firmware does not get a per-contact RSSI from MeshCore, so the UI must not invent one.
struct MeshContact {
  char name[32];
  uint8_t type;
  uint8_t path_len;
  uint32_t lastmod;      // MeshCore RTC seconds when it was last heard
  bool has_pos;
  int32_t lat_e6;
  int32_t lon_e6;
};

// Newest first, at most max. Returns how many valid contacts were written (0 is a real answer:
// nothing has been heard yet).
int meshContactList(MeshContact* out, int max);
// Cheap change detector: count, names, types and lastmod. Changes when the list would change.
uint32_t meshContactSignature();
// MeshCore's current clock, for age arithmetic. 0 when the mesh is not up.
uint32_t meshNow();
}
#endif  // OM_ENABLE_MESH
