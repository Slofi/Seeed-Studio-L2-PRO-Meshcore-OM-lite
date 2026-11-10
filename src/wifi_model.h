// SPDX-License-Identifier: MIT
// Wi-Fi state that must be provable without a radio: the saved-network store and the async state
// machine. Plain C++ (no Arduino, no LovyanGFX), so test/host compiles this header with the system
// g++ and the firmware compiles the very same rules.
//
// Why a model at all: the ESP32's Wi-Fi API is asynchronous (a scan runs in the driver and is polled,
// a connect is completed by the supplicant), so the screen needs one honest state that cannot be
// "connected" while the link is down. Every transition goes through this class, and the timeouts are
// part of it, not of a UI callback. The radio side (src/wifi_link.cpp) only reports facts: a scan
// finished, a station got an IP, the supplicant refused.
//
// The saved list is the NVS-backed list: at most five networks, one entry per SSID, adding an
// existing SSID replaces its password in place. The firmware persists the same five entries as one
// blob; this class owns the ordering and the limit.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace om {
namespace wifi {

constexpr int kMaxSaved = 5;
constexpr int kMaxScan = 16;
constexpr int kSsidBytes = 32;    // the 802.11 SSID limit, without the terminator
constexpr int kPassBytes = 63;    // WPA2/WPA3 passphrase limit
constexpr int kIpTextCap = 16;    // "255.255.255.255" plus the terminator
constexpr int kReasonCap = 32;
constexpr int kNoRssi = -127;

inline void copyText(char* out, size_t cap, const char* in) {
  if (out == nullptr || cap == 0) return;
  if (in == nullptr) {
    out[0] = 0;
    return;
  }
  snprintf(out, cap, "%s", in);
}

// ------------------------------------------------------------------------------- saved networks
struct SavedNetwork {
  char ssid[kSsidBytes + 1] = "";
  char password[kPassBytes + 1] = "";
};

class SavedStore {
 public:
  int count() const { return count_; }
  bool full() const { return count_ >= kMaxSaved; }

  const SavedNetwork* at(int index) const {
    if (index < 0 || index >= count_) return nullptr;
    return &items_[index];
  }

  // Index of an SSID, or -1. Compared byte for byte: an SSID is not a locale string.
  int find(const char* ssid) const {
    if (ssid == nullptr || ssid[0] == 0) return -1;
    for (int i = 0; i < count_; ++i) {
      if (strcmp(items_[i].ssid, ssid) == 0) return i;
    }
    return -1;
  }

  // Adds a network, or replaces the password of one already saved under the same SSID. Returns its
  // index, or -1 for an empty/bad SSID, an over-long password, or a sixth distinct network. A
  // replacement never moves the entry and never changes the count.
  int add(const char* ssid, const char* password) {
    if (ssid == nullptr || ssid[0] == 0) return -1;
    const size_t ssidLen = strlen(ssid);
    if (ssidLen > (size_t)kSsidBytes) return -1;
    const char* pass = password != nullptr ? password : "";
    if (strlen(pass) > (size_t)kPassBytes) return -1;

    const int existing = find(ssid);
    if (existing >= 0) {
      copyText(items_[existing].password, sizeof(items_[existing].password), pass);
      return existing;
    }
    if (count_ >= kMaxSaved) return -1;
    copyText(items_[count_].ssid, sizeof(items_[count_].ssid), ssid);
    copyText(items_[count_].password, sizeof(items_[count_].password), pass);
    ++count_;
    return count_ - 1;
  }

  bool forget(const char* ssid) { return forgetAt(find(ssid)); }

  // Removes one entry, keeping the list compact (0 first). False for an index outside the list.
  bool forgetAt(int index) {
    if (index < 0 || index >= count_ || index >= kMaxSaved) return false;
    for (int i = index; i + 1 < count_; ++i) items_[i] = items_[i + 1];
    --count_;
    items_[count_] = SavedNetwork();
    return true;
  }

  void clear() {
    for (int i = 0; i < kMaxSaved; ++i) items_[i] = SavedNetwork();
    count_ = 0;
  }

 private:
  SavedNetwork items_[kMaxSaved];
  int count_ = 0;
};

// ----------------------------------------------------------------------------------- scan list
struct ScanResult {
  char ssid[kSsidBytes + 1] = "";
  int16_t rssi = kNoRssi;   // dBm, negative
  bool secured = false;
};

class ScanList {
 public:
  int count() const { return count_; }
  const ScanResult* at(int index) const {
    if (index < 0 || index >= count_) return nullptr;
    return &items_[index];
  }

  void clear() { count_ = 0; }

  // One row per SSID: a second sighting of the same SSID keeps the stronger signal (and its security
  // flag). An empty SSID is a hidden network with no name to show, so it is skipped.
  int addOrMerge(const char* ssid, int rssi, bool secured) {
    if (ssid == nullptr || ssid[0] == 0 || strlen(ssid) > (size_t)kSsidBytes) return -1;
    for (int i = 0; i < count_; ++i) {
      if (strcmp(items_[i].ssid, ssid) != 0) continue;
      if (rssi > (int)items_[i].rssi) {
        items_[i].rssi = (int16_t)rssi;
        items_[i].secured = secured;
      }
      return i;
    }
    if (count_ >= kMaxScan) return -1;
    copyText(items_[count_].ssid, sizeof(items_[count_].ssid), ssid);
    items_[count_].rssi = (int16_t)rssi;
    items_[count_].secured = secured;
    ++count_;
    return count_ - 1;
  }

