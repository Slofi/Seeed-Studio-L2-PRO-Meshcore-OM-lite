// SPDX-License-Identifier: MIT
// EST-399: the four logic halves of the UI fix ticket, with the system g++ over the headers the
// firmware compiles.
//
//   1. ui/delete_arm.h: one "tap again" arm PER ROW. Arm A, tap B -> B is armed and NOT forgotten;
//      the same row twice inside the window is the confirmation; the window expires; the arm is
//      cleared on another tap / on leaving the page; and a stale arm can never light a label.
//   2. ui/press_targets.h + ui/rptr_layout.cpp: the pressed rectangle is the rectangle the hit test
//      accepted, at EVERY scroll. A sweep over the scrolling lists (the RPTR page's card refresh key
//      and banner button, its rows, the RPTR list, the Nodes list, the Settings rows and the two
//      selectors) proves the invariant the EST-398 #2 bug broke: the rectangle that lights up
//      CONTAINS the finger that lit it.
//   3. ui/emoji_persist.h: the Recent row's NVS write happens on leaving the emoji page or after 30 s
//      of no change, a failure keeps the list dirty, is counted, and is retried no sooner than 5 s.
//   4. the deferred-write arithmetic survives the millis() wrap (both of the above).
#include <stdio.h>
#include <string.h>

#include "ui/delete_arm.h"
#include "ui/emoji_persist.h"
#include "ui/layout.h"
#include "ui/press_targets.h"
#include "ui/rptr_layout.h"
#include "ui/theme.h"

using namespace om;
using namespace om::ui;

static int g_fail = 0;
static int g_bugCase = 0;   // how many taps the OLD plate would have lit in the wrong place

static void check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL %s\n", what);
    ++g_fail;
  }
}

