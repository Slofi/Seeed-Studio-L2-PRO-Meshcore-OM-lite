// SPDX-License-Identifier: MIT
// Settings screen drawing. One frame, one set of rectangles (ui/settings_layout.h).
#include "ui/screen_settings.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "../fonts.h"   // om::useAppFont: the old screens' 12 px default after the frame closes
#include "ui/text_wrap.h"

namespace om {
namespace ui {

const char* const kSettingsNameChars = "QWERTZUIOPASDFGHJKL_YXCVBNM.,-1234567890";
const char* const kSettingsBase64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

namespace {

// --------------------------------------------------------------------------------------- geometry
int editorColumns(SettingsEditor e) { return e == SettingsEditor::Base64 ? 13 : 10; }
int editorRows(SettingsEditor e) { return e == SettingsEditor::Base64 ? 5 : 4; }
int editorKeyW(SettingsEditor e) { return e == SettingsEditor::Base64 ? 22 : 28; }
int editorKeyH(SettingsEditor e) { return e == SettingsEditor::Base64 ? 22 : 24; }
int editorPitchX(SettingsEditor e) { return e == SettingsEditor::Base64 ? 23 : 31; }
int editorPitchY(SettingsEditor e) { return e == SettingsEditor::Base64 ? 24 : 28; }
int editorStartX(SettingsEditor e) { return e == SettingsEditor::Base64 ? 10 : 8; }
int editorStartY(const SettingsLayout& L) { return L.body.y + 34; }

Rect editorFieldRect(const SettingsLayout& L) {
  return Rect{(int16_t)(L.body.x + 8), (int16_t)(L.body.y + 4), (int16_t)(L.body.w - 16), 24};
}

Rect editorKeyRect(const SettingsLayout& L, SettingsEditor e, int index) {
  const int cols = editorColumns(e);
  const int r = index / cols;
  const int c = index % cols;
  return Rect{(int16_t)(editorStartX(e) + c * editorPitchX(e)),
              (int16_t)(editorStartY(L) + r * editorPitchY(e)), (int16_t)editorKeyW(e),
              (int16_t)editorKeyH(e)};
}

Rect editorDelRect(const SettingsLayout& L) {
  return Rect{(int16_t)(L.body.x + 8), (int16_t)(L.body.bottom() - 32), 90, 24};
}

Rect editorOkRect(const SettingsLayout& L) {
  return Rect{(int16_t)(L.body.right() - 8 - 90), (int16_t)(L.body.bottom() - 32), 90, 24};
}

// ---------------------------------------------------------------------------------------- drawing
void drawKeyBox(lgfx::LovyanGFX* d, const Rect& r, uint16_t fill, uint16_t line, const char* text,
                uint16_t ink) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kKeyRadius, fill);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kKeyRadius, line);
  if (text != nullptr && text[0]) {
    setRole(d, FontRole::SansBody);
    d->setTextDatum(lgfx::textdatum_t::middle_center);
    d->setTextColor(ink, fill);
    d->drawString(text, r.x + r.w / 2, r.y + r.h / 2);
  }
}

void drawChevron(lgfx::LovyanGFX* d, int right, int cy, uint16_t col) {
  // Two 1 px lines, 4 px each: no glyph, so no missing-glyph box at any face.
  d->drawLine((int16_t)(right - 4), (int16_t)(cy - 3), (int16_t)right, (int16_t)cy, col);
  d->drawLine((int16_t)right, (int16_t)cy, (int16_t)(right - 4), (int16_t)(cy + 3), col);
}

void drawBackKey(lgfx::LovyanGFX* d, const Rect& r) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, kPanel2);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, kLine);
  const int cy = r.y + r.h / 2;
  d->drawLine((int16_t)(r.x + 7), (int16_t)(cy - 3), (int16_t)(r.x + 4), (int16_t)cy, kAcc);
  d->drawLine((int16_t)(r.x + 4), (int16_t)cy, (int16_t)(r.x + 7), (int16_t)(cy + 3), kAcc);
  drawTextLeft(d, FontRole::SansTab, kDim, kPanel2, r.x + 12, cy + 1, "Back");
}

