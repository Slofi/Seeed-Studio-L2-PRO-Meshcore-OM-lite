// SPDX-License-Identifier: MIT
// The Info page (EST-342): a full page over the Nodes tab, no tab bar, the header row is Back.
#pragma once

#include "ui/node_info.h"
#include "ui/widgets.h"

namespace om {
namespace ui {

// The header row that draws the back chevron and also answers the tap.
Rect nodeInfoBackRect();

void nodeInfoDraw(const StatusModel& status, const NodeInfoModel& m);

}  // namespace ui
}  // namespace om
