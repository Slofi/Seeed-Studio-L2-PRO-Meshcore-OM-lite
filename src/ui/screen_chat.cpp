// SPDX-License-Identifier: MIT
// The Chat tab. Every rectangle comes from chat_layout.h (or from a widget), every colour and size
// from theme.h, and every pixel goes into the off-screen frame that chatScreenDraw opens.
#include "ui/screen_chat.h"

#include <stdio.h>
#include <string.h>

#include "../fonts.h"   // om::useAppFont: leave the 12 px default for the old screens
#include "board.h"
#include "mesh_link.h"   // meshPathHopName: naming the repeaters a packet came over
#include "ui/badge_font.h"
#include "ui/compose_text.h"
#include "ui/fonts.h"
#include "ui/text_wrap.h"
#include "ui/theme.h"

namespace om {
namespace ui {
namespace {

struct MeasureCtx {
  lgfx::LovyanGFX* d;
  FontRole role;
};

int measureRange(void* ctx, const char* text, int len) {
  MeasureCtx* c = (MeasureCtx*)ctx;
  return measureText(c->d, c->role, text, len);
}

// Message ages use the monotonic uptime stamps, not the wall clock: a GPS sync can jump the system
// time forward mid-session, and an age must not go negative or leap by months because of it.
void ageText(char* out, size_t cap, uint32_t nowSec, uint32_t ts) {
  const uint32_t age = (nowSec >= ts) ? (nowSec - ts) : 0;
  if (age < 60) snprintf(out, cap, "%us", (unsigned)age);
  else if (age < 3600) snprintf(out, cap, "%um", (unsigned)(age / 60));
  else snprintf(out, cap, "%uh", (unsigned)(age / 3600));
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

// A backspace key without a glyph: the mock's ⌫ is not in the ASCII-only faces.
void drawBackspace(lgfx::LovyanGFX* d, const Rect& r, uint16_t col) {
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  const int hw = 4;
  const int hh = 4;
  d->drawLine(cx + hw, cy - hh, cx - hw, cy - hh, col);
  d->drawLine(cx - hw, cy - hh, cx - hw - 3, cy, col);
  d->drawLine(cx - hw - 3, cy, cx - hw, cy + hh, col);
  d->drawLine(cx - hw, cy + hh, cx + hw, cy + hh, col);
  d->drawLine(cx + hw, cy + hh, cx + hw, cy - hh, col);
  d->drawLine(cx - 3, cy - 2, cx + 1, cy + 2, col);
  d->drawLine(cx + 1, cy - 2, cx - 3, cy + 2, col);
}

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
  char time[8];
  ageText(time, sizeof(time), v.nowSec, m.ts);
  const int timeW = textWidth(d, FontRole::MonoMeta, time);
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
// on the block the eye sees.
int chatMsgHeights(const ChatView& v, int16_t* out, int max) {
  lgfx::LovyanGFX* d = om_display();
  const ChatLayout g = chatLayoutFor(l2(), v.osk, v.replySeq != 0, v.nums, v.selector);
  int n = 0;
  if (d == nullptr) return 0;
  for (int i = 0; i < v.msgCount && i < max; ++i) {
    WrapLine lines[kMaxWrapLines];
    const int nl = wrapMessage(d, v, v.msgs[i], g.msgs, lines, kMaxWrapLines, nullptr);
    out[n++] = (int16_t)(nl * kMsgLineH);
  }
  return n;
}

const ChatEntry* openChat(const ChatView& v) {
  if (v.chats == nullptr || v.selChat < 0 || v.selChat >= v.chatCount) return nullptr;
  return &v.chats[v.selChat];
}

// One row of the dropdown: the chat's name and the last message's age on the first line, its sender
// and the start of its text on the second, the unread count at the right. The open chat carries the
// gold dot.
void drawSelRow(lgfx::LovyanGFX* d, const ChatView& v, const ChatEntry& e, int entry,
                const Rect& list) {
  const int top = selRowTop(list, v.channelCount, v.dmCount, entry, v.selScroll);
  const Rect row{list.x, (int16_t)top, list.w, kSelRowH};
  if (!row.overlaps(list)) return;
  const bool on = (entry == v.selChat);
  if (on) d->fillRect(row.x, row.y, row.w, row.h, kSelBg);

  const int cy1 = row.y + kSelLine1Cy;
  const int cy2 = row.y + kSelLine2Cy;
  const int dotX = row.x + kSelPadX;
  if (on) d->fillCircle(dotX + kSelDot / 2, cy1, kSelDot / 2, kAcc);
  const int nameX = dotX + kSelDot + kSelDotGap;

  int right = row.right() - kSelPadX;
  char age[8];
  if (e.hasLast) {
    ageText(age, sizeof(age), v.nowSec, e.lastTs);
    drawTextRight(d, FontRole::MonoMeta, kDimmer, on ? kSelBg : kPanel, right, cy1, age);
    right -= textWidth(d, FontRole::MonoMeta, age) + kSelGap;
  }
  drawTextEllipsis(d, FontRole::SansBody, on ? kAccInk : kInk, on ? kSelBg : kPanel, nameX, cy1,
                   right - nameX, e.name);

  int textRight = row.right() - kSelPadX;
  if (e.unread > 0) {
    char b[4];
    countText(b, sizeof(b), e.unread);
    drawTextRight(d, FontRole::MonoMeta, kAcc, on ? kSelBg : kPanel, textRight, cy2, b);
    textRight -= textWidth(d, FontRole::MonoMeta, b) + kSelGap;
  }
  char line[kMeshNameCap + 2 + kMeshTextCap];   // "from: text", both at MeshCore's own limits
  if (e.hasLast) {
    snprintf(line, sizeof(line), "%s: %s", e.lastFrom, e.lastText);
    // A preview line may still be too wide: drawTextEllipsis cuts on a code point boundary and ends
    // the line with "...", never mid-sequence and never without saying so.
    drawTextEllipsis(d, FontRole::MonoMeta, kDim, on ? kSelBg : kPanel, nameX, cy2,
                     textRight - nameX, line);
  } else {
    snprintf(line, sizeof(line), "no messages yet");
    drawTextEllipsis(d, FontRole::MonoMeta, kDimmer, on ? kSelBg : kPanel, nameX, cy2,
                     textRight - nameX, line);
  }

  const int lineY = row.bottom() - 1;
  if (lineY >= list.y && lineY < list.bottom()) {
    d->drawFastHLine(row.x, lineY, row.w, kRowLine);
  }
}

// The selector: a vertical list over the message area, sections "Channels" and "Direct messages",
// and the pinned "Manage channels" row at its floor.
void drawSelector(lgfx::LovyanGFX* d, const ChatView& v, const ChatLayout& g) {
  d->fillRect(g.selPanel.x, g.selPanel.y, g.selPanel.w, g.selPanel.h, kPanel);
  d->drawRect(g.selPanel.x, g.selPanel.y, g.selPanel.w, g.selPanel.h, kLine);

  d->setClipRect(g.selList.x, g.selList.y, g.selList.w, g.selList.h);
  int y = g.selList.y - v.selScroll;
  drawTextLeft(d, FontRole::MonoMeta, kDim, kPanel, g.selList.x + kSelPadX,
               y + kSelSectionH / 2, "Channels");
  y += kSelSectionH;
  for (int i = 0; i < v.channelCount && i < v.chatCount; ++i) {
    drawSelRow(d, v, v.chats[i], i, g.selList);
  }
  y += v.channelCount * kSelRowH;
  drawTextLeft(d, FontRole::MonoMeta, kDim, kPanel, g.selList.x + kSelPadX,
               y + kSelSectionH / 2, "Direct messages");
  y += kSelSectionH;
  if (v.dmCount <= 0) {
    drawTextLeft(d, FontRole::MonoMeta, kDimmer, kPanel, g.selList.x + kSelPadX,
                 y + kSelEmptyH / 2, "No direct messages yet");
  } else {
    for (int i = 0; i < v.dmCount; ++i) {
      const int entry = v.channelCount + i;
      if (entry < v.chatCount) drawSelRow(d, v, v.chats[entry], entry, g.selList);
    }
  }
  d->clearClipRect();

  const int cy = g.selManage.y + g.selManage.h / 2;
  d->drawFastHLine(g.selManage.x, g.selManage.y, g.selManage.w, kLine);
  drawTextLeft(d, FontRole::SansBody, kAcc, kPanel, g.selManage.x + kSelPadX, cy,
               "Manage channels");
  drawChevron(d, g.selManage.right() - kSelPadX - kSelChevronW, cy, kAcc, false, true);
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
  char time[8];
  ageText(time, sizeof(time), v.nowSec, m.ts);
  const int timeW = textWidth(d, FontRole::MonoMeta, time);
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
  // MeshCore counts "<our name>: " against MAX_TEXT_LEN, so our budget is that much smaller, and a
  // reply spends another "@Name " before the draft even starts.
  const int allow = composeAllowance(v.replySeq != 0, v.replyName);
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

void drawKey(lgfx::LovyanGFX* d, const Rect& r, const KeyDef& k, bool shift) {
  const bool on = (k.kind == KeyKind::Send) || (k.kind == KeyKind::Shift && shift);
  const uint16_t bg = on ? kSelBg2 : kKeyBg;
  d->fillRoundRect(r.x, r.y, r.w, r.h, kKeyRadius, bg);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kKeyRadius, on ? kAcc : kKeyLine);
  const uint16_t fg = on ? kAccInk : kInk;
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  if (k.kind == KeyKind::Del) {
    drawBackspace(d, r, fg);
    return;
  }
  char label[8];
  snprintf(label, sizeof(label), "%s", k.kind == KeyKind::Shift ? (shift ? "CAPS" : "shift")
                                                               : k.label);
  if (shift && label[0] >= 'a' && label[0] <= 'z') label[0] = (char)(label[0] - 32);
  setRole(d, FontRole::MonoMeta);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(fg, bg);
  d->drawString(label, cx, cy);
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

  for (int r = 0; r < 4; ++r) {
    KeyDef defs[10];
    const int n = keyRow(v.nums, r, defs);
    for (int c = 0; c < n && c < g.keyCount[r]; ++c) drawKey(d, g.keys[r][c], defs[c], v.shift);
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
  c.dmAvailable = v.dmAvailable;
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
  char age[8];
  ageText(age, sizeof(age), v.nowSec, m.ts);
  const int ageW = textWidth(d, FontRole::MonoMeta, age);
  drawTextEllipsis(d, FontRole::SansBody, kInk, kSheet, g.head.x, cy1,
                   g.head.w - ageW - kSheetGap, m.name);
  drawTextRight(d, FontRole::MonoMeta, kDim, kSheet, g.head.right(), cy1, age);

  char hops[24];
  char snr[28];
  if (m.hops < 0) snprintf(hops, sizeof(hops), "local echo");
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
  if (v.selector) drawSelector(d, v, g);
  if (v.osk) drawSheet(d, v, g);
  else drawCompose(d, v, g);
  // The sheet covers the bottom edge, so the tab bar is not drawn while it is open (mock .kb-open).
  if (!v.osk) drawTabBar(d, L, Tab::Chat, v.unread);
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

  // The message sheet swallows the screen: a row is its action, a tap on the header or anywhere
  // outside the panel closes it (the same rule the keyboard sheet has).
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
  // settings, a tap on the header or anywhere else closes it.
  if (v.selector) {
    if (g.selManage.contains(x, y)) {
      r.hit = ChatHit::SelManage;
      return r;
    }
    if (g.selList.contains(x, y)) {
      const int i = selRowAt(g.selList, v.channelCount, v.dmCount, v.selScroll, y);
      if (i >= 0 && i < v.chatCount) {
        r.hit = ChatHit::SelRow;
        r.index = i;
        return r;
      }
    }
    r.hit = ChatHit::SelClose;
    return r;
  }

  if (v.osk) {
    if (g.draft.contains(x, y)) {
      r.hit = ChatHit::Field;
      return r;
    }
    for (int row = 0; row < 4; ++row) {
      KeyDef defs[10];
      const int n = keyRow(v.nums, row, defs);
      for (int c = 0; c < n && c < g.keyCount[row]; ++c) {
        if (!g.keys[row][c].contains(x, y)) continue;
        r.hit = ChatHit::Key;
        r.keyRow = row;
        r.keyCol = c;
        r.keyKind = defs[c].kind;
        r.keyCh = defs[c].label[0];
        return r;
      }
    }
    // A tap on a message above the sheet still finds it: the shell closes the sheet and opens the
    // message menu, which is what the mock does when a message is tapped with the keyboard up.
    if (g.msgs.contains(x, y)) {
      int16_t heights[kMaxChatMsgs];
      const int n = chatMsgHeights(v, heights, kMaxChatMsgs);
      const int i = msgAt(g.msgs, heights, n, v.scroll, y);
      if (i >= 0) {
        r.hit = ChatHit::Msg;
        r.index = i;
        r.seq = v.msgs[i].seq;
        return r;
      }
    }
    // The tab bar is hidden while the sheet is open, so everything else is "outside" and closes it.
    r.hit = ChatHit::Outside;
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
      r.hit = ChatHit::Msg;
      r.index = i;
      r.seq = v.msgs[i].seq;
    }
  }
  return r;
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
  return selMaxScroll(g.selList, v.channelCount, v.dmCount);
}

}  // namespace ui
}  // namespace om
