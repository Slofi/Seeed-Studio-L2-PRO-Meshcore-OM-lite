// SPDX-License-Identifier: MIT
// OM Lite board layer — Wio Tracker L2 Pro (implementation).
//
// The power sequence, the backlight's vendor register values and the panel/touch timing follow the
// board's vendor configuration (Seeed's Meshtastic variant and the schematic). The structure and the
// code are ours; WadaMesh (GPL-3.0) was consulted as documentation only and nothing is copied.
#include "board.h"

#include <LovyanGFX.hpp>
#include <SD_MMC.h>
#include <Wire.h>
#include <lgfx/v1/panel/Panel_NV3031B.hpp>

#include "fonts.h"
#include "frame_metrics.h"   // the one luma rule (EST-355): the change dip's brightness numbers
#include "gps_mode.h"
#include "timekeeper.h"
#include "ui/layout.h"   // clampTouchToScreen: the same edge rule the hit tests use
#include "ui/rgb565.h"   // the one byte-order rule for native image pushes (EST-352)

namespace om {
namespace {

// ---------------------------------------------------------------------------- IO expander

uint8_t s_output[2] = {0xFF, 0xFF};
uint8_t s_config[2] = {0xFF, 0xFF};
bool s_expanderReady = false;

bool readRegisters(uint8_t reg, uint8_t* data, size_t len) {
  Wire.beginTransmission(kExpanderAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)kExpanderAddr, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; ++i) data[i] = (uint8_t)Wire.read();
  return true;
}

bool writeRegisters(uint8_t reg, const uint8_t* data, size_t len) {
  Wire.beginTransmission(kExpanderAddr);
  Wire.write(reg);
  for (size_t i = 0; i < len; ++i) Wire.write(data[i]);
  return Wire.endTransmission() == 0;
}

void stageInput(uint8_t bit) {
  s_config[bit >> 3] |= (uint8_t)(1U << (bit & 7));
}

void stageOutput(uint8_t bit, bool high) {
  const uint8_t mask = (uint8_t)(1U << (bit & 7));
  if (high) s_output[bit >> 3] |= mask;
  else s_output[bit >> 3] &= (uint8_t)~mask;
  s_config[bit >> 3] &= (uint8_t)~mask;
}

bool latch() {
  return writeRegisters(0x02, s_output, sizeof(s_output)) &&
         writeRegisters(0x06, s_config, sizeof(s_config));
}

bool setOutput(uint8_t bit, bool high) {
  if (!s_expanderReady) return false;
  const uint8_t o0 = s_output[0], o1 = s_output[1], c0 = s_config[0], c1 = s_config[1];
  stageOutput(bit, high);
  if (latch()) return true;
  s_output[0] = o0; s_output[1] = o1; s_config[0] = c0; s_config[1] = c1;
  return false;
}

bool expanderBegin() {
  s_expanderReady = false;
  Wire.setClock(100000);
  Wire.setTimeOut(30);
  pinMode(kPinUserButton, INPUT_PULLUP);

  if (!readRegisters(0x02, s_output, sizeof(s_output)) ||
      !readRegisters(0x06, s_config, sizeof(s_config))) {
    return false;
  }

  // Inputs: wake button, I2C interrupt, SD detect, LCD control line.
  stageInput(kBitWakeButton);
  stageInput(kBitI2cInterrupt);
  stageInput(kBitSdDetect);
  stageInput(kBitLcdControl);
  // Outputs and their power-up levels.
  stageOutput(kBitTouchInterrupt, false);
  stageOutput(kBitLcdPower, true);
  stageOutput(kBitLcdReset, true);
  stageOutput(kBitGrovePower, false);
  stageOutput(kBitTouchReset, false);
  stageOutput(kBitGnssReset, true);
  stageOutput(kBitUserLed, false);
  stageOutput(kBitUsbOtg, false);
  stageOutput(kBitAudioPa, false);
  stageOutput(kBitGnssPower, true);
  stageOutput(kBitSdPower, false);
  stageOutput(kBitBatterySense, false);
  if (!latch()) return false;
  delay(10);
  // The power rails are latched; from here on setOutput() must actually run. Without this the
  // ready guard rejected every pulse in the reset sequence, so the panel's reset never fired and
  // the GT911 was never released from reset (hence no touch at 0x5D) — measured 2026-11-09.
  s_expanderReady = true;

  // GNSS and panel reset pulses, then the panel's control line, then touch release.
  //
  // The GNSS power rail (bit 13) is set high above and never brought low anywhere in this file, so
  // a soft reboot leaves the module powered: the PCA9554 keeps its latched outputs while the SoC
  // restarts. What a reboot does do is this reset pulse, which restarts the module (its almanac is
  // in its own flash and survives; a kept RTC keeps the start warm). Do not "fix" that by dropping
  // bit 13 and raising it again: a cold module starts slower and may need a full almanac download.
  if (!setOutput(kBitGnssReset, false)) return false;
  delay(40);
  if (!setOutput(kBitLcdReset, false)) return false;
  delay(10);
  if (!setOutput(kBitLcdReset, true)) return false;
  delay(500);
  if (!setOutput(kBitLcdControl, true)) return false;
  delay(10);
  if (!setOutput(kBitTouchReset, true)) return false;
  delay(60);

  s_expanderReady = true;
  Serial.printf("[om] expander ready out=%02X/%02X cfg=%02X/%02X\n",
                s_output[0], s_output[1], s_config[0], s_config[1]);
  return true;
}

// ---------------------------------------------------------------------------- backlight

// The panel's backlight is an I2C LED driver at 0x2C. The register values are the vendor's
// bring-up configuration (channel enables + PWM + latch); brightness is 0x18..0x1B.
bool backlightWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kBacklightAddr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool backlightBegin(uint8_t brightness) {
  bool ok = true;
  ok = backlightWrite(0x00, 0x01) && ok;
  ok = backlightWrite(0x01, 0x01) && ok;
  ok = backlightWrite(0x02, 0x00) && ok;
  ok = backlightWrite(0x04, 0x4E) && ok;
  ok = backlightWrite(0x05, 0xF0) && ok;
  for (uint8_t ch = 0; ch < 4; ++ch) ok = backlightWrite(0x14 + ch, 200) && ok;
  ok = backlightWrite(0x02, 0x0F) && ok;
  boardSetBrightness(brightness);
  return ok;
}

// ---------------------------------------------------------------------------- display device

class Lgfx final : public lgfx::LGFX_Device {
 public:
  Lgfx() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI3_HOST;
      cfg.spi_mode = 3;
      cfg.freq_write = 75000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = kPinLcdSclk;
      cfg.pin_miso = -1;
      cfg.pin_mosi = -1;
      cfg.pin_dc = -1;
      cfg.pin_io0 = kPinLcdIo0;
      cfg.pin_io1 = kPinLcdIo1;
      cfg.pin_io2 = kPinLcdIo2;
      cfg.pin_io3 = kPinLcdIo3;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = kPinLcdCs;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 1;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = true;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 0;
      cfg.x_max = 239;
      cfg.y_min = 0;
      cfg.y_max = 319;
      cfg.pin_int = -1;
      cfg.bus_shared = false;
      cfg.offset_rotation = 2;
      cfg.i2c_port = 0;
      cfg.i2c_addr = kTouchAddr;
      cfg.pin_sda = kPinSda;
      cfg.pin_scl = kPinScl;
      cfg.freq = 100000;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }

