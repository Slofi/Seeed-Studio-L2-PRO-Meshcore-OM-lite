// SPDX-License-Identifier: MIT
// EST-385: the Wi-Fi NTP retry schedule and server rotation, as plain C++ so test/host proves it and
// the firmware compiles the very same rules. The radio side (src/wifi_link.cpp) only reports facts:
// an attempt started, the lwIP client answered, the attempt timed out, the link went down.
//
// Why a model at all: the measured bug was that ONE attempt per connection was made and the code gave
// up after 20 s, while lwIP's own first request can be delayed up to 5 s and its own receive timeout is
// 15 s (SNTP_STARTUP_DELAY_FUNC, SNTP_RECV_TIMEOUT in the bundled lwIP: see PROGRESS.md EST-385). A
// retry schedule is policy, not radio work, so it is decided here and cannot drift from the tests.
//
// Stop rule (Filip, EST-383/385): keep trying while the Wi-Fi link is up and the clock is NOT held by a
// source better than Wi-Fi. Only a GPS fix ranks above Wi-Fi (src/timekeeper.cpp: GPS 5, WiFi 4,
// phone 3, manual 2, gps-nofix 1), so a phone or hand-set time never stops the retries; a GPS fix does.
#pragma once

#include <stdint.h>

#include "timekeeper.h"   // kMinSaneEpoch / kMaxSaneEpoch: the one set of bounds

namespace om {
namespace ntp {

// --------------------------------------------------------------------------------- the reply capture
// EST-391. The SNTP capture's sanity check, in ONE place and compared in a 64-bit integer. On the
// device's toolchain (esp-2021r2-patch5, GCC 8.4, xtensa-esp32s3) sizeof(time_t) == 4, so the old
// check `tv->tv_sec >= (time_t)om::kMaxSaneEpoch` compared against -192522496 (the assembled `.word`)
// and was TRUE for every real reply: the capture threw all of them away, which is exactly the device's
// "Wi-Fi time never arrives" (reach=1, sync=0, notify=0). Widening the seconds to int64_t BEFORE the
// comparison is the whole fix; the bounds stay the timekeeper's own.
inline bool replyEpochSane(int64_t sec) {
  return sec >= (int64_t)om::kMinSaneEpoch && sec < (int64_t)om::kMaxSaneEpoch;
}

// ------------------------------------------------------------------------------- the server rotation
// Per attempt, in this order, then back to the first: a pool name, a second public name, a fixed
// public IP (Cloudflare, no DNS at all) and the DHCP default gateway (many routers answer NTP). The
// `text` of every entry is the only thing a log or an answer may print; the gateway's ADDRESS is never
// printed, its text is the word "gateway" (REBUILD.md rule 9: no IPs of the user's network).
enum class ServerKind : uint8_t { Name = 0, Ip, Gateway };

struct Server {
  ServerKind kind = ServerKind::Name;
  const char* text = "";
};

constexpr int kServerCount = 4;
constexpr Server kServers[kServerCount] = {
    {ServerKind::Name, "pool.ntp.org"},
    {ServerKind::Name, "time.google.com"},
    {ServerKind::Ip, "162.159.200.1"},   // Cloudflare, a PUBLIC address (allowed in a log)
    {ServerKind::Gateway, "gateway"},    // the DHCP default gateway; its address is never printed
};

inline const char* serverKindToken(ServerKind k) {
  switch (k) {
    case ServerKind::Name: return "dns";
    case ServerKind::Ip: return "ip";
    case ServerKind::Gateway: return "gateway";
    default: return "dns";
  }
}

// ---------------------------------------------------------------------------------------- the timing
// "Attempts at 0 s, 30 s, 2 min, then every 10 min" (the ticket). The first three are offsets from the
// moment the station connected, so a slow attempt cannot push the next one out; after the third, and
// after every success, the next attempt is 10 min after the previous one ended.
constexpr int kFirstSlots = 3;
constexpr uint32_t kSlotMs[kFirstSlots] = {0, 30UL * 1000UL, 120UL * 1000UL};
constexpr uint32_t kRepeatMs = 10UL * 60UL * 1000UL;

// One attempt's window. The bundled lwIP starts its first request up to 5 s late (SNTP_STARTUP_DELAY
// with LWIP_RAND) and waits SNTP_RECV_TIMEOUT = 15 s for the reply, then re-sends; 25 s leaves room for
// that first retry without ever overlapping the next slot (30 s).
constexpr uint32_t kAttemptWindowMs = 25UL * 1000UL;

// Our own DNS lookup, non-blocking: polled by the loop, never WiFi.hostByName() (which blocks up to
// 16 s + 15 s and would stall the mesh loop).
constexpr uint32_t kDnsTimeoutMs = 3UL * 1000UL;

// --------------------------------------------------------------------------------------- the result
enum class Result : uint8_t { None = 0, Ok, Timeout, DnsFail, NotStarted, Held };

constexpr const char* resultToken(Result r) {
  switch (r) {
    case Result::None: return "none";
    case Result::Ok: return "ok";
    case Result::Timeout: return "timeout";
    case Result::DnsFail: return "dns-fail";
    case Result::NotStarted: return "not-started";
    case Result::Held: return "held";
    default: return "none";
  }
}

// ------------------------------------------------------------------------------------- the stop rule
// timeSourcePriority(TimeSource::Wifi) is 4 (src/timekeeper.cpp). test_ntp.cpp pins this number against
// the real function so the two cannot drift.
constexpr int kWifiPriority = 4;

// False only when a real clock is held by a BETTER source (a GPS fix, priority 5).
constexpr bool wanted(bool clockReal, int sourcePriority) {
  return !(clockReal && sourcePriority > kWifiPriority);
}

// -------------------------------------------------------------------------------------- the schedule
class Plan {
 public:
  void reset() { *this = Plan(); }

