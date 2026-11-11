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
#include <esp_system.h>   // EST-387: esp_reset_reason(), the one input of the GNSS reset decision
#include <lgfx/v1/panel/Panel_NV3031B.hpp>

#include "diag_logic.h"   // EST-376: the FUSB302 register map `pmu probe` reads
#include "fonts.h"
#include "frame_metrics.h"   // the one luma rule (EST-355): the change dip's brightness numbers
#include "gps_aid.h"         // EST-387: CASIC AID-INI, the ACK/NAK reader and the once-per-boot rule
#include "gps_boot.h"        // EST-387: may this boot touch the GNSS reset line?
#include "gps_mode.h"
#include "gps_raw.h"     // EST-383: the `gps raw` diagnostic window
#include "position.h"    // EST-387: the own position an aid frame may carry
#include "sd_budget.h"   // EST-380: the SD open-file budget the mount limit is checked against
#include "timekeeper.h"
#include "ui/layout.h"   // clampTouchToScreen: the same edge rule the hit tests use
#include "ui/press.h"     // EST-390: the one pressed-look blend for omFrameBlendRect
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

// EST-397: writes the shadow to the chip and reports WHICH write landed. The output register (0x02)
// is the one the rollback decision turns on: once it is written the chip holds the new levels, so
// restoring the old shadow would make it disagree with the hardware. A failed config write (0x06)
// therefore re-reads both registers instead of guessing.
bool latch(bool* outputLatched = nullptr) {
  if (outputLatched != nullptr) *outputLatched = false;
  if (!writeRegisters(0x02, s_output, sizeof(s_output))) return false;
  if (outputLatched != nullptr) *outputLatched = true;
  return writeRegisters(0x06, s_config, sizeof(s_config));
}

bool setOutput(uint8_t bit, bool high) {
  if (!s_expanderReady) return false;
  const uint8_t o0 = s_output[0], o1 = s_output[1], c0 = s_config[0], c1 = s_config[1];
  stageOutput(bit, high);
  bool outputsWritten = false;
  if (latch(&outputsWritten)) return true;
  if (!outputsWritten) {
    // The FIRST write failed: the chip still holds the old levels, so the shadow is right again.
    s_output[0] = o0; s_output[1] = o1; s_config[0] = c0; s_config[1] = c1;
    return false;
  }
  // The output register took the new levels and the config write failed: do NOT roll back. Read the
  // chip back so the shadow says what the hardware really holds, and say so on one line.
  if (!readRegisters(0x02, s_output, sizeof(s_output)) ||
      !readRegisters(0x06, s_config, sizeof(s_config))) {
    Serial.println("[om] expander: config write failed, chip read back failed too");
    return false;
  }
  Serial.printf("[om] expander: config write failed, chip re-read out=%02X/%02X cfg=%02X/%02X\n",
                s_output[0], s_output[1], s_config[0], s_config[1]);
  return false;
}

// EST-387: the SDK's reset reason in gps_boot.h's terms. The static_asserts are the contract: the
// header is plain C++ (so test/host can prove the table) and carries ESP-IDF's numbers, so a future
// SDK that renumbered them fails the DEVICE build instead of silently changing the rule.
GpsBootReason espBootReason() {
  static_assert((int)ESP_RST_UNKNOWN == (int)GpsBootReason::Unknown, "ESP_RST_UNKNOWN number");
  static_assert((int)ESP_RST_POWERON == (int)GpsBootReason::PowerOn, "ESP_RST_POWERON number");
  static_assert((int)ESP_RST_EXT == (int)GpsBootReason::External, "ESP_RST_EXT number");
  static_assert((int)ESP_RST_SW == (int)GpsBootReason::Software, "ESP_RST_SW number");
  static_assert((int)ESP_RST_PANIC == (int)GpsBootReason::Panic, "ESP_RST_PANIC number");
  static_assert((int)ESP_RST_INT_WDT == (int)GpsBootReason::IntWdt, "ESP_RST_INT_WDT number");
  static_assert((int)ESP_RST_TASK_WDT == (int)GpsBootReason::TaskWdt, "ESP_RST_TASK_WDT number");
  static_assert((int)ESP_RST_WDT == (int)GpsBootReason::Wdt, "ESP_RST_WDT number");
  static_assert((int)ESP_RST_DEEPSLEEP == (int)GpsBootReason::DeepSleep, "ESP_RST_DEEPSLEEP number");
  static_assert((int)ESP_RST_BROWNOUT == (int)GpsBootReason::Brownout, "ESP_RST_BROWNOUT number");
  return gpsBootReasonOf((int)esp_reset_reason());
}

