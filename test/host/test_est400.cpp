// EST-400 host test: the S5 polish fixes that carry logic. The manual-time stepper's real range
// (derived from the clock's own sanity window), the Settings label rooms (Stepper before its minus
// key, Toggle before its value), the ellipsis cut shared by drawTextEllipsis() and the toast, the
// Info page's clipped/scrolled body, the map callout's own text rows and the picker's empty-state
// band from the layout. Plain g++, no Arduino, no LovyanGFX.
#include <stdio.h>
#include <string.h>

#include "ui/map_chrome.h"
#include "ui/node_info.h"
#include "ui/node_info_layout.h"
#include "ui/rptr_layout.h"
#include "ui/settings_layout.h"
#include "ui/text_wrap.h"
#include "ui/theme.h"

using namespace om::ui;

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// --- the manual-time stepper's range ---------------------------------------------------------------
static void testYearStepper() {
  // The limits are the clock's own window, derived, not a second copy of 2000..2099.
  CHECK(om::kMinSaneYear == 2025);
  CHECK(om::kMaxSaneYear == 2038);
  CHECK(om::kMinSaneYear == om::civilYearOfEpoch(om::kMinSaneEpoch));
  CHECK(om::kMaxSaneYear == om::civilYearOfEpoch(om::kMaxSaneEpoch));
  // A year the clock can name from the first day of that year: 2038-01-01 is inside INT32_MAX.
  CHECK(om::saneEpoch(om::makeUtcEpoch(om::kMaxSaneYear, 1, 1, 0, 0, 0)));
  CHECK(!om::saneEpoch(om::makeUtcEpoch(2099, 1, 1, 0, 0, 0)));
  CHECK(!om::saneEpoch(om::makeUtcEpoch(2000, 1, 1, 0, 0, 0)));

  CivilTime t{};
  t.year = om::kMaxSaneYear;
  t.month = 12;
  t.day = 31;
  CivilTime next = stepCivilTime(t, TimeField::Year, +1);
  CHECK(next.year == om::kMinSaneYear);   // 2038 + 1 wraps to 2025, never to 2099/2000
  CHECK(next.month == 12 && next.day == 31);
  next = stepCivilTime(next, TimeField::Year, -1);
  CHECK(next.year == om::kMaxSaneYear);   // and back
  next = stepCivilTime(next, TimeField::Year, -1);
  CHECK(next.year == 2037);

  // Every year the stepper can walk to is one the clock's window can name (from Jan 1).
  for (int y = om::kMinSaneYear; y <= om::kMaxSaneYear; ++y) {
    CHECK(om::saneEpoch(om::makeUtcEpoch(y, 1, 1, 0, 0, 0)));
    CHECK(om::civilYearOfEpoch(om::makeUtcEpoch(y, 1, 1, 0, 0, 0)) == y);
  }

  // The day is re-clamped inside the year step (29 Feb 2028 -> 28 Feb 2029), the other fields wrap
  // exactly as they did.
  CivilTime leap{};
  leap.year = 2028;
  leap.month = 2;
  leap.day = 29;
  const CivilTime later = stepCivilTime(leap, TimeField::Year, +1);
  CHECK(later.year == 2029 && later.month == 2 && later.day == 28);
  CivilTime m{};
  m.year = 2030;
  m.month = 12;
  m.day = 31;
  const CivilTime jan = stepCivilTime(m, TimeField::Month, +1);
  CHECK(jan.month == 1 && jan.day == 31);
}

// --- the Settings label rooms ----------------------------------------------------------------------
static void testLabelRooms() {
  SettingRow rows[2];
  rows[0].kind = SetRowKind::Stepper;
  rows[1].kind = SetRowKind::Toggle;
  const SettingsLayout L = settingsLayoutFor(320, 240, true);
  const Rect step = settingsRowRect(L, rows, 2, 0, 0);
  const int room = settingsStepperLabelW(step);
  CHECK(room > 0);
  // The label's last pixel ends before the minus key's first one, with kSelGap of air between.
  const Rect minus = settingsStepMinusRect(step);
  CHECK(step.x + kSetPadX + room + kSelGap == minus.x);
  CHECK(step.x + kSetPadX + room <= minus.x);

  const Rect tog = settingsRowRect(L, rows, 2, 1, 0);
  const int valueW = 3 * kSetMonoAdvance;   // "OFF"
  const int roomT = settingsToggleLabelW(tog, valueW);
  CHECK(roomT > 0);
  CHECK(tog.x + kSetPadX + roomT + kSelGap == tog.right() - kSetPadX - valueW);
  CHECK(roomT + valueW <= tog.w - 2 * kSetPadX);

  // A row too narrow for both gives the label no negative width (a clip of 0 draws nothing).
  const Rect narrow{0, 0, 100, kRowH};
  CHECK(settingsStepperLabelW(narrow) == 0);
  CHECK(settingsToggleLabelW(narrow, 90) == 0);

  // The feature still measures as before at the L2's width.
  CHECK(room == 201 - 7 - kSelGap);   // minus x 201, row x 0, kSetPadX 7
}

