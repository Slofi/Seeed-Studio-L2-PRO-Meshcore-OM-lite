// SPDX-License-Identifier: MIT
// EST-385: the Wi-Fi NTP retry schedule, the server rotation and the stop rule, with the system g++.
//
// It proves, over the same header the firmware compiles:
//   * the ticket's schedule: attempt 1 at 0 s, attempt 2 at 30 s, attempt 3 at 2 min (counted from the
//     connection, so a slow attempt cannot push the next one out), then every 10 min;
//   * the rotation: pool.ntp.org, time.google.com, 162.159.200.1, the default gateway, then round
//     again; a success keeps the 10 min cadence and the rotation;
//   * the stop rule against the REAL timeSourcePriority(): a GPS fix stops the schedule, Wi-Fi/phone/
//     manual/gps-nofix do not, and a plan that is not wanted is never due;
//   * one attempt at a time, `ntp now` on an idle schedule, the attempt window and millis() wrap.
#include <stdio.h>
#include <string.h>

#include "ntp_plan.h"
#include "timekeeper.h"

using namespace om;
using namespace om::ntp;

static int g_fail = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL %s\n", what);
    ++g_fail;
  }
}

// ---- the rotation table -------------------------------------------------------------------------
static void testServers() {
  check(kServerCount == 4, "four servers in the rotation");
  check(strcmp(kServers[0].text, "pool.ntp.org") == 0 && kServers[0].kind == ServerKind::Name,
        "first is pool.ntp.org by name");
  check(strcmp(kServers[1].text, "time.google.com") == 0 && kServers[1].kind == ServerKind::Name,
        "second is time.google.com by name");
  check(strcmp(kServers[2].text, "162.159.200.1") == 0 && kServers[2].kind == ServerKind::Ip,
        "third is the fixed public IP");
  check(strcmp(kServers[3].text, "gateway") == 0 && kServers[3].kind == ServerKind::Gateway,
        "fourth is the default gateway");
  // The gateway's token is a word, never an address: a log can print it as it is.
  const char* word = kServers[3].text;
  check(strchr(word, '.') == nullptr && strchr(word, ':') == nullptr,
        "the gateway token carries no address");
  for (int i = 0; i < kServerCount; ++i) {
    check(kServers[i].text[0] != 0, "every server has a log token");
    check(strcmp(serverKindToken(kServers[i].kind), "") != 0, "every kind has a token");
  }
  // The result tokens are distinct words (they are printed in `ntp status`).
  const Result all[] = {Result::None, Result::Ok, Result::Timeout, Result::DnsFail, Result::NotStarted,
                        Result::Held};
  for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
    check(strchr(resultToken(all[i]), ' ') == nullptr, "a result token has no space");
    for (size_t j = i + 1; j < sizeof(all) / sizeof(all[0]); ++j) {
      check(strcmp(resultToken(all[i]), resultToken(all[j])) != 0, "result tokens are distinct");
    }
  }
}

// ---- the schedule: 0 s, 30 s, 2 min, then every 10 min -----------------------------------------
static void testSchedule() {
  Plan p;
  check(!p.linked(), "a fresh plan is not linked");
  check(!p.due(0, true), "nothing is due before a connection");

  const uint32_t t0 = 100000;
  p.connected(t0);
  check(p.linked(), "connected arms the plan");
  check(p.attempts() == 0, "the connection itself is not an attempt");
  check(p.due(t0, true), "attempt 1 is due at once (0 s)");
  check(!p.due(t0 - 1, true), "not one ms early");
  check(strcmp(p.nextServer().text, "pool.ntp.org") == 0, "the next server is the first");

  check(p.start(t0), "attempt 1 starts");
  check(p.active(), "the attempt is in flight");
  check(p.attempts() == 1, "one attempt counted");
  check(strcmp(p.server().text, "pool.ntp.org") == 0, "attempt 1 uses pool.ntp.org");
  check(!p.due(t0 + kSlotMs[1], true), "no second attempt while one is in flight");
  check(!p.start(t0 + 1), "a second start is refused");
  check(!p.expired(t0 + kAttemptWindowMs - 1), "the window is not over yet");
  check(p.expired(t0 + kAttemptWindowMs), "the window is 25 s");

  // A timeout: the next slot is counted from the CONNECTION (t0 + 30 s), not from the end.
  p.result(Result::Timeout, t0 + kAttemptWindowMs);
  check(!p.active(), "a result closes the attempt");
  check(p.lastResult() == Result::Timeout, "the result is kept");
  check(p.lastServer() == 0, "the last attempt's server is kept");
  check(p.lastMs() == kAttemptWindowMs, "the attempt's duration is kept");
  check(p.nextAtMs() == t0 + kSlotMs[1], "the next attempt is at 30 s from the connection");
  check(p.nextInSec(t0 + kAttemptWindowMs) == 5, "5 s left when the attempt used its 25 s");
  check(!p.due(t0 + kSlotMs[1] - 1, true), "not due one ms before 30 s");
  check(p.due(t0 + kSlotMs[1], true), "due at 30 s");
  check(strcmp(p.nextServer().text, "time.google.com") == 0, "attempt 2 rotates to time.google.com");

  check(p.start(t0 + kSlotMs[1]), "attempt 2 starts");
  check(strcmp(p.server().text, "time.google.com") == 0, "attempt 2's server");
  p.result(Result::Timeout, t0 + kSlotMs[1] + 1000);
  check(p.nextAtMs() == t0 + kSlotMs[2], "the third attempt is at 2 min from the connection");

  check(p.start(t0 + kSlotMs[2]), "attempt 3 starts");
  check(strcmp(p.server().text, "162.159.200.1") == 0, "attempt 3 uses the fixed public IP");
  p.result(Result::Timeout, t0 + kSlotMs[2] + 1000);
  check(p.nextAtMs() == t0 + kSlotMs[2] + 1000 + kRepeatMs, "after the third: 10 min from its end");

  const uint32_t fourth = p.nextAtMs();
  check(p.start(fourth), "attempt 4 starts");
  check(strcmp(p.server().text, "gateway") == 0, "attempt 4 uses the default gateway");
  p.result(Result::Timeout, fourth + 1000);
  const uint32_t fifth = p.nextAtMs();
  check(fifth == fourth + 1000 + kRepeatMs, "every 10 min afterwards");

  check(p.start(fifth), "attempt 5 starts");
  check(strcmp(p.server().text, "pool.ntp.org") == 0, "the rotation wraps to pool.ntp.org");

  // A success does not stop the schedule: it is refreshed every 10 min, rotation continues.
  p.result(Result::Ok, fifth + 200);
  check(p.lastResult() == Result::Ok, "an ok result is kept");
  check(p.nextAtMs() == fifth + 200 + kRepeatMs, "a success schedules the next refresh in 10 min");
  check(strcmp(p.nextServer().text, "time.google.com") == 0, "the rotation continues after a success");
  check(p.due(fifth + 200 + kRepeatMs, true), "the refresh is due 10 min later");
}

