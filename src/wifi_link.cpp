// SPDX-License-Identifier: MIT
// The Wi-Fi station's radio side. See wifi_link.h for the rules and wifi_model.h for the state.
#include "wifi_link.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <string.h>
#include <sys/time.h>

#include "timekeeper.h"

// The SNTP reply is captured here instead of being written straight into the system clock. The
// ESP-IDF declaration is weak (`nm liblwip.a` shows W sntp_sync_time), so this definition replaces
// it; the loop task then feeds the epoch through timeKeeper().setFromSource(), which keeps GPS the
// higher source and refuses a reply below the saved floor. tv is written by the SNTP task, so the
// two words it touches are volatile and read once by the loop.
static volatile uint32_t s_ntpEpoch = 0;
static volatile bool s_ntpGot = false;

extern "C" void sntp_sync_time(struct timeval* tv) {
  if (tv == nullptr) return;
  if (tv->tv_sec < (time_t)om::kMinSaneEpoch || tv->tv_sec >= (time_t)om::kMaxSaneEpoch) return;
  s_ntpEpoch = (uint32_t)tv->tv_sec;
  s_ntpGot = true;
}

// The notification callback is the second, independent capture: lwIP calls it after sntp_sync_time()
// whether or not the weak replacement above took, so a toolchain that keeps the default clock write
// still feeds the timekeeper instead of silently leaving the status bar at "--:--".
static void sntpNotify(struct timeval* tv) {
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
constexpr uint32_t kNtpTimeoutMs = 20000;

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

// SNTP is started once per successful connection. s_ntpDone keeps it from being queried again while
// the same association lasts; a new connection resets it.
bool s_ntpActive = false;
bool s_ntpDone = false;
uint32_t s_ntpStartMs = 0;

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

void stopNtp() {
  if (s_ntpActive) {
    esp_sntp_stop();
    s_ntpActive = false;
  }
}

void startNtp(uint32_t nowMs) {
  stopNtp();
  s_ntpGot = false;
  s_ntpEpoch = 0;
  s_ntpDone = false;
  s_ntpActive = true;
  s_ntpStartMs = nowMs;
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_set_time_sync_notification_cb(&sntpNotify);
  esp_sntp_init();
  // No configTime(): it would overwrite TZ, and the timekeeper owns Ljubljana. No other client is
  // started in this ticket.
  Serial.println("[wifi] NTP: pool.ntp.org (one query)");
}

void pollNtp(uint32_t nowMs) {
  if (!s_ntpActive || s_ntpDone) return;
  if (s_ntpGot) {
    const uint32_t epoch = s_ntpEpoch;
    s_ntpGot = false;
    const bool applied = timeKeeper().setFromSource(epoch, TimeSource::Wifi, millis() / 1000);
    Serial.printf("[wifi] NTP %lu s: %s, source %s\n", (unsigned long)epoch,
                  applied ? "applied" : "kept (a higher source holds the clock)",
                  timeSourceName(timeKeeper().source()));
    stopNtp();
    s_ntpDone = true;
    bump();
    return;
  }
  if (nowMs - s_ntpStartMs >= kNtpTimeoutMs) {
    Serial.println("[wifi] NTP: no answer, clock unchanged");
    stopNtp();
    s_ntpDone = true;
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
  Serial.printf("[wifi] connected to \"%s\" ip=%s rssi=%d\n", ssid, ipText, rssi);
  startNtp(nowMs);
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
  stopNtp();
  s_ntpDone = false;

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

  s_scan.clear();
  s_autoNext = 0;
  s_autoActive = false;
  s_machine.enable(nowMs);
  bump();

  const uint32_t heap1 = ESP.getFreeHeap();
  const uint32_t psram1 = ESP.getFreePsram();
  Serial.printf("[wifi] driver up in %lu ms: heap %u -> %u (%+ld B), PSRAM %u -> %u (%+ld B)\n",
                (unsigned long)(millis() - t0), (unsigned)heap0, (unsigned)heap1,
                (long)heap1 - (long)heap0, (unsigned)psram0, (unsigned)psram1,
                (long)psram1 - (long)psram0);
  // Coexistence, measured rather than assumed: the SX1262 is on SPI2 (radio.h) with its own IRQ,
  // the display on SPI3, the card on SPI2/1-bit; Wi-Fi is the SoC's internal radio and shares no bus
  // or pin with any of them. What it does share is CPU time: the Wi-Fi/lwIP tasks run on the other
  // core, and wifiLoop() only polls. The heartbeat's loop stack and the mesh serial show the rest.
  Serial.println("[wifi] link check: SX1262 on SPI2, display SPI3, card SPI2; Wi-Fi uses no SPI bus");
  if (s_saved.count() > 0) autoTryNext(nowMs);
  else Serial.println("[wifi] no saved network: switch on, then Scan");
}

void stopDriver() {
  stopNtp();
  clearPendingPass();
  s_autoActive = false;
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
    stopNtp();
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
  const int16_t started = WiFi.scanNetworks(true, false);
  if (started == WIFI_SCAN_FAILED) {
    s_machine.failed("scan failed", millis());
    bump();
    Serial.println("[wifi] scan could not start");
    return false;
  }
  s_machine.beginScan(millis());
  bump();
  Serial.println("[wifi] scan started");
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
            WiFi.scanDelete();
            s_machine.failed("scan failed", nowMs);
            bump();
            Serial.println("[wifi] scan failed");
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
            stopNtp();
            s_ntpDone = false;
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
