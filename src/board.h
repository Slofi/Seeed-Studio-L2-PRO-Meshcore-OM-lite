// SPDX-License-Identifier: MIT
// OM Lite board layer — Wio Tracker L2 Pro.
//
// Pin and power facts come from the board (Seeed's Meshtastic variant and the vendor schematic,
// cross-checked against the running device); the code here is ours. Nothing in this file is copied
// from WadaMesh (GPL-3.0); see the project note.
#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>

#include "gps_sky.h"

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

// Screen brightness used wherever a screen is switched on (backlight PWM, 0..255).
constexpr uint8_t kScreenBrightness = 160;

// The panel's sleep-out takes this long before it accepts pixels again (LovyanGFX's Panel_NV3031B
// CMD_SLPOUT_DELAY = 120 ms; wakeup() sends SLPOUT but does not wait). A caller that wants a fresh
// frame must wake the panel, wait this long, draw, and only then light the backlight, otherwise the
// first thing the user sees is the stale frame or nothing.
//
// What LovyanGFX 1.2.27 actually sends for this panel (checked in Panel_NV3031B.cpp):
//   sleep()  = SLPIN only.
//   wakeup() = SWRESET, 120 ms, the WHOLE init table (which ends with SLPOUT + 100 ms), then DISPON
//              with the dummy byte NV303x needs. So the panel cannot be held DISPOFF through the
//              sleep-out: it turns DISPLAY ON inside wakeup(). The caller therefore sends DISPOFF
//              immediately after wakeup() returns, keeps it through the frame push, and sends DISPON
//              only once the whole frame is in the panel's RAM. The backlight stays 0 until then, so
//              nothing of the init or the scan-out is ever visible (EST-336).
// boardSetBrightness() does NOT ramp: it writes the level to the four channels and latches, so the
// light steps in one go.
constexpr uint32_t kPanelWakeOutMs = 120;
constexpr uint32_t kPanelFramePeriodMs = 20;   // one full panel frame at ~60 Hz, after DISPON

// Panel sleep/wake only: the backlight is left alone, so a caller can wake, redraw, then light it.
void panelPower(bool on);

// DISPOFF / DISPON (NV3031B 0x28 / 0x29 + the dummy byte). Keeping the display off across a wake's
// sleep-out and the frame push is what removes the diagonal artefact: the panel is never seen
// scanning a half-written frame.
void panelDisplay(bool on);

// Screen off: backlight 0 + panel sleep. Screen on: panel wake + backlight, with no redraw; use it
// only where the panel's content is still valid. The wake button uses panelPower() and redraws.
void screenPower(bool on);

// SD_MMC 1-bit (CLK 2 / CMD 3 / D0 1), card power on expander bit 14, detect on bit 2.
bool sdPresent();
bool sdMount();
void sdUnmount();

// Cell voltage in mV: ADS1115 0x48 AIN0, x2 divider. 0 when the read fails.
uint16_t batteryMilliVolts();

// EST-376: one READ-ONLY 8-bit register read from the USB-C port controller at 0x22 (believed
// AW35615, FUSB302-compatible). There is deliberately no write function here: a write to SWITCHES0
// could drop the board's USB sink role and stop charging. Which registers are safe to read is
// diag_logic.h's rule, not this function's.
bool usbPdReadRegister(uint8_t reg, uint8_t* out);

// EST-376: the ADS1115's four single-ended inputs in ADC mV at the pin (the cell's x2 divider is
// applied only by batteryMilliVolts()). AIN1, AIN2 and AIN3 are read first and AIN0 LAST, so the
// conversion register and the sense rail are left exactly as a battery read leaves them.
struct AdcProbe {
  bool ok[4] = {false, false, false, false};
  uint16_t mv[4] = {0, 0, 0, 0};   // at the pin, the same scale the battery read uses
  uint16_t raw[4] = {0, 0, 0, 0};  // the ADC's own counts, for a divider ratio later
};
void adcProbe(AdcProbe& out);