// ---- the stop rule ------------------------------------------------------------------------------
static void testStopRule() {
  // Against the real ranking (src/timekeeper.cpp), so the two numbers cannot drift apart.
  check(timeSourcePriority(TimeSource::Gps) == 5, "GPS fix is priority 5");
  check(timeSourcePriority(TimeSource::Wifi) == 4, "Wi-Fi is priority 4");
  check(kWifiPriority == timeSourcePriority(TimeSource::Wifi), "the model's number is the real one");
  check(timeSourcePriority(TimeSource::Gps) > kWifiPriority, "a GPS fix is better than Wi-Fi");
  check(timeSourcePriority(TimeSource::Phone) < kWifiPriority, "the phone is lower");
  check(timeSourcePriority(TimeSource::Manual) < kWifiPriority, "manual is lower");
  check(timeSourcePriority(TimeSource::GpsNoFix) < kWifiPriority, "gps-nofix is lower");
  check(timeSourcePriority(TimeSource::None) < kWifiPriority, "no source is lower");

  // A real clock from a GPS fix stops the schedule; nothing else does.
  check(!wanted(true, timeSourcePriority(TimeSource::Gps)), "a GPS fix stops the retries");
  check(wanted(true, timeSourcePriority(TimeSource::Wifi)), "Wi-Fi keeps refreshing itself");
  check(wanted(true, timeSourcePriority(TimeSource::Phone)), "a phone time does not stop Wi-Fi");
  check(wanted(true, timeSourcePriority(TimeSource::Manual)), "a manual time does not stop Wi-Fi");
  check(wanted(true, timeSourcePriority(TimeSource::GpsNoFix)), "gps-nofix does not stop Wi-Fi");
  check(wanted(false, timeSourcePriority(TimeSource::Gps)), "no real clock means keep trying");

  // A plan that is not wanted is never due, however overdue it is.
  Plan p;
  p.connected(1000);
  check(p.due(1000, true), "due when wanted");
  check(!p.due(1000, false), "not due when a better source holds the clock");
  check(!p.due(1000 + kRepeatMs * 5, false), "still not due, however late");
  check(p.due(1000 + kRepeatMs * 5, true), "and due again when the better source is gone");
}

// ---- ntp now, the window and millis() wrap ------------------------------------------------------
static void testForceAndWrap() {
  Plan p;
  p.connected(5000);
  check(p.force(5000), "force on an idle plan is accepted");
  check(p.due(5000, true), "forced: due now");
  check(p.start(5000), "forced attempt starts");
  check(!p.force(6000), "force is refused while an attempt is in flight");
  p.result(Result::Timeout, 6000);
  check(p.nextAtMs() > 6000, "the result scheduled a later slot");
  check(p.force(6000), "force is accepted again after the result");
  check(p.due(6000, true), "forced: due now");
  check(!p.due(6000, false), "but never due when a better source holds the clock");

  // millis() wrap: every wait is a subtraction, so the slots survive the rollover.
  Plan w;
  const uint32_t start = 0xFFFFFF00u;
  w.connected(start);
  check(w.due(start, true), "due across the wrap at 0 s");
  check(w.start(start), "attempt starts across the wrap");
  w.result(Result::Timeout, start + 2000);
  check(w.nextAtMs() == start + kSlotMs[1], "the 30 s slot wraps correctly");
  check(!w.due(start + kSlotMs[1] - 1, true), "not due one ms early across the wrap");
  check(w.due(start + kSlotMs[1], true), "due at 30 s across the wrap");

  // reset() drops the link and the schedule.
  w.reset();
  check(!w.linked() && !w.active(), "reset drops the schedule");
  check(!w.due(start + kSlotMs[1], true), "nothing is due after reset");
  check(!w.start(start + kSlotMs[1]), "nothing starts after reset");
}

int main() {
  testServers();
  testSchedule();
  testStopRule();
  testForceAndWrap();
  if (g_fail) {
    printf("est385: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("est385: ntp retry schedule, server rotation and stop rule ok\n");
  return 0;
}
