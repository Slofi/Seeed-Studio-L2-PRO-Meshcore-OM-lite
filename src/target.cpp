// SPDX-License-Identifier: MIT
// OM Lite board glue for MeshCore's companion code (implementation).
//
// The structure follows MeshCore's own MIT variants (variants/xiao_s3_wio): the board, radio,
// wrapper, clock and sensors are ordinary globals that MyMesh.cpp references through <target.h>.
// Nothing here is upstream companion code; that is vendored unmodified in lib/meshcore_companion/.
#include <Arduino.h>
#include <SPI.h>

#include "target.h"

#include "board.h"
#include "clock_keep.h"   // EST-403: save the epoch before a restart we control
#include "position.h"
#include "radio_stats.h"
#include "radio_fault.h"
#include "radio_diag.h"
#include "timekeeper.h"

// Set by the facade once the_mesh.begin() has returned. Before that, MeshCore's
// bootstrapRTCfromContacts() writes the clock; after it, CMD_SET_DEVICE_TIME is the phone.
bool target_mesh_begun = false;

// --- globals MyMesh.cpp expects ------------------------------------------------------------------
OmBoard board;

static SPIClass s_spi(FSPI);
static Module s_module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, s_spi);
static CustomSX1262 s_radio(&s_module);

OmSX1262Wrapper radio_driver(s_radio, board);
OmRTCClock rtc_clock;
OmSensors sensors;
OmNullSerialInterface null_serial;

// --- radio ---------------------------------------------------------------------------------------
bool radio_init() {
  // CustomSX1262::std_init() sets the SPI pins itself and then runs RadioLib begin() with the
  // LORA_* values from target.h (869.618 / 62.5 / SF8 / CR8 / 22 dBm, TCXO 1.8 V).
  s_spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  return s_radio.std_init(&s_spi);
}

uint32_t radio_get_rng_seed() { return s_radio.random(0x7FFFFFFF); }

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  s_radio.setFrequency(freq);
  s_radio.setSpreadingFactor(sf);
  s_radio.setBandwidth(bw);
  s_radio.setCodingRate(cr);
}

void radio_set_tx_power(int8_t dbm) { s_radio.setOutputPower(dbm); }

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(s_radio);
  return mesh::LocalIdentity(&rng);
}

// --- radio fault mark ----------------------------------------------------------------------------
// MeshCore queues a packet and reports success before the SX1262 transmits. startSendRaw() is the
// RadioLib answer, isSendComplete() catches a TX that never finishes, onSendFinished() clears the
// mark after a real transmission (EST-337's rule: a failed TX must still set it).
int OmSX1262Wrapper::recvRaw(uint8_t* bytes, int sz) {
  const int len = CustomSX1262Wrapper::recvRaw(bytes, sz);
  if (len > 0) {
    // The RSSI/SNR are the chip's values for the packet just read (the same ones upstream's
    // logRxRaw() reports). The wall clock and its "real" flag come from the one timekeeper, so a
    // packet heard before a clock sync is not dated 2024.
    const int rssi = (int)lroundf(getLastRSSI());
    const int snr4 = (int)lroundf(getLastSNR() * 4.0f);
    om::radio::noteRawPacket(bytes, len, rssi, snr4, om::timeKeeper().now(),
                             om::timeKeeper().realSinceBoot(), millis());
    // EST-376: the `radio sample` packet line (a no-op unless a sample is running). It reuses the
    // same two numbers and the same first payload byte as the ring above.
    om::rdiag::notePacket(bytes, len, rssi, snr4);
  }
  return len;
}

bool OmSX1262Wrapper::startSendRaw(const uint8_t* bytes, int len) {
  const bool ok = CustomSX1262Wrapper::startSendRaw(bytes, len);
  if (!ok) {
    txWaiting_ = false;
    txTimedOut_ = false;
    if (om::radioFaultState().set(om::RadioFault::Tx)) {
      Serial.printf("[radio] fault: %s\n", om::radioFaultToken(om::RadioFault::Tx));
    }
    return false;
  }
  txWaiting_ = true;
  txTimedOut_ = false;
  txStartMs_ = millis();
  return true;
}