 private:
  lgfx::Bus_SPI _bus;
  lgfx::Panel_NV3031B _panel;
  lgfx::Touch_GT911 _touch;
};

Lgfx s_display;

// The off-screen frame. Created on first use (LovyanGFX needs the bus up first) and kept for the
// life of the session; 320x240x16 = 154 KB from PSRAM, which the map already treats as its canvas.
lgfx::LGFX_Sprite s_frame;
int s_frameDepth = 0;
constexpr uint16_t kProbeGold = 0xED89;   // #e8b04f in RGB565, the theme colour
bool s_frameNeedsSwap = false;            // set from the boot probe
omFrameOverlay s_frameOverlay = nullptr;  // the toast, drawn last before every push (EST-344)

// EST-355: a completed frame whose push the caller will perform itself (omFrameFlush). The hold is
// one shot: it applies to the next outer omFrameEnd and is cleared there. `s_framePending` says a
// frame is composed and waiting, so a second compose cannot silently replace it.
bool s_holdPush = false;
bool s_framePending = false;
uint16_t s_pushCount = 0;
uint32_t s_pushUs = 0;
uint32_t s_frameStartUs = 0;
uint32_t s_composeUs = 0;

}  // namespace

void omFrameSetOverlay(omFrameOverlay fn) { s_frameOverlay = fn; }