// --- the ellipsis cut ------------------------------------------------------------------------------
static int measureFixed(void* ctx, const char* text, int len) {
  const int adv = *(const int*)ctx;
  int units = 0;
  for (int i = 0; i < len;) {
    int at = i;
    om::ui::utf8Decode(text, &at);
    if (at <= i) at = i + 1;
    i = at;
    ++units;
  }
  return units * adv;
}

static void testEllipsisFit() {
  const int adv = 6;
  void* ctx = (void*)&adv;
  CHECK(ellipsisFit("hello", measureFixed, ctx, 30) == -1);    // 30 px: fits exactly
  CHECK(ellipsisFit("hello!", measureFixed, ctx, 30) == 2);    // "he..." = 12 + 18
  CHECK(ellipsisFit("hello!", measureFixed, ctx, 24) == 1);    // "h..." = 6 + 18
  CHECK(ellipsisFit("hello!", measureFixed, ctx, 18) == 0);    // room only for the dots
  CHECK(ellipsisFit("hello!", measureFixed, ctx, 0) == 0);
  CHECK(ellipsisFit("", measureFixed, ctx, 30) == -1);

  // A code point is never split: "a" + one 2-byte letter fits before the dots, the next one does not.
  const char* mixed = "a\xC4\x8D\xC4\x8D\xC4\x8D\xC4\x8D\xC4\x8D";   // a + five c-caron
  CHECK(ellipsisFit(mixed, measureFixed, ctx, 32) == 3);      // "a" + one 2-byte letter
  CHECK((mixed[3] & 0xC0) != 0x80);                            // the cut is a lead byte
  for (int w = 0; w <= 60; ++w) {
    const int cut = ellipsisFit(mixed, measureFixed, ctx, w);
    CHECK(cut == -1 || cut == 0 || ((mixed[cut] & 0xC0) != 0x80));
    CHECK(cut <= (int)strlen(mixed));
  }
  // A long string is cut to something that, with the dots, fits the width.
  const char* longText = "abcdefghijklmnopqrstuvwxyz";
  for (int w = 18; w <= 60; ++w) {
    const int cut = ellipsisFit(longText, measureFixed, ctx, w);
    CHECK(cut >= 0);
    CHECK((cut + 3) * adv <= w);
  }
}

