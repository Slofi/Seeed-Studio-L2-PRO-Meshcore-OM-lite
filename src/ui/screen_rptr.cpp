// SPDX-License-Identifier: MIT
// RPTR screen drawing (EST-358): the scan list, the login page and the six admin views, in the
// mock's layer. Rectangles come from ui/rptr_layout.h (the same ones the taps use), colours and
// fonts from ui/theme.h, keys from the shared sheet (ui/keyboard_draw.h).
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

// A row's small key: - / + / H / D. A rounded box with one mono glyph in the middle.
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

void drawListRow(lgfx::LovyanGFX* d, const Rect& r, const RptrItem& it) {
  d->fillCircle(r.x + kRowPadX + 3, r.y + 12, 3, kGreen);
  drawTextEllipsis(d, FontRole::SansBody, kInk, kScreen, r.x + kRowPadX + 12, r.y + 11,
                   r.w - 24 - (it.inContacts ? 40 : 0), it.name);
  if (it.inContacts) {
    // The ticket's mark: this repeater already has a contact here.
    drawTextRight(d, FontRole::MonoMeta, kAcc, kScreen, r.right() - kRowPadX, r.y + 11, "saved");
  }
  if (it.meta[0]) {
    drawTextEllipsis(d, FontRole::MonoMeta, kDim, kScreen, r.x + kRowPadX + 12, r.y + 24,
                     r.w - 2 * kRowPadX - 12, it.meta);
  }
  d->drawFastHLine(r.x + kRowPadX, r.bottom() - 1, r.w - 2 * kRowPadX, kRowLine);
}

void drawInfoCard(lgfx::LovyanGFX* d, const Rect& r, const RptrView& v) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadius, kPanel);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadius, kLine);
  int y = r.y + 11;
  for (int i = 0; i < v.infoCount; ++i) {
    char label[20] = "";
    char value[52] = "";
    const char* bar = strchr(v.info[i], '|');
    if (bar) {
      const int n = (int)(bar - v.info[i]);
      snprintf(label, sizeof(label), "%.*s", n < (int)sizeof(label) - 1 ? n : (int)sizeof(label) - 1,
               v.info[i]);
      snprintf(value, sizeof(value), "%s", bar + 1);
    } else {
      snprintf(value, sizeof(value), "%s", v.info[i]);
    }
    drawTextEllipsis(d, FontRole::MonoMeta, kDimmer, kPanel, r.x + 7, y, 76, label);
    drawTextEllipsis(d, FontRole::MonoMeta, kInk, kPanel, r.x + 88, y, r.w - 95, value);
    y += 17;
  }
}

