// SPDX-License-Identifier: MIT
// EST-356: the Bluetooth switch, PIN and state. The transport itself is ble_transport.cpp (upstream's
// ESP32 companion BLE class); the protocol and the stores are MeshCore's companion code (MyMesh).
#include "ble_link.h"

#if defined(OM_ENABLE_MESH)

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

#include "ble_pin.h"
#include "ble_transport.h"
#include "mem_stats.h"
#include "mesh_link.h"
#include "mesh_text.h"
#include "position.h"
#include "wifi_link.h"

namespace om {
namespace {

constexpr const char* kPrefsNs = "omble";
constexpr const char* kPrefsKey = "on";
// EST-357: the crash guard's two keys. `crashed` is true exactly while the transport is being
// started; `gbuild` resets it on the first boot of a new image, like the mesh guard in mesh_link.cpp.
constexpr const char* kCrashedKey = "crashed";
constexpr const char* kBuildKey = "gbuild";

// EST-357: Bluedroid's need on this build. The EST-356 core dump died inside hash_map_set with
// 53,948 B of internal heap free, so that number is a hard lower bound, not a target. The exact peak
// is the delta between the two internal numbers on the `[ble] driver up ... internal A -> B` line;
// these constants are deliberately conservative until the first live one replaces them. A start that
// cannot leave that margin is refused (never attempted), and the crash guard covers a peak that is
// still higher than the estimate.
constexpr uint32_t kStartNeedFree = 80u * 1024u;    // internal free bytes before start
constexpr uint32_t kStartNeedBlock = 8u * 1024u;    // largest single internal block before start

// MeshCore's companion command ids, as in lib/meshcore_companion/MyMesh.cpp. Only the two the phone
// uses to SEND are needed here: the listener already sees everything that arrives.
constexpr uint8_t kCmdSendTxtMsg = 2;          // [2][type][attempt][ts:4][pubkey:6][text]
constexpr uint8_t kCmdSendChannelTxtMsg = 3;   // [3][type][channel][ts:4][text]
// EST-384: the companion's set-location command (CMD_SET_ADVERT_LATLON = 14 in
// lib/meshcore_companion/MyMesh.cpp). Upstream validates the two int32 micro-degrees itself; we take
// the same point into the Fixed source through the transport hook, before MyMesh writes sensors.
constexpr uint8_t kCmdSetAdvertLatLon = 14;    // [14][lat:int32][lon:int32][alt:int32 optional]
constexpr uint8_t kTxtTypePlain = 0;

// EST-357: the shell's toast hook and the one text queued during setup (a toast cannot be drawn
// before setup finished, so bleBegin() only stores it and the first bleLoop() shows it).
void (*s_notice)(const char*) = nullptr;
char s_pendingNotice[64] = "";

void queueNotice(const char* text) {
  if (text == nullptr) return;
  snprintf(s_pendingNotice, sizeof(s_pendingNotice), "%s", text);
}

void notifyNow(const char* text) {
  Serial.printf("[ble] notice: %s\n", text);
  if (s_notice != nullptr) s_notice(text);
}

void flushNotice() {
  if (s_pendingNotice[0] == 0) return;
  notifyNow(s_pendingNotice);
  s_pendingNotice[0] = 0;
}

Preferences s_prefs;
bool s_begun = false;
bool s_enabled = false;          // the user's switch, persisted
ble::Model s_model;
uint32_t s_serial = 1;
uint32_t s_appliedPin = 0;       // the passkey the transport has
uint32_t s_lastWifiSerial = 0;
int s_lastWifiPhase = -1;

void bump() {
  ++s_serial;
  if (s_serial == 0) s_serial = 1;
}

void saveEnabled() { s_prefs.putBool(kPrefsKey, s_enabled); }

void logHeap(const char* what) {
  Serial.printf("[ble] %s: heap=%u psram=%u internal=%u largest=%u wifi=%s\n", what,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
                (unsigned)internalFreeBytes(), (unsigned)internalLargestBlock(),
                wifiIsEnabled() ? wifi::phaseToken(wifiStatus().phase) : "off");
}

// EST-357: the start guard. Bluedroid's allocations are small and stay in internal RAM
// (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096), so the only honest test is the internal heap, measured
// right now, against the need measured on this build. A refusal turns the switch off (so a saved
// "on" cannot retry every pass) and says the exact numbers on the log and on the screen.
bool startGuardAllows() {
  const uint32_t freeNow = internalFreeBytes();
  const uint32_t blockNow = internalLargestBlock();
  if (freeNow >= kStartNeedFree && blockNow >= kStartNeedBlock) return true;
  Serial.printf("[ble] not started: %u B free (largest %u B), need %u B free and %u B largest\n",
                (unsigned)freeNow, (unsigned)blockNow, (unsigned)kStartNeedFree,
                (unsigned)kStartNeedBlock);
  char text[64];
  snprintf(text, sizeof(text), "Bluetooth: %u B free, need %u B", (unsigned)freeNow,
           (unsigned)kStartNeedFree);
  notifyNow(text);
  s_enabled = false;
  saveEnabled();
  s_model.disable();
  bump();
  return false;
}

void startTransport() {
  if (!startGuardAllows()) return;

  // The PIN is made once, at the first switch-on, and written through CMD_SET_DEVICE_PIN's path.
  uint32_t pin = meshBlePin();
  if (!ble::pinValid(pin)) {
    pin = ble::pinFromRandom(esp_random());
    if (!meshBlePinSet(pin)) {
      Serial.println("[ble] cannot save the PIN, staying off");
      s_enabled = false;
      saveEnabled();
      return;
    }
    Serial.println("[ble] first switch-on: a 6-digit PIN was made and saved");
  }

  const uint32_t heap0 = ESP.getFreeHeap();
  const uint32_t psram0 = ESP.getFreePsram();
  const uint32_t internal0 = internalFreeBytes();
  const uint32_t t0 = millis();

  // EST-357: the crash window. The flag is in NVS before the first Bluedroid allocation, so a panic
  // inside the driver is seen on the next boot and the switch is forced off there (bleBegin). NVS
  // commits each putBool, and the mesh guard uses the same one-shot flag.
  s_prefs.putBool(kCrashedKey, true);

  if (!bleTransport().begin(meshNodeName(), pin)) {
    s_prefs.putBool(kCrashedKey, false);   // a clean refusal is not a crash
    Serial.printf("[ble] driver init FAILED in %lu ms: heap %u, psram %u\n",
                  (unsigned long)(millis() - t0), (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getFreePsram());
    s_enabled = false;
    saveEnabled();
    s_model.disable();
    bump();
    return;
  }
  bleTransport().enable();
  s_prefs.putBool(kCrashedKey, false);
  s_appliedPin = pin;
  s_model.enable();
  bump();

  Serial.printf("[ble] driver up in %lu ms: heap %u -> %u (%+ld B), internal %u -> %u (%+ld B), "
                "largest %u B, psram %u -> %u (%+ld B), name \"%s\"\n",
                (unsigned long)(millis() - t0), (unsigned)heap0, (unsigned)ESP.getFreeHeap(),
                (long)ESP.getFreeHeap() - (long)heap0, (unsigned)internal0,
                (unsigned)internalFreeBytes(), (long)internalFreeBytes() - (long)internal0,
                (unsigned)internalLargestBlock(), (unsigned)psram0, (unsigned)ESP.getFreePsram(),
                (long)ESP.getFreePsram() - (long)psram0, meshNodeName());
  // The coexistence number the ticket asks for: both radios up, one heap.
  logHeap("coexist (BLE on)");
}

void stopTransport() {
  if (s_model.phase() == ble::Phase::Off) return;
  bleTransport().disable();
  s_model.disable();
  bump();
  // The controller stays initialized: the radio is off (no advertising, no connection) and the
  // memory comes back only at reboot. This keeps a switch-off/switch-on cycle from re-initialising
  // bluedroid, which is the one sequence that cannot be tested without the device.
  logHeap("off (stack kept until reboot)");
}

}  // namespace

bool bleBegin() {
  if (s_begun) return true;
  s_prefs.begin(kPrefsNs, false);
  // EST-357: a new image resets the guard, exactly like the mesh guard: `crashed` describes the image
  // that wrote it, and a parked flag would otherwise outlive the fix. A reboot of the same image keeps
  // it, which is what stops the boot loop.
  {
    const char* build = __DATE__ " " __TIME__;
    if (s_prefs.getString(kBuildKey, "") != build) {
      s_prefs.putBool(kCrashedKey, false);
      s_prefs.putString(kBuildKey, build);
      Serial.printf("[ble] new firmware image (%s): crash guard reset\n", build);
    }
  }
  if (s_prefs.getBool(kCrashedKey, false)) {
    // The previous boot died after the flag was set and before the driver was up: the NVS "on" from
    // that run must not start BLE again (EST-356 boot-looped exactly this way). Switch it off and say
    // so; a later manual switch-on is still allowed and gets the guarded path again.
    s_enabled = false;
    saveEnabled();
    s_prefs.putBool(kCrashedKey, false);
    queueNotice("Bluetooth off: the last start crashed");
    Serial.println("[ble] previous boot died while starting Bluetooth: switch forced off (guard)");
  } else {
    s_enabled = s_prefs.getBool(kPrefsKey, ble::kDefaultEnabled);
  }
  s_model.reset();
  s_serial = 1;
  s_begun = true;
  // NodePrefs is loaded by now (meshLinkBegin). Zero means "not made yet"; a saved PIN is the one
  // the next pairing will use.
  meshBlePinActivate();
  s_appliedPin = meshBlePin();
  s_lastWifiSerial = wifiSerial();
  s_lastWifiPhase = wifiIsEnabled() ? (int)wifiStatus().phase : -1;
  Serial.printf("[ble] off at boot: switch=%s pin_set=%d heap=%u psram=%u internal=%u largest=%u "
                "wifi=%s\n",
                s_enabled ? "on" : "off", ble::pinValid(s_appliedPin) ? 1 : 0,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
                (unsigned)internalFreeBytes(), (unsigned)internalLargestBlock(),
                wifiIsEnabled() ? wifi::phaseToken(wifiStatus().phase) : "off");
  return true;
}

void bleLoop() {
  if (!s_begun) return;

  // EST-357: a notice queued by bleBegin (the parked crash guard) is shown on the first pass, after
  // setup has finished and drawHome() can redraw safely.
  flushNotice();

  // Apply the switch. A saved "on" from NVS is applied here on the first pass, not in begin(), so
  // the boot log stays a clean "off at boot" and a driver failure is still visible.
  if (s_enabled && s_model.phase() == ble::Phase::Off) {
    // EST-357: judge the start with Wi-Fi where it will really be, not with the pre-Wi-Fi heap. The
    // first pass can run while the station is still scanning or connecting; starting BLE then and
    // letting Wi-Fi take its ~37.6 KB afterwards is the EST-356 way to run out of internal RAM. A
    // manual `ble on` skips this and the guard measures whatever the heap is at that moment.
    const bool wifiBusy =
        wifiIsEnabled() && (wifiStatus().phase == wifi::Phase::Scanning ||
                            wifiStatus().phase == wifi::Phase::Connecting);
    if (!wifiBusy) startTransport();
  } else if (!s_enabled && s_model.phase() != ble::Phase::Off) {
    stopTransport();
  }

  if (s_model.enabled()) {
    // The phone's CMD_SET_DEVICE_PIN writes NodePrefs and saves it; the transport's static passkey
    // is this firmware's to update, so the page and the next pairing cannot disagree.
    const uint32_t stored = meshBlePin();
    if (stored != s_appliedPin) {
      if (ble::pinValid(stored)) {
        bleTransport().applyPin(stored);
        s_appliedPin = stored;
        bump();
        Serial.println("[ble] PIN changed (phone or screen), applied to the next pairing");
      } else if (stored == 0) {
        s_appliedPin = 0;
        Serial.println("[ble] PIN cleared; a new one is made the next time Bluetooth is switched on");
      }
    }

    const bool conn = bleTransport().isConnected();
    const bool wasConnected = s_model.phase() == ble::Phase::Connected;
    if (conn && !wasConnected) {
      s_model.noteConnected();
      bump();
      logHeap("connected");
    } else if (!conn && wasConnected) {
      s_model.noteDisconnected();
      bump();
      logHeap("disconnected, advertising again");
    }
  }

  // Wi-Fi and BLE share the SoC's radio and DRAM: whenever Wi-Fi's PHASE changes while BLE is up,
  // print the one number a reviewer needs. A scan changes several serials but no phase.
  const uint32_t ws = wifiSerial();
  if (ws != s_lastWifiSerial) {
    s_lastWifiSerial = ws;
    const int phase = wifiIsEnabled() ? (int)wifiStatus().phase : -1;
    if (phase != s_lastWifiPhase) {
      s_lastWifiPhase = phase;
      if (s_model.enabled()) logHeap(wifiIsEnabled() ? "wifi changed" : "wifi off");
    }
  }
}

void bleSetEnabled(bool on) {
  if (!s_begun) return;
  if (s_enabled == on) return;
  s_enabled = on;
  saveEnabled();
  if (s_enabled) startTransport();
  else stopTransport();
  bump();
  Serial.printf("[ble] switch %s\n", s_enabled ? "on" : "off");
}

bool bleIsEnabled() { return s_enabled; }
ble::Phase blePhase() { return s_model.phase(); }
bool bleConnected() { return s_model.connected(); }
uint32_t bleSerial() { return s_serial; }

uint32_t blePin() { return meshBlePin(); }

bool blePinSet(uint32_t pin) {
  if (!ble::pinValid(pin)) return false;
  if (!meshBlePinSet(pin)) return false;
  s_appliedPin = pin;
  if (s_model.enabled()) bleTransport().applyPin(pin);
  bump();
  Serial.println("[ble] PIN saved (6 digits, shown on the Bluetooth page only)");
  return true;
}

uint32_t bleNewPin() {
  const uint32_t pin = ble::pinFromRandom(esp_random());
  if (!blePinSet(pin)) return 0;
  return pin;
}

void blePinText(char* out, size_t cap) { ble::pinText(out, cap, meshBlePin()); }

uint32_t bleFreeHeap() { return ESP.getFreeHeap(); }
uint32_t bleFreePsram() { return ESP.getFreePsram(); }

// EST-357: the start guard's numbers and the toast hook.
void bleSetNotice(void (*fn)(const char*)) { s_notice = fn; }
uint32_t bleStartNeedFree() { return kStartNeedFree; }
uint32_t bleStartNeedBlock() { return kStartNeedBlock; }

// Called by the transport with each frame the phone sent, before MyMesh handles it. Copies the two
// send commands and, since EST-384, takes the set-location command into the Fixed position;
// everything else is upstream's business.
void bleTransportFrame(const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0) return;
  if (data[0] == kCmdSendChannelTxtMsg && len >= 7 && data[1] == kTxtTypePlain) {
    const uint8_t channelIdx = data[2];
    const size_t bodyLen = len - 7;
    char text[kMeshTextCap];
    const size_t n = bodyLen > (size_t)kMeshTextMax ? (size_t)kMeshTextMax : bodyLen;
    memcpy(text, &data[7], n);
    text[n] = 0;
    if (text[0] != 0) meshPhoneChannelSent(channelIdx, text);
  } else if (data[0] == kCmdSendTxtMsg && len >= 13 && data[1] == kTxtTypePlain) {
    const size_t bodyLen = len - 13;
    char text[kMeshTextCap];
    const size_t n = bodyLen > (size_t)kMeshTextMax ? (size_t)kMeshTextMax : bodyLen;
    memcpy(text, &data[13], n);
    text[n] = 0;
    if (text[0] != 0) meshPhoneDmSent(&data[7], 6, text);
  } else if (data[0] == kCmdSetAdvertLatLon && len >= 9) {
    // EST-384: the phone set the node's location. That point becomes the Fixed position and the
    // source switches, so a later GPS fix cannot move the node away from what the phone sent. The
    // same int32/1e6 decode upstream uses; an out-of-range pair is refused by posPhoneSet().
    int32_t lat = 0;
    int32_t lon = 0;
    memcpy(&lat, &data[1], 4);
    memcpy(&lon, &data[5], 4);
    posPhoneSet((double)lat / 1000000.0, (double)lon / 1000000.0);
  }
}

}  // namespace om

#else  // !OM_ENABLE_MESH: no companion, so the switch has nothing to drive

#include <stddef.h>

namespace om {
bool bleBegin() { return false; }
void bleLoop() {}
void bleSetEnabled(bool) {}
bool bleIsEnabled() { return false; }
ble::Phase blePhase() { return ble::Phase::Off; }
bool bleConnected() { return false; }
uint32_t bleSerial() { return 1; }
uint32_t blePin() { return 0; }
bool blePinSet(uint32_t) { return false; }
uint32_t bleNewPin() { return 0; }
void blePinText(char* out, size_t cap) {
  if (out != nullptr && cap > 0) out[0] = 0;
}
uint32_t bleFreeHeap() { return 0; }
uint32_t bleFreePsram() { return 0; }
void bleSetNotice(void (*)(const char*)) {}
uint32_t bleStartNeedFree() { return 0; }
uint32_t bleStartNeedBlock() { return 0; }
void bleTransportFrame(const uint8_t*, size_t) {}
}  // namespace om

#endif  // OM_ENABLE_MESH
