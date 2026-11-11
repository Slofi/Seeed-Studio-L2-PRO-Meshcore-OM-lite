// SPDX-License-Identifier: MIT
// EST-390: the touch-feedback rule, with the system g++ over the headers the firmware compiles
// (src/ui/press.h and src/ui/press_targets.h plus the real layout arithmetic of every screen).
//
// The ticket's own list, one section each:
//   1. the pressed state machine (arm, stay, slide off, clear) and the one blend arithmetic;
//   2. pressed rect == hit rect for a sample of elements on EVERY screen the new layer has: the tab
//      bar and the status keys, the Nodes rows and its sheet, the selector rows (Chat and Settings),
//      the Settings rows/steps/tabs/back/action/modal, the shared keyboard keys, the emoji cells, the
//      Map chrome (tabs, chip, ghosts, markers, callout) and the RPTR list/page/picker keys;
//   3. the key preview bubble for every key of the sheet: centred, above the finger, inside the screen
//      and never over the composer's draft line (the text being typed);
//   4. consumed presses: an invalid target arms nothing (so nothing is drawn pressed), and the areas
//      that only close a pop-up answer no target;
//   5. the latency log's once-a-minute window;
//   6. the touch-sound default: the quietest short library sound, and the setting OFF (the source
//      check in test_est390.py reads sounds::touchClicks()'s default).
#include <stdio.h>
#include <string.h>

#include "ui/chat_layout.h"
#include "ui/layout.h"
#include "ui/map_chrome.h"
#include "ui/node_menu.h"
#include "ui/press.h"
#include "ui/press_targets.h"
#include "ui/rptr_layout.h"
#include "ui/settings_layout.h"
#include "ui/theme.h"
#include "sound_synth.h"

using namespace om;
using namespace om::ui;

static int g_fail = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL %s\n", what);
    ++g_fail;
  }
}

