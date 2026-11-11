#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-402, the source half: the charging bolt is fed by ONE read of STATUS0 every 2 s, it is debounced
# by charge_state.h, it is drawn only when the debounced flag is true, and an unknown flag can never
# raise it. The arithmetic is test_est402.cpp; this file reads the tree.
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
FAIL = 0


def fail(msg):
    global FAIL
    print("FAIL", msg)
    FAIL += 1


def strip_comments(text):
    """Remove // and /* */ comments: prose about a write is not a write."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def body_of(text, signature):
    """The {...} body after a function signature, by brace depth."""
    at = text.find(signature)
    if at < 0:
        return None
    open_at = text.find("{", at + len(signature))
    if open_at < 0:
        return None
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at:i + 1]
    return None


def main():
    files = {}
    for path in sorted(SRC.rglob("*.cpp")) + sorted(SRC.rglob("*.h")):
        files[path.relative_to(ROOT).as_posix()] = strip_comments(
            path.read_text(encoding="utf-8"))

    # --- 1. the rule itself ----------------------------------------------------------------
    charge = files.get("src/charge_state.h", "")
    if not charge:
        fail("src/charge_state.h is missing")
    for needle, what in (
        ("constexpr uint32_t kChargePollMs = 2000;", "the 2 s poll constant"),
        ("if (streak_ < 2) ++streak_;", "the two-equal-reads streak"),
        ("if (streak_ < 2) return false;", "one read is not a decision"),
        ("if (!ready_) return false;", "a controller that was not ready ignores every read"),
        ("bool bolt() const", "the bolt accessor"),
        ("bool unknown() const", "the unknown state is named"),
    ):
        if needle not in charge:
            fail(f"charge_state.h: {what} is missing ({needle})")
    # begin(false) must reset the state to Unknown, not to Absent or Present.
    begin = body_of(charge, "void begin(bool controllerReady)")
    if begin is None:
        fail("charge_state.h: begin() is missing")
    elif "state_ = VbusState::Unknown;" not in begin:
        fail("charge_state.h: begin() does not start from UNKNOWN")

    # --- 2. the shell's wiring --------------------------------------------------------------
    main_cpp = files.get("src/main.cpp", "")
    for needle, what in (
        ('#include "charge_state.h"', "the rule's include"),
        ("g_chargeBolt.begin(om::usbPdReady());", "the boot start from the real readiness"),
        ("s.externalPower = g_chargeBolt.bolt();", "buildStatus feeds the bar"),
        ("millis() - lastVbusPoll >= om::kChargePollMs", "the 2 s poll gate"),
        ("om::usbPdVbusPresent(&present)", "the one STATUS0 read"),
        ("if (g_chargeBolt.sample(read) && uiDrawOk) redrawCurrentScreen();",
         "a debounced change repaints the bar"),
    ):
        if needle not in main_cpp:
            fail(f"main.cpp: {what} is missing ({needle})")
    if main_cpp.count("om::usbPdVbusPresent(") != 1:
        fail(f"main.cpp: {main_cpp.count('om::usbPdVbusPresent(')} poll calls, expected 1")
    if main_cpp.count("om::usbPdVbusPresent(&present)") != 1:
        fail("main.cpp: the poll must read STATUS0 exactly once per pass")
    # The poll must be inside loop(), after the screen-change decision.
    loop = body_of(main_cpp, "void loop()")
    if loop is None:
        fail("main.cpp: loop() not found")
    elif "millis() - lastVbusPoll >= om::kChargePollMs" not in loop:
        fail("main.cpp: the 2 s poll is not in loop()")
    # Unknown must be fed as Unavailable, never as Absent (a failed read is not "no cable").
    if "!answered ? om::VbusRead::Unavailable" not in main_cpp:
        fail("main.cpp: a failed STATUS0 read is not mapped to Unavailable")

    # --- 3. the board layer ----------------------------------------------------------------
    board_cpp = files.get("src/board.cpp", "")
    if "static bool s_usbPdReady = false;" not in board_cpp:
        fail("board.cpp: s_usbPdReady is not a static initialised to false")
    if board_cpp.count("s_usbPdReady = ok;") != 1:
        fail("board.cpp: readiness is not set exactly once, from the attempt's result")
    if "bool usbPdReady() { return s_usbPdReady; }" not in board_cpp:
        fail("board.cpp: usbPdReady() is missing")
    vbus = body_of(board_cpp, "bool usbPdVbusPresent(bool* present)")
    if vbus is None:
        fail("board.cpp: usbPdVbusPresent() is missing")
    else:
        if vbus.count("usbPdStatus0(&status0)") != 1:
            fail("board.cpp: the poll does not make exactly one STATUS0 read")
        if "diag::status0VbusOk(status0)" not in vbus:
            fail("board.cpp: the poll does not decode VBUSOK")
        if "Wire." in vbus:
            fail("board.cpp: the poll touches Wire directly (it must go through usbPdStatus0)")
    # The boot line is printed once, in the failure branch only: the ticket says "log it once", so no
    # second copy may live in main.cpp or anywhere else.
    if board_cpp.count('"[pmu] charging bolt off (power flag unknown)"') != 1:
        fail("board.cpp: the one 'charging bolt off' boot line is missing or repeated")
    if sum(text.count("[pmu] charging bolt off") for text in files.values()) != 1:
        fail("the 'charging bolt off' line is logged more than once in src/")
    # The readiness must be assigned after the retry, not before the first attempt.
    pb = body_of(board_cpp, "void usbPdPowerBegin()")
    if pb is None:
        fail("board.cpp: usbPdPowerBegin() is missing")
    elif pb.find("s_usbPdReady = ok;") < pb.find("usbPdMeasureAttempt(&readBack, why, sizeof(why))"):
        fail("board.cpp: readiness is decided before the first attempt")

    # --- 4. the drawing --------------------------------------------------------------------
    widgets_h = files.get("src/ui/widgets.h", "")
    if "bool externalPower = false;" not in widgets_h:
        fail("widgets.h: StatusModel has no externalPower flag")
    widgets_cpp = files.get("src/ui/widgets.cpp", "")
    if "drawBattery(d, rects.battIcon.x, cy, m.batteryPct, m.externalPower);" not in widgets_cpp:
        fail("widgets.cpp: the status bar does not pass externalPower to the battery")
    batt = body_of(widgets_cpp, "void drawBattery(lgfx::LovyanGFX* d, int x, int cy, int pct,\n                 bool externalPower)")
    if batt is None:
        # The signature may be on one line; find it by its first line.
        batt = body_of(widgets_cpp, "void drawBattery(lgfx::LovyanGFX* d, int x, int cy, int pct")
    if batt is None:
        fail("widgets.cpp: drawBattery() not found")
    else:
        if "if (externalPower) drawBolt(d, x, y);" not in batt:
            fail("widgets.cpp: the bolt is not drawn under the externalPower flag")
    bolt = body_of(widgets_cpp, "void drawBolt(lgfx::LovyanGFX* d, int x, int y)")
    if bolt is None:
        fail("widgets.cpp: drawBolt() is missing")
    else:
        if "kAcc" not in bolt:
            fail("widgets.cpp: the bolt is not drawn in the theme accent")
        if "kBar" not in bolt:
            fail("widgets.cpp: the bolt has no bar-colour edge (gold on the fill is ~1.1:1)")
        if "kBoltRuns" not in bolt:
            fail("widgets.cpp: the bolt does not use the shared shape in ui/battery_bolt.h")
    # The shape and the sizes live where the ticket says they do.
    if "constexpr int16_t kBoltW" not in files.get("src/ui/theme.h", ""):
        fail("theme.h: kBoltW is missing")
    if "constexpr int16_t kBoltH" not in files.get("src/ui/theme.h", ""):
        fail("theme.h: kBoltH is missing")

    # --- 5. the ticket's "no charging vs full guess" --------------------------------------
    # The one place that could name a charge level is the bolt path; it must not read BC_LVL or the
    # battery trend. The comment in charge_state.h is stripped, so only code counts.
    for rel in ("src/main.cpp", "src/ui/widgets.cpp", "src/charge_state.h", "src/board.cpp"):
        text = files.get(rel, "")
        for word in ("bcLvl", "bc_lvl", "BC_LVL", "chargingFull", "isFull"):
            if word in text:
                fail(f"{rel}: {word!r} looks like a charging-vs-full guess")

    if FAIL == 0:
        print("est402 (source): 2 s STATUS0 poll -> debounce -> bolt; unknown latches off; no charge guess")
        return 0
    print(f"est402 (source): {FAIL} failure(s)")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
