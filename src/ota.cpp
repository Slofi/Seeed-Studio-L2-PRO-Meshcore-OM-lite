// SPDX-License-Identifier: MIT
// OM Lite USB OTA — update the firmware over the USB CDC port, no BOOT/RESET dance.
//
// Host -> device: "OMOTA1" (6) + size u32 LE + md5 (16 raw) + <size> image bytes.
// The image is written with Update.h and the device reboots itself on success. The magic is only
// looked for at the start of a transfer, so ordinary serial logs are unaffected.
#include "ota.h"

#include <Update.h>

namespace om {
namespace {

constexpr char kMagic[6] = {'O', 'M', 'O', 'T', 'A', '1'};
bool s_active = false;

bool readExact(uint8_t* dst, size_t len, uint32_t timeoutMs) {
  const uint32_t start = millis();
  size_t got = 0;
  while (got < len) {
    const int n = Serial.readBytes(dst + got, len - got);
    if (n > 0) {
      got += (size_t)n;
      continue;
    }
    if (millis() - start > timeoutMs) return false;
    delay(5);
  }
  return true;
}

}  // namespace

bool otaActive() { return s_active; }

void otaPoll() {
  if (s_active) return;

  // Scan for the magic ONE BYTE AT A TIME. The old version read six bytes and threw them away when
  // they did not match, which leaves 1-5 bytes behind on a partial write - and since the reader then
  // always takes six, the stream stays misaligned for ever and no later upload is ever recognised
  // (observed: the device printed heartbeats and ignored every header until it was reset).
  // A prefix state machine recovers from any garbage, including that residue.
  static uint8_t match = 0;
  bool gotMagic = false;
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    if ((char)c == kMagic[match]) {
      if (++match < 6) continue;
      match = 0;
      gotMagic = true;
      break;
    }
    match = ((char)c == kMagic[0]) ? 1 : 0;
  }
  if (!gotMagic) return;   // nothing (or only part of a magic) arrived: never start an upload

  s_active = true;
  Serial.println();
  Serial.println("[ota] upload started");

  uint8_t hdr[4];
  if (!readExact(hdr, 4, 1500)) {
    Serial.println("[ota] no size");
    s_active = false;
    return;
  }
  const uint32_t size = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16) |
                        ((uint32_t)hdr[3] << 24);
  uint8_t md5[16];
  if (!readExact(md5, 16, 1500)) {
    Serial.println("[ota] no md5");
    s_active = false;
    return;
  }
  char md5hex[33];
  for (int i = 0; i < 16; ++i) snprintf(md5hex + i * 2, 3, "%02x", md5[i]);

  Serial.printf("[ota] %u bytes, md5 %s\n", (unsigned)size, md5hex);
  if (!Update.begin(size)) {
    Serial.printf("[ota] begin failed: %s\n", Update.errorString());
    s_active = false;
    return;
  }
  Update.setMD5(md5hex);

  // The OTA slot erase takes seconds; without flow control the host overruns the 256-byte CDC
  // FIFO and the image is lost. One ACK per chunk: the host sends the next chunk only after it.
  Serial.println("[ota] ready");
  uint8_t buf[1024];
  uint32_t left = size;
  while (left > 0) {
    const size_t want = left < sizeof(buf) ? left : sizeof(buf);
    if (!readExact(buf, want, 8000)) {
      Serial.println("[ota] timeout");
      Update.abort();
      s_active = false;
      return;
    }
    if (Update.write(buf, want) != want) {
      Serial.printf("[ota] write failed: %s\n", Update.errorString());
      Update.abort();
      s_active = false;
      return;
    }
    left -= (uint32_t)want;
    Serial.write((uint8_t)0x06);   // ACK
  }

  if (!Update.end()) {
    Serial.printf("[ota] end failed: %s\n", Update.errorString());
    s_active = false;
    return;
  }
  Serial.printf("[ota] ok (%u bytes), rebooting\n", (unsigned)size);
  Serial.flush();
  delay(300);
  ESP.restart();
}

}  // namespace om