static bool sameRect(const Rect& a, const Rect& b) {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static bool overlap(const Rect& a, const Rect& b) { return a.overlaps(b); }

// The centre of a rectangle: a point the hit test must attribute to it.
static void centre(const Rect& r, int* x, int* y) {
  *x = r.x + r.w / 2;
  *y = r.y + r.h / 2;
}

// ---- 1. the state machine ----------------------------------------------------------------------
static void testTracker() {
  PressTracker t;
  check(!t.active(), "tracker: starts clear");

  PressTarget key;
  key.kind = PressKind::Key;
  key.rect = Rect{10, 100, 30, 22};
  key.cp = 'k';
  t.arm(key, 1000);
  check(t.active(), "tracker: a valid target arms");
  check(t.target().cp == 'k', "tracker: the character is kept for the bubble");

  check(!t.move(20, 110), "tracker: a sample inside the rect keeps the look");
  check(t.active(), "tracker: still active inside");
  check(t.move(5, 110), "tracker: sliding off clears the look");
  check(!t.active(), "tracker: cleared after the slide-off");
  check(!t.move(5, 110), "tracker: a second outside sample reports nothing new");

  // Back inside after a slide-off does NOT come back: the press was spent by leaving the element.
  check(!t.move(20, 110), "tracker: re-entering the rect does not re-arm");
  check(!t.active(), "tracker: stays clear after re-entering");

  // The edge rule is half-open, exactly like Rect::contains and the hit tests.
  t.arm(key, 2000);
  check(!t.move(10, 100), "tracker: the top-left pixel is inside");
  check(t.move(40, 122), "tracker: the first pixel past the bottom-right clears the look");
  check(!t.active(), "tracker: cleared on the far edge");

  // An invalid target (nothing actionable, or a consumed press) never arms.
  PressTracker u;
  u.arm(PressTarget{}, 10);
  check(!u.active(), "tracker: an invalid target arms nothing");
  PressTarget row;
  row.kind = PressKind::Row;
  row.rect = Rect{};   // empty
  u.arm(row, 10);
  check(!u.active(), "tracker: an empty rect arms nothing");

  // Release is the shell's clear; the tracker itself has no time limit (a long hold keeps the look).
  t.arm(key, 3000);
  t.move(20, 110);
  check(t.active(), "tracker: a long hold keeps the look");
  t.clear();
  check(!t.active(), "tracker: clear() on release");
  check(t.target().kind == PressKind::None, "tracker: clear() forgets the target too");
}

// ---- 2. the blend ------------------------------------------------------------------------------
static void testBlend() {
  const uint16_t acc = kPressedTint;
  const uint16_t bg = kKeyBg;
  check(pressBlend(acc, bg, 0) == bg, "blend: alpha 0 is the background unchanged");
  check(pressBlend(acc, bg, 255) == acc, "blend: alpha 255 is the accent");
  const uint16_t mid = pressBlend(acc, bg, kPressedAlpha);
  check(mid != bg && mid != acc, "blend: the pressed tint is between the two");
  // Every channel moved toward the accent, none past it: a real mix, not a swap.
  const int mR = (mid >> 11) & 0x1F, mG = (mid >> 5) & 0x3F, mB = mid & 0x1F;
  const int aR = (acc >> 11) & 0x1F, aG = (acc >> 5) & 0x3F, aB = acc & 0x1F;
  const int bR = (bg >> 11) & 0x1F, bG = (bg >> 5) & 0x3F, bB = bg & 0x1F;
  check(mR > bR && mR < aR, "blend: red moved from the background toward the accent");
  check(mG > bG && mG < aG, "blend: green moved from the background toward the accent");
  check(mB > bB && mB < aB, "blend: blue moved from the background toward the accent");
  // A tint, not a replacement: the plate has to stay dark enough for the white glyph on it.
  check((mid >> 11 & 0x1F) < 16, "blend: the key plate stays dark under the tint");
  // Symmetric ends and a monotone ramp: more alpha is never darker on any channel.
  int prev = -1;
  for (int a = 0; a <= 255; a += 15) {
    const uint16_t c = pressBlend(acc, bg, (uint8_t)a);
    const int r = (c >> 11) & 0x1F;
    check(r >= prev, "blend: the ramp is monotone");
    prev = r;
  }
}

// ---- 3. the tab bar and the status keys --------------------------------------------------------
static void testChrome() {
  const Layout& L = l2();
  for (int i = 0; i < kTabCount; ++i) {
    const Rect tab = L.tab((Tab)i);
    int x = 0, y = 0;
    centre(tab, &x, &y);
    const PressTarget t = pressTargetForChrome(L, x, y);
    check(t.valid(), "chrome: a tab answers");
    check(t.kind == PressKind::Tab, "chrome: the tab's kind");
    check(sameRect(t.rect, tab), "chrome: the pressed rect IS the tab rect");
    check(t.rect.contains(x, y), "chrome: the pressed rect contains the finger");
  }
  int x = 0, y = 0;
  centre(L.gear(), &x, &y);
  const PressTarget g = pressTargetForChrome(L, x, y);
  check(g.valid() && sameRect(g.rect, L.gear()), "chrome: the gear is its own rect");
  centre(L.radioFault(), &x, &y);
  const PressTarget f = pressTargetForChrome(L, x, y);
  check(f.valid() && sameRect(f.rect, L.radioFault()), "chrome: the fault mark is its own rect");
  // The status bar between the keys and the content are not targets.
  check(!pressTargetForChrome(L, kStatusPadX + 2, L.status.y + 2).valid(),
        "chrome: an inert status-bar cell is no target");
  check(!pressTargetForChrome(L, 160, 120).valid(), "chrome: the middle of the page is no target");
  // Every pixel of the tab bar belongs to exactly one tab (no gap, no double answer).
  int hits = 0;
  for (int px = 0; px < L.width; ++px) {
    if (L.tabAt(px, L.tabbar.y + L.tabbar.h / 2) >= 0) ++hits;
  }
  check(hits == L.width, "chrome: the whole bar is covered by tabs");
}

// ---- 4. the Nodes list and its sheet -----------------------------------------------------------
static void testNodes() {
  const Layout& L = l2();
  const int count = 9;
  for (int scroll = 0; scroll <= L.maxScroll(count); scroll += 30) {
    for (int i = 0; i < count; ++i) {
      const Rect vis = L.rowVisible(i, scroll);
      if (vis.empty()) continue;
      int x = 0, y = 0;
      centre(vis, &x, &y);
      const PressTarget t = pressTargetForNodesRow(L, scroll, count, x, y);
      check(t.valid(), "nodes: a visible row answers");
      check(t.kind == PressKind::Row, "nodes: the row's kind");
      check(sameRect(t.rect, vis), "nodes: the pressed rect IS the visible row slice");
      check(t.rect.contains(x, y), "nodes: the rect contains the finger");
    }
  }
  // The header and the floor under the last row are not rows.
  check(!pressTargetForNodesRow(L, 0, count, 5, L.listHeader().y + 2).valid(),
        "nodes: the header is no row");
  check(!pressTargetForNodesRow(L, 0, count, 5, L.tabbar.y + 2).valid(),
        "nodes: the tab bar is no row");

  // The long-press sheet: every row's own rectangle.
  const int rows = 6;
  const NodeSheetLayout g = nodeSheetLayout(L, rows);
  for (int i = 0; i < rows; ++i) {
    int x = 0, y = 0;
    centre(g.rows[i], &x, &y);
    const PressTarget t = pressTargetForNodeSheetRow(g, x, y);
    check(t.valid() && t.kind == PressKind::Row, "nodes: a sheet row answers");
    check(sameRect(t.rect, g.rows[i]), "nodes: the sheet pressed rect IS the row rect");
  }
  check(!pressTargetForNodeSheetRow(g, g.head.x + 2, g.head.y + 2).valid(),
        "nodes: the sheet header is no target (it only closes)");
}

// ---- 5. the keyboard sheet and the preview bubble ----------------------------------------------
static void testKeyboard() {
  const Layout& L = l2();
  for (int replying = 0; replying < 2; ++replying) {
    for (int nums = 0; nums < 2; ++nums) {
      const ChatLayout g = chatLayoutFor(L, true, replying != 0, nums != 0, false);
      check(!g.draft.empty(), "keyboard: the sheet has a draft line");
      for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < g.keyCount[r]; ++c) {
          const Rect hit = g.keyHit[r][c];
          const Rect drawn = g.keys[r][c];
          int x = 0, y = 0;
          centre(hit, &x, &y);
          const PressTarget t = pressTargetForKeypad(g, nums != 0, x, y);
          check(t.valid(), "keyboard: every key answers");
          check(t.kind == PressKind::Key, "keyboard: the key's kind");
          check(sameRect(t.rect, hit), "keyboard: the pressed rect IS the tap target (keyHit)");
          check(t.rect.contains(x, y), "keyboard: the rect contains the finger");

          // The bubble: centred on the DRAWN key, above it, on the screen, and never over the
          // composer's draft line, for EVERY key of both pages.
          const Rect b = pressBubbleRect(drawn, t.bubbleTopLimit);
          check(b.w == kAccentBubbleW && b.h == kAccentBubbleH, "bubble: the accent bubble's box");
          check(b.x >= 0 && b.right() <= kScreenW, "bubble: inside the screen horizontally");
          check(b.y >= kStatusH, "bubble: never under the status bar");
          const int wantCx = drawn.x + drawn.w / 2;
          const int gotCx = b.x + b.w / 2;
          check(gotCx >= wantCx - 1 && gotCx <= wantCx + 1, "bubble: centred on the drawn key");
          check(b.bottom() <= drawn.y, "bubble: above the key it previews");
          check(!overlap(b, g.draft), "bubble: never covers the composer draft line");
          check(!overlap(b, hit), "bubble: never covers the pressed key itself");

          // Only a character key carries a preview; the modifier keys carry none.
          KeyDef defs[10];
          const int n = keyRow(nums != 0, r, defs);
          check(c < n, "keyboard: the key table agrees with the layout");
          const bool isChar = (c < n && defs[c].kind == KeyKind::Char && defs[c].label[0] != 0);
          check(isChar == (t.cp != 0), "keyboard: the bubble exists exactly for character keys");
          if (isChar) check(t.cp == (uint32_t)(unsigned char)defs[c].label[0],
                            "keyboard: the bubble shows the key's own character");
        }
      }
      // The 2 px gaps between keys and the sheet's header line are no target: nothing is pressed.
      check(!pressTargetForKeypad(g, nums != 0, g.keys[1][0].right() + 1, g.keys[1][0].y + 4).valid(),
            "keyboard: the gap between keys is no target");
      check(!pressTargetForKeypad(g, nums != 0, 5, g.sheet.y + 1).valid(),
            "keyboard: the sheet's header line is no target");
      check(!pressTargetForKeypad(g, nums != 0, 5, g.msgs.y + 2).valid(),
            "keyboard: a press above the sheet (a message behind it) is no target");
    }
  }
  // Shift: the bubble shows the character that will actually be typed.
  const ChatLayout g = chatLayoutFor(L, true, false, false, false);
  for (int c = 0; c < g.keyCount[1]; ++c) {
    KeyDef defs[10];
    keyRow(false, 1, defs);
    if (defs[c].kind != KeyKind::Char || defs[c].label[0] < 'a' || defs[c].label[0] > 'z') continue;
    const Rect hit = g.keyHit[1][c];
    const PressTarget t = pressTargetForKeypad(g, false, hit.x + hit.w / 2, hit.y + 4,
                                               KeyboardField::Chat, true);
    check(t.cp == (uint32_t)(unsigned char)(defs[c].label[0] - 32),
          "keyboard: shift previews the capital that will be typed");
    break;
  }
}

