// SPDX-License-Identifier: MIT
// EST-362: the RPTR screen's pixels. The list, one page, the console and the one confirmation pop-up.
// Rectangles come from ui/rptr_layout.h (the same ones the taps use), colours and fonts from
// ui/theme.h, keys from the shared sheet (ui/keyboard_draw.h).
#include "ui/screen_rptr.h"

#include <Arduino.h>
#include <string.h>

#include "../fonts.h"
#include "board.h"
#include "ui/keyboard_draw.h"

namespace om {
namespace ui {
namespace {

void chip(lgfx::LovyanGFX* d, const Rect& r, const char* text, bool on) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, on ? kSelBg2 : kPanel);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, on ? kAcc : kLine);
  setRole(d, FontRole::MonoMeta);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(on ? kAccInk : kDim, on ? kSelBg2 : kPanel);
  d->drawString(text, r.x + r.w / 2, r.y + r.h / 2);
}

void smallKey(lgfx::LovyanGFX* d, const Rect& r, const char* text, bool strong) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, strong ? kSelBg2 : kPanel2);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, strong ? kAcc : kLine);
  setRole(d, FontRole::MonoMeta);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(strong ? kAccInk : kInk, strong ? kSelBg2 : kPanel2);
  d->drawString(text, r.x + r.w / 2, r.y + r.h / 2);
}

void drawBack(lgfx::LovyanGFX* d, const Rect& r, const char* label) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, kPanel2);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, kLine);
  drawTextLeft(d, FontRole::MonoMeta, kInk, kPanel2, r.x + 8, r.y + r.h / 2, label);
}

// A small gold key: the remembered password. A ring and a stem, no font glyph involved.
void drawKeyIcon(lgfx::LovyanGFX* d, int cx, int cy, uint16_t ink, uint16_t bg) {
  d->drawCircle(cx, cy, 3, ink);
  d->drawFastHLine(cx + 3, cy, 5, ink);
  d->drawFastVLine(cx + 6, cy, 3, ink);
  d->drawFastVLine(cx + 8, cy, 2, ink);
  (void)bg;
}

void checkbox(lgfx::LovyanGFX* d, const Rect& r, bool on, bool danger) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, on ? kSelBg2 : kPanel);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, on ? kAcc : kLine);
  if (on) {
    // A tick, drawn as two strokes: no glyph the small faces might lack.
    d->drawLine(r.x + 4, r.y + 8, r.x + 7, r.y + 11, danger ? kBad : kGreen);
    d->drawLine(r.x + 7, r.y + 11, r.x + 12, r.y + 5, danger ? kBad : kGreen);
  }
}

void drawListRow(lgfx::LovyanGFX* d, const Rect& r, const RptrItem& it) {
  d->fillCircle(r.x + kRowPadX + 3, r.y + 12, 3, kGreen);
  int right = r.right() - kRowPadX;
  if (it.saved) {
    drawKeyIcon(d, right - 8, r.y + 11, kAcc, kScreen);
    right -= 20;
  }
  drawTextEllipsis(d, FontRole::SansBody, kInk, kScreen, r.x + kRowPadX + 12, r.y + 11,
                   right - (r.x + kRowPadX + 12), it.name);
  if (it.meta[0]) {
    drawTextEllipsis(d, FontRole::MonoMeta, kDim, kScreen, r.x + kRowPadX + 12, r.y + 25,
                     r.w - 2 * kRowPadX - 12, it.meta);
  }
  d->drawFastHLine(r.x + kRowPadX, r.bottom() - 1, r.w - 2 * kRowPadX, kRowLine);
}

