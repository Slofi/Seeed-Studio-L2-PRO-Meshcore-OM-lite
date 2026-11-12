#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-414, the source half: the five glass-test changes that are wiring plus the one-copy rules the
# compile cannot state. The arithmetic is test_est414.cpp; this file pins:
#
#   1. the indicator is ONE helper (one definition of drawScrollIndicator, one of scrollIndicatorFor)
#      and the three lists of the ticket each call it with their own layout rectangle;
#   2. the end bounce is ONE rule (listGiveScroll / listSnapScroll in layout.h): the exact give line
#      appears once in src/, the old names forward, and the Nodes list no longer clamps the give away
#      in refreshNodes;
#   3. opening the RPTR tab does NOT scan (EST-362's line is gone), while the Scan chip still can;
#   4. Settings > Channels shows no unread count, and the Chat tab's own unread is untouched;
#   5. Settings > RPTR > "Open repeater" is a live Chevron while a session is open (tapping it leaves
#      Settings for the RPTR tab) and stays a plain "none" value with no action when not.
#
# Run it against another tree with `python3 test_est414.py /path/to/tree` (that is how it is armed:
# against the tree before this ticket it must exit non-zero).
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FAIL = 0


def fail(msg):
    global FAIL
    print("FAIL", msg)
    FAIL += 1


def need(haystack, needle, what):
    if needle not in haystack:
        fail(f"{what}: missing {needle!r}")


def forbid(haystack, needle, what):
    if needle in haystack:
        fail(f"{what}: found {needle!r} (must be gone)")


def count(haystack, needle):
    return haystack.count(needle)


def block(text, start, end):
    """The text from `start` to the first `end` after it ('' when start is absent)."""
    i = text.find(start)
    if i < 0:
        return ""
    j = text.find(end, i + len(start))
    return text[i:j] if j >= 0 else text[i:]