uint16_t omFramePushCount() { return s_pushCount; }
uint32_t omFrameLastPushUs() { return s_pushUs; }
uint32_t omFrameLastComposeUs() { return s_composeUs; }

lgfx::LovyanGFX* om_display() {
  if (s_frameDepth > 0 && s_frame.getBuffer() != nullptr) return &s_frame;
  return &s_display;
}

void omPushRgb565(lgfx::LovyanGFX* d, int x, int y, int w, int h, const uint16_t* pixels) {
  if (d == nullptr || pixels == nullptr || w <= 0 || h <= 0) return;
  // pushImage(uint16_t*) takes the SOURCE byte order from this flag: ON means "native RGB565
  // words", OFF means "already in this surface's stored order". Our buffers are always native
  // (JPEGDEC writes little-endian RGB565), so the flag is ON for every push, whatever surface it
  // is: the frame sprite converts native -> its stored order, the panel converts native -> wire
  // order. One rule, and it is the one the host test proves with a known colour (EST-352).
  const bool wasSwap = d->getSwapBytes();
  if (!wasSwap) d->setSwapBytes(true);
  d->pushImage(x, y, w, h, pixels);
  if (!wasSwap) d->setSwapBytes(false);
}

static bool ensureFrameBuffer() {
  if (s_frame.getBuffer() != nullptr) return true;
  s_frame.setPsram(true);
  s_frame.setColorDepth(16);
  if (s_frame.createSprite(320, 240) == nullptr || s_frame.getBuffer() == nullptr) {
    // No PSRAM for it: keep drawing straight to the panel rather than failing to draw at all.
    Serial.println("[om] frame buffer: unavailable, drawing direct");
    s_frame.deleteSprite();
    return false;
  }
  Serial.printf("[om] frame buffer %dx%d (%u KB)\n", s_frame.width(), s_frame.height(),
                (unsigned)((size_t)320 * 240 * 2 / 1024));
  // Read back what the sprite actually stored for a known colour. A byte order that does not match
  // what the panel is fed is exactly what turns gold into green ("the screen is green/white"), so
  // measure it here and let the push compensate instead of assuming.
  s_frame.fillScreen(kProbeGold);
  const uint16_t stored = ((const uint16_t*)s_frame.getBuffer())[0];
  s_frame.fillScreen(0x0000);
  s_frameNeedsSwap = (stored != kProbeGold);
  useAppFont(&s_frame);   // every framed view draws with the app font
  Serial.printf("[om] frame probe %04x (native gold %04x) -> push swap %s\n", stored, kProbeGold,
                s_frameNeedsSwap ? "ON" : "OFF");
  // EST-352: the gold probe above only says what fillScreen stores. Prove the actual IMAGE path too:
  // one native word through omPushRgb565 (the map tiles' rule) and back out of omFramePixel.
  uint16_t goldStored = 0, goldBack = 0;
  const bool goldOk = omFrameColourProbe(kProbeGold, goldStored, goldBack);
  Serial.printf("[om] colour push probe gold %04x -> stored %04x -> back %04x %s\n", kProbeGold,
                goldStored, goldBack, goldOk ? "OK" : "FAILED");
  s_frame.fillScreen(0x0000);
  return true;
}