// ---- 6. the dropdown (Chat and Settings use this one) ------------------------------------------
static void testSelector() {
  const Rect list{0, 40, 320, 120};
  const Rect manage{0, 160, 320, kSelManageH};
  for (int i = 0; i < 4; ++i) {
    const int top = selRowTop(list, 2, 2, i, 0);
    if (top < 0 || top + kSelRowH > list.bottom()) continue;
    const int x = 10;
    const int y = top + kSelRowH / 2;
    const PressTarget t = pressTargetForSelectorRow(list, 2, 2, 0, manage, x, y);
    check(t.valid() && t.kind == PressKind::Row, "selector: a chat row answers");
    check(t.rect.x == list.x && t.rect.y == top && t.rect.w == list.w && t.rect.h == kSelRowH,
          "selector: the pressed rect IS the row rect");
    check(t.rect.contains(x, y), "selector: the rect contains the finger");
  }
  const PressTarget m = pressTargetForSelectorRow(list, 2, 2, 0, manage, 10, manage.y + 4);
  check(m.valid() && sameRect(m.rect, manage), "selector: the Manage row is its own rect");
  // The section labels and the pads only close the dropdown: no pressed look behind them.
  const int labelY = list.y + 2;
  check(!pressTargetForSelectorRow(list, 2, 2, 0, manage, 10, labelY).valid(),
        "selector: a section label is no target");
  check(!pressTargetForSelectorRow(list, 2, 2, 0, manage, 10, list.y - 4).valid(),
        "selector: a press above the panel is no target (it only closes)");
}