def main(argv):
    root = pathlib.Path(argv[1]).resolve() if len(argv) > 1 else ROOT
    src = root / "src"

    def read(rel):
        """A file of the tree under test; a missing one is a FAIL, not a crash (the pre-EST-414 tree
        has no ui/scroll_indicator.*, and this test still has to name every finding there)."""
        p = src / rel
        if not p.exists():
            fail(f"{rel}: file is missing (this tree predates the change)")
            return ""
        return p.read_text(encoding="utf-8")

    main_cpp = read("main.cpp")
    nodes_tab = read("screens/nodes_tab.cpp")
    rptr_tab = read("screens/rptr_tab.cpp")
    screen_nodes = read("ui/screen_nodes.cpp")
    screen_chat = read("ui/screen_chat.cpp")
    chat_selector = read("ui/chat_selector.cpp")
    indicator_h = read("ui/scroll_indicator.h")
    indicator_cpp = read("ui/scroll_indicator.cpp")
    layout_h = read("ui/layout.h")
    chat_layout_cpp = read("ui/chat_layout.cpp")
    settings_layout_h = read("ui/settings_layout.h")
    node_info_layout_h = read("ui/node_info_layout.h")
    theme_h = read("ui/theme.h")

    # --- 1. one indicator, three lists ---------------------------------------------------------------
    # One arithmetic (the header, host-tested) and one drawing (the .cpp): a copy in a screen would
    # make a second set of pixels, which is exactly what the ticket forbids.
    if count(indicator_h, "ScrollIndicator scrollIndicatorFor(") != 1:
        fail("scrollIndicatorFor: must be defined exactly once (ui/scroll_indicator.h)")
    if count(indicator_cpp, "ScrollIndicator scrollIndicatorFor(") != 0:
        fail("scrollIndicatorFor: the arithmetic must not be repeated in the .cpp")
    if count(indicator_cpp, "void drawScrollIndicator(") != 1:
        fail("drawScrollIndicator: must be defined exactly once (ui/scroll_indicator.cpp)")
    # No screen may draw a track of its own: the thumb colour is used in the one helper and nowhere
    # else in src/.
    src_all = [p for p in src.rglob("*") if p.suffix in (".h", ".cpp")]
    thumb_users = [str(p.relative_to(root)) for p in src_all
                   if "kScrollThumb" in p.read_text(encoding="utf-8")]
    if sorted(thumb_users) != ["src/ui/scroll_indicator.cpp", "src/ui/theme.h"]:
        fail(f"the thumb colour is used outside the one helper: {thumb_users}")
    for name, text in (("nodes list", screen_nodes), ("message view", screen_chat),
                       ("channel list", chat_selector)):
        if count(text, "drawScrollIndicator(d, ") != 1:
            fail(f"{name}: exactly one indicator call expected")

    # The three lists of the ticket, each passing its own layout rectangle.
    need(screen_nodes, "drawScrollIndicator(d, scrollIndicatorFor(body", "nodes list: indicator call")
    need(screen_nodes, "const Rect body = L.listBody();", "nodes list: geometry from the layout")
    need(screen_chat, "drawScrollIndicator(d, scrollIndicatorFor(g.msgs",
         "message view: indicator call")
    need(chat_selector, "drawScrollIndicator(d, scrollIndicatorFor(list",
         "channel list: indicator call")

    # Only when the content does not fit, and never over text (the reserved-margin decision).
    need(indicator_h, "contentTotal <= contentVisible", "indicator: hidden when the content fits")
    need(indicator_h, "viewport.right() - kScrollTrackW", "indicator: track at the right edge")
    need(indicator_h, "kScrollMinThumbH", "indicator: minimum thumb")
    need(theme_h, "constexpr int16_t kScrollTrackW    = 4;", "theme: the 4 px track")
    need(theme_h, "constexpr uint16_t kScrollThumb    = kAcc;", "theme: the gold thumb")
    need(theme_h, "constexpr uint16_t kScrollTrack    = kLine;", "theme: the track colour")
    # The bottom-anchored chat band must ask for the from-end position, and the top-down lists must
    # not: the two calls below are the only place the flag is decided.
    need(screen_chat, "msgMaxScroll(g.msgs, heights, n), true))",
         "message view: bottom-anchored (thumb at the bottom at rest)")
    need(screen_nodes, "L.maxScroll(view.count), false));",
         "nodes list: top-down position")
    need(chat_selector, "selMaxScroll(list, m.channelCount, m.dmCount), false));",
         "channel list: top-down position")

    # --- 2. one bounce rule --------------------------------------------------------------------------
    give = "if (next < -kSelGive) next = -kSelGive;"
    if count(layout_h, give) != 1:
        fail("the give line must be in ui/layout.h exactly once")
    src_copy = "\n".join([layout_h, chat_layout_cpp, settings_layout_h, node_info_layout_h])
    if count(src_copy, give) != 1:
        fail("a second copy of the give arithmetic is still in the tree")
    if count(src_copy, "constexpr int listGiveScroll(") != 1:
        fail("listGiveScroll must be declared once")
    need(layout_h, "constexpr int listSnapScroll(", "the snap rule")
    # Every old name forwards to it, so no caller keeps its own numbers.
    need(chat_layout_cpp, "return listGiveScroll(base, dyUp, maxScroll);",
         "selDragScroll forwards to the one rule")
    need(settings_layout_h, "return listGiveScroll(base, dyUp, maxScroll);",
         "settingsDragScroll forwards to the one rule")
    need(settings_layout_h, "return listSnapScroll(v, maxScroll);",
         "settingsSnapScroll forwards to the one rule")
    need(node_info_layout_h, "return listGiveScroll(base, dyUp, maxScroll);",
         "nodeInfoDragScroll forwards to the one rule")
    need(node_info_layout_h, "return listSnapScroll(v, maxScroll);",
         "nodeInfoSnapScroll forwards to the one rule")
    # The three lists of the ticket use it: Nodes, the channel list, the message band.
    need(nodes_tab, "om::ui::listGiveScroll(g_nodeScroll0, dy, maxScroll)",
         "nodes list: the shared give")
    need(nodes_tab, "om::ui::listSnapScroll(g_nodeScroll, maxScroll)",
         "nodes list: the shared snap")
    need(main_cpp, "om::ui::listGiveScroll(g_chatScroll0, -dy, g_chatMaxScroll)",
         "message view: the shared give, bottom-anchored sign")
    need(main_cpp, "om::ui::listSnapScroll(g_chatScroll, g_chatMaxScroll)",
         "message view: the shared snap")
    need(main_cpp, "om::ui::listGiveScroll(g_selScroll0, dy, g_selMaxScroll)",
         "channel list: the shared give")
    need(main_cpp, "om::ui::listSnapScroll(g_selScroll, g_selMaxScroll)",
         "channel list: the shared snap")
    # The Nodes list's own bug: refreshNodes clamped the give away, which is why it stopped hard at
    # its last row. It now keeps the give WHILE THE DRAG IS LIVE and clamps absolutely otherwise, so a
    # contact table that shrinks while nobody drags cannot leave a gap either.
    refresh = block(nodes_tab, "static void refreshNodes()", "\n}\n")
    if not refresh:
        fail("refreshNodes() not found")
    else:
        forbid(refresh, "if (g_nodeScroll > maxScroll) g_nodeScroll = maxScroll;",
               "nodes list: the hard stop at the last row")
        need(refresh, "g_nodeScroll = g_nodeDrag ? om::ui::listGiveScroll(0, g_nodeScroll, maxScroll)",
             "nodes list: the refresh keeps the drag's give")
        need(refresh, ": om::ui::listSnapScroll(g_nodeScroll, maxScroll);",
             "nodes list: and clamps absolutely outside a drag")
    need(nodes_tab, "g_nodeDrag = false;   // the drag is over", "nodes list: the drag flag ends on release")
    # ... and a new gesture starts clamped, so an interrupted drag (a screen change before the finger
    # lifts) cannot leave the flag true and stop the next refresh from snapping.
    begin = block(nodes_tab, "void beginDrag() {", "\n}\\n")
    if not begin:
        fail("nodes_tab beginDrag() not found")
    else:
        need(begin, "g_nodeDrag = false;", "nodes list: a new gesture starts inside the bounds")
    # The two chat lists have the same "the view clamps every frame" problem, so drawChat keeps the
    # give only while g_listGive is set and snaps absolutely otherwise.
    draw_chat = block(main_cpp, "static void drawChat()", "\n}\n")
    if not draw_chat:
        fail("drawChat() not found")
    else:
        need(draw_chat, "if (g_listGive) {", "drawChat: the drag's give is kept")
        need(draw_chat, "g_chatScroll = om::ui::listGiveScroll(0, g_chatScroll, g_chatMaxScroll);",
             "drawChat: the message band's give")
        need(draw_chat, "g_chatScroll = om::ui::listSnapScroll(g_chatScroll, g_chatMaxScroll);",
             "drawChat: the message band's absolute clamp")
        forbid(draw_chat, "if (g_chatScroll > g_chatMaxScroll) g_chatScroll = g_chatMaxScroll;",
               "drawChat: the old hard clamp")
    need(main_cpp, "static bool g_listGive = false;", "the one drag-give flag")
    if count(main_cpp, "g_listGive = true;") != 2:
        fail("g_listGive must be set by exactly the two chat drag branches")
    if count(main_cpp, "g_listGive = false;") < 3:
        fail("g_listGive must be cleared on the press and on both release clamps")
    # The old inline copy in the chat drag is gone.
    forbid(main_cpp, "constexpr int kGive = 10;", "message view: the local kGive copy")

    # --- 3. no automatic scan when the RPTR tab opens ------------------------------------------------
    forbid(main_cpp, "rptrScanStart", "tab open: no scan start anywhere in main.cpp")
    forbid(main_cpp, "[rptr] auto scan", "tab open: the auto-scan log line")
    forbid(main_cpp, "om::rptrScan()", "tab open: the scan state is no longer read there")
    # ... and the Scan chip is still the one door, with everything else kept.
    need(rptr_tab, "case om::ui::RptrHit::Scan:", "the Scan chip still scans")
    need(rptr_tab, "if (!om::rptrScanStart())", "the Scan chip still calls the link")
    need(main_cpp, "om::rptrRememberedCount()", "Settings > RPTR: remembered passwords kept")
    need(rptr_tab, "om::rptrAutoLogin()", "the one-tap auto-login is kept")
    need(rptr_tab, "found.rows[i].name", "the last scan's list is still read")

    # --- 4. Settings > Channels has no unread count --------------------------------------------------
    channels = block(main_cpp, "case SetPage::Channels:", "case SetPage::Channel:")
    if not channels:
        fail("SetPage::Channels block not found")
    else:
        forbid(channels, "meshUnread", "Settings > Channels: the unread count")
        forbid(channels, '"+%d"', "Settings > Channels: the \"+2\" text")
        need(channels, 'snprintf(v, sizeof(v), "ON");', "Settings > Channels: ON/muted/OFF kept")
    # The Chat tab's own unread is untouched: the tab badge, the chat model's count and the selector
    # row's dot/count all still read it.
    need(main_cpp, "tabBarModel(active, unreadTotal()", "Chat tab: the unread badge")
    need(main_cpp, "c.unread = unread > 0 ? (uint16_t)unread : 0;", "Chat tab: per-chat unread")
    need(main_cpp, "om::meshUnread(i)", "the chat list still reads each chat's unread")

    # --- 5. Settings > RPTR > Open repeater ----------------------------------------------------------
    need(main_cpp, "A_RptrOpen,", "the action id")
    rptr_root = block(main_cpp, "case SetPage::RptrRoot:", "case SetPage::Preset:")
    if not rptr_root:
        fail("SetPage::RptrRoot block not found")
    else:
        need(rptr_root, "if (openRptr[0]) {", "open repeater: the two states")
        need(rptr_root, 'setAdd(om::ui::SetRowKind::Chevron, A_RptrOpen, "Open repeater", openRptr);',
             "open repeater: a live Chevron while a session is open")
        need(rptr_root, 'setAdd(om::ui::SetRowKind::Value, A_None, "Open repeater", "none");',
             "open repeater: a plain none row when nothing is open")
    handler = block(main_cpp, "case A_RptrOpen:", "case A_ChatOpen:")
    if not handler:
        fail("case A_RptrOpen not found")
    else:
        need(handler, "settingsLeaveState();", "open repeater: leaves Settings")
        need(handler, "showTab(3);", "open repeater: jumps to the RPTR tab")
        forbid(handler, "rptr_tab::open(", "open repeater: it must NOT re-open a session")
        forbid(handler, "clearSession", "open repeater: the session must survive")
        forbid(handler, "rptrScanStart", "open repeater: no scan on the way in")
    # Leaving Settings is one helper now, used by Back too, so a jump cannot leave a sheet behind.
    need(main_cpp, "static void settingsLeaveState()", "one way out of Settings")
    need(block(main_cpp, "static void settingsBack()", "\n}\n"), "settingsLeaveState();",
         "Back still goes through the same helper")

    if FAIL == 0:
        print("est414: one indicator on all three lists, one bounce rule, no tab-open scan, "
              "Settings row fixes")
        return 0
    print(f"est414: {FAIL} failure(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
