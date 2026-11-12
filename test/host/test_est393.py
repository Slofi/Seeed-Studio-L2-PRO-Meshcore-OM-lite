#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-393: the map picker's banner and its mark, at the source level.
#
# The C++ test (test_est393.cpp) proves the rectangles, the hit test and the shared drawing over the
# firmware's own headers. This grep proves the SHELL side, which no host binary can compile: the
# picker draws the SAME cross helper as the Map screen, it no longer has a crosshair of its own, its
# banner is placed from the screen floor, and the shell still turns the cross's pixel into the
# coordinate it saves.
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


def main():
    map_pick = read("src/ui/map_pick.cpp")
    map_pick_h = read("src/ui/map_pick.h")
    screen_map = read("src/ui/screen_map.cpp")
    chrome = read("src/ui/map_chrome.h")
    rptr_layout_h = read("src/ui/rptr_layout.h")
    rptr_layout_cpp = read("src/ui/rptr_layout.cpp")
    main_cpp = read("src/main.cpp")

    # 1. The picker draws the Map's one cross helper, and no crosshair of its own is left.
    if count(map_pick, "drawMapCentreCross(d, m.cross)") != 1:
        fail("map_pick.cpp does not draw the shared centre cross once")
    for gone in ("crosshair", "drawCircle(cx", "fillCircle(cx", "drawFastHLine(cx",
                 "drawFastVLine(cx"):
        if gone in map_pick:
            fail(f"map_pick.cpp still carries the old crosshair ({gone!r})")
    if "kMapCrossInk" in map_pick:
        fail("the picker paints the cross ink itself instead of calling the shared helper")

    # 2. The Map screen calls the same helper on its own cross rectangle.
    if count(screen_map, "drawMapCentreCross(d, mapCentreCrossRect(c))") != 1:
        fail("screen_map.cpp does not draw the centre cross through the shared helper")
    if count(chrome, "inline void drawMapCentreCross(") != 1:
        fail("map_chrome.h does not hold exactly one drawMapCentreCross() definition")
    if "kMapCentreCrossArm = 3" not in chrome:
        fail("the cross arm is not the EST-373 constant (3, i.e. 7 px)")
    for stroke in ("d->fillRect((int16_t)(cx - arm), cy, span, 1, kMapCrossInk);",
                   "d->fillRect(cx, (int16_t)(cy - arm), 1, span, kMapCrossInk);"):
        if stroke not in chrome:
            fail(f"the shared cross is missing its 1 px stroke {stroke!r}")

    # Only map_chrome.h paints the cross's ink: a second file with fillRect + kMapCrossInk would be
    # a second copy of the drawing.
    painters = []
    ink_stroke = re.compile(r"fillRect\([^\n;]*kMapCrossInk")
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in (".cpp", ".h") or path.name in ("map_chrome.h",):
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if ink_stroke.search(text):
            painters.append(str(path.relative_to(ROOT)))
    if painters:
        fail("the cross is painted outside ui/map_chrome.h: " + ", ".join(painters))
    if count(map_pick_h, "EST-393") != 1:
        fail("map_pick.h does not say the picker reuses the Map's cross")

    # 3. The banner is placed from the SCREEN FLOOR, not from the tab bar, and both keys hang off it.
    if "kRptrPickPanelH = 42" not in rptr_layout_h:
        fail("rptr_layout.h does not name the 42 px picker banner")
    if "Rect cross;" not in rptr_layout_h or "crosshair" in rptr_layout_h:
        fail("rptr_layout.h still calls the picker's mark a crosshair")
    if ("m.panel = Rect{0, (int16_t)(L.height - kRptrPickPanelH), L.width, kRptrPickPanelH};"
            not in rptr_layout_cpp):
        fail("the picker's banner is not placed from the screen floor")
    if "tabbar.y - 42" in rptr_layout_cpp:
        fail("the old banner position (tab bar - 42) is still in the layout")
    if "m.cross = mapCentreCrossAt(kMapCentreX, kMapCentreY);" not in rptr_layout_cpp:
        fail("the picker's cross is not the Map's own 7x7 box at its centre")
    if "crosshair" in rptr_layout_cpp:
        fail("rptr_layout.cpp still mentions a crosshair")

    # The drawing and the hit/press path read the SAME key rectangles.
    draw = block(map_pick, "void drawMapPick(", "void drawMapPickScreen(")
    if "m.panel" not in draw or "m.coords" not in draw or "m.useGps" not in draw or \
            "m.apply" not in draw:
        fail("the picker's drawing does not read its own layout rectangles")
    for needle in ("m.useGps", "m.apply"):
        if needle not in rptr_layout_cpp:
            fail(f"the picker's hit test does not read {needle}")

    # 4. The saved point is the pixel under the cross: both shell paths ask mapScreenToLatLon() for
    #    the Map's own centre constants. EST-410: the RPTR picker's path moved with the RPTR tab to
    #    screens/rptr_tab.cpp; the Settings one is still in main.cpp.
    for fn, rel in (("rptrMapPickCentreToDraft", "src/screens/rptr_tab.cpp"),
                    ("ownPickCentreToDraft", "src/main.cpp")):
        text = main_cpp if rel == "src/main.cpp" else read(rel)
        body = block(text, f"static void {fn}()", "\n}\n")
        if not body:
            fail(f"{fn}() not found in {rel}")
            continue
        if "om::mapScreenToLatLon(om::ui::kMapCentreX, om::ui::kMapCentreY" not in body:
            fail(f"{fn}() does not turn the pixel under the cross into the saved coordinate")
        if count(body, "mapScreenToLatLon(") != 1:
            fail(f"{fn}() uses more than one centre point")

    if FAIL == 0:
        print("est393.py: one shared cross, floor banner, keys on the same rects, centre pixel ok")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
