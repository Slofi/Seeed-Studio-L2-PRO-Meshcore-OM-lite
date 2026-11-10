// SPDX-License-Identifier: MIT
// EST-356: upstream's ESP32 BLE companion transport, compiled here and wrapped by BleSerial.
//
// Why the .cpp is included instead of being built by PlatformIO: lib/meshcore's library.json builds
// `helpers/*.cpp` (direct children only), so `helpers/esp32/SerialBLEInterface.cpp` is not part of
// any library build. This file gives the byte-identical upstream source one translation unit without
// touching lib/ (REBUILD.md: never edit a file there).
#include <Arduino.h>
#include <BLEDevice.h>
#include <esp32-hal-bt.h>

#include "ble_transport.h"

#include "../lib/meshcore/helpers/esp32/SerialBLEInterface.cpp"

namespace om {
namespace {

// One transport exists. begin() creates the BLE device; the object itself only allocates its two
// frame queues at construction time (a few hundred bytes).
SerialBLEInterface s_inner;

constexpr const char* kNamePrefix = "MeshCore-";   // upstream's BLE_NAME_PREFIX default

}  // namespace

BleSerial& bleTransport() {
  static BleSerial s_serial;
  return s_serial;
}

bool BleSerial::begin(const char* name, uint32_t pin) {
  if (begun_) {
    applyPin(pin);
    return true;
  }
  // btStart() is what BLEDevice::init() calls first. Asking it here means a controller that cannot
  // come up is a false return instead of a half-initialised BLEDevice with a dead stack.
  if (!btStart()) return false;

  char dev_name[32];
  snprintf(dev_name, sizeof(dev_name), "%s", (name != nullptr && name[0] != 0) ? name : "OM Lite");
  s_inner.begin(kNamePrefix, dev_name, pin);

  begun_ = BLEDevice::getInitialized() && btStarted();
  started_ = false;
  return begun_;
}

void BleSerial::applyPin(uint32_t pin) {
  if (!begun_) return;
  // The same two calls upstream's begin() makes: the static passkey is what the phone is asked for.
  BLESecurity sec;
  sec.setStaticPIN(pin);
  sec.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
}

void BleSerial::enable() {
  if (!begun_ || started_) return;
  s_inner.enable();
  started_ = true;
}

void BleSerial::disable() {
  if (!begun_ || !started_) return;
  s_inner.disable();
  started_ = false;
}

bool BleSerial::isEnabled() const { return begun_ && started_ && s_inner.isEnabled(); }

bool BleSerial::isConnected() const { return begun_ && started_ && s_inner.isConnected(); }

bool BleSerial::isWriteBusy() const { return begun_ && started_ && s_inner.isWriteBusy(); }

size_t BleSerial::writeFrame(const uint8_t src[], size_t len) {
  if (!begun_ || !started_) return 0;
  return s_inner.writeFrame(src, len);
}

size_t BleSerial::checkRecvFrame(uint8_t dest[]) {
  if (!begun_ || !started_) return 0;
  const size_t len = s_inner.checkRecvFrame(dest);
  // The phone's own outgoing messages only exist in this direction; copy them to our chats before
  // MyMesh consumes the frame (the listener sees only what arrives over the air).
  if (len > 0) bleTransportFrame(dest, len);
  return len;
}

}  // namespace om