void drawPageRow(lgfx::LovyanGFX* d, const RptrPageLayout& a, const rptr::UiRow& row, int i,
                 int scroll) {
  const RptrRowRects r = rptrPageRowRects(a, row, i, scroll);
  if (!r.body.overlaps(a.body)) return;
  const uint16_t bg = row.danger ? kDangerBg : kScreen;
  if (row.danger) d->fillRect(r.body.x, r.body.y, r.body.w, r.body.h, kDangerBg);
  const bool checkboxRow = row.act == rptr::Act::RegionToggle || row.act == rptr::Act::Toggle;
  if (checkboxRow) checkbox(d, r.box, row.on, row.danger);
  drawTextEllipsis(d, FontRole::SansBody, row.danger ? kDangerInk : kInk, bg, r.label.x,
                   r.label.y + 7, r.label.w, row.label);
  if (row.value[0]) {
    uint16_t ink = kDim;
    if (row.act == rptr::Act::Toggle || row.act == rptr::Act::RegionToggle) {
      ink = row.on ? kGreen : kBad;
    } else if (row.act == rptr::Act::Open || row.act == rptr::Act::Step ||
               row.act == rptr::Act::Pick) {
      ink = kInk;
    }
    drawTextEllipsis(d, FontRole::MonoMeta, ink, bg, r.value.x, r.value.y + 6, r.value.w,
                     row.value);
  }
  if (row.stepKeys) {
    smallKey(d, r.keyA, "-", false);
    smallKey(d, r.keyB, "+", false);
  } else if (row.key[0]) {
    smallKey(d, r.keyA, row.key, false);
  } else if (row.arrow) {
    smallKey(d, r.keyA, ">", false);
  }
  d->drawFastHLine(r.body.x, r.body.bottom() - 1, r.body.w, kRowLine);
}

void drawCard(lgfx::LovyanGFX* d, const RptrPageLayout& a, const rptr::ScreenOut& s) {
  if (a.card.empty()) return;
  d->fillRoundRect(a.card.x, a.card.y, a.card.w, a.card.h, kRadius, kPanel);
  d->drawRoundRect(a.card.x, a.card.y, a.card.w, a.card.h, kRadius, kLine);
  const int16_t colW = (int16_t)((a.card.w - 16) / 2);
  for (int i = 0; i < s.cellCount && i < 6; ++i) {
    const int col = i % 2;
    const int row = i / 2;
    const int16_t x = (int16_t)(a.card.x + 8 + col * colW);
    const int16_t y = (int16_t)(a.card.y + 3 + row * kRptrCardRowH);
    drawTextEllipsis(d, FontRole::MonoMeta, kDimmer, kPanel, x, y, colW - 4, s.cells[i].label);
    // The value on the same line, right-aligned to the cell.
    drawTextRight(d, FontRole::MonoMeta, kInk, kPanel, (int16_t)(x + colW - 6), y,
                  s.cells[i].value);
  }
  // The refresh icon: a small open ring with an arrowhead, in the card's top right corner.
  if (!a.refresh.empty()) {
    const int cx = a.refresh.x + a.refresh.w / 2;
    const int cy = a.refresh.y + a.refresh.h / 2;
    d->drawArc(cx, cy, 7, 8, 40, 320, kDim);
    d->fillTriangle((int16_t)(cx + 6), (int16_t)(cy - 6), (int16_t)(cx + 10), (int16_t)(cy - 2),
                    (int16_t)(cx + 3), (int16_t)(cy - 2), kDim);
  }
}

void drawBanner(lgfx::LovyanGFX* d, const RptrPageLayout& a, const rptr::ScreenOut& s) {
  if (a.banner.empty() || s.banner[0] == 0) return;
  d->fillRoundRect(a.banner.x, a.banner.y, a.banner.w, a.banner.h, kRadius, kWarnBg);
  d->drawRoundRect(a.banner.x, a.banner.y, a.banner.w, a.banner.h, kRadius, kWarnLine);
  const int16_t textW = s.bannerButton ? (int16_t)(a.bannerButton.x - a.banner.x - 8)
                                       : (int16_t)(a.banner.w - 12);
  drawTextEllipsis(d, FontRole::SansBody, kWarnInk, kWarnBg, a.banner.x + 6, a.banner.y + 9,
                   textW, s.banner);
  if (s.bannerButton) {
    d->fillRoundRect(a.bannerButton.x, a.bannerButton.y, a.bannerButton.w, a.bannerButton.h,
                     kRadiusSm, kSelBg2);
    d->drawRoundRect(a.bannerButton.x, a.bannerButton.y, a.bannerButton.w, a.bannerButton.h,
                     kRadiusSm, kAcc);
    setRole(d, FontRole::MonoMeta);
    d->setTextDatum(lgfx::textdatum_t::middle_center);
    d->setTextColor(kAccInk, kSelBg2);
    d->drawString("Set clock", a.bannerButton.x + a.bannerButton.w / 2,
                  a.bannerButton.y + a.bannerButton.h / 2);
  }
}