  // Strongest first. Insertion sort: 16 rows at most, and it keeps the code tiny.
  void sortByStrength() {
    for (int i = 1; i < count_; ++i) {
      const ScanResult key = items_[i];
      int j = i - 1;
      while (j >= 0 && items_[j].rssi < key.rssi) {
        items_[j + 1] = items_[j];
        --j;
      }
      items_[j + 1] = key;
    }
  }

 private:
  ScanResult items_[kMaxScan];
  int count_ = 0;
};

// --------------------------------------------------------------------------------- state machine
// Off is the only state at boot (Wi-Fi is off by default). Idle is "on, not connected", which is
// distinct from Off so the page and the status bar can say the radio is powered but the link is not.
enum class Phase : uint8_t { Off = 0, Idle, Scanning, Connecting, Connected, Failed };

constexpr uint32_t kScanTimeoutMs = 15000;
constexpr uint32_t kConnectTimeoutMs = 20000;

struct Status {
  Phase phase = Phase::Off;
  char ssid[kSsidBytes + 1] = "";   // the network being connected to / the one connected
  char ip[kIpTextCap] = "";
  int rssi = kNoRssi;
  char reason[kReasonCap] = "";
  int found = 0;                    // networks seen by the last completed scan
};

constexpr const char* phaseToken(Phase p) {
  switch (p) {
    case Phase::Off: return "off";
    case Phase::Idle: return "idle";
    case Phase::Scanning: return "scanning";
    case Phase::Connecting: return "connecting";
    case Phase::Connected: return "connected";
    case Phase::Failed: return "failed";
    default: return "off";
  }
}

// The one line the Settings page shows. No em dash (REBUILD.md).
inline void stateLine(const Status& s, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  switch (s.phase) {
    case Phase::Off: snprintf(out, cap, "Off"); break;
    case Phase::Idle: snprintf(out, cap, "On, not connected"); break;
    case Phase::Scanning: snprintf(out, cap, "Scanning"); break;
    case Phase::Connecting: snprintf(out, cap, "Connecting"); break;
    case Phase::Connected: snprintf(out, cap, "Connected"); break;
    case Phase::Failed:
      if (s.reason[0]) snprintf(out, cap, "Failed: %s", s.reason);
      else snprintf(out, cap, "Failed");
      break;
    default: snprintf(out, cap, "Off"); break;
  }
}

enum class Tick : uint8_t { None = 0, ScanTimeout, ConnectTimeout };

class Machine {
 public:
  void reset() {
    st_ = Status();
    since_ = 0;
  }

  bool on() const { return st_.phase != Phase::Off; }
  bool connected() const { return st_.phase == Phase::Connected; }
  bool scanning() const { return st_.phase == Phase::Scanning; }
  bool connecting() const { return st_.phase == Phase::Connecting; }

  // Power on: Off -> Idle. Any other state is already on, so this does not restart a connection.
  void enable(uint32_t nowMs) {
    if (st_.phase != Phase::Off) return;
    st_ = Status();
    st_.phase = Phase::Idle;
    since_ = nowMs;
  }

  // Power off from any state. All connection facts are dropped, so the page cannot show a stale IP.
  void disable() {
    st_ = Status();
    since_ = 0;
  }

  // A scan may be started from Idle or from Connected; from Connected the link details are kept so
  // scanDone() can return to Connected when the station is still associated.
  void beginScan(uint32_t nowMs) {
    if (st_.phase == Phase::Off) return;
    st_.phase = Phase::Scanning;
    st_.reason[0] = 0;
    since_ = nowMs;
  }

  // The driver's scan finished: st.found is the number of visible networks. `stillConnected` is
  // WiFi.status() == WL_CONNECTED at that moment. A scan that dropped the link goes back to Idle and
  // drops the stale SSID/IP; it does not invent a "connected" state.
  void scanDone(bool stillConnected, int found, uint32_t nowMs) {
    if (st_.phase != Phase::Scanning) return;
    st_.found = found > 0 ? found : 0;
    if (stillConnected && st_.ssid[0] != 0) {
      st_.phase = Phase::Connected;
    } else {
      st_.phase = Phase::Idle;
      st_.ssid[0] = 0;
      st_.ip[0] = 0;
      st_.rssi = kNoRssi;
    }
    since_ = nowMs;
  }

  void connectStarted(const char* ssid, uint32_t nowMs) {
    if (st_.phase == Phase::Off) return;
    st_.phase = Phase::Connecting;
    copyText(st_.ssid, sizeof(st_.ssid), ssid);
    st_.ip[0] = 0;
    st_.rssi = kNoRssi;
    st_.reason[0] = 0;
    since_ = nowMs;
  }

