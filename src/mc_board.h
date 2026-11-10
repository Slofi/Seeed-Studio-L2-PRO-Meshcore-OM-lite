// SPDX-License-Identifier: MIT
// MeshCore board adapter for OM Lite.
#pragma once

#include <MeshCore.h>

namespace om {
class OmBoard : public mesh::MainBoard {
public:
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override { return "OM Lite"; }
  void reboot() override;
  uint8_t getStartupReason() const override { return 0; }
};
}
