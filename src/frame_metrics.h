// SPDX-License-Identifier: MIT
// How bright a frame is, and whether a screen change needs the backlight dip (EST-355).
//
// Why this exists: the diagonal tear (board.h, EST-353) is written on EVERY push, but it is only
// SEEN when the frame being replaced and the frame arriving differ strongly in brightness (Filip,
// 2026-11-10 16:35). So the decision "does this change get the dip" must be the same rule in the
// firmware and in test/host, and it must be cheap: a frame is 320 x 240 pixels, and this runs on the
// loop task just before a transition starts.
//
// The measure is one number per frame, 0..255: the mean perceived brightness of a grid of sampled
// pixels. Luma uses the BT.601 weights on the 5/6/5 channels expanded to 8 bits, so a white frame is
// 250 (not exactly 255 because 5-bit red and blue stop at 248; close enough for a threshold).
// Plain C++ (no Arduino, no LovyanGFX), so the host tests compile this file's own arithmetic.
#pragma once

#include <stdint.h>

namespace om {

// One native RGB565 word -> perceived brightness, 0..255. The weights are 77/150/29 on the 8-bit
// channels (green carries most of the eye's response); the shifts expand 5/6/5 to 0..248/0..252.
constexpr uint8_t lumaFromRgb565(uint16_t c) {
  const uint32_t r = (uint32_t)((c >> 11) & 0x1Fu) * 8u;
  const uint32_t g = (uint32_t)((c >> 5) & 0x3Fu) * 4u;
  const uint32_t b = (uint32_t)(c & 0x1Fu) * 8u;
  return (uint8_t)((77u * r + 150u * g + 29u * b) >> 8);
}

// The mean of a frame's samples. `sum` is the summed luma of `count` pixels; 0 when nothing was
// sampled, so a missing frame reads as "black" (which only makes the dip rule more eager).
inline uint8_t meanLumaFromSum(uint32_t sum, uint32_t count) {
  if (count == 0) return 0;
  const uint32_t mean = sum / count;
  return (uint8_t)(mean > 255u ? 255u : mean);
}

// The dip rule. `mapInvolved` is true when the Map tab is on either side of the change: its topo
// tiles are the one page this firmware has with a bright background, so a switch to or from it is
// always dipped (Filip saw the tear exactly there). Otherwise the two frames are compared: a
// difference above `threshold` (theme.h kChangeLumaDelta) means the tear would be seen, so the dip
// is needed; below it the two frames are close enough in brightness that the crossing line reads as
// a seam inside the same picture and was never visible.
constexpr bool screenChangeNeedsDip(uint8_t oldLuma, uint8_t newLuma, bool mapInvolved,
                                    uint8_t threshold) {
  if (mapInvolved) return true;
  const int diff = (int)oldLuma - (int)newLuma;
  const int magnitude = diff < 0 ? -diff : diff;
  return magnitude > (int)threshold;
}

}  // namespace om
