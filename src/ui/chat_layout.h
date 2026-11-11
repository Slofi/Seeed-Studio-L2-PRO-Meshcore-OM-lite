// SPDX-License-Identifier: MIT
// Chat geometry: the header, the chat selector, the message band, the compose line, the keyboard
// sheet and the two overlays. Plain C++
// (no LovyanGFX) so test/host can prove the arithmetic with the system g++. One function hands out
// the rectangles, and the same rectangles answer the taps: a screen never repeats a coordinate.
#pragma once

#include <stdint.h>

#include "layout.h"
#include "theme.h"
#include "ui/badge_font.h"   // EST-382: the unread count's one number rule (badgeText)
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
                                     RptrPassword, RptrPosition, RptrRegion };   // EST-358/360

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
// EST-360: RegionEntry::name[31], so 30 characters; the sheet offers only the bytes MeshCore's
// region-name rule accepts (see om::rptr::regionNameChar).
constexpr int kKeyboardRegionMaxBytes = 30;

bool keyboardAllowsChar(KeyboardField field, char c);
int keyboardMaxBytes(KeyboardField field);           // 0 for Chat (its own draft budget applies)
bool keyboardAcceptsBytes(KeyboardField field, int usedBytes, int addBytes);
bool keyboardAccents(KeyboardField field);           // the s/c/z long press
bool keyboardEmoji(KeyboardField field);             // the smiley key / emoji page

// The four key rows of the sheet: 10/9/9/5 keys in letters, 10/10/10/5 in numbers on the CHAT field
// (EST-406 removed the Send key); every other field keeps its Send key. Labels are the device
// keyboard's own characters (QWERTZ letters as today) with the mock's row structure. Glyphs
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
  Rect draft;       // the draft line's text plate (EST-406: the send button is NOT inside it)
  // EST-406: the send button at the right end of the CHAT draft line. One rectangle drives the
  // drawing and the tap (sendButtonHit), it is 30 x 26, and it is empty on the Settings / RPTR
  // editors, whose sheet keeps its Send key.
  Rect sendBtn;
  Rect keys[4][10];       // the DRAWN key rectangles (the mock's 4 px side pad stays visible)
  Rect keyHit[4][10];     // the touch rectangles: outer keys reach the screen edges, bottom row the floor
  uint8_t keyCount[4] = {0, 0, 0, 0};
  bool selector = false;   // the chat dropdown is open
  Rect selPanel;           // the dropdown: the message band, header bottom to compose top
  Rect selList;            // the scrolling part (section labels and chat rows)
  Rect selManage;          // the pinned last row: "Manage channels"
  // EST-382: the ADVERT key and its Flood / Local dropdown. The rectangles are always laid out; the
  // menu is drawn and hit only while ChatView::advertMenu is true.
  Rect advert;             // the key in the freed right of the header (>= 30 x 26)
  Rect advertMenu;         // the small panel under the key
  Rect advertOpt[2];       // Flood, Local
};

// EST-406: does the draft send? False for a null, empty or whitespace-only draft, which is the send
// button's dim, no-action state. Plain C++ so test/host proves the rule the drawing and the shell use.
bool sendEnabled(const char* draft);

// The send button's hit test: one rectangle, the one the button is drawn on. Cannot answer while the
// layout has no button (the keyboard is closed or a Settings editor is up).
inline bool sendButtonHit(const ChatLayout& g, int x, int y) {
  return !g.sendBtn.empty() && g.sendBtn.contains(x, y);
}

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
// then the chat selector over it, then the message sheet. EST-382 adds the small ADVERT dropdown under
// the header; it is the only one of the two dropdowns that can be open at a time (the shell closes the
// other when one opens). A press that lands OUTSIDE the one on top closes it and is CONSUMED: that
// press's release and any drag that follows it must not press what was behind (Filip, 2026-11-10: "I
// pressed away, in the chat area, and it opened a chat menu"). Only the next new press may act on what
// is behind.
enum class ChatOverlay : uint8_t { None = 0, Keyboard, Selector, AdvertMenu, MessageSheet };

struct ChatOverlayPress {
  ChatOverlay overlay = ChatOverlay::None;
  bool inside = true;   // always true when overlay == None
};

// The overlay on top at a press and whether (x, y) is inside it. `menuPanel` is the open message
// sheet's panel (messageSheetLayout().panel) and is read only when `menuOpen` is true;
// `advertMenu` is the ADVERT dropdown's panel and is read only when `advertOpen` is true. The two
// new trailing parameters default to closed/empty, so existing callers keep their meaning.
ChatOverlayPress chatOverlayPress(const ChatLayout& g, bool menuOpen, const Rect& menuPanel,
                                  int x, int y, bool advertOpen = false,
                                  const Rect& advertMenu = Rect{});

// --- the chat selector arithmetic ---------------------------------------------------------------
// The list holds a "Channels" label, one row per channel, a "Direct messages" label and either one
// row per DM or the "No direct messages yet" line. `entry` counts channels first, then DMs.
int selContentHeight(int channelCount, int dmCount);
int selMaxScroll(const Rect& list, int channelCount, int dmCount);
// Screen y of one entry's row at a scroll (negative or past the list when scrolled out of view).
int selRowTop(const Rect& list, int channelCount, int dmCount, int entry, int scroll);
// The entry under py, or -1 (labels, the empty-DM line, the pads and the gaps answer -1).
int selRowAt(const Rect& list, int channelCount, int dmCount, int scroll, int py);