void drawWrapped(lgfx::LovyanGFX* d, FontRole role, uint16_t ink, uint16_t bg, const Rect& box,
                 const char* text) {
  setRole(d, role);
  int y = box.y + 2;
  const int lineH = 15;
  char line[48];
  const char* p = text;
  while (*p && y + lineH <= box.bottom()) {
    // Take as many words as fit the box, then draw that line.
    size_t n = 0;
    size_t lastSpace = 0;
    line[0] = 0;
    while (p[n] && n + 1 < sizeof(line)) {
      line[n] = p[n];
      line[n + 1] = 0;
      if (p[n] == ' ') lastSpace = n;
      if (measureText(d, role, line, (int)n + 1) > box.w) break;
      ++n;
    }
    if (p[n] && lastSpace > 0) n = lastSpace;
    line[n < sizeof(line) ? n : sizeof(line) - 1] = 0;
    drawTextLeft(d, role, ink, bg, box.x, y, line);
    p += n;
    while (*p == ' ') ++p;
    y += lineH;
  }
}

void drawButton(lgfx::LovyanGFX* d, const Rect& r, const char* label, bool danger) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, danger ? kDangerBg : kSelBg2);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, danger ? kBad : kAcc);
  setRole(d, FontRole::SansBody);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(danger ? kDangerInk : kAccInk, danger ? kDangerBg : kSelBg2);
  d->drawString(label, r.x + r.w / 2, r.y + r.h / 2);
}

// The sheet over the tab bar: the same shared keyboard the Settings editors and the composer use.
void drawSheet(lgfx::LovyanGFX* d, const RptrView& v) {
  const KeyboardField field = rptrEditorField(v);
  const ChatLayout g = chatLayoutFor(l2(), true, false, v.nums, false, field);
  d->fillRoundRect(g.sheet.x, g.sheet.y, g.sheet.w, g.sheet.h + kOskRadius, kOskRadius, kSheet);
  d->drawFastHLine(g.sheet.x, g.sheet.y, g.sheet.w, kLine);

  if (v.editTitle[0]) {
    drawTextEllipsis(d, FontRole::MonoMeta, kDimmer, kSheet, 12, g.sheet.y + 8, g.sheet.w - 24,
                     v.editTitle);
  }

  d->fillRoundRect(g.draft.x, g.draft.y, g.draft.w, g.draft.h, kKeyRadius, kCodeBg);
  d->drawRoundRect(g.draft.x, g.draft.y, g.draft.w, g.draft.h, kKeyRadius, kAcc);
  const int cy = g.draft.y + g.draft.h / 2;
  if (v.secret) {
    const int cap = (g.draft.w - 2 * kFieldPadX - 40) / 6;
    int n = v.textLen;
    if (n > cap) n = cap;
    if (n == 0) {
      drawTextLeft(d, FontRole::MonoMeta, kDimmer, kCodeBg, g.draft.x + kFieldPadX, cy,
                   "(password)");
    } else {
      for (int i = 0; i < n; ++i) {
        d->fillCircle((int16_t)(g.draft.x + kFieldPadX + 3 + i * 6), (int16_t)cy, 2, kInk);
      }
    }
  } else if (v.text[0] == 0) {
    drawTextLeft(d, FontRole::MonoMeta, kDimmer, kCodeBg, g.draft.x + kFieldPadX, cy,
                 v.editTitle[0] ? v.editTitle : "(type below)");
  } else {
    drawTextEllipsis(d, FontRole::MonoMeta, kInk, kCodeBg, g.draft.x + kFieldPadX, cy,
                     g.draft.w - 2 * kFieldPadX - 34, v.text);
  }
  char used[16];
  snprintf(used, sizeof(used), "%d/%d", v.textLen, keyboardMaxBytes(field));
  drawTextRight(d, FontRole::MonoMeta, kDimmer, kCodeBg, g.draft.right() - kFieldPadX, cy, used);

  for (int r = 0; r < 4; ++r) {
    KeyDef defs[10];
    const int n = keyRow(v.nums, r, defs, field);
    for (int c = 0; c < n && c < g.keyCount[r]; ++c) {
      if (defs[c].kind == KeyKind::Send) {
        snprintf(defs[c].label, sizeof(defs[c].label), "%s", v.okLabel);
      }
      drawKeyCap(d, g.keys[r][c], defs[c], v.shift);
    }
  }
  if (v.accentCp != 0 && v.accentRow < 4 && v.accentCol < 10) {
    drawAccentBubble(d, g.keys[v.accentRow][v.accentCol], v.accentCp);
  }
}

