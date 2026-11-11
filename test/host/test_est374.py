#!/usr/bin/env python3
"""EST-374: leaving the Map always shows the bottom bar.

The tucked bottom bar belongs to the MAP screen. Every other screen draws its own full bar, so the
bar is on the next screen by construction, but only if THREE things stay true in the source:

  1. every page switch goes through showTab(), which is the only function that writes g_tab;
  2. showTab() calls map_tab::leave() when it leaves the Map (the one named place for the rule);
  3. no other screen reads the saved `tabsHidden` key, i.e. no second screen can tuck its bar.

C++ cannot prove a call graph, so this scans the sources. Run by test/host/run.sh; exits non-zero
with a line per violation. It also prints the page-switch list, which PROGRESS.md quotes.
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


main_text = code(SRC / "main.cpp")

# The DECLARATION (`static int g_tab = 0;`) is not a page switch; blank it out (same length, so the
# reported line numbers still match main.cpp) before looking for writers.
decl_re = re.compile(r"\b(?:static\s+)?int\s+g_tab\s*=\s*[^;]+;")
main_text = decl_re.sub(lambda m: " " * len(m.group(0)), main_text)

# 1. ONE writer of the page variable, and it is showTab(). A second writer would be a page switch that
#    never passes the rule (the bug this ticket is about: a screen drawn with the wrong bar).
writers = [m.start() for m in re.finditer(r"(?<![=!<>+\-*/])g_tab\s*=(?!=)", main_text)]
if len(writers) != 1:
    problems.append(f"main.cpp: {len(writers)} writers of g_tab, expected exactly 1 (inside showTab)")
else:
    # The DEFINITION (with a body), not the forward declaration at the top of the file.
    show_tab_at = main_text.find("static void showTab(int tab) {")
    body_end = main_text.find("\n}\n", show_tab_at) if show_tab_at >= 0 else -1
    if show_tab_at < 0 or body_end < 0:
        problems.append("main.cpp: showTab() is missing or changed shape")
    elif not (show_tab_at < writers[0] < body_end):
        problems.append("main.cpp: g_tab is written outside showTab()")

# 2. showTab() is the only page switcher, it is reached from every screen's bar, and it calls
#    map_tab::leave() on the way off the Map.
calls = [
    (m.start(), " ".join(m.group(1).split()))
    for m in re.finditer(r"\bshowTab\s*\(([^)]*)\)\s*;", main_text)
]
# The forward declaration at the top of the file is not a call site.
def _line_of(pos: int) -> str:
    start = main_text.rfind("\n", 0, pos) + 1
    return main_text[start:main_text.find("\n", pos)].strip()


calls = [(pos, args) for pos, args in calls if not _line_of(pos).startswith("static void showTab(")]
if len(calls) < 8:
    problems.append(f"main.cpp: only {len(calls)} showTab() call sites, expected the 8 known routes")
if not re.search(r"if\s*\(fromTab\s*==\s*2\s*&&\s*tab\s*!=\s*2\)\s*om::map_tab::leave\(\)\s*;",
                 main_text):
    problems.append("main.cpp: showTab() does not call map_tab::leave() when it leaves the Map")
for needle in ("om::map_tab::enter()", "om::map_tab::tracesResetAndSeed()"):
    if needle not in main_text:
        problems.append(f"main.cpp: showTab() no longer prepares the Map ({needle} missing)")

# Every tab bar tap in main.cpp goes to showTab: `const int tab = L.tabAt(x, y);` must be followed
# by a showTab() branch, and the Map's own hit test must be the only place that can suppress it.
tab_at = re.findall(r"const int tab = ([^;]*);", main_text)
if len(tab_at) < 4:
    problems.append(f"main.cpp: only {len(tab_at)} tabAt() reads, expected one per screen")
if "mapBarsTucked(om::map_tab::tabsHidden(), true)" not in main_text:
    problems.append("main.cpp: the Map's tucked bar rule is not applied to the bar's tap zone")

# 3. The tucked bar is the MAP's alone. These screens must draw their own bar and never read the
#    saved key, or a second screen could hide the bar behind the user's back.
for name in ("screen_nodes.cpp", "screen_chat.cpp", "screen_rptr.cpp", "screen_settings.cpp"):
    path = SRC / "ui" / name
    body = code(path)
    if "tabsHidden" in body:
        problems.append(f"src/ui/{name}: reads tabsHidden (only the Map screen may tuck its bar)")
for name in ("screen_nodes.cpp", "screen_chat.cpp", "screen_rptr.cpp"):
    body = code(SRC / "ui" / name)
    if "drawTabBar(" not in body:
        problems.append(f"src/ui/{name}: no drawTabBar() call (the bar would be missing)")
map_body = code(SRC / "ui" / "screen_map.cpp")
if "drawTabBar(" not in map_body or "drawBarStrip(" not in map_body:
    problems.append("src/ui/screen_map.cpp: must draw BOTH the shown bar and the tucked strip")

# 4. The rule has one definition, and it is the plain-header one the C++ test walks per tab.
chrome = code(SRC / "ui" / "map_chrome.h")
if "constexpr bool mapBarsTucked(bool tabsHidden, bool onMapScreen)" not in chrome:
    problems.append("src/ui/map_chrome.h: mapBarsTucked() is missing or changed shape")
users = []
for path in sorted(SRC.rglob("*.cpp")) + sorted(SRC.rglob("*.h")):
    if "mapBarsTucked(" in code(path) and path.name != "map_chrome.h":
        users.append(str(path.relative_to(ROOT)))
if users != ["src/main.cpp"]:
    problems.append(f"mapBarsTucked() is read outside main.cpp's Map hit test: {users}")

if problems:
    for p in problems:
        print("FAIL " + p)
    print(f"est374_switches: {len(problems)} problem(s)")
    sys.exit(1)

print("est374_switches: every page switch goes through showTab -> map_tab::leave():")
lines = main_text.splitlines()
for pos, args in calls:
    line_no = main_text.count("\n", 0, pos) + 1
    context = lines[line_no - 1].strip()
    print(f"  main.cpp:{line_no}: showTab({args})   [{context}]")
print("est374_switches: 0 problems")
