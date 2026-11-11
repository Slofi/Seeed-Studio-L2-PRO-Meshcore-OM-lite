// SPDX-License-Identifier: MIT
// EST-391: the NTP reply capture's sanity check must compare in 64 bits.
//
// On the device's toolchain (esp-2021r2-patch5, GCC 8.4, xtensa-esp32s3) sizeof(time_t) == 4, so the
// pre-EST-391 expression `tv->tv_sec >= (time_t)om::kMaxSaneEpoch` compared a real 2026 stamp against
// -192522496 (the assembled `.word`, PROVEN 2026-11-11) and was true for every reply: the capture threw
// them all away (device: reach=1, sync=0, notify=0). This test reproduces the device's 32-bit struct
// and asserts BOTH sides:
//   * the old expression REJECTS a 2026-11-11 stamp (so this test fails on the old code), and
//   * ntp::replyEpochSane(), the firmware's own check, ACCEPTS it.
// The edges of the window are checked too, so a wider or shifted bound cannot slip in.
#include <stdint.h>
#include <stdio.h>

#include "ntp_plan.h"
#include "timekeeper.h"

static int g_fail = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL %s\n", what);
    ++g_fail;
  }
}

// What `struct timeval` is on the device: one int32_t second count.
struct DeviceTimeval {
  int32_t tv_sec;
};

// The check exactly as src/wifi_link.cpp had it before EST-391, applied to a 32-bit tv_sec. On the
// host a real `time_t` is 64 bits, which is why the bug never showed here; the explicit int32_t casts
// are the device's own arithmetic.
static bool oldCheckAccepted(int32_t tvSec) {
  return !(tvSec < (int32_t)om::kMinSaneEpoch || tvSec >= (int32_t)om::kMaxSaneEpoch);
}

int main() {
  // The proof, as a number: the 2100 bound cast down to the device's time_t is negative.
  check((int32_t)om::kMaxSaneEpoch == -192522496, "kMaxSaneEpoch as a 32-bit time_t is -192522496");
  check((int64_t)om::kMaxSaneEpoch == 4102444800LL, "the same bound in 64 bits is 4102444800");

  // The reported case: a valid reply from 2026-11-11 (14:58:40 UTC, the moment of the device run).
  DeviceTimeval tv;
  tv.tv_sec = 1794409120;
  check(oldCheckAccepted(tv.tv_sec) == false,
        "the OLD 32-bit expression rejects the 2026-11-11 reply (the bug reproduced)");
  check(om::ntp::replyEpochSane((int64_t)tv.tv_sec),
        "the NEW check accepts the same 2026-11-11 reply");
  check(om::ntp::replyEpochSane((int64_t)tv.tv_sec) && !oldCheckAccepted(tv.tv_sec),
        "accepted by the new check AND rejected by the old one");

  // The window's edges, in 64 bits: [kMinSaneEpoch, kMaxSaneEpoch).
  check(!om::ntp::replyEpochSane((int64_t)om::kMinSaneEpoch - 1), "one second before the floor");
  check(om::ntp::replyEpochSane((int64_t)om::kMinSaneEpoch), "exactly the floor");
  check(om::ntp::replyEpochSane((int64_t)om::kMaxSaneEpoch - 1), "one second before the ceiling");
  check(!om::ntp::replyEpochSane((int64_t)om::kMaxSaneEpoch), "exactly the ceiling");
  // The values the old expression's arithmetic produced, and an unset clock.
  check(!om::ntp::replyEpochSane(0), "an unset clock (0)");
  check(!om::ntp::replyEpochSane(-192522496), "the negative bound value itself");

  if (g_fail) {
    printf("est391: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("est391: the NTP capture accepts a 2026 reply on a 32-bit time_t\n");
  return 0;
}