void drawListStage(lgfx::LovyanGFX* d, const RptrView& v) {
  const RptrListLayout r = rptrListLayoutFor(l2());
  d->fillRect(r.head.x, r.head.y, r.head.w, r.head.h, kPanel);
  d->drawFastHLine(r.head.x, r.head.bottom() - 1, r.head.w, kLine);
  drawTextLeft(d, FontRole::MonoMeta, kDimmer, kPanel, 8, r.head.y + r.head.h / 2, "REPEATERS");
  drawTextLeft(d, FontRole::MonoMeta, kAcc, kPanel, 76, r.head.y + r.head.h / 2, v.state);
  chip(d, r.scan, v.scanning ? "Scanning" : "Scan", v.scanning);

  if (v.count <= 0) {
    const char* title = v.scanning ? "Listening..." : "No repeaters yet.";
    const char* sub = v.scanning ? "Scan sent. Waiting for repeater replies."
                                 : "Scan sends a request,\nevery repeater in range answers.";
    drawEmptyState(d, r.empty, title, sub);
    return;
  }
  d->setClipRect(r.list.x, r.list.y, r.list.w, r.list.h);
  for (int i = 0; i < v.count; ++i) {
    const RptrRow row = rptrListRow(r, i, v.scroll);
    const Rect rr{r.list.x, row.y, r.list.w, row.h};
    if (!rr.overlaps(r.list)) continue;
    drawListRow(d, rr, v.items[i]);
  }
  d->clearClipRect();
}

void drawPageStage(lgfx::LovyanGFX* d, const RptrView& v) {
  const rptr::ScreenOut& s = v.screen;
  const bool console = s.hasConsole;
  const bool card = s.cellCount > 0;
  const bool banner = s.banner[0] != 0;
  const RptrPageLayout a =
      rptrPageLayoutFor(l2(), card, banner, s.hasApply, s.bigButton, console);
  drawBack(d, a.back, v.page == rptr::Page::Repeater ? "< list" : "< back");
  drawTextEllipsis(d, FontRole::SansTitle, kInk, kScreen, a.title.x, a.title.y + 11, a.title.w,
                   s.title);
  // The result of the last Apply is the page's second line: a check and "Applied", or the
  // repeater's error in plain words. Nothing else competes for it.
  if (v.result[0]) {
    const uint16_t ink = v.resultPending ? kDim : (v.resultOk ? kGreen : kBad);
    int16_t x = a.sub.x;
    if (!v.resultPending && v.resultOk) {
      // The tick is drawn, not typed: the faces carry ASCII only.
      d->drawLine((int16_t)(x + 2), (int16_t)(a.sub.y + 7), (int16_t)(x + 5), (int16_t)(a.sub.y + 10),
                  kGreen);
      d->drawLine((int16_t)(x + 5), (int16_t)(a.sub.y + 10), (int16_t)(x + 10), (int16_t)(a.sub.y + 3),
                  kGreen);
      x = (int16_t)(x + 14);
    } else if (!v.resultPending) {
      drawTextLeft(d, FontRole::MonoMeta, kBad, kScreen, x, a.sub.y + 6, "!");
      x = (int16_t)(x + 8);
    }
    drawTextEllipsis(d, FontRole::MonoMeta, ink, kScreen, x, a.sub.y + 1,
                     (int16_t)(a.sub.w - (x - a.sub.x)), v.result);
  } else {
    drawTextEllipsis(d, FontRole::MonoMeta, kDim, kScreen, a.sub.x, a.sub.y + 1, a.sub.w,
                     s.subtitle);
  }

  drawCard(d, a, s);
  drawBanner(d, a, s);
  if (console) {
    // The raw log: answers live ONLY here, in mono, with nothing to tap.
    d->setClipRect(a.body.x, a.body.y, a.body.w, a.body.h);
    setRole(d, FontRole::MonoMeta);
    int y = a.body.y + 2;
    for (int i = 0; i < s.logLineCount && y + 12 <= a.body.bottom(); ++i) {
      drawTextEllipsis(d, FontRole::MonoMeta, i == s.logLineCount - 1 ? kInk : kDim, kScreen,
                       a.body.x + 6, y, a.body.w - 12, s.logLines[i]);
      y += 13;
    }
    d->clearClipRect();
    d->fillRoundRect(a.input.x, a.input.y, a.input.w, a.input.h, kRadiusSm, kFieldBg);
    d->drawRoundRect(a.input.x, a.input.y, a.input.w, a.input.h, kRadiusSm, kLine);
    const char* typed = s.consoleInput[0] ? s.consoleInput : "command";
    drawTextEllipsis(d, FontRole::MonoMeta, s.consoleInput[0] ? kInk : kDimmer, kFieldBg,
                     a.input.x + 6, a.input.y + a.input.h / 2, a.input.w - 12, typed);
    drawButton(d, a.send, "Send", false);
  } else {
    d->setClipRect(a.body.x, a.body.y, a.body.w, a.body.h);
    for (int i = 0; i < s.count; ++i) drawPageRow(d, a, s.rows[i], i, v.pageScroll);
    d->clearClipRect();
  }

  if (!a.apply.empty()) drawButton(d, a.apply, "Apply", s.applyDanger);
  if (!a.big.empty()) drawButton(d, a.big, s.bigLabel, false);
}