// True when expanderBegin() asserted the GNSS reset line this boot. gpsBegin() reads it: a receiver
// that was restarted needs its one-time init command again, a receiver that kept running does not.
bool s_gnssResetThisBoot = false;

// The latched level of the GNSS reset line (bit 9): true = asserted = the module is held in reset.
bool gnssResetAsserted() {
  return (s_output[kBitGnssReset >> 3] & (uint8_t)(1U << (kBitGnssReset & 7))) != 0;
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

  // EST-387: the two inputs of the reset decision, read where both are available.
  //   the SDK's reset reason (gps_boot.h's table decides; the numbers are static_asserted below), and
  //   the level the expander was left holding. If the PREVIOUS boot died between the latch and the
  //   release, it left the line asserted: the receiver is sitting in reset and would never talk, so
  //   that is worth a reset whatever this boot's reason says. On a power-on the expander's output
  //   register reads back its power-up default (all ones), which lands in the same branch, which is
  //   right for a cold boot anyway.
  const GpsBootReason bootReason = espBootReason();
  const bool reasonResets = gpsBootResetsReceiver(bootReason);
  const bool heldFromLastBoot = gnssResetAsserted();
  const bool resetReceiver = reasonResets || heldFromLastBoot;

  // Inputs: wake button, I2C interrupt, SD detect. (The LCD control line is an OUTPUT: line 200's
  // setOutput(kBitLcdControl, true) sets its config bit, and this function's own stageOutput() would
  // clear it anyway, so a stageInput() here was a no-op that read as intent. EST-397, #11.)
  stageInput(kBitWakeButton);
  stageInput(kBitI2cInterrupt);
  stageInput(kBitSdDetect);
  // Outputs and their power-up levels.
  stageOutput(kBitTouchInterrupt, false);
  stageOutput(kBitLcdPower, true);
  stageOutput(kBitLcdReset, true);
  stageOutput(kBitGrovePower, false);
  stageOutput(kBitTouchReset, false);
  // The GNSS reset line. Polarity, measured on this board: bit 9 HIGH holds the L76K in reset, LOW
  // lets it run (the line has stayed low since the boot sequence and the module talks, so low cannot
  // be "reset"). The power rail (bit 13) is set high above and never brought low anywhere in this
  // file, so a soft restart leaves the module powered: the PCA9554 keeps its latched outputs while
  // the SoC restarts.
  // EST-387: until now the line was asserted (high) on EVERY boot and released below, so every flash
  // and every crash restart restarted the receiver and threw away its RTC, almanac and ephemeris: a
  // cold start each time, indoors with one satellite. Now it is asserted only when the SoC really
  // lost power (gps_boot.h holds the table). Staging the level the line already has is not an edge,
  // so on a warm boot the receiver does not notice the restart at all. Do not "fix" this by dropping
  // bit 13 and raising it again: a cold module starts slower and may need a full almanac download.
  stageOutput(kBitGnssReset, resetReceiver);
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
  if (resetReceiver && !setOutput(kBitGnssReset, false)) return false;
  delay(40);
  // EST-387: the two boot lines the ticket names, printed where the decision was made. A kept
  // receiver is the fix; a reset one names why, so the log says why the clock may start cold.
  if (resetReceiver) {
    Serial.printf("[gps] boot: reset (%s)\n",
                  reasonResets ? gpsBootReasonToken(bootReason) : "left asserted by the last boot");
  } else {
    Serial.printf("[gps] boot: kept running (reset reason %d)\n", (int)esp_reset_reason());
  }
  s_gnssResetThisBoot = resetReceiver;
  if (!setOutput(kBitLcdReset, false)) return false;
  delay(10);
  if (!setOutput(kBitLcdReset, true)) return false;
  delay(500);
  if (!setOutput(kBitLcdControl, true)) return false;
  delay(10);
  if (!setOutput(kBitTouchReset, true)) return false;
  delay(60);

  // EST-397: the one s_expanderReady = true (the earlier one, after the rails are latched). A second
  // assignment here was a harmless duplicate that hid which one the reset pulses depend on.
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

// EST-390: the pressed look's one primitive. Same order rule as omFramePixel(): read the stored word
// as native, blend with ui::pressBlend (the host-tested arithmetic), store it back the way this panel
// keeps it. Clamped to the frame, so a rectangle at an edge (a tab key, the bottom keyboard row) can
// never write outside the buffer.
void omFrameBlendRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t native, uint8_t alpha) {
  if (s_frame.getBuffer() == nullptr || w <= 0 || h <= 0) return;
  const int16_t fw = (int16_t)s_frame.width();
  const int16_t fh = (int16_t)s_frame.height();
  if (x < 0) { w = (int16_t)(w + x); x = 0; }
  if (y < 0) { h = (int16_t)(h + y); y = 0; }
  if (x >= fw || y >= fh) return;
  if ((int)x + w > fw) w = (int16_t)(fw - x);
  if ((int)y + h > fh) h = (int16_t)(fh - y);
  if (w <= 0 || h <= 0) return;
  uint16_t* px = (uint16_t*)s_frame.getBuffer();
  for (int row = 0; row < h; ++row) {
    uint16_t* line = px + (size_t)(y + row) * (size_t)fw + (size_t)x;
    for (int col = 0; col < w; ++col) {
      const uint16_t stored = line[col];
      const uint16_t bg = s_frameNeedsSwap ? ui::rgb565FrameRead(stored) : stored;
      const uint16_t out = ui::pressBlend(native, bg, alpha);
      line[col] = s_frameNeedsSwap ? ui::rgb565FrameStore(out) : out;
    }
  }
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
// address (an AXP2101 answers 0x4A in register 0x03), 0x14 as the remaining unknown. The point is
// that the next boot log can answer "what is 0x34" instead of guessing it. No charge decision is
// made here: a battery voltage trend is not a charge signal.
//
// EST-376: 0x22 is NOT probed at boot any more. It is the USB-C port controller (AW35615,
// FUSB302-compatible) and 0x00/0x06 are meaningless to it; the read-only `pmu probe` command reads
// the FUSB302 map instead (diag_logic.h holds the registers that are safe to read).
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
  // EST-401: the one write to the USB-C controller, on the re-owned bus and before anything else
  // talks to 0x22. Step 1 of the charging bolt: without it STATUS0's VBUSOK is dead and the battery
  // log's `vbus` column would say "no cable" forever. It never reports a bolt.
  usbPdPowerBegin();
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
  // EST-380: the file budget. The Arduino/IDF default is 5 handles, and the map alone keeps five
  // open for good (main pack 1 + z16 idx+pack + z17 idx+pack), so after the first zoom to z17 every
  // later open, exists() and history append failed. The count lives in sd_budget.h with a
  // static_assert, and this line prints it at boot so the number in the image is visible:
  //   map main pack 1 + overlay 2 levels x 2 files = 4 + history pool 4 + battery log 1 +
  //   transient 6 (exists probe, size probe, map index read, tail reader, directory listing,
  //   screenshot 0) + margin 2 = 18.
  // The limit is not free: with CONFIG_FATFS_PER_FILE_CACHE=1 every FATFS file carries a
  // FF_MAX_SS (4096 B) sector buffer, so one slot is about 4,136 B and 18 slots about 80 KB. IDF
  // allocates that table in ONE ff_memalloc() and this build has CONFIG_FATFS_ALLOC_PREFER_EXTRAM,
  // so it is PSRAM first (measured in the libfatfs object: heap_caps_malloc_prefer with
  // MALLOC_CAP_SPIRAM first, then internal). The [mem] boot lines around this call show the real
  // internal/PSRAM split on the device.
  Serial.printf("[sd] open-file budget %d = map %d + overlays %d + history %d + power log %d + "
                "transient %d + margin %d\n",
                sd_budget::kMaxOpenFiles, sd_budget::kMapMainPack, sd_budget::kMapOverlayFiles,
                sd_budget::kHistoryPoolFiles, sd_budget::kPowerLogFiles,
                sd_budget::kTransientFiles, sd_budget::kMargin);
  // 20 MHz, 1-bit: CLK/CMD/D0 are ordinary GPIOs through the matrix, not an IOMUX SD pad group.
  return SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, sd_budget::kBeginMaxOpenFiles);
}

