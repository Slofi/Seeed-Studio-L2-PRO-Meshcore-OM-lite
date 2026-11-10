// SPDX-License-Identifier: MIT
// Chat geometry: the header, the chat selector, the message band, the compose line, the keyboard
// sheet and the two overlays. Plain C++
// (no LovyanGFX) so test/host can prove the arithmetic with the system g++. One function hands out
// the rectangles, and the same rectangles answer the taps: a screen never repeats a coordinate.
#pragma once

#include <stdint.h>

#include "layout.h"
#include "theme.h"
#include "ui/emoji_picker.h"
#include "ui/emoji_recent.h"

namespace om {
namespace ui {

// EST-354: Reveal is the password sheet's show/hide key. It exists only on the Wi-Fi password
// field, so the chat keyboard's key tables are unchanged.
enum class KeyKind : uint8_t { Char, Shift, Mode, Space, Del, Send, Emoji, Reveal };

struct KeyDef {
  char label[8] = "";
  uint8_t units = kKeyUnit;
  KeyKind kind = KeyKind::Char;
};

// The sheet's page state (EST-346). One struct so the opening rule lives in ONE place: every time the
// sheet opens it starts on the letters page with shift off, whatever page it was closed on (Filip:
// "when switching to numbers/symbols and then closing the keyboard, next time it should be reset,
// back to the letters; now it shows the last state"). The draft text is NOT here: it lives in the
// composer and survives closing the sheet. Plain C++ so test/host proves the transition.
enum class KeyPage : uint8_t { Letters = 0, Numbers, Emoji };

struct KeyboardState {
  bool open = false;
  bool shift = false;
  KeyPage page = KeyPage::Letters;

