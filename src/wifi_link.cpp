// SPDX-License-Identifier: MIT
// The Wi-Fi station's radio side. See wifi_link.h for the rules and wifi_model.h for the state.
#include "wifi_link.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <lwip/dns.h>
#include <string.h>
#include <sys/time.h>

#include "ble_link.h"
#include "mem_stats.h"
#include "ntp_plan.h"
#include "ntp_sleep.h"
#include "timekeeper.h"

// The SNTP reply is captured here instead of being written straight into the system clock. The
// ESP-IDF declaration is weak (`nm liblwip.a` shows W sntp_sync_time), so this definition replaces
// it; the loop task then feeds the epoch through timeKeeper().setFromSource(), which keeps GPS the
// higher source and refuses a reply below the saved floor. tv is written by the SNTP task, so the
// two words it touches are volatile and read once by the loop.
//
// EST-385: on THIS IDF (4.4) `sntp_set_system_time()` calls `sntp_sync_time()` directly (checked in
// the built ELF: the literal at 0x4207fd2c holds our 0x4216fb70), so replacing it is enough and the
// notification callback IDF's own body would call never fires. The callback is still registered as a
// second, independent capture (the ticket keeps both hooks) and counts its calls so a future core that
// does call it shows up in the log instead of silently changing behaviour.
static volatile uint32_t s_ntpEpoch = 0;
static volatile bool s_ntpGot = false;
static volatile uint32_t s_ntpNotifyCount = 0;

extern "C" void sntp_sync_time(struct timeval* tv) {
  if (tv == nullptr) return;
  if (tv->tv_sec < (time_t)om::kMinSaneEpoch || tv->tv_sec >= (time_t)om::kMaxSaneEpoch) return;
  s_ntpEpoch = (uint32_t)tv->tv_sec;
  s_ntpGot = true;
}

// The notification callback is the second, independent capture: lwIP calls it after sntp_sync_time()
// (IDF's own sntp_sync_time() body does; ours replaces that body, so today this counts 0). It only
// touches volatiles, because it runs in the tcpip thread.
static void sntpNotify(struct timeval* tv) {
  ++s_ntpNotifyCount;
  if (tv == nullptr) return;
  if (tv->tv_sec < (time_t)om::kMinSaneEpoch || tv->tv_sec >= (time_t)om::kMaxSaneEpoch) return;
  s_ntpEpoch = (uint32_t)tv->tv_sec;
  s_ntpGot = true;
}

