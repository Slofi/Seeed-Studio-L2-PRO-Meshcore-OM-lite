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

// EST-390 (Filip, 2026-11-11): the L2 has no vibration motor, so the pressed look IS the touch
// feedback. ONE constant for the whole app: the accent (OM gold) mixed into whatever the element has
// already drawn, on EXACTLY the rectangle the hit test uses. It is a tint, not a second plate, so a
// key, a tab, a row and a sheet button all read as "the app saw the finger" without redrawing their
// own content, and the text under it stays readable. 56/255 is about 22 % gold: on the darkest
// surfaces (kKeyBg #232220) that lifts the plate to about #4a3c28, clearly above the resting key,
// while the key's white glyph keeps its contrast. ui/press.h holds pressBlend() and the rule.
constexpr uint8_t kPressedAlpha = 56;
constexpr uint16_t kPressedTint = kAcc;

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
constexpr uint16_t kMapCredit = rgb565(0x9aa3b0);  // tile attribution, plain dim text on the tiles
// EST-370 (Filip ~23:40): the attribution is PLAIN text now, no outline and no plate ("no outline on
// the credits!"). Its box is still mapCreditRect(); the screen draws it transparently.
// EST-373 (Filip 2026-11-11 ~00:38: "just a cross, small, thin, no outlines"): the Map's centre mark
// is TWO thin dark lines and nothing else. With the 1 px kMapBg outline gone, the old white kInk is
// invisible on the light topo tiles (1.02:1 on #f0f0e0), so the cross gets one dark ink of its own.
// Measured on the real z13 tile image: 14.16:1 on the #f0f0e0 ground, 12.34:1 on #e0e0e0, 10.56:1 on
// the #d0d0d0 road grey, 3.10:1 on its darkest 1 % (#6c6c6a). A dark TILE would swallow it (1.09:1 on
// #181818): that is what one ink and no outline costs, and this tile pack has no dark tiles.
constexpr uint16_t kMapCrossInk = rgb565(0x202020);
// EST-374 (Filip 2026-11-11, the "hide" idea): while the bars are hidden the zoom tabs leave their
// + and - glyphs behind at the same centre points, with no tab body. The glyph is the SAME SansTitle
// character as on the tab (same size, same stroke); only its ink changes, because the tab's own kInk
// on a kPanel background would be a white glyph floating on the topo with nothing around it. Measured
// on the real z13 tile: 4.34:1 on the #f0f0e0 ground (34.6 % of the image), 3.78:1 on #e0e0e0 and
// 3.23:1 on the #d0d0d0 road grey, 3.69:1 on the map canvas itself (#191816), and 1.08:1 on the
// darkest 1 % (#6c6c6a): a grey glyph on a dark thin line blends in, which is the honest limit of a
// mid grey and why the glyph box is several pixels wide. It stays well below the dark centre cross
// (#202020) so it never competes with it.
constexpr uint16_t kMapGhostInk = rgb565(0x707070);
// EST-368: OM's Traces (OverMesh .trace CSS): a dashed route, a moving dot with a short fading
// tail, and an expanding ring for a direct arrival.
// EST-372 (Filip ~23:55: "make it a little bit more visible, also maybe change the trace colour to
// a bolder one"): one bold fuchsia-violet, full opacity. Measured against the real z13 tile
// (map-preview-ljubljana-z13-2026-11-09.png): 4.1:1 on the #f0f0e0 ground, 3.6:1 on #e0e0e0 and
// 3.8:1 on a #181818 dark tile; the old #8b5cf6 at the old 34 % alpha was about 1.5:1 on the
// light ground, which is why it disappeared. Widths and the dot's outline are here with it.
constexpr uint16_t kTraceLine = rgb565(0xc026d3);   // the route, dashes and the direct-arrival ring
constexpr uint16_t kTraceTail = rgb565(0xc026d3);   // the tail behind the dot
constexpr uint16_t kTraceDot  = rgb565(0xc026d3);   // the dot itself
constexpr uint16_t kTraceDotOutline = rgb565(0x14101a);  // the dot's 1 px dark ring
constexpr uint16_t kTraceRing = rgb565(0xc026d3);   // the direct arrival's expanding ring
constexpr int16_t kTraceRouteW = 2;       // the dashed route, 2 px
constexpr int16_t kTraceTailW = 2;        // the fading tail, 2 px
constexpr int16_t kTraceDotR = 2;         // 5 px fill
constexpr int16_t kTraceDotOutlineW = 1;  // one dark pixel around it (7 px overall)
constexpr int16_t kTraceRingW = 2;        // the expanding ring, 2 px
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

