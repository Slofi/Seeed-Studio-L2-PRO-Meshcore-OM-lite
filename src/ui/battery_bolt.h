// SPDX-License-Identifier: MIT
// EST-402/404: the charging bolt's pixel shape, one source for the panel and the preview.
//
// A 7 x 11 gold bolt drawn with integer rows (fillRect), so it is crisp at this size: no anti-aliased
// mask, no font glyph. EST-404: it is centred on the 16 x 7 battery icon and is 4 px taller than the
// body, so its tips stand 2 px above and below the frame and are never clipped by it.
// `x` is the run's left column from the bolt box's left edge; `w` its width. theme.h owns the two
// sizes (kBoltW / kBoltH); this file owns only the shape.
#pragma once

#include <stdint.h>

#include "ui/theme.h"

namespace om {
namespace ui {

struct BoltRun {
  int8_t x;
  uint8_t w;
};

// The classic lightning silhouette: the top tip sits upper right, the upper edge steps left into the
// one-row middle bar, then the lower edge starts at the bar's right end and steps left again to the
// bottom tip at lower left. Every row is non-empty, so the shape reads at 1:1. EST-404: 11 rows, so
// the first and last rows are the tips that stand 2 px clear of the icon's 7 px frame.
constexpr BoltRun kBoltRuns[kBoltH] = {
    {4, 3},   // ....###
    {3, 3},   // ...###.
    {2, 3},   // ..###..
    {1, 3},   // .###...
    {0, 3},   // ###....
    {0, 7},   // #######  (the middle bar, at the body's centre line)
    {3, 4},   // ...####
    {2, 3},   // ..###..
    {1, 3},   // .###...
    {0, 3},   // ###....
    {0, 2},   // ##.....
};

}  // namespace ui
}  // namespace om
