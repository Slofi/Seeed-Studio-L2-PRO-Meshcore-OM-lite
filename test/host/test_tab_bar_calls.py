#!/usr/bin/env python3
"""EST-365: the tab bar's badge has ONE source.

The RPTR page drew the tab bar with a literal 0 while the Nodes and Chat pages passed their
unread count, so the badge disappeared on that tab only. The C++ test walks the TabBarModel; this
test guards the call sites, which C++ alone cannot: every drawTabBar() call must hand it a view's
`tabs` model, and main.cpp must fill that model for all four tabs from the one currentTabBar().

Run by test/host/run.sh; exits non-zero with a line per violation.
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


# 1. Every drawTabBar() call passes a model that lives on a view (`v.tabs`, `view.tabs`, ...).
#    A literal count or an active tab as the third argument is the bug that came back once already.
#    EST-374: the bar's drawing moved into drawTabBarAt(dy) and drawTabBar() is a one-line wrapper,
#    so definitions are dropped before the search: a signature is not a call site, and the wrapper's
#    body would otherwise be read as "drawTabBar(d, L, m) { drawTabBarAt(...)" and fail on its own.
call_re = re.compile(r"drawTabBar\s*\(([^;]*?)\)\s*;", re.S)
def_re = re.compile(r"\bvoid\s+drawTabBar\w*\s*\([^)]*\)\s*\{")
for path in sorted(SRC.rglob("*.cpp")):
    body = def_re.sub("", strip_comments(path.read_text(encoding="utf-8")))
    for m in call_re.finditer(body):
        args = " ".join(m.group(1).split())
        if ".tabs" not in args and "tabBarModel" not in args:
            problems.append(f"{path.relative_to(ROOT)}: drawTabBar({args}) does not pass a tab model")

# 2. main.cpp fills the model for every tab host, through the one helper, and the helper uses the
#    one unread source. The four screens that draw the bar must all be covered.
main = strip_comments((SRC / "main.cpp").read_text(encoding="utf-8"))
if "static om::ui::TabBarModel currentTabBar(om::ui::Tab active)" not in main:
    problems.append("main.cpp: the one currentTabBar() helper is missing")
if "om::ui::tabBarModel(active, unreadTotal(), g_newNodeDot)" not in main:
    problems.append("main.cpp: currentTabBar does not use unreadTotal()/g_newNodeDot")
for tab in ("Nodes", "Chat"):
    want = f"tabs = currentTabBar(om::ui::Tab::{tab});"
    if want not in main:
        problems.append(f"main.cpp: no {want}")
# EST-371: the Map tab's state and drawing glue moved to screens/map_tab.cpp. It gets the one model
# through the Host's tabBar callback, which setup() wires to appTabBar() -> currentTabBar().
map_tab = strip_comments((SRC / "screens" / "map_tab.cpp").read_text(encoding="utf-8"))
if "v.tabs = tabBar(ui::Tab::Map);" not in map_tab:
    problems.append("screens/map_tab.cpp: the Map view does not take its tab model from the Host")
if "mapHost.tabBar = appTabBar;" not in main or "static om::ui::TabBarModel appTabBar(" not in main:
    problems.append("main.cpp: the Map module's tabBar host is not wired to the one helper")
# EST-410: the same for RPTR, moved to screens/rptr_tab.cpp. The one model still comes from
# currentTabBar() through appTabBar(), so the badge has one source on every tab.
rptr_tab = strip_comments((SRC / "screens" / "rptr_tab.cpp").read_text(encoding="utf-8"))
if "v.tabs = tabBar(ui::Tab::Rptr);" not in rptr_tab:
    problems.append("screens/rptr_tab.cpp: the RPTR view does not take its tab model from the Host")
if "rptrHost.tabBar = appTabBar;" not in main:
    problems.append("main.cpp: the RPTR module's tabBar host is not wired to the one helper")

# 3. Each tab screen actually draws the bar (a screen that stops drawing it would pass 1 and 2).
for name in ("screen_nodes.cpp", "screen_chat.cpp", "screen_map.cpp", "screen_rptr.cpp"):
    body = strip_comments((SRC / "ui" / name).read_text(encoding="utf-8"))
    if "drawTabBar(" not in body:
        problems.append(f"src/ui/{name}: no drawTabBar() call")

if problems:
    for p in problems:
        print("FAIL " + p)
    print(f"tab_bar_calls: {len(problems)} problem(s)")
    sys.exit(1)
print("tab_bar_calls: all checks passed")