// ----------------------------------------------------------------------- the screen-change dip
// EST-355. A tab switch used to be one push into a panel that scans the same RAM it is being
// written to, so the scan pointer and the write pointer cross along a diagonal (board.h has the
// whole mechanism, EST-353). DISPOFF/DISPON around the push did not hide it on this panel (Filip,
// 2026-11-10 ~16:30: "it is still visible on the switches"), so the light itself is taken down for
// the push: nothing the panel scans while the frame is being written is visible. The three
// durations are here, not in board.cpp, so they can be tuned after Filip looks at the glass.
//
// Only a change whose old and new frames differ strongly in brightness gets the dip: the tear is
// there on every push, but it is SEEN only when the two frames differ strongly in brightness (Filip,
// 16:35: "the map has a bright background colour making it visible; other pages have the same
// coloured background"). An instant switch keeps its push and no blink.
constexpr uint32_t kChangeOutMs = 40;     // light down: the wake path's gamma curve (fade.h), fast
// One full panel refresh at the frame rate the panel is configured for (~60 Hz -> 16.7 ms). The L2
// wires no TE line (board.h), so the frame phase cannot be read: this is the ~60 Hz assumption, not
// a measurement. It is the pause between "the push has ended" and "the light may come up", so the
// panel has scanned the new frame at least once before anything is lit.
constexpr uint32_t kChangeSettleMs = 17;
constexpr uint32_t kChangeInMs = 60;      // light up: the same curve, slower so it reads as a fade
// The dip rule. Luma is 0..255 (frame_metrics.h); 32 is about an eighth of the range, far above the
// few units that separate two dark pages (a gold label, a toast) and far below what one of the
// map's topo tiles adds.
constexpr uint8_t kChangeLumaDelta = 32;
// The brightness sample grid: every 15th pixel in both axes = 21 x 16 = 336 samples per frame. Cheap
// enough to take twice per change, dense enough that no page can hide its background between them.
constexpr int16_t kChangeLumaStep = 15;
// How long after a change ends a finger that is already down is swallowed (touch is not read while
// the change runs, so a finger that lands inside it must lift before it can tap, the same rule the
// wake gate applies).
constexpr uint32_t kChangeGateMs = 40;

// The new top bar (EST-337): time on the left, then from the right edge the gear, battery, GPS and
// the status icons that appear only when they are on. The clock is always five mono glyphs ("08:53"
// or "--:--"), so its width is fixed by the mono advance (6 px) and the fault mark's slot does not
// move when the time arrives. Icons are the vector masks from tools/mkicons.py; the Wi-Fi and
// Bluetooth slots are reserved here and drawn only when those settings exist (later tickets).
constexpr int16_t kStatusClockW    = 30;  // 5 glyphs x 6 px, MonoStatus
constexpr int16_t kStatusIconBtW   = 9;   // the rune mask is 9 x 13
constexpr int16_t kStatusIconBtH   = 13;
constexpr int16_t kStatusIconWifi  = 11;
constexpr int16_t kStatusIconDnd   = 11;  // a moon: "do not disturb", the existing dnd setting
constexpr int16_t kStatusIconFault = 11;  // the red radio-fault mark

constexpr int16_t kRowPadX    = 7;
constexpr int16_t kRowPadY    = 3;
constexpr int16_t kRowGap     = 6;
constexpr int16_t kTypeDot    = 6;

// The Nodes screen's one-line header above the list ("4 nodes heard · 2 in the last hour"), in mono
// dim. It is part of the content, so the rows start below it and the tap bands use the same split.
constexpr int16_t kNodesHeadH = 18;

// A one-line toast near the bottom of the content (the fault mark's explanation). Above the tab bar
// with a margin, so it never covers the bar.
constexpr int16_t kToastH     = 22;
constexpr int16_t kToastMargin = 8;
// How long a toast stays up (EST-346, Filip: 5 s instead of 3 s). ONE constant for every toast: the
// new-node notice, a ping result, a trace and the radio fault all pass it to showToast(), so a later
// change is one number. Any touch while a toast shows dismisses it and is spent (main.cpp).
constexpr uint32_t kToastMs   = 5000;

