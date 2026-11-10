// SPDX-License-Identifier: MIT
// The one timestamp formatter. See age.h for the rules.
#include "ui/age.h"

#include <stdio.h>

namespace om {
namespace ui {
namespace {

// A clock difference this small is treated as "now" (the GPS update lands between two reads of the
// same second). Anything further in the future is not a time we can trust.
constexpr uint32_t kFutureSlackSec = 60;

void ageFallback(char* out, size_t cap, uint32_t ageSec) {
  if (ageSec >= 30UL * 86400UL) snprintf(out, cap, "30d+");
  else if (ageSec < 60) snprintf(out, cap, "now");
  else if (ageSec < 3600) snprintf(out, cap, "%lum", (unsigned long)(ageSec / 60));
  else if (ageSec < 86400) snprintf(out, cap, "%luh", (unsigned long)(ageSec / 3600));
  else snprintf(out, cap, "%lud", (unsigned long)(ageSec / 86400));
}

}  // namespace

void stampText(char* out, size_t cap, const Stamp& s, uint32_t nowWall, uint32_t nowUptime,
               bool nowReal, StampForm form) {
  if (!out || cap == 0) return;
  out[0] = 0;

  // A real time is shown only when BOTH the event and the clock now are real. The fixed cut-off
  // rules out the 2024 VolatileRTCClock, and the future check rules out a stamp a clock jump left
  // ahead of us.
  const bool realTime = nowReal && s.wallReal && stampIsReal(s.wall) &&
                        s.wall <= nowWall + kFutureSlackSec;
  if (realTime) {
    int sy = 0, sm = 0, sd = 0, sh = 0, smin = 0, swd = 0;
    int ny = 0, nm = 0, nd = 0, nh = 0, nmin = 0, nwd = 0;
    if (localCivil(s.wall, sy, sm, sd, sh, smin, &swd) &&
        localCivil(nowWall, ny, nm, nd, nh, nmin, &nwd)) {
      const int64_t sdays = daysFromCivil(sy, sm, sd);
      const int64_t ndays = daysFromCivil(ny, nm, nd);
      const long long dayDiff = (long long)(ndays - sdays);
      if (dayDiff == 0) {
        snprintf(out, cap, "%02d:%02d", sh, smin);
        return;
      }
      if (dayDiff > 0 && dayDiff < 7) {
        if (form == StampForm::DayTime) {
          static const char* const kWeekday[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
          const int wd = swd >= 0 && swd < 7 ? swd : 0;
          snprintf(out, cap, "%s %02d:%02d", kWeekday[wd], sh, smin);
        } else {
          // A narrow column has room for five glyphs: the date alone.
          snprintf(out, cap, "%02d.%02d", sd, sm);
        }
        return;
      }
      // Older than a week (or a real stamp in the future inside the slack: still a time).
      if (form == StampForm::DayTime) snprintf(out, cap, "%02d.%02d. %02d:%02d", sd, sm, sh, smin);
      else snprintf(out, cap, "%02d.%02d", sd, sm);
      return;
    }
  }

  // The age fallback. Prefer the monotonic uptime: a clock sync between the event and now must not
  // turn "5m" into "6h". A contact loaded from flash has no uptime, so its wall difference is the
  // only honest age left. A wall stamp before the cut-off is the old clock: no age, "--".
  uint32_t age = 0;
  bool haveAge = false;
  if (s.uptime != 0 && nowUptime >= s.uptime) {
    age = nowUptime - s.uptime;
    haveAge = true;
  } else if (s.wall != 0 && stampIsReal(s.wall) && s.wall <= nowWall) {
    age = nowWall - s.wall;
    haveAge = true;
  } else if (s.wall != 0 && stampIsReal(s.wall) && s.wall > nowWall) {
    // A stamp in the future: the clock moved after it was stored. "now" is the honest age.
    age = 0;
    haveAge = true;
  }
  if (!haveAge) {
    snprintf(out, cap, "--");
    return;
  }
  ageFallback(out, cap, age);
}

}  // namespace ui
}  // namespace om