void sdUnmount() {
  SD_MMC.end();
  (void)setOutput(kBitSdPower, false);
}

// EST-377: the latched expander output, not a diagnostic. No code path drops the GNSS rail today
// (see the bring-up note above), so this reads 1; it becomes a real switch the day GPS duty cycling
// lands, and the battery log follows it without a second source of truth.
bool gpsRailOn() {
  return (s_output[kBitGnssPower >> 3] & (uint8_t)(1U << (kBitGnssPower & 7))) != 0;
}

// ---------------------------------------------------------------------------- battery

// ADS1115 at 0x48, single-ended AIN0..AIN3, single shot, 128 SPS. The config byte is the one the
// battery read has always sent (OS=1, PGA=000, MODE=1); only the MUX field changes with the
// channel (AIN0-GND = 0xC3, AIN1 = 0xD3, AIN2 = 0xE3, AIN3 = 0xF3). The board divides the cell
// voltage by 2, so the cell is twice what AIN0 sees; that doubling happens in batteryMilliVolts()
// only, so every reading here is the ADC pin's own mV and the four are directly comparable.
// Sense (expander bit 15) is switched per read, exactly as the battery read always did.
static bool adsReadChannel(uint8_t channel, uint16_t* raw) {
  if (raw == nullptr || channel > 3 || !s_expanderReady) return false;
  if (!setOutput(kBitBatterySense, true)) return false;
  delay(5);
  Wire.beginTransmission(0x48);
  Wire.write(0x01);   // config
  Wire.write((uint8_t)(0xC3 | (uint8_t)(channel << 4)));   // OS=1, MUX=AINn-GND, single shot
  Wire.write(0x83);
  bool ok = Wire.endTransmission() == 0;
  uint16_t value = 0;
  if (ok) {
    delay(12);
    Wire.beginTransmission(0x48);
    Wire.write(0x00);   // conversion
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x48, 2) == 2) {
      // EST-397: two locals, then combine. In `(Wire.read() << 8) | Wire.read()` the two calls are
      // unsequenced (the operands of | are), so a compiler that evaluates the right one first swaps
      // the ADS1115's high and low byte and the battery reads a garbled milli-voltage. GCC orders
      // them left to right today; nothing in the code or a test pinned that.
      const uint8_t hi = (uint8_t)Wire.read();
      const uint8_t lo = (uint8_t)Wire.read();
      value = (uint16_t)(((uint16_t)hi << 8) | lo);
    } else {
      ok = false;
    }
  }
  (void)setOutput(kBitBatterySense, false);
  if (!ok) return false;
  *raw = value;
  return true;
}