namespace om {
namespace {

constexpr const char* kPrefsNs = "omwifi";
constexpr uint32_t kStoreMagic = 0x57494649u;   // "WIFI"
constexpr uint16_t kStoreVersion = 1;

// The one NVS value: magic, version, count and the five entries. It is small (under 500 B) and is
// written only when the saved list changes, never on a scan or a switch.
struct StoreBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  wifi::SavedNetwork nets[wifi::kMaxSaved];
};

Preferences s_prefs;
wifi::SavedStore s_saved;
wifi::ScanList s_scan;
wifi::Machine s_machine;
// EST-355: the scan a user asked for, and the driver's permission to run it. The request and the
// driver call are separated because the driver refuses a scan for a moment after the switch goes on
// (the device evidence: the first scan failed, the second found 12 networks).
wifi::ScanGate s_scanGate;

bool s_enabled = false;        // the user's switch, persisted in NVS
bool s_driverUp = false;
uint32_t s_serial = 1;
uint32_t s_serviceWorstUs = 0;

// Auto-connect: after the switch goes on, try every saved network once, in list order. A user
// connect cancels the pass.
bool s_autoActive = false;
int s_autoNext = 0;

// The attempt's secret. It lives only in this buffer and is wiped when the attempt ends (success,
// refusal, timeout, switch off); it is never printed and never copied to a log string.
char s_pendingPass[wifi::kPassBytes + 1] = "";
bool s_rememberPending = false;

// EST-385: the retry schedule and the server rotation live in ntp_plan.h (plain C++, host-tested).
// One attempt at a time; s_ntpActive says the lwIP client is running for it.
ntp::Plan s_ntp;
bool s_ntpActive = false;
// EST-386: owns the attempt's modem-sleep change (ntp_sleep.h). It is the only code that turns sleep
// off and the only code that may turn it back on: see the header for the Bluetooth rule.
ntp::SleepGuard s_ntpSleep;
uint32_t s_ntpInitMs = 0;
bool s_ntpChecked = false;      // the "did the client really start" line was printed
bool s_ntpStopLogged = false;   // the "a better source holds the clock" stop was printed

// Our own DNS lookup, so the measurement does not depend on the client's own resolver and the loop
// never blocks (WiFi.hostByName() waits up to 16 s + 15 s). The callback runs in the tcpip thread, so
// these are written there and read once by the loop: 0 idle, 1 in progress, 2 ok, 3 failed.
volatile uint32_t s_dnsState = 0;
volatile uint32_t s_dnsAddr = 0;
uint32_t s_dnsStartMs = 0;

void autoTryNext(uint32_t nowMs);

void bump() {
  ++s_serial;
  if (s_serial == 0) s_serial = 1;
}

void clearPendingPass() {
  memset(s_pendingPass, 0, sizeof(s_pendingPass));
  s_rememberPending = false;
}

void saveEnabled() { s_prefs.putBool("on", s_enabled); }

void saveStore() {
  StoreBlob blob{};
  blob.magic = kStoreMagic;
  blob.version = kStoreVersion;
  blob.count = (uint16_t)s_saved.count();
  for (int i = 0; i < s_saved.count(); ++i) {
    const wifi::SavedNetwork* n = s_saved.at(i);
    if (n != nullptr) blob.nets[i] = *n;
  }
  const bool ok = s_prefs.putBytes("nets", &blob, sizeof(blob)) == sizeof(blob);
  memset(&blob, 0, sizeof(blob));
  if (!ok) Serial.println("[wifi] saved networks NOT written to NVS");
}

void loadStore() {
  s_saved.clear();
  StoreBlob blob{};
  const size_t len = s_prefs.getBytesLength("nets");
  const size_t got =
      len == sizeof(blob) ? s_prefs.getBytes("nets", &blob, sizeof(blob)) : (size_t)0;
  if (got == sizeof(blob) && blob.magic == kStoreMagic && blob.version == kStoreVersion &&
      blob.count <= (uint16_t)wifi::kMaxSaved) {
    for (int i = 0; i < (int)blob.count; ++i) {
      if (blob.nets[i].ssid[0] != 0) s_saved.add(blob.nets[i].ssid, blob.nets[i].password);
    }
  }
  memset(&blob, 0, sizeof(blob));
}

// ------------------------------------------------------------------------------------------------- NTP
// EST-385. The device measured one query per connection and a 20 s give-up ("NTP: no answer") while
// the same LAN answered a PC in milliseconds. On THIS stack (Arduino-ESP32 2.0.17 / ESP-IDF 4.4):
//  - esp_sntp_init()/esp_sntp_stop() are marshalled to the tcpip thread with tcpip_callback(), and
//    esp_sntp_setserver()/setservername() with the blocking tcpip_api_call() (relocations in
//    liblwip.a:sntp.c.obj). The IDF wrappers are therefore already thread-safe and must NOT be wrapped
//    in LOCK_TCPIP_CORE(): that macro does not even exist in this build, because
//    CONFIG_LWIP_TCPIP_CORE_LOCKING is not set (sdkconfig:1449) and lwipopts.h:58-61 leaves
//    LOCK_TCPIP_CORE/UNLOCK_TCPIP_CORE undefined. LWIP_ASSERT_CORE_LOCKED() is the empty default
//    (lwip/opt.h:226-228; CONFIG_LWIP_CHECK_THREAD_SAFETY is not set, sdkconfig:1450), so a call in the
//    wrong thread fails SILENTLY. That is why the client's own state is logged and checked below.
//  - SNTP_SERVER_DNS is 1 (lwipopts.h:1209): a name is resolved by lwIP in the tcpip thread.
//  - The first request is delayed by LWIP_RAND() % 5000 ms (LWIP_RAND is lwipopts.h:86, so
//    sntp_opts.h:144-157 turns SNTP_STARTUP_DELAY on); SNTP_RECV_TIMEOUT and SNTP_RETRY_TIMEOUT are the
//    15 s default (sntp_opts.h:163-190). One 20 s window can cut the first retry exactly where it is
//    due, so an attempt now gets 25 s and every attempt is retried on a schedule.
//  - CONFIG_LWIP_SNTP_DEBUG is not set, so lwIP prints nothing per send/receive. The per-exchange
//    evidence the client exposes is the capture hooks, esp_sntp_enabled(), the address it holds and the
//    sync status; all of those are logged.
// No configTime(): it would overwrite TZ, and the timekeeper owns Ljubljana.

// The only place an NTP log turns an address into text. It is used for the PUBLIC server address; the
// gateway and the station's own address are never passed here.
void ntpIpText(uint32_t addr, char* out, size_t cap) {
  const IPAddress ip(addr);
  snprintf(out, cap, "%u.%u.%u.%u", (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
}

// Count and "set/unset" only, never the addresses (rule 9). DNS_MAX_SERVERS is 3 in this build
// (CONFIG_LWIP_DNS_MAX_SERVERS=3), so 0..2 is a valid index.
void ntpLogDnsServers() {
  bool set[3] = {false, false, false};
  int count = 0;
  for (int i = 0; i < 3; ++i) {
    set[i] = (uint32_t)WiFi.dnsIP((uint8_t)i) != 0;
    if (set[i]) ++count;
  }
  Serial.printf("[ntp] dns=%d main=%s backup=%s fallback=%s (addresses not printed)\n", count,
                set[0] ? "set" : "unset", set[1] ? "set" : "unset", set[2] ? "set" : "unset");
}

// lwIP's DNS answer, in the tcpip thread. Only volatiles: the loop reads them once per pass.
void ntpDnsFound(const char* name, const ip_addr_t* addr, void* arg) {
  (void)name;
  (void)arg;
  if (addr != nullptr && IP_IS_V4(addr) && !ip_addr_isany(addr)) {
    s_dnsAddr = ip4_addr_get_u32(ip_2_ip4(addr));
    s_dnsState = 2;
  } else {
    s_dnsState = 3;
  }
}

void stopNtp() {
  if (s_ntpActive) {
    esp_sntp_stop();   // queued to the tcpip thread; see the block comment above
    s_ntpActive = false;
  }
}

// Starts the lwIP client for the attempt that is open. The server is an IP whenever we already know
// one (a fixed IP, the gateway, or our own successful lookup), so the exchange does not depend on a
// second DNS resolution; only a name we could not resolve is handed to the client as a name.
void ntpStartClient(uint32_t nowMs) {
  const ntp::Server& srv = s_ntp.server();
  uint32_t ip = 0;
  if (srv.kind == ntp::ServerKind::Ip) {
    IPAddress parsed;
    if (parsed.fromString(srv.text)) ip = (uint32_t)parsed;
  } else if (srv.kind == ntp::ServerKind::Gateway) {
    ip = (uint32_t)WiFi.gatewayIP();
  } else if (s_dnsState == 2) {
    ip = s_dnsAddr;
  }

  char text[wifi::kIpTextCap];
  if (srv.kind == ntp::ServerKind::Gateway && ip != 0) {
    const IPAddress gw(ip);
    ip_addr_t a;
    IP_ADDR4(&a, gw[0], gw[1], gw[2], gw[3]);
    esp_sntp_setserver(0, &a);
    // The gateway is a LAN address: the log says the word only (rule 9).
    Serial.println("[ntp] client server=gateway (the DHCP default gateway, its address is not printed)");
  } else if (ip != 0) {
    const IPAddress v(ip);
    ip_addr_t a;
    IP_ADDR4(&a, v[0], v[1], v[2], v[3]);
    esp_sntp_setserver(0, &a);
    ntpIpText(ip, text, sizeof(text));
    Serial.printf("[ntp] client server=%s addr=%s (no second DNS)\n", srv.text, text);
  } else {
    // lwIP's sntp_setservername() stores the POINTER, not a copy: the name must be a table literal.
    // The cached address of the previous attempt is dropped first, so the client really re-resolves.
    esp_sntp_setserver(0, nullptr);
    esp_sntp_setservername(0, srv.text);
    Serial.printf("[ntp] client server=%s addr=unset (the client resolves it)\n", srv.text);
  }

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_set_time_sync_notification_cb(&sntpNotify);
  s_ntpGot = false;
  s_ntpEpoch = 0;
  esp_sntp_init();
  s_ntpActive = true;
  s_ntpInitMs = nowMs;
  s_ntpChecked = false;
}

// Ends the open attempt: the schedule's next slot, the client stopped, modem sleep back to the
// default when this attempt turned it off (EST-386: never over a running BT stack). One line with the
// result and one with the next attempt.
void ntpEndAttempt(ntp::Result r, uint32_t nowMs) {
  const char* server = s_ntp.server().text;
  const uint32_t took = s_ntp.attemptAgeMs(nowMs);
  const uint32_t n = s_ntp.attempts();
  const bool enabled = esp_sntp_enabled();
  s_ntp.result(r, nowMs);
  stopNtp();
  if (s_ntpSleep.end() == ntp::SleepAction::TurnOn) WiFi.setSleep(true);
  Serial.printf("[ntp] attempt %lu server=%s result=%s after %lu ms (client=%d clock=%s)\n",
                (unsigned long)n, server, ntp::resultToken(r), (unsigned long)took, enabled ? 1 : 0,
                timeSourceName(timeKeeper().source()));
  if (r != ntp::Result::Held) {
    Serial.printf("[ntp] next attempt in %lu s (attempt %lu, server=%s)\n",
                  (unsigned long)s_ntp.nextInSec(nowMs), (unsigned long)(s_ntp.attempts() + 1),
                  s_ntp.nextServer().text);
  }
}

// Opens the next attempt: the rotation's server, the DNS picture, modem sleep off while it is in
// flight ONLY when Bluetooth is off (EST-386: the coexistence rule), and our own non-blocking lookup
// for a name.
void ntpBeginAttempt(uint32_t nowMs) {
  if (!s_ntp.start(nowMs)) return;
  const ntp::Server& srv = s_ntp.server();
  stopNtp();   // no client may survive from an earlier attempt
  s_dnsState = 0;
  s_dnsAddr = 0;
  s_dnsStartMs = nowMs;
  s_ntpChecked = false;

  Serial.printf("[ntp] attempt %lu window=%lu ms server=%s (%s)\n", (unsigned long)s_ntp.attempts(),
                (unsigned long)ntp::kAttemptWindowMs, srv.text, ntp::serverKindToken(srv.kind));
  ntpLogDnsServers();
  const bool btOn = om::bleIsEnabled();
  const wifi_ps_type_t ps = WiFi.getSleep();
  if (s_ntpSleep.begin(btOn) == ntp::SleepAction::TurnOff) {
    // Already off counts as applied: Arduino's setSleep() returns false when the type did not change.
    const bool set = WiFi.setSleep(false);
    s_ntpSleep.offResult(set || ps == WIFI_PS_NONE);
    Serial.printf("[ntp] sleep=%s -> off for the attempt (set=%d)\n",
                  ps == WIFI_PS_NONE ? "off" : "on", set ? 1 : 0);
  } else {
    // Bluetooth is on: ESP-IDF refuses PS_NONE while BT is enabled (libpp.a pm.o aborts on this
    // build), so the radio is left alone for the whole attempt.
    Serial.println("[ntp] modem sleep kept on (Bluetooth is on)");
  }

  if (srv.kind == ntp::ServerKind::Name) {
    // dns_gethostbyname() from the loop task is what WiFi.hostByName() does too (WiFiGeneric.cpp:1574);
    // the difference is that this one does not wait: the answer arrives in the callback and the 3 s
    // timeout is polled by the loop.
    ip_addr_t addr;
    const err_t err = dns_gethostbyname(srv.text, &addr, &ntpDnsFound, nullptr);
    if (err == ERR_OK) {
      if (IP_IS_V4(&addr) && !ip_addr_isany(&addr)) {
        s_dnsAddr = ip4_addr_get_u32(ip_2_ip4(&addr));
        s_dnsState = 2;
        char text[wifi::kIpTextCap];
        ntpIpText(s_dnsAddr, text, sizeof(text));
        Serial.printf("[ntp] dns %s ok public_ip=%s in 0 ms\n", srv.text, text);
      } else {
        s_dnsState = 3;
        Serial.printf("[ntp] dns %s failed: no v4 address\n", srv.text);
      }
    } else if (err == ERR_INPROGRESS) {
      s_dnsState = 1;
      Serial.printf("[ntp] dns %s asked, waiting up to %lu ms\n", srv.text,
                    (unsigned long)ntp::kDnsTimeoutMs);
    } else {
      s_dnsState = 3;
      Serial.printf("[ntp] dns %s failed err=%d\n", srv.text, (int)err);
    }
  }
}

// The link went down, the switch went off, or a new connect took over: drop the schedule and the
// client, and put modem sleep back when this attempt turned it off.
void ntpStop(const char* why) {
  if (s_ntp.linked()) {
    Serial.printf("[ntp] stop: %s after %lu attempt(s)\n", why, (unsigned long)s_ntp.attempts());
  }
  s_ntp.result(ntp::Result::Held, millis());
  stopNtp();
  if (s_ntpSleep.end() == ntp::SleepAction::TurnOn && s_driverUp) WiFi.setSleep(true);
  s_ntp.reset();
  s_ntpStopLogged = false;
}

void pollNtp(uint32_t nowMs) {
  if (!s_ntp.linked()) return;

  // EST-386: Bluetooth started while this attempt had modem sleep off. Put sleep back at once, once;
  // the attempt keeps it on to its end. Both run on the loop task, so no BLE change can fall between
  // this check and the setSleep() call.
  if (s_ntpSleep.poll(om::bleIsEnabled()) == ntp::SleepAction::TurnOn) {
    WiFi.setSleep(true);
    Serial.println("[ntp] modem sleep back on (Bluetooth started during the attempt)");
  }

  // The stop rule: only a source better than Wi-Fi (a GPS fix) ends the schedule (ntp_plan.h).
  const bool want = ntp::wanted(timeKeeper().realSinceBoot(),
                                timeSourcePriority(timeKeeper().source()));
  if (!want) {
    if (s_ntp.active()) s_ntp.result(ntp::Result::Held, nowMs);
    if (s_ntpActive) stopNtp();
    if (s_ntpSleep.end() == ntp::SleepAction::TurnOn && s_driverUp) WiFi.setSleep(true);
    if (!s_ntpStopLogged) {
      s_ntpStopLogged = true;
      Serial.printf("[ntp] stop: clock is from %s, better than Wi-Fi (%lu attempt(s))\n",
                    timeSourceName(timeKeeper().source()), (unsigned long)s_ntp.attempts());
    }
    return;
  }

  if (s_ntpGot) {
    const uint32_t epoch = s_ntpEpoch;
    s_ntpGot = false;
    // A reply that arrived after its own attempt was closed is dropped: the next attempt asks again.
    if (!s_ntp.active()) return;
    const bool applied = timeKeeper().setFromSource(epoch, TimeSource::Wifi, millis() / 1000);
    Serial.printf("[wifi] NTP %lu s: %s, source %s\n", (unsigned long)epoch,
                  applied ? "applied" : "kept (a higher source holds the clock)",
                  timeSourceName(timeKeeper().source()));
    ntpEndAttempt(ntp::Result::Ok, nowMs);
    bump();
    return;
  }

  if (!s_ntp.active()) {
    if (s_ntp.due(nowMs, want)) ntpBeginAttempt(nowMs);
    return;
  }

  // A name we resolved ourselves: the client is not started until the answer or the timeout.
  if (s_ntp.server().kind == ntp::ServerKind::Name && s_dnsState == 1) {
    const uint32_t waited = nowMs - s_dnsStartMs;
    if (waited < ntp::kDnsTimeoutMs) return;
    s_dnsState = 3;
    Serial.printf("[ntp] dns %s TIMED OUT after %lu ms; the client keeps the name and resolves it\n",
                  s_ntp.server().text, (unsigned long)waited);
  }

  if (!s_ntpActive) {
    ntpStartClient(nowMs);
    return;
  }

  // 100 ms after init: did the tcpip callback really run, and does the client hold an address? This is
  // the line that tells a silent tcpip_callback failure from a lost packet.
  if (!s_ntpChecked && (uint32_t)(nowMs - s_ntpInitMs) >= 100) {
    s_ntpChecked = true;
    const bool enabled = esp_sntp_enabled();
    const ip_addr_t* server = esp_sntp_getserver(0);
    const bool addrSet = server != nullptr && !ip_addr_isany(server);
    char text[wifi::kIpTextCap];
    const char* addrText = "unset";
    if (addrSet && s_ntp.server().kind == ntp::ServerKind::Gateway) {
      addrText = "set";   // a LAN address is never printed
    } else if (addrSet && server != nullptr && IP_IS_V4(server)) {
      ntpIpText(ip4_addr_get_u32(ip_2_ip4(server)), text, sizeof(text));
      addrText = text;
    }
    // sntp_getreachability(0) is lwIP's own per-server shift register: bit 0 is the LAST exchange.
    // CONFIG_LWIP_SNTP_DEBUG is off on this build, so this is the closest thing to a per-send trace.
    Serial.printf("[ntp] client enabled=%d addr=%s sync=%d notify=%lu reach=%u\n", enabled ? 1 : 0,
                  addrText, (int)esp_sntp_get_sync_status(), (unsigned long)s_ntpNotifyCount,
                  (unsigned)sntp_getreachability(0));
    if (!enabled) {
      // tcpip_callback() failed (its return value is not visible from the esp_sntp_* API) or the
      // client stopped itself: no packet will ever be sent, so do not sit out the whole window.
      Serial.println("[ntp] client did not start: no packet was sent");
      ntpEndAttempt(ntp::Result::NotStarted, nowMs);
      return;
    }
  }

  if (s_ntp.expired(nowMs)) {
    // No reply and no callback in the whole window. If the client never held an address the cause is
    // DNS; otherwise a packet was sent and nothing came back.
    const ip_addr_t* server = esp_sntp_getserver(0);
    const bool addrSet = server != nullptr && !ip_addr_isany(server);
    Serial.printf("[ntp] no answer for %s in %lu ms (enabled=%d addr=%s reach=%u)\n",
                  s_ntp.server().text, (unsigned long)s_ntp.attemptAgeMs(nowMs),
                  esp_sntp_enabled() ? 1 : 0, addrSet ? "set" : "unset",
                  (unsigned)sntp_getreachability(0));
    ntpEndAttempt(addrSet ? ntp::Result::Timeout : ntp::Result::DnsFail, nowMs);
  }
}

void onAttemptFailed(const char* reason) {
  clearPendingPass();
  WiFi.disconnect(false, false);   // stop a late association after the timeout says failed
  bump();
  Serial.printf("[wifi] connect failed: %s\n", reason != nullptr ? reason : "failed");
  if (s_autoActive) {
    // The auto pass moves to the next saved network; the last failure stays as the page's reason
    // when none connects.
    s_autoActive = false;
    autoTryNext(millis());
  }
}

void onConnected(uint32_t nowMs) {
  const IPAddress ip = WiFi.localIP();
  char ipText[wifi::kIpTextCap];
  snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", (unsigned)ip[0], (unsigned)ip[1],
           (unsigned)ip[2], (unsigned)ip[3]);
  const int rssi = (int)WiFi.RSSI();
  char ssid[wifi::kSsidBytes + 1];
  snprintf(ssid, sizeof(ssid), "%s", s_machine.status().ssid);
  s_machine.connected(ipText, rssi, nowMs);

  if (s_rememberPending) {
    const int at = s_saved.add(ssid, s_pendingPass);
    if (at >= 0) {
      saveStore();
      Serial.printf("[wifi] saved \"%s\" (%d of %d)\n", ssid, s_saved.count(), wifi::kMaxSaved);
    } else {
      Serial.printf("[wifi] \"%s\" not saved: the list already holds %d networks\n", ssid,
                    wifi::kMaxSaved);
    }
  }
  clearPendingPass();
  s_autoActive = false;
  bump();
  Serial.printf("[wifi] connected to \"%s\" ip=%s rssi=%d internal=%u largest=%u\n", ssid, ipText,
                rssi, (unsigned)internalFreeBytes(), (unsigned)internalLargestBlock());
  printMemLine("wifi connected");
  // EST-385: the schedule starts with an attempt at 0 s; pollNtp() keeps it going.
  s_ntp.connected(nowMs);
  s_ntpStopLogged = false;
  ntpBeginAttempt(nowMs);
}

void startConnect(const char* ssid, const char* password, bool autoAttempt, uint32_t nowMs) {
  if (!s_driverUp || ssid == nullptr || ssid[0] == 0) return;
  if (strlen(ssid) > (size_t)wifi::kSsidBytes) return;
  const char* pass = password != nullptr ? password : "";
  if (strlen(pass) > (size_t)wifi::kPassBytes) return;

  if (autoAttempt) {
    clearPendingPass();
  } else {
    snprintf(s_pendingPass, sizeof(s_pendingPass), "%s", pass);
    s_rememberPending = true;
  }
  s_autoActive = autoAttempt;
  ntpStop("reconnect");
  s_scanGate.clear();   // EST-355: a pending scan request is not serviced while the station connects

  // begin() is the non-blocking call: it hands the credentials to the supplicant and returns; the
  // loop polls WiFi.status() and the model's timeout. Nothing here waits for an association.
  const wl_status_t st = WiFi.begin(ssid, pass);
  s_machine.connectStarted(ssid, nowMs);
  bump();
  Serial.printf("[wifi] connecting to \"%s\"%s\n", ssid, autoAttempt ? " (saved)" : "");
  if (st == WL_CONNECT_FAILED) {
    s_machine.failed("driver refused", nowMs);
    onAttemptFailed("driver refused");
  }
}

void autoTryNext(uint32_t nowMs) {
  while (s_autoNext < s_saved.count()) {
    const wifi::SavedNetwork* n = s_saved.at(s_autoNext++);
    if (n == nullptr) continue;
    startConnect(n->ssid, n->password, true, nowMs);
    if (s_machine.connecting()) return;
  }
  s_autoActive = false;
  if (s_saved.count() > 0 && s_machine.status().phase != wifi::Phase::Connected) {
    Serial.println("[wifi] auto-connect: no saved network connected");
  }
}

void startDriver(uint32_t nowMs) {
  const uint32_t heap0 = ESP.getFreeHeap();
  const uint32_t psram0 = ESP.getFreePsram();
  const uint32_t internal0 = internalFreeBytes();
  const uint32_t t0 = millis();

  // The IDF keeps its own copy of the credentials by default. Ours is the NVS blob above, so the
  // driver's copy is turned off: one store, and no extra flash writes behind the user's back.
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);   // the model owns the retry policy (none while it says Failed)
  if (!WiFi.mode(WIFI_STA)) {
    // The driver could not allocate its buffers. Turn the switch back off instead of retrying every
    // loop pass; the heap numbers below are the evidence for a later ticket.
    Serial.printf("[wifi] driver init FAILED in %lu ms: heap %u, PSRAM %u\n",
                  (unsigned long)(millis() - t0), (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getFreePsram());
    s_driverUp = false;
    s_enabled = false;
    saveEnabled();
    s_machine.disable();
    bump();
    return;
  }
  WiFi.setSleep(true);            // modem sleep, the default; the LoRa radio is unaffected
  s_driverUp = true;
  // The station is up as far as we are concerned; a scan asked for now waits for kScanReadyMs
  // (wifi_model.h) so the driver does not refuse it while its own bring-up is still running.
  s_scanGate.driverUp(nowMs);

  s_scan.clear();
  s_autoNext = 0;
  s_autoActive = false;
  s_machine.enable(nowMs);
  bump();

  const uint32_t heap1 = ESP.getFreeHeap();
  const uint32_t psram1 = ESP.getFreePsram();
  Serial.printf("[wifi] driver up in %lu ms: heap %u -> %u (%+ld B), internal %u -> %u (%+ld B), "
                "largest %u B, PSRAM %u -> %u (%+ld B)\n",
                (unsigned long)(millis() - t0), (unsigned)heap0, (unsigned)heap1,
                (long)heap1 - (long)heap0, (unsigned)internal0, (unsigned)internalFreeBytes(),
                (long)internalFreeBytes() - (long)internal0, (unsigned)internalLargestBlock(),
                (unsigned)psram0, (unsigned)psram1, (long)psram1 - (long)psram0);
  // Coexistence, measured rather than assumed: the SX1262 is on SPI2 (radio.h) with its own IRQ,
  // the display on SPI3, the card on SPI2/1-bit; Wi-Fi is the SoC's internal radio and shares no bus
  // or pin with any of them. What it does share is CPU time: the Wi-Fi/lwIP tasks run on the other
  // core, and wifiLoop() only polls. The heartbeat's loop stack and the mesh serial show the rest.
  Serial.println("[wifi] link check: SX1262 on SPI2, display SPI3, card SPI2; Wi-Fi uses no SPI bus");
  if (s_saved.count() > 0) autoTryNext(nowMs);
  else Serial.println("[wifi] no saved network: switch on, then Scan");
}

void stopDriver() {
  ntpStop("driver down");
  clearPendingPass();
  s_autoActive = false;
  s_scanGate.driverDown();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  s_driverUp = false;
  s_machine.disable();
  bump();
  Serial.printf("[wifi] driver down: heap %u, PSRAM %u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());
}

void applySwitch(uint32_t nowMs) {
  if (s_enabled && !s_driverUp) startDriver(nowMs);
  else if (!s_enabled && s_driverUp) stopDriver();
}

}  // namespace

void wifiBegin() {
  s_prefs.begin(kPrefsNs, false);
  s_enabled = s_prefs.getBool("on", false);
  loadStore();
  s_machine.reset();
  s_scanGate.reset();
  s_serial = 1;
  // The radio itself is not touched here: the ESP32 boots with Wi-Fi off, and a switch that is off
  // must not allocate the driver at all. The first wifiLoop() applies a saved "on".
  Serial.printf("[wifi] off at boot: heap=%u psram=%u saved=%d switch=%s\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(), s_saved.count(),
                s_enabled ? "on" : "off");
}

void wifiSetEnabled(bool on) {
  if (s_enabled == on) return;
  s_enabled = on;
  saveEnabled();
  if (!on) {
    s_scan.clear();
    ntpStop("switch off");
    s_autoActive = false;
  }
  applySwitch(millis());
  bump();
  Serial.printf("[wifi] switch %s\n", s_enabled ? "on" : "off");
}

bool wifiIsEnabled() { return s_enabled; }

bool wifiStartScan() {
  if (!s_driverUp || s_machine.status().phase == wifi::Phase::Off) return false;
  if (s_machine.scanning()) return true;     // the running scan is the answer
  if (s_machine.connecting()) return false;  // one operation at a time; the connect has its timeout
  s_scan.clear();
  WiFi.scanDelete();
  // EST-355: the request is recorded, not started. wifiLoop() asks the driver as soon as it is ready
  // (kScanReadyMs after the switch went on, and retried if the driver refuses) so the switch-on path
  // can never show "Failed: scan failed" for a scan the driver was not ready for yet.
  s_scanGate.request(millis());
  s_machine.beginScan(millis());
  bump();
  Serial.println("[wifi] scan requested");
  return true;
}

bool wifiIsScanning() { return s_machine.scanning(); }
int wifiScanCount() { return s_scan.count(); }
const wifi::ScanResult* wifiScanAt(int index) { return s_scan.at(index); }

int wifiSavedCount() { return s_saved.count(); }
const wifi::SavedNetwork* wifiSavedAt(int index) { return s_saved.at(index); }
int wifiSavedIndexFor(const char* ssid) { return s_saved.find(ssid); }

void wifiForget(int index) {
  const wifi::SavedNetwork* n = s_saved.at(index);
  char ssid[wifi::kSsidBytes + 1] = "";
  if (n != nullptr) snprintf(ssid, sizeof(ssid), "%s", n->ssid);
  if (!s_saved.forgetAt(index)) return;
  saveStore();
  bump();
  Serial.printf("[wifi] forgot \"%s\" (%d left)\n", ssid, s_saved.count());
}

bool wifiConnect(const char* ssid, const char* password) {
  if (!s_driverUp || ssid == nullptr || ssid[0] == 0) return false;
  if (strlen(ssid) > (size_t)wifi::kSsidBytes) return false;
  if (password != nullptr && strlen(password) > (size_t)wifi::kPassBytes) return false;
  s_autoActive = false;   // a user choice cancels the saved-list pass
  s_autoNext = s_saved.count();
  startConnect(ssid, password, false, millis());
  return s_machine.connecting();
}

bool wifiConnectSaved(int index) {
  const wifi::SavedNetwork* n = s_saved.at(index);
  if (n == nullptr) return false;
  return wifiConnect(n->ssid, n->password);
}

const wifi::Status& wifiStatus() { return s_machine.status(); }
uint32_t wifiSerial() { return s_serial; }
uint32_t wifiServiceWorstUs() { return s_serviceWorstUs; }

// --- NTP (EST-385) -------------------------------------------------------------------------------
const ntp::Plan& wifiNtpPlan() { return s_ntp; }

NtpNowResult wifiNtpNow() {
  if (!s_enabled || !s_driverUp) return NtpNowResult::Off;
  if (s_machine.status().phase != wifi::Phase::Connected) return NtpNowResult::NotConnected;
  // A GPS fix holds the clock: `ntp now` must not overwrite it either.
  if (!ntp::wanted(timeKeeper().realSinceBoot(), timeSourcePriority(timeKeeper().source()))) {
    return NtpNowResult::BetterSource;
  }
  if (s_ntp.active()) return NtpNowResult::Busy;
  const uint32_t nowMs = millis();
  if (!s_ntp.force(nowMs)) return NtpNowResult::NotConnected;
  ntpBeginAttempt(nowMs);
  return NtpNowResult::Started;
}

NtpDns wifiNtpDns() {
  NtpDns d;
  // DNS_MAX_SERVERS is 3 in this build (CONFIG_LWIP_DNS_MAX_SERVERS=3); Wi-Fi off answers 0.
  d.main = (uint32_t)WiFi.dnsIP(0) != 0;
  d.backup = (uint32_t)WiFi.dnsIP(1) != 0;
  d.fallback = (uint32_t)WiFi.dnsIP(2) != 0;
  d.count = (d.main ? 1 : 0) + (d.backup ? 1 : 0) + (d.fallback ? 1 : 0);
  return d;
}

bool wifiNtpClientUp() { return esp_sntp_enabled(); }
bool wifiNtpSleepOn() { return WiFi.getSleep() != WIFI_PS_NONE; }

void wifiLoop() {
  const uint32_t t0 = micros();
  const bool wasUp = s_driverUp;
  const uint32_t nowMs = millis();

  applySwitch(nowMs);

  if (s_driverUp) {
    const wifi::Tick tick = s_machine.tick(nowMs);
    if (tick != wifi::Tick::None) {
      if (tick == wifi::Tick::ScanTimeout) {
        // The driver may still be scanning; stop it so a later Scan starts cleanly.
        esp_wifi_scan_stop();
        WiFi.scanDelete();
        Serial.println("[wifi] scan timed out");
        bump();
      } else {
        Serial.println("[wifi] connect timed out");
        onAttemptFailed("no answer");
      }
    } else {
      switch (s_machine.status().phase) {
        case wifi::Phase::Idle:
          break;

        case wifi::Phase::Scanning: {
          // EST-355: the request and the driver call are separate. `pending` means the driver has not
          // accepted a start yet: wait until it is ready, and retry a refusal instead of failing the
          // page. Only the last attempt becomes Failed("scan failed").
          if (s_scanGate.pending()) {
            if (!s_scanGate.due(nowMs)) break;
            const int16_t started = WiFi.scanNetworks(true, false);
            if (started == WIFI_SCAN_FAILED) {
              if (s_scanGate.retry(nowMs)) {
                Serial.printf("[wifi] scan start refused (try %d of %d), retrying in %lu ms\n",
                              s_scanGate.attempts(), wifi::kScanAttempts,
                              (unsigned long)wifi::kScanRetryMs);
              } else {
                s_machine.failed("scan failed", nowMs);
                bump();
                Serial.println("[wifi] scan failed: the driver refused every try");
              }
            } else {
              s_scanGate.started();
              Serial.println("[wifi] scan started");
            }
            break;
          }
          const int16_t n = WiFi.scanComplete();
          if (n >= 0) {
            for (int16_t i = 0; i < n; ++i) {
              const String ssid = WiFi.SSID(i);
              s_scan.addOrMerge(ssid.c_str(), (int)WiFi.RSSI(i),
                                WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            }
            s_scan.sortByStrength();
            WiFi.scanDelete();
            s_machine.scanDone(WiFi.status() == WL_CONNECTED, n, nowMs);
            bump();
            Serial.printf("[wifi] scan done: %d visible, %d rows\n", (int)n, s_scan.count());
          } else if (n == WIFI_SCAN_FAILED) {
            // A started scan the driver aborted: the same retry rule, so one bad moment does not
            // leave the page on "Failed".
            WiFi.scanDelete();
            if (s_scanGate.retry(nowMs)) {
              Serial.printf("[wifi] scan aborted (try %d of %d), retrying in %lu ms\n",
                            s_scanGate.attempts(), wifi::kScanAttempts,
                            (unsigned long)wifi::kScanRetryMs);
            } else {
              s_machine.failed("scan failed", nowMs);
              bump();
              Serial.println("[wifi] scan failed");
            }
          }
          break;
        }

        case wifi::Phase::Connecting: {
          const wl_status_t st = WiFi.status();
          if (st == WL_CONNECTED) {
            onConnected(nowMs);
          } else if (st == WL_CONNECT_FAILED) {
            s_machine.failed("wrong password", nowMs);
            onAttemptFailed("wrong password");
          } else if (st == WL_NO_SSID_AVAIL) {
            s_machine.failed("not found", nowMs);
            onAttemptFailed("not found");
          } else if (st == WL_CONNECTION_LOST) {
            s_machine.failed("connection lost", nowMs);
            onAttemptFailed("connection lost");
          }
          break;
        }

        case wifi::Phase::Connected: {
          if (WiFi.status() == WL_CONNECTED) {
            pollNtp(nowMs);
          } else {
            ntpStop("link lost");
            s_machine.failed("connection lost", nowMs);
            clearPendingPass();
            bump();
            Serial.println("[wifi] connection lost");
          }
          break;
        }

        default:
          break;
      }
    }
  }

  // Steady-state cost only: the one-time driver init above is logged with its own duration, so this
  // number answers "what does Wi-Fi cost the mesh loop on an ordinary pass".
  if (wasUp && s_driverUp) {
    const uint32_t us = micros() - t0;
    if (us > s_serviceWorstUs) s_serviceWorstUs = us;
  }
}

}  // namespace om
