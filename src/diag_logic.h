// SPDX-License-Identifier: MIT
// EST-376: the plain-C++ half of tomorrow's diagnostics — the `radio sample` summary arithmetic and
// the read-only FUSB302 (AW35615) field decode for `pmu probe`.
//
// No Arduino, no RadioLib, no Wire: test/host builds this file with the system g++, so the numbers
// the device prints are the numbers the test checks, not a second copy of the rule.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace diag {

// --- the audio-free sample summary --------------------------------------------------------------
// `radio sample <seconds>` keeps one noise-floor value per 5 s line and prints one summary at the
// end: samples, min / median / max / mean, the packet count and the mean RSSI and SNR.
struct SampleStats {
  int count = 0;
  int min = 0;
  int median = 0;
  int max = 0;
  int mean = 0;
};

// Sorts `values` ascending IN PLACE (insertion sort, no allocation: the sampler holds at most 128
// values) and fills count / min / median / max / mean. The median of an even count is the mean of
// the two middle values. The median and the mean are integers truncated toward zero: the noise
// floor is a whole dBm and MeshCore prints it as one.
bool summarise(int* values, int count, SampleStats* out);

// --- the USB-C port controller at 0x22 (AW35615, FUSB302-compatible) ----------------------------
// The FUSB302 has two register pages. 0x01..0x10 are the plain configuration/status page (device
// id, switches, measure, control, power); 0x3C..0x43 repeat MEASURE/SLICE/CONTROL/STATUS on the
// interrupt page. Only the reads below are safe in a probe that must not change the chip:
//   0x01..0x10, 0x3C, 0x3D, 0x40, 0x41   read, no side effect
//   0x3E, 0x3F, 0x42                    NOT read: interrupt registers, clear on read
//   0x43                                NOT read: the FIFO, a read pops a byte
constexpr uint8_t kUsbPdAddr = 0x22;
constexpr uint8_t kRegPageSize = 0x44;   // index by the register number, so no second mapping

constexpr uint8_t kRegDeviceId = 0x01;
constexpr uint8_t kRegPower = 0x0B;
constexpr uint8_t kRegMeasureLow = 0x3C;   // 0x3C MEASURE
constexpr uint8_t kRegSliceLow = 0x3D;     // 0x3D SLICE
constexpr uint8_t kRegStatus0 = 0x40;
constexpr uint8_t kRegStatus1 = 0x41;

constexpr uint8_t kFusbReadRegs[] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x3C, 0x3D, 0x40, 0x41,
};
constexpr int kFusbReadCount = (int)(sizeof(kFusbReadRegs) / sizeof(kFusbReadRegs[0]));

// False for the four registers a read-only probe must never touch (see above).
bool fusbReadable(uint8_t reg);

// What the probe read. Index = the register number; `valid[i]` says the I2C read answered.
struct RegisterSet {
  uint8_t value[kRegPageSize] = {};
  bool valid[kRegPageSize] = {};
};

// FUSB302 (datasheet) bit positions. VBUSOK is bit 7 of STATUS0, the one field the ticket names
// exactly; BC_LVL is the two-bit charging-current level and COMP the comparator output.
constexpr uint8_t kDeviceIdVersionShift = 4;   // DEVICE_ID 7:4 VERSION_ID, 3:0 PRODUCT_ID
constexpr uint8_t kDeviceIdProductMask = 0x0F;
constexpr uint8_t kStatus0VbusOkBit = 7;
constexpr uint8_t kStatus0BcLvlShift = 4;      // BC_LVL 5:4: 00 100 mA, 01 500 mA, 10 1.5 A, 11 3 A
constexpr uint8_t kStatus0BcLvlMask = 0x03;
constexpr uint8_t kStatus0CompBit = 2;

// The fields the probe can name. `have*` is false when that register was not read.
struct Fusb302 {
  bool haveId = false;
  bool havePower = false;
  bool haveStatus0 = false;
  uint8_t deviceId = 0;
  uint8_t versionId = 0;
  uint8_t productId = 0;
  uint8_t power = 0;
  uint8_t status0 = 0;
  bool vbusOk = false;
  uint8_t bcLvl = 0;
  bool comp = false;
};

Fusb302 fusb302Fields(const RegisterSet& regs);

// The BC_LVL encoding as words (STATUS0 5:4), for the log line's parenthetical.
const char* bcLevelName(uint8_t bcLvl);

// --- POWER (0x0B): the ONE register this firmware writes (EST-401) -------------------------------
// The chip answers 0x01 after power-on: the bandgap and wake circuitry only, so the measure block
// that feeds STATUS0 is dead and VBUSOK reads 0 even with a cable in (measured 2026-11-11). The
// datasheet's POWER bits this build sets, by name:
//   PWR0 (bit 0) bandgap and wake circuitry
//   PWR1 (bit 1) receiver and current references
//   PWR2 (bit 2) measure block
// Nothing else in POWER is touched: the byte to write is the read-back ORed with these three bits,
// so bits 3..7 stay exactly as the chip reported them. SWITCHES0 (0x02) is never written, and
// neither is any CC, termination or PD register: the default pull-downs are what make this board a
// USB sink and let it charge.
constexpr uint8_t kPowerBandgapBit = 1U << 0;
constexpr uint8_t kPowerReferencesBit = 1U << 1;
constexpr uint8_t kPowerMeasureBit = 1U << 2;
constexpr uint8_t kPowerMeasureOnMask =
    (uint8_t)(kPowerBandgapBit | kPowerReferencesBit | kPowerMeasureBit);   // 0x07

// The byte to write: the read-back value with the three bits above set (0x01 -> 0x07, 0x0F -> 0x0F).
uint8_t powerValueToWrite(uint8_t readBack);

// True when a POWER read-back is a value this build is willing to write back. The register's PWR
// field is bits 3:0 (its reset value is the measured 0x01), so a byte with any of 7:4 set did not come
// from the register we think we are reading; the attempt then FAILS CLOSED and writes nothing, rather
// than driving reserved bits. bit 3 is left to the chip: if the hardware ever sets it, it survives the
// OR above untouched.
bool powerReadSane(uint8_t readBack);

// True when a read-back carries every bit powerValueToWrite() asks for, i.e. the write took.
bool powerBitsPresent(uint8_t readBack);

// STATUS0 (0x40) bit 7 VBUSOK: the one field the battery log records. No other bit of STATUS0 is
// interpreted here (BC_LVL is fusb302Fields' business).
bool status0VbusOk(uint8_t status0);

// --- the runtime RX-boost choice (EST-377) -------------------------------------------------------
// `radio boost on|off` is RAM only, but the choice must outlive the resetAGC() calls that radio_diag
// runs later (a TCXO change, the AGC retry): that reset can drop the RX gain because this build does
// not define SX126X_RX_BOOSTED_GAIN. "none set" (known() false) means no boost command has been
// accepted since boot, so a reset must not invent one.
class BoostChoice {
 public:
  void note(bool on) {
    known_ = true;
    on_ = on;
  }
  bool known() const { return known_; }
  bool on() const { return on_; }
  // True exactly when a choice exists AND the chip's read-back differs from it.
  bool needsReapply(bool chipReadBack) const { return known_ && chipReadBack != on_; }

 private:
  bool known_ = false;
  bool on_ = false;
};

}  // namespace diag
}  // namespace om