// The one scale the battery read has always used. Kept identical so `pmu probe`'s AIN0 is the
// number the battery path sees (the config's PGA field is 000, i.e. +/-6.144 V; the divisor is the
// existing 4.096 V one, deliberately not changed here: a battery percentage must not move).
static uint16_t adsPinMilliVolts(uint16_t raw) {
  const float volts = (float)(int16_t)raw * 4.096f / 32767.0f;
  const float mv = volts * 1000.0f;
  return mv <= 0.0f ? 0 : (uint16_t)mv;
}

uint16_t batteryMilliVolts() {
  uint16_t raw = 0;
  if (!adsReadChannel(0, &raw)) return 0;
  if (!raw) return 0;
  // EST-397: ONE scale. The board divides the cell voltage by 2, so the cell is twice the pin; the
  // ADC constant lives in adsPinMilliVolts() only, so a future calibration cannot be made twice.
  return (uint16_t)(adsPinMilliVolts(raw) * 2u);
}

// EST-376: the four single-ended inputs once each, AIN1, AIN2, AIN3 and AIN0 LAST, so the chip is
// left exactly as a battery read leaves it (AIN0's conversion in the register, sense rail off).
void adcProbe(AdcProbe& out) {
  out = AdcProbe();
  const uint8_t order[4] = {1, 2, 3, 0};
  for (int i = 0; i < 4; ++i) {
    const uint8_t ch = order[i];
    uint16_t raw = 0;
    out.ok[ch] = adsReadChannel(ch, &raw);
    out.raw[ch] = raw;
    out.mv[ch] = out.ok[ch] ? adsPinMilliVolts(raw) : 0;
  }
}

