#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-400, the source half: the S5 polish fixes that are wiring rather than arithmetic (the arithmetic
# is test_est400.cpp). One section per finding:
#
#   EST-396 #3  the Nodes sheet row's value column is a theme constant, aligned a pad in, and the
#               gold ink is a flag instead of sniffing the first two characters of the value.
#   EST-396 #4  the map callout's two text rows and its inset are constants, not literals.
#   EST-396 #5  the dead `topRight` coordinate rule is gone from handleTap.
#   EST-396 #6  the one-line toast ellipsises to its plate (drawTextEllipsis), never drawString.
#   EST-396 #7  the message / action sheets use kSheetRadius, not the keyboard's kOskRadius.
#   EST-396 #8  the notice log line is printed AFTER the show really happened.
#   EST-398 #5  ONE applyManualTime() (one localEpochFromCivil call) behind both doors.
#   EST-398 #6  the year stepper's wrap uses the clock's derived kMinSaneYear/kMaxSaneYear.
#   EST-398 #7  the Warn plate insets from r.x + kSetPadX.
#   EST-398 #8  Stepper and Toggle labels ellipsise to their measured room.
#   EST-398 #9  the RPTR confirmation uses the shared wrapper, with an ellipsis on a cut tail.
#   EST-398 #10 one ui/keys.cpp (small keys + editorKeyAt), no second copy in map_pick.
#   EST-398 #11 the picker's empty state comes from the layout (m.empty).
#   EST-398 #12 the Info page clips to a layout body and scrolls, with the hint in the layout.
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
FAIL = 0


def fail(msg):
    global FAIL
    print("FAIL", msg)
    FAIL += 1


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def need(haystack, needle, what):
    if needle not in haystack:
        fail(f"{what}: missing {needle!r}")


def forbid(haystack, needle, what):
    if needle in haystack:
        fail(f"{what}: found {needle!r} (must be gone)")


def count(haystack, needle):
    return haystack.count(needle)


def body_of(text, marker):
    """The text from `marker` to the first line that is exactly '}' (a C function's body)."""
    at = text.find(marker)
    if at < 0:
        return ""
    end = text.find("\n}\n", at)
    return text[at:end if end >= 0 else len(text)]


