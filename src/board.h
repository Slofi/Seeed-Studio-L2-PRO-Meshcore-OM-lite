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

// EST-412: the back Grove HY2.0-4P port. Seeed's wiki names the two data pins GROVE_DAT_1 = GPIO14
// and GROVE_DAT_2 = GPIO13, and no source in this tree says which is SDA; cardkb.cpp tries both
// orders once. Grove VCC is its own 5 V boost, switched by expander bit 7 (kBitGrovePower).
constexpr int kPinGroveDat1 = 14;
constexpr int kPinGroveDat2 = 13;

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

// EST-412: Grove VCC (its own 5 V boost) through expander bit 7. `grove on|off` uses this and
// nothing else: no bus is opened and no pin mode is set, so a meter on SDA/SCL sees only the
// keyboard's own pull-ups. False when the expander write did not land (the shadow is rolled back).
bool boardGrovePower(bool on);
// The latched level of the same bit, for `grove status` and for the OFF log. False before the
// expander is ready (the boot shadow's power-up level is off).
bool boardGrovePowerOn();

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
// AW35615, FUSB302-compatible). Which registers are safe to read is diag_logic.h's rule, not this
// function's.
bool usbPdReadRegister(uint8_t reg, uint8_t* out);

// EST-401: the measure block. The chip's POWER register (0x0B) comes up at 0x01 (bandgap and wake
// only), so STATUS0's VBUSOK reads 0 even with a cable in. This is the ONE write this firmware ever
// makes to 0x22: POWER, read back, ORed with PWR0/PWR1/PWR2 (diag_logic.h's mask). SWITCHES0 (0x02)
// and every CC/termination/PD register stay untouched; a write there could drop the board's USB sink
// role and stop charging. Called once from boardBegin(), retried once when the chip was not ready,
// and it logs `[pmu] measure block on (POWER=0x07)` - or, on a failure, the reason (twice: once before
// the retry, once as the final `FAILED`).
void usbPdPowerBegin();

// EST-401: one read of STATUS0 (0x40) for the battery log's `vbus` column. False when the I2C read
// failed, which the log must record as "unknown", never as "no cable".
bool usbPdStatus0(uint8_t* out);

// EST-402: did the boot bring the measure block up? True only when the chip answered at 0x22, the
// POWER write was verified by a read-back, and STATUS0's VBUSOK therefore means something. False
// latches "unknown" for the charging bolt: the bar never shows it, whatever a later read says.
bool usbPdReady();

// EST-402: one STATUS0 read for the charging bolt, decoded to "VBUSOK is set". Returns false when
// the read failed; *present is then untouched/meaningless, and the caller must treat it as unknown,
// never as "no cable". This is a read-only helper: the register pointer is the only byte sent.
bool usbPdVbusPresent(bool* present);

// EST-376: the ADS1115's four single-ended inputs in ADC mV at the pin (the cell's x2 divider is
// applied only by batteryMilliVolts()). AIN1, AIN2 and AIN3 are read first and AIN0 LAST, so the
// conversion register and the sense rail are left exactly as a battery read leaves them.
struct AdcProbe {
  bool ok[4] = {false, false, false, false};
  uint16_t mv[4] = {0, 0, 0, 0};   // at the pin, the same scale the battery read uses
  uint16_t raw[4] = {0, 0, 0, 0};  // the ADC's own counts, for a divider ratio later
};
void adcProbe(AdcProbe& out);

// EST-401: AIN1, AIN2 and AIN3 only, for the battery log's once-a-minute line (about 18 ms per
// channel, so the minute is the only caller). AIN0 is NOT read here: the line's batt_mv is the same
// number the status bar shows. The conversion register is left holding AIN3; every read below
// re-programs the MUX with OS=1, so nothing depends on that leftover.
struct AdcAux {
  bool ok[3] = {false, false, false};
  uint16_t mv[3] = {0, 0, 0};   // at the pin, the same scale the battery read uses
};
void adcAuxProbe(AdcAux& out);

// L76K GNSS: UART1, RX 18 / TX 17 at 9600. gpsPoll() chews whatever has arrived and returns the
// latest GGA/GSV state; it never blocks. gpsBegin() only opens the UART; after the first
// checksum-valid sentence gps_mode.h sends ONE `$PCAS04,7` (GPS + BeiDou + GLONASS), logged once as
// `[gps] mode 7 sent`. Nothing watches for it to be taken: a heard GLONASS satellite in the
// heartbeat's `heard=..(g.. l.. b..)` count is the only proof.
//
// EST-387: the reset line (expander bit 9, high = held in reset) is asserted only when this boot is a
// real power-on, a brown-out, a deep-sleep wake or an unreadable reason (gps_boot.h holds the table);
// a software, OTA, panic or watchdog restart leaves the receiver running, so `$PCAS04,7` is re-armed
// only after a reset. The boot log says which happened: `[gps] boot: kept running (reset reason N)` or
// `[gps] boot: reset (<reason>)`, then `[gps] init: mode 7 kept|armed`.
//
// GpsState and the GSV half of the parser live in gps_sky.h (plain C++): GSV's "in view" number is
// what the module's almanac EXPECTS, and only a satellite with a C/N0 value above zero is HEARD, so
// the two counts are kept apart. That header also holds the 60 s best-C/N0 window and the most
// satellites heard at once since boot.
//
// EST-387 aiding lives in gps_aid.h (CASIC AID-INI, the ACK/NAK reader, the once-per-boot rule) and is
// wired here; its switch is OFF until the device proves the module acknowledges the frame.
void gpsBegin(bool swapPins = false);
void gpsPoll(GpsState& out);

// EST-383: `gps raw <seconds>`. Opens a time-boxed diagnostic window in gpsPoll(): every RMC, ZDA,
// GGA and GSA line is printed with its SAFE fields only (never a latitude or longitude), and one
// `[gpsraw] done ... types="..." zda=N` line names every talker+type that arrived. The window runs
// from the loop, so the USB command never blocks. start() returns false when a window is already
// running or the length is outside 5..120.
bool gpsRawStart(int seconds, uint32_t nowMs);
bool gpsRawActive();
int gpsRawSecondsLeft(uint32_t nowMs);
// EST-377: the GNSS power rail's latched state (expander bit 13). It is set high at boot and never
// dropped by this firmware today, so this is the honest 1 the battery log writes, not a constant.
bool gpsRailOn();

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

// EST-390: mix ONE native colour into every pixel of a rectangle of the frame that is being composed
// (a 0..255 alpha, the ui::pressBlend() arithmetic), clamping to the frame. This is what the pressed
// look is made of: the accent is laid over the element's own pixels, so a key, a tab, a row and a
// button do not need a second drawing path and their text keeps its contrast. Outside a frame (a
// direct draw to the panel) it does nothing, like every frame-only path.
void omFrameBlendRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t native, uint8_t alpha);

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