void drawTabRow(lgfx::LovyanGFX* d, const SettingsLayout& L, SettingsTab active) {
  if (L.tabrow.empty()) return;
  d->fillRect(L.tabrow.x, L.tabrow.y, L.tabrow.w, L.tabrow.h, kBar);
  d->drawFastHLine(L.tabrow.x, L.tabrow.bottom() - 1, L.tabrow.w, kLine);
  for (int i = 0; i < kSettingsTabCount; ++i) {
    const Rect& r = L.tabs[i];
    const bool on = ((int)active == i);
    if (on) d->fillRect(r.x, r.y, r.w, r.h, kSelBg);
    const int cy = r.y + r.h / 2 - 1;
    setRole(d, FontRole::MonoMeta);
    d->setTextDatum(lgfx::textdatum_t::middle_center);
    d->setTextColor(on ? kAcc : kDimmer, on ? kSelBg : kBar);
    d->drawString(settingsTabLabel((SettingsTab)i), r.x + r.w / 2, cy);
    // The selected tab's gold underline, at the floor of the row like the bottom tab bar's line.
    if (on) d->fillRect(r.x, r.bottom() - 2, r.w, 2, kAcc);
  }
}

void drawTitleRow(lgfx::LovyanGFX* d, const SettingsLayout& L, const SettingsView& v) {
  drawBackKey(d, L.backButton);
  drawTextEllipsis(d, FontRole::SansTitle, kInk, kScreen,
                   L.backButton.right() + 8, L.title.y + L.title.h / 2 + 1,
                   L.title.w - L.backButton.right() - 16, v.title ? v.title : "");
  d->drawFastHLine(L.title.x, L.title.bottom() - 1, L.title.w, kLine);
}

int measureBody(void* ctx, const char* text, int len) {
  return measureText((lgfx::LovyanGFX*)ctx, FontRole::SansBody, text, len);
}

void drawWarn(lgfx::LovyanGFX* d, const Rect& r, const char* text) {
  d->fillRoundRect(r.x, r.y + 3, r.w - 2 * kSetPadX, r.h - 6, kRadius, kWarnBg);
  d->drawRoundRect(r.x, r.y + 3, r.w - 2 * kSetPadX, r.h - 6, kRadius, kWarnLine);
  const int x = r.x + kSetPadX + 6;
  const int w = r.w - 2 * (kSetPadX + 6);
  WrapLine lines[3];
  const int n = wrapText(text ? text : "", measureBody, d, w, w, lines, 3);
  char buf[96];
  for (int i = 0; i < n; ++i) {
    if (lines[i].len <= 0) continue;
    int len = lines[i].len;
    if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
    memcpy(buf, text + lines[i].start, (size_t)len);
    buf[len] = 0;
    drawTextLeft(d, FontRole::SansBody, kWarnInk, kWarnBg, x, r.y + 9 + i * 11, buf);
  }
}

