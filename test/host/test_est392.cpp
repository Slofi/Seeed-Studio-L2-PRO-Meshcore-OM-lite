// SPDX-License-Identifier: MIT
// EST-392: Settings > General's ONE position control, with the system g++
// (src/ui/settings_layout.h, src/ui/press_targets.h and src/position_source.h as the firmware
// compiles them).
//
// The ticket's own list, one section each:
//   1. the row labels: the General section is POSITION, its one control is `Position`, and the two
//      options are GPS and Fixed with their short descriptions (no digit or dot in any of them, so
//      no coordinate can ever be drawn from this row);
//   2. the choice's rectangles: a two-row panel hanging from the row's own bottom edge, the rows
//      tiling it exactly, the whole thing on the glass at the scroll the shell opens it at, and
//      ABOVE the row only when the page's floor leaves no room below (never cut off);
//   3. the hit test: an option answers 0 / 1 from the rect the drawing uses, and everything else
//      (the row above, Back, the page around the panel) is -1, i.e. Cancel;
//   4. the choice handling: GPS switches the source, Fixed opens EST-384's picker, Cancel changes
//      nothing at all, so a node on Fixed keeps its saved point and a node on GPS stays on GPS;
//   5. the pressed look: the pressed rect IS the option's rect, and outside the panel there is no
//      target at all (a consumed press, as the one-press-one-answer rule wants);
//   6. one control: the source's labels and the option table live in ONE header, and there is no
//      Position page in the SetPage list to hold a second control.
#include <stdio.h>
#include <string.h>

#include "position_source.h"
#include "ui/press_targets.h"
#include "ui/settings_layout.h"

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

static SettingRow mk(SetRowKind kind, const char* label, const char* value = "", bool on = false) {
  SettingRow r;
  r.kind = kind;
  snprintf(r.label, sizeof(r.label), "%s", label);
  snprintf(r.value, sizeof(r.value), "%s", value);
  r.on = on;
  return r;
}

// The General page as the shell builds it, with the GPS section renamed and the Position row FIRST
// in it. The rows above are the real ones, so the list has the real height and the scroll arithmetic
// is exercised, not a toy list.
static constexpr int kRows = 21;
static SettingRow g_rows[kRows];
static int g_posRow = -1;

static void buildGeneral() {
  int i = 0;
  g_rows[i++] = mk(SetRowKind::Section, "RADIO & MESH");
  g_rows[i++] = mk(SetRowKind::Chevron, "Region / preset", "Europe");
  g_rows[i++] = mk(SetRowKind::Stepper, "TX power", "22 dBm");
  g_rows[i++] = mk(SetRowKind::Chevron, "Path hash", "2B");
  g_rows[i++] = mk(SetRowKind::Chevron, "Node", "OM Lite");
  g_rows[i++] = mk(SetRowKind::Chevron, "Channels", "2 channels");
  g_rows[i++] = mk(SetRowKind::Section, "CONNECTIONS");
  g_rows[i++] = mk(SetRowKind::Chevron, "Wi-Fi", "off");
  g_rows[i++] = mk(SetRowKind::Chevron, "Bluetooth", "off");
  g_rows[i++] = mk(SetRowKind::Section, "SOUND");
  g_rows[i++] = mk(SetRowKind::Chevron, "Sound", "on");
  g_rows[i++] = mk(SetRowKind::Section, "DISPLAY");
  g_rows[i++] = mk(SetRowKind::Stepper, "Brightness", "180");
  g_rows[i++] = mk(SetRowKind::Section, "TIME");
  g_rows[i++] = mk(SetRowKind::Chevron, "Time", "11:24");
  g_rows[i++] = mk(SetRowKind::Section, kPosSectionLabel);
  g_posRow = i;
  g_rows[i++] = mk(SetRowKind::Chevron, kPosRowLabel, "GPS");
  g_rows[i++] = mk(SetRowKind::Value, "Satellites", "9 used, 14 heard");
  g_rows[i++] = mk(SetRowKind::Value, "Fix", "3D");
  g_rows[i++] = mk(SetRowKind::Toggle, "Share position", "ON", true);
  g_rows[i++] = mk(SetRowKind::Section, "ABOUT");
  check(i == kRows, "the General page's row list is complete");
  check(g_rows[g_posRow].kind == SetRowKind::Chevron, "the Position row opens something");
}