// ---- 7. Settings --------------------------------------------------------------------------------
static SettingRow g_rows[8];

static void buildSettingsRows() {
  for (int i = 0; i < 8; ++i) g_rows[i] = SettingRow{};
  for (int i = 0; i < 8; ++i) {
    g_rows[i].kind = SetRowKind::Chevron;
    snprintf(g_rows[i].label, sizeof(g_rows[i].label), "Row %d", i);
  }
  g_rows[2].kind = SetRowKind::Stepper;
}

static void testSettings() {
  buildSettingsRows();
  const SettingsLayout L = settingsLayoutFor(kScreenW, kScreenH, false, false, true);
  // Every visible row's own rectangle.
  for (int i = 0; i < 8; ++i) {
    const Rect vis = settingsRowVisible(L, g_rows, 8, i, 0);
    if (vis.empty()) continue;
    // A row's LABEL side: the stepper's value between its two keys is deliberately not a target.
    const int x = vis.x + 10;
    const int y = vis.y + vis.h / 2;
    const PressTarget t = pressTargetForSettings(L, g_rows, 8, 0, x, y);
    check(t.valid(), "settings: a row answers");
    check(t.kind == PressKind::Row, "settings: the row's kind");
    check(sameRect(t.rect, vis), "settings: the pressed rect IS the visible row");
  }
  // The stepper's two keys are their own targets; the value between them is nothing.
  const Rect stepRow = settingsRowVisible(L, g_rows, 8, 2, 0);
  const Rect full = settingsRowRect(L, g_rows, 8, 2, 0);
  const Rect minus = settingsStepMinusRect(full);
  const Rect plus = settingsStepPlusRect(full);
  const PressTarget pm = pressTargetForSettings(L, g_rows, 8, 0, minus.x + 2, minus.y + 2);
  check(pm.valid() && sameRect(pm.rect, minus), "settings: the stepper's minus key");
  const PressTarget pp = pressTargetForSettings(L, g_rows, 8, 0, plus.x + 2, plus.y + 2);
  check(pp.valid() && sameRect(pp.rect, plus), "settings: the stepper's plus key");
  const Rect value = settingsStepValueRect(full);
  const PressTarget pv = pressTargetForSettings(L, g_rows, 8, 0, value.x + 4, value.y + 4);
  check(pv.valid() && pv.kind == PressKind::Row && sameRect(pv.rect, stepRow),
        "settings: the stepper's value and label are the row, the keys are the keys");
  check(stepRow.contains(minus.x + 2, minus.y + 2), "settings: the stepper keys are inside the row");

  // The tab row, Back and the title action.
  for (int i = 0; i < kSettingsTabCount; ++i) {
    int x = 0, y = 0;
    centre(L.tabs[i], &x, &y);
    const PressTarget t = pressTargetForSettings(L, g_rows, 8, 0, x, y);
    check(t.valid() && t.kind == PressKind::Tab && sameRect(t.rect, L.tabs[i]),
          "settings: a tab is its own rect");
  }
  int x = 0, y = 0;
  centre(L.backButton, &x, &y);
  const PressTarget b = pressTargetForSettings(L, g_rows, 8, 0, x, y);
  check(b.valid() && sameRect(b.rect, L.backButton), "settings: Back is its own rect");
  centre(L.titleAction, &x, &y);
  const PressTarget a = pressTargetForSettings(L, g_rows, 8, 0, x, y);
  check(a.valid() && sameRect(a.rect, L.titleAction), "settings: the title action is its own rect");

  // The scroll moves the rows and the pressed rect follows them.
  const int scroll = 20;
  const Rect vis = settingsRowVisible(L, g_rows, 8, 3, scroll);
  const PressTarget t = pressTargetForSettings(L, g_rows, 8, scroll, vis.x + 4, vis.y + 4);
  check(t.valid() && sameRect(t.rect, vis), "settings: the pressed rect follows the scroll");
  // A sub-page has no tab row, so the same pixel is no longer a tab.
  const SettingsLayout sub = settingsLayoutFor(kScreenW, kScreenH, true, false, false);
  check(!pressTargetForSettings(sub, g_rows, 8, 0, 40, kStatusH + 1).valid(),
        "settings: a sub-page has no tab row");
}

