// SPDX-License-Identifier: MIT
// EST-365: the radio/loop model behind `radio` and Settings > About. See radio_stats.h.
#include "radio_stats.h"

#include <string.h>

namespace om {
namespace radio {
namespace {

// --- noise floor window --------------------------------------------------------------------------
int s_noise[kNoiseSamples] = {};
int s_noiseCount = 0;
int s_noiseHead = 0;          // next write
uint32_t s_noiseLastMs = 0;
bool s_noiseEver = false;

// --- reception ring ------------------------------------------------------------------------------
PacketSample s_packets[kPacketSamples];
int s_packetCount = 0;
int s_packetHead = 0;         // next write
uint32_t s_packetsHeard = 0;

// --- loop pass -----------------------------------------------------------------------------------
bool s_passOpen = false;
uint32_t s_passStartUs = 0;
uint32_t s_passPartUs[kPartCount] = {};
uint32_t s_lastPassUs = 0;
uint32_t s_worstUs = 0;
Part s_worstPart = Part::Other;
uint32_t s_worstParts[kPartCount] = {};
uint32_t s_minuteStartMs = 0;
uint32_t s_minuteWorstUs = 0;
Part s_minuteWorstPart = Part::Other;
uint32_t s_lastMinuteWorstUs = 0;   // the last completed minute
Part s_lastMinuteWorstPart = Part::Other;
uint32_t s_histogram[kHistogramBuckets] = {};

Part worstPartOf(const uint32_t* parts) {
  Part best = Part::Other;
  uint32_t bestUs = 0;
  for (int i = 0; i < kPartCount; ++i) {
    if (parts[i] > bestUs) {
      bestUs = parts[i];
      best = (Part)i;
    }
  }
  return best;
}

int histogramIndex(uint32_t us) {
  for (int i = 0; i < kHistogramBuckets - 1; ++i) {
    if (us < kHistogramBoundsUs[i]) return i;
  }
  return kHistogramBuckets - 1;
}

}  // namespace

const char* partName(Part p) {
  switch (p) {
    case Part::History: return "history";
    case Part::Wifi: return "wifi";
    case Part::Ble: return "ble";
    case Part::Gps: return "gps";
    case Part::Usb: return "usb";
    case Part::Mesh: return "mesh";
    case Part::Rptr: return "rptr";
    case Part::MapDecode: return "map";
    case Part::Draw: return "draw";
    case Part::FramePush: return "push";
    case Part::Other:
    case Part::Count:
    default: return "other";
  }
}

const char* payloadTypeName(uint8_t type) {
  switch (type) {
    case 0x00: return "REQ";
    case 0x01: return "RESP";
    case 0x02: return "TXT";
    case 0x03: return "ACK";
    case 0x04: return "ADVERT";
    case 0x05: return "GRP_TXT";
    case 0x06: return "GRP_DATA";
    case 0x07: return "ANON_REQ";
    case 0x08: return "PATH";
    case 0x09: return "TRACE";
    case 0x0A: return "MULTIPART";
    case 0x0B: return "CONTROL";
    case 0x0F: return "RAW";
    default: return "?";
  }
}

const char* routeName(uint8_t route) {
  switch (route) {
    case 0x00: return "tflood";
    case 0x01: return "flood";
    case 0x02: return "direct";
    case 0x03: return "tdirect";
    default: return "?";
  }
}

void begin(uint32_t nowMs) {
  memset(s_noise, 0, sizeof(s_noise));
  s_noiseCount = 0;
  s_noiseHead = 0;
  s_noiseLastMs = nowMs;
  s_noiseEver = false;
  for (int i = 0; i < kPacketSamples; ++i) s_packets[i] = PacketSample();
  s_packetCount = 0;
  s_packetHead = 0;
  s_packetsHeard = 0;
  s_passOpen = false;
  s_passStartUs = 0;
  memset(s_passPartUs, 0, sizeof(s_passPartUs));
  s_lastPassUs = 0;
  s_worstUs = 0;
  s_worstPart = Part::Other;
  memset(s_worstParts, 0, sizeof(s_worstParts));
  s_minuteStartMs = nowMs;
  s_minuteWorstUs = 0;
  s_minuteWorstPart = Part::Other;
  s_lastMinuteWorstUs = 0;
  s_lastMinuteWorstPart = Part::Other;
  memset(s_histogram, 0, sizeof(s_histogram));
}

void noteNoise(int dbm, uint32_t nowMs) {
  // One sample per period: the loop calls this often, the window stays a real three minutes.
  if (s_noiseEver && (uint32_t)(nowMs - s_noiseLastMs) < kNoisePeriodMs) return;
  s_noise[s_noiseHead] = dbm;
  s_noiseHead = (s_noiseHead + 1) % kNoiseSamples;
  if (s_noiseCount < kNoiseSamples) ++s_noiseCount;
  s_noiseLastMs = nowMs;
  s_noiseEver = true;
}

void noteRawPacket(const uint8_t* raw, int len, int rssi, int snr4, uint32_t wall, bool wallReal,
                   uint32_t nowMs) {
  if (raw == nullptr || len <= 0) return;
  ++s_packetsHeard;
  PacketSample& p = s_packets[s_packetHead];
  p = PacketSample();
  p.atMs = nowMs;
  p.atWall = wall;
  p.atWallReal = wallReal;
  p.rssi = (int8_t)rssi;
  p.snr4 = (int8_t)snr4;
  p.len = (uint16_t)(len > 0xFFFF ? 0xFFFF : len);
  // MeshCore's raw packet: header, [transport codes x2 when the route says so], path_len byte,
  // path bytes, payload. A short or malformed frame keeps what could be read and no hash.
  p.type = (uint8_t)((raw[0] >> 2) & 0x0F);
  p.route = (uint8_t)(raw[0] & 0x03);
  int at = 1;
  if (p.route == 0x00 || p.route == 0x03) at += 4;
  if (at < len) {
    const uint8_t pathByte = raw[at++];
    p.pathCount = (uint8_t)(pathByte & 63);
    p.pathHashSize = (uint8_t)((pathByte >> 6) + 1);
    at += (int)p.pathCount * (int)p.pathHashSize;
  }
  if (at + kHashBytes <= len) {
    memcpy(p.hash, &raw[at], kHashBytes);
    p.hashValid = true;
  }
  s_packetHead = (s_packetHead + 1) % kPacketSamples;
  if (s_packetCount < kPacketSamples) ++s_packetCount;
}

void beginPass(uint32_t nowUs) {
  s_passOpen = true;
  s_passStartUs = nowUs;
  memset(s_passPartUs, 0, sizeof(s_passPartUs));
}

void addPart(Part p, uint32_t us) {
  const int i = (int)p;
  if (i < 0 || i >= kPartCount) return;
  s_passPartUs[i] += us;
}

uint32_t passPartUs(Part p) {
  const int i = (int)p;
  return (i >= 0 && i < kPartCount) ? s_passPartUs[i] : 0;
}

void endPass(uint32_t nowUs, uint32_t nowMs) {
  if (!s_passOpen) return;
  s_passOpen = false;
  const uint32_t total = nowUs - s_passStartUs;
  // Whatever no block claimed (touch, the small housekeeping, the delay-free remainder) is Other, so
  // the parts always add up to the pass and the worst pass names a real block.
  uint32_t known = 0;
  for (int i = 0; i < kPartCount; ++i) known += s_passPartUs[i];
  if (total > known) addPart(Part::Other, total - known);
  s_lastPassUs = total;
  ++s_histogram[histogramIndex(total)];

  if (total > s_worstUs) {
    s_worstUs = total;
    s_worstPart = worstPartOf(s_passPartUs);
    memcpy(s_worstParts, s_passPartUs, sizeof(s_worstParts));
  }
  // "the worst loop pass per minute": a window of 60 s, reported for the minute that just ended.
  if ((uint32_t)(nowMs - s_minuteStartMs) >= 60000) {
    s_lastMinuteWorstUs = s_minuteWorstUs;
    s_lastMinuteWorstPart = s_minuteWorstPart;
    s_minuteStartMs = nowMs;
    s_minuteWorstUs = 0;
    s_minuteWorstPart = Part::Other;
  }
  if (total > s_minuteWorstUs) {
    s_minuteWorstUs = total;
    s_minuteWorstPart = worstPartOf(s_passPartUs);
  }
}

Snapshot snapshot() {
  Snapshot s;
  if (s_noiseCount > 0) {
    int64_t sum = 0;
    int lo = s_noise[0];
    for (int i = 0; i < s_noiseCount; ++i) {
      const int v = s_noise[i];
      sum += v;
      if (v < lo) lo = v;
    }
    s.noise.samples = s_noiseCount;
    s.noise.min = lo;
    s.noise.avg = (int)(sum / s_noiseCount);
    s.noise.last = s_noise[(s_noiseHead + kNoiseSamples - 1) % kNoiseSamples];
  }
  s.packetsHeard = s_packetsHeard;
  s.packetSamples = s_packetCount;
  s.loopValid = s_lastPassUs != 0;
  s.worstUs = s_worstUs;
  s.worstPart = s_worstUs != 0 ? worstPartOf(s_worstParts) : Part::Other;
  s.worstMinuteUs = s_lastMinuteWorstUs;
  s.worstMinutePart = s_lastMinuteWorstUs != 0 ? s_lastMinuteWorstPart : Part::Other;
  s.lastPassUs = s_lastPassUs;
  return s;
}

int packetCount() { return s_packetCount; }

bool packetAt(int i, PacketSample* out) {
  if (out == nullptr || i < 0 || i >= s_packetCount) return false;
  const int at = (s_packetHead + kPacketSamples - 1 - i) % kPacketSamples;
  *out = s_packets[at];
  return true;
}

uint32_t histogramBucket(int i) {
  if (i < 0 || i >= kHistogramBuckets) return 0;
  return s_histogram[i];
}

uint32_t histogramLastPassTotal() {
  uint32_t n = 0;
  for (int i = 0; i < kHistogramBuckets; ++i) n += s_histogram[i];
  return n;
}

}  // namespace radio
}  // namespace om
