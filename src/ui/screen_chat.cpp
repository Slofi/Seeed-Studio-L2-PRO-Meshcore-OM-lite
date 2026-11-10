// SPDX-License-Identifier: MIT
// The Chat tab. Every rectangle comes from chat_layout.h (or from a widget), every colour and size
// from theme.h, and every pixel goes into the off-screen frame that chatScreenDraw opens.
#include "ui/screen_chat.h"

#include <stdio.h>
#include <string.h>

#include "../fonts.h"   // om::useAppFont: leave the 12 px default for the old screens
#include "board.h"
#include "mesh_link.h"   // meshPathHopName: naming the repeaters a packet came over
#include "ui/age.h"       // the one timestamp formatter
#include "ui/badge_font.h"
#include "ui/chat_selector.h"   // EST-350: one selector, drawn here and in Settings > Chat
#include "ui/compose_text.h"
#include "ui/dm_delivery.h"
#include "ui/emoji_picker.h"
#include "ui/fonts.h"
#include "ui/keyboard_draw.h"   // the one key-cap / accent-bubble drawing (EST-352)
#include "ui/text_wrap.h"
#include "ui/theme.h"

namespace om {
namespace ui {
namespace {

// EST-350: the ChatView's selector fields as the shared model (ui/chat_selector.h). One adapter, so
// the drawing and the hit test read the same numbers and Settings has its own model of the same shape.
SelectorModel selectorModelOf(const ChatView& v) {
  SelectorModel m;
  m.chats = v.chats;
  m.chatCount = v.chatCount;
  m.channelCount = v.channelCount;
  m.dmCount = v.dmCount;
  m.selected = v.selChat;
  m.scroll = v.selScroll;
  m.nowSec = v.nowSec;
  m.nowWall = v.nowWall;
  m.nowReal = v.nowReal;
  return m;
}

struct MeasureCtx {
  lgfx::LovyanGFX* d;
  FontRole role;
};

int measureRange(void* ctx, const char* text, int len) {
  MeasureCtx* c = (MeasureCtx*)ctx;
  return measureText(c->d, c->role, text, len);
}

// The one formatter (ui/age.cpp) takes both clocks and the realness flag. The chat line and the
// selector use the narrow Clock form (five glyphs, kMsgStampW); the sheet header uses DayTime.
void messageStamp(char* out, size_t cap, const ChatView& v, const ChatMsgView& m, StampForm form) {
  Stamp s;
  s.wall = m.wall;
  s.uptime = m.ts;
  s.wallReal = m.wallReal;
  stampText(out, cap, s, v.nowWall, v.nowSec, v.nowReal, form);
}

// An unread count as the screens show it: one digit, or "9+" above nine (ui/badge_font.h holds the
// one rule, so the red badge and the gold text can never disagree).
void countText(char* out, size_t cap, int n) {
  badgeText(n, out, (int)cap);
}

// The last `maxW` pixels of s: a long draft keeps its tail visible instead of running off the line.
const char* tailFits(lgfx::LovyanGFX* d, FontRole role, int maxW, const char* s) {
  if (maxW <= 0) return s + strlen(s);
  const char* p = s;
  while (*p != 0 && measureText(d, role, p, (int)strlen(p)) > maxW) {
    ++p;
    // Keep the tail on a code point boundary: a dropped first byte would draw as a placeholder.
    while ((*p & 0xC0) == 0x80) ++p;
  }
  return p;
}

// A backspace key without a glyph lives in ui/widgets.cpp (drawKeyCap), so the chat and the
// Settings editors draw the same key.

// The header's and the Manage row's chevrons: vectors, because the faces carry no arrows.
void drawChevron(lgfx::LovyanGFX* d, int x, int cy, uint16_t col, bool up, bool right) {
  if (right) {
    d->drawLine(x, cy - 3, x + 3, cy, col);
    d->drawLine(x + 3, cy, x, cy + 3, col);
    return;
  }
  if (up) {
    d->drawLine(x, cy + 2, x + 3, cy - 2, col);
    d->drawLine(x + 3, cy - 2, x + 6, cy + 2, col);
  } else {
    d->drawLine(x, cy - 2, x + 3, cy + 2, col);
    d->drawLine(x + 3, cy + 2, x + 6, cy - 2, col);
  }
}

// Wraps one message and reports the width the first line loses to the time and the name.
int wrapMessage(lgfx::LovyanGFX* d, const ChatView& v, const ChatMsgView& m, const Rect& band,
                WrapLine* lines, int maxLines, int* prefixOut) {
  char time[16];
  messageStamp(time, sizeof(time), v, m, StampForm::Clock);
  // Fixed five-glyph column: the name and the text do not move when the stamp is an age fallback.
  const int timeW = kMsgStampW;
  const int nameW = textWidth(d, FontRole::MonoMeta, m.name);
  const int prefix = kMsgPadX + timeW + kMsgHdrGap + nameW + kMsgHdrGap;
  const int firstW = band.w - prefix - kMsgPadX;
  const int nextW = band.w - 2 * kMsgPadX;
  MeasureCtx ctx{d, FontRole::SansBody};
  if (prefixOut) *prefixOut = prefix;
  const char* text = m.text ? m.text : "";
  return wrapText(text, measureRange, &ctx, firstW > 0 ? firstW : nextW, nextW, lines, maxLines);
}

// The drawn height of every message block. The draw and the hit test both use this, so a tap lands
// on the block the eye sees. My own DMs get one more line: their delivery state (EST-342).
int chatMsgHeights(const ChatView& v, int16_t* out, int max) {
  lgfx::LovyanGFX* d = om_display();
  const ChatLayout g = chatLayoutFor(l2(), v.osk, v.replySeq != 0, v.nums, v.selector);
  int n = 0;
  if (d == nullptr) return 0;
  for (int i = 0; i < v.msgCount && i < max; ++i) {
    WrapLine lines[kMaxWrapLines];
    const int nl = wrapMessage(d, v, v.msgs[i], g.msgs, lines, kMaxWrapLines, nullptr);
    const int statusLine = (v.msgs[i].dm && v.msgs[i].mine) ? 1 : 0;
    out[n++] = (int16_t)((nl + statusLine) * kMsgLineH);
  }
  return n;
}

const ChatEntry* openChat(const ChatView& v) {
  if (v.chats == nullptr || v.selChat < 0 || v.selChat >= v.chatCount) return nullptr;
  return &v.chats[v.selChat];
}

// The header row: the open chat's name with a down chevron, and the gold dot counting unread in the
// other chats on the right.
void drawHead(lgfx::LovyanGFX* d, const ChatView& v, const ChatLayout& g) {
  const uint16_t bg = v.selector ? kSelBg : kScreen;
  d->fillRect(g.head.x, g.head.y, g.head.w, g.head.h, bg);
  d->drawFastHLine(g.head.x, g.head.bottom() - 1, g.head.w, kLine);

  const ChatEntry* cur = openChat(v);
  const char* name = cur ? cur->name : "No channels";
  const int cy = g.head.y + g.head.h / 2;
  const int room = g.headTitle.w - kHeadChevronW - kHeadChevronGap;
  const int nameW = textWidth(d, FontRole::SansBody, name);
  if (nameW > room) {
    drawTextEllipsis(d, FontRole::SansBody, kInk, bg, g.headTitle.x, cy, room, name);
  } else {
    drawTextLeft(d, FontRole::SansBody, kInk, bg, g.headTitle.x, cy, name);
  }
  const int shown = nameW > room ? room : nameW;
  drawChevron(d, g.headTitle.x + shown + kHeadChevronGap, cy, kDim, v.selector, false);

  const int elsewhere = chatUnreadElsewhere(v.chats, v.chatCount, v.selChat);
  if (elsewhere > 0) {
    char b[4];
    countText(b, sizeof(b), elsewhere);
    drawTextRight(d, FontRole::MonoMeta, kAcc, bg, g.headDot.x - kElsewhereGap, cy, b);
    d->fillCircle(g.headDot.x + kElsewhereDot / 2, cy, kElsewhereDot / 2, kAcc);
  }
}

void drawMessage(lgfx::LovyanGFX* d, const ChatView& v, const ChatMsgView& m, const Rect& band,
                 int16_t top) {
  WrapLine lines[kMaxWrapLines];
  int prefix = 0;
  const int nl = wrapMessage(d, v, m, band, lines, kMaxWrapLines, &prefix);
  char time[16];
  messageStamp(time, sizeof(time), v, m, StampForm::Clock);
  const int timeW = kMsgStampW;
  const int padX = band.x + kMsgPadX;
  const int cy = top + kMsgLineH / 2;

  drawTextLeft(d, FontRole::MonoMeta, kDimmer, kScreen, padX, cy, time);
  drawTextLeft(d, FontRole::MonoMeta, m.mine ? kGreen : kAcc, kScreen, padX + timeW + kMsgHdrGap,
               cy, m.name);

  char buf[kMeshTextCap];   // one wrapped line of a MAX_TEXT_LEN message
  for (int r = 0; r < nl; ++r) {
    int len = lines[r].len;
    if (len < 0) len = 0;
    if (lines[r].start + len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1 - lines[r].start;
    if (len > 0) memcpy(buf, m.text + lines[r].start, (size_t)len);
    buf[len] = 0;
    const int lx = (r == 0) ? padX + prefix : padX;
    drawTextLeft(d, FontRole::SansBody, kInk, kScreen, lx, top + kMsgLineH / 2 + r * kMsgLineH,
                 buf);
  }

  // My own DMs say whether MeshCore's ACK came back, and when (EST-342). One extra line under the
  // text; chatMsgHeights counts it, so the tap band includes it.
  if (m.dm && m.mine) {
    const DmState st = (DmState)m.delivery;
    char state[48];
    if (st == DmState::Delivered && m.ackWall != 0) {
      Stamp s;
      s.wall = m.ackWall;
      s.uptime = m.ts;
      s.wallReal = m.ackReal;
      char when[24];
      stampText(when, sizeof(when), s, v.nowWall, v.nowSec, v.nowReal, StampForm::DayTime);
      snprintf(state, sizeof(state), "delivered %s", when);
    } else {
      snprintf(state, sizeof(state), "%s", dmStateToken(st));
    }
    drawTextLeft(d, FontRole::MonoMeta, st == DmState::Delivered ? kDim : kDimmer, kScreen,
                 padX, top + kMsgLineH / 2 + nl * kMsgLineH, state);
  }
}

void drawMessages(lgfx::LovyanGFX* d, const ChatView& v, const ChatLayout& g) {
  d->fillRect(g.msgs.x, g.msgs.y, g.msgs.w, g.msgs.h, kScreen);
  if (v.msgCount <= 0) {
    if (v.chatCount <= 0) {
      drawEmptyState(d, g.msgs, "No channels", "Add one from Manage channels.");
    } else {
      drawEmptyState(d, g.msgs, "No messages yet", "");
    }
    return;
  }
  int16_t heights[kMaxChatMsgs];
  int16_t tops[kMaxChatMsgs];
  const int n = chatMsgHeights(v, heights, kMaxChatMsgs);
  msgSpans(g.msgs, heights, n, v.scroll, tops);
  d->setClipRect(g.msgs.x, g.msgs.y, g.msgs.w, g.msgs.h);
  for (int i = 0; i < n; ++i) {
    if (tops[i] >= g.msgs.bottom()) continue;
    if (tops[i] + heights[i] <= g.msgs.y) continue;
    drawMessage(d, v, v.msgs[i], g.msgs, tops[i]);
  }
  d->clearClipRect();
}

// The draft text, its caret and (near the limit) the remaining bytes, shared by the compose field and
// the sheet's draft line. The counter takes its room out of the text before the tail is measured, so
// the tail can never run under it (EST-334).
void drawDraft(lgfx::LovyanGFX* d, const Rect& field, uint16_t bg, const ChatView& v, bool focus) {
  const char* draft = v.draft ? v.draft : "";
  // A channel text counts "<our name>: " against MAX_TEXT_LEN (and a reply spends another
  // "@Name "); a DM frame spends only its timestamp and attempt bytes (EST-342).
  const int allow = v.dmThread
      ? kMeshDmTextMax
      : composeAllowance(v.replySeq != 0, v.replyName, v.ownName);
  const int left = composeRemaining(allow, (int)strlen(draft));

  char count[8];
  int countW = 0;
  if (left <= kComposeWarn) {
    snprintf(count, sizeof(count), "%d", left);
    countW = textWidth(d, FontRole::MonoMeta, count) + kComposeCountGap;
  }

  const int x = field.x + kFieldPadX;
  const int cy = field.y + field.h / 2;
  const int room = field.w - 2 * kFieldPadX - kCaretW - kHairline - countW;
  const char* tail = tailFits(d, FontRole::MonoMeta, room, draft);
  if (*tail == 0) {
    drawTextLeft(d, FontRole::MonoMeta, kDimmer, bg, x, cy, "message");
    if (focus) d->fillRect(x, cy - kCaretH / 2, kCaretW, kCaretH, kAcc);
  } else {
    const int textW = textWidth(d, FontRole::MonoMeta, tail);
    drawTextLeft(d, FontRole::MonoMeta, kInk, bg, x, cy, tail);
    if (focus) d->fillRect(x + textW, cy - kCaretH / 2, kCaretW, kCaretH, kAcc);
  }
  if (countW > 0) {
    drawTextRight(d, FontRole::MonoMeta, kDim, bg, field.right() - kFieldPadX, cy, count);
  }
}

// The compose line: the draft field only. The Send button moved into the keyboard (Filip, 2026-11-10)
// and the keyboard button is gone as well: a tap anywhere on the field opens the sheet.
void drawCompose(lgfx::LovyanGFX* d, const ChatView& v, const ChatLayout& g) {
  d->fillRect(g.compose.x, g.compose.y, g.compose.w, g.compose.h, kScreen);
  d->drawFastHLine(g.compose.x, g.compose.y, g.compose.w, kLine);

  d->fillRoundRect(g.field.x, g.field.y, g.field.w, g.field.h, kRadius, kFieldBg);
  d->drawRoundRect(g.field.x, g.field.y, g.field.w, g.field.h, kRadius, kLine);
  drawDraft(d, g.field, kFieldBg, v, false);
}

// EST-346: the long press's preview, a small bubble ABOVE the key being held, with the letter that a
// release will enter (š / č / ž, or the capital with shift). Its rectangle comes from the key the
// layout already handed out, so the bubble always sits over the right key. The drawing itself is
// ui/widgets.cpp's drawAccentBubble, shared with the Settings editors (EST-352).
void drawAccentBubble(lgfx::LovyanGFX* d, const ChatLayout& g, const ChatView& v) {
  if (v.accentCp == 0 || v.accentRow < 0 || v.accentRow > 3 || v.accentCol < 0 ||
      v.accentCol >= g.keyCount[v.accentRow]) {
    return;
  }
  ui::drawAccentBubble(d, g.keys[v.accentRow][v.accentCol], v.accentCp);
}

// The emoji page: a rail of group icons plus "abc", the Recent strip with Send, and the scrolling
// 22 px grid. Every rectangle comes from emojiPadLayout(), the same one the hit test uses.
void drawEmojiPage(lgfx::LovyanGFX* d, const ChatView& v, const ChatLayout& g) {
  const EmojiGroup* group = emojiGroupAt(v.emojiGroup);
  const int count = group ? group->count : 0;
  const EmojiPadLayout e =
      emojiPadLayout(l2(), g, v.emojiRecentCount, v.emojiGroup, v.emojiScroll, count);
  if (e.area.empty()) return;

  for (int i = 0; i < kEmojiGroupCount; ++i) {
    const bool on = (i == v.emojiGroup);
    d->fillRoundRect(e.cat[i].x, e.cat[i].y, e.cat[i].w, e.cat[i].h, kKeyRadius,
                     on ? kSelBg : kKeyBg);
    d->drawRoundRect(e.cat[i].x, e.cat[i].y, e.cat[i].w, e.cat[i].h, kKeyRadius,
                     on ? kAcc : kKeyLine);
    const uint32_t icon = emojiGroupAt(i) ? emojiGroupAt(i)->icon : 0;
    const int cy = e.cat[i].y + e.cat[i].h / 2;
    if (!drawEmoji(d, icon, e.cat[i].x + (e.cat[i].w - 13) / 2, cy,
                   on ? kSelBg : kKeyBg)) {
      d->drawRect(e.cat[i].x + 12, cy - kMissingBoxH / 2, 16, kMissingBoxH, kDimmer);
    }
  }

  d->fillRoundRect(e.abc.x, e.abc.y, e.abc.w, e.abc.h, kKeyRadius, kKeyBg);
  d->drawRoundRect(e.abc.x, e.abc.y, e.abc.w, e.abc.h, kKeyRadius, kKeyLine);
  setRole(d, FontRole::MonoMeta);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(kInk, kKeyBg);
  d->drawString("abc", e.abc.x + e.abc.w / 2, e.abc.y + e.abc.h / 2);

  // The Recent strip: the last 16 picks, most recent on the left. The newline under it separates
  // it from the scrolling grid without a second label stealing a row.
  for (int i = 0; i < v.emojiRecentCount && i < kEmojiRecentMax; ++i) {
    const Rect r = e.recent[i];
    if (r.empty()) continue;
    const int cy = r.y + r.h / 2;
    if (!drawEmoji(d, v.emojiRecent[i], r.x + (r.w - 13) / 2, cy, kSheet)) {
      d->drawRect(r.x + 2, cy - kMissingBoxH / 2, r.w - 4, kMissingBoxH, kDimmer);
    }
  }
  d->drawFastHLine(e.recentStrip.x, e.recentStrip.bottom() - 1, e.recentStrip.w, kRowLine);

  d->fillRoundRect(e.send.x, e.send.y, e.send.w, e.send.h, kKeyRadius, kSelBg2);
  d->drawRoundRect(e.send.x, e.send.y, e.send.w, e.send.h, kKeyRadius, kAcc);
  setRole(d, FontRole::SansTab);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(kAccInk, kSelBg2);
  d->drawString("Send", e.send.x + e.send.w / 2, e.send.y + e.send.h / 2);

  // Only the cells that intersect the viewport are painted. A clip is still set, so a half cell
  // at the top or bottom edge is cut exactly where the drag put it.
  d->setClipRect(e.grid.x, e.grid.y, e.grid.w, e.grid.h);
  for (int i = 0; i < count; ++i) {
    const Rect c = emojiPadCell(e, i);
    if (c.empty() || !c.overlaps(e.grid)) continue;
    const int cy = c.y + c.h / 2;
    if (!drawEmoji(d, group->cps[i], c.x + (c.w - 13) / 2, cy, kSheet)) {
      d->drawRect(c.x + 4, cy - kMissingBoxH / 2, c.w - 8, kMissingBoxH, kDimmer);
    }
  }
  d->clearClipRect();
}

void drawSheet(lgfx::LovyanGFX* d, const ChatView& v, const ChatLayout& g) {
  // Rounded at the top only: the body is drawn past the bottom edge so the lower corners fall off
  // the screen, exactly like the mock's .osk sitting on the frame's floor.
  d->fillRoundRect(g.sheet.x, g.sheet.y, g.sheet.w, g.sheet.h + kOskRadius, kOskRadius, kSheet);
  d->drawFastHLine(g.sheet.x, g.sheet.y, g.sheet.w, kLine);

  if (g.replyBar.w > 0 && v.replySeq != 0) {
    d->fillRoundRect(g.replyBar.x, g.replyBar.y, g.replyBar.w, g.replyBar.h, kRadiusSm, kReplyBg);
    d->drawRoundRect(g.replyBar.x, g.replyBar.y, g.replyBar.w, g.replyBar.h, kRadiusSm, kReplyLine);
    char bar[kMeshNameCap + 3];   // "RE " + the whole name, MeshCore's own limit
    // The name was copied when REPLY was tapped, so this bar still names the right sender even after
    // the ring has shifted and v.msgs no longer contains that message.
    snprintf(bar, sizeof(bar), "RE %s", v.replyName ? v.replyName : "");
    drawTextEllipsis(d, FontRole::MonoMeta, kReplyInk, kReplyBg, g.replyBar.x + kFieldPadX,
                     g.replyBar.y + g.replyBar.h / 2, g.replyBar.w - 2 * kFieldPadX, bar);
  }

  d->fillRoundRect(g.draft.x, g.draft.y, g.draft.w, g.draft.h, kKeyRadius, kCodeBg);
  d->drawRoundRect(g.draft.x, g.draft.y, g.draft.w, g.draft.h, kKeyRadius, kAcc);
  drawDraft(d, g.draft, kCodeBg, v, true);

  if (v.emoji) {
    drawEmojiPage(d, v, g);
  } else {
    for (int r = 0; r < 4; ++r) {
      KeyDef defs[10];
      const int n = keyRow(v.nums, r, defs);
      for (int c = 0; c < n && c < g.keyCount[r]; ++c) drawKeyCap(d, g.keys[r][c], defs[c], v.shift);
    }
    // Above the keys, after them: the held key's preview bubble (EST-346).
    drawAccentBubble(d, g, v);
  }
}

// One path hash, named when it matches a known repeater's public-key prefix (mesh_link scans the
// contact table), "" otherwise, which leaves the hex in formatMessagePath().
const char* resolvePathHop(void*, const uint8_t* hash, uint8_t len) {
#if defined(OM_ENABLE_MESH)
  static char name[kMeshNameMax];
  return om::meshPathHopName(hash, len, name, sizeof(name));
#else
  (void)hash;
  (void)len;
  return "";
#endif
}

void formatSheetPath(const ChatView& v, char* out, size_t cap) {
  out[0] = 0;
  if (v.menuPath == nullptr || v.menuPathCount == 0) return;
  formatMessagePath(out, cap, v.menuPath, v.menuPathSize, v.menuPathCount, resolvePathHop, nullptr);
}

// The open message's rows. `pathText` must outlive the rows; every caller keeps both as locals.
int buildSheetRows(const ChatView& v, const char* pathText, SheetRow* out) {
  SheetContext c;
  const ChatMsgView& m = v.msgs[v.menuMsg];
  c.mine = m.mine;
  c.senderHasKey = v.menuSenderHasKey;
  c.alreadyDm = v.menuInDm;
  c.dm = m.dm;
  c.senderKnown = v.menuSenderKnown;
  c.senderHasPosition = v.menuSenderHasPos;
  c.pathKnown = v.menuPathCount > 0 && v.menuPathSize >= 1 && v.menuPathSize <= 3;
  c.pathText = pathText;
  return messageSheetRows(c, out, kMaxSheetRows);
}

// The sheet's header: what INFO used to show, on two lines. Sender on the first (age at the right),
// channel, hops and SNR on the second, then the 1 px line the rows start under.
void drawSheetHead(lgfx::LovyanGFX* d, const ChatView& v, const ChatMsgView& m,
                   const MessageSheetLayout& g) {
  const int cy1 = g.head.y + kSheetLine1Cy;
  char age[24];
  messageStamp(age, sizeof(age), v, m, StampForm::DayTime);
  const int ageW = textWidth(d, FontRole::MonoMeta, age);
  drawTextEllipsis(d, FontRole::SansBody, kInk, kSheet, g.head.x, cy1,
                   g.head.w - ageW - kSheetGap, m.name);
  drawTextRight(d, FontRole::MonoMeta, kDim, kSheet, g.head.right(), cy1, age);

  char hops[24];
  char snr[28];
  // A channel message we sent is a local echo; a DM is not (it went direct), so an unknown hop
  // count reads "--" there, matching the Nodes surfaces (EST-344).
  if (m.hops < 0) snprintf(hops, sizeof(hops), m.dm ? "--" : "local echo");
  else snprintf(hops, sizeof(hops), "%d hop%s", (int)m.hops, m.hops == 1 ? "" : "s");
  if (m.hops < 0 || m.snrq == 0) snprintf(snr, sizeof(snr), "SNR --");
  else snprintf(snr, sizeof(snr), "SNR %.1f dB", (double)m.snrq / 4.0);
  char meta[96];
  snprintf(meta, sizeof(meta), "%s \xC2\xB7 %s \xC2\xB7 %s", m.chanName ? m.chanName : "", hops,
           snr);
  drawTextEllipsis(d, FontRole::MonoMeta, kDim, kSheet, g.head.x, g.head.y + kSheetLine2Cy,
                   g.head.w, meta);
  d->drawFastHLine(g.head.x, g.head.bottom() - 1, g.head.w, kLine);
}

// One action row: the label on the left, an optional mono value on the right (the path, the repeat
// count), and the 1 px row line the mock uses between rows.
void drawSheetRow(lgfx::LovyanGFX* d, const Rect& r, const SheetRow& row, int heard) {
  char heardText[8];
  const char* value = row.value;
  if (row.action == SheetAction::HeardRepeats) {
    snprintf(heardText, sizeof(heardText), "%d", heard < 0 ? 0 : heard);
    value = heardText;
  }
  const int cy = r.y + r.h / 2;
  const int labelW = textWidth(d, FontRole::SansBody, row.label);
  drawTextLeft(d, FontRole::SansBody, kInk, kSheet, r.x, cy, row.label);
  if (value != nullptr && value[0] != 0) {
    const int room = r.w - labelW - kSheetGap;
    if (room > 0) {
      const int w = textWidth(d, FontRole::MonoMeta, value);
      const int x = r.right() - (w <= room ? w : room);
      drawTextEllipsis(d, FontRole::MonoMeta, kDim, kSheet, x, cy, room, value);
    }
  }
  d->drawFastHLine(r.x, r.bottom() - 1, r.w, kRowLine);
}

void drawMessageSheet(lgfx::LovyanGFX* d, const ChatView& v) {
  if (v.msgs == nullptr || v.menuMsg < 0 || v.menuMsg >= v.msgCount) return;
  char path[96];
  formatSheetPath(v, path, sizeof(path));
  SheetRow rows[kMaxSheetRows];
  const int n = buildSheetRows(v, path, rows);
  const MessageSheetLayout g = messageSheetLayout(l2(), n);

  // Rounded at the top only, like the keyboard sheet: the body is drawn past the bottom edge so the
  // lower corners fall off the screen, and it covers the tab bar because it is drawn after it.
  d->fillRoundRect(g.panel.x, g.panel.y, g.panel.w, (int16_t)(g.panel.h + kOskRadius), kOskRadius,
                   kSheet);
  d->drawFastHLine(g.panel.x, g.panel.y, g.panel.w, kLine);
  drawSheetHead(d, v, v.msgs[v.menuMsg], g);
  for (int i = 0; i < n; ++i) drawSheetRow(d, g.rows[i], rows[i], v.menuHeard);
}

}  // namespace

Rect chatSheetPanel(const ChatView& v) {
  if (v.menuMsg < 0 || v.msgs == nullptr) return Rect{};
  char path[96];
  formatSheetPath(v, path, sizeof(path));
  SheetRow rows[kMaxSheetRows];
  const int n = buildSheetRows(v, path, rows);
  return messageSheetLayout(l2(), n).panel;
}

void chatScreenDraw(const ChatView& v) {
  if (!omFrameBegin()) {
    omFrameEnd();
    return;
  }
  lgfx::LovyanGFX* d = om_display();
  if (d == nullptr) {
    omFrameEnd();
    return;
  }
  const Layout& L = l2();
  const ChatLayout g = chatLayoutFor(L, v.osk, v.replySeq != 0, v.nums, v.selector);

  d->fillScreen(kScreen);
  drawStatusBar(d, L, v.status);
  drawHead(d, v, g);
  drawMessages(d, v, g);
  if (v.selector) {
    // EST-350: the SAME drawing the Settings > Chat selector uses (ui/chat_selector.cpp).
    drawChatSelector(d, selectorModelOf(v), g.selPanel, g.selList, g.selManage);
  }
  if (v.osk) drawSheet(d, v, g);
  else drawCompose(d, v, g);
  // The sheet covers the bottom edge, so the tab bar is not drawn while it is open (mock .kb-open).
  if (!v.osk) drawTabBar(d, L, Tab::Chat, v.unread, v.newNodeDot);
  // The message sheet is last: the mock's bottom sheet sits over the tab bar.
  if (v.menuMsg >= 0) drawMessageSheet(d, v);

  // The channel list and its pages are still the old screens and share this frame; leave them the
  // 12 px default font the old drawing code expects.
  om::useAppFont(d);
  omFrameEnd();
}

ChatHitResult chatScreenHit(const ChatView& v, int x, int y) {
  ChatHitResult r;
  const ChatLayout g = chatLayoutFor(l2(), v.osk, v.replySeq != 0, v.nums, v.selector);

  // The message sheet swallows the screen: a row is its action, a touch on its header or inside its
  // panel closes it. A touch OUTSIDE it never gets here: the shell closes the sheet on that press and
  // spends the whole gesture, so nothing behind it is pressed (EST-339).
  if (v.menuMsg >= 0 && v.msgs != nullptr) {
    char path[96];
    formatSheetPath(v, path, sizeof(path));
    SheetRow rows[kMaxSheetRows];
    const int n = buildSheetRows(v, path, rows);
    const MessageSheetLayout m = messageSheetLayout(l2(), n);
    const int row = messageSheetRowAt(m, x, y);
    if (row >= 0) {
      r.hit = ChatHit::SheetRow;
      r.sheet = rows[row].action;
      r.index = v.menuMsg;
      r.seq = v.menuSeq;
      return r;
    }
    r.hit = ChatHit::SheetClose;
    return r;
  }

  // The dropdown swallows the screen: a row opens that chat, the last row opens the channel
  // settings, a tap on the header or anywhere else closes it. EST-350: the hit test is the ONE in
  // ui/chat_selector.h, the same call Settings > Chat makes with its own rectangles.
  if (v.selector) {
    const SelectorPress h =
        selectorPress(selectorModelOf(v), g.selPanel, g.selList, g.selManage, x, y);
    if (h.kind == SelectorHitKind::Manage) {
      r.hit = ChatHit::SelManage;
      return r;
    }
    if (h.kind == SelectorHitKind::Row) {
      r.hit = ChatHit::SelRow;
      r.index = h.row;
      return r;
    }
    r.hit = ChatHit::SelClose;
    return r;
  }

  if (v.osk) {
    if (g.draft.contains(x, y)) {
      r.hit = ChatHit::Field;
      return r;
    }
    if (v.emoji) {
      const EmojiGroup* group = emojiGroupAt(v.emojiGroup);
      const EmojiPadLayout e = emojiPadLayout(l2(), g, v.emojiRecentCount, v.emojiGroup,
                                             v.emojiScroll, group ? group->count : 0);
      const EmojiPadHit h = emojiPadHit(e, x, y);
      if (h.abc) {
        r.hit = ChatHit::EmojiBack;
        return r;
      }
      if (h.cat >= 0) {
        r.hit = ChatHit::EmojiCat;
        r.emojiGroup = h.cat;
        return r;
      }
      if (h.send) {
        r.hit = ChatHit::EmojiSend;
        return r;
      }
      if (h.recent >= 0 && v.emojiRecent != nullptr && h.recent < v.emojiRecentCount) {
        r.hit = ChatHit::EmojiCell;
        r.emojiCp = v.emojiRecent[h.recent];
        return r;
      }
      if (h.cell >= 0 && group != nullptr && h.cell < group->count) {
        r.hit = ChatHit::EmojiCell;
        r.emojiCp = group->cps[h.cell];
        return r;
      }
      if (chatSheetClosesAt(g, x, y)) r.hit = ChatHit::Outside;
      return r;
    }
    // keyAt() uses keyHit: the outer keys reach the screen edges and the bottom row reaches the
    // floor, so a firm tap low on Send (or on 123/space) is still that key, not "outside" (EST-338).
    const KeyHit key = keyAt(g, v.nums, x, y);
    if (key.row >= 0) {
      KeyDef defs[10];
      const int n = keyRow(v.nums, key.row, defs);
      if (key.col < n) {
        r.hit = ChatHit::Key;
        r.keyRow = key.row;
        r.keyCol = key.col;
        r.keyKind = defs[key.col].kind;
        r.keyCh = defs[key.col].label[0];
        return r;
      }
    }
    // Anything above the sheet is OUTSIDE it: the shell has already closed the sheet on that press
    // and spends the gesture, so a message behind the keyboard is never tapped (Filip, EST-339: "I
    // pressed away, in the chat area, and it opened a chat menu"). Inside the sheet, a touch that is
    // not on a key/draft (a 2 px gap, the header line) does nothing at all.
    if (chatSheetClosesAt(g, x, y)) r.hit = ChatHit::Outside;
    return r;
  }

  if (g.head.contains(x, y)) {
    r.hit = ChatHit::Head;
    return r;
  }
  // The whole compose row is the draft field: a tap anywhere on it opens the keyboard sheet.
  if (g.field.contains(x, y)) {
    r.hit = ChatHit::Field;
    return r;
  }
  if (g.msgs.contains(x, y)) {
    int16_t heights[kMaxChatMsgs];
    const int n = chatMsgHeights(v, heights, kMaxChatMsgs);
    const int i = msgAt(g.msgs, heights, n, v.scroll, y);
    if (i >= 0) {
      r.index = i;
      r.seq = v.msgs[i].seq;
      // A tap on the sender's NAME is the same as the sheet's "Direct message" row (EST-342); a tap
      // anywhere else on the message still opens the sheet.
      Rect name;
      if (!v.msgs[i].mine && !v.msgs[i].dm && chatSenderNameRect(v, i, &name) &&
          name.contains(x, y)) {
        r.hit = ChatHit::SenderName;
      } else {
        r.hit = ChatHit::Msg;
      }
    }
  }
  return r;
}

bool chatSenderNameRect(const ChatView& v, int msgIndex, Rect* out) {
  if (out == nullptr || v.msgs == nullptr || msgIndex < 0 || msgIndex >= v.msgCount) return false;
  lgfx::LovyanGFX* d = om_display();
  if (d == nullptr) return false;
  const ChatLayout g = chatLayoutFor(l2(), v.osk, v.replySeq != 0, v.nums, v.selector);
  int16_t heights[kMaxChatMsgs];
  int16_t tops[kMaxChatMsgs];
  const int n = chatMsgHeights(v, heights, kMaxChatMsgs);
  if (msgIndex >= n) return false;
  msgSpans(g.msgs, heights, n, v.scroll, tops);
  const int nameW = textWidth(d, FontRole::MonoMeta, v.msgs[msgIndex].name);
  *out = Rect{(int16_t)(g.msgs.x + kMsgPadX + kMsgStampW + kMsgHdrGap), tops[msgIndex],
              (int16_t)nameW, kMsgLineH};
  return true;
}

int chatContentHeight(const ChatView& v) {
  int16_t heights[kMaxChatMsgs];
  const int n = chatMsgHeights(v, heights, kMaxChatMsgs);
  return msgContentHeight(heights, n);
}

int chatMaxScroll(const ChatView& v) {
  const ChatLayout g = chatLayoutFor(l2(), v.osk, v.replySeq != 0, v.nums, v.selector);
  int16_t heights[kMaxChatMsgs];
  const int n = chatMsgHeights(v, heights, kMaxChatMsgs);
  return msgMaxScroll(g.msgs, heights, n);
}

int chatSelectorMaxScroll(const ChatView& v) {
  if (!v.selector) return 0;   // the dropdown is closed: nothing to scroll
  const ChatLayout g = chatLayoutFor(l2(), v.osk, v.replySeq != 0, v.nums, v.selector);
  return selectorMaxScroll(selectorModelOf(v), g.selList);   // EST-350: the shared clamp
}

int chatEmojiMaxScroll(const ChatView& v) {
  if (!v.osk || !v.emoji) return 0;
  const ChatLayout g = chatLayoutFor(l2(), v.osk, v.replySeq != 0, v.nums, v.selector);
  const EmojiGroup* group = emojiGroupAt(v.emojiGroup);
  const int count = group ? group->count : 0;
  const EmojiPadLayout e =
      emojiPadLayout(l2(), g, v.emojiRecentCount, v.emojiGroup, v.emojiScroll, count);
  return emojiPadMaxScroll(e, count);
}

}  // namespace ui
}  // namespace om
