// SPDX-License-Identifier: MIT
// OM Lite board layer — Wio Tracker L2 Pro.
//
// Pin and power facts come from the board (Seeed's Meshtastic variant and the vendor schematic,
// cross-checked against the running device); the code here is ours. Nothing in this file is copied
// from WadaMesh (GPL-3.0); see the project note.
#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>

namespace om {

// --- I2C bus (shared by the expander, touch, codec, battery ADC and backlight driver) ---
constexpr int kPinSda = 47;
constexpr int kPinScl = 48;

// --- IO expander: PCA9554-compatible at 0x21 (input reg 0x00, output 0x02, config 0x06) ---
constexpr uint8_t kExpanderAddr = 0x21;
enum ExpanderBit : uint8_t {
  kBitWakeButton = 0,
  kBitI2cInterrupt = 1,
  kBitSdDetect = 2,
  kBitTouchInterrupt = 3,
  kBitLcdControl = 4,
  kBitLcdPower = 5,
  kBitLcdReset = 6,
  kBitGrovePower = 7,
  kBitTouchReset = 8,
  kBitGnssReset = 9,
  kBitUserLed = 10,
  kBitUsbOtg = 11,
  kBitAudioPa = 12,
  kBitGnssPower = 13,
  kBitSdPower = 14,
  kBitBatterySense = 15,
};

// --- display: NV3031B 240x320 over QSPI on SPI3 ---
constexpr int kPinLcdSclk = 42;
constexpr int kPinLcdIo0 = 41;
constexpr int kPinLcdIo1 = 40;
constexpr int kPinLcdIo2 = 39;
constexpr int kPinLcdIo3 = 38;
constexpr int kPinLcdCs = 46;

// --- backlight: LED driver at 0x2C (PWM registers 0x18..0x1B, latch 0x0F) ---
constexpr uint8_t kBacklightAddr = 0x2C;

// --- touch: GT911 on the shared I2C bus, reset through the expander ---
constexpr uint8_t kTouchAddr = 0x5D;

// --- buttons / user LED ---
constexpr int kPinUserButton = 0;   // active low, INPUT_PULLUP
constexpr int kPinUserLed = 45;

// Brings up I2C, the expander and its power rails, the backlight and the display.
// Returns false if the expander or the panel does not answer; the caller decides what to do.
bool boardBegin();

// 0..255
void boardSetBrightness(uint8_t brightness);

// The Wake button (expander input 0, active low). Returns false if the expander is not readable.
bool wakePressed(bool& pressed);

// Screen off: backlight 0 + panel sleep. Screen on: panel wake + backlight.
void screenPower(bool on);

// SD_MMC 1-bit (CLK 2 / CMD 3 / D0 1), card power on expander bit 14, detect on bit 2.
bool sdPresent();
bool sdMount();
void sdUnmount();

// Cell voltage in mV: ADS1115 0x48 AIN0, x2 divider. 0 when the read fails.
uint16_t batteryMilliVolts();

// L76K GNSS: UART1, RX 18 / TX 17 at 9600. gpsPoll() chews whatever has arrived and returns the
// latest GGA state; it never blocks.
struct GpsState {
  bool valid = false;      // fix quality > 0
  uint8_t sats = 0;
  double lat = 0.0;
  double lon = 0.0;
  uint32_t bytes = 0;      // bytes received on the UART (0 = the module is not talking to us)
  uint32_t sentences = 0;  // complete NMEA lines
};
void gpsBegin(bool swapPins = false);
void gpsPoll(GpsState& out);

// Speaker amplifier enable (expander bit 12). The codec itself lives in audio.cpp.
bool setAudioPa(bool on);

// --- drawing surface ---
// While a frame is open, om_display() hands out an off-screen 320x240 buffer instead of the panel,
// and omFrameEnd() pushes it in slices. A full-screen redraw then appears as one update instead of
// a blank flash and a repaint (that was the flicker while scrolling). Begin/end nest, so a view
// drawn on top of another view still pushes exactly once.
lgfx::LovyanGFX* om_display();
bool omFrameInit();   // allocate the frame buffer (call once, at boot)
bool omFrameBegin();
void omFrameEnd();
// Touch always reads the real panel, never the frame buffer.
bool omTouch(uint16_t& x, uint16_t& y);

}  // namespace om
