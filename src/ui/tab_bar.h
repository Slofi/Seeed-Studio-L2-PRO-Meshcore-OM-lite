// SPDX-License-Identifier: MIT
// EST-365: the ONE tab bar model. Every screen that draws the bar carries this struct and hands it
// to drawTabBar(), so the unread count cannot be forgotten by a screen that passes a literal 0.
// The shell builds it in exactly one place (currentTabBar() in main.cpp) from unreadTotal() and the
// new-node dot; the model is plain C++ so test/host builds every tab/page state.
#pragma once

#include <stdint.h>

#include "ui/layout.h"

namespace om {
namespace ui {

struct TabBarModel {
  Tab active = Tab::Nodes;
  int unread = 0;          // the Chat tab's badge: every unread channel message and DM
  bool newNodeDot = false; // EST-342: the gold dot on the Nodes tab
};

// Copy the shell's numbers into a screen's bar model. Kept as a function (not two assignments at
// each call site) so the test can walk every screen state through the same rule.
constexpr TabBarModel tabBarModel(Tab active, int unread, bool newNodeDot) {
  TabBarModel m;
  m.active = active;
  m.unread = unread > 0 ? unread : 0;
  m.newNodeDot = newNodeDot;
  return m;
}

}  // namespace ui
}  // namespace om
