// SPDX-License-Identifier: MIT
// OM Lite audio — ES8311 codec + class-D amp on the Wio Tracker L2 Pro.
//
// Register values are the ES8311 datasheet/vendor bring-up for MCLK = 256 x fs, codec in SLAVE
// mode (the ESP32-S3 drives BCLK/WS/MCLK), 16-bit standard I2S. They are hardware facts checked
// against the board's reference firmware; the structure and code here are ours.
#include "audio.h"

#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

#include "board.h"

namespace om {
namespace {

constexpr i2s_port_t kPort = I2S_NUM_0;
constexpr int kRate = 16000;          // in the ES8311 rate table; MCLK = 256 * fs
constexpr uint8_t kAddr = 0x18;
constexpr uint32_t kAmpSettleMs = 250;

bool s_open = false;
bool s_i2s = false;

int s_writes = 0;
int s_writeFails = 0;

bool w(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(kAddr);
  Wire.write(reg);
  Wire.write(val);
  const uint8_t err = Wire.endTransmission();
  ++s_writes;
  if (err != 0) {
    ++s_writeFails;
    Serial.printf("[codec] write %02X=%02X failed err=%u\n", reg, val, err);
  }
  return err == 0;
}

bool codecBegin() {
  bool ok = true;
  // The first write after the codec rail comes up is expected to NACK (measured err=2 on this
  // board): the double write is deliberate, so the first result must NOT poison `ok`.
  (void)w(0x44, 0x08);
  ok &= w(0x44, 0x08);
  ok &= w(0x01, 0x30);
  ok &= w(0x02, 0x00);
  ok &= w(0x03, 0x10);
  ok &= w(0x16, 0x24);
  ok &= w(0x04, 0x10);
  ok &= w(0x05, 0x00);
  ok &= w(0x0B, 0x00);
  ok &= w(0x0C, 0x00);
  ok &= w(0x10, 0x1F);
  ok &= w(0x11, 0x7F);
  ok &= w(0x00, 0x80);   // out of reset, slave mode
  ok &= w(0x01, 0x3F);   // external MCLK, not inverted
  ok &= w(0x13, 0x10);
  ok &= w(0x1B, 0x0A);
  ok &= w(0x1C, 0x6A);
  ok &= w(0x44, 0x58);
  return ok;
}

bool codecStart() {
  bool ok = true;
  ok &= w(0x00, 0x80);
  ok &= w(0x01, 0x3F);
  ok &= w(0x09, 0x0C);   // standard I2S, 16-bit
  ok &= w(0x0A, 0x0C);
  ok &= w(0x02, 0x00);   // pre_div = pre_multi = 1
  ok &= w(0x05, 0x00);   // adc_div = dac_div = 1
  ok &= w(0x03, 0x10);   // single speed, adc_osr 0x10
  ok &= w(0x04, 0x20);   // dac_osr 0x20 at 16 kHz
  ok &= w(0x07, 0x00);   // lrck_h
  ok &= w(0x08, 0xFF);   // lrck_l
  ok &= w(0x06, 0x03);   // bclk_div = 4
  ok &= w(0x17, 0xBF);   // DAC path up, ADC/mic stays down
  ok &= w(0x0E, 0x02);
  ok &= w(0x12, 0x00);
  ok &= w(0x14, 0x1A);
  ok &= w(0x0D, 0x01);
  ok &= w(0x15, 0x40);
  ok &= w(0x37, 0x08);
  ok &= w(0x45, 0x00);
  return ok;
}

bool i2sInstall() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  esp_err_t err = i2s_driver_install(kPort, &cfg, 0, nullptr);
  if (err != ESP_OK) {
    Serial.printf("[audio] i2s install err=%d\n", (int)err);
    return false;
  }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = 10;
  pins.bck_io_num = 11;
  pins.ws_io_num = 12;
  pins.data_out_num = 16;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  err = i2s_set_pin(kPort, &pins);
  if (err != ESP_OK) {
    Serial.printf("[audio] i2s pins err=%d\n", (int)err);
    i2s_driver_uninstall(kPort);
    return false;
  }
  s_i2s = true;
  return true;
}

// 16-bit sine with a short fade in/out so it does not click. Loudness is the codec register.
void playTone(int freq, int ms) {
  const int total = (kRate * ms) / 1000;
  const int fade = total / 8 > 0 ? total / 8 : 1;
  const float step = 2.0f * (float)M_PI * (float)freq / (float)kRate;
  int16_t buf[128];
  float phase = 0.0f;
  int done = 0;
  while (done < total) {
    int n = 0;
    for (; n < 128 && done < total; ++n, ++done) {
      float amp = 22000.0f;
      if (done < fade) amp *= (float)done / fade;
      else if (done > total - fade) amp *= (float)(total - done) / fade;
      buf[n] = (int16_t)(sinf(phase) * amp);
      phase += step;
      if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
    }
    size_t written = 0;
    i2s_write(kPort, buf, (size_t)n * sizeof(int16_t), &written, pdMS_TO_TICKS(200));
  }
}

void codecMute(bool mute) {
  uint8_t v = 0;
  Wire.beginTransmission(kAddr);
  Wire.write(0x31);
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom((int)kAddr, 1) == 1) v = (uint8_t)Wire.read();
  v = (uint8_t)(v & 0x9F);
  w(0x31, mute ? (uint8_t)(v | 0x60) : v);
}

}  // namespace

bool audioBegin() {
  setAudioPa(false);
  delay(10);
  // The codec must answer at 0x18; the I2C scan already proved the bus, this proves the chip.
  Wire.beginTransmission(kAddr);
  const uint8_t probe = Wire.endTransmission();
  Serial.printf("[audio] codec probe: %s (err=%u)\n", probe == 0 ? "ack" : "NO ACK", probe);
  if (probe != 0) return false;
  const bool began = codecBegin();
  Serial.printf("[audio] codec init: %s (%d/%d writes failed)\n", began ? "ok" : "FAILED",
                s_writeFails, s_writes);
  if (!began) return false;
  s_open = true;
  if (!i2sInstall()) return false;
  const bool started = codecStart();
  Serial.printf("[audio] codec start: %s (%d/%d writes failed)\n", started ? "ok" : "FAILED",
                s_writeFails, s_writes);
  if (!started) return false;
  audioVolume(60);
  codecMute(false);
  setAudioPa(true);
  delay(kAmpSettleMs);
  return true;
}

void audioVolume(uint8_t pct) {
  if (!s_open) return;
  if (pct > 100) pct = 100;
  if (pct == 0) { w(0x32, 0x00); return; }
  constexpr uint8_t kFloor = 0x60;
  w(0x32, (uint8_t)(kFloor + ((uint32_t)(0xFF - kFloor) * pct) / 100));
}

void audioTone(uint16_t freq, uint16_t ms) {
  if (!s_open || !s_i2s) return;
  playTone(freq, ms);
}

void audioStandby(bool standby) {
  if (!s_open) return;
  if (standby) {
    codecMute(true);
    setAudioPa(false);
  } else {
    codecMute(false);
    setAudioPa(true);
    delay(kAmpSettleMs);
  }
}

}  // namespace om