bool OmSX1262Wrapper::isSendComplete() {
  const bool done = CustomSX1262Wrapper::isSendComplete();
  if (done) {
    txWaiting_ = false;
    return true;
  }
  if (txWaiting_ && (uint32_t)(millis() - txStartMs_) > kTxTimeoutMs) {
    txWaiting_ = false;
    txTimedOut_ = true;   // a late completion still clears the mark in onSendFinished()
    if (om::radioFaultState().set(om::RadioFault::Tx)) {
      Serial.printf("[radio] fault: %s\n", om::radioFaultToken(om::RadioFault::Tx));
    }
  }
  return false;
}

void OmSX1262Wrapper::onSendFinished() {
  CustomSX1262Wrapper::onSendFinished();
  const bool wasSending = txWaiting_ || txTimedOut_;
  txWaiting_ = false;
  txTimedOut_ = false;
  if (wasSending && om::radioFaultState().onTxOk()) Serial.println("[radio] fault cleared");
}

// --- board ---------------------------------------------------------------------------------------
uint16_t OmBoard::getBattMilliVolts() { return om::batteryMilliVolts(); }
void OmBoard::reboot() {
  om::clockKeepStoreNow();   // EST-403: the software reset keeps the RTC counter, not the epoch
  ESP.restart();
}
float OmBoard::getMCUTemperature() { return temperatureRead(); }

// --- clock ---------------------------------------------------------------------------------------
uint32_t OmRTCClock::getCurrentTime() { return om::timeKeeper().now(); }

void OmRTCClock::setCurrentTime(uint32_t time) {
  if (target_mesh_begun) {
    // CMD_SET_DEVICE_TIME from the phone: a real source, ranked below GPS and Wi-Fi.
    om::timeKeeper().setFromSource(time, om::TimeSource::Phone, millis() / 1000);
  } else {
    // bootstrapRTCfromContacts() during begin(): plumbing, never a trusted source. The timekeeper
    // clamps it to its floor, so a 2024-era contact stamp cannot move the clock backwards.
    om::timeKeeper().setHardware(time);
  }
}

// --- sensors with GPS ----------------------------------------------------------------------------
void OmSensors::setGpsFix(double lat, double lon, bool valid) {
  // EST-384: the raw fix goes to the one source, which decides whether it becomes the node's
  // position (GPS selected) or only feeds the clock (Fixed selected).
  om::posSetGpsFix(lat, lon, valid);
}

bool OmSensors::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  bool added = false;
  const om::PositionFix& own = om::posOwn();
  if ((requester_permissions & TELEM_PERM_LOCATION) && own.valid) {
    telemetry.addGPS(TELEM_CHANNEL_SELF, (float)own.lat, (float)own.lon, 0.0f);
    added = true;
  }
  return added;
}

const char* OmSensors::getSettingName(int i) const {
  if (i == 0) return "gps";
  if (i == 1) return "gps_interval";
  return nullptr;
}

const char* OmSensors::getSettingValue(int i) const {
  static char interval[12];
  if (i == 0) return gps_enabled_ ? "1" : "0";
  if (i == 1) {
    snprintf(interval, sizeof(interval), "%lu", (unsigned long)gps_interval_);
    return interval;
  }
  return nullptr;
}

bool OmSensors::setSettingValue(const char* name, const char* value) {
  if (name == nullptr || value == nullptr) return false;
  if (strcmp(name, "gps") == 0) {
    gps_enabled_ = value[0] == '1';
    return true;
  }
  if (strcmp(name, "gps_interval") == 0) {
    gps_interval_ = (uint32_t)strtoul(value, nullptr, 10);
    return true;
  }
  return false;
}

namespace om {
void targetSetGpsFix(double lat, double lon, bool valid) { sensors.setGpsFix(lat, lon, valid); }

// EST-376: the runtime A/B knobs. Called from rdiag::loop() (the loop task), never from the USB
// command stack: setTCXO writes DIO3 and setRxBoostedGainMode writes the RX gain register over SPI.
int radioDiagSetTcxo(float volts) { return s_radio.setTCXO(volts); }
int radioDiagSetBoost(bool on) { return s_radio.setRxBoostedGainMode(on); }
}  // namespace om