void drawRow(lgfx::LovyanGFX* d, const SettingRow& row, const Rect& r) {
  const int cy = r.y + r.h / 2;
  const int labelX = r.x + kSetPadX;
  const bool lineBelow = row.kind != SetRowKind::Section && row.kind != SetRowKind::Warn &&
                         row.kind != SetRowKind::Button;

  switch (row.kind) {
    case SetRowKind::Section:
      setRole(d, FontRole::MonoMeta);
      d->setTextDatum(lgfx::textdatum_t::middle_left);
      d->setTextColor(kDimmer, kScreen);
      d->drawString(row.label, labelX, r.y + r.h - 4);
      return;
    case SetRowKind::Warn:
      drawWarn(d, r, row.label);
      return;
    case SetRowKind::Note:
      drawTextEllipsis(d, FontRole::SansBody, kDim, kScreen, labelX, cy, r.w - 2 * kSetPadX,
                       row.label);
      if (lineBelow) d->drawFastHLine(r.x, r.bottom() - 1, r.w, kRowLine);
      return;
    case SetRowKind::Button: {
      const Rect b{(int16_t)(r.x + kSetPadX), (int16_t)(r.y + 3), (int16_t)(r.w - 2 * kSetPadX),
                   (int16_t)(r.h - 6)};
      const uint16_t bg = row.danger ? kDangerBg : kPanel2;
      const uint16_t edge = row.danger ? kDangerLine : kLine;
      d->fillRoundRect(b.x, b.y, b.w, b.h, kRadius, bg);
      d->drawRoundRect(b.x, b.y, b.w, b.h, kRadius, edge);
      setRole(d, FontRole::SansBody);
      d->setTextDatum(lgfx::textdatum_t::middle_center);
      d->setTextColor(row.danger ? kDangerInk : kInk, bg);
      d->drawString(row.label, b.x + b.w / 2, b.y + b.h / 2);
      if (row.value[0]) {
        drawTextRight(d, FontRole::MonoMeta, row.danger ? kDangerInk : kDim, bg, b.right() - 8,
                      b.y + b.h / 2, row.value);
      }
      return;
    }
    case SetRowKind::Stepper: {
      const Rect minus = settingsStepMinusRect(r);
      const Rect plus = settingsStepPlusRect(r);
      drawKeyBox(d, minus, kKeyBg, kKeyLine, "", kInk);
      drawKeyBox(d, plus, kKeyBg, kKeyLine, "", kInk);
      const int mcy = minus.y + minus.h / 2;
      d->fillRect(minus.x + 5, mcy, minus.w - 10, 1, kInk);
      d->fillRect(plus.x + 5, mcy, plus.w - 10, 1, kInk);
      d->fillRect(plus.x + plus.w / 2, plus.y + 5, 1, plus.h - 10, kInk);
      setRole(d, FontRole::SansBody);
      d->setTextDatum(lgfx::textdatum_t::middle_left);
      d->setTextColor(kInk, kScreen);
      d->drawString(row.label, labelX, cy);
      const Rect val = settingsStepValueRect(r);
      drawTextRight(d, FontRole::MonoMeta, kInk, kScreen, val.right(), cy, row.value);
      break;
    }
    case SetRowKind::Toggle: {
      setRole(d, FontRole::SansBody);
      d->setTextDatum(lgfx::textdatum_t::middle_left);
      d->setTextColor(kInk, kScreen);
      d->drawString(row.label, labelX, cy);
      drawTextRight(d, FontRole::MonoMeta, row.on ? kGreen : kDimmer, kScreen, r.right() - kSetPadX,
                    cy, row.on ? (row.value[0] ? row.value : "ON") : "OFF");
      break;
    }
    case SetRowKind::Check: {
      drawTextEllipsis(d, FontRole::SansBody, kInk, kScreen, labelX, cy,
                       r.w - 2 * kSetPadX - 90, row.label);
      drawTextRight(d, FontRole::MonoMeta, kDim, kScreen, r.right() - kSetPadX - 14, cy,
                    row.value);
      if (row.on) {
        d->fillSmoothCircle((int16_t)(r.right() - kSetPadX - 3), (int16_t)cy, 3, kGreen);
      }
      break;
    }
    case SetRowKind::Chevron: {
      drawTextEllipsis(d, FontRole::SansBody, kInk, kScreen, labelX, cy, r.w - 2 * kSetPadX - 94,
                       row.label);
      if (row.value[0]) {
        drawTextRight(d, FontRole::MonoMeta, kDim, kScreen, r.right() - kSetPadX - 12, cy,
                      row.value);
      }
      drawChevron(d, (int)(r.right() - kSetPadX), cy, kDimmer);
      break;
    }
    case SetRowKind::Value:
    default: {
      drawTextEllipsis(d, FontRole::SansBody, kInk, kScreen, labelX, cy,
                       r.w - 2 * kSetPadX - 12, row.label);
      if (row.value[0]) {
        drawTextRight(d, FontRole::MonoMeta, kDim, kScreen, r.right() - kSetPadX, cy, row.value);
      }
      break;
    }
  }
  if (lineBelow) d->drawFastHLine(r.x, r.bottom() - 1, r.w, kRowLine);
}

void drawEditor(lgfx::LovyanGFX* d, const SettingsLayout& L, const SettingsView& v) {
  const Rect field = editorFieldRect(L);
  d->fillRect(field.x, field.y, field.w, field.h, kCodeBg);
  d->drawRect(field.x, field.y, field.w, field.h, kLine);
  if (v.editText != nullptr && v.editText[0]) {
    drawTextEllipsis(d, FontRole::SansBody, kInk, kCodeBg, field.x + 6, field.y + field.h / 2,
                     field.w - 12, v.editText);
  } else {
    drawTextLeft(d, FontRole::SansBody, kDimmer, kCodeBg, field.x + 6, field.y + field.h / 2,
                 "(tap the letters)");
  }

  const char* chars =
      v.editor == SettingsEditor::Base64 ? kSettingsBase64Chars : kSettingsNameChars;
  const int total = (int)strlen(chars);
  const int rows = editorRows(v.editor);
  const int cols = editorColumns(v.editor);
  for (int i = 0; i < total && i < rows * cols; ++i) {
    const Rect kr = editorKeyRect(L, v.editor, i);
    char ch[2] = {chars[i], 0};
    drawKeyBox(d, kr, kKeyBg, kKeyLine, ch, kInk);
  }

  drawKeyBox(d, editorDelRect(L), kPanel2, kLine, "< DEL", kInk);
  drawKeyBox(d, editorOkRect(L), kSelBg2, kAcc, v.okLabel ? v.okLabel : "OK", kAccInk);
}

}  // namespace

