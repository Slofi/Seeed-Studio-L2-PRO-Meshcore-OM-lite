// SPDX-License-Identifier: MIT
// Settings screen drawing. One frame, one set of rectangles (ui/settings_layout.h).
#include "ui/screen_settings.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "../fonts.h"   // om::useAppFont: the old screens' 12 px default after the frame closes
#include "ui/keyboard_draw.h"   // the chat sheet's key caps and accent bubble (EST-352)
#include "ui/text_wrap.h"

namespace om {
namespace ui {

KeyboardField settingsEditorField(SettingsEditor e) {
  switch (e) {
    case SettingsEditor::Base64: return KeyboardField::Base64Key;
    case SettingsEditor::WifiPass: return KeyboardField::WifiPassword;
    default: return KeyboardField::NodeName;
  }
}

namespace {

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

// EST-354: the lock mark on a secured network row. Drawn as vectors (the faces carry no padlock
// glyph): a 7 x 5 body with a three-line shackle above it, one pixel wide.
void drawLock(lgfx::LovyanGFX* d, int right, int cy, uint16_t col) {
  const int x = right - 7;
  d->drawRoundRect((int16_t)x, (int16_t)cy, 7, 5, 1, col);
  d->drawLine((int16_t)(x + 2), (int16_t)cy, (int16_t)(x + 2), (int16_t)(cy - 2), col);
  d->drawLine((int16_t)(x + 2), (int16_t)(cy - 2), (int16_t)(x + 4), (int16_t)(cy - 2), col);
  d->drawLine((int16_t)(x + 4), (int16_t)(cy - 2), (int16_t)(x + 4), (int16_t)cy, col);
}

// The chat selector's chevron: down closed, up open. The same two 4 px lines the Chat tab's header
// draws, so the Settings selector looks exactly like the one it reuses.
void drawSelectorChevron(lgfx::LovyanGFX* d, int x, int cy, uint16_t col, bool up) {
  if (up) {
    d->drawLine((int16_t)x, (int16_t)(cy + 2), (int16_t)(x + 3), (int16_t)(cy - 2), col);
    d->drawLine((int16_t)(x + 3), (int16_t)(cy - 2), (int16_t)(x + 6), (int16_t)(cy + 2), col);
  } else {
    d->drawLine((int16_t)x, (int16_t)(cy - 2), (int16_t)(x + 3), (int16_t)(cy + 2), col);
    d->drawLine((int16_t)(x + 3), (int16_t)(cy + 2), (int16_t)(x + 6), (int16_t)(cy - 2), col);
  }
}

void drawBackKey(lgfx::LovyanGFX* d, const Rect& r) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, kPanel2);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, kLine);
  const int cy = r.y + r.h / 2;
  d->drawLine((int16_t)(r.x + 7), (int16_t)(cy - 3), (int16_t)(r.x + 4), (int16_t)cy, kAcc);
  d->drawLine((int16_t)(r.x + 4), (int16_t)cy, (int16_t)(r.x + 7), (int16_t)(cy + 3), kAcc);
  drawTextLeft(d, FontRole::SansTab, kDim, kPanel2, r.x + 12, cy + 1, "Back");
}

