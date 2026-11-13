#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-412: the source half of the CardKB ticket. The C++ half (test_est412.cpp) proves the mapping,
# the OFF default and the polling gate; this file pins what the shell actually does with them:
#  * Grove power is the ONLY thing `grove on|off|status` touches, and the switch row exists with the
#    OFF default and the saved key;
#  * the enable path opens a SECOND bus (Wire1) and probes 0x5F exactly twice, one pin order each;
#  * the poll is the one call site, inside the loop, behind shouldPoll(switch, found) (EST-417: the
#    text field no longer gates it);
#  * the key routing uses mapCode(..., false) and the on-screen doors (composeAppend/Backspace,
#    chatSendDraft, settingsEditAppendChar, settingsEditorOk, settingsBack, the RPTR sheet API);
#  * no heap allocation lives in the keyboard's driver.
#
# Run it alone against another tree with `python3 test_est412.py /path/to/tree`.
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
    usbc = (src / "usb_commands.cpp").read_text(encoding="utf-8")
    usbparse = (src / "usb_cmd_logic.cpp").read_text(encoding="utf-8")
    usbh = (src / "usb_cmd_logic.h").read_text(encoding="utf-8")
    boardh = (src / "board.h").read_text(encoding="utf-8")
    boardc = (src / "board.cpp").read_text(encoding="utf-8")
    rptrh = (src / "screens/rptr_tab.h").read_text(encoding="utf-8")
    rptrc = (src / "screens/rptr_tab.cpp").read_text(encoding="utf-8")

    # --- the safety command: expander bit 7 and nothing else --------------------------------
    need(usbh, "Grove,", "the command language has no grove verb")
    need(usbparse, 'if (strcmp(verb, "grove") == 0)', "grove is not parsed")
    for word in ('"on"', '"off"', '"status"'):
        need(block(usbparse, 'if (strcmp(verb, "grove") == 0)',
                   'if (strcmp(verb, "tap") == 0)'), word,
             f"grove does not accept {word}")
    grove = block(usbc, "case om::usb::Kind::Grove:", "case om::usb::Kind::KeyWake:")
    if not grove:
        fail("the grove command has no handler")
    need(grove, "boardGrovePower(on)", "the handler does not switch the expander bit")
    need(grove, "boardGrovePowerOn()", "grove status does not read the latched level")
    forbid(grove, "Wire1", "grove on|off must not open the second bus")
    forbid(grove, "cardkb::begin", "grove on|off must not probe or enable the keyboard")
    forbid(usbc, "Wire1.", "the USB command module must not touch the Grove bus")
    need(boardh, "bool boardGrovePower(bool on)", "board.h has no Grove power setter")
    need(boardc, "setOutput(kBitGrovePower, on)", "the setter does not write expander bit 7")

    # --- the enabled path: ONE second bus, the two pin orders, no more -----------------------
    need(driver, "constexpr uint8_t kAddr = 0x5F", "the driver does not probe 0x5F")
    need(driver, "constexpr uint32_t kBusHz = 100000", "the bus is not 100 kHz")
    need(driver, "Wire1.begin(sda, scl, kBusHz)", "the driver does not open Wire1")
    # EST-421: the probes moved out of begin() into the start window (one attempt per call), so the
    # two measured orders now live in step(); the window itself is test_est421's subject.
    need(driver, "openAndProbe(sda, scl)", "the start window never probes the keyboard")
    need(driver, "w.sda13First ? kPinGroveDat2 : kPinGroveDat1",
         "SDA = 13 is no longer the first order")
    need(driver, "w.sda13First ? kPinGroveDat1 : kPinGroveDat2",
         "the (14, 13) fallback order is missing")
    if driver.count("openAndProbe(") != 2:   # the helper's definition and its one call
        fail(f"the enable path must have one probe call site, found "
             f"{driver.count('openAndProbe(')} mentions")
    need(driver, "[kb] cardkb found", "the found log line is missing")
    need(driver, "[kb] cardkb not found after", "the not-found log line is missing")
    # Review fix (Claude): no rescan, so a not-found window must drop the 5 V rail again. EST-421:
    # that now happens at the end of the 3000 ms window, before the log line.
    timeout = block(driver, "case WindowAction::TimedOut:", "case WindowAction::Probe:")
    need(timeout, "boardGrovePower(false);", "a timed-out window leaves the Grove 5 V rail on")
    if timeout.find("boardGrovePower(false)") > timeout.find("[kb] cardkb not found after"):
        fail("the not-found log comes before the Grove rail is dropped")
    need(boardh, "kPinGroveDat1 = 14", "Grove DAT_1 is not GPIO14")
    need(boardh, "kPinGroveDat2 = 13", "Grove DAT_2 is not GPIO13")
    forbid(driver, "malloc(", "the keyboard driver must not allocate")
    forbid(driver, "heap_caps_", "the keyboard driver must not allocate from the heap")
    forbid(driver, "xTaskCreate", "the keyboard driver must not create a task")

    # --- the switch: OFF default, saved key, row and both enable/disable paths --------------
    need(maincpp, "static bool g_kbEnabled = om::cardkb::kDefaultEnabled;",
         "the switch is not initialised from the one OFF default")
    row = block(maincpp, "EST-412: the M5 CardKB on the back Grove port",
                'setSection("SOUND");')
    need(row, 'A_GroveKeyboard, "Keyboard (Grove)"',
         "the General page has no Keyboard (Grove) row")
    need(row, "g_kbEnabled", "the row does not show the live switch")
    need(maincpp, '"grovekb"', "the switch is not saved in NVS")
    boot = block(maincpp, "EST-412: the Grove keyboard switch.", "EST-405: the screen auto-off")
    need(boot, "enabledFromNvs(present, saved)", "boot does not use the missing-key rule")
    need(boot, "cardkb::begin()", "a saved ON does not enable the keyboard")
    # EST-422: the switch asks first, so the row only opens the shared confirm sheet; the ON/OFF work
    # (the NVS write, begin/end) lives in the one apply, under ConfirmKind::KeyboardSwitch.
    toggle = block(maincpp, "case A_GroveKeyboard:", "case A_Sound:")
    need(toggle, "ConfirmKind::KeyboardSwitch", "the switch does not ask in the shared sheet")
    apply_kb = block(maincpp, "case om::ui::ConfirmKind::KeyboardSwitch:", "case om::ui::ConfirmKind::Position:")
    need(apply_kb, "cardkb::begin()", "the ON action does not enable the keyboard")
    need(apply_kb, "cardkb::end()", "the OFF action does not close the bus and the power")
    need(apply_kb, 'mapLayerPrefWrite(nullptr, "grovekb", g_kbEnabled)',
         "the switch does not write the shared bool key")
    # EST-421: begin() no longer knows whether the keyboard answered, so the toggle cannot toast
    # "not found"; the loop's cardKbStep() does, after the 3000 ms window.
    forbid(apply_kb, 'showToast("CardKB not found")',
           "a non-blocking begin() cannot report a missing keyboard")

    # --- polling: one call site, in the loop, behind the gate --------------------------------
    field = block(maincpp, "static bool textFieldOpen()", "static om::IdleBlockers idleBlockersNow")
    need(field, "g_kb.open", "the gate does not see the chat composer")
    need(field, "rptr_tab::keyboardOpen()", "the gate does not see the RPTR editor")
    need(field, "g_setEditor != om::ui::SettingsEditor::None",
         "the gate does not see the Settings editors")
    step = block(maincpp, "static void cardKbStep(uint32_t nowMs)", "void loop()")
    need(step, "shouldPoll(g_kbEnabled, om::cardkb::found())",
         "the poll is not behind the switch + found gate")
    forbid(step, "shouldPoll(g_kbEnabled, om::cardkb::found(),",
           "the poll is still gated on a text field")
    need(step, "cardkb::poll(nowMs)", "cardKbStep does not read the keyboard")
    if maincpp.count("om::cardkb::poll(nowMs)") != 1:
        fail("the CardKB poll must have exactly one call site (the loop task)")
    if driver.count("cardkb::poll(") != 0 or usbc.count("cardkb::poll(") != 0:
        fail("the USB handler or the driver polls the keyboard")
    loopbody = block(maincpp, "void loop()", "// --- app_shell")
    need(loopbody, "cardKbStep(millis());", "the loop never calls the CardKB step")

    # --- routing: the same doors the on-screen keys use -------------------------------------
    key = block(maincpp, "static void cardKbKey(uint8_t code)", "static void cardKbStep")
    need(key, "om::cardkb::mapCode(code, false)", "the handler does not use the tested mapping")
    for door in ("om::ui::composeAppend(g_kbText", "om::ui::composeBackspace(g_kbText)",
                 "chatSendDraft()", "settingsEditAppendChar(k.ch)",
                 "om::ui::composeBackspace(settingsEditBuf())", "settingsEditorOk()",
                 "settingsBack()", "om::rptr_tab::keyboardText(k.ch)",
                 "om::rptr_tab::keyboardConfirm()", "om::rptr_tab::keyboardClose()"):
        need(key, door, f"the CardKB bypasses the on-screen door {door!r}")
    need(key, "g_kb.close()", "Esc does not close the chat sheet")
    need(key, "++g_kbIgnored", "an unknown code is not counted")
    need(key, "screenWake", "a key does not wake the screen like a touch")
    need(rptrh, "bool keyboardText(char ch)", "the RPTR sheet has no CardKB text door")
    need(rptrc, "bool keyboardText(char ch)", "the RPTR text door is not implemented")
    need(rptrc, "bool keyboardConfirm()", "the RPTR confirm door is not implemented")

    # --- the logic header keeps the tested rules --------------------------------------------
    need(logic, "constexpr bool kDefaultEnabled = false", "the default is not OFF")
    need(logic, "return enabled && found;", "the gate is not the switch + found AND")
    need(logic, "0xB4", "the left arrow code is missing")
    need(logic, "0xB7", "the right arrow code is missing")

    if FAIL == 0:
        print("est412: grove command, OFF switch, second bus, two probes and one gated poll")
        return 0
    print(f"est412: {FAIL} failure(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