void settingsScreenDraw(const SettingsView& v) {
  if (!omFrameBegin()) {
    omFrameEnd();
    return;
  }
  lgfx::LovyanGFX* d = om_display();
  if (!d) {
    omFrameEnd();
    return;
  }

  const SettingsLayout L = settingsLayoutFor((int16_t)l2().width, (int16_t)l2().height, v.subPage);
  d->fillScreen(kScreen);
  drawTabRow(d, L, v.tab);
  drawTitleRow(d, L, v);

  if (v.editor != SettingsEditor::None) {
    drawEditor(d, L, v);
  } else if (v.rows != nullptr && v.rowCount > 0) {
    d->setClipRect(L.body.x, L.body.y, L.body.w, L.body.h);
    for (int i = 0; i < v.rowCount; ++i) {
      if (settingsRowVisible(L, v.rows, v.rowCount, i, v.scroll).empty()) continue;
      drawRow(d, v.rows[i], settingsRowRect(L, v.rows, v.rowCount, i, v.scroll));
    }
    d->clearClipRect();
  } else {
    drawEmptyState(d, L.body, "No settings here", "Nothing on this tab yet.");
  }

  drawStatusBar(d, l2(), v.status);

  // The old direct-draw screens share the panel and set their own font; leave them the default.
  om::useAppFont(d);
  omFrameEnd();
}

int settingsTabHit(const SettingsView& v, int x, int y) {
  if (v.subPage) return -1;
  const SettingsLayout L = settingsLayoutFor((int16_t)l2().width, (int16_t)l2().height, false);
  return settingsTabAtIn(L, x, y);
}

bool settingsBackHit(const SettingsView& v, int x, int y) {
  const SettingsLayout L = settingsLayoutFor((int16_t)l2().width, (int16_t)l2().height, v.subPage);
  return L.backButton.contains(x, y);
}

int settingsRowHit(const SettingsView& v, int x, int y) {
  if (v.editor != SettingsEditor::None || v.rows == nullptr || v.rowCount <= 0) return -1;
  const SettingsLayout L = settingsLayoutFor((int16_t)l2().width, (int16_t)l2().height, v.subPage);
  return settingsRowAtIn(L, v.rows, v.rowCount, x, y, v.scroll);
}

int settingsStepperHit(const SettingsView& v, int x, int y) {
  const int i = settingsRowHit(v, x, y);
  if (i < 0 || v.rows[i].kind != SetRowKind::Stepper) return 0;
  const SettingsLayout L = settingsLayoutFor((int16_t)l2().width, (int16_t)l2().height, v.subPage);
  return settingsStepperPart(settingsRowRect(L, v.rows, v.rowCount, i, v.scroll), x, y);
}

int settingsEditorKeyHit(const SettingsView& v, int x, int y) {
  if (v.editor == SettingsEditor::None) return -1;
  const SettingsLayout L = settingsLayoutFor((int16_t)l2().width, (int16_t)l2().height, v.subPage);
  const char* chars =
      v.editor == SettingsEditor::Base64 ? kSettingsBase64Chars : kSettingsNameChars;
  const int total = (int)strlen(chars);
  for (int i = 0; i < total; ++i) {
    if (editorKeyRect(L, v.editor, i).contains(x, y)) return i;
  }
  return -1;
}

bool settingsEditorDelHit(const SettingsView& v, int x, int y) {
  if (v.editor == SettingsEditor::None) return false;
  const SettingsLayout L = settingsLayoutFor((int16_t)l2().width, (int16_t)l2().height, v.subPage);
  return editorDelRect(L).contains(x, y);
}

bool settingsEditorOkHit(const SettingsView& v, int x, int y) {
  if (v.editor == SettingsEditor::None) return false;
  const SettingsLayout L = settingsLayoutFor((int16_t)l2().width, (int16_t)l2().height, v.subPage);
  return editorOkRect(L).contains(x, y);
}

}  // namespace ui
}  // namespace om