void drawModal(lgfx::LovyanGFX* d, const RptrView& v) {
  // A scrim: the page behind stays readable, but dimmed.
  d->fillRect(0, 0, l2().width, l2().height, kPanel2);
  const RptrModalLayout m = rptrModalLayoutFor(l2(), v.modalVerb);
  d->fillRoundRect(m.box.x, m.box.y, m.box.w, m.box.h, kRadius, kPanel);
  d->drawRoundRect(m.box.x, m.box.y, m.box.w, m.box.h, kRadius, kAcc);
  drawWrapped(d, FontRole::SansBody, kInk, kPanel, m.text, v.modalText);
  drawButton(d, m.cancel, "Cancel", false);
  drawButton(d, m.confirm, v.modalVerb, true);
}

}  // namespace

void rptrScreenDraw(const RptrView& v) {
  if (!omFrameBegin()) {
    omFrameEnd();
    return;
  }
  lgfx::LovyanGFX* d = om_display();
  if (!d) {
    omFrameEnd();
    return;
  }
  d->fillScreen(kScreen);
  if (v.list) {
    drawListStage(d, v);
  } else {
    drawPageStage(d, v);
  }
  if (v.modal) {
    drawModal(d, v);
  } else if (v.osk) {
    // The sheet covers the tab bar, exactly like the chat composer's (Filip, S459).
    drawSheet(d, v);
  } else {
    drawTabBar(d, l2(), Tab::Rptr, 0, false);
  }
  drawStatusBar(d, l2(), v.status);
  om::useAppFont(d);
  omFrameEnd();
}

bool rptrSheetOpen(const RptrView& v) { return v.osk; }

KeyboardField rptrEditorField(const RptrView& v) {
  return v.osk ? (KeyboardField)v.field : KeyboardField::RptrPassword;
}

RptrKeyHit rptrEditorKeyAt(const RptrView& v, int x, int y) {
  RptrKeyHit out;
  if (!v.osk) return out;
  const KeyboardField field = rptrEditorField(v);
  const ChatLayout g = chatLayoutFor(l2(), true, false, v.nums, false, field);
  const KeyHit key = keyAt(g, v.nums, x, y, field);
  if (key.row < 0) return out;
  KeyDef defs[10];
  const int n = keyRow(v.nums, key.row, defs, field);
  if (key.col < 0 || key.col >= n) return out;
  out.pressed = true;
  out.kind = defs[key.col].kind;
  out.row = key.row;
  out.col = key.col;
  out.ch = defs[key.col].label[0];
  if (v.shift && !v.nums && out.ch >= 'a' && out.ch <= 'z') out.ch = (char)(out.ch - 32);
  return out;
}

}  // namespace ui
}  // namespace om
