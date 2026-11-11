// SPDX-License-Identifier: MIT
// EST-384: the shared map picker's pixels. Moved out of screen_rptr.cpp so the RPTR Position page and
// the node's Fixed position draw the SAME picker, not a copy.
#include "ui/map_pick.h"

#include <string.h>

#include "../fonts.h"   // useAppFont, the same face the RPTR frame restores
#include "board.h"
#include "map.h"
#include "ui/keys.h"        // EST-400: the shared small keys (also the RPTR page's)
#include "ui/map_chrome.h"   // EST-393: the Map's own centre cross, reused here
#include "ui/rptr_layout.h"

namespace om {
namespace ui {
namespace {

// EST-400: nothing here any more. drawSmallKey / drawSheetBackKey / drawActionKey are ui/keys.cpp,
// the one copy the RPTR page draws too; this file used to carry byte-identical versions of all three
// (and a comment explaining that the copy was deliberate, which is what EST-384 was avoiding).

}  // namespace

void drawMapPick(lgfx::LovyanGFX* d, const MapPickView& v) {
  om::mapRender();
  const RptrMapPickLayout m = rptrMapPickLayoutFor(l2(), v.action);
  if (!om::mapReady()) {
    // EST-400: the empty state's band comes from the layout (inside the picker's own map area with
    // kMapMargin on each side), not from the L2's 320 px and a literal {8, 60, 304, 100}.
    drawEmptyState(d, m.empty, "Map pack not open.",
                   "The card needs /maps/slo/tiles.pack.");
  }
  drawSheetBackKey(d, m.back, v.backLabel);
  drawSmallKey(d, m.zoomIn, "+", false);
  drawSmallKey(d, m.zoomOut, "-", false);
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
  if (!m.apply.empty()) drawActionKey(d, m.apply, v.actionLabel, false);
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
