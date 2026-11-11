// SPDX-License-Identifier: MIT
// EST-376: the summary arithmetic and the FUSB302 decode. See diag_logic.h.
#include "diag_logic.h"

namespace om {
namespace diag {

bool summarise(int* values, int count, SampleStats* out) {
  if (out == nullptr) return false;
  *out = SampleStats();
  if (values == nullptr || count <= 0) return false;

  // Insertion sort: stable, in place, no allocation. The sampler never holds more than 128 values,
  // so the quadratic worst case is at most 8,128 comparisons once, at the end of a run.
  for (int i = 1; i < count; ++i) {
    const int key = values[i];
    int j = i - 1;
    while (j >= 0 && values[j] > key) {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = key;
  }

  int64_t sum = 0;
  for (int i = 0; i < count; ++i) sum += values[i];

  out->count = count;
  out->min = values[0];
  out->max = values[count - 1];
  out->median = (count % 2 != 0) ? values[count / 2]
                                 : (values[count / 2 - 1] + values[count / 2]) / 2;
  out->mean = (int)(sum / count);   // truncates toward zero, like every other integer here
  return true;
}

bool fusbReadable(uint8_t reg) {
  // 0x3E / 0x3F / 0x42 are the interrupt registers (clear on read), 0x43 is the FIFO (a read pops
  // a byte). A probe that changes the chip is not read-only, so none of the four is ever read.
  return !(reg == 0x3E || reg == 0x3F || reg == 0x42 || reg == 0x43);
}

Fusb302 fusb302Fields(const RegisterSet& regs) {
  Fusb302 f;
  if (regs.valid[kRegDeviceId]) {
    f.haveId = true;
    f.deviceId = regs.value[kRegDeviceId];
    f.versionId = (uint8_t)(f.deviceId >> kDeviceIdVersionShift);
    f.productId = (uint8_t)(f.deviceId & kDeviceIdProductMask);
  }
  if (regs.valid[kRegPower]) {
    f.havePower = true;
    f.power = regs.value[kRegPower];
  }
  if (regs.valid[kRegStatus0]) {
    f.haveStatus0 = true;
    f.status0 = regs.value[kRegStatus0];
    f.vbusOk = (f.status0 & (uint8_t)(1U << kStatus0VbusOkBit)) != 0;
    f.bcLvl = (uint8_t)((f.status0 >> kStatus0BcLvlShift) & kStatus0BcLvlMask);
    f.comp = (f.status0 & (uint8_t)(1U << kStatus0CompBit)) != 0;
  }
  return f;
}

const char* bcLevelName(uint8_t bcLvl) {
  switch (bcLvl & kStatus0BcLvlMask) {
    case 0x00: return "default 100mA";
    case 0x01: return "500mA";
    case 0x02: return "1.5A";
    default: return "3A";
  }
}

}  // namespace diag
}  // namespace om