// --- the Info page's body and its scroll -----------------------------------------------------------
static void testNodeInfoLayout() {
  const Layout& L = l2();
  const NodeInfoLayout g = nodeInfoLayoutFor(L);
  // The header row is the header it always was, and the body starts 5 px under it.
  CHECK(g.back.x == L.content.x && g.back.y == L.content.y);
  CHECK(g.back.w == L.content.w && g.back.h == kHeadH);
  CHECK(g.body.y == g.back.bottom() + 5);
  // The hint is on the same line the old literal baseline drew, but it owns a rectangle now.
  CHECK(g.hint.y + g.hint.h / 2 == L.height - 10);
  CHECK(g.hint.bottom() <= L.height);
  // The body never reaches the hint: this is the overlap the finding named.
  CHECK(g.body.bottom() <= g.hint.y);
  CHECK(g.body.h > 0);

  // Today's maximum is eight rows (hasSnr is never true), and they fit; the ninth needs a scroll and
  // the scroll puts its last row exactly on the body's floor.
  CHECK(nodeInfoMaxScroll(8, g) == 0);
  const int max9 = nodeInfoMaxScroll(9, g);
  CHECK(max9 == 9 * kNodeInfoRowH - g.body.h);
  CHECK(max9 > 0);
  const int lastBottom = g.body.y - max9 + 9 * kNodeInfoRowH;
  CHECK(lastBottom == g.body.bottom());
  // The give and snap are the other lists' rule.
  CHECK(nodeInfoDragScroll(max9, 30, max9) == max9 + kSelGive);
  CHECK(nodeInfoSnapScroll(max9 + kSelGive, max9) == max9);
  CHECK(nodeInfoSnapScroll(-10, max9) == 0);
  CHECK(nodeInfoSnapScroll(7, max9) == 7);

  // The cheap line counter agrees with the builder for every model shape (the scroll limit is this
  // count, so a drift here would show as an unscrollable last row).
  NodeInfoLine lines[kMaxNodeInfoLines];
  for (int key = 0; key < 2; ++key) {
    for (int seen = 0; seen < 2; ++seen) {
      for (int pos = 0; pos < 2; ++pos) {
        for (int path = 0; path < 2; ++path) {
          for (int snr = 0; snr < 2; ++snr) {
            NodeInfoModel m;
            snprintf(m.name, sizeof(m.name), "%s", "Alice");
            m.hasKey = key != 0;
            m.hasPosition = pos != 0;
            m.hasSnr = snr != 0;
            if (seen) snprintf(m.lastSeen, sizeof(m.lastSeen), "%s", "12:30");
            if (path) snprintf(m.path, sizeof(m.path), "%s", "direct");
            CHECK(nodeInfoLineCount(m) == nodeInfoLines(m, lines, kMaxNodeInfoLines));
          }
        }
      }
    }
  }
  // The largest real page: name, type, three key rows, advert, position, path, SNR = 9.
  NodeInfoModel full;
  full.hasKey = full.hasPosition = full.hasSnr = true;
  snprintf(full.lastSeen, sizeof(full.lastSeen), "%s", "12:30");
  snprintf(full.path, sizeof(full.path), "%s", "2 hops");
  CHECK(nodeInfoLineCount(full) == 9);
  CHECK(nodeInfoLineCount(full) <= kMaxNodeInfoLines);
}

// --- the map callout and the picker's empty band ---------------------------------------------------
static void testMapConstants() {
  const Layout& L = l2();
  const MapChrome c = mapChromeFor(L, true, false);
  CHECK(c.callout.x == kMapMargin);
  CHECK(c.callout.w == L.width - 2 * kMapMargin);
  CHECK(c.callout.h == kMapCalloutH);
  CHECK(c.callout.bottom() == L.tabbar.y - kMapCalloutPadY);
  CHECK(!c.callout.overlaps(L.tabbar));
  // Both text rows are inside the box and never on each other.
  CHECK(kMapCalloutLine1Cy > 0 && kMapCalloutLine1Cy < kMapCalloutH);
  CHECK(kMapCalloutLine2Cy > kMapCalloutLine1Cy && kMapCalloutLine2Cy < kMapCalloutH);
  CHECK(kMapCalloutLine2Cy + 5 <= kMapCalloutH);
  CHECK(kMapCalloutPadX * 2 < c.callout.w);

  // The picker's empty state comes from the layout, inside the picker's own map area, and it is
  // clear of the back key, the zoom label and the coordinate panel.
  const RptrMapPickLayout m = rptrMapPickLayoutFor(L, false);
  CHECK(m.empty.w == m.map.w - 2 * kMapMargin);
  CHECK(m.empty.x == m.map.x + kMapMargin);
  CHECK(m.empty.h == kMapPickEmptyH);
  CHECK(m.empty.y == m.map.y + (m.map.h - kMapPickEmptyH) / 2);
  CHECK(m.map.contains(m.empty.x, m.empty.y));
  CHECK(m.map.contains(m.empty.right() - 1, m.empty.bottom() - 1));
  CHECK(!m.empty.overlaps(m.back));
  CHECK(!m.empty.overlaps(m.hint));
  CHECK(!m.empty.overlaps(m.panel));

  // The sheet row's value column is a theme constant and leaves a real label.
  const NodeSheetLayout sheet = nodeSheetLayout(L, 8);
  CHECK(sheet.rows[0].w - kNodeSheetValueW > 0);

  // The two message/action sheets have their own radius now, separate from the keyboard sheet's.
  CHECK(kSheetRadius == kRadiusLg);
  CHECK(kSheetRadius == kOskRadius);   // same 6 px today, independent constants from now on
}

int main() {
  testYearStepper();
  testLabelRooms();
  testEllipsisFit();
  testNodeInfoLayout();
  testMapConstants();
  if (g_fail == 0) {
    printf("EST-400: all checks passed (stepper range, label rooms, ellipsis, node info, map)\n");
    return 0;
  }
  printf("EST-400: %d failure(s)\n", g_fail);
  return 1;
}
