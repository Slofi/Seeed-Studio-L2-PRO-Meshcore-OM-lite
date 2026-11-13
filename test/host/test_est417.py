#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-417: the source half of the CardKB arrows ticket. The C++ half (test_est417.cpp) proves the
# measured byte table, the key-to-action table for all four contexts, the no-wrap tab step, the
# one-row scroll rule, the map step and the lost-keyboard limit; this file pins what the shell and
# the driver actually do with them:
#  * the probe tries SDA = GPIO13 first (Filip's measured order) and keeps the (14, 13) fallback once;
#  * an unplugged keyboard: close the bus, drop Grove power, ONE log line, one toast, switch untouched;
#  * the poll gate is switch + found only (a text field no longer gates a read), one call site;
#  * each arrow byte is decided by arrowFor() before mapCode, and the actions use the SAME doors a
#    tap, a drag or a tab tap use;
#  * the map key pan takes the drag's path (clearSelection, setLock(false), mapPan, draw), one fixed
#    quarter-canvas step;
#  * a text field scrolls only the chat behind a composer, and a sheet blocks every arrow;
#  * the chat selector is a list (the channel list), not a blocking sheet;
#  * no wrap: the tab helper is the only tab stepper, with the ends as stops.
#
# Run it alone against another tree with `python3 test_est417.py /path/to/tree`.
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FAIL = 0


def fail(what):
    global FAIL
    print("FAIL", what)
    FAIL += 1


def need(text, needle, why):
    if needle not in text:
        fail(f"{why}: missing {needle!r}")


def forbid(text, needle, why):
    if needle in text:
        fail(f"{why}: still present {needle!r}")


def block(text, start, end):
    i = text.find(start)
    if i < 0:
        return ""
    j = text.find(end, i + len(start))
    return text[i:j] if j >= 0 else text[i:]