bool omFrameColourProbe(uint16_t native, uint16_t& stored, uint16_t& back) {
  stored = 0;
  back = 0;
  if (!ensureFrameBuffer() || s_frame.getBuffer() == nullptr) return false;
  uint16_t* px = (uint16_t*)s_frame.getBuffer();
  const uint16_t saved = px[0];      // the frame belongs to the screen: put its pixel back
  omPushRgb565(&s_frame, 0, 0, 1, 1, &native);
  stored = px[0];
  back = omFramePixel(0, 0);
  px[0] = saved;
  const uint16_t expectStored = s_frameNeedsSwap ? ui::rgb565FrameStore(native) : native;
  return stored == expectStored && back == native;
}

// Allocate at boot: the log then says whether the off-screen path exists, instead of the first tap
// discovering it, and no 154 KB allocation happens inside a touch handler.
bool omFrameInit() { return ensureFrameBuffer(); }

bool omFrameBegin() {
  if (s_frameDepth == 0) {
    ensureFrameBuffer();
    s_frameStartUs = micros();   // EST-365: one compose time per outer frame
  }
  ++s_frameDepth;
  return s_frame.getBuffer() != nullptr;
}

namespace {

// The one push of a finished frame. Five slices of 48 rows: 320 x 48 x 2 = 30,720 B stays under the
// SPI peripheral's `SPI_MS_DATA_BITLEN` cap of 0x3FFFF bits = 32,767 B per DMA transfer (a 64-row
// slice would be 40,960 B and split into two transfers with a 1 ms wait between them,
// `Bus_SPI::writeBytes`), and 240 = 5 x 48 exactly. Each slice is one contiguous DMA (the sprite is
// pushed raw, `no_convert`). 153,600 B of pixels at 4 bits per clock and 75 MHz is 4.10 ms of wire
// time (see cfg.freq_write above); omFrameLastPushUs() is what the bus really does.
uint32_t pushFrame() {
  const uint16_t* px = (const uint16_t*)s_frame.getBuffer();
  // The panel is configured with setSwapBytes(true) (it wants swapped bytes on the wire). If the
  // sprite stored swapped words too, the panel's second swap turns gold green: compensate only when
  // the boot probe says the two orders differ.
  if (s_frameNeedsSwap) s_display.setSwapBytes(false);
  const uint32_t startUs = micros();
  for (int yy = 0; yy < 240; yy += 48) {
    s_display.pushImage(0, yy, 320, 48, px + (size_t)yy * 320);
  }
  const uint32_t pushUs = micros() - startUs;
  if (s_frameNeedsSwap) s_display.setSwapBytes(true);
  s_pushUs = pushUs;
  ++s_pushCount;
  return pushUs;
}

}  // namespace

void omFrameEnd() {
  if (s_frameDepth == 0) return;
  if (--s_frameDepth > 0) return;          // an inner view: the outer one still owns the push
  if (s_frame.getBuffer() == nullptr) return;
  // The frame is finished: draw the overlay (the toast) LAST, on top of every screen, then push.
  // This is the one point every redraw path passes through, so a later frame cannot erase it
  // (EST-344). It runs only on the outer frame, after a complete screen.
  if (s_frameOverlay != nullptr) s_frameOverlay(&s_frame, (uint32_t)millis());
  s_composeUs = micros() - s_frameStartUs;   // draw + overlay, measured just before the push
  // EST-355: the screen-change transition holds this one push, measures the composed frame and makes
  // the push itself (omFrameFlush) with the light off. The hold is one shot, so any other redraw
  // during the transition still pushes normally.
  if (s_holdPush) {
    s_holdPush = false;
    s_framePending = true;
    return;
  }
  (void)pushFrame();
}

void omFrameHoldPush(bool hold) { s_holdPush = hold; }

bool omFramePending() { return s_framePending; }

bool omFrameFlush() {
  if (!s_framePending || s_frame.getBuffer() == nullptr) return false;
  s_framePending = false;
  (void)pushFrame();
  return true;
}

