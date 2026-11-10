// SPDX-License-Identifier: MIT
// Minimal CayenneLPP encoder for MeshCore's companion code.
//
// MyMesh.h has a `CayenneLPP telemetry` member and MyMesh.cpp encodes battery, MCU temperature
// and (through SensorManager) a GPS fix. Upstream normally pulls ElectronicCats/CayenneLPP from
// the PlatformIO registry; this sandbox has no network, and vendoring ArduinoJson only for an
// unused JSON decoder would cost more than it is worth. This header implements exactly the
// encoders this firmware calls, with the standard LPP type codes and big-endian fields.
//
// If a later ticket needs the full library (decode, more sensor types), add it to lib_deps and
// delete this file; nothing else in src/ depends on its internals.
#pragma once

#include <stdint.h>
#include <string.h>

#define LPP_TEMPERATURE   103
#define LPP_VOLTAGE       116
#define LPP_GPS           136

#define LPP_TEMPERATURE_SIZE 2
#define LPP_VOLTAGE_SIZE     2
#define LPP_GPS_SIZE         9

class CayenneLPP {
 public:
  explicit CayenneLPP(uint8_t maxSize = 200) : max_(maxSize > sizeof(buf_) ? sizeof(buf_) : maxSize) {
    reset();
  }

  void reset() { len_ = 0; }
  uint8_t getSize() const { return len_; }
  uint8_t* getBuffer() { return buf_; }

  uint8_t addTemperature(uint8_t channel, float value) {
    if (!room(2)) return 0;
    const int16_t v = (int16_t)(value * 10.0f);
    return put(channel, LPP_TEMPERATURE, (uint16_t)v, 2);
  }

  uint8_t addVoltage(uint8_t channel, float value) {
    if (!room(2)) return 0;
    uint16_t v = (uint16_t)(value * 100.0f);
    return put(channel, LPP_VOLTAGE, v, 2);
  }

  uint8_t addGPS(uint8_t channel, float latitude, float longitude, float altitude) {
    if (!room(9)) return 0;
    const int32_t lat = (int32_t)(latitude * 10000.0f);
    const int32_t lon = (int32_t)(longitude * 10000.0f);
    const int32_t alt = (int32_t)(altitude * 100.0f);
    buf_[len_++] = channel;
    buf_[len_++] = LPP_GPS;
    put3(lat);
    put3(lon);
    put3(alt);
    return 9;
  }

 private:
  bool room(uint8_t n) const { return (uint16_t)len_ + 2 + n <= max_; }

  uint8_t put(uint8_t channel, uint8_t type, uint16_t value, uint8_t n) {
    buf_[len_++] = channel;
    buf_[len_++] = type;
    if (n == 1) {
      buf_[len_++] = (uint8_t)value;
    } else {
      buf_[len_++] = (uint8_t)(value >> 8);
      buf_[len_++] = (uint8_t)(value & 0xFF);
    }
    return (uint8_t)(2 + n);
  }

  void put3(int32_t v) {
    const uint32_t u = (uint32_t)v;
    buf_[len_++] = (uint8_t)(u >> 16);
    buf_[len_++] = (uint8_t)(u >> 8);
    buf_[len_++] = (uint8_t)u;
  }

  uint8_t buf_[200];
  uint8_t len_ = 0;
  uint8_t max_;
};