constexpr int16_t kTabGap     = 2;
constexpr int16_t kTabIcon    = 11;
constexpr int16_t kTabPadY    = 3;   // icon top: 2 px selected line + 1 px; leaves a 2 px gap to the label
constexpr int16_t kTabLabelPadB = 5; // label centre, in px up from the tab bar's floor

// The unread badge on the Chat tab (Filip, 2026-11-10: "too fuzzy, make it sharper"). One red shape
// for every count that sits on kBad: a 13 px circle for one digit, a 19x13 pill for two ("10"),
// drawn with LovyanGFX's anti-aliased fills, with a 1 px ring in the bar colour so it separates from
// the icon underneath. The digits are a pixel face (ui/badge_font.h), never a smooth font: at this
// size the smooth face's coverage bled white into the red and read pink.
// Placement: the top-right corner of the tab icon, starting 1 px under the selected tab's gold line
// (2 px) and ending 1 px above the tab label's line box, so it never touches either.
constexpr int16_t kBadgeD      = 13;              // diameter, 2 * 6 + 1
constexpr int16_t kBadgePillW  = 19;              // two digits (two 5 px glyphs and a 1 px gap, centred)
// EST-364: above 99 the badge says "99+", three glyphs (5+1+5+1+5 = 17 px) and the same 3 px of
// padding on each side as the 19 px pill.
constexpr int16_t kBadgePill3W = 23;              // "99+"
constexpr int16_t kBadgeRing   = 1;               // the separating ring, in the bar colour
constexpr int16_t kBadgeTop    = kTabTopLine + 1; // tab top -> badge top (clear of the gold line)
constexpr int16_t kBadgeCxOfs  = kTabIcon / 2;    // tab centre -> badge centre: the icon's right edge
// The new-node notice's dot: a plain 5 px gold circle (no count), same corner as the badge.
constexpr int16_t kNewDotR    = 2;

constexpr int16_t kBattW      = 16;
constexpr int16_t kBattH      = 7;
constexpr int16_t kBattGap    = 8;   // mock .status .bits gap: icon -> "97%"
// EST-402/404: the charging bolt drawn OVER the battery icon while external power is present. EST-404:
// the box is 7 x 11, so it is 4 px taller than the 7 px body and its tips stand 2 px above and below
// the frame instead of being clipped to the interior. 7 of 16 px is the middle 44 % of the body. Its
// 1 px bar-colour edge is drawn first, all round, so the gold stays crisp over the fill AND the frame.
// The shape's rows are ui/battery_bolt.h; gold is the accent from widgets.cpp.
constexpr int16_t kBoltW      = 7;
constexpr int16_t kBoltH      = 11;
constexpr int16_t kBarsW      = 2;
constexpr int16_t kBarsGap    = 1;
constexpr int16_t kBarsH      = 7;
constexpr uint8_t kBarsCount  = 5;

constexpr int16_t kRadiusSm   = 3;
constexpr int16_t kRadius     = 4;
constexpr int16_t kRadiusLg   = 6;

constexpr int16_t kGearSize   = 13;  // the vector gear mask (tools/mkicons.py), 8 teeth at 13x13

// Chat (work order item 3). Every number comes from the mock's L2 rules (.chans, .msgs, .compose,
// .osk) rounded to whole pixels for the panel.
// Header row (replaces the chip strip, Filip 2026-11-10): the open chat's name and a down chevron on
// the left, the gold dot counting unread in the OTHER chats on the right, 1 px bottom line.
constexpr int16_t kHeadH        = 23;
// EST-382: the Chat page's header carries the shortened chat selector AND the ADVERT key. The key's
// tap target must be at least 30 x 26 (the ticket) and the drawn rectangle IS the tap target, so the
// Chat header is 26 px. Nodes and the Info page keep kHeadH; only the chat geometry uses this one.
constexpr int16_t kChatHeadH    = 26;
constexpr int16_t kHeadPadX     = 8;
constexpr int16_t kHeadChevronW = 7;   // the down chevron drawn after the name (two 4 px lines)
constexpr int16_t kHeadChevronGap = 5;
constexpr int16_t kElsewhereDot = 5;   // the gold dot: something is waiting in another chat
constexpr int16_t kElsewhereGap = 4;
constexpr int16_t kElsewhereW   = 20;  // room kept for its count ("99+"), so the name never jumps

