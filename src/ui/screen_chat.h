// SPDX-License-Identifier: MIT
// Chat tab on the new UI layer: channel chips, the flat message list, the compose line, the keyboard
// sheet that covers the tab bar, the message menu and the INFO panel. Draws only into the off-screen
// frame (chatScreenDraw opens and closes it), and hands out the same rectangles for the taps
// (chatScreenHit) so a screen never repeats a coordinate.
#pragma once

#include <stdint.h>

#include "ui/chat_layout.h"
#include "ui/widgets.h"

namespace om {
namespace ui {

// MeshCore's ring in mesh_link.cpp keeps 32 messages; the view never has more than that.
constexpr int kMaxChatMsgs = 32;
constexpr int kMaxChips = 12;   // eight channels plus DMs, matching om::kMaxChannels

struct ChatChip {
  char label[28] = "";
  uint16_t unread = 0;
  bool dm = false;
  bool muted = false;
};

struct ChatMsgView {
  int16_t meshIdx = -1;     // index in the mesh ring, for REPLY / INFO / COPY
  const char* name = "";
  const char* text = "";
  const char* chanName = "";
  uint32_t ts = 0;          // uptime seconds when it was stored
  int8_t hops = -1;         // packet hops, -1 = local echo / unknown
  int8_t snrq = 0;          // SNR x 4 (MeshCore's unit), 0 = unknown
  bool mine = false;
};

struct ChatView {
  StatusModel status;
  const ChatChip* chips = nullptr;
  int chipCount = 0;
  int selChip = 0;
  int chipScroll = 0;
  const ChatMsgView* msgs = nullptr;
  int msgCount = 0;
  int scroll = 0;           // px scrolled up from the newest message (0 = at the bottom)
  uint32_t nowSec = 0;
  bool osk = false;
  bool shift = false;
  bool nums = false;
  const char* draft = "";
  int replyMsg = -1;        // index into msgs
  int menuMsg = -1;
  int infoMsg = -1;
  int unread = 0;           // chat badge on the tab bar
};

void chatScreenDraw(const ChatView& v);

enum class ChatHit : uint8_t {
  None, Chip, ChipMenu, Msg, MenuReply, MenuInfo, MenuCopy, MenuClose, InfoClose,
  Field, KbdBtn, Send, Key, Outside,
};

struct ChatHitResult {
  ChatHit hit = ChatHit::None;
  int index = -1;       // chip or message index
  int keyRow = -1;
  int keyCol = -1;
  KeyKind keyKind = KeyKind::Char;
  char keyCh = 0;
};

// What a tap at (x, y) would hit. Pure read: it measures text but changes nothing.
ChatHitResult chatScreenHit(const ChatView& v, int x, int y);

// Message band arithmetic for the shell: the content height is what decides whether a new message
// may move the view (only when the reader is already at the bottom), and maxScroll clamps a drag.
int chatContentHeight(const ChatView& v);
int chatMaxScroll(const ChatView& v);

// The chip strip scroll that brings chip i completely into view (clamped).
int chatChipScrollInto(const ChatView& v, int chip);

}  // namespace ui
}  // namespace om