// EST-363: the title row's commit action, drawn like the keyboard's OK key (gold when there is
// something to commit, dim when there is not). It sits outside the scrolling body, so it is always
// on the glass no matter where the list has been dragged to.
void drawTitleAction(lgfx::LovyanGFX* d, const Rect& r, const char* label, bool on) {
  if (r.empty() || label == nullptr || label[0] == 0) return;
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, on ? kSelBg2 : kPanel2);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, on ? kAcc : kLine);
  setRole(d, FontRole::SansTab);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(on ? kAccInk : kDimmer, on ? kSelBg2 : kPanel2);
  d->drawString(label, r.x + r.w / 2, r.y + r.h / 2 + 1);
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
  // The title's room ends where the action key starts (EST-363), so a long page name is ellipsised
  // rather than drawn under the key.
  const int16_t titleRight = L.titleAction.empty() ? (int16_t)L.title.right()
                                                   : (int16_t)(L.titleAction.x - 6);
  drawTextEllipsis(d, FontRole::SansTitle, kInk, kScreen,
                   L.backButton.right() + 8, L.title.y + L.title.h / 2 + 1,
                   (int16_t)(titleRight - L.backButton.right() - 8), v.title ? v.title : "");
  drawTitleAction(d, L.titleAction, v.titleAction, v.titleActionOn);
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
    case SetRowKind::Selector: {
      // EST-350: a section header that is the chat selector. Same shape as the Chat tab's header
      // (mono label, the name in ink, a chevron after it), gold and lifted while the list is open.
      // `on` is the dropdown's open state.
      const uint16_t bg = row.on ? kSelBg : kScreen;
      if (row.on) d->fillRect(r.x, r.y, r.w, r.h, bg);
      setRole(d, FontRole::MonoMeta);
      d->setTextDatum(lgfx::textdatum_t::middle_left);
      d->setTextColor(kDim, bg);
      d->drawString(row.label, labelX, cy);
      const int labelW = textWidth(d, FontRole::MonoMeta, row.label);
      const int nameX = labelX + labelW + kSelGap;
      const int room = r.right() - kSetPadX - 8 - nameX;
      drawTextEllipsis(d, FontRole::SansBody, row.on ? kAccInk : kInk, bg, nameX, cy, room,
                       row.value);
      const int nameW = textWidth(d, FontRole::SansBody, row.value);
      int chevX = nameX + (nameW > room ? room : nameW) + kHeadChevronGap;
      const int chevMax = r.right() - kSetPadX - 8;
      if (chevX > chevMax) chevX = chevMax;
      drawSelectorChevron(d, chevX, cy, kDim, row.on);
      break;
    }
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
      // The dim value can be a list of short event tags (EST-361), so reserve its measured width
      // before clipping the name; long tag lists must never push the name off the row.
      const int valueW = row.value[0] ? measureText(d, FontRole::MonoMeta, row.value,
                                                    (int)strlen(row.value))
                                      : 0;
      int labelW = r.w - 2 * kSetPadX - valueW - 20;
      if (labelW < 40) labelW = 40;
      drawTextEllipsis(d, FontRole::SansBody, kInk, kScreen, labelX, cy, labelW, row.label);
      drawTextRight(d, FontRole::MonoMeta, kDim, kScreen, r.right() - kSetPadX - 14, cy,
                    row.value);
      if (row.on) {
        d->fillSmoothCircle((int16_t)(r.right() - kSetPadX - 3), (int16_t)cy, 3, kGreen);
      }
      break;
    }
    case SetRowKind::Chevron: {
      // `on` is the secured flag on a scanned network row (EST-354): a lock sits between the value
      // (RSSI) and the chevron. Other chevron rows leave it false.
      const int right = (int)(r.right() - kSetPadX);
      drawChevron(d, right, cy, kDimmer);
      int valueRight = right - 12;
      if (row.on) {
        drawLock(d, valueRight, cy + 1, kDimmer);
        valueRight -= 11;
      }
      drawTextEllipsis(d, FontRole::SansBody, kInk, kScreen, labelX, cy,
                       r.w - 2 * kSetPadX - (row.on ? 112 : 100), row.label);
      if (row.value[0]) {
        drawTextRight(d, FontRole::MonoMeta, kDim, kScreen, valueRight, cy, row.value);
      }
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

// EST-352: the editor IS the chat keyboard sheet. chatLayoutFor() hands out the sheet, the draft
// line and the four key rows; keyRow() gives that field's keys; drawKeyCap() draws them; settings
// never holds a key coordinate of its own. The old 4x10 letter grid and 5x13 base64 grid are gone.
void drawEditor(lgfx::LovyanGFX* d, const SettingsLayout& L, const SettingsView& v) {
  const KeyboardField field = settingsEditorField(v.editor);
  const ChatLayout g = chatLayoutFor(l2(), true, false, v.editNums, false, field);
  const char* hint = field == KeyboardField::Base64Key
                         ? "Channel key, base64, 24 characters"
                         : (field == KeyboardField::WifiPassword
                                ? "Wi-Fi password, 63 characters, dots"
                                : "Name, MeshCore characters, 31 maximum");
  drawTextLeft(d, FontRole::MonoMeta, kDimmer, kScreen, kSetPadX, L.body.y + 10, hint);

  d->fillRoundRect(g.sheet.x, g.sheet.y, g.sheet.w, g.sheet.h + kOskRadius, kOskRadius, kSheet);
  d->drawFastHLine(g.sheet.x, g.sheet.y, g.sheet.w, kLine);

  d->fillRoundRect(g.draft.x, g.draft.y, g.draft.w, g.draft.h, kKeyRadius, kCodeBg);
  d->drawRoundRect(g.draft.x, g.draft.y, g.draft.w, g.draft.h, kKeyRadius, kAcc);
  const bool empty = v.editText == nullptr || v.editText[0] == 0;
  const bool secret = v.editor == SettingsEditor::WifiPass && !v.editReveal;
  if (empty) {
    drawTextLeft(d, FontRole::MonoMeta, kDimmer, kCodeBg, g.draft.x + kFieldPadX,
                 g.draft.y + g.draft.h / 2, "(type on the keys below)");
  } else if (secret) {
    // Never draw the passphrase back after entry: one dot per byte, capped to the field's width.
    // The string is ASCII by the field's own filter, so a byte is one character.
    const int cap = (g.draft.w - 2 * kFieldPadX - 40) / 6;
    int n = (int)strlen(v.editText);
    if (n > cap) n = cap;
    const int dy = g.draft.y + g.draft.h / 2;
    for (int i = 0; i < n; ++i) {
      d->fillCircle((int16_t)(g.draft.x + kFieldPadX + 3 + i * 6), (int16_t)dy, 2, kInk);
    }
  } else {
    drawTextEllipsis(d, FontRole::MonoMeta, kInk, kCodeBg, g.draft.x + kFieldPadX,
                     g.draft.y + g.draft.h / 2, g.draft.w - 2 * kFieldPadX - 34, v.editText);
  }
  char used[16];
  const int maxBytes = keyboardMaxBytes(field);
  snprintf(used, sizeof(used), "%d/%d", (int)strlen(v.editText ? v.editText : ""), maxBytes);
  drawTextRight(d, FontRole::MonoMeta, kDimmer, kCodeBg, g.draft.right() - kFieldPadX,
                g.draft.y + g.draft.h / 2, used);

  for (int r = 0; r < 4; ++r) {
    KeyDef defs[10];
    const int n = keyRow(v.editNums, r, defs, field);
    for (int c = 0; c < n && c < g.keyCount[r]; ++c) {
      if (defs[c].kind == KeyKind::Send) {
        snprintf(defs[c].label, sizeof(defs[c].label), "%s", v.okLabel ? v.okLabel : "OK");
      } else if (defs[c].kind == KeyKind::Reveal) {
        snprintf(defs[c].label, sizeof(defs[c].label), "%s", v.editReveal ? "hide" : "show");
      }
      drawKeyCap(d, g.keys[r][c], defs[c], v.editShift);
    }
  }

  if (v.accentCp != 0 && v.accentRow >= 0 && v.accentRow <= 3 && v.accentCol >= 0 &&
      v.accentCol < g.keyCount[v.accentRow]) {
    drawAccentBubble(d, g.keys[v.accentRow][v.accentCol], v.accentCp);
  }
}

// EST-382: the Clear-messages confirmation. A scrim (the page behind stays readable but dimmed),
// the question wrapped by the shared wrapper, then Cancel on the left as the DEFAULT key (gold, like
// a page's commit key) and the destructive Clear on the right. A tap outside the box is Cancel.
void drawClearAsk(lgfx::LovyanGFX* d, const SettingsView& v) {
  const SettingsModalLayout m = settingsModalLayoutFor(l2().width, l2().height);
  d->fillRect(0, 0, l2().width, l2().height, kPanel2);
  d->fillRoundRect(m.box.x, m.box.y, m.box.w, m.box.h, kRadius, kPanel);
  d->drawRoundRect(m.box.x, m.box.y, m.box.w, m.box.h, kRadius, kLine);
  // The shared UTF-8 wrapper, three lines at most, every line cut on a code-point boundary. The box
  // is sized for a 31-character channel name plus the sentence.
  const char* text = v.clearText ? v.clearText : "";
  WrapLine lines[3];
  const int n = wrapText(text, measureBody, d, m.text.w, m.text.w, lines, 3);
  char buf[160];
  for (int i = 0; i < n; ++i) {
    int len = lines[i].len;
    if (len < 0) len = 0;
    if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
    memcpy(buf, text + lines[i].start, (size_t)len);
    buf[len] = 0;
    drawTextLeft(d, FontRole::SansBody, kInk, kPanel, m.text.x, m.text.y + 6 + i * 14, buf);
  }

  drawKeyBox(d, m.cancel, kSelBg2, kAcc, "Cancel", kAccInk);
  drawKeyBox(d, m.confirm, kDangerBg, kDangerLine, "Clear", kDangerInk);
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

  const SettingsLayout L = settingsLayoutFor((int16_t)l2().width, (int16_t)l2().height, v.subPage,
                                             v.selector, v.titleAction[0] != 0);
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
    // EST-350: the dropdown over the rows under it, drawn with the Chat tab's own selector drawing
    // (ui/chat_selector.cpp) and the three rectangles the hit test uses.
    if (v.selector) drawChatSelector(d, v.sel, L.selPanel, L.selList, L.selManage);
  } else {
    drawEmptyState(d, L.body, "No settings here", "Nothing on this tab yet.");
  }

  drawStatusBar(d, l2(), v.status);

  // EST-382: the confirmation is the highest layer, over the status bar too: while it is up, nothing
  // behind it can be read as a tap target.
  if (v.clearAsk) drawClearAsk(d, v);

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
  const SettingsLayout L = settingsLayoutFor((int16_t)l2().width, (int16_t)l2().height, v.subPage,
                                             false, v.titleAction[0] != 0);
  return L.backButton.contains(x, y);
}

