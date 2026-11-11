// SPDX-License-Identifier: MIT
#include "ui/chat_layout.h"

#include <stdio.h>
#include <string.h>

#include "rptr_model.h"   // EST-360: the region-name rule is one function, shared with the model

namespace om {
namespace ui {
namespace {

// The sheet is exactly as tall as its contents: padding, the draft line, and four 22 px key rows.
// EST-406: the CHAT's draft line is the send button's own 26 px (the button has to be at least
// 30 x 26 and its drawn rect is its tap target); the editors keep the 18 px line.
int sheetHeight(bool replying, KeyboardField field) {
  const int draftH = (field == KeyboardField::Chat) ? kSendBtnH : kOskDraftH;
  return kOskPad * 2 + draftH + kOskDraftGap + 4 * kOskKeyH + 3 * kOskKeyGap +
         (replying ? kOskReplyH + kOskDraftGap : 0);
}

const char kLetters0[] = "qwertzuiop";
const char kLetters1[] = "asdfghjkl";
const char kLetters2[] = "yxcvbnm";
const char kNumbers0[] = "1234567890";
const char kNumbers1[] = "@#$%&*()!-";   // 10 keys, the old row plus "!"
const char kSymbols[] = "+=/:;!?_";   // 8: the numbers row already carries "-"

int addChars(KeyboardField field, KeyDef* out, int at, const char* chars) {
  for (int i = 0; chars[i]; ++i) {
    if (!keyboardAllowsChar(field, chars[i])) continue;
    out[at].label[0] = chars[i];
    out[at].label[1] = 0;
    out[at].units = kKeyUnit;
    out[at].kind = KeyKind::Char;
    ++at;
  }
  return at;
}

void put(KeyDef& k, const char* label, uint8_t units, KeyKind kind) {
  snprintf(k.label, sizeof(k.label), "%s", label);
  k.units = units;
  k.kind = kind;
}

// A character key the field accepts, or nothing. Space is a character here too: a name takes it, a
// base64 key does not.
void putChar(KeyboardField field, KeyDef* out, int& n, const char* label, uint8_t units,
             KeyKind kind) {
  if (kind == KeyKind::Space && !keyboardAllowsChar(field, ' ')) return;
  if (kind == KeyKind::Char && !keyboardAllowsChar(field, label[0])) return;
  put(out[n++], label, units, kind);
}

}  // namespace

// --- the shared sheet's per-field rules (EST-352) ----------------------------------------------
bool keyboardAllowsChar(KeyboardField field, char c) {
  const unsigned char u = (unsigned char)c;
  switch (field) {
    case KeyboardField::Base64Key:
      return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') ||
             u == '+' || u == '/' || u == '=';
    case KeyboardField::NodeName:
      // MeshCore's own rule (AdvertDataParser::isValidName): printable ASCII except these seven.
      if (u < 0x20 || u == 0x7F) return false;
      switch (u) {
        case '[': case ']': case '\\': case ':': case ',': case '?': case '*': return false;
        default: return true;
      }
    case KeyboardField::WifiPassword:
    case KeyboardField::RptrPassword:
      // A passphrase (Wi-Fi or a repeater's admin password) is any printable ASCII, space included;
      // no control character can be typed on this sheet anyway.
      return u >= 0x20 && u < 0x7F;
    case KeyboardField::RptrPosition:
      // EST-358: the position editor takes only what a coordinate pair is made of.
      return (u >= '0' && u <= '9') || u == '.' || u == '-' || u == ',' || u == ' ';
    case KeyboardField::RptrRegion:
      // EST-360: MeshCore's own region-name rule, shared with rptr_model.cpp (RegionMap::is_name_char
      // accepts '-', '$', '#', digits and every byte at or above 'A'; a space or '*' is not a name).
      return om::rptr::regionNameChar(c);
    case KeyboardField::Chat:
    default:
      return true;   // the chat's own key table is already the set it offers
  }
}

int keyboardMaxBytes(KeyboardField field) {
  switch (field) {
    case KeyboardField::NodeName: return kKeyboardNameMaxBytes;
    case KeyboardField::Base64Key: return kKeyboardBase64MaxBytes;
    case KeyboardField::WifiPassword: return kKeyboardWifiPassMaxBytes;
    case KeyboardField::RptrPassword: return kKeyboardRptrPassMaxBytes;
    case KeyboardField::RptrPosition: return kKeyboardRptrPosMaxBytes;
    case KeyboardField::RptrRegion: return om::rptr::kRegionNameMax;
    default: return 0;   // chat: the draft budget is composeAllowance()
  }
}

bool keyboardAcceptsBytes(KeyboardField field, int usedBytes, int addBytes) {
  if (usedBytes < 0) usedBytes = 0;
  if (addBytes <= 0) return true;
  const int cap = keyboardMaxBytes(field);
  return cap == 0 || usedBytes + addBytes <= cap;
}

bool keyboardAccents(KeyboardField field) {
  // A password is taken literally: the accented s/c/z long press would enter bytes the network's
  // passphrase did not ask for. A region name goes to MeshCore's name rule and the repeater's own
  // ASCII names, so it is typed plainly too.
  return field != KeyboardField::Base64Key && field != KeyboardField::WifiPassword &&
         field != KeyboardField::RptrPassword && field != KeyboardField::RptrPosition &&
         field != KeyboardField::RptrRegion;
}
bool keyboardEmoji(KeyboardField field) { return field == KeyboardField::Chat; }

// EST-346: the three keys with a long-press accent. Slovenian: s -> š (U+0161), c -> č (U+010D),
// z -> ž (U+017E); with shift the capital (Š U+0160, Č U+010C, Ž U+017D). Anything else is 0, so the
// caller arms no long press and a tap stays exactly what it was.
uint32_t accentCodePoint(char key, bool shift) {
  switch (key) {
    case 's': return shift ? 0x0160u : 0x0161u;
    case 'c': return shift ? 0x010Cu : 0x010Du;
    case 'z': return shift ? 0x017Du : 0x017Eu;
    default: return 0;
  }
}

int keyRow(bool nums, int row, KeyDef* out, KeyboardField field) {
  if (out == nullptr || row < 0 || row > 3) return 0;
  int n = 0;
  if (!nums) {
    switch (row) {
      case 0:
        return addChars(field, out, 0, kLetters0);
      case 1:
        return addChars(field, out, 0, kLetters1);
      case 2:
        put(out[n++], "shift", kKeyUnitWide, KeyKind::Shift);
        n = addChars(field, out, n, kLetters2);
        put(out[n++], "", kKeyUnitWide, KeyKind::Del);
        return n;
      default:
        // Filip, 2026-11-10: "where are the dot and comma?" The bottom row is
        // 123, emoji, comma, space, dot, Send. The smiley opens the emoji page; the password field
        // puts its show/hide key in the smiley's place (EST-354).
        // EST-406 (Filip, 2026-11-11): the chat's Send left the keyboard for the message input line,
        // so the space bar takes its 22 units; the keys before it keep their place. The Settings and
        // RPTR editors keep their Send key (their sheet's commit key), hence the field test.
        put(out[n++], "123", kKeyUnitWide, KeyKind::Mode);
        if (field == KeyboardField::WifiPassword) {
          put(out[n++], "show", kKeyUnitWide, KeyKind::Reveal);
        } else if (keyboardEmoji(field)) {
          put(out[n++], "emoji", kKeyUnitWide, KeyKind::Emoji);
        }
        putChar(field, out, n, ",", kKeyUnit, KeyKind::Char);
        putChar(field, out, n, "space",
                field == KeyboardField::Chat ? kKeyUnitSpaceChat : kKeyUnitSpace, KeyKind::Space);
        putChar(field, out, n, ".", kKeyUnit, KeyKind::Char);
        if (field != KeyboardField::Chat) put(out[n++], "Send", kKeyUnitWide, KeyKind::Send);
        return n;
    }
  }
  // A passphrase takes the common symbol set on its 123 page (the chat keyboard's own rows are
  // tuned for prose). Letters, digits, space, comma and dot stay where they are; these two rows add
  // the punctuation a home router password usually has.
  if (field == KeyboardField::WifiPassword) {
    switch (row) {
      case 0: return addChars(field, out, 0, kNumbers0);
      case 1: return addChars(field, out, 0, "@#$%^&*()");
      case 2:
        put(out[n++], "abc", kKeyUnitWide, KeyKind::Mode);
        n = addChars(field, out, n, "-+=_/'\"~");
        put(out[n++], "", kKeyUnitWide, KeyKind::Del);
        return n;
      default:
        put(out[n++], "abc", kKeyUnitWide, KeyKind::Mode);
        put(out[n++], "show", kKeyUnitWide, KeyKind::Reveal);
        put(out[n++], "space", kKeyUnitSpace, KeyKind::Space);
        put(out[n++], ",", kKeyUnit, KeyKind::Char);
        put(out[n++], ".", kKeyUnit, KeyKind::Char);
        put(out[n++], "Send", kKeyUnitWide, KeyKind::Send);
        return n;
    }
  }
  // The key entry is base64: its 123 page is digits plus the three base64 symbols, so "+ / =" are
  // one tap away without a row of keys the field would have to reject.
  if (field == KeyboardField::Base64Key) {
    switch (row) {
      case 0: return addChars(field, out, 0, kNumbers0);
      case 1: return addChars(field, out, 0, "+/=");
      case 2:
        put(out[n++], "abc", kKeyUnitWide, KeyKind::Mode);
        put(out[n++], "", kKeyUnitWide, KeyKind::Del);
        return n;
      default:
        put(out[n++], "abc", kKeyUnitWide, KeyKind::Mode);
        put(out[n++], "+", kKeyUnit, KeyKind::Char);
        put(out[n++], "/", kKeyUnit, KeyKind::Char);
        put(out[n++], "=", kKeyUnit, KeyKind::Char);
        put(out[n++], "Send", kKeyUnitWide, KeyKind::Send);
        return n;
    }
  }
  // EST-360: a region name's 123 page is digits plus the only three symbols MeshCore's name rule
  // allows ('#', '$', '-'), so nothing is offered that putRegion() would refuse.
  if (field == KeyboardField::RptrRegion) {
    switch (row) {
      case 0: return addChars(field, out, 0, kNumbers0);
      case 1: return addChars(field, out, 0, "#$-");
      case 2:
        put(out[n++], "abc", kKeyUnitWide, KeyKind::Mode);
        put(out[n++], "", kKeyUnitWide, KeyKind::Del);
        return n;
      default:
        put(out[n++], "abc", kKeyUnitWide, KeyKind::Mode);
        put(out[n++], "-", kKeyUnit, KeyKind::Char);
        put(out[n++], "Send", kKeyUnitWide, KeyKind::Send);
        return n;
    }
  }
  switch (row) {
    case 0:
      return addChars(field, out, 0, kNumbers0);
    case 1:
      return addChars(field, out, 0, kNumbers1);
    case 2:
      put(out[n++], "abc", kKeyUnitWide, KeyKind::Mode);
      n = addChars(field, out, n, kSymbols);
      put(out[n++], "", kKeyUnitWide, KeyKind::Del);
      return n;
    default:
      // The dot and the comma stay on the 123 page too, and the smiley is always one key away (the
      // password field's show/hide key takes that slot). EST-406: the chat's Send is gone (the draft
      // line's button sends) and the space bar takes its room; the editors keep their Send key.
      put(out[n++], "abc", kKeyUnitWide, KeyKind::Mode);
      if (field == KeyboardField::WifiPassword) {
        put(out[n++], "show", kKeyUnitWide, KeyKind::Reveal);
      }
      else if (keyboardEmoji(field)) put(out[n++], "emoji", kKeyUnitWide, KeyKind::Emoji);
      putChar(field, out, n, "space",
              field == KeyboardField::Chat ? kKeyUnitSpaceChat : kKeyUnitSpace, KeyKind::Space);
      putChar(field, out, n, ",", kKeyUnit, KeyKind::Char);
      putChar(field, out, n, ".", kKeyUnit, KeyKind::Char);
      if (field != KeyboardField::Chat) put(out[n++], "Send", kKeyUnitWide, KeyKind::Send);
      return n;
  }
}

// EST-406: the send button's state. A draft that is only whitespace has nothing to send, so the
// button is drawn dim and a tap on it does nothing at all.
bool sendEnabled(const char* draft) {
  if (draft == nullptr) return false;
  for (const char* p = draft; *p != 0; ++p) {
    const char c = *p;
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return true;
  }
  return false;
}

ChatLayout chatLayoutFor(const Layout& L, bool osk, bool replying, bool nums, bool selector,
                         KeyboardField field) {
  ChatLayout g;
  g.osk = osk;
  g.selector = selector;

  // The header replaces the old chip strip in the same row, so the message band keeps its top edge.
  // EST-382: the header is 26 px (kChatHeadH) and holds THREE things on its right: the elsewhere dot,
  // the shortened selector's title (left) and the ADVERT key (far right). The key's rect is also its
  // tap target, so the 30 x 26 minimum is met by the drawing itself.
  g.head = Rect{L.content.x, L.content.y, L.content.w, kChatHeadH};
  const ChatAdvertLayout advert = chatAdvertLayoutFor(L);
  g.advert = advert.button;
  g.advertMenu = advert.menu;
  for (int i = 0; i < 2; ++i) g.advertOpt[i] = advert.opt[i];
  g.headDot = Rect{(int16_t)(g.advert.x - kAdvertGap - kElsewhereDot),
                   (int16_t)(g.head.y + (g.head.h - 1 - kElsewhereDot) / 2), kElsewhereDot,
                   kElsewhereDot};
  // The title stops before the reserved unread-count room, so a count appearing beside the dot never
  // moves the name. The chevron follows the measured name inside this rect.
  g.headTitle = Rect{(int16_t)(g.head.x + kHeadPadX), g.head.y,
                     (int16_t)(g.headDot.x - kElsewhereGap - kElsewhereW - (g.head.x + kHeadPadX)),
                     (int16_t)(g.head.h - 1)};

  if (osk) {
    const int16_t sh = (int16_t)sheetHeight(replying, field);
    g.sheet = Rect{0, (int16_t)(L.height - sh), L.width, sh};
  }

  if (osk) {
    g.msgs = Rect{L.content.x, g.head.bottom(), L.content.w,
                  (int16_t)(g.sheet.y - g.head.bottom())};
  } else {
    g.compose = Rect{L.content.x, (int16_t)(L.content.bottom() - kComposeH), L.content.w, kComposeH};
    g.msgs = Rect{L.content.x, g.head.bottom(), L.content.w,
                  (int16_t)(g.compose.y - g.head.bottom())};

    // The draft field is the whole compose row: the Send button moved into the keyboard (Filip,
    // 2026-11-10) and the keyboard button is gone too (tapping the field opens the sheet).
    const int16_t rowY = (int16_t)(g.compose.y + 1 + (kComposeH - 1 - kFieldH) / 2);
    g.field = Rect{(int16_t)(g.compose.x + kComposePadX), rowY,
                   (int16_t)(g.compose.w - 2 * kComposePadX), kFieldH};
  }

  // The selector drops down over the message area: the header's bottom edge to the compose line (or
  // the keyboard sheet), with its last row pinned to the panel's floor.
  if (selector) {
    g.selPanel = g.msgs;
    g.selManage = Rect{g.selPanel.x, (int16_t)(g.selPanel.bottom() - kSelManageH), g.selPanel.w,
                       kSelManageH};
    g.selList = Rect{g.selPanel.x, g.selPanel.y, g.selPanel.w,
                     (int16_t)(g.selManage.y - g.selPanel.y)};
  }

  if (osk) {
    int y = g.sheet.y + kOskPad;
    const int16_t innerW = (int16_t)(L.width - 2 * kOskPad);
    if (replying) {
      g.replyBar = Rect{kOskPad, (int16_t)y, innerW, kOskReplyH};
      y += kOskReplyH + kOskDraftGap;
    }
    // EST-406: the chat draft line ends with the send button. Its height is the button's (26 px), so
    // the button never overhangs the line, and the text plate stops kOskKeyGap short of it: the
    // visible text area shrinks by the button, which is why a long draft's tail never runs under it.
    // The Settings / RPTR editors have no send button and keep the whole width and the 18 px line.
    const int16_t draftH = (field == KeyboardField::Chat) ? kSendBtnH : kOskDraftH;
    if (field == KeyboardField::Chat) {
      g.sendBtn = Rect{(int16_t)(L.width - kOskPad - kSendBtnW), (int16_t)y, kSendBtnW, draftH};
      g.draft = Rect{kOskPad, (int16_t)y, (int16_t)(g.sendBtn.x - kOskKeyGap - kOskPad), draftH};
    } else {
      g.draft = Rect{kOskPad, (int16_t)y, innerW, draftH};
    }
    y += draftH + kOskDraftGap;
    for (int r = 0; r < 4; ++r) {
      KeyDef defs[10];
      const int n = keyRow(nums, r, defs, field);
      uint8_t units[10];
      for (int i = 0; i < n; ++i) units[i] = defs[i].units;
      const Rect row{kOskPad, (int16_t)y, innerW, kOskKeyH};
      g.keyCount[r] = (uint8_t)rowKeyRects(row, units, n, kOskKeyGap, g.keys[r]);
      // Hit rectangles: the drawn keys stay where the mock puts them (4 px side pad), but a finger in
      // that pad on the outermost key, or on the last pixel rows under the bottom row, belongs to the
      // key. Only the 2 px gaps BETWEEN keys still do nothing (EST-338).
      for (int c = 0; c < n; ++c) g.keyHit[r][c] = g.keys[r][c];
      if (n > 0) {
        if (n == 1) {
          g.keyHit[r][0] = Rect{0, g.keys[r][0].y, L.width, g.keys[r][0].h};
        } else {
          g.keyHit[r][0].x = 0;
          g.keyHit[r][0].w = (int16_t)g.keys[r][0].right();
          g.keyHit[r][n - 1].w = (int16_t)(L.width - g.keys[r][n - 1].x);
        }
        if (r == 3) {
          for (int c = 0; c < n; ++c) {
            g.keyHit[r][c].h = (int16_t)(L.height - g.keyHit[r][c].y);
          }
        }
      }
      y += kOskKeyH + kOskKeyGap;
    }
  }

  return g;
}

KeyHit keyAt(const ChatLayout& g, bool nums, int x, int y, KeyboardField field) {
  KeyHit hit;
  for (int r = 0; r < 4; ++r) {
    KeyDef defs[10];
    const int n = keyRow(nums, r, defs, field);
    for (int c = 0; c < n && c < g.keyCount[r]; ++c) {
      if (!g.keyHit[r][c].contains(x, y)) continue;
      hit.row = r;
      hit.col = c;
      return hit;
    }
  }
  return hit;
}

// --- emoji page --------------------------------------------------------------------------------
EmojiPadLayout emojiPadLayout(const Layout& L, const ChatLayout& g, int recentCount, int group,
                              int scroll, int count) {
  EmojiPadLayout e;
  if (!g.osk || g.sheet.empty() || g.draft.empty()) return e;
  const int16_t top = (int16_t)(g.draft.bottom() + kOskDraftGap);
  const int16_t bottom = (int16_t)(g.sheet.bottom() - kOskPad);
  if (bottom - top < 2 * kEmojiRailCellH) return e;
  e.area = Rect{kOskPad, top, (int16_t)(L.width - 2 * kOskPad), (int16_t)(bottom - top)};

  e.rail = Rect{e.area.x, e.area.y, kEmojiRailW, e.area.h};
  int16_t ry = e.area.y;
  for (int i = 0; i < kEmojiGroupCount; ++i) {
    e.cat[i] = Rect{e.area.x, ry, kEmojiRailW, kEmojiRailCellH};
    ry = (int16_t)(ry + kEmojiRailCellH);
  }
  e.abc = Rect{e.area.x, (int16_t)(e.area.bottom() - kEmojiAbcH), kEmojiRailW, kEmojiAbcH};

  const int16_t cx = (int16_t)(e.area.x + kEmojiRailW + 2);
  const int16_t cw = (int16_t)(e.area.right() - cx);
  e.recentStrip = Rect{cx, e.area.y, cw, kEmojiStripH};
  const int nRecent = recentCount < 0 ? 0 : (recentCount > kEmojiRecentMax ? kEmojiRecentMax
                                                                          : recentCount);
  for (int i = 0; i < kEmojiRecentMax; ++i) {
    e.recent[i] = Rect{};
    if (i < nRecent) {
      e.recent[i] = Rect{(int16_t)(cx + i * kEmojiRecentCell), (int16_t)(e.area.y + 2),
                         kEmojiRecentCell, kEmojiRecentCell};
    }
  }

  e.grid = Rect{cx, (int16_t)(e.recentStrip.bottom() + kEmojiGridGap), cw,
                (int16_t)(e.area.bottom() - e.recentStrip.bottom() - kEmojiGridGap)};
  e.cols = e.grid.w / kEmojiCell;
  e.rows = e.grid.h / kEmojiCell;
  e.group = group;
  e.scroll = scroll;
  e.count = count;
  return e;
}

Rect emojiPadCell(const EmojiPadLayout& e, int index) {
  if (e.cols <= 0 || index < 0 || index >= e.count) return Rect{};
  const int col = index % e.cols;
  const int row = index / e.cols;
  return Rect{(int16_t)(e.grid.x + col * kEmojiCell),
              (int16_t)(e.grid.y + row * kEmojiCell - e.scroll), kEmojiCell, kEmojiCell};
}

int emojiPadContentHeight(const EmojiPadLayout& e, int count) {
  if (e.cols <= 0 || count <= 0) return 0;
  return ((count + e.cols - 1) / e.cols) * kEmojiCell;
}

int emojiPadMaxScroll(const EmojiPadLayout& e, int count) {
  const int content = emojiPadContentHeight(e, count);
  return content > e.grid.h ? content - e.grid.h : 0;
}

EmojiPadHit emojiPadHit(const EmojiPadLayout& e, int x, int y) {
  EmojiPadHit h;
  if (e.area.empty() || !e.area.contains(x, y)) return h;
  if (e.abc.contains(x, y)) {
    h.abc = true;
    return h;
  }
  for (int i = 0; i < kEmojiGroupCount; ++i) {
    if (e.cat[i].contains(x, y)) {
      h.cat = i;
      return h;
    }
  }
  for (int i = 0; i < kEmojiRecentMax; ++i) {
    if (e.recent[i].contains(x, y)) {
      h.recent = i;
      return h;
    }
  }
  if (e.grid.contains(x, y) && e.cols > 0) {
    const int col = (x - e.grid.x) / kEmojiCell;
    const int row = (y - e.grid.y + e.scroll) / kEmojiCell;
    if (col >= 0 && col < e.cols && row >= 0) {
      const int index = row * e.cols + col;
      if (index >= 0 && index < e.count) h.cell = index;
    }
  }
  return h;
}

ChatOverlayPress chatOverlayPress(const ChatLayout& g, bool menuOpen, const Rect& menuPanel,
                                  int x, int y, bool advertOpen, const Rect& advertMenu) {
  ChatOverlayPress p;
  // The order is the drawing order: the message sheet sits over everything (screen_chat draws it
  // last), then the ADVERT dropdown, then the selector over the message band, then the keyboard.
  if (menuOpen) {
    p.overlay = ChatOverlay::MessageSheet;
    p.inside = menuPanel.contains(x, y);
    return p;
  }
  if (advertOpen) {
    p.overlay = ChatOverlay::AdvertMenu;
    p.inside = advertMenu.contains(x, y);
    return p;
  }
  if (g.selector) {
    p.overlay = ChatOverlay::Selector;
    p.inside = g.selPanel.contains(x, y);
    return p;
  }
  if (g.osk) {
    p.overlay = ChatOverlay::Keyboard;
    p.inside = g.sheet.contains(x, y);
    return p;
  }
  return p;
}

int selContentHeight(int channelCount, int dmCount) {
  if (channelCount < 0) channelCount = 0;
  if (dmCount < 0) dmCount = 0;
  const int channels = kSelSectionH + channelCount * kSelRowH;
  const int dms = kSelSectionH + (dmCount > 0 ? dmCount * kSelRowH : kSelEmptyH);
  return channels + dms;
}

int selMaxScroll(const Rect& list, int channelCount, int dmCount) {
  const int content = selContentHeight(channelCount, dmCount);
  return content > list.h ? content - list.h : 0;
}

namespace {

// Top of one entry's row, in scrolling-list pixels (0 = the list's first pixel at scroll 0).
int selRowOffset(int channelCount, int dmCount, int entry) {
  if (entry < 0) return -1;
  if (entry < channelCount) return kSelSectionH + entry * kSelRowH;
  const int dm = entry - channelCount;
  if (dm >= dmCount) return -1;
  return kSelSectionH + channelCount * kSelRowH + kSelSectionH + dm * kSelRowH;
}

}  // namespace

int selRowTop(const Rect& list, int channelCount, int dmCount, int entry, int scroll) {
  const int off = selRowOffset(channelCount, dmCount, entry);
  if (off < 0) return -1;
  return list.y + off - scroll;
}

int selRowAt(const Rect& list, int channelCount, int dmCount, int scroll, int py) {
  if (py < list.y || py >= list.bottom()) return -1;
  for (int i = 0; i < channelCount + dmCount; ++i) {
    // EST-382: ask the SAME row layout the drawing uses, so the row that is drawn is the row a tap
    // selects (the dot and count rects hang off this one; unread does not change the row's extent).
    const SelRowLayout o = selRowLayoutFor(list, channelCount, dmCount, i, scroll, 0, false);
    if (o.row.contains(list.x, py)) return i;   // x is inside by the list guard above
  }
  return -1;
}

// The list follows the finger. A larger scroll moves the rows UP (selRowTop subtracts it), so a finger
// that went UP by dyUp (screen y decreased) has to INCREASE the scroll by dyUp. This is the sign that
// was wrong: the old drag did `base - dy` and the list ran away from the finger (EST-334).
int selDragScroll(int base, int dyUp, int maxScroll) {
  int next = base + dyUp;
  if (next < -kSelGive) next = -kSelGive;
  if (next > maxScroll + kSelGive) next = maxScroll + kSelGive;
  return next;
}

int msgContentHeight(const int16_t* heights, int n) {
  if (heights == nullptr || n <= 0) return 0;
  int h = 2 * kMsgPadY + (n - 1) * kMsgGap;
  for (int i = 0; i < n; ++i) h += heights[i];
  return h;
}

int msgMaxScroll(const Rect& band, const int16_t* heights, int n) {
  const int content = msgContentHeight(heights, n);
  return content > band.h ? content - band.h : 0;
}

void msgSpans(const Rect& band, const int16_t* heights, int n, int scroll, int16_t* top) {
  if (heights == nullptr || top == nullptr) return;
  int y = band.bottom() - kMsgPadY + scroll;
  for (int i = n - 1; i >= 0; --i) {
    y -= heights[i];
    top[i] = (int16_t)y;
    y -= kMsgGap;
  }
}

int msgAt(const Rect& band, const int16_t* heights, int n, int scroll, int py) {
  if (heights == nullptr || n <= 0) return -1;
  if (py < band.y || py >= band.bottom()) return -1;
  int y = band.bottom() - kMsgPadY + scroll;
  for (int i = n - 1; i >= 0; --i) {
    const int top = y - heights[i];
    if (py >= top && py < top + heights[i]) return i;
    y = top - kMsgGap;
  }
  return -1;
}

int rowKeyRects(const Rect& row, const uint8_t* units, int n, int gap, Rect* out) {
  if (units == nullptr || out == nullptr || n <= 0) return 0;
  int total = 0;
  for (int i = 0; i < n; ++i) total += units[i] > 0 ? units[i] : 1;
  const int avail = row.w - gap * (n - 1);
  if (total <= 0 || avail <= 0) return 0;
  // Boundaries from the running unit count, so rounding never accumulates: every key is within one
  // pixel of its exact share and the row still ends on its right edge.
  int cum = 0;
  for (int i = 0; i < n; ++i) {
    const int left = row.x + i * gap + (avail * cum) / total;
    cum += units[i] > 0 ? units[i] : 1;
    const int right = (i == n - 1) ? row.right() : row.x + i * gap + (avail * cum) / total;
    int w = right - left;
    if (w < 1) w = 1;
    out[i] = Rect{(int16_t)left, row.y, (int16_t)w, row.h};
  }
  return n;
}

// --- EST-382: the ADVERT key and its small dropdown --------------------------------------------
ChatAdvertLayout chatAdvertLayoutFor(const Layout& L) {
  ChatAdvertLayout a;
  const int16_t headY = L.content.y;
  const int16_t h = kChatHeadH;
  // Right-aligned in the header with the same side padding the other header pieces use; the height is
  // the header's, so the drawn key IS the 62 x 26 tap target.
  a.button = Rect{(int16_t)(L.content.right() - kHeadPadX - kAdvertW), headY, kAdvertW, h};
  // The panel hangs under the key, right edges aligned, two rows and a 1 px border. It never leaves
  // the screen on the left and is drawn over the message band.
  const int16_t menuW = kAdvertOptW;
  int16_t menuX = (int16_t)(a.button.right() - menuW);
  if (menuX < 2) menuX = 2;
  const int16_t menuH = (int16_t)(2 * kAdvertOptH + 2 * kAdvertMenuPad);
  a.menu = Rect{menuX, (int16_t)(headY + h), menuW, menuH};
  for (int i = 0; i < 2; ++i) {
    a.opt[i] = Rect{(int16_t)(a.menu.x + kAdvertMenuPad),
                    (int16_t)(a.menu.y + kAdvertMenuPad + i * kAdvertOptH),
                    (int16_t)(a.menu.w - 2 * kAdvertMenuPad), kAdvertOptH};
  }
  return a;
}

ChatAdvertHit chatAdvertHitAt(const ChatAdvertLayout& a, bool open, int x, int y) {
  if (open) {
    if (a.opt[0].contains(x, y)) return ChatAdvertHit::Flood;
    if (a.opt[1].contains(x, y)) return ChatAdvertHit::Local;
    if (a.menu.contains(x, y)) return ChatAdvertHit::MenuOther;
    return ChatAdvertHit::None;
  }
  return a.button.contains(x, y) ? ChatAdvertHit::Button : ChatAdvertHit::None;
}

}  // namespace ui
}  // namespace om