// EST-401: AIN1..AIN3 for the battery log's once-a-minute line. Same scale as the battery read, so
// the numbers go straight into the CSV; a failed channel stays 0 and the log says so.
void adcAuxProbe(AdcAux& out) {
  out = AdcAux();
  for (uint8_t i = 0; i < 3; ++i) {
    uint16_t raw = 0;
    out.ok[i] = adsReadChannel((uint8_t)(i + 1), &raw);
    out.mv[i] = out.ok[i] ? adsPinMilliVolts(raw) : 0;
  }
}

// ---------------------------------------------------------------------------- USB-C port controller

// EST-376: READ ONLY. The register pointer is sent (that is how an I2C register read addresses a
// byte) and exactly one byte is read back; no data byte is ever written here.
bool usbPdReadRegister(uint8_t reg, uint8_t* out) {
  if (out == nullptr) return false;
  Wire.beginTransmission(diag::kUsbPdAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)diag::kUsbPdAddr, 1) != 1) return false;
  *out = (uint8_t)Wire.read();
  return true;
}

// EST-401: the one and only data write this firmware sends to 0x22. Static on purpose: it cannot be
// reached from another file, so `test/host/test_est401.py` can prove that the single two-byte
// transmission to this address writes POWER and nothing else. SWITCHES0 (0x02) must never be
// written: its default pull-downs are what make the board a USB sink and let it charge.
static bool usbPdWritePower(uint8_t value) {
  Wire.beginTransmission(diag::kUsbPdAddr);
  Wire.write(diag::kRegPower);   // 0x0B POWER
  Wire.write(value);             // read-back | PWR0 | PWR1 | PWR2 (diag_logic.h)
  return Wire.endTransmission() == 0;
}

// EST-401: one attempt. Reads POWER first, so the byte written is the chip's own value plus the three
// measure bits, then reads it back: "accepted" is never claimed from the write's return code alone.
// `why` names the failure for the boot line.
static bool usbPdMeasureAttempt(uint8_t* readBack, char* why, size_t whyCap) {
  *readBack = 0;
  uint8_t power = 0;
  if (!usbPdReadRegister(diag::kRegPower, &power)) {
    snprintf(why, whyCap, "no answer at 0x22");
    return false;
  }
  if (!diag::powerReadSane(power)) {
    // A byte with 7:4 set is not a POWER value this build understands: fail closed, write nothing.
    snprintf(why, whyCap, "POWER read 0x%02X, not written back", power);
    return false;
  }
  const uint8_t want = diag::powerValueToWrite(power);
  if (!usbPdWritePower(want)) {
    snprintf(why, whyCap, "write refused (read=0x%02X want=0x%02X)", power, want);
    return false;
  }
  if (!usbPdReadRegister(diag::kRegPower, readBack)) {
    snprintf(why, whyCap, "no read-back after the write");
    return false;
  }
  if (!diag::powerBitsPresent(*readBack)) {
    snprintf(why, whyCap, "read-back 0x%02X, want 0x%02X", *readBack, want);
    return false;
  }
  return true;
}