uint8_t omFrameMeanLuma(int step) {
  if (s_frame.getBuffer() == nullptr) return 0;
  if (step < 1) step = 1;
  const int w = (int)s_frame.width();
  const int h = (int)s_frame.height();
  const uint16_t* px = (const uint16_t*)s_frame.getBuffer();
  uint32_t sum = 0;
  uint32_t count = 0;
  // The same native-value read the screenshot uses: the sprite stores the panel's byte order, and
  // brightness must not depend on which way this panel happens to keep colour.
  for (int y = step / 2; y < h; y += step) {
    const uint16_t* row = px + (size_t)y * (size_t)w;
    for (int x = step / 2; x < w; x += step) {
      const uint16_t stored = row[x];
      sum += lumaFromRgb565(s_frameNeedsSwap ? ui::rgb565FrameRead(stored) : stored);
      ++count;
    }
  }
  return meanLumaFromSum(sum, count);
}

bool omTouch(uint16_t& x, uint16_t& y) {
  if (!s_display.getTouch(&x, &y)) return false;
  // The GT911 can report a point on or past the panel edge at the very bottom (239 or more after
  // rotation). Clamp before any hit test, so the last pixel rows still belong to the bottom key
  // (EST-338). Log the first clamp on a boot; it is the device evidence for the raw value.
  const int w = (int)s_display.width();
  const int h = (int)s_display.height();
  int cx = (int)x, cy = (int)y;
  ui::clampTouchToScreen(cx, cy, w, h);
  if (cx != (int)x || cy != (int)y) {
    static bool logged = false;
    if (!logged) {
      Serial.printf("[touch] raw %u,%u -> %d,%d (clamped to %dx%d)\n", (unsigned)x, (unsigned)y,
                    cx, cy, w, h);
      logged = true;
    }
    x = (uint16_t)cx;
    y = (uint16_t)cy;
  }
  return true;
}

// ---------------------------------------------------------------------------- screenshot

bool omFrameSize(int& width, int& height) {
  if (s_frame.getBuffer() == nullptr) return false;
  width = (int)s_frame.width();
  height = (int)s_frame.height();
  return width > 0 && height > 0;
}

uint16_t omFramePixel(int x, int y) {
  const uint16_t stored = ((const uint16_t*)s_frame.getBuffer())[(size_t)y * s_frame.width() + (size_t)x];
  // The push above sends the stored words as they lie in memory when the probe found them swapped;
  // the host wants the native value (low byte first). Undo the sprite's order here, once, so the
  // screenshot tool never has to know which way this panel happens to store colour.
  return s_frameNeedsSwap ? ui::rgb565FrameRead(stored) : stored;
}

// A plain I2C sweep. Its output is the first thing to read when a rail or a device is silent:
// 0x21 expander, 0x2C backlight driver, 0x48 battery ADC, 0x18 codec, 0x5D touch.
void i2cScan() {
  Serial.print("[i2c] found:");
  uint8_t n = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" %02X", addr);
      ++n;
    }
  }
  Serial.printf(" (%u device%s)\n", n, n == 1 ? "" : "s");
}

// EST-372: a READ-ONLY identity probe for the addresses the scan cannot name. It reads bytes and
// prints them; it never writes, and nothing in the app uses the result yet. 0x34 is a common PMU
// address (an AXP2101 answers 0x4A in register 0x03), 0x22 is probed as a possible second
// expander (P0 input, config register), 0x14 as the remaining unknown. The point is that the next
// boot log can answer "what is 0x34" instead of guessing it. No charge decision is made here: a
// battery voltage trend is not a charge signal.
static bool readByteAt(uint8_t addr, uint8_t reg, uint8_t* out) {
  if (out == nullptr) return false;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, 1) != 1) return false;
  *out = (uint8_t)Wire.read();
  return true;
}

