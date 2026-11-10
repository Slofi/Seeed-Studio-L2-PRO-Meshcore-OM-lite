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
}
#endif  // OM_ENABLE_MESH