void usbPdPowerBegin() {
  uint8_t readBack = 0;
  char why[64];
  if (usbPdMeasureAttempt(&readBack, why, sizeof(why))) {
    Serial.printf("[pmu] measure block on (POWER=0x%02X)\n", readBack);
    return;
  }
  // One more try: a chip that shares the 3.3 V rail can miss the first exchange right after boot.
  Serial.printf("[pmu] measure block not ready (%s), retrying\n", why);
  delay(20);
  if (usbPdMeasureAttempt(&readBack, why, sizeof(why))) {
    Serial.printf("[pmu] measure block on (POWER=0x%02X)\n", readBack);
    return;
  }
  Serial.printf("[pmu] measure block FAILED (%s)\n", why);
}

bool usbPdStatus0(uint8_t* out) { return usbPdReadRegister(diag::kRegStatus0, out); }

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

// EST-383: the `gps raw` window and the no-fix time gate. The gate compares the module's own
// consecutive RMC/ZDA sentences; the window only tallies types and prints the safe fields.
static GpsRawWindow s_gpsRaw;
static NoFixTimeGate s_noFixTime;

// EST-387: one line each, printed once per boot, so the log says what the boot line could not: whether
// the receiver's one init command was re-armed, and what the aid switch is doing.
static bool s_gpsInitLogged = false;
static bool s_aidLogged = false;

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
  // The no-fix chain starts over (a time before a restart is not a consecutive sentence).
  s_noFixTime.reset();
  // EST-387: the one init command is re-armed only when the receiver was really restarted. A warm
  // boot left the L76K running with its constellation mode in force, so re-sending `$PCAS04,7` would
  // be a command to a receiver that is already warm, for no gain: we do not know whether the L76K
  // restarts its engine on a mode change (nothing in this repo proves it either way, and the module
  // never acknowledges the command), so the one state we can keep for free is "send nothing at all".
  // `$PCAS04,7` itself is the only init command this firmware has: EST-338's `$PCAS03` GSV probe is
  // long gone, so there is no harmless-list to resend.
  if (s_gnssResetThisBoot) {
    s_gpsMode.onModuleReboot();
    if (!s_gpsInitLogged) Serial.println("[gps] init: mode 7 armed (receiver was reset)");
  } else {
    // The receiver never stopped: mark its one init command as already applied, so the first NMEA
    // sentence does NOT trigger it (a fresh GpsModeCommand would otherwise send it at boot anyway).
    s_gpsMode.onModuleWarmStart();
    if (!s_gpsInitLogged) Serial.println("[gps] init: mode 7 kept (receiver kept running)");
  }
  s_gpsInitLogged = true;
  // One line so a log reader knows what the aid switch is doing. It is a compile-time constant; see
  // gps_aid.h for why it is off (the AID-INI field table is unverified, and the module's ACK is what
  // settles it: the device line reads `[gps] aid time=ok|nak|no-answer ...` once it is on).
  if (!s_aidLogged) {
    Serial.println(om::aid::kEnabled ? "[gps] aid: armed (kEnabled=true)"
                                     : "[gps] aid: off (kEnabled=false)");
  }
  s_aidLogged = true;
}

// EST-383: the `gps raw` window. It is started by the USB command and runs from gpsPoll(), so no
// command ever blocks the loop for up to the window's 120 s.
bool gpsRawStart(int seconds, uint32_t nowMs) {
  // The command's reply line is the start marker; nothing extra is printed here.
  return s_gpsRaw.start(seconds, nowMs);
}

bool gpsRawActive() { return s_gpsRaw.active(); }

int gpsRawSecondsLeft(uint32_t nowMs) { return s_gpsRaw.secondsLeft(nowMs); }

// ---------------------------------------------------------------------------- EST-387: aiding
// The state the ticket's log line needs and nothing else: at most one frame in flight, its answer (or
// the timeout) printed once, and `pos=sent|none` naming whether the own position was in it.
static om::aid::CasicReader s_casic;
static om::aid::AidGate s_aidGate;
static bool s_aidPending = false;
static bool s_aidPosSent = false;
static TimeSource s_aidSource = TimeSource::None;
static uint32_t s_aidSentMs = 0;