static void pmuProbe() {
  uint8_t id = 0;
  if (readByteAt(0x34, 0x03, &id)) {
    Serial.printf("[pmu] 0x34 reg03=0x%02X%s\n", id, id == 0x4A ? " (AXP2101 chip id)" : "");
  } else {
    Serial.printf("[pmu] 0x34 reg03: no answer\n");
  }
  uint8_t s0 = 0, s1 = 0;
  if (readByteAt(0x34, 0x00, &s0) && readByteAt(0x34, 0x01, &s1)) {
    Serial.printf("[pmu] 0x34 reg00=0x%02X reg01=0x%02X (raw, not interpreted)\n", s0, s1);
  }
  uint8_t in = 0, cfg = 0;
  if (readByteAt(0x22, 0x00, &in) && readByteAt(0x22, 0x06, &cfg)) {
    Serial.printf("[pmu] 0x22 in=0x%02X cfg=0x%02X\n", in, cfg);
  }
  uint8_t u14 = 0;
  if (readByteAt(0x14, 0x00, &u14)) Serial.printf("[pmu] 0x14 reg00=0x%02X\n", u14);
}

bool boardBegin() {
  Wire.begin(kPinSda, kPinScl, 100000);
  delay(20);
  i2cScan();

  const bool expander = expanderBegin();
  Serial.printf("[om] expander: %s\n", expander ? "ok" : "FAILED");
  if (expander) {
    uint8_t in[2] = {0xFF, 0xFF};
    if (readRegisters(0x00, in, sizeof(in))) {
      Serial.printf("[om] expander inputs: %02X/%02X (wake bit %s)\n",
                    in[0], in[1], (in[0] & 1) ? "high" : "low");
    }
  }

  // A second sweep after the rails are up: 0x5D must appear once the GT911 is released from reset.
  i2cScan();
  // EST-372: one read-only look at the devices the scan cannot name. Nothing uses the result yet.
  pmuProbe();

  // Continue even without the expander: the scan output is the diagnosis, and a partially powered
  // panel may still answer. The return value stays false so the app can say so.
  const bool light = backlightBegin(kScreenBrightness);
  Serial.printf("[om] backlight: %s\n", light ? "ok" : "FAILED");

  s_display.init();
  s_display.setSwapBytes(true);
  s_display.setRotation(0);
  // LovyanGFX's touch driver takes over the I2C peripheral; re-own it for our other devices.
  Wire.end();
  Wire.begin(kPinSda, kPinScl, 100000);
  Serial.printf("[om] display %dx%d\n", s_display.width(), s_display.height());
  return expander && light;
}

void boardSetBrightness(uint8_t brightness) {
  for (uint8_t ch = 0; ch < 4; ++ch) (void)backlightWrite(0x18 + ch, brightness);
  (void)backlightWrite(0x0F, 0x55);
}

bool wakePressed(bool& pressed) {
  uint8_t in[2] = {0xFF, 0xFF};
  if (!s_expanderReady || !readRegisters(0x00, in, sizeof(in))) return false;
  pressed = (in[0] & 1) == 0;   // the wake button pulls its expander input low
  return true;
}

void panelPower(bool on) {
  // sleep() = SLPIN only; wakeup() = SWRESET + 120 ms + the full init table (ending in SLPOUT) +
  // DISPON. See board.h for the whole sequence and why the caller sends DISPOFF right after it.
  if (on) s_display.wakeup();
  else s_display.sleep();
}

void panelDisplay(bool on) {
  // NV3031B DISPOFF (0x28) / DISPON (0x29 + the dummy byte LovyanGFX adds). The display switches
  // without touching the panel's RAM, so the frame pushed while it is off appears complete.
  if (on) s_display.powerSaveOff();
  else s_display.powerSaveOn();
}

void screenPower(bool on) {
  if (on) {
    panelPower(true);
    panelDisplay(true);
    delay(kPanelFramePeriodMs);
    boardSetBrightness(kScreenBrightness);
  } else {
    // Light off first, then the display, then the panel: nothing is ever seen half-lit or scanning.
    boardSetBrightness(0);
    panelDisplay(false);
    panelPower(false);
  }
}