def main():
    main_cpp = read("src/main.cpp")
    theme_h = read("src/ui/theme.h")
    timekeeper_h = read("src/timekeeper.h")
    settings_layout_h = read("src/ui/settings_layout.h")
    screen_settings_cpp = read("src/ui/screen_settings.cpp")
    screen_settings_h = read("src/ui/screen_settings.h")
    screen_rptr_cpp = read("src/ui/screen_rptr.cpp")
    screen_rptr_h = read("src/ui/screen_rptr.h")
    screen_nodes_cpp = read("src/ui/screen_nodes.cpp")
    screen_chat_cpp = read("src/ui/screen_chat.cpp")
    node_menu_cpp = read("src/ui/node_menu.cpp")
    node_menu_h = read("src/ui/node_menu.h")
    map_chrome_h = read("src/ui/map_chrome.h")
    map_chrome_cpp = read("src/ui/map_chrome.cpp")
    screen_map_cpp = read("src/ui/screen_map.cpp")
    map_pick_cpp = read("src/ui/map_pick.cpp")
    rptr_layout_h = read("src/ui/rptr_layout.h")
    rptr_layout_cpp = read("src/ui/rptr_layout.cpp")
    keys_h = read("src/ui/keys.h")
    keys_cpp = read("src/ui/keys.cpp")
    widgets_cpp = read("src/ui/widgets.cpp")
    text_wrap_h = read("src/ui/text_wrap.h")
    text_wrap_cpp = read("src/ui/text_wrap.cpp")
    toasts_cpp = read("src/screens/toasts.cpp")
    node_info_h = read("src/ui/node_info.h")
    node_info_layout_h = read("src/ui/node_info_layout.h")
    screen_node_info_cpp = read("src/ui/screen_node_info.cpp")
    nodes_tab_cpp = read("src/screens/nodes_tab.cpp")

    # ---- EST-396 #3: the Nodes sheet row's value column -------------------------------------
    need(theme_h, "constexpr int16_t kNodeSheetValueW = 70;", "theme keeps the value column")
    forbid(screen_nodes_cpp, "r.w - 70", "the hard-coded 70 px label width")
    forbid(screen_nodes_cpp, "value[0] == 'o' && value[1] == 'n'", "the text-sniffed gold ink")
    need(screen_nodes_cpp, "r.w - kNodeSheetValueW", "the label stops at the value column")
    need(screen_nodes_cpp, "r.right() - kSheetPadX", "the value is a sheet pad in from the edge")
    need(screen_nodes_cpp, "v.menu[i].valueAccent", "the drawing passes the flag")
    need(node_menu_h, "bool valueAccent = false;", "the row model carries the flag")
    need(node_menu_cpp, 'c.favourite ? "on" : "off",', "the favourite row passes its state")

    # ---- EST-396 #4: the callout's own text rows --------------------------------------------
    for const in ("kMapCalloutPadX", "kMapCalloutPadY", "kMapCalloutLine1Cy", "kMapCalloutLine2Cy"):
        need(map_chrome_h, f"constexpr int16_t {const}", f"map_chrome.h defines {const}")
    need(map_chrome_cpp, "barTop - kMapCalloutPadY - kMapCalloutH", "the callout's inset")
    need(map_chrome_cpp, "(int16_t)(w - 2 * kMapMargin)", "the callout's margin")
    need(screen_map_cpp, "c.callout.x + kMapCalloutPadX", "the callout text x")
    need(screen_map_cpp, "c.callout.y + kMapCalloutLine1Cy", "the callout first line")
    need(screen_map_cpp, "c.callout.y + kMapCalloutLine2Cy", "the callout second line")
    forbid(screen_map_cpp, "c.callout.x + 7", "the callout's literal x pad")
    forbid(screen_map_cpp, "c.callout.y + 12", "the callout's literal first baseline")
    forbid(screen_map_cpp, "c.callout.y + 28", "the callout's literal second baseline")

    # ---- EST-396 #5: the dead coordinate rule -----------------------------------------------
    forbid(main_cpp, "topRight", "the dead handleTap coordinate rule")

    # ---- EST-396 #6: the one-line toast ellipsises ------------------------------------------
    toast = body_of(widgets_cpp, "void drawToast(")
    if not toast:
        fail("drawToast not found")
    else:
        need(toast, "drawTextEllipsis(", "the toast draws through the ellipsis helper")
        forbid(toast, "drawString(", "the toast still draws an unclipped string")
        forbid(toast, "middle_center", "the toast still uses the unclipped centred datum")
        need(toast, "measureText(", "the toast measures its text before deciding")
    need(text_wrap_h, "int ellipsisFit(", "text_wrap.h declares the shared cut")
    need(text_wrap_cpp, "int ellipsisFit(", "text_wrap.cpp defines it")
    need(widgets_cpp, "ellipsisFit(s, measureCb, &ctx, maxW)", "drawTextEllipsis asks it")

    # ---- EST-396 #7: the sheets' own radius -------------------------------------------------
    need(theme_h, "constexpr int16_t kSheetRadius = kRadiusLg;", "theme keeps kSheetRadius")
    for name, text in (("screen_chat.cpp", screen_chat_cpp), ("screen_nodes.cpp", screen_nodes_cpp)):
        need(text, "kSheetRadius", f"{name} uses its own sheet radius")
        # screen_chat still draws the KEYBOARD sheet with kOskRadius (that is its own radius); the
        # finding is the message/action sheets that borrowed it.
        forbid(text, "g.panel.h + kOskRadius", f"{name} borrows the keyboard sheet's radius")

    # ---- EST-396 #8: the notice log after the show ------------------------------------------
    deliver = body_of(toasts_cpp, "ui::NoticeDelivery deliver(")
    if not deliver:
        fail("deliver() not found")
    else:
        if count(deliver, "logDecision(") != 1:
            fail(f"deliver() must log exactly once, found {count(deliver, 'logDecision(')}")
        show_at = deliver.find("showNotice(")
        log_at = deliver.find("logDecision(")
        if show_at < 0 or log_at < 0:
            fail("deliver() lost its show call or its log call")
        elif log_at < show_at:
            fail("the notice log is printed BEFORE the show can refuse it")
        need(deliver, 'dec.reason = "refused";', "a refused show logs a skip reason")
        forbid(deliver, "const ui::NoticeDecision dec", "the decision is still immutable")

    # ---- EST-398 #5: one manual-time apply --------------------------------------------------
    if count(main_cpp, "localEpochFromCivil(") != 1:
        fail(f"main.cpp must build the manual epoch once, found "
             f"{count(main_cpp, 'localEpochFromCivil(')}")
    if count(main_cpp, "applyManualTime") != 3:
        fail("applyManualTime must be one definition plus two call sites, found "
             f"{count(main_cpp, 'applyManualTime')}")
    need(main_cpp, "static bool applyManualTime() {", "the one manual-time helper")
    forbid(main_cpp, ', "The next GPS fix takes over."', "the old Note row")

    # ---- EST-398 #6: the year stepper's real range ------------------------------------------
    need(timekeeper_h, "constexpr int civilYearOfEpoch(uint32_t epoch)", "the derived year")
    need(timekeeper_h, "constexpr int kMinSaneYear = civilYearOfEpoch(kMinSaneEpoch);",
         "the lower limit comes from the clock")
    need(timekeeper_h, "constexpr int kMaxSaneYear = civilYearOfEpoch(kMaxSaneEpoch);",
         "the upper limit comes from the clock")
    need(settings_layout_h, "if (t.year > om::kMaxSaneYear) t.year = om::kMinSaneYear;",
         "the year wraps at the clock's top")
    need(settings_layout_h, "if (t.year < om::kMinSaneYear) t.year = om::kMaxSaneYear;",
         "the year wraps at the clock's floor")
    forbid(settings_layout_h, "if (t.year > 2099)", "the old 2099 wrap")
    forbid(settings_layout_h, "if (t.year < 2000)", "the old 2000 wrap")

    # ---- EST-398 #7: the Warn box ------------------------------------------------------------
    warn = body_of(screen_settings_cpp, "void drawWarn(")
    if not warn:
        fail("drawWarn not found")
    else:
        need(warn, "r.x + kSetPadX", "the plate insets like every other row")
        forbid(warn, "fillRoundRect(r.x, r.y + 3", "the left-flush plate")
        forbid(warn, "drawRoundRect(r.x, r.y + 3", "the left-flush plate edge")

    # ---- EST-398 #8: the two unellipsised row kinds ------------------------------------------
    need(settings_layout_h, "constexpr int16_t settingsStepperLabelW(", "the stepper's room")
    need(settings_layout_h, "constexpr int16_t settingsToggleLabelW(", "the toggle's room")
    need(screen_settings_cpp, "settingsStepperLabelW(r)", "the Stepper draws with its room")
    need(screen_settings_cpp, "settingsToggleLabelW(r, valueW)", "the Toggle measures its value")
    # The remaining unclipped label is the Selector's section label (it measures its own value and
    # ellipsises the name after it); the two kinds this finding names are gone.
    if count(screen_settings_cpp, "d->drawString(row.label, labelX, cy);") != 1:
        fail("the Stepper/Toggle labels are still drawn unclipped")

    # ---- EST-398 #9: the shared wrapper ------------------------------------------------------
    forbid(screen_rptr_cpp, "char line[48]", "the private 48-byte line buffer")
    forbid(screen_rptr_cpp, "lastSpace", "the private word break")
    need(screen_rptr_cpp, "wrapText(text, measureWrapped", "the shared wrapper")
    need(screen_rptr_cpp, "wrapConsumed(text, lines, n)", "the cut is detected")
    need(screen_rptr_cpp, 'strncat(buf, "...",', "the cut tail says so")

    # ---- EST-398 #10: one shared file --------------------------------------------------------
    for fn in ("void drawSmallKey(", "void drawSheetBackKey(", "void drawActionKey(",
               "EditorKeyHit editorKeyAt("):
        if count(keys_cpp, fn) != 1:
            fail(f"keys.cpp must define {fn!r} exactly once")
        need(keys_h, fn.split("(")[0].replace("void ", ""), "keys.h declares it")
    for name, text in (("screen_rptr.cpp", screen_rptr_cpp), ("map_pick.cpp", map_pick_cpp)):
        for old in ("void smallKey(", "void drawBack(", "void drawButton(", "drawPickAction"):
            forbid(text, old, f"{name} keeps a private copy")
        need(text, "drawSmallKey(", f"{name} asks the shared small key")
        need(text, "drawSheetBackKey(", f"{name} asks the shared back key")
        need(text, "drawActionKey(", f"{name} asks the shared action key")
    need(screen_settings_h, "using SettingsKeyHit = EditorKeyHit;", "one key-hit struct")
    need(screen_rptr_h, "using RptrKeyHit = EditorKeyHit;", "one key-hit struct")
    forbid(screen_settings_h, "struct SettingsKeyHit", "a second key-hit struct")
    forbid(screen_rptr_h, "struct RptrKeyHit", "a second key-hit struct")
    key_at = count(keys_cpp, "const KeyHit key = keyAt(")
    key_at += count(screen_settings_cpp, "const KeyHit key = keyAt(")
    key_at += count(screen_rptr_cpp, "const KeyHit key = keyAt(")
    if key_at != 1:
        fail(f"the editor key hit test must exist once, found {key_at}")
    need(screen_settings_cpp, "return editorKeyAt(g, field, v.editNums, v.editShift, x, y);",
         "Settings asks the shared test")
    need(screen_rptr_cpp, "return editorKeyAt(g, field, v.nums, v.shift, x, y);",
         "RPTR asks the shared test")

    # ---- EST-398 #11: the picker's empty state ----------------------------------------------
    forbid(map_pick_cpp, "Rect{8, 60, 304, 100}", "the screen-sized literal rect")
    need(map_pick_cpp, "drawEmptyState(d, m.empty,", "the empty state comes from the layout")
    need(rptr_layout_h, "Rect empty;", "the picker layout owns the band")
    need(rptr_layout_h, "constexpr int16_t kMapPickEmptyH", "its height is a constant")
    need(rptr_layout_cpp, "m.empty = Rect{", "the layout builds it")
    need(rptr_layout_cpp, "m.map.w - 2 * kMapMargin", "it is inside the map, one margin in")

    # ---- EST-398 #12: the Info page's clip and scroll ---------------------------------------
    need(node_info_layout_h, "constexpr NodeInfoLayout nodeInfoLayoutFor(", "the page layout")
    need(node_info_layout_h, "constexpr int nodeInfoMaxScroll(", "the scroll limit")
    need(node_info_h, "inline int nodeInfoLineCount(", "the cheap line counter")
    need(screen_node_info_cpp, "setClipRect(g.body.x", "the rows are clipped")
    need(screen_node_info_cpp, "g.hint.y + g.hint.h / 2", "the hint's own line")
    forbid(screen_node_info_cpp, "const int16_t rowH = 20", "the literal row height")
    forbid(screen_node_info_cpp, "L.height - 10", "the literal hint baseline")
    # EST-411: the Info page's state and drawing glue moved with the Nodes tab.
    need(nodes_tab_cpp, "nodeInfoDraw(status, g_nodeInfoModel, g_nodeInfoScroll)",
         "the shell passes the scroll")
    need(nodes_tab_cpp, "g_nodeInfoScroll0 = g_nodeInfoScroll;", "the drag base")
    need(nodes_tab_cpp, "om::ui::nodeInfoDragScroll(", "the drag")
    need(nodes_tab_cpp, "om::ui::nodeInfoSnapScroll(", "the release clamp")
    need(nodes_tab_cpp, "g_nodeInfoScroll = 0;", "the page opens at the top")

    if FAIL == 0:
        print("EST-400: source checks passed (sheets, toast, notice log, manual time, year range, "
              "keys, picker, node info)")
        return 0
    print(f"EST-400: {FAIL} source check(s) failed")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
