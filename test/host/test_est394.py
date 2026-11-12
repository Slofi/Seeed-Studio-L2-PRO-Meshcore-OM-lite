#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-394: traces stay on their repeaters, at the source level.
#
# The C++ test (test_est394.cpp) proves the model: one route sampled in view A and in view B lands
# where mapProject puts its lat/lon in B (pan and zoom), including the dot, the tail and the ring.
# This grep proves the shell wiring no host binary can compile: the Map tab queues micro-degrees,
# it never projects a route when the message arrives, every MapView is built from a frame sampled in
# the CURRENT view, the replay's animation frames only run over a canvas that matches the view, and
# the picker's zoom label is drawn from its own rectangle under the back key.
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FAIL = 0


def fail(msg):
    global FAIL
    print("FAIL", msg)
    FAIL += 1


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def count(text, needle):
    return text.count(needle)


def block(text, start, end):
    i = text.find(start)
    if i < 0:
        return ""
    j = text.find(end, i + len(start))
    return text[i:j if j >= 0 else len(text)]


def main():
    trace_h = read("src/ui/map_trace.h")
    trace_cpp = read("src/ui/map_trace.cpp")
    map_tab = read("src/screens/map_tab.cpp")
    screen_map = read("src/ui/screen_map.cpp")
    main_cpp = read("src/main.cpp")
    map_pick = read("src/ui/map_pick.cpp")
    layout_cpp = read("src/ui/rptr_layout.cpp")
    layout_h = read("src/ui/rptr_layout.h")

    # 1. The model keeps georeferenced points. The queue and the animation take TraceGeoPt, and the
    #    sample/tick are the only place a view enters.
    if "struct TraceGeoPt" not in trace_h:
        fail("map_trace.h has no georeferenced route point")
    if "typedef bool (*TraceProject)" not in trace_h:
        fail("map_trace.h does not take the caller's view as a projector")
    for sig in ("bool sample(uint32_t nowMs, TraceProject project, TraceFrame* out);",
                "bool tick(uint32_t nowMs, TraceProject project, TraceFrame* out);",
                "bool sampleActive(uint32_t nowMs, TraceProject project, TraceFrame* out);"):
        if sig not in trace_h:
            fail(f"map_trace.h is missing {sig!r}")
    if "TracePt route_[kTraceMaxPoints]" in trace_h:
        fail("the animation still stores screen pixels")
    if "TraceGeoPt route_[kTraceMaxPoints]" not in trace_h:
        fail("the animation does not store the route's micro-degrees")
    if "traceGeom(px, routeCount_, &geom);" not in trace_cpp:
        fail("the sample does not build its geometry from the projected points")

    # 2. The Map tab queues the route's own lat/lon: no mapProject() in traceEnqueue. That one call
    #    was the bug (the pixels were fixed at the moment the message arrived).
    enqueue = block(map_tab, "bool traceEnqueue(", "void traceEnqueueRingMessage(")
    if not enqueue:
        fail("traceEnqueue() not found in map_tab.cpp")
    if "om::mapProject(" in enqueue:
        fail("traceEnqueue() still projects the route when the message arrives")
    if "ui::TraceGeoPt pts[ui::kTraceMaxPoints];" not in enqueue:
        fail("traceEnqueue() does not queue micro-degrees")
    if count(enqueue, ".latE6 = ") < 2 or count(enqueue, ".lonE6 = ") < 2:
        fail("traceEnqueue() does not copy both lat and lon into the queue")

    # 3. Every DRAWING MapView gets a frame sampled in the CURRENT view: build() calls
    #    traceSampleNow() when its `sampleTrace` argument is true, and that is the one call site that
    #    hands mapProject() to the model. EST-409: the two query callers pass false, so a hit test can
    #    no longer advance the animation; the draw paths still pass true.
    build = block(map_tab, "void build(ui::MapView& v, bool sampleTrace)", "\n}\n")
    if "if (sampleTrace) {" not in build or "traceSampleNow();" not in build:
        fail("build() does not re-sample the trace for the view it is about to draw")
    if build.find("traceSampleNow();") > build.find("v.trace = s_traceFrame;"):
        fail("build() copies the trace frame before re-sampling it")
    if "build(v, true)" not in block(map_tab, "void draw() {", "void pump("):
        fail("draw() does not ask build() for a freshly sampled trace")
    for query in ("om::ui::PressTarget pressTarget(","bool tap(int x, int y)"):
        if "build(v, false)" not in block(map_tab, query, "\n}\n"):
            fail(f"{query.strip()} does not keep build() a query (EST-409)")
    sample = block(map_tab, "void traceSampleNow()", "\n}\n")
    if "s_traceQueue.sampleActive(millis(), om::mapProject, &s_traceFrame)" not in sample:
        fail("traceSampleNow() does not sample the running route through mapProject()")
    if "s_traceQueue.tick(now, om::mapProject, &s_traceFrame)" not in map_tab:
        fail("the replay's tick does not project through mapProject()")

    # 4. During a drag the replay's own 30 fps frames do not paint over a canvas that no longer
    #    matches the view; the drag's draw() is the frame that shows the trace.
    pump = block(map_tab, "void pump(bool visible, bool uiDrawOk)", "\n}\n")
    if "const bool freshCanvas = om::mapCanvasReady() && !om::mapViewStale();" not in pump:
        fail("pump() does not check that the dragged canvas matches the view")
    if "if (!uiDrawOk || !freshCanvas) return;" not in pump:
        fail("an animation frame can still be drawn over a stale canvas")
    # EST-405 moved the screen's on/off state from a loop local to a file-scope static (g_screenOn),
    # because the screen auto-off timer is now a second caller of the sleep transition. Same test.
    if "if (g_tab == 2 && !g_settings && g_screenOn && g_drag) om::map_tab::draw();" not in main_cpp:
        fail("the release of a map drag does not draw the final position and its trace")

    # 5. The drawing side is unchanged: it still gets a pixel frame, so clipping and the palette are
    #    exactly EST-368/372's. The conversion happens once, in the model's sample.
    if "void drawTrace(lgfx::LovyanGFX* d, const TraceFrame& f" not in screen_map:
        fail("the Map screen no longer draws the pixel TraceFrame")
    if "mapProject" in block(screen_map, "void drawTraceRoute(", "void drawTraceMoving("):
        fail("the drawing code projects on its own instead of using the sampled frame")

    # 6. The picker's zoom label is drawn from its own rectangle, and that rectangle is under the
    #    back key with a gap: no "Cancel" width can crowd it.
    if "Rect hint;" not in layout_h:
        fail("rptr_layout.h has no rectangle for the picker's zoom label")
    if "m.hint = Rect{8, (int16_t)(m.back.y + m.back.h + 6), 60, 14};" not in layout_cpp:
        fail("the zoom label is not placed under the back key with a gap")
    draw = block(map_pick, "void drawMapPick(", "void drawMapPickScreen(")
    if "m.hint.x" not in draw or "m.hint.w" not in draw:
        fail("map_pick.cpp does not draw the zoom label from its own rectangle")
    if "m.back.right() + 8" in draw:
        fail("map_pick.cpp still puts the zoom label against the back key's edge")
    if "FontRole::MonoMeta" not in draw:
        fail("the zoom label lost its mono face")

    if FAIL == 0:
        print("est394.py: geo trace sampled per view, drag frames guarded, release draws, "
              "zoom label clear")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