static bool sameRect(const Rect& a, const Rect& b) {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// The centre of a rectangle: a point the hit test must attribute to it.
static void centre(const Rect& r, int* x, int* y) {
  *x = r.x + r.w / 2;
  *y = r.y + r.h / 2;
}

// ---- 1. one arm per row ------------------------------------------------------------------------
static void testDeleteArm() {
  DeleteArm arm;
  check(arm.armedRow(1000) == -1, "delete arm: nothing armed at the start");

  // The EST-398 #1 sequence: arm network A, then tap network B.
  check(arm.tap(0, 1000) == DeleteArm::Tap::Armed, "delete arm: the first tap arms A");
  check(arm.isArmed(0, 1001), "delete arm: A is the armed row");
  check(arm.tap(1, 1500) == DeleteArm::Tap::Armed,
        "delete arm: a tap on B ARMS B, it does not confirm anything");
  check(!arm.isArmed(0, 1501), "delete arm: arming B disarms A");
  check(arm.isArmed(1, 1501), "delete arm: B is the armed row after one tap");
  check(arm.armedRow(1501) == 1, "delete arm: the armed row is B's index");

  // The second tap on the SAME row is the confirmation, and it disarms.
  check(arm.tap(1, 2000) == DeleteArm::Tap::Confirmed, "delete arm: the second tap on B confirms");
  check(arm.armedRow(2000) == -1, "delete arm: a confirmation disarms the row");

  // A third tap starts over (never two destructive taps in a row).
  check(arm.tap(1, 2100) == DeleteArm::Tap::Armed, "delete arm: after confirming, a tap arms again");

  // The window: 5999 ms after arming still confirms, 6000 ms does not.
  DeleteArm w;
  w.tap(2, 10000);
  check(w.armedRow(10000 + kDeleteArmMs - 1) == 2, "delete arm: armed one ms before the window ends");
  check(w.isArmed(2, 10000 + kDeleteArmMs - 1), "delete arm: the same row still confirms at 5999 ms");
  check(w.armedRow(10000 + kDeleteArmMs) == -1, "delete arm: the window ends at exactly 6000 ms");
  check(!w.isArmed(2, 10000 + kDeleteArmMs), "delete arm: no confirmation after the window");
  check(w.tap(2, 10000 + kDeleteArmMs) == DeleteArm::Tap::Armed,
        "delete arm: a late tap only re-arms (safety first)");
  check(w.index == 2, "delete arm: the late tap armed the row again");

  // clear(): what leaving the page does.
  w.clear();
  check(w.index == -1 && w.armedRow(20000) == -1, "delete arm: clear() disarms");
  check(w.tap(3, 20001) == DeleteArm::Tap::Armed, "delete arm: after clear() the next tap arms");

  // The millis() wrap: armed just before the wrap, tapped just after it.
  DeleteArm wr;
  const uint32_t before = 0xFFFFFF00u;
  wr.tap(5, before);
  check(wr.isArmed(5, (uint32_t)(before + 1000)), "delete arm: the window survives the millis wrap");
  check(!wr.isArmed(5, (uint32_t)(before + kDeleteArmMs)),
        "delete arm: the wrapped window still ends at 6000 ms");
  // Two arms are two arms: one expiring says nothing about the other.
  DeleteArm a, b;
  a.tap(0, 1000);
  b.tap(7, 1000);
  a.clear();
  check(b.isArmed(7, 1001), "delete arm: clearing one arm leaves the other armed");
}

// ---- 2. the pressed rectangle is the hit rectangle, at every scroll -----------------------------
static void pressMustContain(const PressTarget& t, int x, int y, const char* what) {
  if (!t.valid()) {
    printf("FAIL %s: no pressed target at %d,%d\n", what, x, y);
    ++g_fail;
    return;
  }
  if (!t.rect.contains(x, y)) {
    printf("FAIL %s: the pressed rect (%d,%d %dx%d) does not contain the finger %d,%d\n", what, t.rect.x,
           t.rect.y, t.rect.w, t.rect.h, x, y);
    ++g_fail;
  }
}

// EST-398 #2: the card's refresh key and the banner's button at every scroll the page can have.
static void testRptrPageScrolled() {
  const Layout& L = l2();
  const RptrPageLayout a = rptrPageLayoutFor(L, true, true, true, false, false);
  check(!a.refreshTap.empty() && !a.bannerButton.empty(), "rptr page: the card and the banner exist");
  const int maxScroll = rptrPageMaxScroll(a, 6);

  for (int scroll = 0; scroll <= maxScroll; scroll += 3) {
    // The refresh key: the visible slice of its scrolled rectangle.
    const Rect vis = rptrPageScrolledRect(a.refreshTap, scroll, a.body);
    // The finger goes on the slice's TOP edge on purpose: that is the pixel row the old scroll-0
    // plate did not cover, so the sweep has a real chance of seeing the EST-398 #2 bug.
    const int x = vis.x + 2;
    const int y = vis.y;
    if (vis.empty() || x >= vis.right() || y >= vis.bottom()) continue;   // scrolled out of the viewport
    const RptrHitResult hit = rptrPageHitAt(a, rptr::ScreenOut{}, scroll, x, y);
    check(hit.kind == RptrHit::Refresh, "rptr page: the scrolled refresh key still answers Refresh");
    const PressTarget t = pressTargetForRptrPage(a, rptr::ScreenOut{}, scroll, x, y);
    check(t.valid() && t.kind == PressKind::Button, "rptr page: the refresh key is a button target");
    pressMustContain(t, x, y, "rptr page refresh key");
    check(sameRect(t.rect, vis), "rptr page: the refresh plate IS the scrolled slice the hit test used");
    // The EST-398 #2 bug, reproduced in one line: the OLD plate was `a.refreshTap` unscrolled, so on
    // a scrolled page it does NOT contain the finger. If this ever stops being true, the two
    // rectangles have become indistinguishable and the assertions above would be vacuous.
    if (y < a.refreshTap.y) {
      check(!a.refreshTap.contains(x, y),
            "rptr page: the old scroll-0 plate does not contain the finger (the bug)");
      ++g_bugCase;
    }

    // The banner's button, the same way.
    const Rect bvis = rptrPageScrolledRect(a.bannerButton, scroll, a.body);
    const int bx = bvis.x + 2;
    const int by = bvis.y + 2;
    if (bvis.empty() || bx >= bvis.right() || by >= bvis.bottom()) continue;
    const RptrHitResult bhit = rptrPageHitAt(a, rptr::ScreenOut{}, scroll, bx, by);
    check(bhit.kind == RptrHit::BannerButton, "rptr page: the scrolled banner button answers");
    const PressTarget bt = pressTargetForRptrPage(a, rptr::ScreenOut{}, scroll, bx, by);
    pressMustContain(bt, bx, by, "rptr page banner button");
    check(sameRect(bt.rect, bvis), "rptr page: the banner plate IS the scrolled slice");

    // The BACK key does NOT scroll: it must keep its rect at every scroll (the header is fixed).
    int hx = 0, hy = 0;
    centre(a.back, &hx, &hy);
    const PressTarget bt2 = pressTargetForRptrPage(a, rptr::ScreenOut{}, scroll, hx, hy);
    check(bt2.valid() && sameRect(bt2.rect, a.back), "rptr page: Back stays put while the column scrolls");
  }

  // The scroll-0 case is what the old code painted: it is the rect the plate must NOT use once the
  // page is scrolled, so the scroll-0 plate and the scrolled plate must differ for a moved scroll.
  const Rect at0 = rptrPageScrolledRect(a.refreshTap, 0, a.body);
  const Rect at30 = rptrPageScrolledRect(a.refreshTap, 30, a.body);
  check(!sameRect(at0, at30), "rptr page: scrolling moves the refresh plate (the bug is possible)");
  check(at30.y == (int16_t)(at0.y - 30) || at30.y == a.body.y,
        "rptr page: the plate moves by the scroll, or is cut at the viewport's top edge");
  check(at30.y < at0.y, "rptr page: the plate moves UP with the content");

  // One row of the page at a moved scroll: the pressed body is the visible slice.
  rptr::ScreenOut screen{};
  screen.count = 1;
  snprintf(screen.rows[0].label, sizeof(screen.rows[0].label), "row");
  const int rowScroll = a.rowsY - a.body.y + 10;   // push row 0 up by 10 px inside the viewport
  const RptrRowRects rr = rptrPageRowRects(a, screen.rows[0], 0, rowScroll);
  const Rect rvis = rr.body.intersect(a.body);
  const PressTarget rt = pressTargetForRptrPage(a, screen, rowScroll, rvis.x + 4, rvis.y + 4);
  pressMustContain(rt, rvis.x + 4, rvis.y + 4, "rptr page row at a moved scroll");
}

// Every other scrolling list: a grid of taps, the pressed rect must contain the finger.
static void testOtherScrolledLists() {
  const Layout& L = l2();

  // Nodes list.
  for (int scroll = 0; scroll <= L.maxScroll(12); scroll += 17) {
    for (int i = 0; i < 12; ++i) {
      const Rect vis = L.rowVisible(i, scroll);
      if (vis.empty()) continue;
      const int x = vis.x + 5;
      const int y = vis.y + vis.h / 2;
      if (!vis.contains(x, y)) continue;
      const PressTarget t = pressTargetForNodesRow(L, scroll, 12, x, y);
      pressMustContain(t, x, y, "nodes row");
      check(sameRect(t.rect, vis), "nodes: the pressed rect is the visible slice");
    }
  }

  // RPTR list.
  const RptrListLayout r = rptrListLayoutFor(L);
  for (int scroll = 0; scroll <= rptrListMaxScroll(r, 8); scroll += 13) {
    for (int i = 0; i < 8; ++i) {
      const RptrRow row = rptrListRow(r, i, scroll);
      const Rect vis = Rect{r.list.x, row.y, r.list.w, row.h}.intersect(r.list);
      if (vis.empty()) continue;
      const int x = vis.x + 5;
      const int y = vis.y + 2;
      if (!vis.contains(x, y)) continue;
      const PressTarget t = pressTargetForRptrList(r, 8, scroll, x, y);
      pressMustContain(t, x, y, "rptr list row");
    }
  }

  // Settings rows (and the stepper keys inside them).
  SettingRow rows[6];
  for (int i = 0; i < 6; ++i) {
    rows[i].kind = SetRowKind::Button;
    snprintf(rows[i].label, sizeof(rows[i].label), "row %d", i);
  }
  rows[2].kind = SetRowKind::Stepper;
  const SettingsLayout sl = settingsLayoutFor(L.width, L.height, true, false, false);
  for (int scroll = 0; scroll <= 60; scroll += 11) {
    for (int i = 0; i < 6; ++i) {
      const Rect vis = settingsRowVisible(sl, rows, 6, i, scroll);
      if (vis.empty()) continue;
      const int x = vis.x + 8;
      const int y = vis.y + 4;
      if (!vis.contains(x, y)) continue;
      const PressTarget t = pressTargetForSettings(sl, rows, 6, scroll, x, y);
      pressMustContain(t, x, y, "settings row");
    }
    // The stepper's own keys.
    const Rect full = settingsRowRect(sl, rows, 6, 2, scroll);
    const Rect minus = settingsStepMinusRect(full);
    const Rect plus = settingsStepPlusRect(full);
    const Rect keys[2] = {minus, plus};
    for (const Rect& key : keys) {
      const int x = key.x + 2;
      const int y = key.y + 2;
      if (!sl.body.contains(x, y)) continue;
      const PressTarget t = pressTargetForSettings(sl, rows, 6, scroll, x, y);
      pressMustContain(t, x, y, "settings stepper key");
    }
  }

  // The chat/Settings dropdown selector, scrolled.
  Rect list{0, 60, 300, 120};
  for (int scroll = 0; scroll <= kSelRowH * 4; scroll += 9) {
    for (int i = 0; i < 8; ++i) {
      const int top = selRowTop(list, 5, 3, i, scroll);
      const Rect row{list.x, (int16_t)top, list.w, kSelRowH};
      const Rect vis = row.intersect(list);
      if (vis.empty()) continue;
      const int x = vis.x + 6;
      const int y = vis.y + 3;
      if (!vis.contains(x, y)) continue;
      const PressTarget t = pressTargetForSelectorRow(list, 5, 3, scroll, Rect{}, x, y);
      pressMustContain(t, x, y, "selector row");
    }
  }
}

// ---- 3. the emoji Recent row's batched NVS write ------------------------------------------------
static void testEmojiGate() {
  EmojiSaveGate g;
  check(!g.due(false, 1000), "emoji gate: nothing dirty, nothing written");
  check(!g.pending(), "emoji gate: starts clean");

  g.changed(1000);
  check(g.pending(), "emoji gate: a pick marks the list dirty");
  check(!g.due(true, 1000), "emoji gate: not due while the page is showing and nothing has settled");
  check(!g.due(true, 1000 + kEmojiSaveQuietMs - 1), "emoji gate: 29999 ms of quiet is not enough");
  check(g.due(true, 1000 + kEmojiSaveQuietMs), "emoji gate: 30 s of quiet is due");

  // Six picks in a row: six marks, one write.
  EmojiSaveGate six;
  for (int i = 0; i < 6; ++i) {
    six.changed(2000 + (uint32_t)i * 500);
    check(!six.due(true, 2000 + (uint32_t)i * 500), "emoji gate: a pick during the window writes nothing");
  }
  check(!six.due(true, 2000 + 5 * 500 + kEmojiSaveQuietMs - 1),
        "emoji gate: the quiet window restarts on every pick");
  check(six.due(true, 2000 + 5 * 500 + kEmojiSaveQuietMs), "emoji gate: due after the last pick + 30 s");

  // Leaving the page writes at once, whatever the clock says.
  check(six.due(false, 2001), "emoji gate: leaving the emoji page writes at once");

  // A write that lands clears the flag and is counted once.
  EmojiSaveGate ok;
  ok.changed(1000);
  check(ok.due(false, 1000), "emoji gate: leaving is due");
  ok.result(true, 1000);
  check(!ok.pending(), "emoji gate: a successful write clears the flag");
  check(ok.writes == 1 && ok.fails == 0, "emoji gate: the write is counted");
  check(!ok.due(false, 1001), "emoji gate: a clean gate writes nothing, even off the page");

  // A write that fails keeps the list dirty, counts the failure and waits 5 s.
  EmojiSaveGate bad;
  bad.changed(1000);
  bad.result(false, 1000);
  check(bad.pending(), "emoji gate: a failed write keeps the list dirty");
  check(bad.fails == 1 && bad.writes == 0, "emoji gate: the failure is counted");
  check(!bad.due(false, 1000 + kEmojiSaveRetryMs - 1), "emoji gate: a retry waits 4999 ms");
  check(bad.due(false, 1000 + kEmojiSaveRetryMs), "emoji gate: the retry goes out at 5 s");
  bad.result(false, 1000 + kEmojiSaveRetryMs);
  check(bad.fails == 2, "emoji gate: the second failure is counted too");
  bad.result(true, 20000);
  check(!bad.pending() && bad.fails == 2 && bad.writes == 1,
        "emoji gate: a later success clears it and keeps both counts");

  // The millis() wrap.
  EmojiSaveGate wr;
  const uint32_t before = 0xFFFFF000u;
  wr.changed(before);
  check(wr.due(false, (uint32_t)(before + 10)), "emoji gate: leaving is due across the wrap");
  wr.result(false, before);
  check(!wr.due(false, (uint32_t)(before + 100)), "emoji gate: the retry window survives the wrap");
  check(wr.due(false, (uint32_t)(before + kEmojiSaveRetryMs)), "emoji gate: the retry crosses the wrap");
}

int main() {
  testDeleteArm();
  testRptrPageScrolled();
  testOtherScrolledLists();
  testEmojiGate();
  // The EST-398 #2 proof is only a proof if it ran: at least one tap puts the finger where the old
  // scroll-0 plate was not.
  check(g_bugCase > 0, "the scrolled-refresh bug was reproduced by the sweep");
  if (g_fail) {
    printf("%d est399 check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("est399: the per-row delete arm, the scrolled pressed rect on every list, and the batched "
         "emoji write: all checks passed (%d tap(s) where the old scroll-0 plate missed the finger)\n",
         g_bugCase);
  return 0;
}
