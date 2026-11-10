// SPDX-License-Identifier: MIT
// "Last heard" text for the Nodes list. See age.h for the two rules.
#include "ui/age.h"

#include <stdio.h>

namespace om {
namespace ui {

void ageText(char* out, size_t cap, uint32_t lastmod, uint32_t now, uint32_t buildEpoch) {
  if (!out || cap == 0) return;
  if (now == 0 || lastmod == 0 || lastmod > now) {
    snprintf(out, cap, "--");
    return;
  }
  // A contact can only have been heard after this firmware was built: a timestamp below the build
  // time is the previous session's 2024 clock, not an age. The next advert from the node replaces
  // it, and the row then shows a real age.
  if (buildEpoch != 0 && lastmod < buildEpoch) {
    snprintf(out, cap, "--");
    return;
  }
  const uint32_t s = now - lastmod;
  if (s >= 30UL * 86400UL) snprintf(out, cap, "30d+");
  else if (s < 60) snprintf(out, cap, "now");
  else if (s < 3600) snprintf(out, cap, "%lum", (unsigned long)(s / 60));
  else if (s < 86400) snprintf(out, cap, "%luh", (unsigned long)(s / 3600));
  else snprintf(out, cap, "%lud", (unsigned long)(s / 86400));
}

}  // namespace ui
}  // namespace om