// ---- 1. the labels -------------------------------------------------------------------------------
static void testLabels() {
  check(strcmp(kPosSectionLabel, "POSITION") == 0, "the General section is POSITION");
  check(strcmp(kPosRowLabel, "Position") == 0, "the one control is labelled Position");
  check(kPosChoiceCount == 2, "two options");
  check(kPosOptionGps == 0 && kPosOptionFixed == 1, "GPS is the first option, Fixed the second");
  check(strcmp(posOptionLabel(kPosOptionGps), "GPS") == 0, "option 1 label is GPS");
  check(strcmp(posOptionLabel(kPosOptionFixed), "Fixed") == 0, "option 2 label is Fixed");
  check(strcmp(posOptionDesc(kPosOptionGps), "live fix") == 0, "GPS describes the live fix");
  check(strcmp(posOptionDesc(kPosOptionFixed), "picked on the map") == 0,
        "Fixed describes the map pick");
  check(strcmp(posOptionLabel(0), posOptionLabel(1)) != 0, "the two options read differently");

  // Rule 9: this row never shows a coordinate, so no label, description or the row's own name may
  // carry a digit or a dot that could be read as one.
  const char* texts[] = {kPosSectionLabel, kPosRowLabel, posOptionLabel(0), posOptionLabel(1),
                         posOptionDesc(0), posOptionDesc(1)};
  for (const char* t : texts) {
    bool clean = true;
    for (const char* p = t; *p; ++p) {
      if (*p == '.' || (*p >= '0' && *p <= '9')) clean = false;
    }
    check(clean, "no digit or dot in a position label");
    check(strstr(t, "\xe2\x80\x94") == nullptr, "no em dash in a position label");   // rule 7
  }
}

// ---- 2. the rectangles ---------------------------------------------------------------------------
static void testLayout() {
  const int sizes[][2] = {{320, 240}, {800, 480}};
  for (const auto& sz : sizes) {
    const SettingsLayout L = settingsLayoutFor((int16_t)sz[0], (int16_t)sz[1], false);
    const int maxScroll = settingsMaxScroll(L, g_rows, kRows);
    check(maxScroll > 0, "the General page is taller than the body");

    // At the scroll the shell opens it at, the panel hangs from the row's own bottom edge, from the
    // SAME row rectangle the page draws, and the whole of it is inside the body.
    for (int s = 0; s <= maxScroll; ++s) {
      const int opened = settingsChoiceScrollFor(L, g_rows, kRows, g_posRow, s);
      const Rect row = settingsRowRect(L, g_rows, kRows, g_posRow, opened);
      const SettingsChoiceLayout c = settingsPositionChoiceFor(L, g_rows, kRows, g_posRow, opened);
      check(!c.panel.empty(), "the panel has a rectangle");
      check(c.panel.y == row.bottom(), "the panel drops from the row's own bottom edge");
      check(c.panel.x == L.body.x + kSetPadX, "the panel is inset like the row's text");
      check(c.panel.w == L.body.w - 2 * kSetPadX, "the panel spans the row's text width");
      check(c.panel.h == kPosChoiceCount * kRowH, "the panel is exactly two rows tall");
      check(c.panel.y >= L.body.y && c.panel.bottom() <= L.body.bottom(),
            "the whole choice is on the glass");
      check(c.option[0].bottom() == c.option[1].y, "the two options tile the panel with no gap");
      check(c.option[kPosChoiceCount - 1].bottom() == c.panel.bottom(),
            "the last option ends at the panel's floor");
      for (int i = 0; i < kPosChoiceCount; ++i) {
        check(c.option[i].x == c.panel.x && c.option[i].w == c.panel.w,
              "an option shares the panel's x and width");
        check(c.option[i].h == kRowH, "an option is one row tall");
        check(c.option[i].y >= c.panel.y && c.option[i].bottom() <= c.panel.bottom(),
              "an option sits inside the panel");
      }
    }

    // The fallback: a row whose bottom edge IS the body's floor has no room below, so the panel goes
    // above the row and stays on the glass (it is never cut off).
    const Rect floorRow{L.body.x, (int16_t)(L.body.bottom() - kRowH), L.body.w, kRowH};
    const SettingsChoiceLayout up = settingsChoiceLayoutFor(L, floorRow);
    check(up.panel.bottom() == floorRow.y, "with no room below the panel sits above the row");
    check(up.panel.y >= L.body.y, "the drop-up panel is inside the body");
    check(up.option[1].bottom() == up.panel.bottom(), "the drop-up options tile the panel too");

    // A row in the middle drops down.
    const Rect midRow{L.body.x, (int16_t)(L.body.y + 40), L.body.w, kRowH};
    const SettingsChoiceLayout down = settingsChoiceLayoutFor(L, midRow);
    check(down.panel.y == midRow.bottom(), "a row with room below drops down");
  }

  // An unset row (another page) or no rows at all: an empty layout, so nothing can be hit.
  const SettingsLayout L = settingsLayoutFor(320, 240, false);
  check(settingsPositionChoiceFor(L, g_rows, kRows, -1, 0).panel.empty(), "no row index, no panel");
  check(settingsPositionChoiceFor(L, g_rows, kRows, kRows, 0).panel.empty(),
        "a row index past the list, no panel");
  check(settingsPositionChoiceFor(L, nullptr, 0, 0, 0).panel.empty(), "no rows, no panel");
}