static void aidLog(const char* answer) {
  // Rule 9: tokens only. `src=` names the clock source that triggered the frame, never a value.
  Serial.printf("[gps] aid time=%s pos=%s src=%s\n", answer, s_aidPosSent ? "sent" : "none",
                timeSourceName(s_aidSource));
  s_aidPending = false;
}

// One CASIC frame finished on the UART. Only the answer to our AID-INI matters; anything else the
// receiver sends in binary is ignored, and none of it reaches the NMEA parser (that is the point of
// consuming the frame here).
static void aidOnFrame() {
  if (!s_casic.answersFor(om::aid::kAidClass, om::aid::kAidIniId)) return;
  if (!s_aidPending) return;   // a late answer for a frame already reported: never a second line
  aidLog(s_casic.isNak() ? "nak" : "ok");
}

// Once per gpsPoll(): the answer timeout first, then the send decision.
static void aidPoll(const GpsState& gps) {
  const uint32_t now = millis();
  if (s_aidPending) {
    if (now - s_aidSentMs >= om::aid::kAnswerTimeoutMs) aidLog("no-answer");
    return;
  }
  if (gps.bytes == 0) return;   // a receiver we have never heard from cannot be aided
  const TimeKeeper& tk = timeKeeper();
  if (!s_aidGate.shouldSend(tk.realSinceBoot(), tk.source(), tk.now(), gps.valid)) return;
  om::aid::AidTime t;
  if (!om::aid::aidTimeFromEpoch(tk.now(), t)) return;
  // The own position, when there is one (Fixed or the last fix): the same point the node advertises.
  const PositionFix& own = posOwn();
  const bool withPos = own.valid;
  uint8_t payload[om::aid::kAidIniBytes];
  const int payloadLen = om::aid::aidIniPayload(t, /*timeValid=*/true, withPos, own.lat, own.lon,
                                                /*altMeters=*/0.0f, payload, (int)sizeof(payload));
  if (payloadLen != om::aid::kAidIniBytes) return;
  uint8_t frame[om::aid::kMaxFrame];
  const int len = om::aid::casicFrame(om::aid::kAidClass, om::aid::kAidIniId, payload, payloadLen,
                                      frame, (int)sizeof(frame));
  if (len <= 0) return;
  s_aidGate.onSent(tk.source());
  s_aidSource = tk.source();
  s_aidPosSent = withPos;
  s_aidSentMs = millis();
  s_aidPending = true;
  Serial1.write(frame, (size_t)len);
}

