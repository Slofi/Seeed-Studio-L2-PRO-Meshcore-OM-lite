// SPDX-License-Identifier: MIT
// EST-356: the phone link's transport. This is upstream MeshCore's own ESP32 BLE companion
// transport (lib/meshcore/helpers/esp32/SerialBLEInterface), used as upstream's
// examples/companion_radio/main.cpp uses it, behind our OFF-by-default switch.
//
// The wrapper exists for three reasons, none of which is a second protocol:
//   * the switch: MyMesh owns the pointer from startInterface() on, so enable()/disable() must be
//     safe to call before the BLE stack exists and while the switch is off (the upstream class
//     dereferences its server pointers);
//   * the phone's outgoing frames pass through checkRecvFrame() before MyMesh handles them, which is
//     where the phone's own channel/DM messages are copied into our chats (the listener only sees
//     what ARRIVES);
//   * heap numbers around the one place the BLE stack is brought up.
#pragma once

#include <helpers/BaseSerialInterface.h>

namespace om {

class BleSerial : public BaseSerialInterface {
 public:
  // Creates the BLE device, service and characteristic with the node's name and the stored PIN.
  // Returns false when the controller did not come up (the upstream begin() is void, so the check
  // is BLEDevice's own initialised flag).
  bool begin(const char* name, uint32_t pin);

  // The PIN the next pairing uses. Upstream's transport sets the static passkey at begin(); this
  // updates the same security parameter at runtime, so "New PIN" (or the phone's CMD_SET_DEVICE_PIN)
  // does not need a reboot.
  void applyPin(uint32_t pin);

  // BaseSerialInterface, called by MyMesh.
  void enable() override;
  void disable() override;
  bool isEnabled() const override;
  bool isConnected() const override;
  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

  bool ready() const { return begun_; }

 private:
  friend BleSerial& bleTransport();
  BleSerial() = default;

  // The upstream object is a file static in ble_transport.cpp: exactly one transport exists, and
  // this header stays free of the BLE headers (only mesh_link.cpp needs it).
  bool begun_ = false;
  bool started_ = false;
};

BleSerial& bleTransport();

// Called by the transport with every frame the phone sent, BEFORE MyMesh parses it. Defined in
// ble_link.cpp; the transport stays free of the chat stores.
void bleTransportFrame(const uint8_t* data, size_t len);

}  // namespace om