// L76K GNSS: UART1, RX 18 / TX 17 at 9600. gpsPoll() chews whatever has arrived and returns the
// latest GGA/GSV state; it never blocks. gpsBegin() only opens the UART; after the first
// checksum-valid sentence gps_mode.h sends ONE `$PCAS04,7` (GPS + BeiDou + GLONASS), logged once as
// `[gps] mode 7 sent`. Nothing watches for it to be taken: a heard GLONASS satellite in the
// heartbeat's `heard=..(g.. l.. b..)` count is the only proof.
//
// GpsState and the GSV half of the parser live in gps_sky.h (plain C++): GSV's "in view" number is
// what the module's almanac EXPECTS, and only a satellite with a C/N0 value above zero is HEARD, so
// the two counts are kept apart. That header also holds the 60 s best-C/N0 window and the most
// satellites heard at once since boot.
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
// An overlay drawn on the finished frame, once, right before omFrameEnd() pushes it. The UI registers
// the toast here (EST-344): then EVERY framed redraw carries it, no screen can paint over it, and a
// new one is live on the very next frame instead of surviving on the panel until the next repaint.
// The callback must only draw; it runs with the frame open.
using omFrameOverlay = void (*)(lgfx::LovyanGFX* d, uint32_t nowMs);
void omFrameSetOverlay(omFrameOverlay fn);

// --- EST-353/EST-355: why the diagonal is there, and what hides it ---
// Every push goes into the panel's RAM while the panel scans that same RAM out. The NV3031B scans in
// its native PORTRAIT memory order (MADCTL MV is set for our landscape rotation), i.e. down our
// frame's columns, while the push streams down the frame's rows: the two pointers cross along a
// diagonal, and everything scanned before the write arrives keeps the OLD pixels for that frame.
// A faster push cannot remove that line, it only changes its slope, and the panel wires no TE line
// (the L2 facts above and the panel config have no TE pin, and the bus has no MISO), so the frame
// phase cannot be read.
//
// EST-353 tried DISPOFF/DISPON around the push. On this panel the tear was still visible afterwards
// (Filip, 2026-11-10 ~16:30), so EST-355 does what cannot depend on the panel's timing instead: the
// BACKLIGHT is taken down for the push (screen_change.h drives it from the loop, out/settle/in in
// theme.h). The display itself stays ON: DISPOFF/DISPON are not sent any more.
//
// Two calls make that possible without duplicating the push path: omFrameHoldPush(true) composes a
// frame but leaves its push to omFrameFlush(), so the caller can measure the composed frame first
// (omFrameMeanLuma, the dip rule in frame_metrics.h) and push with the light already off. An ordinary
// redraw (a toast, a pan, a scroll, a key) never holds its push: its pixels are the same image except
// for the part that moved, so the crossing is invisible there and dipping every push would make the
// panel flash continuously.
void omFrameHoldPush(bool hold);
bool omFramePending();     // a frame is composed and its push has not happened yet
bool omFrameFlush();       // push the held frame now; true when one was pending
// The mean perceived brightness of the current frame, 0..255, from a grid of samples every `step`
// pixels in both axes (theme.h kChangeLumaStep). Costs a few hundred PSRAM reads.
uint8_t omFrameMeanLuma(int step);
uint16_t omFramePushCount();
uint32_t omFrameLastPushUs();
// EST-365: how long the last outer frame took from omFrameBegin() to the moment its push started
// (the draw + the overlay, not the SPI push). The loop attributes the difference to Draw.
uint32_t omFrameLastComposeUs();
// Touch always reads the real panel, never the frame buffer.
bool omTouch(uint16_t& x, uint16_t& y);

// Screenshot support (tools/shot.py over USB). omFrameSize() is false when there is no frame buffer,
// i.e. when the app is drawing straight to the panel and there is nothing to capture.
// omFramePixel() returns the colour AS DRAWN, in native RGB565: the panel's wire byte order is
// compensated with the same boot-probe flag the push uses, so a host that reads the low byte first
// gets real colours whatever order the sprite happens to store. Top row first, x to the right.
bool omFrameSize(int& width, int& height);
uint16_t omFramePixel(int x, int y);

// --- native RGB565 images (EST-352) -------------------------------------------------------------
// ONE rule for every native RGB565 buffer pushed into a drawing surface (map tiles today, anything
// else that decodes an image later): the buffer holds native little-endian words, while the frame
// sprite and the panel both work in the byte-exchanged rgb565_2Byte order. pushImage(uint16_t*)
// takes the SOURCE order from the surface's swap flag, so setting that flag for the push is the
// whole conversion; it is restored afterwards. Pushing without it is what made the topo tiles
// magenta/cyan (the sprite's default flag is off, its stored order is swapped).
void omPushRgb565(lgfx::LovyanGFX* d, int x, int y, int w, int h, const uint16_t* pixels);

// The boot self-test for that rule: push one native word through omPushRgb565 into the frame and
// read the frame's stored word and the screenshot read-back. `ok` means stored is the order the
// frame probe measured AND the read-back equals the native colour. No panel push, no visible frame.
bool omFrameColourProbe(uint16_t native, uint16_t& stored, uint16_t& back);

}  // namespace om
