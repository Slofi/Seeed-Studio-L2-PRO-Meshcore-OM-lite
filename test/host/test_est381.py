#!/usr/bin/env python3
"""EST-381: the frame that carries a toast is the frame that is pushed, on every screen.

The ticket: a message plays its sound on the Map but no pop-up appears (bars shown or tucked), while
Nodes, RPTR and Settings show it. Every map draw goes through drawMapFrame(), whose last act is
omFrameEnd() -> the overlay. A call graph is not something C++ can prove on a laptop, so this scans
the sources for the four facts that make that sentence true, and fails on the first drift:

  1. EVERY map draw entry (the steady frame, an animation frame, a slide frame) calls drawMapFrame(),
     drawMapFrame() ends with exactly one omFrameEnd() and nothing after it, and screen_map.cpp never
     pushes to the panel itself;
  2. omFrameEnd() draws the overlay BEFORE it pushes (board.cpp): one point, after the whole screen;
  3. the toast's press edge comes FIRST in the loop, it spends the gesture, and every map act is
     behind `!g_spent` (the consumed tap: no marker, no chip, no pan, no callout);
  4. the notice door has no Map special case: main.cpp asks `chatOpenForChannel/Thread` (false off the
     Chat tab) and the USB `notify test` hook calls the same notifyNotice() with chatOpen=false.

Run by test/host/run.sh; prints one line per checked fact and exits non-zero on any violation.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
problems = []


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def code(path: pathlib.Path) -> str:
    return strip_comments(path.read_text(encoding="utf-8"))


def body_of(text: str, start: int) -> tuple:
    """The {..} body at/after `start`, balanced. Returns (first, last) offsets or (-1, -1)."""
    first = text.find("{", start)
    if first < 0:
        return (-1, -1)
    depth = 0
    for i in range(first, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return (first, i)
    return (-1, -1)


# --- 1. every map frame ends with the one omFrameEnd(), and nothing after it --------------------
map_screen = code(SRC / "ui" / "screen_map.cpp")

for entry, want_frame in (
    ("void mapScreenDraw(", "drawMapFrame("),
    ("void mapScreenAnimate(", "drawMapFrame("),
    ("void mapScreenSlide(", "drawMapFrame("),
):
    at = map_screen.find(entry)
    if at < 0:
        problems.append(f"screen_map.cpp: {entry.strip()} is missing")
        continue
    first, last = body_of(map_screen, at)
    if first < 0 or want_frame not in map_screen[first:last]:
        problems.append(f"screen_map.cpp: {entry.strip()}) does not go through {want_frame})")

frame_at = map_screen.find("void drawMapFrame(")
if frame_at < 0:
    problems.append("screen_map.cpp: drawMapFrame() is missing")
else:
    first, last = body_of(map_screen, frame_at)
    body = map_screen[first:last] if first >= 0 else ""
    # The guard paths close their own frame (omFrameBegin may have failed), so there is more than one
    # omFrameEnd(); what matters is that the LAST one is the last statement of the function.
    ends = [m.start() for m in re.finditer(r"\bomFrameEnd\s*\(\s*\)", body)]
    if not ends:
        problems.append("screen_map.cpp: drawMapFrame never calls omFrameEnd()")
    else:
        tail = body[ends[-1]:].strip()
        if tail != "omFrameEnd();":
            problems.append("screen_map.cpp: something is drawn after drawMapFrame's omFrameEnd(): "
                            + tail[:60].replace("\n", " "))

# No second push path in the map screen: the panel is written only by board.cpp's pushFrame().
for bad in ("pushImage", "omPushRgb565", "s_display"):
    if bad in map_screen:
        problems.append(f"screen_map.cpp: {bad} - the map must draw into the frame, not to the panel")

# The map's own draw() must draw on every path (a redraw callback that can return without a frame is
# exactly what would lose a pop-up even though the drawing code is right).
map_tab = code(SRC / "screens" / "map_tab.cpp")
draw_at = map_tab.find("void draw() {")
if draw_at < 0:
    problems.append("map_tab.cpp: draw() is missing")
else:
    first, last = body_of(map_tab, draw_at)
    body = map_tab[first:last] if first >= 0 else ""
    frames = len(re.findall(r"\bmapScreen(?:Draw|Animate|Slide)\s*\(", body))
    returns = len(re.findall(r"\breturn\s*;", body))
    if frames < 2:
        problems.append(f"map_tab.cpp: draw() has {frames} frame calls, expected the cached and the fresh one")
    if returns != 1:
        problems.append(f"map_tab.cpp: draw() has {returns} early returns, expected 1 (the cached path)")

# --- 2. board.cpp: the overlay is drawn before the push ----------------------------------------
board = code(SRC / "board.cpp")
end_at = board.find("void omFrameEnd()")
if end_at < 0:
    problems.append("board.cpp: omFrameEnd() is missing")
else:
    first, last = body_of(board, end_at)
    body = board[first:last] if first >= 0 else ""
    overlay_at = body.find("s_frameOverlay(")
    push_at = body.find("pushFrame()")
    hold_at = body.find("s_holdPush")
    if overlay_at < 0:
        problems.append("board.cpp: omFrameEnd() never calls the overlay")
    if push_at < 0:
        problems.append("board.cpp: omFrameEnd() never pushes")
    if overlay_at >= 0 and push_at >= 0 and not (overlay_at < push_at):
        problems.append("board.cpp: the overlay is drawn AFTER the push")
    # The hold path still draws the overlay first: the frame is composed complete before it is held.
    if overlay_at >= 0 and hold_at >= 0 and not (overlay_at < hold_at):
        problems.append("board.cpp: a held frame is flushed without the overlay")

# --- 3. the consumed tap: the toast's press edge first, every map act behind !g_spent -------------
main = code(SRC / "main.cpp")

dismiss_at = main.find("toastDismissOnPress(om::ui::toastState(), millis())")
if dismiss_at < 0:
    problems.append("main.cpp: the toast's press edge is gone")
else:
    first, last = body_of(main, dismiss_at)
    spend = main[first:last] if first >= 0 else ""
    for flag in ("g_spent = true", "g_drag = true", "g_acted = true"):
        if flag not in spend:
            problems.append(f"main.cpp: a dismissing press does not set `{flag}`")
    if "redrawCurrentScreen()" not in spend:
        problems.append("main.cpp: a dismissing press does not redraw the frame without the toast")

# The map's own tap is only reached through handleTap(), and only from the release of an unspent,
# undragged, unacted gesture.
handle_at = main.find("static void handleTap(")
if handle_at < 0:
    problems.append("main.cpp: handleTap() is missing")
else:
    first, last = body_of(main, handle_at)
    if "om::map_tab::tap(" not in main[first:last]:
        problems.append("main.cpp: handleTap() no longer reaches the map's hit test")

release_at = main.find("if (!isDown) {")
release_end = main.find("} else if (!g_down) {", release_at) if release_at >= 0 else -1
if release_at < 0 or release_end < 0:
    problems.append("main.cpp: the release branch is missing or changed shape")
else:
    release = main[release_at:release_end]
    if "if (!spent) {" not in release:
        problems.append("main.cpp: the release tap is not inside an `if (!spent)` branch")
    calls = list(re.finditer(r"handleTap\s*\(\s*g_sx\s*,\s*g_sy\s*,\s*false\s*\)", release))
    if len(calls) != 1:
        problems.append(f"main.cpp: {len(calls)} short-tap calls in the release branch, expected 1")
    for call in calls:
        window = release[max(0, call.start() - 120):call.start()]
        if "!g_drag && !g_acted" not in window:
            problems.append("main.cpp: the release tap is not guarded by !g_drag && !g_acted")

# The map's pan and its press-continuation both live behind !g_spent.
drag_at = main.find("if (g_drag && !g_spent) {")
if drag_at < 0:
    problems.append("main.cpp: the drag branch is no longer `if (g_drag && !g_spent)`")
else:
    body_first, body_last = body_of(main, drag_at)
    body = main[body_first:body_last]
    if "if (g_tab == 2)" not in body:
        problems.append("main.cpp: the map's drag handler is no longer inside the drag branch")
    if "om::mapPan(" not in body:
        problems.append("main.cpp: the map's pan is not inside the !g_spent drag branch")

cont_at = main.find("const bool cont = (g_tab == 2")
if cont_at < 0:
    problems.append("main.cpp: the map's press continuation is missing")
elif "!g_spent" not in main[cont_at:main.find(";", cont_at)]:
    problems.append("main.cpp: the map's press continuation is not guarded by !g_spent")

# --- 4. the notice door has no Map special case ------------------------------------------------
notify_calls = [m.group(0).replace("\n", " ") for m in
                re.finditer(r"notifyNotice\s*\([^;]*;", main)]
if len(notify_calls) != 2:   # the DM arrival and the channel arrival
    problems.append(f"main.cpp: {len(notify_calls)} notifyNotice() calls, expected the DM and the channel one")
for call in notify_calls:
    if "g_tab == 2" in call or "g_tab==2" in call:
        problems.append("main.cpp: a notifyNotice() call has a Map special case: " + call)
for want in ("chatOpenForThread(thread)", "chatOpenForChannel(ch)"):
    if want not in main:
        problems.append(f"main.cpp: the arrival no longer asks {want}")

usb = code(SRC / "usb_commands.cpp")
hook = usb.find("case om::usb::Kind::Notify:")
if hook < 0:
    problems.append("usb_commands.cpp: the `notify test` handler is missing")
else:
    first, last = body_of(usb, hook)
    body = usb[first:last] if first >= 0 else ""
    if "toasts::notifyNotice(" not in body:
        problems.append("usb_commands.cpp: `notify test` does not go through toasts::notifyNotice()")
    if "NoticeKind::Channel" not in body:
        problems.append("usb_commands.cpp: `notify test` does not use NoticeKind::Channel")
    if re.search(r"notifyNotice\([^;]*,\s*true\s*,", body):
        problems.append("usb_commands.cpp: `notify test` passes chatOpen=true")

# And the one-place log line: toasts.cpp prints what ui::noticeDecide() decided, never its own words.
toasts = code(SRC / "screens" / "toasts.cpp")
if "ui::noticeDecide(" not in toasts:
    problems.append("toasts.cpp: the notice decision no longer comes from ui::noticeDecide()")
if "ui::noticeLogLine(" not in toasts:
    problems.append("toasts.cpp: the [notice] line no longer comes from ui::noticeLogLine()")
for stray in re.finditer(r"\"\[notice\]\s*(?:show|queue|skip)", toasts):
    problems.append("toasts.cpp: a hand-written [notice] decision line bypasses the one formatter")

if problems:
    for p in problems:
        print("FAIL " + p)
    print(f"est381_frame: {len(problems)} problem(s)")
    sys.exit(1)

print("est381_frame: every map draw ends with the one overlay frame; the toast's press edge comes")
print("est381_frame: first and spends the gesture; the door has no Map exception. 0 problems")
