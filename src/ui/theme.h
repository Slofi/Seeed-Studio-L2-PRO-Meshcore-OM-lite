// SPDX-License-Identifier: MIT
// OM Lite UI theme: every colour, every size, every font role. Nothing else in src/ui may hold one.
//
// Source of truth: /home/slofi/Projects/pocket-mesh/ui-mock/index.html (CSS custom properties and the
// .dev-l2 rules). Warm palette + OM gold accent, Filip 2026-11-10.
//
// This header is plain C++ (no Arduino, no LovyanGFX) so test/host can compile the layout with the
// system g++. The smooth font blobs are declared in ui/fonts.h; their pixel sizes live here.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

// --------------------------------------------------------------------------------------------- colours
// #rrggbb -> native RGB565 (r 5 bits, g 6, b 5), the same arithmetic the panel uses.
constexpr uint16_t rgb565(uint32_t rgb) {
  return (uint16_t)((((rgb >> 16) & 0xFF) >> 3) << 11 |
                    (((rgb >> 8) & 0xFF) >> 2) << 5 |
                    ((rgb & 0xFF) >> 3));
}

// Surfaces
constexpr uint16_t kBg        = rgb565(0x15110f);  // app background (--bg)
constexpr uint16_t kScreen    = rgb565(0x130e0b);  // screen / detail fill
constexpr uint16_t kPanel     = rgb565(0x211a16);  // --panel
constexpr uint16_t kPanel2    = rgb565(0x2c231d);  // --panel2, toast, keys
constexpr uint16_t kBar       = rgb565(0x1d1611);  // status bar and tab bar
constexpr uint16_t kLine      = rgb565(0x40322a);  // --line, 1 px separators
constexpr uint16_t kRowLine   = rgb565(0x251d17);  // list row bottom line

// Ink
constexpr uint16_t kInk       = rgb565(0xe8edf5);  // --ink
constexpr uint16_t kDim       = rgb565(0x93a0b4);  // --dim
constexpr uint16_t kDimmer    = rgb565(0x5f6b7d);  // --dimmer
constexpr uint16_t kWhite     = rgb565(0xffffff);

// Accent and selection
constexpr uint16_t kAcc       = rgb565(0xe8b04f);  // OM gold (--acc)
constexpr uint16_t kAccInk    = rgb565(0xf6e3b8);  // selected text
constexpr uint16_t kSelBg     = rgb565(0x1f1a10);  // selected row / tab
constexpr uint16_t kSelBg2    = rgb565(0x3a2c12);  // strong selected chip / button

// Node types and status
constexpr uint16_t kClient    = rgb565(0x4aa3ff);  // client dot (blue, never used as selection)
constexpr uint16_t kGreen     = rgb565(0x39d98a);  // repeater dot, online / good
constexpr uint16_t kWarn      = rgb565(0xffb020);  // room dot, warning
constexpr uint16_t kBad       = rgb565(0xff5c5c);  // unread badge, error

// Components the later screens already need (chat, map, RPTR) so a second palette cannot appear.
constexpr uint16_t kCodeBg    = rgb565(0x18120d);
constexpr uint16_t kReplyBg   = rgb565(0x12293c);
constexpr uint16_t kReplyLine = rgb565(0x24425c);
constexpr uint16_t kReplyInk  = rgb565(0xbcd8f0);
constexpr uint16_t kTagInk    = rgb565(0x9ad7ff);
constexpr uint16_t kMapBg     = rgb565(0x161a10);
constexpr uint16_t kMapSide   = rgb565(0x16100b);
constexpr uint16_t kWarnBg    = rgb565(0x2a1e10);
constexpr uint16_t kWarnLine  = rgb565(0x5c4320);
constexpr uint16_t kWarnInk   = rgb565(0xffd08a);
constexpr uint16_t kDangerBg  = rgb565(0x3a1418);
constexpr uint16_t kDangerLine= rgb565(0x7a2630);
constexpr uint16_t kDangerInk = rgb565(0xffd7da);

// --------------------------------------------------------------------------------------------- sizes
// The L2 Pro is 320x240, touch. The mock is the spec; the ticket fixes the two bars at 26 and 25.
// (The mock's L2 CSS still says 18 px for .status; the ticket and the brief say 26, so 26 it is.)
constexpr int16_t kScreenW    = 320;
constexpr int16_t kScreenH    = 240;

constexpr int16_t kStatusH    = 26;
constexpr int16_t kTabH       = 25;
constexpr int16_t kTabTopLine = 2;
constexpr int16_t kRowH       = 30;

constexpr int16_t kStatusPadX = 6;
constexpr int16_t kStatusGap  = 6;
constexpr int16_t kStatusDot  = 5;

constexpr int16_t kRowPadX    = 7;
constexpr int16_t kRowPadY    = 3;
constexpr int16_t kRowGap     = 6;
constexpr int16_t kTypeDot    = 6;

constexpr int16_t kTabGap     = 2;
constexpr int16_t kTabIcon    = 11;
constexpr int16_t kTabPadY    = 3;   // icon top: 2 px selected line + 1 px; leaves a 2 px gap to the label

constexpr int16_t kBattW      = 16;
constexpr int16_t kBattH      = 7;
constexpr int16_t kBarsW      = 2;
constexpr int16_t kBarsGap    = 1;
constexpr int16_t kBarsH      = 7;
constexpr uint8_t kBarsCount  = 5;

constexpr int16_t kRadiusSm   = 3;
constexpr int16_t kRadius     = 4;
constexpr int16_t kRadiusLg   = 6;

constexpr int16_t kBadgeH     = 9;
constexpr int16_t kBadgePadX  = 2;
constexpr int16_t kGearSize   = 11;

// Content rectangle: what is left between the two bars.
constexpr int16_t kContentY   = kStatusH;
constexpr int16_t kContentH   = kScreenH - kStatusH - kTabH;
constexpr int16_t kTabY       = kScreenH - kTabH;

// --------------------------------------------------------------------------------------------- fonts
// Two families, four sizes. Sans for names, labels and prose; mono for numbers, ids, ages and the
// status line. Generated from DejaVu Sans / DejaVu Sans Mono as anti-aliased VLW blobs (8-bit
// coverage) by tools/mkvlw.py; the blobs themselves are in src/ui/font_*.cpp.
// Sizes are whole pixels. The mock's 7.5/8/8.5/10 px were rounded up for the device panel, except
// the status line: 11 px (the brief) made "MC . EU868 . 2B" collide with the node count at 320 px,
// so the status uses the same 10 px mono as the row metadata.
enum class FontRole : uint8_t {
  SansBody = 0,    // list names, empty state              (11 px, mock 10 px)
  SansTitle,       // detail heading                       (14 px, mock 13 px)
  SansTab,         // tab labels                           ( 9 px, mock 7.5 px)
  MonoMeta,        // ages, path, numbers in a row         (10 px, mock 8 px)
  MonoStatus,      // the status line                      (10 px, same face as MonoMeta)
  Count,
};

constexpr int16_t kSansBodyPx   = 11;
constexpr int16_t kSansTitlePx  = 14;
constexpr int16_t kSansTabPx    = 9;
constexpr int16_t kMonoMetaPx   = 10;
constexpr int16_t kMonoStatusPx = 10;

// Draw style
constexpr int16_t kLineW      = 1;
constexpr int16_t kHairline   = 1;

}  // namespace ui
}  // namespace om
