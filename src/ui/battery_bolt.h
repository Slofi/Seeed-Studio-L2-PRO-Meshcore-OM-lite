// SPDX-License-Identifier: MIT
// EST-402: the charging bolt's pixel shape, one source for the panel and the preview.
//
// A 5 x 7 gold bolt drawn with integer rows (fillRect), so it is crisp at this size: no anti-aliased
// mask, no font glyph. It is centred on the 16 x 7 battery icon and spans the icon's own seven rows.
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

// The classic lightning silhouette: the upper edge steps left into the middle bar, the lower edge
// steps back right and then left again. Every row is non-empty, so the shape reads at 1:1.
constexpr BoltRun kBoltRuns[kBoltH] = {
    {3, 2},   // ...##
    {2, 2},   // ..##.
    {1, 2},   // .##..
    {0, 5},   // #####
    {3, 2},   // ...##
    {2, 2},   // ..##.
    {1, 2},   // .##..
};

}  // namespace ui
}  // namespace om