void gpsPoll(GpsState& out) {
  while (Serial1.available()) {
    const uint8_t byte = (uint8_t)Serial1.read();
    ++out.bytes;
    // EST-387: the receiver's binary answers (a CASIC ACK/NAK to an aid frame) share this UART with
    // NMEA. With aiding switched off nothing is read here, so the byte stream is exactly what it was.
    if (om::aid::kEnabled) {
      const om::aid::CasicReader::Step step = s_casic.feed(byte);
      if (step == om::aid::CasicReader::Step::Complete) {
        aidOnFrame();
        continue;
      }
      if (step == om::aid::CasicReader::Step::Taken) continue;
    }
    const char c = (char)byte;
    if (c == '\r') continue;
    if (c == '\n') {
      ++out.sentences;
      s_nmea[s_nmeaLen] = 0;
      s_nmeaLen = 0;
      const bool checksumOk = gpsNmeaChecksumOk(s_nmea);
      // EST-383: the raw window sees every complete line, including a bad-checksum one (that is a
      // fact worth knowing), but only the four named types are printed, and only their safe fields.
      if (s_gpsRaw.active()) {
        s_gpsRaw.noteSentence(s_nmea, checksumOk);
        char safe[96];
        if (gpsRawFormat(s_nmea, safe, sizeof(safe))) {
          Serial.printf("[gpsraw] t=%lus %s%s\n", (unsigned long)s_gpsRaw.elapsedSec(millis()), safe,
                        checksumOk ? "" : " badsum=1");
        }
      }
      if (!checksumOk) {
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
          // EST-397: the fix's freshness stamp. Only a GGA with quality > 0 refreshes it, so a module
          // that stops talking lets gpsExpireFix() drop the fix 5 s later instead of advertising the
          // last point for ever.
          if (out.valid) out.lastFixMs = millis();
        }
      } else if (strlen(s_nmea) >= 6 && strncmp(s_nmea + 3, "GSV", 3) == 0) {
        // Satellites EXPECTED (almanac "in view") and HEARD (a C/N0 above zero), per constellation:
        // the only way to tell "hears nothing" from "hears plenty, too weakly for a fix".
        gpsParseGsv(s_nmea, out, millis());
      } else if (strlen(s_nmea) >= 6 &&
                 (strncmp(s_nmea + 3, "RMC", 3) == 0 || strncmp(s_nmea + 3, "ZDA", 3) == 0)) {
        // Time (and date) from RMC/ZDA. The parser checks the checksum and the date range, so the
        // module's default date before its first fix never reaches the clock. EST-383: RMC status A
        // is trusted as a fix. RMC V and ZDA are offered to the no-fix gate, which also requires two
        // consecutive advancing sentences and at least one satellite with a signal.
        const ParsedTime t = parseNmeaTime(s_nmea, /*allowNoFix=*/true);
        if (t.ok) {
          const bool noFixAccepted = s_noFixTime.offer(t.epoch, out.heardTotal > 0);
          if (t.hasFix) {
            out.timeEpoch = t.epoch;
            out.timeEpochFix = true;
          } else if (noFixAccepted) {
            out.timeEpoch = t.epoch;
            out.timeEpochFix = false;
          }
        }
      }
      continue;
    }
    if (s_nmeaLen < sizeof(s_nmea) - 1) {
      s_nmea[s_nmeaLen++] = c;
    } else {
      // EST-397: a line longer than the buffer is dropped whole (the newline that follows resets the
      // parser). Counted, so a module in the wrong mode or a run of binary bytes can be told apart
      // from radio noise, which lands in checksumBad.
      ++out.lineOverflow;
      s_nmeaLen = 0;   // the rest of this line is not a sentence we can parse
    }
  }
  // EST-397: the fix expires AFTER the parser ran, so a fresh GGA in this poll always wins.
  gpsExpireFix(out, millis());
  // Once per poll, with no data needed: the 60 s best-C/N0 window has to expire even when the module
  // has gone quiet (a stale "best" from the last satellite heard would otherwise stay for ever).
  gpsSkyRefresh(out, millis());

  // EST-387: the aid frame's one send and its answer. With aiding switched off this is a no-op.
  if (om::aid::kEnabled) aidPoll(out);

  // EST-383: one summary when the `gps raw` window is over, even if the module sent nothing.
  if (s_gpsRaw.finished(millis())) {
    char types[200];
    int at = 0;
    types[0] = 0;
    for (int i = 0; i < s_gpsRaw.typeCount(); ++i) {
      const int n = snprintf(types + at, sizeof(types) - (size_t)at, "%s%s:%lu",
                             i > 0 ? "," : "", s_gpsRaw.typeAt(i),
                             (unsigned long)s_gpsRaw.countAt(i));
      if (n <= 0 || (size_t)n >= sizeof(types) - (size_t)at) break;
      at += n;
    }
    // The final line is the command's answer (tools/cmd.py stops on it), so the data lines above it
    // are all printed first, exactly like `nodes`. The immediate reply to the command is a plain
    // `[gpsraw] start` line, which the host tool correctly does not mistake for an answer.
    Serial.printf(
        "[cmd] ok gps raw secs=%d types=\"%s\" zda=%d bad=%lu dropped=%d\n", s_gpsRaw.seconds(),
        types[0] != 0 ? types : "none", s_gpsRaw.sawZda() ? 1 : 0,
        (unsigned long)s_gpsRaw.badSum(), s_gpsRaw.droppedTypes());
    s_gpsRaw.stop();
  }
}

bool setAudioPa(bool on) { return setOutput(kBitAudioPa, on); }

}  // namespace om