// EST-363: the title row's commit action. Back is on the left of the same row and the action on the
// right, so the two rectangles are disjoint and one tap can only be one of them.
bool settingsTitleActionHit(const SettingsView& v, int x, int y) {
  if (v.titleAction[0] == 0) return false;
  const SettingsLayout L = settingsLayoutFor((int16_t)l2().width, (int16_t)l2().height, v.subPage,
                                             false, true);
  return settingsTitleActionHitIn(L, x, y);
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

SettingsKeyHit settingsEditorKeyAt(const SettingsView& v, int x, int y) {
  SettingsKeyHit out;
  if (v.editor == SettingsEditor::None) return out;
  const KeyboardField field = settingsEditorField(v.editor);
  const ChatLayout g = chatLayoutFor(l2(), true, false, v.editNums, false, field);
  const KeyHit key = keyAt(g, v.editNums, x, y, field);
  if (key.row < 0) return out;
  KeyDef defs[10];
  const int n = keyRow(v.editNums, key.row, defs, field);
  if (key.col < 0 || key.col >= n) return out;
  out.pressed = true;
  out.kind = defs[key.col].kind;
  out.row = key.row;
  out.col = key.col;
  out.ch = defs[key.col].label[0];
  if (v.editShift && !v.editNums && out.ch >= 'a' && out.ch <= 'z') {
    out.ch = (char)(out.ch - 32);
  }
  return out;
}

SettingsModalHit settingsClearAskHit(const SettingsView& v, int x, int y) {
  const SettingsModalLayout m = settingsModalLayoutFor(l2().width, l2().height);
  (void)v;
  return settingsModalHitAt(m, x, y);
}

SelectorPress settingsSelectorPress(const SettingsView& v, int x, int y) {
  SelectorPress none;
  none.kind = SelectorHitKind::Close;   // a press with no dropdown open is not ours
  if (!v.selector) return none;
  const SettingsLayout L = settingsLayoutFor((int16_t)l2().width, (int16_t)l2().height, v.subPage,
                                             v.selector, v.titleAction[0] != 0);
  return selectorPress(v.sel, L.selPanel, L.selList, L.selManage, x, y);
}

}  // namespace ui
}  // namespace om
