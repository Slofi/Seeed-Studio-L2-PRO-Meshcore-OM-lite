#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-421: the source half of the CardKB start-window fix. The C++ half (test_est421.cpp) proves the
# window's timing; this file pins how the driver and the shell use it:
#  * `begin()` powers the rail and arms the window only: no probe, no settle, NO `delay(` anywhere in
#    the keyboard driver (the old fixed 50 ms is gone);
#  * `step(nowMs)` is the one place the window advances: it asks cardkb_logic.h's StartWindow, probes
#    0x5F in the order the step names (SDA 13 first on the even attempts), logs
#    `[kb] cardkb found (sda=.. scl=..) after N ms`, and at the window's end drops the rail, logs
#    `[kb] cardkb not found after 3000 ms, Grove power off` and asks for one toast;
#  * the switch OFF path cancels an open window;
#  * the shell calls step() once per loop pass, before the gate, and shows the ticket's toast;
#  * the poll stays the one call site behind shouldPoll(switch, found), so nothing is read while the
#    window is open (found() is false until the answer).
#
# Run it alone against another tree with `python3 test_est421.py /path/to/tree`.
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
    header = (src / "cardkb.h").read_text(encoding="utf-8")
    logic = (src / "cardkb_logic.h").read_text(encoding="utf-8")

    # --- the window's rules live in the tested header ----------------------------------------
    need(logic, "constexpr uint32_t kStartWindowMs = 3000", "the 3000 ms window is missing")
    need(logic, "constexpr uint32_t kProbeIntervalMs = 100", "the ~100 ms probe spacing is missing")
    need(logic, "class StartWindow", "there is no start-window state machine")
    need(logic, "s.sda13First = (probes_ % 2) == 0;", "the pin order does not alternate")
    need(logic, "s.elapsedMs >= kStartWindowMs", "the window has no timeout edge")
    need(logic, "void cancel() { active_ = false; }", "the window cannot be cancelled")
    need(logic, "dueMs_ = nowMs + kProbeIntervalMs;",
         "the next probe is not 100 ms after the attempt that really happened")

    # --- begin(): power and arm, nothing else, and no waiting --------------------------------
    begin = block(driver, "bool begin() {", "void step(uint32_t nowMs) {")
    if not begin:
        fail("the driver has no begin()")
    need(begin, "boardGrovePower(true)", "begin does not power the Grove rail")
    need(begin, "s_window.start(millis())", "begin does not arm the start window")
    forbid(begin, "openAndProbe", "begin still probes instead of leaving it to the loop task")
    forbid(begin, "delay(", "begin still waits for the keyboard")
    forbid(driver, "delay(", "the keyboard driver must never block")
    forbid(driver, "kSettleMs", "the old fixed settle is still in the driver")

    # --- step(): the one place the window advances -------------------------------------------
    step = block(driver, "void step(uint32_t nowMs) {", "void end() {")
    if not step:
        fail("the driver has no step()")
    need(step, "s_window.next(nowMs)", "step does not ask the tested state machine")
    need(step, "WindowAction::TimedOut", "step has no timeout branch")
    need(step, "WindowAction::Probe", "step has no probe branch")
    need(step, "openAndProbe(sda, scl)", "step does not probe 0x5F")
    need(step, "w.sda13First ? kPinGroveDat2 : kPinGroveDat1",
         "SDA 13 is not the first order (kPinGroveDat2)")
    need(step, "w.sda13First ? kPinGroveDat1 : kPinGroveDat2",
         "the alternate (14, 13) order is missing")
    need(step, "[kb] cardkb found (sda=%d scl=%d) after %lu ms",
         "the found log does not print the measured N")
    need(step, "s_window.cancel()", "a successful probe does not close the window")
    need(step, '"[kb] cardkb not found after %lu ms, Grove power off\\n"',
         "the not-found log is not the ticket's line")
    need(step, "s_notFoundNotice = true;", "the timeout does not ask for the toast")
    if step.find("boardGrovePower(false)") > step.find("[kb] cardkb not found after"):
        fail("the not-found log comes before the rail is dropped")
    if step.find("takeNotFoundNotice") >= 0:
        fail("the driver must not take its own notice (the shell does)")

    # --- the OFF path cancels a running window ----------------------------------------------
    end = block(driver, "void end() {", "bool found()")
    need(end, "s_window.cancel()", "switching OFF does not cancel an open window")
    need(end, "boardGrovePower(false)", "switching OFF does not drop the rail")
    need(end, "s_notFoundNotice = false;", "switching OFF mid-window would still toast")

    # --- the shell: one call per pass, before the gate, plus the one toast --------------------
    shell = block(maincpp, "static void cardKbStep(uint32_t nowMs)", "void loop()")
    need(shell, "om::cardkb::step(nowMs)", "the loop never advances the start window")
    if shell.count("om::cardkb::step(nowMs)") != 1:
        fail("the window must advance at exactly one place")
    gate = shell.find("shouldPoll(g_kbEnabled, om::cardkb::found())")
    if gate < 0:
        fail("the poll is not behind the switch + found gate")
    elif shell.find("om::cardkb::step(nowMs)") > gate:
        fail("the window must advance before the gate (found() is false meanwhile)")
    need(shell, "takeNotFoundNotice()", "the shell never takes the not-found notice")
    need(shell, 'showToast("CardKB not found (switch off/on to retry)")',
         "the ticket's not-found toast is missing")
    need(maincpp, "static bool g_kbEnabled = om::cardkb::kDefaultEnabled;",
         "the OFF default moved")
    if maincpp.count("om::cardkb::poll(nowMs)") != 1:
        fail("the CardKB poll must still have exactly one call site")

    boot = block(maincpp, "EST-412: the Grove keyboard switch.", "EST-405: the screen auto-off")
    need(boot, "cardkb::begin()", "a saved ON no longer enables the keyboard")
    need(boot, 'showToast("Grove power failed")', "an expander failure is not said out loud")
    # EST-422: the switch asks first; the ON/OFF work (and so begin()/end()) moved into the one apply.
    toggle = block(maincpp, "case A_GroveKeyboard:", "case A_Sound:")
    need(toggle, "ConfirmKind::KeyboardSwitch", "the switch does not ask in the shared sheet")
    apply_kb = block(maincpp, "case om::ui::ConfirmKind::KeyboardSwitch:",
                     "case om::ui::ConfirmKind::Position:")
    need(apply_kb, "cardkb::begin()", "the ON action does not arm the window")
    need(apply_kb, "cardkb::end()", "the OFF action does not cancel and power down")
    forbid(apply_kb, 'showToast("CardKB not found")',
           "a non-blocking begin() cannot know whether the keyboard answered")
    need(header, "void step(uint32_t nowMs);", "cardkb.h does not declare step()")
    need(header, "bool takeNotFoundNotice();", "cardkb.h does not declare the not-found notice")

    if FAIL == 0:
        print("est421: no delay in begin, one window step, alternating probes, after-N log, the timeout toast")
        return 0
    print(f"est421: {FAIL} failure(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
