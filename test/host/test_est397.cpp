// SPDX-License-Identifier: MIT
// EST-397: the two core fixes whose logic is host-testable.
//
//  #1 the clock's sanity window stops at INT32_MAX, because time_t is 32 bits and signed on this
//     toolchain: every epoch above it becomes a NEGATIVE tv_sec and settimeofday() would set the clock
//     into 1901. Tested at the exact edge.
//  #4 a fix expires 5 s after the last GGA that carried one, so a receiver that goes quiet stops
//     advertising its last point.
//
// Plain C++ against the same headers the device builds, no Arduino.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "gps_sky.h"
#include "timekeeper.h"

static int g_fail = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++g_fail;
  }
}

int main() {
  // ---- #1: the bound -------------------------------------------------------------------------
  // The first value a 32-bit signed time_t cannot hold is INT32_MAX + 1; the cast makes it negative.
  constexpr uint32_t kFirstNegative = (uint32_t)INT32_MAX + 1u;   // 2038-01-19 03:14:08 UTC
  check((int32_t)kFirstNegative < 0, "the premise: (int32_t)INT32_MAX+1 is negative");
  check(!om::saneEpoch(kFirstNegative), "the first epoch time_t turns negative is refused");
  check(!om::saneEpoch(om::kMaxSaneEpoch), "the bound is exclusive (kMaxSaneEpoch itself is refused)");
  check(!om::saneEpoch(0xFFFFFFFFu), "the top of uint32_t is refused");
  check(om::kMaxSaneEpoch <= (uint32_t)INT32_MAX, "kMaxSaneEpoch is at or below INT32_MAX");
  check(om::kMaxSaneEpoch > 4102444800UL / 2u, "the gate did not shrink to something silly");
  // The last epoch that still fits is accepted, so the everyday range is untouched.
  check(om::saneEpoch(om::kMaxSaneEpoch - 1u), "the last epoch a 32-bit time_t holds is accepted");
  check(om::saneEpoch(1764000000u), "a plain 2025 epoch is still accepted");   // 2025-11-24
  check(!om::saneEpoch(om::kMinSaneEpoch - 1u), "the floor is still enforced");

  // TimeKeeper's own write path refuses out-of-window epochs before any (time_t) cast: a fake setter
  // proves that an epoch above the bound never reaches it.
  {
    static uint32_t written = 0;
    static bool anyWrite = false;
    struct Fake {
      static void set(uint32_t e) { written = e; anyWrite = true; }
      static uint32_t get() { return written; }
    };
    om::TimeKeeper tk;
    tk.begin(1764000000u, 1764000000u, 0, &Fake::set, &Fake::get);
    const uint32_t floor = tk.floor();
    anyWrite = false;
    // setFromSource runs saneEpoch first, so a 2039 value is refused there and never cast.
    check(!tk.setFromSource(kFirstNegative, om::TimeSource::Wifi, 100), "a 2039 Wi-Fi time is refused");
    check(!anyWrite, "a refused time never reaches the clock setter");
    check(tk.now() == floor || !anyWrite, "the clock is unchanged by a refused time");
    // A sane one still gets through (positive control: the test can see a real write).
    check(tk.setFromSource(1764000100u, om::TimeSource::Wifi, 100), "a sane time is still applied");
    check(written == 1764000100u, "the sane time reached the setter");
    // setHardware (MeshCore's path) is gated the same way.
    anyWrite = false;
    tk.setHardware(kFirstNegative);
    check(!anyWrite, "setHardware refuses an epoch above the bound");
  }

  // ---- #4: the fix expires --------------------------------------------------------------------
  {
    om::GpsState g;
    g.valid = true;
    g.lat = 46.0;   // invented, never a real position (rule 9)
    g.lon = 14.0;
    g.lastFixMs = 1000;
    om::gpsExpireFix(g, 1000);
    check(g.valid, "a fresh fix stays valid");
    om::gpsExpireFix(g, 1000 + om::kGpsFixExpiryMs - 1u);
    check(g.valid, "one millisecond before the window closes the fix is still valid");
    om::gpsExpireFix(g, 1000 + om::kGpsFixExpiryMs);
    check(!g.valid, "5 s after the last GGA the fix is gone");
    check(g.lat == 46.0 && g.lon == 14.0, "the last known point is kept (nothing may use it while !valid)");

    // A GGA restamps and the fix comes back; a second expiry is idempotent.
    g.valid = true;
    g.lastFixMs = 9000;
    om::gpsExpireFix(g, 13000);
    check(g.valid, "a restamped fix survives");
    om::gpsExpireFix(g, 14000);
    check(!g.valid, "and expires 5 s after its own stamp");
    om::gpsExpireFix(g, 99999999u);
    check(!g.valid, "expiring an expired fix is a no-op");

    // millis() wrapping is unsigned arithmetic in the helper: a stamp near the wrap still counts.
    om::GpsState w;
    w.valid = true;
    w.lastFixMs = 0xFFFFFF00u;   // 256 ms before the wrap
    om::gpsExpireFix(w, 0x00000010u);   // 272 ms later, i.e. inside the window
    check(w.valid, "the wrap does not expire a fix that is only 272 ms old");
    om::gpsExpireFix(w, 0x00000200u);   // 768 ms after the wrap: 1024 ms total, still inside
    check(w.valid, "still inside after the wrap");
    om::gpsExpireFix(w, 0x00001400u);   // ~5.3 s after the stamp
    check(!w.valid, "and it expires on schedule across the wrap");
  }

  // ---- #13: the overflow counter exists and is separate from checksumBad -----------------------
  {
    om::GpsState g;
    check(g.lineOverflow == 0 && g.checksumBad == 0, "a new GpsState starts with both counters at 0");
    ++g.lineOverflow;
    check(g.checksumBad == 0, "an overflow is not counted as a bad checksum");
  }

  if (g_fail != 0) {
    std::printf("%d est397 check(s) FAILED\n", g_fail);
    return 1;
  }
  std::printf("est397: clock bound at INT32_MAX, fix expiry, overflow counter: all checks passed\n");
  return 0;
}
