// SPDX-License-Identifier: MIT
// OM Lite board glue for MeshCore's companion code (Wio Tracker L2 Pro).
//
// MeshCore's examples/companion_radio expects a <target.h> that provides the pins, the
// board object, the radio driver, the clock, the sensors and a few helper functions.
// Everything here is our own board adaptation (structure follows MeshCore's MIT
// variants, e.g. variants/xiao_s3_wio); the companion code itself is vendored
// unmodified in lib/meshcore_companion/.
#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/KeyValueStore.h>
#include <helpers/SensorManager.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/radiolib/RadioLibWrappers.h>

// --- board pins (Wio Tracker L2 Pro) -------------------------------------------------------------
#define P_LORA_NSS     21
#define P_LORA_DIO_1    9
#define P_LORA_RESET    7
#define P_LORA_BUSY     8
#define P_LORA_SCLK     4
#define P_LORA_MISO     5
#define P_LORA_MOSI     6

#ifndef LORA_FREQ
#define LORA_FREQ      869.618f
#endif
#ifndef LORA_BW
#define LORA_BW        62.5f
#endif
#ifndef LORA_SF
#define LORA_SF        8
#endif
#ifndef LORA_CR
#define LORA_CR        8
#endif
#ifndef LORA_TX_POWER
#define LORA_TX_POWER  22
#endif

#define SX126X_DIO2_AS_RF_SWITCH true
#define SX126X_DIO3_TCXO_VOLTAGE 1.8f

class OmSX1262Wrapper : public CustomSX1262Wrapper {
 public:
  OmSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board)
      : CustomSX1262Wrapper(radio, board) {}

  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  // EST-365: every raw packet the radio hands up passes here, including adverts and flood repeats
  // this node does not process. That is what "packets heard" means, and it is upstream's own
  // recvRaw() (no second SPI read, no behaviour change).
  int recvRaw(uint8_t* bytes, int sz) override;

 private:
  static constexpr uint32_t kTxTimeoutMs = 15000;   // longer than any legal packet's airtime
  bool txWaiting_ = false;
  bool txTimedOut_ = false;
  uint32_t txStartMs_ = 0;
};

// The board object MyMesh calls. Battery comes from our ADS1115 board layer; everything the
// generic ESP32 board adds (sleep, OTA, CLI) is either not used by this firmware or a no-op.
class OmBoard : public mesh::MainBoard {
 public:
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override { return "OM Lite"; }
  void reboot() override;
  uint8_t getStartupReason() const override { return 0; }
  float getMCUTemperature() override;
  void attachDynamicPrefs(KeyValueStore* prefs) { (void)prefs; }
};

// MeshCore's clock is the timekeeper's clock (GPS > Wi-Fi > phone > manual). setCurrentTime()
// is MeshCore plumbing: during begin() it is the contact-bootstrap path, after begin() it is
// the phone's CMD_SET_DEVICE_TIME, which is a real source ("phone").
class OmRTCClock : public mesh::RTCClock {
 public:
  uint32_t getCurrentTime() override;
  void setCurrentTime(uint32_t time) override;
  void tick() override {}
};

// Sensors with GPS. MeshCore uses node_lat/node_lon for adverts and querySensors() for
// telemetry; the GPS fix itself is parsed by board.cpp and pushed in with targetSetGpsFix().
class OmSensors : public SensorManager {
 public:
  bool begin() override { return true; }
  void loop() override {}
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  int getNumSettings() const override { return 2; }
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;

  void setGpsFix(double lat, double lon, bool valid);

 private:
  bool gps_enabled_ = false;
  uint32_t gps_interval_ = 0;
  bool fix_valid_ = false;
};

// A no-op transport, kept for builds without the phone link. Since EST-356 MyMesh gets the real BLE
// transport (src/ble_transport.*), whose BleSerial::enable() is a no-op while the switch is off, so
// MyMesh::loop() never dereferences a null _serial either way.
class OmNullSerialInterface : public BaseSerialInterface {
 public:
  void enable() override {}
  void disable() override {}
  bool isEnabled() const override { return false; }
  bool isConnected() const override { return false; }
  bool isWriteBusy() const override { return false; }
  size_t writeFrame(const uint8_t src[], size_t len) override {
    (void)src;
    (void)len;
    return 0;
  }
  size_t checkRecvFrame(uint8_t dest[]) override {
    (void)dest;
    return 0;
  }
};

extern bool target_mesh_begun;   // set by the facade after the_mesh.begin() returns
extern OmBoard board;
extern OmSX1262Wrapper radio_driver;
extern OmRTCClock rtc_clock;
extern OmSensors sensors;
extern OmNullSerialInterface null_serial;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();

// Board glue -> mesh sensors. Called by the GPS poll with the latest fix.
namespace om {
void targetSetGpsFix(double lat, double lon, bool valid);
}
