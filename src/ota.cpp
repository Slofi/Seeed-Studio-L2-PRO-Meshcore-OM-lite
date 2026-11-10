// SPDX-License-Identifier: MIT
// OM Lite USB commands on the CDC port: OTA, and a screenshot so the screen can be checked without a
// photo.
//
// Host -> device, OTA: "OMOTA1" (6) + size u32 LE + md5 (16 raw) + <size> image bytes.
// The image is written with Update.h and the device reboots itself on success.
// Host -> device, screenshot: "OMSHOT" (6). Device answers "OMSHOT1 <w> <h> <bytes>\n" and then the
// off-screen frame as native RGB565 little-endian, row by row from the top left; or "OMSHOT0 no
// frame\n" when there is no frame buffer. See tools/shot.py.
// Host -> device, test command (EST-343): "OMCMD1 <len>\n" then <len> bytes of UTF-8 text. The answer
// is one line "[cmd] ok ..." or "[cmd] error <reason>"; `nodes` and `msgs` print their data lines
// first. See tools/cmd.py.
// The magics are only looked for at the start of a transfer, so ordinary serial logs are unaffected.
#include "ota.h"

#include <Update.h>
#include <stdio.h>   // snprintf: the command slot's copy

#include "board.h"
#include "usb_cmd.h"
#include "usb_frames.h"

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

// The one six-byte prefix matcher (usb_frames.h). The OTA and screenshot machines are the same ones
// ota.cpp has always used; a third machine (OMCMD1) is fed the identical byte stream, which is how a
// command is added without altering what the other two do.
using usb::Magic;

bool feed(Magic& m, char c) { return usb::feedMagic(m, c); }

// One completed (or failed) OMCMD1 frame. Data lines for `nodes`/`msgs` are printed by the shell
// before the final "[cmd] ok ..." line. A framing failure still answers with the same shape, so the
// host never waits for a line that cannot come.
//
// EST-350: the handler only COPIES a ready command into the one slot below; it never executes it.
// Running a command here puts the command's own call stack (usbCommandExecute, the history dump, the
// FS, FatFs and the SD driver) on top of the reader's: measured on the ELF, the frame chain to the
// history read was loop() 1,408 B + otaPoll 1,136 + this handler 304 + usbCommandExecute 1,632 +
// meshHistoryDump 384 + HistoryStore::collect 784 + readWindow 144 = 5,792 B BEFORE FatFs (stat 32,
// vfs_fat_stat 368, f_stat 624, find_volume 64, the disk-IO 32s, sdmmc 48/80/80/32, xQueueReceive
// 64, about 1,490 B more) on a loop task whose stack is 8,192 B (cores/esp32/main.cpp). `history
// "ERA-3 (SI)" 5` is where that ended in a panic twice. loop() runs it after otaPoll() has returned:
// loop() 1,408 + otaCommandPoll 304 + usbCommandExecute 1,648 + 384 + 784 + 144 = 4,672 B, i.e.
// 1,120 B of headroom recovered, and the handler's own frame is 32 B instead of 304.
char s_cmd[usb::kMaxCommandBytes + 1];
bool s_cmdReady = false;

void handleCommandFrame(usb::CommandFrame& frame) {
  if (frame.event() == usb::CommandFrame::Event::Ready) {
    if (s_cmdReady) {
      // One slot, so a second command cannot overwrite one that has not run yet. The host gets an
      // answer for it, the same way every other refusal is answered.
      Serial.println("[cmd] error busy");
    } else {
      snprintf(s_cmd, sizeof(s_cmd), "%s", frame.text());
      s_cmdReady = true;
    }
  } else {
    Serial.printf("[cmd] error %s\n", frame.error());
  }
  frame.consume();
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

// EST-350: the command slot's other end. Called by loop() AFTER otaPoll() returns, so everything the
// command touches (the history store's own loop-owned read path, the SD card, the radio) runs with
// the reader's frames off the stack.
void otaCommandPoll() {
  if (!s_cmdReady) return;
  s_cmdReady = false;   // freed before running: a command that takes a while must not block the host
  char reply[256];
  om::usbCommandExecute(s_cmd, reply, sizeof(reply));
  Serial.println(reply);
  s_cmd[0] = 0;
}

void otaPoll() {
  if (s_active) return;

  // Scan for the three magics ONE BYTE AT A TIME. The old version read six bytes and threw them
  // away when they did not match, which leaves 1-5 bytes behind on a partial write - and since the
  // reader then always takes six, the stream stays misaligned for ever and no later upload is ever
  // recognised (observed: the device printed heartbeats and ignored every header until it was reset).
  // A prefix state machine recovers from any garbage, including that residue. The OTA and screenshot
  // machines are the same ones as before; the command machine is a third copy fed the identical
  // bytes, and it never enters a blocking read while idle (the S492 lesson).
  static Magic ota = {kMagic, 0};
  static Magic shot = {kShotMagic, 0};
  static usb::CommandFrame command;
  bool gotMagic = false;
  bool gotShot = false;
  while (Serial.available() > 0) {
    const int ci = Serial.read();
    if (ci < 0) break;
    const char c = (char)ci;

    // A command frame in progress owns every byte until it is complete or has failed, but only for
    // the bytes that are already in the FIFO. When none are left, the loop returns to the mesh,
    // the touch poll and the screen; poll() below is what ends a stalled frame on its deadline.
    if (command.busy()) {
      if (command.feed(c, millis()) != usb::CommandFrame::Event::None) handleCommandFrame(command);
      continue;
    }

    const bool isOta = feed(ota, c);
    const bool isShot = feed(shot, c);
    const bool isCmd = command.scan(c);
    if (isCmd) {
      // The bytes of one magic must not count towards another.
      ota.match = 0;
      shot.match = 0;
      command.begin(millis());
      continue;   // the length line follows in this same stream
    }
    if (isShot && !isOta) {
      ota.match = 0;
      gotShot = true;
      break;
    }
    if (isOta) {
      shot.match = 0;
      gotMagic = true;
      break;
    }
  }
  // A command frame that arrived in pieces, or one whose host vanished, is advanced even when the
  // FIFO is empty: this is the only blocking made impossible by construction.
  if (command.busy() &&
      command.poll(millis()) != usb::CommandFrame::Event::None) {
    handleCommandFrame(command);
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