// EST-382: one row's whole layout, from the SAME arithmetic that hit-tests it. The highlight follows
// `selected`; the left dot and the right count follow the chat's unread only (one source: the chat
// model's `unread`), so they can never disagree. `countText` is badgeText()'s own string (1..99, then
// "99+"), drawn in FontRole::MonoMeta whose advance is kSelMonoAdvance for every glyph.
// `dot` and `count` are empty when the chat has no unread; `nameX` keeps the dot's slot reserved so a
// name never jumps when the dot appears.
struct SelRowLayout {
  Rect row;
  Rect dot;        // empty when hasDot is false
  Rect count;      // empty when hasCount is false
  char countText[4] = "";
  int nameX = 0;   // the name's left edge (after the reserved dot slot)
  int line1Cy = 0;
  int line2Cy = 0;
  bool highlight = false;
  bool hasDot = false;
  bool hasCount = false;
};

inline SelRowLayout selRowLayoutFor(const Rect& list, int channelCount, int dmCount, int entry,
                                    int scroll, uint16_t unread, bool selected) {
  SelRowLayout o;
  const int top = selRowTop(list, channelCount, dmCount, entry, scroll);
  o.row = Rect{list.x, (int16_t)top, list.w, kSelRowH};
  o.line1Cy = top + kSelLine1Cy;
  o.line2Cy = top + kSelLine2Cy;
  o.highlight = selected;
  const int dotX = list.x + kSelPadX;
  o.nameX = dotX + kSelDot + kSelDotGap;
  if (unread == 0) return o;
  o.hasDot = true;
  o.dot = Rect{(int16_t)dotX, (int16_t)(o.line1Cy - kSelDot / 2), kSelDot, kSelDot};
  const int chars = badgeText((int)unread, o.countText, (int)sizeof(o.countText));
  o.hasCount = chars > 0;
  if (o.hasCount) {
    const int w = chars * kSelMonoAdvance;
    o.count = Rect{(int16_t)(o.row.right() - kSelPadX - w),
                   (int16_t)(o.line2Cy - kSelCountH / 2), (int16_t)w, kSelCountH};
  }
  return o;
}

// --- EST-382: the ADVERT key and its Flood / Local dropdown -------------------------------------
// The key sits in the header's freed right, exactly as tall as the Chat header (26 px), so its tap
// target and its drawing are the same rectangle. The dropdown hangs under it, right-aligned, with two
// 36 px rows (about 1.3x the selector's 30 px rows).
struct ChatAdvertLayout {
  Rect button;
  Rect menu;       // the panel's border box
  Rect opt[2];     // 0 = Flood, 1 = Local
};

ChatAdvertLayout chatAdvertLayoutFor(const Layout& L);

enum class ChatAdvertHit : uint8_t { None = 0, Button, Flood, Local, MenuOther };

// Only meaningful while the dropdown is open (`open`): an option row sends and closes, any other
// point in the panel only closes, and a point outside answers None (the shell's overlay rule closes
// it and spends the gesture). Closed, only the button answers.
ChatAdvertHit chatAdvertHitAt(const ChatAdvertLayout& a, bool open, int x, int y);
// The scroll for a drag that began at `base` while the finger has moved `dyUp` px UP the glass
// (screen y decreases, so dyUp is startY - nowY). The list follows the finger: dyUp px up moves the
// rows dyUp px up, and the scroll grows by dyUp. kSelGive px of stretch is allowed past either end;
// the release path snaps it back into [0, maxScroll].
int selDragScroll(int base, int dyUp, int maxScroll);

// --- emoji keyboard page -----------------------------------------------------------------------
// The smiley key swaps the four key rows for this: a rail of five group buttons plus "abc", a
// Recent strip of the last 16 picks, and a scrolling 22 px grid below it. EST-406 removed the page's
// own Send button: the composer's send button on the draft line above it is the one door.
// `area` is exactly the rectangle the key rows would have taken, so the sheet height does not
// change. All arithmetic lives here so the grid draws and answers with one cell rectangle.
struct EmojiPadLayout {
  Rect area;
  Rect rail;
  Rect cat[kEmojiGroupCount];   // faces, hands, hearts, objects, symbols
  Rect abc;                // back to the letters/numbers keyboard
  Rect recentStrip;
  Rect recent[kEmojiRecentMax];
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
// row, `cell` an index into the selected group; -1 when nothing. `abc` is a flag.
struct EmojiPadHit {
  int cat = -1;
  int recent = -1;
  int cell = -1;
  bool abc = false;
};
EmojiPadHit emojiPadHit(const EmojiPadLayout& e, int x, int y);

// EST-390: one grid cell's rectangle, the inverse of emojiPadHit()'s index arithmetic. `index` is a
// group entry (0..count-1) as the drawing walks it, so the pressed rectangle for a cell is the same
// pixels the glyph sits on. Empty for an index outside the group.
inline Rect emojiCellRect(const EmojiPadLayout& e, int index) {
  if (e.grid.empty() || e.cols <= 0 || index < 0 || index >= e.count) return Rect{};
  const int row = index / e.cols;
  const int col = index % e.cols;
  return Rect{(int16_t)(e.grid.x + col * kEmojiCell),
              (int16_t)(e.grid.y + row * kEmojiCell - e.scroll), kEmojiCell, kEmojiCell};
}

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
