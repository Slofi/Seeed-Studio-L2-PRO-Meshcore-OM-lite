// SPDX-License-Identifier: MIT
// Wi-Fi station for OM Lite: the radio side of Settings > Wi-Fi (EST-354).
//
// Everything here is asynchronous. A scan is started in the driver and polled, a connect is started
// and polled, and both have timeouts in wifi_model.h; the Arduino loop never waits for either. The
// only synchronous call is the one-time driver init inside `WiFi.mode(WIFI_STA)` when the switch is
// turned on (its duration is measured and logged). Nothing else goes on the network: the only client
// is SNTP, and EST-385 gives it a retry schedule instead of one query per connection: attempts at
// 0 s / 30 s / 2 min / then every 10 min, rotating pool.ntp.org, time.google.com, a fixed public IP
// and the default gateway (ntp_plan.h). Its answer goes through
// timeKeeper().setFromSource(..., TimeSource::Wifi) so the source ranking stays GPS first.
//
// Passwords: the five saved networks are one NVS blob (namespace "omwifi") and the password typed on
// the device lives in a static buffer only for the attempt. No function here prints a password, and
// the USB command surface has no word that carries one.
#pragma once

#include <stdint.h>

#include "ntp_plan.h"
#include "wifi_model.h"

namespace om {

void wifiBegin();          // load NVS (switch off by default), radio stays off
void wifiLoop();           // service the switch, scan, connect and NTP; never blocks on a link
void wifiSetEnabled(bool on);
bool wifiIsEnabled();

// Asks for an asynchronous scan. True when the request is accepted (or a scan is already running); a
// completed scan's list is in wifiScanCount()/wifiScanAt(), strongest first. EST-355: the request is
// recorded and the driver is asked from wifiLoop() once it is ready (a moment after the switch goes
// on) with retries, so a scan asked for right after switching Wi-Fi on is not failed by the driver's
// own bring-up.
bool wifiStartScan();
bool wifiIsScanning();
int wifiScanCount();
const wifi::ScanResult* wifiScanAt(int index);

int wifiSavedCount();
const wifi::SavedNetwork* wifiSavedAt(int index);
int wifiSavedIndexFor(const char* ssid);
void wifiForget(int index);

// Starts an asynchronous connect. Returns false only when Wi-Fi is off or the SSID is unusable.
// On success the network is added to (or replaces its entry in) the saved list; a full list keeps
// the connection but does not save a sixth network.
bool wifiConnect(const char* ssid, const char* password);
bool wifiConnectSaved(int index);

const wifi::Status& wifiStatus();

// --- NTP (EST-385) -------------------------------------------------------------------------------
// The retry schedule's own state: attempts, the last result/server, the next attempt. The USB
// `ntp status` answer reads this; nothing here carries a LAN address or an SSID.
const ntp::Plan& wifiNtpPlan();

// What `ntp now` did: Started, or why not. It goes through the same attempt path the schedule uses.
enum class NtpNowResult : uint8_t { Started = 0, Off, NotConnected, BetterSource, Busy };
NtpNowResult wifiNtpNow();

// The station's DNS servers as COUNT and set/unset flags only (their addresses are never exposed).
struct NtpDns {
  int count = 0;
  bool main = false;
  bool backup = false;
  bool fallback = false;
};
NtpDns wifiNtpDns();

// Measurement facts for `ntp status`: the lwIP client's own enabled flag and whether modem sleep is
// on (false while an attempt is in flight, because it is turned off for the exchange).
bool wifiNtpClientUp();
bool wifiNtpSleepOn();

// Changes whenever the page or the status bar would look different (phase, scan count, saved list,
// switch). The shell redraws on a change instead of polling every field.
uint32_t wifiSerial();

// The longest steady-state wifiLoop() pass since boot, in microseconds. The one-time driver init is
// excluded (it is logged with its own duration), so this number answers "does Wi-Fi block the loop".
uint32_t wifiServiceWorstUs();

}  // namespace om
