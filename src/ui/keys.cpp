// SPDX-License-Identifier: MIT
// EST-400: one copy of the small keys the RPTR page and the map picker draw, and one editor key hit
// test for the Settings and RPTR sheets.
#include "ui/keys.h"

#include "ui/widgets.h"

namespace om {
namespace ui {

void drawSmallKey(lgfx::LovyanGFX* d, const Rect& r, const char* text, bool strong) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, strong ? kSelBg2 : kPanel2);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, strong ? kAcc : kLine);
  setRole(d, FontRole::MonoMeta);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(strong ? kAccInk : kInk, strong ? kSelBg2 : kPanel2);
  d->drawString(text, r.x + r.w / 2, r.y + r.h / 2);
}

void drawSheetBackKey(lgfx::LovyanGFX* d, const Rect& r, const char* label) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, kPanel2);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, kLine);
  drawTextLeft(d, FontRole::MonoMeta, kInk, kPanel2, r.x + 8, r.y + r.h / 2, label);
}

void drawActionKey(lgfx::LovyanGFX* d, const Rect& r, const char* label, bool danger) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, danger ? kDangerBg : kSelBg2);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, danger ? kBad : kAcc);
  setRole(d, FontRole::SansBody);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(danger ? kDangerInk : kAccInk, danger ? kDangerBg : kSelBg2);
  d->drawString(label, r.x + r.w / 2, r.y + r.h / 2);
}

EditorKeyHit editorKeyAt(const ChatLayout& g, KeyboardField field, bool nums, bool shift, int x,
                         int y) {
  EditorKeyHit out;
  const KeyHit key = keyAt(g, nums, x, y, field);
  if (key.row < 0) return out;
  KeyDef defs[10];
  const int n = keyRow(nums, key.row, defs, field);
  if (key.col < 0 || key.col >= n) return out;
  out.pressed = true;
  out.kind = defs[key.col].kind;
  out.row = key.row;
  out.col = key.col;
  out.ch = defs[key.col].label[0];
  if (shift && !nums && out.ch >= 'a' && out.ch <= 'z') out.ch = (char)(out.ch - 32);
  return out;
}

}  // namespace ui
}  // namespace om