// EST-382: the ADVERT key in the Chat header and its small Flood / Local dropdown. The key is 62 x 26
// (above the 30 x 26 minimum), the option rows are 36 px (about 1.3x the selector's 30 px rows).
constexpr int16_t kAdvertW       = 62;
constexpr int16_t kAdvertGap     = 6;   // selector -> key
constexpr int16_t kAdvertOptW    = 76;  // wide enough for "Flood" + padding
constexpr int16_t kAdvertOptH    = 36;
constexpr int16_t kAdvertMenuPad = 1;   // the panel's border
constexpr int16_t kSelMonoAdvance = 6;  // FontRole::MonoMeta: one advance per glyph (host-tested)

// The chat selector (the dropdown over the message area). Section labels, two-line chat rows and the
// pinned "Manage channels" row at the panel's floor.
constexpr int16_t kSelPadX      = 7;
constexpr int16_t kSelRowH      = 30;  // mock list rows: min 30 px
constexpr int16_t kSelSectionH  = 15;  // "Channels" / "Direct messages"
constexpr int16_t kSelEmptyH    = 22;  // "No direct messages yet"
constexpr int16_t kSelManageH   = 30;  // the last row: Manage channels >
constexpr int16_t kSelDot       = 5;   // the open chat's gold dot in the list
constexpr int16_t kSelDotGap    = 6;   // dot -> name
constexpr int16_t kSelGap       = 6;   // name -> age, preview -> unread count
constexpr int16_t kSelLine1Cy   = 9;   // from the row's top: name / age
constexpr int16_t kSelLine2Cy   = 21;  // from the row's top: sender and the start of the text
constexpr int16_t kSelCountH    = 10;  // EST-382: the unread count's own rect on line 2
constexpr int16_t kSelChevronW  = 5;   // the right chevron of the Manage row
// A drag stretches the list this many px past either end and the release snaps it back (the same
// fingertip of give the message band has). The list follows the finger: see selDragScroll().
constexpr int16_t kSelGive      = 10;

// Message band: .msgs padding 5px 7px, 4px between messages, line-height 1.35 at 9 px -> 14 px pitch.
constexpr int16_t kMsgPadX     = 7;
constexpr int16_t kMsgPadY     = 5;
constexpr int16_t kMsgGap      = 4;
constexpr int16_t kMsgLineH    = 14;
constexpr int16_t kMsgHdrGap   = 4;    // time -> name -> text gaps on the first line
// The chat line's time column is always five mono glyphs (HH:MM) wide, so the name and the text start
// at the same x whether the clock shows "14:02" or the age fallback "5m" (EST-338).
constexpr int16_t kMsgStampW   = 5 * 6;

// Compose line: .compose padding 4px 6px, a .field of mono 8.5 px with 3px 5px padding and 1 px border.
// 27 px keeps the field at 20 px and the line 3 px inside the edges. No Send button (Filip,
// 2026-11-10) and no keyboard button either (Filip, 2026-11-10: "remove the keyboard icon, tapping
// the message line already brings it up"): the field is the whole compose row.
constexpr int16_t kComposeH     = 27;   // 1 px top line + 3 px + 20 px field + 3 px
constexpr int16_t kComposePadX  = 6;
constexpr int16_t kFieldH       = 20;
constexpr int16_t kFieldPadX    = 5;
constexpr int16_t kCaretH       = 9;
constexpr int16_t kCaretW       = 1;
// The draft's remaining-byte counter: shown at the right of the draft line once this many bytes are
// left, in mono dim, so the composer never cuts a message on send without saying why (EST-334).
constexpr int16_t kComposeWarn  = 20;
constexpr int16_t kComposeCountGap = 4;   // draft tail -> counter

// A code point the face cannot draw: one small dim box, a space plus 2 px wide (Filip, 2026-11-10:
// the old path drew LovyanGFX's full-height missing-glyph rectangle, which read as the caret).
constexpr int16_t kMissingExtra = 2;
constexpr int16_t kMissingBoxH  = 6;

