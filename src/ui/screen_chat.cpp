// SPDX-License-Identifier: MIT
// The Chat tab. Every rectangle comes from chat_layout.h (or from a widget), every colour and size
// from theme.h, and every pixel goes into the off-screen frame that chatScreenDraw opens.
#include "ui/screen_chat.h"

#include <stdio.h>
#include <string.h>

#include "../fonts.h"   // om::useAppFont: leave the 12 px default for the old screens
#include "board.h"
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

void chipBadge(char* out, size_t cap, int n) {
  if (n > 9) snprintf(out, cap, "9+");
  else snprintf(out, cap, "%d", n);
}

void textCentre(lgfx::LovyanGFX* d, FontRole role, uint16_t fg, uint16_t bg, int cx, int cy,
                const char* s) {
  setRole(d, role);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(fg, bg);
  d->drawString(s, cx, cy);
}

// The last `maxW` pixels of s: a long draft keeps its tail visible instead of running off the line.
const char* tailFits(lgfx::LovyanGFX* d, FontRole role, int maxW, const char* s) {
  if (maxW <= 0) return s + strlen(s);
  const char* p = s;
  while (*p != 0 && measureText(d, role, p, (int)strlen(p)) > maxW) ++p;
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

// A small keyboard, drawn from primitives (used on the compose line's button).
void drawKbIcon(lgfx::LovyanGFX* d, const Rect& r, uint16_t col) {
  const int x = r.x + r.w / 2 - 8;
  const int y = r.y + r.h / 2 - 5;
  d->drawRect(x, y, 16, 10, col);
  for (int row = 0; row < 2; ++row) {
    for (int c = 0; c < 3; ++c) d->fillRect(x + 2 + c * 4, y + 2 + row * 3, 2, 2, col);
  }
  d->fillRect(x + 3, y + 7, 10, 1, col);
}

int chipWidths(const ChatView& v, int16_t* out, int max) {
  lgfx::LovyanGFX* d = om_display();
  int n = 0;
  for (int i = 0; i < v.chipCount && i < max; ++i) {
    const int labelW = d ? textWidth(d, FontRole::MonoMeta, v.chips[i].label) : 0;
    int countW = 0;
    if (v.chips[i].unread > 0) {
      char b[4];
      chipBadge(b, sizeof(b), v.chips[i].unread);
      countW = d ? textWidth(d, FontRole::MonoMeta, b) : 0;
    }
    out[n++] = (int16_t)chipWidth(labelW, countW);
  }
  return n;
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
  const ChatLayout g = chatLayoutFor(l2(), v.osk, v.replySeq != 0, v.nums);
  int n = 0;
  if (d == nullptr) return 0;
  for (int i = 0; i < v.msgCount && i < max; ++i) {
    WrapLine lines[kMaxWrapLines];
    const int nl = wrapMessage(d, v, v.msgs[i], g.msgs, lines, kMaxWrapLines, nullptr);
    out[n++] = (int16_t)(nl * kMsgLineH);
  }
  return n;
}

void drawChips(lgfx::LovyanGFX* d, const ChatView& v, const ChatLayout& g) {
  d->fillRect(g.chips.x, g.chips.y, g.chips.w, g.chips.h, kScreen);
  d->drawFastHLine(g.chips.x, g.chips.bottom() - 1, g.chips.w, kLine);

  // The list button: three short lines, the same rect the tap test uses.
  {
    const int cx = g.chipMenu.x + g.chipMenu.w / 2;
    const int cy = g.chipMenu.y + g.chipMenu.h / 2;
    for (int i = -1; i <= 1; ++i) d->drawFastHLine(cx - 6, cy + i * 4, 12, kDim);
    d->drawFastVLine(g.chipMenu.x, g.chipMenu.y + 3, g.chipMenu.h - 6, kLine);
  }

  int16_t widths[kMaxChips];
  const int n = chipWidths(v, widths, kMaxChips);
  const int16_t chipY = (int16_t)(g.chips.y + (g.chips.h - 1 - kChipH) / 2);
  d->setClipRect(g.chipScroll.x, g.chipScroll.y, g.chipScroll.w, g.chipScroll.h);
  for (int i = 0; i < n; ++i) {
    const int x = g.chipScroll.x + chipLeft(widths, i, v.chipScroll);
    if (x + widths[i] <= g.chipScroll.x || x >= g.chipScroll.right()) continue;
    const bool on = (i == v.selChip);
    if (on) {
      d->fillRoundRect(x, chipY, widths[i], kChipH, kChipRadius, kSelBg);
      d->drawRoundRect(x, chipY, widths[i], kChipH, kChipRadius, kAcc);
    }
    const int cy = chipY + kChipH / 2;
    const uint16_t col = on ? kAccInk : (v.chips[i].muted ? kDimmer : kDim);
    drawTextLeft(d, FontRole::MonoMeta, col, on ? kSelBg : kScreen, x + kChipPadX + 1, cy,
                 v.chips[i].label);
    if (v.chips[i].unread > 0) {
      char b[4];
      chipBadge(b, sizeof(b), v.chips[i].unread);
      drawTextRight(d, FontRole::MonoMeta, kBad, on ? kSelBg : kScreen,
                    x + widths[i] - kChipPadX - 1, cy, b);
    }
  }
  d->clearClipRect();
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

  char buf[128];   // one wrapped line of a 96-byte MeshCore message
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
    if (v.chipCount <= 0) {
      drawEmptyState(d, g.msgs, "No channels", "Add one from the channel list.");
    } else {
      drawEmptyState(d, g.msgs, "No messages yet", "Say something.");
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

// The draft text plus its caret, shared by the compose field and the sheet's draft line.
void drawDraft(lgfx::LovyanGFX* d, const Rect& field, uint16_t bg, const char* draft, bool focus) {
  const int x = field.x + kFieldPadX;
  const int cy = field.y + field.h / 2;
  const int room = field.w - 2 * kFieldPadX - kCaretW - kHairline;
  const char* tail = tailFits(d, FontRole::MonoMeta, room, draft ? draft : "");
  if (*tail == 0) {
    drawTextLeft(d, FontRole::MonoMeta, kDimmer, bg, x, cy, "message");
    if (focus) d->fillRect(x, cy - kCaretH / 2, kCaretW, kCaretH, kAcc);
    return;
  }
  const int textW = textWidth(d, FontRole::MonoMeta, tail);
  drawTextLeft(d, FontRole::MonoMeta, kInk, bg, x, cy, tail);
  if (focus) d->fillRect(x + textW, cy - kCaretH / 2, kCaretW, kCaretH, kAcc);
}

void drawCompose(lgfx::LovyanGFX* d, const ChatView& v, const ChatLayout& g) {
  d->fillRect(g.compose.x, g.compose.y, g.compose.w, g.compose.h, kScreen);
  d->drawFastHLine(g.compose.x, g.compose.y, g.compose.w, kLine);

  d->fillRoundRect(g.field.x, g.field.y, g.field.w, g.field.h, kRadius, kFieldBg);
  d->drawRoundRect(g.field.x, g.field.y, g.field.w, g.field.h, kRadius, kLine);
  drawDraft(d, g.field, kFieldBg, v.draft ? v.draft : "", false);

  d->fillRoundRect(g.kbdBtn.x, g.kbdBtn.y, g.kbdBtn.w, g.kbdBtn.h, kRadius, kPanel2);
  d->drawRoundRect(g.kbdBtn.x, g.kbdBtn.y, g.kbdBtn.w, g.kbdBtn.h, kRadius, kLine);
  drawKbIcon(d, g.kbdBtn, kDim);

  d->fillRoundRect(g.sendBtn.x, g.sendBtn.y, g.sendBtn.w, g.sendBtn.h, kRadius, kPanel2);
  d->drawRoundRect(g.sendBtn.x, g.sendBtn.y, g.sendBtn.w, g.sendBtn.h, kRadius, kAcc);
  textCentre(d, FontRole::SansTab, kAccInk, kPanel2, g.sendBtn.x + g.sendBtn.w / 2,
             g.sendBtn.y + g.sendBtn.h / 2, "Send");
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
  textCentre(d, FontRole::MonoMeta, fg, bg, cx, cy, label);
}

void drawSheet(lgfx::LovyanGFX* d, const ChatView& v, const ChatLayout& g) {
  // Rounded at the top only: the body is drawn past the bottom edge so the lower corners fall off
  // the screen, exactly like the mock's .osk sitting on the frame's floor.
  d->fillRoundRect(g.sheet.x, g.sheet.y, g.sheet.w, g.sheet.h + kOskRadius, kOskRadius, kSheet);
  d->drawFastHLine(g.sheet.x, g.sheet.y, g.sheet.w, kLine);

  if (g.replyBar.w > 0 && v.replySeq != 0) {
    d->fillRoundRect(g.replyBar.x, g.replyBar.y, g.replyBar.w, g.replyBar.h, kRadiusSm, kReplyBg);
    d->drawRoundRect(g.replyBar.x, g.replyBar.y, g.replyBar.w, g.replyBar.h, kRadiusSm, kReplyLine);
    char bar[48];
    // The name was copied when REPLY was tapped, so this bar still names the right sender even after
    // the ring has shifted and v.msgs no longer contains that message.
    snprintf(bar, sizeof(bar), "RE %s", v.replyName ? v.replyName : "");
    drawTextEllipsis(d, FontRole::MonoMeta, kReplyInk, kReplyBg, g.replyBar.x + kFieldPadX,
                     g.replyBar.y + g.replyBar.h / 2, g.replyBar.w - 2 * kFieldPadX, bar);
  }

  d->fillRoundRect(g.draft.x, g.draft.y, g.draft.w, g.draft.h, kKeyRadius, kCodeBg);
  d->drawRoundRect(g.draft.x, g.draft.y, g.draft.w, g.draft.h, kKeyRadius, kAcc);
  drawDraft(d, g.draft, kCodeBg, v.draft ? v.draft : "", true);

  for (int r = 0; r < 4; ++r) {
    KeyDef defs[10];
    const int n = keyRow(v.nums, r, defs);
    for (int c = 0; c < n && c < g.keyCount[r]; ++c) drawKey(d, g.keys[r][c], defs[c], v.shift);
  }
}

void drawMenu(lgfx::LovyanGFX* d, const ChatView& v, const ChatLayout& g) {
  const int i = v.menuMsg;
  if (i < 0 || i >= v.msgCount) return;
  d->fillRoundRect(g.menuPanel.x, g.menuPanel.y, g.menuPanel.w, g.menuPanel.h, kRadiusLg, kPanel);
  d->drawRoundRect(g.menuPanel.x, g.menuPanel.y, g.menuPanel.w, g.menuPanel.h, kRadiusLg, kLine);
  drawTextEllipsis(d, FontRole::MonoMeta, kDim, kPanel, g.menuHead.x + kPanelPad,
                   g.menuHead.y + g.menuHead.h / 2, g.menuHead.w - 2 * kPanelPad, v.msgs[i].name);
  static const char* kLabels[3] = {"REPLY", "INFO", "COPY"};
  for (int r = 0; r < 3; ++r) {
    const Rect& row = g.menuRow[r];
    d->fillRoundRect(row.x, row.y, row.w, row.h, kRadiusSm, kPanel2);
    d->drawRoundRect(row.x, row.y, row.w, row.h, kRadiusSm, kLine);
    textCentre(d, FontRole::SansTab, kAcc, kPanel2, row.x + row.w / 2, row.y + row.h / 2,
               kLabels[r]);
  }
}

void drawInfo(lgfx::LovyanGFX* d, const ChatView& v, const ChatLayout& g) {
  const int i = v.infoMsg;
  if (i < 0 || i >= v.msgCount) return;
  const ChatMsgView& m = v.msgs[i];
  d->fillRoundRect(g.infoPanel.x, g.infoPanel.y, g.infoPanel.w, g.infoPanel.h, kRadiusLg, kPanel);
  d->drawRoundRect(g.infoPanel.x, g.infoPanel.y, g.infoPanel.w, g.infoPanel.h, kRadiusLg, kLine);

  const int labelX = g.infoPanel.x + kPanelPad;
  const int valueX = g.infoPanel.x + kInfoColX;
  int y = g.infoPanel.y + kPanelPad + kMenuHeadH / 2;
  drawTextLeft(d, FontRole::MonoMeta, kDimmer, kPanel, labelX, y, "FROM");
  drawTextEllipsis(d, FontRole::MonoMeta, kAcc, kPanel, valueX, y,
                   g.infoPanel.right() - kPanelPad - valueX, m.name);

  char line[64];
  char age[8];
  ageText(age, sizeof(age), v.nowSec, m.ts);
  const struct {
    const char* label;
    const char* value;
  } rows[4] = {
      {"CHANNEL", m.chanName ? m.chanName : ""},
      {"AGE", line},
      {"HOPS", ""},
      {"SNR", ""},
  };
  snprintf(line, sizeof(line), "%s ago", age);
  const int hops = m.hops;
  const int snrq = m.snrq;
  char hopsText[24];
  char snrText[28];
  if (hops < 0) snprintf(hopsText, sizeof(hopsText), "local echo");
  else snprintf(hopsText, sizeof(hopsText), "%d", hops);
  if (hops < 0 || snrq == 0) snprintf(snrText, sizeof(snrText), "--");
  else snprintf(snrText, sizeof(snrText), "%.2f dB (last hop)", (double)snrq / 4.0);
  const char* values[4] = {rows[0].value, rows[1].value, hopsText, snrText};

  for (int r = 0; r < 4; ++r) {
    y += kActionRowH;
    drawTextLeft(d, FontRole::MonoMeta, kDimmer, kPanel, labelX, y, rows[r].label);
    drawTextEllipsis(d, FontRole::MonoMeta, kInk, kPanel, valueX, y,
                     g.infoPanel.right() - kPanelPad - valueX, values[r]);
  }
  drawTextLeft(d, FontRole::MonoMeta, kDimmer, kPanel, labelX, y + kActionRowH,
               "tap anywhere to close");
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
  const ChatLayout g = chatLayoutFor(L, v.osk, v.replySeq != 0, v.nums);

  d->fillScreen(kScreen);
  drawStatusBar(d, L, v.status);
  drawChips(d, v, g);
  drawMessages(d, v, g);
  if (v.osk) drawSheet(d, v, g);
  else drawCompose(d, v, g);
  // The sheet covers the bottom edge, so the tab bar is not drawn while it is open (mock .kb-open).
  if (!v.osk) drawTabBar(d, L, Tab::Chat, v.unread);
  if (v.menuMsg >= 0) drawMenu(d, v, g);
  if (v.infoMsg >= 0) drawInfo(d, v, g);

  // The channel list and its pages are still the old screens and share this frame; leave them the
  // 12 px default font the old drawing code expects.
  om::useAppFont(d);
  omFrameEnd();
}

ChatHitResult chatScreenHit(const ChatView& v, int x, int y) {
  ChatHitResult r;
  const ChatLayout g = chatLayoutFor(l2(), v.osk, v.replySeq != 0, v.nums);

  // Overlays first: a tap anywhere closes INFO (old behaviour), and a tap outside the message menu
  // closes the menu.
  if (v.infoMsg >= 0) {
    r.hit = ChatHit::InfoClose;
    return r;
  }
  if (v.menuMsg >= 0) {
    for (int i = 0; i < 3; ++i) {
      if (g.menuRow[i].contains(x, y)) {
        r.hit = (i == 0) ? ChatHit::MenuReply : (i == 1 ? ChatHit::MenuInfo : ChatHit::MenuCopy);
        r.index = v.menuMsg;
        r.seq = v.menuSeq;
        return r;
      }
    }
    r.hit = ChatHit::MenuClose;
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

  if (g.chipMenu.contains(x, y)) {
    r.hit = ChatHit::ChipMenu;
    return r;
  }
  if (g.chipScroll.contains(x, y)) {
    int16_t widths[kMaxChips];
    const int n = chipWidths(v, widths, kMaxChips);
    const int i = chipAt(widths, n, v.chipScroll, x - g.chipScroll.x);
    if (i >= 0) {
      r.hit = ChatHit::Chip;
      r.index = i;
    }
    return r;
  }
  if (g.field.contains(x, y)) {
    r.hit = ChatHit::Field;
    return r;
  }
  if (g.kbdBtn.contains(x, y)) {
    r.hit = ChatHit::KbdBtn;
    return r;
  }
  if (g.sendBtn.contains(x, y)) {
    r.hit = ChatHit::Send;
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
  const ChatLayout g = chatLayoutFor(l2(), v.osk, v.replySeq != 0, v.nums);
  int16_t heights[kMaxChatMsgs];
  const int n = chatMsgHeights(v, heights, kMaxChatMsgs);
  return msgMaxScroll(g.msgs, heights, n);
}

int chatChipScrollInto(const ChatView& v, int chip) {
  const ChatLayout g = chatLayoutFor(l2(), v.osk, v.replySeq != 0, v.nums);
  int16_t widths[kMaxChips];
  const int n = chipWidths(v, widths, kMaxChips);
  return chipScrollTo(v.chipScroll, widths, n, chip, g.chipScroll.w);
}

}  // namespace ui
}  // namespace om
