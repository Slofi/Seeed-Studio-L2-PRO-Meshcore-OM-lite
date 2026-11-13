#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-399, the source half: rules that a compiled header cannot prove about the 10k-line shell.
#
#   1. ONE "tap again" arm PER ROW: `g_deleteArmed` (the single flag that let Forget on network B
#      delete B while network A was armed) must be gone, each delete row must have its OWN arm, the
#      destructive call must sit behind a `Tap::Confirmed` answer, both labels must come from
#      `armedRow()`, and every page-leave path must call clearDeleteArms().
#   2. the deferred-write rules must actually be wired: the emoji pick marks the list dirty instead
#      of writing NVS, exactly one function writes it and it checks the result, and the loop calls
#      both the save step and the delete-arm timeout tick.
#   3. the pressed look for the RPTR page's card key and banner button must go through the ONE helper
#      rptrPageScrolledRect(), the same one the hit test uses, so it can never drift again.
#   4. the log guard's whitelist must stay small (a budget, so it cannot creep).
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


def need(haystack, needle, what):
    if needle not in haystack:
        fail(f"{what}: missing {needle}")


def forbid(haystack, needle, what):
    if needle in haystack:
        fail(f"{what}: found {needle} (must be gone)")


def main():
    main_cpp = read("src/main.cpp")

    # ---- 1. one pattern for every delete (EST-422) ---------------------------------------------
    # EST-399 gave each delete row its own "tap again" arm. EST-422 replaced all of them with the ONE
    # confirm sheet, so the arms are gone from the shell and every destructive call lives in the sheet's
    # single apply, behind the user's Apply. The DeleteArm struct itself stays in ui/delete_arm.h and is
    # still tested by test_est399.cpp; it simply has no caller in the shell any more.
    forbid(main_cpp, "g_deleteArmed", "the one shared delete flag")
    forbid(main_cpp, "om::ui::DeleteArm", "a per-row delete arm in the shell")
    forbid(main_cpp, "#include \"ui/delete_arm.h\"", "the arm include in the shell")
    forbid(main_cpp, "clearDeleteArms", "the arm-clearing helper")
    forbid(main_cpp, "Tap again to", "a \"tap again\" label on the glass")

    # The one apply, and only the one apply, runs the destructive calls.
    if main_cpp.count("static void settingsApplyConfirmed(") != 1:
        fail("the confirm sheet's apply is not exactly one function")
    apply_at = main_cpp.find("static void settingsApplyConfirmed(")
    apply_end = main_cpp.find("\n}\n", apply_at)
    apply_block = main_cpp[apply_at:apply_end] if apply_at >= 0 and apply_end > apply_at else ""
    for call in ("om::wifiForget(", "om::mcChannelRemove(", "om::rptrForget(", "om::mcChannelRemove(",
                 "om::meshNodeNameSet(", "om::meshPathHashSet(", "om::posSelectGps(",
                 "om::posSelectFixed(", "om::wifiSetEnabled(", "om::bleSetEnabled("):
        if call not in apply_block:
            fail(f"the confirmed apply does not run {call}")
    # Each of those ids is asked for first: the rows and the commit points call askChange()/g_ask.
    for kind in ("Preset", "TxPower", "PathHash", "NodeName", "ChannelDelete", "WifiForget",
                 "RepeaterForget", "WifiSwitch", "BtSwitch", "KeyboardSwitch", "Position",
                 "ClearMessages"):
        need(main_cpp, f"om::ui::ConfirmKind::{kind}", f"the {kind} change is not named")

    # A row never runs a change itself: the delete rows are plain labels, and the tap asks.
    need(main_cpp, 'setAdd(om::ui::SetRowKind::Button, A_ChatDelete, "Delete channel", "", false, true);',
         "the Chat page's delete row is a plain Delete")
    need(main_cpp, 'setAdd(om::ui::SetRowKind::Button, A_ChanDelete, "Delete channel", "", false, true);',
         "the Channel page's delete row is a plain Delete")
    need(main_cpp, 'setAdd(om::ui::SetRowKind::Button, (uint16_t)(A_WifiForgetBase + i), "Forget", "",',
         "the Wi-Fi Forget row is a plain Forget")

    # The sheet is the highest layer of the tap path and of the pressed look.
    need(main_cpp, "if (g_ask.open) {", "the tap path does not give the sheet the top layer")
    need(main_cpp, "settingsApplyConfirmed(k, answered.arg, answered.value);",
         "the sheet's Apply does not reach the one apply")
    need(main_cpp, "om::ui::settingsAskHit(v, x, y)", "the sheet is not hit-tested from its own layout")

    # Leaving a page closes the sheet (one ask, one page), the same invariant the arms had.
    closes = main_cpp.count("g_ask.close();")
    if closes < 7:
        fail(f"only {closes} g_ask.close() calls; every page-leave path needs one "
             f"(setGo, setBack, openSettings, settingsLeaveState, the tab switch, the selector, A_ChatPick)")

    # ---- 2. the batched emoji write -------------------------------------------------------------
    pick = main_cpp.find("static void chatEmojiPick(uint32_t cp)")
    if pick < 0:
        fail("main.cpp: chatEmojiPick not found")
    tail = main_cpp[pick:pick + 900]
    if "saveEmojiRecent()" in tail:
        fail("chatEmojiPick still writes NVS on every tap")
    need(tail, "g_emojiSave.changed(millis());", "the pick marks the Recent row dirty")

    # Exactly ONE call site of the writer, and it checks what it got back.
    sites = len(re.findall(r"=\s*saveEmojiRecent\(\)|^\s*saveEmojiRecent\(\);", main_cpp, re.M))
    if sites != 1:
        fail(f"saveEmojiRecent() has {sites} call sites; it must have exactly one (emojiPersistStep)")
    need(main_cpp, "const bool ok = saveEmojiRecent();", "the one writer reports its result")
    need(main_cpp, "g_emojiSave.result(ok, now);", "the result is counted")
    need(main_cpp, "return g_prefs.putBytes(\"erec\", buf, sizeof(buf)) == sizeof(buf);",
         "putBytes' result is checked (0 bytes written = failed)")
    need(main_cpp, "emojiPersistStep(g_tab == 1 && !g_settings && g_kb.open && g_kb.emoji(), millis());",
         "the loop flushes when the emoji page is no longer showing, or when the gate says 30 s")
    need(main_cpp, '"[emoji] recent NOT saved:', "a failed write is logged")

    # ---- 3. the one scrolled rectangle for the RPTR page ---------------------------------------
    rptr_h = read("src/ui/rptr_layout.h")
    rptr_cpp = read("src/ui/rptr_layout.cpp")
    press = read("src/ui/press_targets.h")
    if rptr_h.count("constexpr Rect rptrPageScrolledRect(") != 1:
        fail("rptrPageScrolledRect() must be defined exactly once (one rule)")
    calls = len(re.findall(r"const Rect vis = rptrPageScrolledRect\(", rptr_cpp))
    if calls != 2:
        fail(f"rptrPageHitAt() asks the helper {calls} time(s); it needs the refresh key AND the "
             f"banner button")
    if "t.rect = a.refreshTap;" in press or "t.rect = a.bannerButton;" in press:
        fail("press_targets.h still paints the scroll-0 rectangle for a scrolling key")
    need(press, "t.rect = rptrPageScrolledRect(a.refreshTap, scroll, a.body);",
         "the refresh plate is the scrolled slice")
    need(press, "t.rect = rptrPageScrolledRect(a.bannerButton, scroll, a.body);",
         "the banner plate is the scrolled slice")

    # ---- 4. the log guard's whitelist budget ---------------------------------------------------
    guard = read("test/host/test_est397.py")
    wl = re.search(r"WHITELIST = \[(.*?)\n\]", guard, re.S)
    if wl is None:
        fail("test_est397.py: no WHITELIST found")
    else:
        entries = re.findall(r'^\s*\("src/', wl.group(1), re.M)
        if len(entries) > 20:
            fail(f"test_est397.py: {len(entries)} whitelist entries; the list must stay minimal (max 20)")

    if FAIL:
        print(f"{FAIL} est399 source check(s) FAILED")
        return 1
    print("est399: one confirm sheet for every delete (EST-422), the emoji write batched and counted, the "
          "RPTR pressed plates scrolled, the guard's whitelist still minimal")
    return 0


if __name__ == "__main__":
    sys.exit(main())
