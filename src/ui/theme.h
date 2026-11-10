// SPDX-License-Identifier: MIT
// OM Lite UI theme: every colour, every size, every font role. Nothing else in src/ui may hold one.
//
// Source of truth: /home/slofi/Projects/pocket-mesh/ui-mock/index.html (CSS custom properties and the
// .dev-l2 rules). NEUTRAL GREY palette (Filip, 2026-11-10: the warm browns were the "too warm" try)
// with the OM gold accent. Tokens: docs/REBUILD.md.
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

// Surfaces (grey tokens, REBUILD.md; brightness kept, tint almost none)
constexpr uint16_t kBg        = rgb565(0x131210);  // app background (--bg)
constexpr uint16_t kScreen    = rgb565(0x100f0d);  // screen / detail fill (mock .screen)
constexpr uint16_t kPanel     = rgb565(0x1c1b19);  // --panel
constexpr uint16_t kPanel2    = rgb565(0x242321);  // --panel2, toast, keys
constexpr uint16_t kBar       = rgb565(0x171614);  // status bar and tab bar
constexpr uint16_t kLine      = rgb565(0x333230);  // --line, 1 px separators
constexpr uint16_t kRowLine   = rgb565(0x1e1d1b);  // list row bottom line

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
// Values follow the mock: a code field / draft sits on --bg, the reply bar and tag stay blue (they
// mark a quote, not a selection), the map canvas is .mapwrap and its side rail .map-side.
constexpr uint16_t kCodeBg    = rgb565(0x131210);  // mock code / .draft field
constexpr uint16_t kReplyBg   = rgb565(0x12293c);
constexpr uint16_t kReplyLine = rgb565(0x24425c);
constexpr uint16_t kReplyInk  = rgb565(0xbcd8f0);
constexpr uint16_t kTagInk    = rgb565(0x9ad7ff);
constexpr uint16_t kMapBg     = rgb565(0x191816);  // mock .mapwrap
constexpr uint16_t kMapSide   = rgb565(0x11100e);  // mock .map-side
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
constexpr int16_t kBattGap    = 8;   // mock .status .bits gap: icon -> "97%"
constexpr int16_t kBarsW      = 2;
constexpr int16_t kBarsGap    = 1;
constexpr int16_t kBarsH      = 7;
constexpr uint8_t kBarsCount  = 5;

constexpr int16_t kRadiusSm   = 3;
constexpr int16_t kRadius     = 4;
constexpr int16_t kRadiusLg   = 6;

constexpr int16_t kBadgeH     = 9;
constexpr int16_t kBadgePadX  = 2;
constexpr int16_t kGearSize   = 9;   // mock: the status ⚙ glyph at the status font size (8.5 px)

// Chat (work order item 3). Every number comes from the mock's L2 rules (.chans, .msgs, .compose,
// .osk) rounded to whole pixels for the panel.
// Chip strip: .chans padding 4px 6px, chips 4px apart, 1 px bottom line. A chip is mono 8 px text with
// 2px 5px padding, a 1 px border and a 9 px pill radius at 14 px high -> 16 px high here.
constexpr int16_t kChipRowH    = 23;
constexpr int16_t kChipH       = 16;
constexpr int16_t kChipPadX    = 5;
constexpr int16_t kChipGap     = 4;
constexpr int16_t kChipRowPadX = 6;
constexpr int16_t kChipRadius  = 8;    // pill; the mock's 9 px at 14 px high is half the height
constexpr int16_t kChipMenuW   = 30;   // the list button at the right end of the strip

// Message band: .msgs padding 5px 7px, 4px between messages, line-height 1.35 at 9 px -> 14 px pitch.
constexpr int16_t kMsgPadX     = 7;
constexpr int16_t kMsgPadY     = 5;
constexpr int16_t kMsgGap      = 4;
constexpr int16_t kMsgLineH    = 14;
constexpr int16_t kMsgHdrGap   = 4;    // time -> name -> text gaps on the first line

// Compose line: .compose padding 4px 6px, a .field of mono 8.5 px with 3px 5px padding and 1 px border,
// buttons at the sans 9 px size. 27 px keeps the field at 20 px and the line 3 px inside the edges.
constexpr int16_t kComposeH     = 27;   // 1 px top line + 3 px + 20 px field + 3 px
constexpr int16_t kComposePadX  = 6;
constexpr int16_t kComposeGap   = 4;
constexpr int16_t kFieldH       = 20;
constexpr int16_t kFieldPadX    = 5;
constexpr int16_t kKbdBtnW      = 26;
constexpr int16_t kSendW        = 44;
constexpr int16_t kCaretH       = 9;
constexpr int16_t kCaretW       = 1;

// Keyboard sheet: .osk padding 4px, a 15 px .draft line, key rows 2 px apart; keys "about 22 px"
// (the ticket). 4 + 18 + 3 + 4*22 + 3*2 + 4 = 123 px, so the sheet covers the tab bar completely.
constexpr int16_t kOskPad      = 4;
constexpr int16_t kOskRadius   = 6;
constexpr int16_t kOskKeyH     = 22;
constexpr int16_t kOskKeyGap   = 2;
constexpr int16_t kOskDraftH   = 18;
constexpr int16_t kOskDraftGap = 3;
constexpr int16_t kOskReplyH   = 18;
constexpr int16_t kKeyRadius   = 3;
constexpr uint8_t kKeyUnit     = 10;   // a normal key; wide keys are 22 units (the mock's flex 2.2)
constexpr uint8_t kKeyUnitWide = 22;

// Message menu and INFO panel: old behaviour (tap a message), new layer.
constexpr int16_t kActionRowH = 24;
constexpr int16_t kMenuHeadH  = 16;
constexpr int16_t kMenuW      = 180;
constexpr int16_t kPanelPad   = 6;
constexpr int16_t kInfoColX   = 86;   // INFO panel: the value column, measured from the panel's left

// Components the chat needs beyond the shared surfaces. Values are the mock's: the sheet is .osk
// (#191816, the same value as the map canvas), a key is #232220 with a #31302e edge, and the compose
// field is .compose .field (#171614, the same value as the bars).
constexpr uint16_t kSheet     = rgb565(0x191816);
constexpr uint16_t kKeyBg     = rgb565(0x232220);
constexpr uint16_t kKeyLine   = rgb565(0x31302e);
constexpr uint16_t kFieldBg   = rgb565(0x171614);

// Content rectangle: what is left between the two bars.
constexpr int16_t kContentY   = kStatusH;
constexpr int16_t kContentH   = kScreenH - kStatusH - kTabH;
constexpr int16_t kTabY       = kScreenH - kTabH;

// --------------------------------------------------------------------------------------------- fonts
// Two families, four sizes. Sans for names, labels and prose; mono for numbers, ids, ages and the
// status line. Generated from DejaVu Sans / DejaVu Sans Mono as anti-aliased VLW blobs (8-bit
// coverage) by tools/mkvlw.py; the blobs themselves are in src/ui/font_*.cpp.
// Sizes are whole pixels. The mock's 7.5/8/8.5/10 px were rounded up for the device panel, except
// the status line: 11 px (the brief) made "MC · EU868 · 2B" collide with the node count at 320 px,
// so the status uses the same 10 px mono as the row metadata.
// One glyph outside ASCII: mono carries U+00B7 MIDDLE DOT, the mock's status separator, added by
// tools/mkvlw.py --extra 0xb7. The settings cog is the mask rendered from U+2699 by tools/mkicons.py
// (drawGear blends it), so the sans9 metrics and the tab labels do not move.
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