static void testModals() {
  const SettingsModalLayout m = settingsModalLayoutFor(kScreenW, kScreenH);
  int x = 0, y = 0;
  centre(m.cancel, &x, &y);
  const PressTarget c = pressTargetForModalKey(false, m.cancel, m.confirm, x, y);
  check(c.valid() && sameRect(c.rect, m.cancel), "modal: Cancel is its own rect");
  centre(m.confirm, &x, &y);
  const PressTarget k = pressTargetForModalKey(true, m.cancel, m.confirm, x, y);
  check(k.valid() && sameRect(k.rect, m.confirm), "modal: Clear is its own rect");
  // Outside the box is the consumed Cancel: no pressed look anywhere.
  check(!pressTargetForModalKey(false, m.cancel, m.confirm, 2, 2).valid(),
        "modal: a press outside the box draws nothing");
  // The RPTR confirmation uses the same two keys.
  const RptrModalLayout r = rptrModalLayoutFor(l2(), "Apply");
  centre(r.confirm, &x, &y);
  const PressTarget rc = pressTargetForModalKey(true, r.cancel, r.confirm, x, y);
  check(rc.valid() && sameRect(rc.rect, r.confirm), "modal: the RPTR confirmation's key");
}

// ---- 8. the Map chrome -------------------------------------------------------------------------
static void testMap() {
  const Layout& L = l2();
  for (int hidden = 0; hidden < 2; ++hidden) {
    const MapChrome c = mapChromeFor(L, false, hidden != 0);
    // The five tabs (or their slivers) and the chip's tap target.
    MapTab tabs[kMapTabCount];
    mapTabRects(c, tabs);
    for (int i = 0; i < kMapTabCount; ++i) {
      int x = 0, y = 0;
      centre(tabs[i].rect, &x, &y);
      const PressTarget t = pressTargetForMap(c, nullptr, 0, x, y);
      check(t.valid(), "map: a tab answers");
      if (!hidden) {
        check(t.kind == PressKind::Tab, "map: the tab's kind");
        check(sameRect(t.rect, tabs[i].rect), "map: the pressed rect IS the tab rect");
      } else {
        // Hidden, a tab's pixels are the hide/show sliver: the chip brings the bars back.
        check(sameRect(t.rect, c.chipTap), "map: hidden, the sliver is the chip's target");
      }
    }
    int x = 0, y = 0;
    centre(c.chipTap, &x, &y);
    const PressTarget chip = pressTargetForMap(c, nullptr, 0, x, y);
    check(chip.valid() && sameRect(chip.rect, c.chipTap), "map: the chip's 30x26 target");
    // The ghost + and - while hidden keep the SHOWN rectangles as their tap zones.
    if (hidden) {
      centre(c.zoomInShown, &x, &y);
      if (!c.chipTap.contains(x, y) && !Rect{c.zoomIn.x, c.zoomIn.y, c.zoomIn.w, c.zoomIn.h}.contains(x, y)) {
        const PressTarget z = pressTargetForMap(c, nullptr, 0, x, y);
        check(z.valid() && sameRect(z.rect, c.zoomInShown), "map: the hidden zoom ghost's zone");
      }
    }
    // The canvas pans: it is no target.
    check(!pressTargetForMap(c, nullptr, 0, c.view.x + 40, c.view.y + 60).valid(),
          "map: the canvas is no target");
  }
  // A marker: the hit test's radius becomes its bounding box, centred on the dot.
  MapMarker mk;
  mk.x = 100;
  mk.y = 120;
  const MapChrome c = mapChromeFor(L, true, false);
  const PressTarget m = pressTargetForMap(c, &mk, 1, mk.x + 3, mk.y + 2);
  check(m.valid(), "map: a marker answers");
  check(m.rect.w == 2 * kMapMarkerHitR && m.rect.h == 2 * kMapMarkerHitR,
        "map: the marker's pressed box is the hit radius");
  check(m.rect.contains(mk.x + 3, mk.y + 2), "map: the marker's box contains the finger");
  // The callout is a button.
  int x = 0, y = 0;
  centre(c.callout, &x, &y);
  const PressTarget co = pressTargetForMap(c, nullptr, 0, x, y);
  check(co.valid() && sameRect(co.rect, c.callout), "map: the callout is its own rect");
}