def main(argv):
    root = pathlib.Path(argv[1]).resolve() if len(argv) > 1 else ROOT
    src = root / "src"
    maincpp = (src / "main.cpp").read_text(encoding="utf-8")
    driver = (src / "cardkb.cpp").read_text(encoding="utf-8") if (src / "cardkb.cpp").exists() else ""
    logic = (src / "cardkb_logic.h").read_text(encoding="utf-8")
    keynav = (src / "keynav_logic.h").read_text(encoding="utf-8") \
        if (src / "keynav_logic.h").exists() else ""
    maptabc = (src / "screens" / "map_tab.cpp").read_text(encoding="utf-8")
    maptabh = (src / "screens" / "map_tab.h").read_text(encoding="utf-8")
    chromh = (src / "ui" / "map_chrome.h").read_text(encoding="utf-8")
    nodesc = (src / "screens" / "nodes_tab.cpp").read_text(encoding="utf-8")
    nodesh = (src / "screens" / "nodes_tab.h").read_text(encoding="utf-8")
    rptrc = (src / "screens" / "rptr_tab.cpp").read_text(encoding="utf-8")
    rptrh = (src / "screens" / "rptr_tab.h").read_text(encoding="utf-8")

    # --- the measured pin order: SDA 13 first, the other order kept ----------------------------
    # EST-421: the probe moved out of begin() into the start window's step(), which tries the
    # measured order (SDA 13 / SCL 14) first and the swap on every alternate attempt.
    begin = block(driver, "bool begin() {", "void step(uint32_t nowMs) {")
    if "openAndProbe" in begin:
        fail("begin() must not probe: the start window's step() does that now")
    step = block(driver, "void step(uint32_t nowMs) {", "void end() {")
    need(step, "w.sda13First ? kPinGroveDat2 : kPinGroveDat1",
         "the probe no longer tries SDA = 13 (kPinGroveDat2) first")
    need(step, "w.sda13First ? kPinGroveDat1 : kPinGroveDat2",
         "the probe no longer tries the other pin order")
    if step.find("w.sda13First ? kPinGroveDat2") > step.find("w.sda13First ? kPinGroveDat1"):
        fail("the probe still tries SDA=14 first (Filip measured SDA=13)")
    if driver.count("openAndProbe(") != 2:   # the helper's definition and its one call
        fail(f"the enable path must have one probe call site, found "
             f"{driver.count('openAndProbe(')} mentions")

    # --- the unplugged keyboard --------------------------------------------------------------
    lost = block(driver, "void noteFailedRead()", "}  // namespace")
    if not lost:
        fail("the driver has no unplugged-keyboard path")
    need(lost, 'Serial.println("[kb] keyboard disconnected")',
         "the disconnect is not logged")
    if driver.count("[kb] keyboard disconnected") != 1:
        fail("the disconnect log line must be the ONE line (a pulled keyboard logs once)")
    need(lost, "busDown();", "the disconnect leaves the second I2C bus open")
    need(lost, "s_found = false;", "the disconnect does not clear found")
    need(lost, "boardGrovePower(false);", "the disconnect leaves the Grove 5 V rail on")
    need(lost, "s_lostNotice = true;", "the disconnect does not ask for the toast")
    need(lost, "lostAfterFailure(", "the disconnect does not go through the tested limit")
    poll = block(driver, "int poll(uint32_t nowMs)", "}  // namespace")
    if poll.count("noteFailedRead()") != 2:
        fail("both a refused request and a bad read must count as a failed read")
    need(poll, "s_failRun = 0;", "a good read does not clear the run of failures")
    # The switch is not touched anywhere in the disconnect path: OFF and ON again is the probe.
    for word in ("boardGrovePower(true)", "Preferences", "g_kbEnabled"):
        forbid(lost, word, "the disconnect must not change the switch or power up again")
    step = block(maincpp, "static void cardKbStep(uint32_t nowMs)", "void loop()")
    need(step, "takeLostNotice()", "the shell never takes the lost notice")
    need(step, 'showToast("Keyboard disconnected")', "the disconnect toast is missing")
    notice = maincpp.find("takeLostNotice()")
    gate = maincpp.find("shouldPoll(g_kbEnabled, om::cardkb::found())")
    if notice < 0 or gate < 0 or notice > gate:
        fail("the lost notice must be taken BEFORE the gate (found() is false by then)")

    # --- the poll gate -----------------------------------------------------------------------
    need(step, "shouldPoll(g_kbEnabled, om::cardkb::found())",
         "the poll is not behind the switch + found gate")
    forbid(step, "textFieldOpen", "a text field must no longer gate the poll")
    if maincpp.count("om::cardkb::poll(nowMs)") != 1:
        fail("the CardKB poll must have exactly one call site (the loop task)")
    need(logic, "inline bool shouldPoll(bool enabled, bool found)", "the gate is not switch + found")
    need(logic, "return enabled && found;", "the gate can still read with the switch off")

    # --- the arrow bytes and the rule header ------------------------------------------------
    for pair in ('kCodeLeft = 0xB4', 'kCodeRight = 0xB7', 'kCodeUp = 0xB5', 'kCodeDown = 0xB6',
                 'kCodeFnLeft = 0x98', 'kCodeFnRight = 0xA5', 'kCodeFnUp = 0x99',
                 'kCodeFnDown = 0xA4'):
        need(logic, pair, "a measured arrow byte is missing")
    need(logic, "inline Arrow arrowFor(uint8_t code)", "there is no one byte-to-arrow door")
    if keynav:
        need(keynav, "constexpr Act actFor(cardkb::Arrow a, Context c)",
             "the action table is not one function")
        need(keynav, "constexpr int tabStep(int current, int delta, int count)",
             "there is no one no-wrap tab step")
        need(keynav, "constexpr int keyScroll(int base, int dir, int rowPx, int maxScroll)",
             "there is no one one-row scroll rule")
        need(keynav, "ui::listSnapScroll(base + dir * rowPx, maxScroll)",
             "a key press must use the release clamp, not a second rule")
        need(keynav, "constexpr bool kFnZoomOnMap = true",
             "the Fn zoom mapping is not one removable constant")
        need(keynav, "if (c == Context::Sheet) return Act::None;",
             "a sheet must block every arrow")
        need(keynav, "if (c == Context::TextField)", "there is no text-field context")
        for word in ("0xB4", "0xB7", "0xB5", "0xB6", "0x98", "0xA5", "0x99", "0xA4"):
            forbid(keynav, word, "the byte table belongs in cardkb_logic.h, not here")
    else:
        fail("src/keynav_logic.h is missing")

    # --- the shell routing --------------------------------------------------------------------
    key = block(maincpp, "static void cardKbKey(uint8_t code)", "static void cardKbStep")
    if key.find("arrowFor(code)") < 0 or key.find("cardKbArrow(arrow)") < 0:
        fail("cardKbKey does not decide an arrow before mapCode")
    if key.find("arrowFor(code)") > key.find("mapCode(code, false)"):
        fail("mapCode runs before arrowFor, so an arrow could be swallowed")
    need(key, "screenWake", "an arrow press must wake a sleeping screen like any key")
    arrow = block(maincpp, "static void cardKbArrow(om::cardkb::Arrow a)", "static void cardKbKey")
    for act in ("Act::TabPrev", "Act::TabNext", "Act::ScrollUp", "Act::ScrollDown",
                "Act::PanLeft", "Act::PanRight", "Act::PanUp", "Act::PanDown",
                "Act::ZoomIn", "Act::ZoomOut", "Act::None"):
        need(arrow, act, f"the arrow router never handles {act}")
    need(arrow, "om::keynav::actFor(a, ctx)", "the router does not use the tested table")
    need(arrow, "keynavContext()", "the router does not ask the screen's context")
    need(arrow, "om::map_tab::panStep(-om::ui::kMapKeyPanX, 0)", "no left pan")
    need(arrow, "om::map_tab::panStep(om::ui::kMapKeyPanX, 0)", "no right pan")
    need(arrow, "om::map_tab::panStep(0, -om::ui::kMapKeyPanY)", "no up (north) pan")
    need(arrow, "om::map_tab::panStep(0, om::ui::kMapKeyPanY)", "no down (south) pan")
    need(arrow, "om::map_tab::zoomStep(1)", "Fn+Up does not zoom in")
    need(arrow, "om::map_tab::zoomStep(-1)", "Fn+Down does not zoom out")
    ctx = block(maincpp, "static om::keynav::Context keynavContext()", "static void keynavTabStep")
    need(ctx, "textFieldOpen()", "the context does not ask whether a text field is open")
    need(ctx, "popupOpen()", "the context does not ask whether a sheet is open")
    need(ctx, "g_tab == 2", "the context never names the Map")
    need(ctx, "g_selector", "the chat selector is not treated as the channel list")

    # popupOpen is ONE list, used by both its readers (the timer and the arrows).
    popup = block(maincpp, "static bool popupOpen()", "static om::IdleBlockers idleBlockersNow")
    if maincpp.count("static bool popupOpen()") != 1:
        fail("popupOpen must be defined once")
    if maincpp.count("popupOpen()") < 2:
        fail("popupOpen has fewer than two readers (the timer and the arrows)")
    # EST-422: the confirmation state is g_ask (the one confirm sheet) and it is still a pop-up here.
    for word in ("g_ask.open", "g_posChoice", "g_ownPick", "g_bubMenuSeq", "modalOpen()",
                 "mapPickerOpen()"):
        need(popup, word, f"popupOpen lost a pop-up: {word}")
    need(maincpp, "b.popup = popupOpen();", "the timer no longer uses the one pop-up list")

    # Tabs: one stepper, the ends, and the Settings tab row inside Settings.
    tab = block(maincpp, "static void keynavTabStep(int delta)", "static void keynavScroll")
    need(tab, "om::keynav::tabStep(", "the tabs are not stepped through the tested helper")
    need(tab, "om::ui::kTabCount", "the main tab row's length is not the layout's")
    need(tab, "om::ui::kSettingsTabCount", "Settings' own tab row is not stepped")
    need(tab, "showTab(t)", "a main tab step does not go through the one tab switch")
    need(tab, "drawSettings();", "a Settings tab step does not redraw")
    need(tab, "setRootForTab(", "a Settings tab step does not open that tab's root page")
    forbid(tab, "% kTabCount", "the tab step wraps")

    # Scrolling: one row per press, each screen's own rows, the release clamp.
    scr = block(maincpp, "static void keynavScroll(om::keynav::Context ctx, int dir)",
                "static void cardKbArrow")
    need(scr, "om::keynav::keyScroll(", "the lists are not scrolled through the tested rule")
    need(scr, "om::nodes_tab::scrollByRow(dir)", "the Nodes list is not scrolled")
    need(scr, "om::rptr_tab::scrollByRow(dir)", "RPTR is not scrolled")
    need(scr, "om::ui::kSelRowH", "the chat selector does not use its own row height")
    need(scr, "g_setMaxScroll", "Settings pages have no ceiling")
    need(scr, "g_chatMaxScroll", "the chat band has no ceiling")
    need(scr, "g_selMaxScroll", "the chat selector has no ceiling")
    need(scr, "-dir", "the bottom-anchored chat band needs the opposite sign")
    # Item 5: a text field only scrolls the chat behind a composer, never a Settings page or RPTR.
    text = block(scr, "if (ctx == om::keynav::Context::TextField)",
                 "if (g_settings) {")
    need(text, "g_tab != 1", "a Settings editor or the RPTR sheet would scroll something")
    if "g_setScroll" in text or "rptr_tab::scrollByRow" in text or "nodes_tab::scrollByRow" in text:
        fail("a text field must not scroll a Settings page, the RPTR list or Nodes")
    nodes = block(nodesc, "void scrollByRow(int dir)", "int count()")
    need(nodes, "om::ui::listSnapScroll(", "the Nodes key scroll does not use the release clamp")
    need(nodes, "om::ui::kNodeInfoRowH", "the Info page does not step its own rows")
    need(nodes, "om::ui::kRowH", "the Nodes list does not step its own rows")
    need(nodes, "g_nodeMenu >= 0 || g_nodeTraceOpen", "a Nodes sheet would still scroll")
    if "listGiveScroll" in nodes:
        fail("a key press must not stretch past the end (no give without a finger)")
    need(nodesh, "void scrollByRow(int dir);", "the Nodes module does not expose the key scroll")
    rptr = block(rptrc, "void scrollByRow(int dir)", "void releaseSnap(bool dragging)")
    need(rptr, "ui::listSnapScroll(", "the RPTR key scroll does not use the release clamp")
    need(rptr, "ui::kRptrRowH", "the RPTR list does not step its own rows")
    need(rptr, "ui::kRptrPageRowH", "an RPTR page does not step its own rows")
    need(rptr, "g_rptrOsk || g_rptrAskOpen || g_rptrMapPick",
         "an RPTR sheet, confirmation or picker would still scroll")
    need(rptrh, "void scrollByRow(int dir);", "the RPTR module does not expose the key scroll")

    # --- the map: one fixed step, through the drag's own path --------------------------------
    need(chromh, "constexpr int16_t kMapKeyPanX = (int16_t)(kMapCanvasW / 4);",
         "the key pan step is not a quarter of the canvas")
    need(chromh, "constexpr int16_t kMapKeyPanY = (int16_t)(kMapCanvasH / 4);",
         "the key pan step is not a quarter of the canvas")
    pan = block(maptabc, "void panStep(int dx, int dy)", "void zoomStep(int delta)")
    need(pan, "clearSelection();", "a key pan does not close the callout like a drag")
    need(pan, "s_lock = false;", "a key pan does not drop the follow lock like a drag")
    need(pan, "om::mapPan(dx, dy)", "a key pan does not move the map through mapPan")
    need(pan, "draw();", "a key pan does not redraw (traces and markers re-project in draw)")
    if "om::mapSetView" in pan:
        fail("a key pan must not recentre the view: it is a pan, not a jump")
    zoom = block(maptabc, "void zoomStep(int delta)", "void showPoint")
    need(zoom, "om::mapZoomBy(delta)", "the key zoom does not use the tap path's zoom")
    need(zoom, "draw();", "the key zoom does not redraw")
    need(maptabh, "void panStep(int dx, int dy);", "map_tab does not expose the key pan")
    need(maptabh, "void zoomStep(int delta);", "map_tab does not expose the key zoom")
    # The shell's own mapPan callers stay the two drag paths (the Map and the own-position picker);
    # a key pan is the module's, through map_tab::panStep.
    if maincpp.count("om::mapPan(g_lastX - (int)x") != 1:
        fail("the drag's one pan call moved or multiplied")
    if "om::mapPan(" in key:
        fail("a key pan must go through map_tab::panStep, not call mapPan from the shell")
    if maincpp.count("om::mapPan(") != 2:
        fail(f"the shell should keep exactly its two drag pan calls, found "
             f"{maincpp.count('om::mapPan(')}")

    if FAIL == 0:
        print("est417: pin order, unplugged keyboard, gate, arrow routing, tab step, scrolls, map pan")
        return 0
    print(f"est417: {FAIL} failure(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
