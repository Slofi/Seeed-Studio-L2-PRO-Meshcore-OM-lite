#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-409: the source half of the Map sweep fixes. The C++ half is test_est409.cpp; this file pins
# the shapes a run cannot see: which value a toggle reads after the NVS write, the log text, the
# cache call sites, the query-safe build and the header cleanup.
#
# Run it alone against an older tree with `python3 test_est409.py /path/to/tree` (the shape checks
# read only src/screens/map_tab.*, src/ui/map_trace.* and src/ui/screen_map.h).
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
    mapc = (src / "screens/map_tab.cpp").read_text(encoding="utf-8")
    maph = (src / "screens/map_tab.h").read_text(encoding="utf-8")
    tracec = (src / "ui/map_trace.cpp").read_text(encoding="utf-8")
    traceh = (src / "ui/map_trace.h").read_text(encoding="utf-8")
    screenh = (src / "ui/screen_map.h").read_text(encoding="utf-8")

    # 2. The toggle's return is the NVS write's, not the new state. Read the state back, like the
    # TabsToggle case already does.
    traces = block(mapc, "case ui::MapHitKind::Traces:", "case ui::MapHitKind::Names:")
    forbid(traces, "const bool on = s_layers.toggleTraces();",
           "the Traces key treats the NVS write's result as the state (turning it off would seed)")
    need(traces, "s_layers.toggleTraces();", "the Traces key no longer toggles the saved switch")
    need(traces, "const bool on = s_layers.traces();",
         "the Traces key must read s_layers.traces() after the toggle")
    need(traces, "if (on) traceSeedFromRing();",
         "a Traces key that is OFF must not seed a replay")
    need(traces, 'Serial.printf("[map] traces %s\\n", on ? "on" : "off");',
         "the traces log word must be the state, not the NVS write's result")
    names = block(mapc, "case ui::MapHitKind::Names:", "case ui::MapHitKind::TabsToggle:")
    forbid(names, "const bool on = s_layers.toggleNames();",
           "the Names key treats the NVS write's result as the state")
    need(names, "s_layers.toggleNames();", "the Names key no longer toggles the saved switch")
    need(names, "const bool on = s_layers.names();",
         "the Names key must read s_layers.names() after the toggle")
    need(names, 'Serial.printf("[map] names %s\\n", on ? "on" : "off");',
         "the names log word must be the state, not the NVS write's result")

    # 3. Both trace lines print counts only, never another node's name (REBUILD.md rule 9).
    forbid(mapc, '"[map] trace %s', "a Map trace log line still prints a sender name")
    need(mapc, '"[map] trace %u hop(s)', "the Map trace log must print counts only")
    need(mapc, "known point(s)", "the Map trace log lost its known-point count")

    # 4. The contact table is copied once per signature, not once per frame.
    need(mapc, '#include "ui/signature_cache.h"', "the Map does not use the signature cache")
    need(mapc, "ui::SignatureCache<MeshContact, 32> s_contacts;",
         "the contact table is not held in a signature cache")
    need(mapc, "s_contacts.begin(om::meshContactSignature())",
         "the cache is not keyed on meshContactSignature()")
    if mapc.count("om::meshContactList(") != 1:
        fail(f"om::meshContactList() must be called only from the cache rebuild "
             f"({mapc.count('om::meshContactList(')} calls)")

    # 6. The slice capacity is the array's real size, not a hand-computed duplicate.
    cap = "sizeof(out->tail) / sizeof(out->tail[0])"
    if tracec.count(cap) != 2:
        fail(f"both traceSlice() calls must pass {cap!r} ({tracec.count(cap)} of 2)")
    forbid(tracec, "out->tail, kTraceMaxPoints + 1)",
           "a traceSlice() call still duplicates the tail's capacity by hand")

    # 7. The three silent trace drops are counted by reason and printed by traceCostLine().
    cost = block(mapc, "void traceCostLine()", "}  // namespace")
    for counter in ("++s_traceDropNoPoint;", "++s_traceDropNoSelf;", "++s_traceDropQueue;"):
        need(mapc, counter, f"the drop is not counted: {counter}")
    need(cost, "s_traceDropped", "traceCostLine() does not print the drop counter")
    for reason in ("no point %lu", "no self %lu", "queue %lu"):
        need(cost, reason, f"traceCostLine() does not print the reason: {reason}")

    # 8. Our own dot is outside the 32-contact budget; the cull uses kMapMarkerR; the count is logged.
    need(screenh, "MapMarker markers[kMapMaxMarkers + 1];",
         "the marker array still has no room for our own dot outside the 32 contacts")
    need(mapc, "x < -ui::kMapMarkerR", "the marker cull still uses a bare 4")
    need(mapc, '"[map] markers %d of %d', "the drawn marker count is not logged")

    # 9. Hops are clamped once and the clamp is reported.
    need(traceh, "bool hopsClamped = false;", "the route has no hops-clamped flag")
    need(tracec, "out->hopsClamped", "traceBuildRoute() does not report the hop clamp")
    forbid(tracec, "out->hops = hopCount;", "the route still keeps the raw packet hop count")
    need(mapc, "route.hopsClamped", "the Map does not report a clamped hop count")

    # 10. The queue keeps the full 32-bit sender hash and steps back after a removal.
    need(mapc, "uint32_t traceKeyForName(", "the sender key is still one byte")
    forbid(mapc, "return (uint8_t)h;", "the sender hash is still truncated to one byte")
    need(traceh, "uint32_t key", "the queue item's sender key is not 32-bit")
    need(tracec, "for (int i = count_ - 1; i >= 0; --i)",
         "the dedupe loop still skips the element after a removal")

    # 11. build() is a query from the hit test and the pressed look: no selection clear, no trace
    # clock advance. Only the draw paths sample the animation.
    forbid(mapc, "if (s_selName[0] && !v.callout) s_selName[0] = 0;",
           "build() still clears the selection (a pan off-screen loses the callout)")
    press = block(mapc, "om::ui::PressTarget pressTarget(", "bool tap(")
    need(press, "build(v, false)", "pressTarget() still advances the trace clock")
    tap = block(mapc, "bool tap(int x, int y)", "void tracesResetAndSeed()")
    need(tap, "build(v, false)", "tap() still advances the trace clock")
    draw = block(mapc, "void draw() {", "void pump(")
    need(draw, "build(v, true)", "the draw path no longer samples its own trace frame")

    # 12. The header advertises only what exists and has a caller.
    for decl in ("void select(const char* name);", "bool locked();", "void tracesToggle();",
                 "void namesToggle();"):
        forbid(maph, decl, "map_tab.h still declares an internal or undefined function")
    need(maph, "void showPoint(double lat, double lon, const char* name);",
         "map_tab.h lost the Show-on-map entry point")
    need(mapc, "static void select(const char* name)", "select() is not internal any more")
    forbid(mapc, "bool locked()", "locked() still has a dead definition")

    if FAIL:
        print(f"{FAIL} est409 source check(s) FAILED")
        return 1
    print("est409: toggle state, counts-only logs, signature cache, tail size, drop counters, "
          "marker budget, hop clamp, 32-bit key, query-safe build and the cleaned header")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