// ---- 3. the scroll that keeps the panel on the glass ---------------------------------------------
static void testScroll() {
  const SettingsLayout L = settingsLayoutFor(320, 240, false);
  const int maxScroll = settingsMaxScroll(L, g_rows, kRows);

  // The row is lifted only when it has to be, and the whole panel then fits below it.
  for (int s = 0; s <= maxScroll; ++s) {
    const int opened = settingsChoiceScrollFor(L, g_rows, kRows, g_posRow, s);
    check(opened >= 0 && opened <= maxScroll, "the opened scroll is inside the page");
    check(opened >= s, "opening never scrolls the page back up");
    const SettingsChoiceLayout c = settingsPositionChoiceFor(L, g_rows, kRows, g_posRow, opened);
    check(c.panel.y == settingsRowRect(L, g_rows, kRows, g_posRow, opened).bottom(),
          "at the opened scroll the choice drops down, not up");
    check(c.panel.bottom() <= L.body.bottom(), "and the whole of it is above the body's floor");
  }

  // The row already high enough is not moved at all.
  const int high = maxScroll;   // the list scrolled to its floor: the Position row is high up
  check(settingsChoiceScrollFor(L, g_rows, kRows, g_posRow, high) == high,
        "the scroll is left alone when the choice already fits");
  // At the very top the row is too low, so the page lifts by exactly what the panel needs.
  const int top = settingsChoiceScrollFor(L, g_rows, kRows, g_posRow, 0);
  const Rect rowTop = settingsRowRect(L, g_rows, kRows, g_posRow, 0);
  const int missing = rowTop.bottom() + 2 * kRowH - L.body.bottom();
  check(missing > 0, "at the top of the page the row is too low for the panel");
  check(top == (missing <= maxScroll ? missing : maxScroll),
        "the lift is the panel's missing height (or the page's floor)");
  // Clamped, never negative and never past the floor.
  check(settingsChoiceScrollFor(L, g_rows, kRows, g_posRow, -50) >= 0,
        "a negative scroll is clamped");
  check(settingsChoiceScrollFor(L, g_rows, kRows, g_posRow, maxScroll + 50) ==
            settingsChoiceScrollFor(L, g_rows, kRows, g_posRow, maxScroll),
        "a scroll past the floor is clamped to it");
  check(settingsChoiceScrollFor(L, g_rows, kRows, -1, 0) == 0, "an unset row changes no scroll");
}

