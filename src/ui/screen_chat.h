// SPDX-License-Identifier: MIT
// Chat tab on the new UI layer: the header with the chat selector (the L2 arrangement), the flat
// message list, the compose line, the keyboard sheet that covers the tab bar, and the message sheet
// a tap on a message opens (its header carries sender, channel, age, hops and SNR, so the old INFO
// step is gone). Draws only into the off-screen frame (chatScreenDraw opens and closes it), and
// hands out the same rectangles for the taps (chatScreenHit) so a screen never repeats a coordinate.
//
// The selector replaces the old chip strip and the three-line button (Filip, 2026-11-10). The
// Tanmatsu keeps its list pane later; the layout is a parameter, so only this L2 arrangement is
// implemented here.
#pragma once

#include <stdint.h>

#include "ui/chat_layout.h"
#include "ui/chat_model.h"
#include "ui/chat_view.h"   // EST-367: kChatViewMsgs, the size of one chat's own view
#include "ui/message_sheet.h"
#include "ui/press.h"
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
  // EST-382: the ADVERT key's small Flood / Local dropdown is open. The shell keeps it exclusive with
  // `selector`: opening one closes the other.
  bool advertMenu = false;
  const ChatMsgView* msgs = nullptr;
  int msgCount = 0;
  // EST-367: the chat's own newest page is being read from the card (the deferred open). The first
  // frame after the tap says so instead of "No messages yet".
  bool loading = false;
  int scroll = 0;           // px scrolled up from the newest message (0 = at the bottom)
  uint32_t nowSec = 0;      // monotonic uptime; chat ages survive a clock sync
  uint32_t nowWall = 0;     // the system clock now (real only when nowReal)
  bool nowReal = false;     // a real source has set the clock since boot
  bool osk = false;
  bool shift = false;
  bool nums = false;
  bool emoji = false;               // the smiley page is open (replaces the four key rows)
  // The accent preview (EST-346): while a long press on s, c or z is past the threshold, the sheet
  // draws this letter in a bubble above that key. -1 / 0 when nothing is held.
  int accentRow = -1;
  int accentCol = -1;
  uint32_t accentCp = 0;
  const uint32_t* emojiRecent = nullptr;   // last 16 picks, most recent first
  int emojiRecentCount = 0;
  int emojiGroup = 0;               // faces / hands / hearts / objects / symbols
  int emojiScroll = 0;              // the selected group's grid scroll, px
  const char* draft = "";
  const char* ownName = "OM Lite";   // MeshCore NodePrefs name, for the composer byte budget
  bool dmThread = false;             // the open chat is a DM: its frame has no "name: " prefix
  uint32_t replySeq = 0;    // held message id, 0 = no reply
  const char* replyName = "";   // copied at the moment REPLY was tapped
  int menuMsg = -1;         // resolved index into msgs, -1 = closed / fell out of the ring
  uint32_t menuSeq = 0;     // held message id, what the tap returns
  // What the open message's sheet needs, resolved by the shell from the mesh facade. A row exists
  // only when its fact is true (message_sheet.h builds the list from these).
  const uint8_t* menuPath = nullptr;   // the packet's path, as it arrived
  uint8_t menuPathSize = 1;
  uint8_t menuPathCount = 0;
  int menuHeard = 0;        // own message: times we heard our packet repeated by others
  bool menuSenderKnown = false;
  bool menuSenderHasPos = false;
  bool menuSenderHasKey = false;   // the sender's public key is known: a DM can be encrypted to
  bool menuInDm = false;           // the sheet is already open inside that sender's DM thread
  // EST-365: the tab bar's badge (and the gold new-node dot), built by the shell once per view.
  TabBarModel tabs;
};

void chatScreenDraw(const ChatView& v);
// S499: only the keyboard sheet, drawn over the kept frame (ui/chat_redraw.h says when that is safe).
void chatSheetDraw(const ChatView& v);

enum class ChatHit : uint8_t {
  None, Head, SelRow, SelManage, SelClose, Msg, SenderName, SheetRow, SheetClose, Field, Key, Outside,
  // EST-406: SendDraft is the composer's send button on the draft line. EmojiSend is gone: the emoji
  // page sends through that one button, which stays visible above the emoji grid.
  EmojiCell, EmojiCat, EmojiBack, SendDraft,
  // EST-382: the ADVERT key and its two options. AdvertClose is a point in the menu that is not an
  // option (or a stray tap while it is open): it only closes the menu, never acts behind it.
  Advert, AdvertFlood, AdvertLocal, AdvertClose,
};

struct ChatHitResult {
  ChatHit hit = ChatHit::None;
  int index = -1;       // chat entry or message index (a snapshot, never held)
  uint32_t seq = 0;     // message id to hold for REPLY / the message sheet
  SheetAction sheet = SheetAction::None;   // which row of the message sheet was tapped
  int keyRow = -1;
  int keyCol = -1;
  KeyKind keyKind = KeyKind::Char;
  char keyCh = 0;
  uint32_t emojiCp = 0;   // EmojiCell: the code point to append
  int emojiGroup = -1;    // EmojiCat: the group to open
};

// What a tap at (x, y) would hit. Pure read: it measures text but changes nothing.
ChatHitResult chatScreenHit(const ChatView& v, int x, int y);

// EST-390: the rectangle the pressed look goes on, from chatScreenHit()'s own answer. Same
// precedence, same rectangles: a key uses keyHit (its tap target), a selector row its row rect, the
// header its head rect, a message its drawn block. A consumed area (a sheet header, a tap outside the
// keyboard) answers an invalid target, so nothing is drawn pressed there.
PressTarget chatPressTarget(const ChatView& v, int x, int y);

// The rectangle of one message's sender name, as drawn on the first line. The tap on a name opens
// that sender's DM (EST-342), so the same pixels that the eye reads are the ones that answer.
bool chatSenderNameRect(const ChatView& v, int msgIndex, Rect* out);

// Message band arithmetic for the shell: the content height is what decides whether a new message
// may move the view (only when the reader is already at the bottom), and maxScroll clamps a drag.
int chatContentHeight(const ChatView& v);
int chatMaxScroll(const ChatView& v);

// The selector list's scroll clamp and the row under a y. The shell drags the list with the finger.
int chatSelectorMaxScroll(const ChatView& v);

// The emoji grid's scroll clamp for the open group (0 when the group fits).
int chatEmojiMaxScroll(const ChatView& v);

// The open message sheet's panel, or an empty rect when no sheet is open. The shell needs it on the
// PRESS, not just the release: a press outside the panel closes the sheet and is spent (EST-339).
Rect chatSheetPanel(const ChatView& v);

}  // namespace ui
}  // namespace om