// ---- 9. RPTR -----------------------------------------------------------------------------------
static void testRptr() {
  const Layout& L = l2();
  // The list: the Scan chip and one row at a few scrolls.
  const RptrListLayout r = rptrListLayoutFor(L);
  int x = 0, y = 0;
  centre(r.scan, &x, &y);
  const PressTarget scan = pressTargetForRptrList(r, 6, 0, x, y);
  check(scan.valid() && sameRect(scan.rect, r.scan), "rptr: the Scan chip is its own rect");
  for (int scroll = 0; scroll <= 60; scroll += 20) {
    for (int i = 0; i < 4; ++i) {
      const RptrRow row = rptrListRow(r, i, scroll);
      if (row.y + row.h <= r.list.y || row.y >= r.list.bottom()) continue;
      const int px = 12;
      int py = row.y + 4;
      if (py < r.list.y + 2) py = r.list.y + 2;   // a partly scrolled row: a point still on it
      if (py >= r.list.bottom()) continue;
      if (py >= row.y + row.h) continue;
      const PressTarget t = pressTargetForRptrList(r, 4, scroll, px, py);
      const Rect want = Rect{r.list.x, row.y, r.list.w, row.h}.intersect(r.list);
      check(t.valid() && t.kind == PressKind::Row, "rptr: a list row answers");
      check(sameRect(t.rect, want), "rptr: the pressed row IS the visible list row");
    }
  }

  // A page: Back, the Apply key and a row's own keys.
  rptr::ScreenOut screen{};
  screen.count = 0;
  screen.hasApply = true;
  const RptrPageLayout a = rptrPageLayoutFor(L, false, false, true, false, false);
  centre(a.back, &x, &y);
  const PressTarget back = pressTargetForRptrPage(a, screen, 0, x, y);
  check(back.valid() && sameRect(back.rect, a.back), "rptr: Back is its own rect");
  centre(a.apply, &x, &y);
  const PressTarget ap = pressTargetForRptrPage(a, screen, 0, x, y);
  check(ap.valid() && sameRect(ap.rect, a.apply), "rptr: Apply is its own rect");

  screen.count = 1;
  snprintf(screen.rows[0].label, sizeof(screen.rows[0].label), "name");
  snprintf(screen.rows[0].key, sizeof(screen.rows[0].key), "x");
  screen.rows[0].arrow = true;
  const RptrRowRects rr = rptrPageRowRects(a, screen.rows[0], 0, 0);
  const PressTarget rb = pressTargetForRptrPage(a, screen, 0, rr.body.x + 4, rr.body.y + 4);
  check(rb.valid() && rb.kind == PressKind::Row, "rptr: a page row's body answers");
  const PressTarget rk = pressTargetForRptrPage(a, screen, 0, rr.keyA.x + 2, rr.keyA.y + 2);
  check(rk.valid() && sameRect(rk.rect, rr.keyA), "rptr: the row's key is its own rect");

  // The map picker's keys; the map itself is no target.
  const RptrMapPickLayout m = rptrMapPickLayoutFor(L, true);
  centre(m.useGps, &x, &y);
  const PressTarget gps = pressTargetForRptrMapPick(m, x, y);
  check(gps.valid() && sameRect(gps.rect, m.useGps), "rptr picker: Use my GPS is its own rect");
  centre(m.apply, &x, &y);
  const PressTarget app = pressTargetForRptrMapPick(m, x, y);
  check(app.valid() && sameRect(app.rect, m.apply), "rptr picker: Apply is its own rect");
  check(!pressTargetForRptrMapPick(m, m.map.x + 20, m.map.y + 60).valid(),
        "rptr picker: the map pans, no pressed look");
}

