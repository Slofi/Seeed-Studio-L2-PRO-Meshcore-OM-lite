// SPDX-License-Identifier: MIT
// RPTR screen (EST-351): the mock's repeater list and repeater page. Everything that the mesh can
// answer today is real (the contacts of type repeater and their stored path / SNR / time); the
// login and the remote admin set say "not built yet" instead of pretending.
#pragma once

#include <LovyanGFX.hpp>

#include "ui/layout.h"
#include "ui/rptr_layout.h"
#include "ui/widgets.h"

namespace om {
namespace ui {

constexpr int kRptrMaxItems = 32;
constexpr int kRptrInfoLines = 5;

struct RptrItem {
  char name[32] = "";
  char meta[40] = "";     // "2 hops · SNR 7.5 dB"
};

struct RptrView {
  StatusModel status;
  bool detail = false;
  int count = 0;
  RptrItem items[kRptrMaxItems];
  int scroll = 0;
  int selected = -1;
  char name[32] = "";
  char sub[64] = "";      // "repeater · 2 hops · last heard 14:02"
  char info[kRptrInfoLines][72] = {};
  int infoCount = 0;
};

void rptrScreenDraw(const RptrView& v);

}  // namespace ui
}  // namespace om
