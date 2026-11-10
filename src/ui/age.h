// SPDX-License-Identifier: MIT
// "Last heard" text for the Nodes list.
//
// Two rules, both about honesty rather than formatting:
//   * a stored lastmod older than the build time cannot be real. It was written by a previous
//     session whose clock was still the old 2024 default, before a real source set it. Printing
//     "30d+" there claims a contact was heard, with a date that never existed; "--" says "unknown"
//     and the next advert from that node replaces it with a real time.
//   * beyond a month the honest answer is "30d+", never "730d".
//
// Plain C++, so test/host proves it with the system g++.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace ui {

// Writes one of "--", "now", "5m", "3h", "12d", "30d+". buildEpoch 0 disables the pre-build check.
void ageText(char* out, size_t cap, uint32_t lastmod, uint32_t now, uint32_t buildEpoch);

}  // namespace ui
}  // namespace om