// ---------------------------------------------------------------------------- SD card

// The card is on the SoC's SD_MMC peripheral in 1-bit mode (CLK 2, CMD 3, D0 1), not the display's
// SPI bus. Card power is expander bit 14; detect is input bit 2, low when a card is inserted.
bool sdPresent() {
  uint8_t in[2] = {0xFF, 0xFF};
  if (!s_expanderReady || !readRegisters(0x00, in, sizeof(in))) return false;
  return (in[0] & (1U << kBitSdDetect)) == 0;
}

bool sdMount() {
  if (!setOutput(kBitSdPower, true)) return false;
  delay(50);
  SD_MMC.setPins(2, 3, 1);
  // 20 MHz, 1-bit: CLK/CMD/D0 are ordinary GPIOs through the matrix, not an IOMUX SD pad group.
  return SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT);
}

void sdUnmount() {
  SD_MMC.end();
  (void)setOutput(kBitSdPower, false);
}

// ---------------------------------------------------------------------------- battery

// ADS1115 at 0x48, single-ended AIN0, +/-4.096 V, single shot. The board divides the cell voltage
// by 2, so the cell is twice what the ADC sees. Sense is enabled on expander bit 15 per read.
uint16_t batteryMilliVolts() {
  if (!s_expanderReady) return 0;
  if (!setOutput(kBitBatterySense, true)) return 0;
  delay(5);
  Wire.beginTransmission(0x48);
  Wire.write(0x01);              // config
  Wire.write(0xC3);              // OS=1, MUX=AIN0-GND, PGA=+/-4.096V, single shot, 128 SPS
  Wire.write(0x83);
  const bool ok = Wire.endTransmission() == 0;
  uint16_t raw = 0;
  if (ok) {
    delay(12);
    Wire.beginTransmission(0x48);
    Wire.write(0x00);            // conversion
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x48, 2) == 2) {
      raw = (uint16_t)((Wire.read() << 8) | Wire.read());
    }
  }
  (void)setOutput(kBitBatterySense, false);
  if (!raw) return 0;
  const float volts = (float)(int16_t)raw * 4.096f / 32767.0f;
  return (uint16_t)(volts * 2.0f * 1000.0f);
}

// ---------------------------------------------------------------------------- GNSS

// The L76K talks NMEA at 9600. GGA gives fix/satellites-used/position, GSV gives satellites in view
// per constellation and their signal strengths, RMC/ZDA give time. The parser keeps a line buffer
// and never blocks, so the UI stays responsive whatever the module is doing. The GSV counting, the
// checksum and the 60 s best-C/N0 window are in gps_sky.h so test/host can prove them.
static char s_nmea[128];
static uint8_t s_nmeaLen = 0;

// The mode command is NOT sent at boot. gpsBegin() used to sleep 250 ms and write "$PCAS04,7" into a
// module that had only just had its reset released; the boot log showed binary bytes before the first
// NMEA line and the module stayed in its factory GPS+BeiDou mode. gps_mode.h now waits for the first
// checksum-valid sentence (the module is up) and sends the ONE command there, once (EST-339: the
// EST-338 TX probe and the mode 5/mode 3 attempts are gone, and "accepted" is never claimed). The
// module's flash is never written ($PCAS00 is never sent).
static GpsModeCommand s_gpsMode;

// One complete $... command with the L76K's XOR checksum.
static void sendL76KCommand(const char* body) {
  uint8_t sum = 0;
  for (const char* p = body; *p; ++p) sum ^= (uint8_t)*p;
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "$%s*%02X\r\n", body, sum);
  Serial1.print(cmd);
}

static double nmeaToDegrees(const char* field, const char* hemi) {
  if (!field || !*field) return 0.0;
  const double value = atof(field);
  const int deg = (int)(value / 100.0);
  const double minutes = value - deg * 100.0;
  double result = deg + minutes / 60.0;
  if (hemi && (*hemi == 'S' || *hemi == 'W')) result = -result;
  return result;
}

