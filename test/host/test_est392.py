#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-392: ONE position source control, at the source level.
#
# The C++ test (test_est392.cpp) proves the choice's labels, rectangles, hit test, pressed rect and
# answers over the firmware's own headers. This grep proves the SHELL side, which no host binary can
# compile: Settings > General builds the only source row, it is labelled from the one header, its
# value is the selected source, the Node page's duplicate is gone, and no second door (a page, an id
# or a row) is left anywhere in src/.
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FAIL = 0


def fail(msg):
    global FAIL
    print("FAIL", msg)
    FAIL += 1


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def count(text, needle):
    return text.count(needle)


def block(text, start, end):
    """The text between the first `start` and the next `end` after it ('' when not found)."""
    i = text.find(start)
    if i < 0:
        return ""
    j = text.find(end, i + len(start))
    return text[i:j if j >= 0 else len(text)]


FLOAT = re.compile(r"%(?:[-+ 0#]*\d*(?:\.\d+)?)?[fF]")


def main():
    main_cpp = read("src/main.cpp")
    layout = read("src/ui/settings_layout.h")
    screen = read("src/ui/screen_settings.cpp")
    screen_h = read("src/ui/screen_settings.h")
    press = read("src/ui/press_targets.h")

    # 1. The General page: the section is POSITION, and ONE row is the source control.
    if count(main_cpp, "setSection(om::ui::kPosSectionLabel)") != 1:
        fail("the General page does not build exactly one POSITION section")
    if count(main_cpp, "setAdd(om::ui::SetRowKind::Chevron, A_PosSource, om::ui::kPosRowLabel") != 1:
        fail("the General page does not build exactly one Position row from the header labels")
    if "om::posSourceLabel()" not in block(main_cpp, "A_PosSource, om::ui::kPosRowLabel",
                                           "setAdd("):
        fail("the Position row's value is not the selected source label")
    if count(main_cpp, "case A_PosSource:") != 1:
        fail("A_PosSource does not have exactly one handler")
    # EST-422: the id is DECLARED in ui/settings_actions.h now (the confirm table needs it), so only
    # the row and the handler are left in the shell.
    if count(main_cpp, "A_PosSource") != 2:
        fail("A_PosSource appears more than at its row and its handler")

    # 2. The order of the section: the control, then the details that stay (satellites and fix).
    pos_block = block(main_cpp, "setSection(om::ui::kPosSectionLabel);", 'setSection("ABOUT")')
    order = [pos_block.find("A_PosSource"), pos_block.find('"Satellites"'), pos_block.find('"Fix"')]
    if any(i < 0 for i in order) or order != sorted(order):
        fail("the Position row, Satellites and Fix are not all present in that order")
    if '"Share position"' not in pos_block:
        fail("the Share position switch left the POSITION section")

    # 3. The duplicate EST-384 added on the Node page is gone, and so is the Position page.
    node_block = block(main_cpp, "case SetPage::Node: {", "case SetPage::NameEdit: {")
    if "A_PosSource" in node_block or "posStatusToken" in node_block:
        fail("the Node page still builds a second position source control")
    if "Share position" not in node_block:
        fail("the Node page lost its Share position switch (a different setting)")
    for gone in ("A_PosPage", "A_PosGps", "A_PosFixed", "A_PosChange", "SetPage::Position",
                 "Change fixed position", '"SOURCE"'):
        for rel in ("src/main.cpp", "src/ui/settings_layout.h"):
            if gone in read(rel):
                fail(f"{rel} still mentions {gone!r} (the removed page / duplicate)")

    # 4. The one door: choosing Fixed opens EST-384's picker, choosing GPS selects GPS, Cancel
    #    writes nothing. The picker's Save is the only place the shell stores a Fixed point.
    if count(main_cpp, "ownPickOpen(") != 2:      # the definition and its one call
        fail("ownPickOpen() has more than one call site in the shell")
    if count(main_cpp, "om::posSelectGps()") != 1:
        fail("the GPS source is selected from more than one place in the shell")
    if count(main_cpp, "om::posSelectFixed(") != 1:
        fail("the Fixed source is stored outside the one picker Save")
    choice_block = block(main_cpp, "if (g_posChoice) {", "// EST-350: while the chat selector")
    # EST-422: GPS is no longer written from the choice itself: the choice opens the shared confirm
    # sheet with the Position kind, and the one apply (settingsApplyConfirmed) writes it. So the
    # handler must ASK, and must still not store a Fixed point of its own.
    for needle in ("settingsPositionChoiceHit", "posChoiceAction", "ownPickOpen",
                   "ConfirmKind::Position"):
        if needle not in choice_block:
            fail(f"the choice handler does not use {needle}")
    if count(choice_block, "posSelectFixed") != 0:
        fail("the choice handler stores a point itself instead of opening the picker")
    if FLOAT.search(choice_block):
        fail("the choice handler formats a float (rule 9)")

    # 5. The labels live in ONE header, and the shell hardcodes neither of them (the toasts that say
    #    "Position: GPS" and the picker's own texts are sentences, not the row's label).
    for needle in ('kPosSectionLabel = "POSITION"', 'kPosRowLabel = "Position"'):
        if count(layout, needle) != 1:
            fail(f"settings_layout.h does not hold {needle}")
    if 'setSection("POSITION")' in main_cpp or 'setSection("Position")' in main_cpp:
        fail("main.cpp hardcodes the POSITION section label")
    if 'A_PosSource, "' in main_cpp:
        fail("main.cpp hardcodes the Position row's label")

    # 6. The drawing, the hit test and the pressed look all use the SAME rectangles: the choice
    #    layout is built in ONE function, the drawing, the hit test and the press target ask it.
    if count(layout, "settingsChoiceLayoutFor(") != 2:   # the definition and its one caller
        fail("the choice rectangles are computed in more than one place")
    if "settingsPositionChoiceFor(L, v.rows, v.rowCount, v.posChoiceRow," not in screen:
        fail("the drawing does not build its panel from the shared layout")
    if "settingsPositionChoiceFor(L, v.rows, v.rowCount, v.posChoiceRow, v.scroll)" not in screen:
        fail("the hit test does not build its panel from the shared layout")
    if "settingsChoiceOptionAt(" not in press or "pressTargetForPositionChoice" not in press:
        fail("the pressed look does not use the choice's own hit test")
    if "posOptionLabel(" not in screen or '"GPS"' in screen or '"Fixed"' in screen:
        fail("screen_settings.cpp invents an option label instead of using the header's")
    if "bool posChoice = false;" not in screen_h or "int posChoiceRow = -1;" not in screen_h:
        fail("screen_settings.h does not carry the open choice and its row")
    if "SettingsChoiceLayout settingsPositionChoice(" not in screen_h:
        fail("screen_settings.h does not expose the choice's rectangles")
    if "int settingsPositionChoiceHit(" not in screen_h:
        fail("the choice's hit test is not exported for the shell")

    # 7. Rule 9: the new choice never prints a coordinate, and its labels carry none (the C++ test
    #    checks the label texts themselves).
    if FLOAT.search(block(layout, "position choice", "manual time")):
        fail("a float format appears in the choice code")
    if FLOAT.search(block(screen, "void drawPositionChoice(", "}  // namespace")):
        fail("the choice's drawing formats a float")

    # 8. The chat selector and the confirmation pop-up are untouched: the choice is its own layer.
    # EST-422 renamed the pop-up's drawing to drawAsk (every important setting asks there now).
    if "drawChatSelector" not in screen or "drawAsk" not in screen:
        fail("an existing overlay's drawing disappeared")

    if FAIL:
        print(f"{FAIL} est392 source check(s) FAILED")
        return 1
    print("est392: one Position row, no second source control, one set of rects for draw/hit/press")
    return 0


if __name__ == "__main__":
    sys.exit(main())