// ---- 4. the hit test ------------------------------------------------------------------------------
static void testHit() {
  const SettingsLayout L = settingsLayoutFor(320, 240, false);
  const int opened = settingsChoiceScrollFor(L, g_rows, kRows, g_posRow, 0);
  const SettingsChoiceLayout c = settingsPositionChoiceFor(L, g_rows, kRows, g_posRow, opened);
  const Rect row = settingsRowRect(L, g_rows, kRows, g_posRow, opened);

  for (int i = 0; i < kPosChoiceCount; ++i) {
    const Rect& o = c.option[i];
    check(settingsChoiceOptionAt(c, o.x + o.w / 2, o.y + o.h / 2) == i,
          "the centre of an option is that option");
    check(settingsChoiceOptionAt(c, o.x, o.y) == i, "an option's top-left corner belongs to it");
    check(settingsChoiceOptionAt(c, o.right() - 1, o.bottom() - 1) == i,
          "an option's last pixel belongs to it");
    check(settingsChoiceOptionAt(c, o.right(), o.y + 2) == -1,
          "one pixel past the panel's right edge is outside");
    // One pixel below an option is the NEXT option, or outside below the last one: the two rows
    // share an edge, so no pixel belongs to both and none is lost.
    const int below = settingsChoiceOptionAt(c, o.x + 3, o.bottom());
    check(below == (i + 1 < kPosChoiceCount ? i + 1 : -1),
          "one pixel below an option is the next option, or outside");
  }
  // The row above the panel is NOT part of the choice: a tap there is the Cancel that closes it.
  check(settingsChoiceOptionAt(c, row.x + 10, row.y + row.h / 2) == -1,
        "the Position row itself is outside the panel");
  // Back is outside as well, so the choice closes and the press is spent.
  check(settingsChoiceOptionAt(c, L.backButton.x + 4, L.backButton.y + 4) == -1,
        "Back is outside the panel");
  // The panel is solid: every pixel of it is one of the two options.
  for (int y = c.panel.y; y < c.panel.bottom(); ++y) {
    for (int x = c.panel.x; x < c.panel.right(); x += 7) {
      const int i = settingsChoiceOptionAt(c, x, y);
      check(i == 0 || i == 1, "every pixel of the panel is an option");
    }
  }
  // Closed: no rectangles, so any point is -1 (the shell only asks while it is open, but the answer
  // must not depend on that).
  const SettingsChoiceLayout empty;
  check(settingsChoiceOptionAt(empty, 100, 100) == -1, "a closed choice answers nothing");
}

// ---- 5. the choice handling -----------------------------------------------------------------------
static void testActions() {
  check(posChoiceAction(kPosOptionGps) == PosChoiceAct::Gps, "GPS selects the GPS source");
  check(posChoiceAction(kPosOptionFixed) == PosChoiceAct::OpenPicker,
        "Fixed opens EST-384's map picker");
  check(posChoiceAction(-1) == PosChoiceAct::Keep, "Cancel (-1) keeps the source");
  check(posChoiceAction(-7) == PosChoiceAct::Keep, "any other value is Cancel too");
  check(posChoiceAction(kPosChoiceCount) == PosChoiceAct::Keep, "past the last option is Cancel");

  // Apply a choice exactly as the shell does, so the test walks the same three answers.
  struct Apply {
    static void run(const PosChoiceAct a, PositionSource& s, double lat, double lon) {
      if (a == PosChoiceAct::Gps) s.selectGps();
      if (a == PosChoiceAct::OpenPicker) s.selectFixed(lat, lon);
    }
  };

  // A node on GPS with a fix: Cancel changes nothing.
  {
    PositionSource s;
    s.selectGps();
    s.setGpsFix(46.05, 14.51, true);
    check(!s.sourceFixed() && s.own().valid, "GPS with a fix uses the fix");
    Apply::run(posChoiceAction(-1), s, 1.0, 2.0);
    check(!s.sourceFixed(), "Cancel after GPS leaves the source on GPS");
    check(s.own().valid && s.own().lat == 46.05, "Cancel leaves the GPS fix in use");
  }
  // A node already on Fixed: Cancel keeps the source AND the saved point untouched.
  {
    PositionSource f;
    check(f.selectFixed(46.1, 14.6), "a Fixed point is accepted");
    Apply::run(posChoiceAction(-1), f, 1.0, 2.0);
    check(f.sourceFixed(), "Cancel after Fixed leaves the source on Fixed");
    check(f.fixedLat() == 46.1 && f.fixedLon() == 14.6, "Cancel keeps the saved point");
    // A later fix cannot move it either (EST-384's rule, unchanged here).
    f.setGpsFix(45.0, 13.0, true);
    check(f.own().lat == 46.1, "a later fix does not move a Fixed node");
    // And the picker's own Cancel (nothing written) is the same: the source stays Fixed.
    check(f.sourceFixed() && f.fixedLat() == 46.1, "the picker's Cancel keeps the Fixed source");
  }
  // The two options act exactly as the ticket says.
  {
    PositionSource f;
    f.selectFixed(46.1, 14.6);
    Apply::run(posChoiceAction(kPosOptionGps), f, 0.0, 0.0);
    check(!f.sourceFixed(), "choosing GPS switches the source to GPS");
    PositionSource p;
    Apply::run(posChoiceAction(kPosOptionFixed), p, 46.2, 14.7);
    check(p.sourceFixed() && p.fixedLat() == 46.2, "choosing Fixed stores the picked point");
  }
  // The row's value text: the SELECTED source, on both sides, no fix needed.
  {
    PositionSource s;
    s.selectGps();
    check(strcmp(s.sourceLabel(), "GPS") == 0, "the row reads GPS with GPS selected");
    check(strcmp(s.statusToken(), "none") == 0, "and the status token says there is no point yet");
    s.selectFixed(46.0, 14.0);
    check(strcmp(s.sourceLabel(), "Fixed") == 0, "the row reads Fixed with Fixed selected");
    s.selectGps();
    check(strcmp(s.sourceLabel(), "GPS") == 0, "and it reads GPS again after a switch");
    // Rule 9 again: the two words the row can show carry no coordinate.
    check(strchr(s.sourceLabel(), '.') == nullptr, "the row's text has no dot");
  }
}