  // Every door into the sheet (the compose field, REPLY, COPY, MENTION) calls this, never a bare
  // `open = true`.
  void openSheet() {
    open = true;
    page = KeyPage::Letters;
    shift = false;
  }
  void close() { open = false; }   // the draft is the caller's and is kept as it was
  void toggleShift() { shift = !shift; }
  // The 123 / abc key: numbers <-> letters. Shift is page state, so it goes off with the page.
  void toggleNumbers() {
    page = (page == KeyPage::Numbers) ? KeyPage::Letters : KeyPage::Numbers;
    shift = false;
  }
  // The smiley / abc keys: the emoji page replaces the four key rows.
  void toggleEmoji() {
    page = (page == KeyPage::Emoji) ? KeyPage::Letters : KeyPage::Emoji;
    shift = false;
  }
  void showLetters() { page = KeyPage::Letters; }
  bool nums() const { return page == KeyPage::Numbers; }
  bool emoji() const { return page == KeyPage::Emoji; }
};

// The accented letter a long press on `key` enters (EST-346: s -> š, c -> č, z -> ž; capitals with
// shift), or 0 when the key has no accented form. The caller keeps the letter as a code point and
// encodes it, so the mapping and the composer budget never disagree about the bytes.
uint32_t accentCodePoint(char key, bool shift);

// EST-352: the one shared keyboard sheet, used by the chat composer and by the Settings text
// editors. Each FIELD says which characters it accepts, how many bytes it holds, and whether the
// accented s/c/z long press and the emoji page belong to it. The key rows are still the same sheet
// (letters, 123, Del, Send); a field only filters the character keys it cannot accept.
enum class KeyboardField : uint8_t { Chat = 0, NodeName, Base64Key, WifiPassword,
                                     RptrPassword, RptrPosition };   // EST-358

// MeshCore's names: node_name[32] and ChannelDetails::name[32], so 31 bytes. AdvertDataParser::
// isValidName rejects exactly [ ] \ : , ? * and accepts the rest of printable ASCII.
constexpr int kKeyboardNameMaxBytes = 31;
// A channel key is 16 bytes; base64 of 16 bytes is 22 significant chars plus "==", 24 in all.
constexpr int kKeyboardBase64MaxBytes = 24;
// A WPA2/WPA3 passphrase is 8..63 printable characters; the sheet accepts printable ASCII and caps
// the buffer at 63 (the ESP32 driver enforces the minimum, not this keyboard).
constexpr int kKeyboardWifiPassMaxBytes = 63;
// EST-358: a repeater's admin password is compared with strcmp and sendLogin() truncates at 15
// characters, so the sheet holds 15 printable ASCII bytes and never shows them back.
constexpr int kKeyboardRptrPassMaxBytes = 15;
// "46.25000, 14.40000" with room to spare while typing.
constexpr int kKeyboardRptrPosMaxBytes = 24;

bool keyboardAllowsChar(KeyboardField field, char c);
int keyboardMaxBytes(KeyboardField field);           // 0 for Chat (its own draft budget applies)
bool keyboardAcceptsBytes(KeyboardField field, int usedBytes, int addBytes);
bool keyboardAccents(KeyboardField field);           // the s/c/z long press
bool keyboardEmoji(KeyboardField field);             // the smiley key / emoji page

// The four key rows of the sheet: 10/9/9/3 keys in letters, 10/10/10/2 in numbers. Labels are the
// device keyboard's own characters (QWERTZ letters as today) with the mock's row structure. Glyphs
// the ASCII-only faces do not carry (the mock's shift arrow and backspace) are drawn as vectors by
// ui/widgets.cpp; the label stays empty for Del. A non-Chat field drops the character keys it does
// not accept (and the space / emoji key when the field has no use for them).
int keyRow(bool nums, int row, KeyDef* out, KeyboardField field = KeyboardField::Chat);

struct ChatLayout {
  Rect head;        // the header row: the whole row toggles the selector
  Rect headTitle;   // the room the open chat's name has (chevron and unread dot sit outside it)
  Rect headDot;     // the gold dot counting unread in the OTHER chats (empty while none wait)
  Rect msgs;        // the message band
  Rect compose;     // the compose line (empty while the sheet is open)
  Rect field;       // the draft field: the whole compose row (tapping it opens the keyboard)
  bool osk = false;
  Rect sheet;
  Rect replyBar;    // empty when the draft is not answering a message
  Rect draft;
  Rect keys[4][10];       // the DRAWN key rectangles (the mock's 4 px side pad stays visible)
  Rect keyHit[4][10];     // the touch rectangles: outer keys reach the screen edges, bottom row the floor
  uint8_t keyCount[4] = {0, 0, 0, 0};
  bool selector = false;   // the chat dropdown is open
  Rect selPanel;           // the dropdown: the message band, header bottom to compose top
  Rect selList;            // the scrolling part (section labels and chat rows)
  Rect selManage;          // the pinned last row: "Manage channels"
};

// A key under a point. keyAt() uses keyHit, never the drawn rectangles.
struct KeyHit {
  int row = -1;
  int col = -1;
};

ChatLayout chatLayoutFor(const Layout& L, bool osk, bool replying, bool nums, bool selector,
                         KeyboardField field = KeyboardField::Chat);

// The key under (x, y), or {-1,-1}. The caller has already clamped into the screen. `field` must be
// the same one chatLayoutFor() built the rectangles with.
KeyHit keyAt(const ChatLayout& g, bool nums, int x, int y,
             KeyboardField field = KeyboardField::Chat);

// A touch inside the keyboard sheet never closes it (only the fingers on a key, the draft, or a tap
// on the sheet's own header/close do something). Only a touch ABOVE the sheet closes it. Plain
// arithmetic so host tests can prove the rule without LovyanGFX.
inline bool chatSheetClosesAt(const ChatLayout& g, int x, int y) {
  (void)x;
  return g.osk && y < g.sheet.y;
}

// --- overlays: one press, one answer (EST-339) ---------------------------------------------------
// Every overlay in the chat is a bottom-anchored panel, drawn in this order: the keyboard sheet,
// then the chat selector over it, then the message sheet. A press that lands OUTSIDE the one on top
// closes it and is CONSUMED: that press's release and any drag that follows it must not press what
// was behind (Filip, 2026-11-10: "I pressed away, in the chat area, and it opened a chat menu"). Only
// the next new press may act on what is behind.
enum class ChatOverlay : uint8_t { None = 0, Keyboard, Selector, MessageSheet };

struct ChatOverlayPress {
  ChatOverlay overlay = ChatOverlay::None;
  bool inside = true;   // always true when overlay == None
};

// The overlay on top at a press and whether (x, y) is inside it. `menuPanel` is the open message
// sheet's panel (messageSheetLayout().panel) and is read only when `menuOpen` is true.
ChatOverlayPress chatOverlayPress(const ChatLayout& g, bool menuOpen, const Rect& menuPanel,
                                  int x, int y);

// --- the chat selector arithmetic ---------------------------------------------------------------
// The list holds a "Channels" label, one row per channel, a "Direct messages" label and either one
// row per DM or the "No direct messages yet" line. `entry` counts channels first, then DMs.
int selContentHeight(int channelCount, int dmCount);
int selMaxScroll(const Rect& list, int channelCount, int dmCount);
// Screen y of one entry's row at a scroll (negative or past the list when scrolled out of view).
int selRowTop(const Rect& list, int channelCount, int dmCount, int entry, int scroll);
// The entry under py, or -1 (labels, the empty-DM line, the pads and the gaps answer -1).
int selRowAt(const Rect& list, int channelCount, int dmCount, int scroll, int py);
// The scroll for a drag that began at `base` while the finger has moved `dyUp` px UP the glass
// (screen y decreases, so dyUp is startY - nowY). The list follows the finger: dyUp px up moves the
// rows dyUp px up, and the scroll grows by dyUp. kSelGive px of stretch is allowed past either end;
// the release path snaps it back into [0, maxScroll].
int selDragScroll(int base, int dyUp, int maxScroll);

// --- emoji keyboard page -----------------------------------------------------------------------
// The smiley key swaps the four key rows for this: a rail of five group buttons plus "abc", a
// Recent strip of the last 16 picks with Send at its right, and a scrolling 22 px grid below it.
// `area` is exactly the rectangle the key rows would have taken, so the sheet height does not
// change. All arithmetic lives here so the grid draws and answers with one cell rectangle.
struct EmojiPadLayout {
  Rect area;
  Rect rail;
  Rect cat[kEmojiGroupCount];   // faces, hands, hearts, objects, symbols
  Rect abc;                // back to the letters/numbers keyboard
  Rect recentStrip;
  Rect recent[kEmojiRecentMax];
  Rect send;               // Send at the right of the Recent strip
  Rect grid;
  int cols = 0;
  int rows = 0;            // visible rows
  int group = 0;
  int scroll = 0;
  int count = 0;           // entries in the selected group
};

// `recentCount` is the number of valid cells in the Recent row. `count` is the selected group's
// size and `scroll` its pixel offset. Empty when the layout has no room for the emoji page.
EmojiPadLayout emojiPadLayout(const Layout& L, const ChatLayout& g, int recentCount, int group,
                              int scroll, int count);

// The screen rectangle of one grid entry (may be outside `grid` when scrolled out).
Rect emojiPadCell(const EmojiPadLayout& e, int index);

// What a point hits on the emoji page. `cat` is a group index, `recent` an index into the Recent
// row, `cell` an index into the selected group; -1 when nothing. `send` and `abc` are flags.
struct EmojiPadHit {
  int cat = -1;
  int recent = -1;
  int cell = -1;
  bool send = false;
  bool abc = false;
};
EmojiPadHit emojiPadHit(const EmojiPadLayout& e, int x, int y);

// Content height and the scroll clamp for one group.
int emojiPadContentHeight(const EmojiPadLayout& e, int count);
int emojiPadMaxScroll(const EmojiPadLayout& e, int count);

// --- message band -------------------------------------------------------------------------------
// heights[] is the drawn height of every message block (text lines, no gaps). Oldest first.
int msgContentHeight(const int16_t* heights, int n);
int msgMaxScroll(const Rect& band, const int16_t* heights, int n);
// Screen y of every message top, bottom-anchored: the newest message sits at the band bottom (minus
// the pad) when scroll is 0, and scroll moves the whole stack up.
void msgSpans(const Rect& band, const int16_t* heights, int n, int scroll, int16_t* top);
// The message under py, or -1. Only the visible part of a block counts, so a tap under the chips row
// or the header cannot hit a message hidden behind it.
int msgAt(const Rect& band, const int16_t* heights, int n, int scroll, int py);

// --- keys ---------------------------------------------------------------------------------------
// Split one row into keys by weight so they tile it exactly: the last key takes the rounding
// remainder, so the row's right edge is exact whatever the division leaves over.
int rowKeyRects(const Rect& row, const uint8_t* units, int n, int gap, Rect* out);

}  // namespace ui
}  // namespace om
