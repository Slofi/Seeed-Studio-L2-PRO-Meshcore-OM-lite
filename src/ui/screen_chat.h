// SPDX-License-Identifier: MIT
// Chat tab on the new UI layer: the header with the chat selector (the L2 arrangement), the flat
// message list, the compose line, the keyboard sheet that covers the tab bar, the message menu and
// the INFO panel. Draws only into the off-screen frame (chatScreenDraw opens and closes it), and
// hands out the same rectangles for the taps (chatScreenHit) so a screen never repeats a coordinate.
//
// The selector replaces the old chip strip and the three-line button (Filip, 2026-11-10). The
// Tanmatsu keeps its list pane later; the layout is a parameter, so only this L2 arrangement is
// implemented here.
#pragma once

#include <stdint.h>

#include "ui/chat_layout.h"
#include "ui/chat_model.h"
#include "ui/widgets.h"

namespace om {
namespace ui {

constexpr int kMaxChats = 12;   // eight channels plus DMs, matching om::kMaxChannels

struct ChatView {
  StatusModel status;
  const ChatEntry* chats = nullptr;   // channels first, then DMs
  int chatCount = 0;
  int channelCount = 0;
  int dmCount = 0;
  int selChat = 0;                    // the open chat (an index into chats)
  int selScroll = 0;                  // the selector list's scroll, px
  bool selector = false;              // the dropdown is open
  const ChatMsgView* msgs = nullptr;
  int msgCount = 0;
  int scroll = 0;           // px scrolled up from the newest message (0 = at the bottom)
  uint32_t nowSec = 0;      // monotonic uptime; chat ages survive a clock sync
  bool osk = false;
  bool shift = false;
  bool nums = false;
  const char* draft = "";
  uint32_t replySeq = 0;    // held message id, 0 = no reply
  const char* replyName = "";   // copied at the moment REPLY was tapped
  int menuMsg = -1;         // resolved index into msgs, -1 = closed / fell out of the ring
  uint32_t menuSeq = 0;     // held message id, what the tap returns
  int infoMsg = -1;
  uint32_t infoSeq = 0;
  int unread = 0;           // chat badge on the tab bar
};

void chatScreenDraw(const ChatView& v);

enum class ChatHit : uint8_t {
  None, Head, SelRow, SelManage, SelClose, Msg, MenuReply, MenuInfo, MenuCopy, MenuClose, InfoClose,
  Field, Key, Outside,
};

struct ChatHitResult {
  ChatHit hit = ChatHit::None;
  int index = -1;       // chat entry or message index (a snapshot, never held)
  uint32_t seq = 0;     // message id to hold for REPLY / INFO / the menu
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

// The selector list's scroll clamp and the row under a y. The shell drags the list with the finger.
int chatSelectorMaxScroll(const ChatView& v);

}  // namespace ui
}  // namespace om
