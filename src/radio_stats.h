// SPDX-License-Identifier: MIT
// EST-365: what the L2 actually hears, and how long one loop pass takes.
//
// The counters here are OUR model; the raw numbers come from upstream's own counters (MeshCore's
// Dispatcher, the RadioLib wrapper's n_recv/n_sent/n_recv_errors and noise floor) in mesh_link.cpp.
// The two things upstream does not keep are this model's job: a short noise-floor history (sampled,
// with min/avg) and the last 20 raw receptions. Everything is plain C++ (no Arduino), so
// test/host builds the noise window, the packet ring, the histogram and the loop attribution.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace radio {

// --- the noise floor window ----------------------------------------------------------------------
// One sample every 5 s, the last 36 = three minutes. The chip's own floor is a 64-sample average
// that only moves when a quiet sample arrives, so sampling it again every few seconds costs nothing
// and keeps a real min/avg over the recent past (the ticket's "last minutes").
constexpr int kNoiseSamples = 36;
constexpr uint32_t kNoisePeriodMs = 5000;

// --- the reception ring --------------------------------------------------------------------------
constexpr int kPacketSamples = 20;
constexpr int kHashBytes = 4;   // the first four payload bytes: a channel hash / sender prefix

struct PacketSample {
  uint32_t atMs = 0;        // millis() when it was read off the radio
  uint32_t atWall = 0;      // system clock then (0 = not known)
  bool atWallReal = false;
  uint8_t type = 0;         // PAYLOAD_TYPE_* (header >> 2)
  uint8_t route = 0;        // ROUTE_TYPE_*
  uint8_t pathCount = 0;    // hash count in the path
  uint8_t pathHashSize = 1;
  int8_t rssi = 0;          // dBm, the chip's last packet RSSI
  int8_t snr4 = 0;          // SNR x 4 (MeshCore's unit)
  uint16_t len = 0;         // raw bytes on the air
  uint8_t hash[kHashBytes] = {};
  bool hashValid = false;
};

// Upstream's own counters, filled by mesh_link.cpp. Kept as a POD so no MeshCore header leaks into
// the host test.
struct UpstreamCounters {
  bool valid = false;
  int noiseFloor = 0;
  int lastRssi = 0;
  int lastSnr4 = 0;
  uint32_t rxPackets = 0;       // radio wrapper n_recv (every packet read off the chip)
  uint32_t txPackets = 0;       // radio wrapper n_sent
  uint32_t crcErrors = 0;       // radio wrapper n_recv_errors (readData / CRC failures)
  uint32_t duplicates = 0;      // MeshCore's seen-table: direct + flood duplicates dropped
  uint32_t recvFlood = 0;
  uint32_t recvDirect = 0;
  uint32_t sentFlood = 0;
  uint32_t sentDirect = 0;
  uint32_t txAirMs = 0;
  uint32_t rxAirMs = 0;
  bool boosted = false;         // RX boosted gain, read back from the chip's register
  bool boostedKnown = false;
};

// --- the loop pass -------------------------------------------------------------------------------
// Which block took the time. The names are printed by `radio`, so a long stall names its culprit.
enum class Part : uint8_t {
  Other = 0,
  History,    // history::loop(): the card flush
  Wifi,       // wifiLoop()
  Ble,        // bleLoop()
  Gps,        // gpsPoll() + the clock feed
  Usb,        // otaPoll() + otaCommandPoll()
  Mesh,       // meshLinkLoop()
  Rptr,       // rptrPump()
  MapDecode,  // mapRender() (the JPEG tile decode + composite)
  Draw,       // the frame compose (everything drawn into the off-screen frame)
  FramePush,  // omFrameEnd() pushing the composed frame to the panel
  Count
};
constexpr int kPartCount = (int)Part::Count;
const char* partName(Part p);

// A pass longer than a LoRa packet can be heard in is the only dangerous one: at SF8 / BW 62.5 a
// packet lasts roughly 0.3..1 s, and the radio holds one packet while the loop is busy. The bucket
// list is printed as a histogram; the last bucket is "and longer".
constexpr int kHistogramBuckets = 10;
constexpr uint32_t kHistogramBoundsUs[kHistogramBuckets - 1] = {
    1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000};
// One LoRa packet, lowest useful SF8/BW62.5 airtime. A pass over this can lose the next packet.
constexpr uint32_t kOnePacketUs = 300000;

// --- model ---------------------------------------------------------------------------------------
void begin(uint32_t nowMs);
void noteNoise(int dbm, uint32_t nowMs);   // keeps one sample per kNoisePeriodMs
void noteRawPacket(const uint8_t* raw, int len, int rssi, int snr4, uint32_t wall, bool wallReal,
                   uint32_t nowMs);

// EST-376: the offset of the first PAYLOAD byte in MeshCore's raw frame (header, the two transport
// codes when the route carries them, then path length + path bytes). -1 when the frame ends before
// its payload. The sampler's `hash=` is this byte, the same one the ring above calls hash[0].
int payloadOffset(const uint8_t* raw, int len, uint8_t* pathCount = nullptr,
                  uint8_t* pathHashSize = nullptr);

void beginPass(uint32_t nowUs);
void addPart(Part p, uint32_t us);
void endPass(uint32_t nowUs, uint32_t nowMs);
uint32_t passPartUs(Part p);   // this pass, for the frame-push subtraction in main.cpp

struct NoiseModel {
  int last = 0;
  int min = 0;
  int avg = 0;
  int samples = 0;
};

struct Snapshot {
  NoiseModel noise;
  uint32_t packetsHeard = 0;
  int packetSamples = 0;
  bool loopValid = false;
  uint32_t worstUs = 0;          // since boot
  Part worstPart = Part::Other;
  uint32_t worstMinuteUs = 0;    // the last completed minute
  Part worstMinutePart = Part::Other;
  uint32_t lastPassUs = 0;
};
Snapshot snapshot();

int packetCount();
// i = 0 is the NEWEST reception; false when i is outside the ring.
bool packetAt(int i, PacketSample* out);

// The histogram is cumulative per bucket? No: `bucket(i)` is the pass count that landed in bucket i
// since the last reset. Tests read the same array `radio` prints.
uint32_t histogramBucket(int i);
uint32_t histogramLastPassTotal();

// Names for the wire values, so the USB line and the test use the same words.
const char* payloadTypeName(uint8_t type);
const char* routeName(uint8_t route);

}  // namespace radio
}  // namespace om