void gpsBegin(bool swapPins) {
  // Arduino's begin(baud, config, rxPin, txPin): 18 is the pin we RECEIVE on by the reference
  // wiring; the swap exists only as a bench check when no byte arrives at all.
  Serial1.end();
  if (swapPins) {
    Serial1.begin(9600, SERIAL_8N1, 17, 18);
    Serial.println("[gps] retry with RX=17 TX=18");
  } else {
    Serial1.begin(9600, SERIAL_8N1, 18, 17);
  }
  s_nmeaLen = 0;
  // The sky counts (expected/heard, the 60 s window, max heard) live in the caller's GpsState now,
  // which starts zeroed. The only re-begin is the 10 s pin retry, and that runs only while no byte
  // has ever arrived (bytes == 0), so there is no sky state to clear here.
  // A re-begin means the module may have restarted too, so the command is armed again and goes out
  // once more as soon as the module talks.
  s_gpsMode.onModuleReboot();
}

void gpsPoll(GpsState& out) {
  while (Serial1.available()) {
    const char c = (char)Serial1.read();
    ++out.bytes;
    if (c == '\r') continue;
    if (c == '\n') {
      ++out.sentences;
      s_nmea[s_nmeaLen] = 0;
      s_nmeaLen = 0;
      if (!gpsNmeaChecksumOk(s_nmea)) {
        ++out.checksumBad;   // a garbled line is never parsed, so it cannot move the fix or the clock
        continue;
      }
      // A whole, trusted sentence is the proof that the module is up. The first one sends the one
      // mode command; every later one sends nothing (s_gpsMode stays sent until gpsBegin re-arms it).
      if (s_gpsMode.onValidSentence(s_nmea)) {
        Serial.println("[gps] mode 7 sent");
        sendL76KCommand("PCAS04,7");
      }
      // $GNGGA,hhmmss.ss,lat,N,lon,E,quality,sats,...   sats = USED in the fix
      if (strncmp(s_nmea, "$GNGGA", 6) == 0 || strncmp(s_nmea, "$GPGGA", 6) == 0) {
        char* fields[15] = {nullptr};
        uint8_t n = 0;
        char* p = s_nmea;
        while (p && n < 15) {
          fields[n++] = p;
          char* comma = strchr(p, ',');
          if (!comma) break;
          *comma = 0;
          p = comma + 1;
        }
        if (n >= 8) {
          out.valid = atoi(fields[6]) > 0;
          out.sats = (uint8_t)atoi(fields[7]);
          out.lat = nmeaToDegrees(fields[2], fields[3]);
          out.lon = nmeaToDegrees(fields[4], fields[5]);
        }
      } else if (strlen(s_nmea) >= 6 && strncmp(s_nmea + 3, "GSV", 3) == 0) {
        // Satellites EXPECTED (almanac "in view") and HEARD (a C/N0 above zero), per constellation:
        // the only way to tell "hears nothing" from "hears plenty, too weakly for a fix".
        gpsParseGsv(s_nmea, out, millis());
      } else if (strlen(s_nmea) >= 6 &&
                 (strncmp(s_nmea + 3, "RMC", 3) == 0 || strncmp(s_nmea + 3, "ZDA", 3) == 0)) {
        // Time (and date) from RMC/ZDA. The parser checks the checksum, the fix status and the date
        // range, so the module's default date before its first fix never reaches the clock.
        const ParsedTime t = parseNmeaTime(s_nmea);
        if (t.ok) out.timeEpoch = t.epoch;
      }
      continue;
    }
    if (s_nmeaLen < sizeof(s_nmea) - 1) s_nmea[s_nmeaLen++] = c;
  }
  // Once per poll, with no data needed: the 60 s best-C/N0 window has to expire even when the module
  // has gone quiet (a stale "best" from the last satellite heard would otherwise stay for ever).
  gpsSkyRefresh(out, millis());
}

bool setAudioPa(bool on) { return setOutput(kBitAudioPa, on); }

}  // namespace om
