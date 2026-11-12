// SPDX-License-Identifier: MIT
// EST-414: the drawing half of the one scroll indicator (ui/scroll_indicator.h holds the geometry).
// Nothing here decides a coordinate: the rectangles come from scrollIndicatorFor(), which takes the
// list's own layout rectangle. Drawn after the rows, so it is never covered by a row's bottom line.
#include "ui/scroll_indicator.h"

#include <LovyanGFX.hpp>

namespace om {
namespace ui {

void drawScrollIndicator(lgfx::LovyanGFX* d, const ScrollIndicator& s) {
  if (d == nullptr || !s.visible || s.track.empty() || s.thumb.empty()) return;
  d->fillRect(s.track.x, s.track.y, s.track.w, s.track.h, kScrollTrack);
  // A 4 px pill: the thumb reads as a thumb, not as a second line, and its colour is the accent the
  // rest of the app uses for "this is the live thing".
  d->fillRoundRect(s.thumb.x, s.thumb.y, s.thumb.w, s.thumb.h, (int16_t)(kScrollTrackW / 2),
                   kScrollThumb);
}

}  // namespace ui
}  // namespace om
