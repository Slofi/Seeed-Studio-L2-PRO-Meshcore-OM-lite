// SPDX-License-Identifier: MIT
// EST-387: may this boot touch the L76K's reset line?
//
// The GNSS power rail (expander bit 13) is never dropped: the PCA9554 keeps its latched outputs while
// the ESP32 restarts, so across a software restart, an OTA reboot, a panic or a watchdog restart the
// module keeps running with its RTC, almanac and ephemeris intact. Until EST-387 `expanderBegin()`
// pulsed the reset line on EVERY boot, i.e. every flash and every crash restart paid for a cold start
// and threw the module's warm state away (measured by Filip/Claude 2026-11-11: indoors, one satellite
// at 29..32 dB-Hz that comes and goes, and no time because the receiver had just restarted).
//
// So: assert the reset only when the SoC really lost power (or browned out). Every other reason is a
// restart of the SoC alone, and the receiver is left exactly as it is.
//
// Plain C++ (no Arduino, no SDK) so test/host proves the table with the system g++; board.cpp maps
// esp_reset_reason_t onto these values and static_asserts the numbers, so the two cannot drift.
#pragma once

#include <stdint.h>

namespace om {

// The values are ESP-IDF's esp_reset_reason_t, by number, because this header stays free of the SDK.
enum class GpsBootReason : int {
  Unknown = 0,      // ESP_RST_UNKNOWN
  PowerOn = 1,      // ESP_RST_POWERON
  External = 2,     // ESP_RST_EXT
  Software = 3,     // ESP_RST_SW (ESP.restart(), OTA reboot)
  Panic = 4,        // ESP_RST_PANIC
  IntWdt = 5,       // ESP_RST_INT_WDT
  TaskWdt = 6,      // ESP_RST_TASK_WDT
  Wdt = 7,          // ESP_RST_WDT
  DeepSleep = 8,    // ESP_RST_DEEPSLEEP
  Brownout = 9,     // ESP_RST_BROWNOUT
  Sdio = 10,        // ESP_RST_SDIO
};

inline GpsBootReason gpsBootReasonOf(int reason) {
  switch (reason) {
    case (int)GpsBootReason::PowerOn: return GpsBootReason::PowerOn;
    case (int)GpsBootReason::External: return GpsBootReason::External;
    case (int)GpsBootReason::Software: return GpsBootReason::Software;
    case (int)GpsBootReason::Panic: return GpsBootReason::Panic;
    case (int)GpsBootReason::IntWdt: return GpsBootReason::IntWdt;
    case (int)GpsBootReason::TaskWdt: return GpsBootReason::TaskWdt;
    case (int)GpsBootReason::Wdt: return GpsBootReason::Wdt;
    case (int)GpsBootReason::DeepSleep: return GpsBootReason::DeepSleep;
    case (int)GpsBootReason::Brownout: return GpsBootReason::Brownout;
    case (int)GpsBootReason::Sdio: return GpsBootReason::Sdio;
    default: return GpsBootReason::Unknown;
  }
}

// The one decision, as a table, so the whole rule reads in one place:
//
//   PowerOn    RESET  the rail came up from nothing: the module has no state worth keeping.
//   Brownout   RESET  the 3V3 rail sagged (the L2's power switch, a flat battery): the module may
//                     have restarted or half-restarted, so give it a clean one.
//   DeepSleep  RESET  the rails were down; nothing in this firmware deep-sleeps today, but a future
//                     duty cycle must never assume a running receiver after a wake.
//   Unknown    RESET  the reason could not be read, i.e. the RTC domain that carries it was lost:
//                     that is what a cold start looks like. A needless reset costs one start; a
//                     wrongly kept receiver after a real power cut costs the whole boot.
//   Software, External, Panic, IntWdt, TaskWdt, Wdt, Sdio   KEEP   the SoC restarted, the module did
//                     not: its time, almanac and ephemeris are still good (this is the fix).
inline bool gpsBootResetsReceiver(GpsBootReason reason) {
  switch (reason) {
    case GpsBootReason::PowerOn:
    case GpsBootReason::Brownout:
    case GpsBootReason::DeepSleep:
    case GpsBootReason::Unknown:
      return true;
    case GpsBootReason::Software:
    case GpsBootReason::External:
    case GpsBootReason::Panic:
    case GpsBootReason::IntWdt:
    case GpsBootReason::TaskWdt:
    case GpsBootReason::Wdt:
    case GpsBootReason::Sdio:
      return false;
  }
  return true;   // unreachable; a reason this code has never seen is treated like Unknown
}

// The word in `[gps] boot: reset (<word>)`. Same vocabulary as crash_report.cpp's reset tokens, so a
// log reader can match the two lines.
inline const char* gpsBootReasonToken(GpsBootReason reason) {
  switch (reason) {
    case GpsBootReason::PowerOn: return "power-on";
    case GpsBootReason::External: return "external";
    case GpsBootReason::Software: return "software";
    case GpsBootReason::Panic: return "panic";
    case GpsBootReason::IntWdt: return "int-wdt";
    case GpsBootReason::TaskWdt: return "task-wdt";
    case GpsBootReason::Wdt: return "wdt";
    case GpsBootReason::DeepSleep: return "deep-sleep";
    case GpsBootReason::Brownout: return "brownout";
    case GpsBootReason::Sdio: return "sdio";
    default: return "unknown";
  }
}

}  // namespace om
