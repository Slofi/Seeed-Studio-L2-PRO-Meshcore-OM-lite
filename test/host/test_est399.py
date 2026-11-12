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

    # ---- 1. one arm per row ---------------------------------------------------------------------
    forbid(main_cpp, "g_deleteArmed", "the one shared delete flag")
    for arm in ("g_wifiForgetArm", "g_chatDeleteArm", "g_chanDeleteArm"):
        need(main_cpp, f"om::ui::DeleteArm {arm};", "main.cpp declares one arm per row family")
    need(main_cpp, "#include \"ui/delete_arm.h\"", "main.cpp includes the arm")

    # Every destructive call is guarded by the arm's own confirmation.
    for call, arm in (("om::wifiForget(", "g_wifiForgetArm"),
                      ("om::mcChannelRemove(g_chanPage)", "g_chanDeleteArm"),
                      ("om::mcChannelRemove(ch)", "g_chatDeleteArm")):
        idx = main_cpp.find(call)
        if idx < 0:
            fail(f"the destructive call {call} is missing")
            continue
        window = main_cpp[max(0, idx - 400):idx]
        if f"{arm}.tap(" not in window:
            fail(f"{call} is not behind {arm}.tap(...) == Confirmed")
        if "om::ui::DeleteArm::Tap::Confirmed" not in window:
            fail(f"{call} is not guarded by Tap::Confirmed")

    # The labels are the arm's own answer, so a label can never outlive the window.
    need(main_cpp, 'forgetArmed ? "Tap again to forget" : "Forget"', "the Wi-Fi row's own label")
    need(main_cpp, "g_wifiForgetArm.armedRow(millis()) == i", "the Wi-Fi label comes from the arm")
    need(main_cpp, 'chatDeleteArmed ? "Tap again to delete" : "Delete channel"', "the chat label")
    need(main_cpp, "g_chatDeleteArm.armedRow(millis()) == ch", "the chat label comes from the arm")
    need(main_cpp, 'chanDeleteArmed ? "Tap again to delete" : "Delete channel"', "the channel label")
    need(main_cpp, "g_chanDeleteArm.armedRow(millis()) == (int)g_chanPage",
         "the channel label comes from the arm")

    # Any other tap on the page disarms (the ticket's \"disarm on any other tap\").
    need(main_cpp, "if (id != A_ChatDelete) g_chatDeleteArm.clear();",
         "another row's action disarms the chat delete")
    need(main_cpp, "if (id != A_ChanDelete) g_chanDeleteArm.clear();",
         "another row's action disarms the channel delete")
    need(main_cpp, "if (id < A_WifiForgetBase || id >= A_WifiForgetBase + om::wifi::kMaxSaved) "
                   "g_wifiForgetArm.clear();",
         "another row's action disarms the Wi-Fi forget")

    # Leaving a page disarms every arm: the three leave paths must call the one clear function.
    # EST-414 moved settingsBack's leave-the-page clears into ONE helper, settingsLeaveState(), which
    # Back and the RPTR "Open repeater" row both call, so the check follows the call into the helper
    # and then proves Back still goes through it. The invariant is the same one EST-399 asked for.
    clears = main_cpp.count("clearDeleteArms();")
    if clears < 7:
        fail(f"only {clears} clearDeleteArms() calls; every page-leave path needs one "
             f"(setGo, setBack, openSettings, settingsLeaveState, the tab switch, the selector, "
             f"A_ChatPick)")
    for fn in ("static void setGo(SetPage p) {", "static void setBack() {",
               "static void openSettings(int mainTab, om::ui::SettingsTab tab) {",
               "static void settingsLeaveState() {"):
        idx = main_cpp.find(fn)
        if idx < 0:
            fail(f"main.cpp: {fn} not found")
            continue
        if "clearDeleteArms();" not in main_cpp[idx:idx + 2500]:
            fail(f"main.cpp: {fn} does not clear the delete arms on the way out")
    idx = main_cpp.find("static void settingsBack() {")
    if idx < 0:
        fail("main.cpp: static void settingsBack() { not found")
    elif "settingsLeaveState();" not in main_cpp[idx:idx + 2500]:
        fail("main.cpp: settingsBack() does not leave through settingsLeaveState()")

    # The timeout reaches the glass.
    need(main_cpp, "static void deleteArmTick(uint32_t now)", "the arm timeout tick exists")
    need(main_cpp, "deleteArmTick(millis());", "the loop runs the arm timeout tick")
    need(main_cpp, "if (expired && g_settings && g_setEditor == om::ui::SettingsEditor::None) drawSettings();",
         "an expired arm redraws the page it was shown on")

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
    print("est399: per-row delete arms wired end to end, the emoji write batched and counted, the "
          "RPTR pressed plates scrolled, the guard's whitelist still minimal")
    return 0


if __name__ == "__main__":
    sys.exit(main())