// ---- 10. the emoji page -------------------------------------------------------------------------
static void testEmoji() {
  const Layout& L = l2();
  const ChatLayout g = chatLayoutFor(L, true, false, false, false);
  const EmojiPadLayout e = emojiPadLayout(L, g, 3, 0, 0, 40);
  for (int i = 0; i < e.count; ++i) {
    const Rect cell = emojiCellRect(e, i);
    if (!e.grid.contains(cell.x + 1, cell.y + 1) || !e.grid.contains(cell.right() - 1, cell.bottom() - 1)) {
      continue;   // a cell below the visible grid: not hittable, so no target either
    }
    int x = 0, y = 0;
    centre(cell, &x, &y);
    const EmojiPadHit h = emojiPadHit(e, x, y);
    check(h.cell == i, "emoji: the cell rect's centre answers that cell");
    const PressTarget t = pressTargetForEmojiCell(cell, 0x1F600);
    check(t.valid() && t.kind == PressKind::Emoji, "emoji: a cell is a target");
    check(sameRect(t.rect, cell), "emoji: the pressed rect IS the cell rect");
  }
  const PressTarget bad = pressTargetForEmojiCell(Rect{}, 0x1F600);
  check(!bad.valid(), "emoji: no rect, no target");
}

// ---- 11. the latency log -----------------------------------------------------------------------
static void testLatencyLog() {
  PressLatencyLog l;
  uint32_t worst = 0, count = 0;
  check(!l.tick(1000, &worst, &count), "latency: the first tick starts the window, prints nothing");
  l.sample(20000);
  l.sample(45000);
  check(!l.tick(30000, &worst, &count), "latency: inside the minute, nothing is printed");
  check(!l.tick(60999, &worst, &count), "latency: one ms before the window ends, nothing");
  check(l.tick(61000, &worst, &count), "latency: at 60 s the window reports once");
  check(worst == 45000, "latency: the WORST sample, not the last");
  check(count == 2, "latency: the press count of the window");
  check(!l.tick(61001, &worst, &count), "latency: only one report per window");
  // An empty window says nothing (no misleading 0 ms line).
  check(!l.tick(122000, &worst, &count), "latency: an empty window prints nothing");
  // millis() wraps: the window arithmetic is unsigned.
  PressLatencyLog w;
  check(!w.tick(0xFFFFFF00u, &worst, &count), "latency: wrap, the window starts");
  w.sample(30000);
  check(!w.tick(0xFFFFFFF0u, &worst, &count), "latency: wrap, still inside the window");
  check(!w.tick(0x0000E95Fu, &worst, &count), "latency: wrap, one ms before the window ends");
  check(w.tick(0x0000E960u, &worst, &count), "latency: wrap, the window closes on time");
  check(worst == 30000, "latency: the worst survives the wrap");
}