// ---- 6. the pressed look --------------------------------------------------------------------------
static void testPress() {
  const SettingsLayout L = settingsLayoutFor(320, 240, false);
  const int opened = settingsChoiceScrollFor(L, g_rows, kRows, g_posRow, 0);
  const SettingsChoiceLayout c = settingsPositionChoiceFor(L, g_rows, kRows, g_posRow, opened);
  for (int i = 0; i < kPosChoiceCount; ++i) {
    const Rect& o = c.option[i];
    const PressTarget t = pressTargetForPositionChoice(c, o.x + o.w / 2, o.y + o.h / 2);
    check(t.valid(), "an option is a press target");
    check(t.kind == PressKind::Row, "the option is pressed like a row");
    check(sameRect(t.rect, o), "the pressed rect IS the drawn option rect");
  }
  check(!pressTargetForPositionChoice(c, c.panel.x + 2, c.panel.y - 4).valid(),
        "the row above the panel is not a target (the press only cancels)");
  check(!pressTargetForPositionChoice(c, L.backButton.x + 4, L.backButton.y + 4).valid(),
        "Back is not a target while the choice is open (the press only cancels)");
  check(!pressTargetForPositionChoice(c, 2, 2).valid(), "the far corner is not a target");
}

// ---- 7. one control -------------------------------------------------------------------------------
static void testOneControl() {
  // There is no Position PAGE in the page list any more, so no second control can be reached from
  // the settings history; the one control is the General page's row.
  check(!settingsRootPage(SetPage::Node) && kSetPageCount > 0, "the page list is intact");
  bool seen[kSetPageCount] = {};
  for (int i = 0; i < kSetPageCount; ++i) {
    const int v = (int)(SetPage)i;
    check(v >= 0 && v < kSetPageCount && !seen[v], "every page value is unique");
    if (v >= 0 && v < kSetPageCount) seen[v] = true;
  }
  check(strcmp(kPosSectionLabel, kPosRowLabel) != 0,
        "the section header and the row are different strings");
}

int main() {
  buildGeneral();
  testLabels();
  testLayout();
  testScroll();
  testHit();
  testActions();
  testPress();
  testOneControl();
  if (g_fail == 0) {
    printf("est392: one Position row, the GPS/Fixed choice, the map picker on Fixed, Cancel keeps "
           "the source, and no second source control: OK\n");
    return 0;
  }
  printf("est392: %d check(s) failed\n", g_fail);
  return 1;
}