  // The station connected: attempt 0 is due at once.
  void connected(uint32_t nowMs) {
    linked_ = true;
    active_ = false;
    attempts_ = 0;
    linkedAt_ = nowMs;
    nextAt_ = nowMs;
    activeFrom_ = 0;
    server_ = 0;
    lastServer_ = -1;
    lastResult_ = Result::None;
    lastAt_ = 0;
    lastMs_ = 0;
  }

  bool linked() const { return linked_; }

  // True when the caller may start an attempt now: the link is up, nothing is in flight, and the stop
  // rule allows it. `want` is `ntp::wanted(timeKeeper().realSinceBoot(), priority)`.
  bool due(uint32_t nowMs, bool want) const {
    if (!linked_ || active_ || !want) return false;
    return (int32_t)(nowMs - nextAt_) >= 0;
  }

  // Begins the next attempt and returns false when there was nothing to start.
  bool start(uint32_t nowMs) {
    if (!linked_ || active_) return false;
    server_ = (int)(attempts_ % (uint32_t)kServerCount);
    active_ = true;
    activeFrom_ = nowMs;
    ++attempts_;
    return true;
  }

  // Ends the in-flight attempt and schedules the next one.
  void result(Result r, uint32_t nowMs) {
    if (!active_) return;
    active_ = false;
    lastServer_ = server_;
    lastResult_ = r;
    lastAt_ = nowMs;
    lastMs_ = nowMs - activeFrom_;
    if (r == Result::Ok) {
      nextAt_ = nowMs + kRepeatMs;   // a good answer is refreshed every 10 min
    } else if (attempts_ < (uint32_t)kFirstSlots) {
      nextAt_ = linkedAt_ + kSlotMs[attempts_];   // 30 s, then 2 min, counted from the connection
    } else {
      nextAt_ = nowMs + kRepeatMs;                // every 10 min
    }
  }

  // `ntp now`: make the next attempt due immediately. False when the link is down or one is in flight.
  bool force(uint32_t nowMs) {
    if (!linked_ || active_) return false;
    nextAt_ = nowMs;
    return true;
  }

  bool active() const { return active_; }
  bool expired(uint32_t nowMs) const {
    return active_ && (uint32_t)(nowMs - activeFrom_) >= kAttemptWindowMs;
  }
  uint32_t attemptAgeMs(uint32_t nowMs) const { return nowMs - activeFrom_; }

  uint32_t attempts() const { return attempts_; }
  const Server& server() const { return kServers[server_]; }
  int serverIndex() const { return server_; }
  // The server the NEXT attempt will use (the rotation's next slot).
  const Server& nextServer() const { return kServers[(int)(attempts_ % (uint32_t)kServerCount)]; }

  Result lastResult() const { return lastResult_; }
  int lastServer() const { return lastServer_; }
  uint32_t lastAtMs() const { return lastAt_; }
  uint32_t lastMs() const { return lastMs_; }

  uint32_t nextAtMs() const { return nextAt_; }
  // Seconds until the next attempt: 0 while one is in flight or already due.
  uint32_t nextInSec(uint32_t nowMs) const {
    if (active_) return 0;
    const int32_t left = (int32_t)(nextAt_ - nowMs);
    return left <= 0 ? 0 : (uint32_t)left / 1000U;
  }

 private:
  bool linked_ = false;
  bool active_ = false;
  uint32_t linkedAt_ = 0;
  uint32_t nextAt_ = 0;
  uint32_t activeFrom_ = 0;
  uint32_t attempts_ = 0;
  int server_ = 0;
  int lastServer_ = -1;
  Result lastResult_ = Result::None;
  uint32_t lastAt_ = 0;
  uint32_t lastMs_ = 0;
};

}  // namespace ntp
}  // namespace om
