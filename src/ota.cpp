// SPDX-License-Identifier: MIT
// OM Lite USB commands on the CDC port: OTA, and a screenshot so the screen can be checked without a
// photo.
//
// Host -> device, OTA: "OMOTA1" (6) + size u32 LE + md5 (16 raw) + <size> image bytes.
// The image is written with Update.h and the device reboots itself on success.
// Host -> device, screenshot: "OMSHOT" (6). Device answers "OMSHOT1 <w> <h> <bytes>\n" and then the
// off-screen frame as native RGB565 little-endian, row by row from the top left; or "OMSHOT0 no
// frame\n" when there is no frame buffer. See tools/shot.py.
// The magics are only looked for at the start of a transfer, so ordinary serial logs are unaffected.
#include "ota.h"

#include <Update.h>

#include "board.h"

namespace om {
namespace {

constexpr char kMagic[6] = {'O', 'M', 'O', 'T', 'A', '1'};
constexpr char kShotMagic[6] = {'O', 'M', 'S', 'H', 'O', 'T'};
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

// One six-byte prefix matcher: exactly the state machine the OTA reader has always used, unchanged.
// It advances on a matching byte and, on a miss, restarts at 1 if this byte could begin the magic
// again, otherwise at 0. Two matchers are fed the same byte stream independently, which is how a
// second command is added without altering what the OTA path does.
struct Magic {
  const char* text;
  uint8_t match;
};

bool feed(Magic& m, char c) {
  if (c == m.text[m.match]) {
    if (++m.match < 6) return false;
    m.match = 0;
    return true;
  }
  m.match = (c == m.text[0]) ? 1 : 0;
  return false;
}

// Send the off-screen frame. In 256-byte chunks, the size of the CDC's TX ring buffer, so a write
// queues one ring at a time and blocks only while that ring drains into USB; no per-chunk ACK from
// the host, so the mesh loop stalls for about the transfer and no longer. The panel's byte order is
// undone in board.cpp, so what goes on the wire is the colour as drawn.
void sendScreenshot() {
  int width = 0, height = 0;
  if (!omFrameSize(width, height)) {
    Serial.println("OMSHOT0 no frame");
    return;
  }
  Serial.printf("OMSHOT1 %d %d %u\n", width, height, (unsigned)((uint32_t)width * height * 2));
  uint8_t chunk[256];
  size_t used = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint16_t px = omFramePixel(x, y);
      chunk[used++] = (uint8_t)(px & 0xFF);
      chunk[used++] = (uint8_t)(px >> 8);
      if (used == sizeof(chunk)) {
        Serial.write(chunk, used);
        used = 0;
      }
    }
  }
  if (used) Serial.write(chunk, used);
  Serial.flush();
}

}  // namespace

bool otaActive() { return s_active; }

void otaPoll() {
  if (s_active) return;

  // Scan for either magic ONE BYTE AT A TIME. The old version read six bytes and threw them away when
  // they did not match, which leaves 1-5 bytes behind on a partial write - and since the reader then
  // always takes six, the stream stays misaligned for ever and no later upload is ever recognised
  // (observed: the device printed heartbeats and ignored every header until it was reset).
  // A prefix state machine recovers from any garbage, including that residue. The OTA machine is the
  // same one as before; the screenshot machine is a second copy fed the identical bytes.
  static Magic ota = {kMagic, 0};
  static Magic shot = {kShotMagic, 0};
  bool gotMagic = false;
  bool gotShot = false;
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    const bool isOta = feed(ota, (char)c);
    const bool isShot = feed(shot, (char)c);
    if (isShot && !isOta) {
      ota.match = 0;   // the bytes of one magic must not count towards the other
      gotShot = true;
      break;
    }
    if (isOta) {
      shot.match = 0;
      gotMagic = true;
      break;
    }
  }
  // A screenshot leaves nothing to read afterwards; anything the host sent later waits in the FIFO for
  // the next poll, so the OTA reader is exactly where it was.
  if (gotShot) {
    sendScreenshot();
    return;
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
