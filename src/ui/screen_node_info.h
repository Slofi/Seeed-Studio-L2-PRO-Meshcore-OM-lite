// SPDX-License-Identifier: MIT
// The Info page (EST-342): a full page over the Nodes tab, no tab bar, the header row is Back.
// EST-400: its geometry is ui/node_info_layout.h (one header, one clipped body, one floor hint) and
// the body scrolls, so the ninth row can never land on the hint.
#pragma once

#include "ui/node_info.h"
#include "ui/node_info_layout.h"
#include "ui/widgets.h"

namespace om {
namespace ui {

// The header row that draws the back chevron and also answers the tap (nodeInfoLayoutFor().back).
Rect nodeInfoBackRect();

// `scroll` is the body's offset in px (0..nodeInfoMaxScroll(nodeInfoLineCount(m), layout)).
void nodeInfoDraw(const StatusModel& status, const NodeInfoModel& m, int scroll = 0);

}  // namespace ui
}  // namespace om
