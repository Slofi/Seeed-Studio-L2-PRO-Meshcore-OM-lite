#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-413: the source half of the clock-floor fix. The C++ half (test_est413.cpp) proves the
# logger's stamp gate, the skip counter and the manual-time seed; this file pins the wiring:
#  * power_log.cpp stamps through MinuteClock (no row from the floor), counts the skipped minutes
#    and refuses to name a tail file before realSinceBoot();
#  * usb_commands.cpp says "no real time yet" before it calls printTail() and reports the skip count;
#  * main.cpp starts the manual-time steppers from manualTimeSeedEpoch() (the build time, never the
#    stale floor) and only uses the clock when it is real;
#  * rptr_tab.cpp's one repeater clock write checks realSinceBoot() itself;
#  * timekeeper.h holds the one seed rule, and the floor itself is untouched.
#
# Run it alone against another tree with `python3 test_est413.py /path/to/tree`.
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
    plog = (src / "power_log.cpp").read_text(encoding="utf-8")
    plogh = (src / "power_log.h").read_text(encoding="utf-8")
    logic = (src / "power_log_logic.h").read_text(encoding="utf-8")
    maincpp = (src / "main.cpp").read_text(encoding="utf-8")
    usbc = (src / "usb_commands.cpp").read_text(encoding="utf-8")
    rptrc = (src / "screens" / "rptr_tab.cpp").read_text(encoding="utf-8")
    tkh = (src / "timekeeper.h").read_text(encoding="utf-8")

    # --- 1. the logger: one gate, one skip count, no floor-stamped row -----------------------
    need(logic, "bool logLocalStamp(uint32_t epochUtc, bool clockRealSinceBoot, LocalStamp& out);",
         "power_log_logic.h has no real-clock gate")
    need(logic, "if (!clockRealSinceBoot) {", "MinuteClock does not refuse a not-real clock")
    need(logic, "++skipped_;", "MinuteClock does not count a skipped minute")
    need(logic, "return logLocalStamp(epochUtc, true, out);",
         "MinuteClock does not stamp through the one gate")

    need(plog, "MinuteClock s_clock;", "power_log.cpp has no MinuteClock")
    need(plog, "s_clock.reset();", "begin() does not clear the skip count")
    need(plog, "s_clock.stamp(om::timeKeeper().now(), clockReal, t)",
         "the loop does not stamp through s_clock")
    need(plog, "const bool clockReal = om::timeKeeper().realSinceBoot();",
         "the loop does not read realSinceBoot()")
    need(plog, "uint32_t skippedMinutes() { return s_clock.skipped(); }",
         "the skip count has no accessor")
    need(plogh, "uint32_t skippedMinutes();", "power_log.h does not declare skippedMinutes()")

    # localNow() delegates to the tested rule; it must not format now() itself again.
    local_now = block(plog, "bool localNow(LocalStamp& out) {", "\n}\n")
    need(local_now, "logLocalStamp(om::timeKeeper().now(), om::timeKeeper().realSinceBoot(), out)",
         "localNow() does not gate on realSinceBoot()")
    forbid(local_now, "localCivil(", "localNow() still formats now() directly")

    # The tail must refuse a not-real clock BEFORE it names the day's file.
    tail = block(plog, "int printTail() {", "\n}\n")
    need(tail, "if (!om::timeKeeper().realSinceBoot()) return -1;",
         "printTail() does not refuse a not-real clock")
    i_gate = tail.find("realSinceBoot()")
    i_name = tail.find("formatFileName")
    if i_gate >= 0 and i_name >= 0 and i_gate > i_name:
        fail("printTail() checks realSinceBoot() after it has named the file")

    # --- 2. the command answer: no real time, and the skip count in every reply ---------------
    need(usbc, "[cmd] error log power tail: no real time yet skipped=%lu write_fail=%lu",
         "`log power tail` has no no-real-time answer")
    i_refuse = usbc.find("no real time yet")
    i_tail = usbc.find("om::plog::printTail()")
    if i_refuse >= 0 and i_tail >= 0 and i_refuse > i_tail:
        fail("usb_commands.cpp calls printTail() before the real-time refusal")
    need(usbc, "[cmd] ok log power tail %d write_fail=%lu skipped=%lu",
         "the tail's ok answer does not report the skip count")
    need(usbc, "[cmd] ok log power on=%d saved=%d skipped=%lu",
         "the on/off answer does not report the skip count")

    # --- 3. the manual-time steppers: never seeded from the floor ------------------------------
    need(tkh, "constexpr uint32_t manualTimeSeedEpoch(bool clockRealSinceBoot, uint32_t nowUtc, "
              "uint32_t buildUtc)", "timekeeper.h has no one manual-time seed rule")
    need(tkh, "return clockRealSinceBoot ? nowUtc : buildUtc;",
         "manualTimeSeedEpoch does not fall back to the build time")
    seed = block(maincpp, "static void timeInitFromClock() {", "\n}\n")
    need(seed, "om::manualTimeSeedEpoch(om::timeKeeper().realSinceBoot(),",
         "timeInitFromClock() does not use the seed rule")
    need(seed, "g_buildEpoch", "timeInitFromClock() does not pass the build time")

    # --- 4. the repeater's clock write refuses a not-real clock on its own ---------------------
    set_clock = block(rptrc, "static void rptrSetClockNow() {", "\n}\n")
    need(set_clock, "!om::timeKeeper().realSinceBoot()",
         "rptrSetClockNow() does not check realSinceBoot()")
    i_real = set_clock.find("realSinceBoot()")
    i_cli = set_clock.find("om::rptr::cliTime")
    if i_real >= 0 and i_cli >= 0 and i_real > i_cli:
        fail("rptrSetClockNow() checks realSinceBoot() after building the CLI line")

    # --- 5. the floor itself is untouched (the ticket's own rule) ------------------------------
    need(tkh, "constexpr uint32_t kFloorSaveIntervalSec = 10UL * 60UL;",
         "the floor save interval changed")
    need((src / "clock_keep.h").read_text(encoding="utf-8"),
         "if (gpsBootResetsReceiver(reason)) return out;",
         "the kept-clock decision changed (a power-on must stay a cold start)")

    if FAIL == 0:
        print("est413: log gate, skip count, tail refusal, manual seed and rptr write are gated")
        return 0
    print(f"est413: {FAIL} failure(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
