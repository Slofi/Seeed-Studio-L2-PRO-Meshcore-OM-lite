// SPDX-License-Identifier: MIT
// EST-384: the shared map picker's pixels. Moved out of screen_rptr.cpp so the RPTR Position page and
// the node's Fixed position draw the SAME picker, not a copy.
#include "ui/map_pick.h"

#include <string.h>

#include "../fonts.h"   // useAppFont, the same face the RPTR frame restores
#include "board.h"
#include "map.h"
#include "ui/map_chrome.h"   // EST-393: the Map's own centre cross, reused here
#include "ui/rptr_layout.h"

namespace om {
namespace ui {
namespace {

// The picker's one strong key (Apply / Save). Same shape as the RPTR page's action keys; kept local
// so this file does not need the RPTR screen's private drawing helpers.
void drawPickAction(lgfx::LovyanGFX* d, const Rect& r, const char* label) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, kSelBg2);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, kAcc);
  setRole(d, FontRole::SansBody);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(kAccInk, kSelBg2);
  d->drawString(label, r.x + r.w / 2, r.y + r.h / 2);
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

}  // namespace

void drawMapPick(lgfx::LovyanGFX* d, const MapPickView& v) {
  om::mapRender();
  const RptrMapPickLayout m = rptrMapPickLayoutFor(l2(), v.action);
  if (!om::mapReady()) {
    drawEmptyState(d, Rect{8, 60, 304, 100}, "Map pack not open.",
                   "The card needs /maps/slo/tiles.pack.");
  }
  drawBack(d, m.back, v.backLabel);
  smallKey(d, m.zoomIn, "+", false);
  smallKey(d, m.zoomOut, "-", false);
  // EST-394: the zoom label owns its own rectangle now, UNDER the back key (rptr_layout.cpp), so the
  // wide "Cancel" of the Settings picker can never crowd it. Same MonoMeta face and kDim ink as before.
  if (v.hint[0]) {
    drawTextEllipsis(d, FontRole::MonoMeta, kDim, kMapBg, m.hint.x,
                     (int16_t)(m.hint.y + m.hint.h / 2), m.hint.w, v.hint);
  }
  // EST-393: the Map's own centre cross, at the same 7x7 box and centre (160,133). The helper and
  // the constants are the Map screen's (ui/map_chrome.h), so there is no second cross to drift.
  drawMapCentreCross(d, m.cross);

  d->fillRect(m.panel.x, m.panel.y, m.panel.w, m.panel.h, kPanel);
  d->drawFastHLine(m.panel.x, m.panel.y, m.panel.w, kLine);
  drawTextEllipsis(d, FontRole::MonoMeta, kInk, kPanel, m.coords.x,
                   (int16_t)(m.coords.y + m.coords.h / 2), m.coords.w, v.coords);
  d->fillRoundRect(m.useGps.x, m.useGps.y, m.useGps.w, m.useGps.h, kRadiusSm, kPanel2);
  d->drawRoundRect(m.useGps.x, m.useGps.y, m.useGps.w, m.useGps.h, kRadiusSm, kLine);
  setRole(d, FontRole::MonoMeta);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(v.gps ? kInk : kDimmer, kPanel2);
  d->drawString("Use my GPS position", m.useGps.x + m.useGps.w / 2, m.useGps.y + m.useGps.h / 2);
  if (!m.apply.empty()) drawPickAction(d, m.apply, v.actionLabel);
}

void drawMapPickScreen(const MapPickView& v, const StatusModel& status) {
  if (!omFrameBegin()) {
    omFrameEnd();
    return;
  }
  lgfx::LovyanGFX* d = om_display();
  if (d == nullptr) {
    omFrameEnd();
    return;
  }
  d->fillScreen(kScreen);
  drawMapPick(d, v);
  drawStatusBar(d, l2(), status);
  om::useAppFont(d);
  omFrameEnd();
}

}  // namespace ui
}  // namespace om