// ---- 12. the touch sound -----------------------------------------------------------------------
static void testTouchSound() {
  // The quietest short sound in the library is the one the Touch event defaults to: 80 ms, one soft
  // note, and the lowest note level of any Solo sound.
  const uint8_t id = sound::defaultSound(sound::Event::Touch);
  check(id == (uint8_t)56, "sound: the touch default is library id 56 (Soft Tick)");
  check(strcmp(sound::soundName(id), "Soft Tick") == 0, "sound: its name");
  check(sound::soundMs(id) == 80, "sound: 80 ms, short");
  check(sound::soundNoteCount(id) == 1, "sound: one note");
  sound::NoteInfo note;
  check(sound::soundNote(id, 0, &note) && note.hz == 1175, "sound: one soft 1175 Hz tick");
  // It really is the quietest short one: no other sound at 120 ms or less renders quieter.
  int16_t buf[sound::kMaxSamples];
  sound::RenderInfo ri;
  const int n = sound::renderSound(id, buf, sound::kMaxSamples, &ri);
  check(n > 0 && ri.peak > 0, "sound: the touch tick renders");
  const int touchPeak = ri.peak;
  for (int i = 0; i < sound::libraryCount(); ++i) {
    const uint8_t other = sound::libraryId(i);
    if (other == id || sound::soundMs(other) > 120) continue;
    const int m = sound::renderSound(other, buf, sound::kMaxSamples, &ri);
    if (m <= 0) continue;
    check(ri.peak >= touchPeak, "sound: no short sound is quieter than the touch tick");
  }
  // The event key stays the USB word it always was.
  check(strcmp(sound::eventKey(sound::Event::Touch), "touch") == 0, "sound: the event key is stable");
}

int main() {
  testTracker();
  testBlend();
  testChrome();
  testNodes();
  testKeyboard();
  testSelector();
  testSettings();
  testModals();
  testMap();
  testRptr();
  testEmoji();
  testLatencyLog();
  testTouchSound();
  if (g_fail == 0) printf("test_est390: all checks passed\n");
  return g_fail == 0 ? 0 : 1;
}
