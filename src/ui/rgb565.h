// SPDX-License-Identifier: MIT
// One rule for RGB565 colours crossing a surface's byte order (EST-352).
//
// LovyanGFX stores an RGB565 pixel in one of two byte orders:
//   rgb565_nonswapped: the native little-endian word, 0xRRRRRGGGGGBBBBB (what JPEGDEC hands back and
//                      what our canvases hold),
//   rgb565_2Byte:      the two bytes exchanged (what the panel wants on the wire and what the frame
//                      sprite's 16-bit write depth stores).
// The frame sprite stores the exchanged order, so a native buffer pushed with pushImage() must be
// declared native (`setSwapBytes(true)`), never "already stored". Pushing the stored order straight
// through is exactly what turned the topo tiles magenta/cyan (EST-352).
//
// Plain C++ (no LovyanGFX), so test/host proves the conversion with the system g++ and the device
// uses the same functions: a known colour through store + read back lands as the same colour.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

// The two bytes exchanged: 0xED89 <-> 0x89ED.
constexpr uint16_t rgb565Swab(uint16_t v) {
  return (uint16_t)((uint16_t)(v >> 8) | (uint16_t)(v << 8));
}

// A native colour as a surface that stores rgb565_2Byte keeps it.
constexpr uint16_t rgb565FrameStore(uint16_t native) { return rgb565Swab(native); }

// A stored word read back to the native order the host screenshot wants.
constexpr uint16_t rgb565FrameRead(uint16_t stored) { return rgb565Swab(stored); }

// RGB888 -> native RGB565 (5/6/5), the exact value JPEGDEC writes for a decoded pixel.
constexpr uint16_t rgb565FromRgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3));
}

constexpr uint8_t rgb565Red(uint16_t v) { return (uint8_t)((v >> 11) << 3); }
constexpr uint8_t rgb565Green(uint16_t v) { return (uint8_t)(((v >> 5) & 0x3F) << 2); }
constexpr uint8_t rgb565Blue(uint16_t v) { return (uint8_t)((v & 0x1F) << 3); }

}  // namespace ui
}  // namespace om