  void connected(const char* ip, int rssi, uint32_t nowMs) {
    if (st_.phase == Phase::Off) return;
    st_.phase = Phase::Connected;
    copyText(st_.ip, sizeof(st_.ip), ip);
    st_.rssi = rssi;
    st_.reason[0] = 0;
    since_ = nowMs;
  }

  void failed(const char* reason, uint32_t nowMs) {
    if (st_.phase == Phase::Off) return;
    st_.phase = Phase::Failed;
    st_.ip[0] = 0;
    st_.rssi = kNoRssi;
    copyText(st_.reason, sizeof(st_.reason), reason != nullptr ? reason : "failed");
    since_ = nowMs;
  }

  // Called every loop pass. A scan or a connect that never answers becomes Failed here, so the UI
  // leaves "Scanning"/"Connecting" even when the driver never reports back. The subtraction is
  // wraparound-safe for millis().
  Tick tick(uint32_t nowMs) {
    const uint32_t elapsed = nowMs - since_;
    if (st_.phase == Phase::Scanning && elapsed >= kScanTimeoutMs) {
      failed("scan timed out", nowMs);
      return Tick::ScanTimeout;
    }
    if (st_.phase == Phase::Connecting && elapsed >= kConnectTimeoutMs) {
      failed("no answer", nowMs);
      return Tick::ConnectTimeout;
    }
    return Tick::None;
  }

  const Status& status() const { return st_; }
  uint32_t sinceMs() const { return since_; }

 private:
  Status st_;
  uint32_t since_ = 0;
};

// ------------------------------------------------------------------------------ the scan's start
// EST-355. On the device the FIRST scan right after the switch went on failed: the driver refuses
// `esp_wifi_scan_start` while its station is still coming up, and the Arduino wrapper turns that into
// WIFI_SCAN_FAILED, which the page showed as `Failed: scan failed`. The second scan a few seconds
// later found 12 networks. So the request and the driver call are separated here:
//  - a scan asked for less than kScanReadyMs after the driver came up waits until it is ready,
//  - a start the driver refuses, and a scan that ends in WIFI_SCAN_FAILED, are retried after
//    kScanRetryMs, up to kScanAttempts tries in total,
//  - only when those are used up does the caller turn it into Failed.
// The switch-on path itself never reports a failure from this: it only arms the wait. Plain C++, so
// the ORDER (request -> not yet -> start -> refused -> start -> done) is host-tested.
constexpr uint32_t kScanReadyMs = 250;     // the station's own bring-up, after WiFi.mode()
constexpr uint32_t kScanRetryMs = 250;     // between a refusal and the next try
constexpr int kScanAttempts = 3;           // the first try plus two retries

class ScanGate {
 public:
  // No driver, no request.
  void reset() {
    up_ = false;
    requested_ = false;
    attempts_ = 0;
    upAtMs_ = 0;
    nextTryMs_ = 0;
  }

  // The driver is up (or came back): a request that was waiting may go as soon as it is ready.
  void driverUp(uint32_t nowMs) {
    up_ = true;
    upAtMs_ = nowMs;
    if (requested_) nextTryMs_ = nowMs + kScanReadyMs;
  }

  void driverDown() {
    up_ = false;
    requested_ = false;
    attempts_ = 0;
  }

  // The user (or the page, or the USB command) asked for a scan. Starting the count over is right:
  // this is a new request, not a retry.
  void request(uint32_t nowMs) {
    requested_ = true;
    attempts_ = 0;
    const uint32_t readyAt = upAtMs_ + kScanReadyMs;
    nextTryMs_ = up_ && (int32_t)(readyAt - nowMs) > 0 ? readyAt : nowMs;
  }

  // Forget a pending request (a connect took over, the switch went off).
  void clear() {
    requested_ = false;
    attempts_ = 0;
  }

  bool pending() const { return requested_; }
  int attempts() const { return attempts_; }

  // Call once per loop pass while the machine is Scanning. True when the driver should be asked now.
  // Never true while the station is still coming up.
  bool due(uint32_t nowMs) const {
    if (!requested_ || !up_) return false;
    return (int32_t)(nowMs - nextTryMs_) >= 0;
  }

  // The driver accepted the start: the request is satisfied.
  void started() {
    requested_ = false;
    attempts_ = 0;
  }

  // The driver refused the start, or a started scan ended in WIFI_SCAN_FAILED. Returns true while
  // another try is scheduled, false when the attempts are used up (the caller says Failed).
  bool retry(uint32_t nowMs) {
    ++attempts_;
    if (attempts_ >= kScanAttempts) {
      requested_ = false;
      return false;
    }
    requested_ = true;
    nextTryMs_ = nowMs + kScanRetryMs;
    return true;
  }

 private:
  bool up_ = false;
  bool requested_ = false;
  int attempts_ = 0;
  uint32_t upAtMs_ = 0;      // when the driver came up (the readiness wait starts here)
  uint32_t nextTryMs_ = 0;   // the earliest moment the driver may be asked
};

}  // namespace wifi
}  // namespace om