// One admin row: label in sans, value in mono under it, and the row's small keys on the right.
void drawAdminRow(lgfx::LovyanGFX* d, const RptrAdminLayout& a, const rptr::AdminRow& row, int i,
                  int scroll) {
  const RptrRowRects r = rptrRowRects(a, row.keys, i, scroll);
  if (r.body.y >= a.body.bottom() || r.body.bottom() <= a.body.y) return;
  const bool danger = row.danger;
  if (danger) d->fillRect(r.body.x, r.body.y, r.body.w, r.body.h, kDangerBg);
  // A toggle's or region's state colours its value, the mock's green/red .meta.
  uint16_t valueInk = kDim;
  if (row.kind == rptr::RowKind::Toggle) valueInk = row.on ? kGreen : kBad;
  if (row.kind == rptr::RowKind::Region) valueInk = row.on ? kGreen : kBad;
  if (row.kind == rptr::RowKind::Fact) valueInk = kInk;
  drawTextEllipsis(d, FontRole::SansBody, danger ? kDangerInk : kInk, danger ? kDangerBg : kScreen,
                   r.label.x, r.label.y + 8, r.label.w, row.label);
  if (row.value[0]) {
    drawTextEllipsis(d, FontRole::MonoMeta, valueInk, danger ? kDangerBg : kScreen, r.value.x,
                     r.value.y + 6, r.value.w, row.value);
  }
  if (row.keys >= 1) {
    const char* aText = row.kind == rptr::RowKind::Region ? "H" : "-";
    smallKey(d, r.keyA, aText, false);
  }
  if (row.keys >= 2) {
    const char* bText = row.kind == rptr::RowKind::Region ? "D" : "+";
    smallKey(d, r.keyB, bText, false);
  }
  d->drawFastHLine(r.body.x, r.body.bottom() - 1, r.body.w, kRowLine);
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
  if (field == KeyboardField::RptrPassword) {
    // A password is never shown again, not even back to the finger that typed it.
    const int cap = (g.draft.w - 2 * kFieldPadX - 40) / 6;
    int n = v.pwLen;
    if (n > cap) n = cap;
    if (n == 0) {
      drawTextLeft(d, FontRole::MonoMeta, kDimmer, kCodeBg, g.draft.x + kFieldPadX, cy,
                   "(password)");
    } else {
      for (int i = 0; i < n; ++i) {
        d->fillCircle((int16_t)(g.draft.x + kFieldPadX + 3 + i * 6), (int16_t)cy, 2, kInk);
      }
    }
  } else {
    const char* text = v.pwText != nullptr ? v.pwText : "";
    if (text[0] == 0) {
      drawTextLeft(d, FontRole::MonoMeta, kDimmer, kCodeBg, g.draft.x + kFieldPadX, cy,
                   v.editTitle[0] ? v.editTitle : "(type below)");
    } else {
      drawTextEllipsis(d, FontRole::MonoMeta, kInk, kCodeBg, g.draft.x + kFieldPadX, cy,
                       g.draft.w - 2 * kFieldPadX - 34, text);
    }
  }
  char used[16];
  snprintf(used, sizeof(used), "%d/%d", v.pwLen, keyboardMaxBytes(field));
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

void drawListStage(lgfx::LovyanGFX* d, const RptrView& v, const RptrLayout& r) {
  d->fillRect(r.head.x, r.head.y, r.head.w, r.head.h, kPanel);
  d->drawFastHLine(r.head.x, r.head.bottom() - 1, r.head.w, kLine);
  drawTextLeft(d, FontRole::MonoMeta, kDimmer, kPanel, 8, r.head.y + r.head.h / 2, "REPEATERS");
  drawTextLeft(d, FontRole::MonoMeta, kAcc, kPanel, 76, r.head.y + r.head.h / 2, v.state);
  chip(d, r.scan, v.scanning ? "Scanning" : "Scan", v.scanning);

  if (v.count <= 0) {
    // Never an empty list without a word: the window is open or it closed with no answer.
    const char* title = v.scanning ? "Listening..." : "No repeaters yet.";
    const char* sub = v.scanning ? "Scan sent. Waiting for repeater replies."
                                 : "Scan sends a request,\nevery repeater in range answers.";
    drawEmptyState(d, r.empty, title, sub);
    return;
  }
  d->setClipRect(r.list.x, r.list.y, r.list.w, r.list.h);
  for (int i = 0; i < v.count; ++i) {
    const RptrRow row = rptrRow(r, i, v.scroll);
    const Rect rr{r.list.x, row.y, r.list.w, row.h};
    if (!rr.overlaps(r.list)) continue;
    drawListRow(d, rr, v.items[i]);
  }
  d->clearClipRect();
}

void drawLoginStage(lgfx::LovyanGFX* d, const RptrView& v, const RptrLayout& r) {
  drawBack(d, r.back, "< list");
  drawTextEllipsis(d, FontRole::SansTitle, kInk, kScreen, r.title.x, r.title.y + 11, r.title.w,
                   v.name);
  drawTextEllipsis(d, FontRole::MonoMeta, kDim, kScreen, r.title.x, r.title.y + 21, r.title.w,
                   v.sub);
  drawInfoCard(d, r.infoCard, v);

  drawTextLeft(d, FontRole::MonoMeta, kDimmer, kScreen, r.loginLabel.x,
               r.loginLabel.y + r.loginLabel.h / 2, "ADMIN LOGIN");
  d->fillRoundRect(r.loginField.x, r.loginField.y, r.loginField.w, r.loginField.h, kRadiusSm,
                   kFieldBg);
  d->drawRoundRect(r.loginField.x, r.loginField.y, r.loginField.w, r.loginField.h, kRadiusSm,
                   v.osk ? kAcc : kLine);
  const int fy = r.loginField.y + r.loginField.h / 2;
  if (v.pwLen == 0) {
    drawTextLeft(d, FontRole::MonoMeta, kDimmer, kFieldBg, r.loginField.x + 6, fy, "password");
  } else {
    int n = v.pwLen;
    const int cap = (r.loginField.w - 16) / 6;
    if (n > cap) n = cap;
    for (int i = 0; i < n; ++i) {
      d->fillCircle((int16_t)(r.loginField.x + 9 + i * 6), (int16_t)fy, 2, kInk);
    }
  }
  d->fillRoundRect(r.loginButton.x, r.loginButton.y, r.loginButton.w, r.loginButton.h, kRadiusSm,
                   kSelBg2);
  d->drawRoundRect(r.loginButton.x, r.loginButton.y, r.loginButton.w, r.loginButton.h, kRadiusSm,
                   kAcc);
  setRole(d, FontRole::SansBody);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(kAccInk, kSelBg2);
  d->drawString(v.loggingIn ? "..." : "Login", r.loginButton.x + r.loginButton.w / 2,
                r.loginButton.y + r.loginButton.h / 2);

  if (v.result[0]) {
    drawTextEllipsis(d, FontRole::MonoMeta, v.resultOk ? kGreen : kBad, kScreen, r.result.x,
                     r.result.y + 7, r.result.w, v.result);
  }
  if (v.canForget) {
    smallKey(d, r.forget, "Forget", false);
  }
  if (r.note.h > 0 && v.note[0]) {
    d->fillRoundRect(r.note.x, r.note.y, r.note.w, r.note.h, kRadius, kWarnBg);
    d->drawRoundRect(r.note.x, r.note.y, r.note.w, r.note.h, kRadius, kWarnLine);
    drawTextEllipsis(d, FontRole::SansBody, kWarnInk, kWarnBg, r.note.x + 6, r.note.y + 10,
                     r.note.w - 12, v.note);
  }
}

void drawAdminStage(lgfx::LovyanGFX* d, const RptrView& v, const RptrAdminLayout& a) {
  drawBack(d, a.back, "< list");
  drawTextEllipsis(d, FontRole::SansTitle, kInk, kScreen, a.title.x, a.title.y + 11, a.title.w,
                   v.name);
  drawTextEllipsis(d, FontRole::MonoMeta, kDim, kScreen, a.title.x, a.title.y + 21, a.title.w,
                   v.sub);
  for (int i = 0; i < kRptrViewCount; ++i) {
    chip(d, a.chips[i], kRptrViewLabel[i], i == v.view);
  }

  d->setClipRect(a.body.x, a.body.y, a.body.w, a.body.h);
  for (int i = 0; i < v.rowCount; ++i) drawAdminRow(d, a, v.rows[i], i, v.aScroll);
  d->clearClipRect();

  // The answer strip: the exact line the last control sent, and what the repeater said.
  d->fillRect(a.answer.x, a.answer.y, a.answer.w, a.answer.h, kPanel);
  d->drawFastHLine(a.answer.x, a.answer.y, a.answer.w, kLine);
  if (v.lastCmd[0]) {
    char line[60];
    snprintf(line, sizeof(line), "> %s", v.lastCmd);
    drawTextEllipsis(d, FontRole::MonoMeta, kDimmer, kPanel, 8, a.answer.y + 9, a.answer.w - 16,
                     line);
    drawTextEllipsis(d, FontRole::MonoMeta,
                     v.answerPending ? kDim : (v.answerBad ? kBad : kGreen), kPanel, 8,
                     a.answer.y + 24, a.answer.w - 16,
                     v.answerPending ? "waiting..." : v.lastAnswer);
  } else {
    drawTextEllipsis(d, FontRole::MonoMeta, kDimmer, kPanel, 8, a.answer.y + 17, a.answer.w - 16,
                     "no command sent yet");
  }
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

  if (v.stage == RptrStage::List) {
    drawListStage(d, v, rptrLayoutFor(l2(), false));
  } else if (v.stage == RptrStage::Login) {
    drawLoginStage(d, v, rptrLayoutFor(l2(), true));
  } else {
    drawAdminStage(d, v, rptrAdminLayoutFor(l2()));
  }

  if (v.osk) {
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
