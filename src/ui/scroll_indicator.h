// SPDX-License-Identifier: MIT
// EST-414: the scroll indicator of a scrolling list, in ONE place. The arithmetic is plain C++ (no
// LovyanGFX) so test/host can prove the thumb's size and position with the system g++; the drawing
// is in scroll_indicator.cpp and every screen calls it with the rectangle its own layout handed out
// (Layout::listBody(), ChatLayout::selList, ChatLayout::msgs), so the width, the colours and the
// geometry cannot differ between the Nodes list, the chat's channel list and an open chat.
//
// It is drawn in the list's own right padding, not over text: kRowPadX, kSelPadX and kMsgPadX are all
// 7 px and the track is the last kScrollTrackW (4) px of the viewport, so the text column ends 3 px
// before the track starts. Rows and bands keep their width; nothing is narrowed.
#pragma once

#include <stdint.h>

#include "layout.h"
#include "theme.h"

// The real declaration: LovyanGFX lives in the inline namespace `lgfx::v1`, so this merges with the
// library's own class instead of clashing with it, and the header stays usable without the library
// (the arithmetic needs no display; test/host compiles this as-is).
namespace lgfx {
inline namespace v1 {
class LovyanGFX;
}
}  // namespace lgfx

namespace om {
namespace ui {

// The track and the thumb, in screen coordinates. Both are empty and `visible` is false when the
// content fits the viewport: a list that does not scroll shows no indicator.
struct ScrollIndicator {
  bool visible = false;
  Rect track;
  Rect thumb;
};

// `viewport` is the scrolling list's own rectangle (already from the layout), `contentTotal` the
// whole content height in px, `contentVisible` the height the viewport shows, `scroll` the current
// offset and `maxScroll` its clamp. `fromEnd` is true for a bottom-anchored view, the open chat's
// message band: there scroll 0 is the NEWEST message, so the thumb's rest position is the bottom of
// the track (and the top of the content is the track's top). The top-down lists pass false.
//
// The thumb's length is the visible share of the content (visible / total, at least
// kScrollMinThumbH), and its top is the scrolled share of the free travel (scroll / maxScroll).
constexpr ScrollIndicator scrollIndicatorFor(const Rect& viewport, int contentTotal,
                                             int contentVisible, int scroll, int maxScroll,
                                             bool fromEnd) {
  ScrollIndicator s;
  if (viewport.empty() || contentVisible <= 0 || contentTotal <= contentVisible) return s;
  // The track sits flush on the viewport's right edge, inside the list's own right padding.
  const int16_t trackX = (int16_t)(viewport.right() - kScrollTrackW);
  s.track = Rect{trackX, viewport.y, kScrollTrackW, viewport.h};

  int thumbH = (int)(((long)viewport.h * contentVisible) / contentTotal);
  if (thumbH < kScrollMinThumbH) thumbH = kScrollMinThumbH;
  if (thumbH > viewport.h) thumbH = viewport.h;

  const int max = maxScroll > 0 ? maxScroll : 0;
  int at = fromEnd ? max - scroll : scroll;
  if (at < 0) at = 0;
  if (at > max) at = max;
  const int travel = (int)viewport.h - thumbH;
  const int top = max > 0 ? (int)(((long)travel * at) / max) : 0;

  s.thumb = Rect{trackX, (int16_t)(viewport.y + top), kScrollTrackW, (int16_t)thumbH};
  s.visible = true;
  return s;
}

// The one drawing helper: the track in the separator grey, the thumb in the OM gold, both from
// theme.h. Does nothing when the content fits.
void drawScrollIndicator(lgfx::LovyanGFX* d, const ScrollIndicator& s);

}  // namespace ui
}  // namespace om