// Keyboard sheet: .osk padding 4px, a 15 px .draft line, key rows 2 px apart; keys "about 22 px"
// (the ticket). 4 + 18 + 3 + 4*22 + 3*2 + 4 = 123 px, so the sheet covers the tab bar completely.
constexpr int16_t kOskPad      = 4;
constexpr int16_t kOskRadius   = 6;
// EST-400: the message / action sheets round their TOP corners with this and overflow the bottom
// edge by the same amount. They used the keyboard sheet's radius, so retuning the keyboard would
// silently change the chat's and the Nodes sheets too. Own constant, same 6 px value.
constexpr int16_t kSheetRadius = kRadiusLg;
constexpr int16_t kOskKeyH     = 22;
constexpr int16_t kOskKeyGap   = 2;
constexpr int16_t kOskDraftH   = 18;
constexpr int16_t kOskDraftGap = 3;
constexpr int16_t kOskReplyH   = 18;
constexpr int16_t kKeyRadius   = 3;
constexpr uint8_t kKeyUnit     = 10;   // a normal key; wide keys are 22 units (the mock's flex 2.2)
constexpr uint8_t kKeyUnitWide = 22;
constexpr uint8_t kKeyUnitSpace = 40;  // the bottom row's space, wider than the comma and the dot

// The accent preview (EST-346): a long press on s, c or z holds a small bubble above that key with the
// accented letter (š, č, ž, or the capital with shift). 26 px fits the widest glyph at 14 px with air,
// and the 2 px gap keeps the bubble clear of the key's own border. Clamped below the status bar.
constexpr int16_t kAccentBubbleW = 26;
constexpr int16_t kAccentBubbleH = 24;
constexpr int16_t kAccentBubbleGap = 2;

// The emoji page takes exactly the four key rows' rectangle (94 px), so the sheet height and the
// message band above it do not move. A group cell is the ticket's 22 px; the Recent strip uses
// 15 px cells so all 16 fit in one row beside Send, with a 13 px glyph centred in each.
constexpr int16_t kEmojiCell      = 22;
constexpr int16_t kEmojiRecentCell = 15;
constexpr int16_t kEmojiRailW     = 40;
constexpr int16_t kEmojiRailCellH = 15;
constexpr int16_t kEmojiStripH    = 20;
constexpr int16_t kEmojiGridGap   = 2;
constexpr int16_t kEmojiSendW     = 26;
constexpr int16_t kEmojiAbcH      = 17;

// The message sheet (a tap on a message, EST-336): a bottom sheet in the mock's style, like the
// keyboard sheet, over the tab bar. Its two-line header carries sender, channel, age, hops and SNR;
// the rows below it are kSheetRowH tall and end on a 1 px row line. Seven rows (a channel message
// once DMs exist) still leave the status bar clear: 2 * 6 + 34 + 7 * 22 = 200 px from the bottom.
constexpr int16_t kSheetPadX    = 10;
constexpr int16_t kSheetPadY    = 6;
constexpr int16_t kSheetHeadH   = 34;
constexpr int16_t kSheetRowH    = 22;
// The Nodes long-press sheet has at most eight rows (a client and a repeater both top out there);
// 20 px keeps the whole panel above the status bar: 12 + 34 + 8 * 20 = 206 px, top edge at 34.
constexpr int16_t kNodeSheetRowH = 20;
// EST-400: the value column of a Nodes sheet row ("SNR 3.2", "on"). The label ellipsises before
// this much room, and the value is right-aligned a sheet pad in from the row's own edge.
constexpr int16_t kNodeSheetValueW = 70;
constexpr int16_t kSheetLine1Cy = 11;   // from the header's top: the sender
constexpr int16_t kSheetLine2Cy = 25;   // from the header's top: channel, hops, SNR
constexpr int16_t kSheetGap     = 6;    // a row's label -> its value
constexpr int16_t kActionRowH   = 24;   // the old action menu used this; other screens still do
constexpr int16_t kMenuHeadH    = 16;
constexpr int16_t kMenuW        = 180;
constexpr int16_t kPanelPad     = 6;
constexpr int16_t kInfoColX     = 86;   // the value column of a label/value panel

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
// One range outside ASCII: every face carries U+00A0..U+017F (Latin-1 Supplement and Latin Extended-A,
// added by tools/mkvlw.py --extra, EST-346), so Slovenian, Croatian, German, Italian, Czech and Polish
// letters draw instead of the placeholder box. That range includes the mock's U+00B7 MIDDLE DOT.
// The settings gear is a vector mask (13x13, 8 teeth) drawn by
// tools/mkicons.py and blended by drawGear, so the sans9 metrics and the tab labels do not move.
// Everything else the faces cannot draw is the screen's dim placeholder box, never the font's
// full-height missing-glyph rectangle (see widgets drawTextLeft / the text wrap).
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
