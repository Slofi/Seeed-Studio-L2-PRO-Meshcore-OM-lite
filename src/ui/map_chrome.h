// SPDX-License-Identifier: MIT
// Map chrome (EST-351, reworked EST-368, EST-370, EST-372, EST-373 and EST-374): the rectangles of the
// zoom / locate / traces / names tabs, the 22 px hide/show chip and its chevron, the plain zoom label,
// the tile attribution, the centre cross and the tapped-marker callout, the tucked bottom bar, and the
// hit test that uses those same rectangles.
// The distance scale is gone (Filip, EST-368: "remove the distance markers, they are quite useless").
// Plain C++ (no LovyanGFX, no Arduino) so test/host can prove it with the system g++.
//
// The tiles are still map.cpp's job. EST-374: the canvas runs from the status bar's bottom edge to the
// screen's bottom edge (320 x 214), in both states, and every piece of chrome here is drawn OVER it.
#pragma once

#include <stdint.h>
#include <stdio.h>

#include "layout.h"
#include "theme.h"

namespace om {
namespace ui {

constexpr int kMapMaxMarkers = 32;
constexpr int16_t kMapMargin = 6;
constexpr int16_t kMapCalloutH = 40;
constexpr int16_t kMapMarkerR = 4;
constexpr int16_t kMapMarkerHitR = 11;

// EST-372 (Filip ~00:00): the right-hand keys are tabs out of the screen edge. They run to x == the
// screen width (no margin, no right border; the drawing rounds the left corners only).
// EST-373 (Filip 2026-11-11 ~00:38: "the point of making them tabs is so they could be shortened"):
// the width is no longer a guess. It is the WIDEST thing drawn on a tab plus 4 px on each side. The
// widest is the Follow lock: a 5 px radius circle with a 5 px arm on each side, 17 px across (the
// Traces and Names masks are 13 x 13, the + and - glyphs measure 12 and 5 px of ink in SansTitle).
// 17 + 4 + 4 = 25 px, so the tabs start at x 295 and the map gains the 19 px the old 44 px column
// took. The height is computed from the layout and never baked in. EST-374: the chip's 26 px tap
// target above the top tab is what makes it 31 px instead of 32 (see kMapChipTapH).
constexpr int16_t kMapTabIconPad = 4;   // 4 px each side of the widest icon (the ticket's number)
constexpr int16_t kMapTabGap = 1;

// The Follow icon (screen_map.cpp draws it with primitives), the widest thing on the column: the
// circle's radius, the length of the four arms and where an arm starts, in px from the centre.
constexpr int16_t kMapLockR = 5;
constexpr int16_t kMapLockArmLen = 5;
constexpr int16_t kMapLockArmOut = 8;                    // arm's outer end, px from the centre
constexpr int16_t kMapLockW = 2 * kMapLockArmOut + 1;    // 17 px across
constexpr int16_t kMapTabIconW = kMapLockW;              // the widest icon drawn on the column
constexpr int16_t kMapTabW = kMapTabIconW + 2 * kMapTabIconPad;   // 25

// EST-373: hidden, each tab keeps this much of itself at the right edge, and nothing else.
// EST-374 (Filip): 3 px, one less than before; the tab and the bottom bar tuck together.
constexpr int16_t kMapTabSliverW = 3;

constexpr int16_t kMapZoomLabelH = 11;   // "z13" in mono 10 px
constexpr int16_t kMapZoomLabelW = 21;   // three 6 px glyphs plus 3 px of air
constexpr int16_t kMapZoomLabelGap = 2;  // chip -> label, chip target -> top tab

// EST-374 (Filip 2026-11-11: "a small chip with an arrow"): the hide/show key is a 22 x 22 rounded
// chip (kPanel fill, kLine edge) at the top right, flush with the tabs' right edge and starting on
// the status bar's bottom row. Inside it a chevron in kInk points right (">", the bars are out) or
// left ("<", they are tucked).
//
// The tap target is 30 x 26 px and must never overlap the top tab's zone. The status bar above and
// the target's own 26 px height leave the top tab starting at 54 (the 2 px gap under the target), so
// five equal tabs in the 215 px band are 31 px each, not 32. That 1 px is the price of the ticket's
// 26 px minimum without a zone that fights the top tab, and it is the only number this ticket takes
// away from EST-373.
constexpr int16_t kMapChipSize = 22;
constexpr int16_t kMapChipGap = kMapZoomLabelGap;
constexpr int16_t kMapChipTapW = 30;
constexpr int16_t kMapChipTapH = 26;
constexpr int16_t kMapChevronW = 4;         // the chevron's box inside the chip
constexpr int16_t kMapChevronH = 7;
constexpr int16_t kMapChevronStroke = 2;    // 2 px: the ticket's "1.5-2 px"
// EST-374: the unread hint while the bars are hidden (the bottom bar's badge is not visible then).
// A 6 px gold dot on the chip's top-left corner. Its top row is the map area's first row, so the
// status bar (drawn last) never eats it.
constexpr int16_t kMapUnreadDotD = 6;

// EST-373: the centre cross. TWO 1 px lines, 7 px long each (arm 3), in ONE dark ink (kMapCrossInk)
// and no outline under them: on a light topo tile the dark line reads on its own, and the old white
// kInk without its 1 px kMapBg plate would be invisible. It is still a cross, never a crosshair: the
// Position picker keeps its own crosshair and does not draw this.
constexpr int16_t kMapCentreCrossArm = 3;   // 2 * 3 + 1 = 7 px

// EST-370: the tile canvas map.cpp renders into, and the point at its centre. mapRender() puts the
// view's geographic centre at this pixel, mapProject() and mapScreenToLatLon() use the same point,
// and the chrome draws its cross there. One source for the zoom focus, the cross and the picker.
//
// EST-374 (Filip): the canvas is the FULL map area, from the status bar's bottom edge to the screen
// floor, in BOTH states, so hiding the bars never moves the centre (and never needs a tile render:
// the chrome is repainted over the cached canvas). kMapCanvasH is unchanged at 214 (240 - 26); what
// changes is where those 214 rows sit. The old canvas was pushed at y 0, so 26 of its rows were
// hidden under the status bar and the bottom row of the map band was a row short of the tab bar.
constexpr int16_t kMapCanvasW = kScreenW;                     // 320
constexpr int16_t kMapCanvasTop = kStatusH;                   // 26, under the status bar
constexpr int16_t kMapCanvasH = (int16_t)(kScreenH - kMapCanvasTop);   // 214
constexpr int16_t kMapCentreX = (int16_t)(kMapCanvasW / 2);            // 160
constexpr int16_t kMapCentreY = (int16_t)(kMapCanvasTop + kMapCanvasH / 2);   // 133

// EST-374 (Filip): the tucked bottom bar. Shown it is the real 25 px page bar; hidden the SAME bar
// slides down by kBarSlideDy and only its top 3 px are still on the screen, in kPanel, with the Map
// tab's gold segment showing: it reads as "tucked under". The strip takes no taps (the map does).
constexpr int16_t kMapBarStripH = 3;
constexpr int16_t kMapTabSlideDx = (int16_t)(kMapTabW - kMapTabSliverW);   // 22: how far a tab rides
constexpr int16_t kMapBarSlideDy = (int16_t)(kTabH - kMapBarStripH);       // 22: how far the bar drops

// EST-374: the motion. The two groups slide together (the tabs right, the bar down) in this many
// FRAMES: 3 interpolated steps plus the steady frame. 22 px over 4 steps is 5, 11 and 16 px.
// The estimate from the EST-371 numbers is a 19.8 ms canvas repush + an 11.5 ms panel push per frame
// (31 ms, under the ticket's 35 ms limit), so 4 frames is about 125 ms inside the 120-150 ms window.
// Set it to 1 for the instant switch; the log line says which one ran.
constexpr int kMapSlideSteps = 4;
// Both groups move by the same 22 px (25 -> 3 for a tab, 25 -> 3 for the bar), which is what lets
// one number drive the slide. The test asserts this too; the assert makes a later change to either
// width stop the build instead of making the two groups drift apart on the glass.
static_assert(kMapTabSlideDx == kMapBarSlideDy, "both groups must slide by the same 22 px");

struct MapPoint {
  int16_t x = 0;
  int16_t y = 0;
};

// One projected contact marker. `type` is MeshCore's ADV_TYPE_* (1 client, 2 repeater, 3 room).
struct MapMarker {
  int16_t x = 0;
  int16_t y = 0;
  uint8_t type = 0;
  const char* name = "";
  bool selected = false;
  bool self = false;   // our own position: the white dot with the gold halo
};

struct MapChrome {
  Rect status;      // the status bar sits over the map and is never a map tap
  Rect view;        // 0,26,320,214: the full canvas, the tiles' area and the touch area that pans
  Rect chip;        // EST-374: the 22x22 hide/show chip, flush with the tabs' right edge
  Rect chipTap;     // EST-374: its 30x26 target (above the top tab, never on it)
  Rect chevron;     // EST-374: the chevron's box inside the chip
  Rect zoomLabel;   // EST-372: plain "z13", immediately left of the chip
  Rect zoomIn;      // the tab rectangles THIS state draws: 25 px shown, 3 px (a sliver) hidden
  Rect zoomOut;
  Rect lock;
  Rect traces;      // EST-368: the Traces toggle, above the locate tab
  Rect names;       // EST-370: the Names toggle, the top tab of the right column
  Rect zoomInShown;   // EST-374: the SHOWN-state zoom tabs; while hidden they are the ghost + / -
  Rect zoomOutShown;  // tap zones (same rect, only the colours change)
  Rect barStrip;    // EST-374: the tucked bottom bar's 3 px strip at the screen floor
  Rect callout;     // the tapped marker's panel; empty when nothing is selected
  int16_t creditBottom = 0;   // the lowest row the attribution may use (the bar, or the strip)
  bool tabsHidden = false;    // EST-373: the saved hide/show state the rects above were built for
};

// All rectangles from the screen size. `selected` adds the callout; it sits over the bottom of the
// map like the mock's .callout. The tabs stack upward from the shown bar's top edge: zoom out, zoom
// in, follow, Traces, Names; every tab's right edge IS the screen edge. `tabsHidden` gives each tab
// its 3 px sliver at that same edge: the tab rectangle and the tap zone move together, never one
// alone, and the ghost zoom zones keep the shown rectangles.
MapChrome mapChromeFor(const Layout& L, bool selected, bool tabsHidden = false);

// EST-370: the map's centre point, derived from the same view rectangle every other piece of chrome
// uses. For the L2 it is (160, 133): the centre of the full canvas, the same in both states.
constexpr MapPoint mapCentreOf(const MapChrome& c) {
  return MapPoint{(int16_t)(c.view.x + c.view.w / 2), (int16_t)(c.view.y + c.view.h / 2)};
}

// The cross's 7x7 box (the screen draws the two 1 px dark lines inside it, nothing else).
constexpr Rect mapCentreCrossRect(const MapChrome& c) {
  const MapPoint p = mapCentreOf(c);
  return Rect{(int16_t)(p.x - kMapCentreCrossArm), (int16_t)(p.y - kMapCentreCrossArm),
              (int16_t)(2 * kMapCentreCrossArm + 1), (int16_t)(2 * kMapCentreCrossArm + 1)};
}

// EST-374: the unread dot's box on the chip's top-left corner, and the one rule that decides it: only
// while the bars are hidden (shown, the bottom bar's own badge is visible) and only when a chat has
// something unread. The screen draws a kAcc circle in this box.
constexpr Rect mapChipUnreadDotRect(const MapChrome& c) {
  return Rect{c.chip.x, c.chip.y, kMapUnreadDotD, kMapUnreadDotD};
}
constexpr bool mapChipShowsUnread(bool tabsHidden, int unread) {
  return tabsHidden && unread > 0;
}

// EST-374: the tucked bottom bar, and why it can never leak off the Map. The state is the saved
// `tabsHidden` key, but the tuck belongs to THIS screen: every other screen draws its own full bar
// (they still call drawTabBar), so a pop-up action, a notification, a USB tap or any page switch
// from code shows the bar on the next screen. Inside the Map it is ONE decision for both groups.
constexpr bool mapBarsTucked(bool tabsHidden, bool onMapScreen) { return tabsHidden && onMapScreen; }

// EST-368 (Filip ~21:35), EST-370 (~23:40: "no outline on the credits!"): the tile attribution is
// plain small dim text drawn straight on the map, no plate and no outline, on the bottom LEFT where
// the removed scale used to sit. `width` is the font's, measured by the caller; the screen draws it
// inside the returned box. EST-374: the box's floor is the lowest clear row, so the line stays
// visible above the shown bar and above the tucked strip.
constexpr int16_t kMapCreditH = 10;
constexpr const char* kMapCreditText = "Esri, HERE, Garmin, OSM";
Rect mapCreditRect(const MapChrome& c, int16_t width);

enum class MapHitKind : uint8_t {
  None = 0,
  ZoomIn,
  ZoomOut,
  Lock,
  Traces,      // EST-368: the Traces toggle
  Names,       // EST-370: the Names toggle
  TabsToggle,  // EST-373/374: the chip, and a hidden tab's 3 px sliver (brings them back)
  Marker,
  Map,
  Callout
};

// One tab: its rectangle and the hit kind that rectangle answers. The screen draws THIS rect and the
// hit test reads THIS rect, so the two can never drift apart (test_est373 proves it).
struct MapTab {
  MapHitKind kind = MapHitKind::None;
  Rect rect;
};

constexpr int kMapTabCount = 5;

// The five tabs, bottom up: zoom out, zoom in, follow, Traces, Names. Every one is out of the same
// MapChrome the screen draws, including the 3 px slivers while the tabs are hidden.
void mapTabRects(const MapChrome& c, MapTab out[kMapTabCount]);

struct MapHit {
  MapHitKind kind = MapHitKind::None;
  int marker = -1;   // valid when kind == Marker
};

// The tab or marker under a point. A marker wins over the map; the callout's own box is not a map
// tap; the chip is the hide/show key; every toggle is a tab like the zoom ones. `count` markers in
// `markers`. While the bars are hidden: their 3 px slivers are the only tab pixels and a tap on one
// brings both groups back, and the zoom tabs leave their ghost + / - behind at the SHOWN rectangles
// (minus those sliver pixels, which belong to the hide/show key) so zooming still works hidden.
MapHit mapHitAt(const MapChrome& c, const MapMarker* markers, int count, int x, int y);

}  // namespace ui
}  // namespace om
