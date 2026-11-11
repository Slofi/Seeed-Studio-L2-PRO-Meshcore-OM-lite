// SPDX-License-Identifier: MIT
// EST-376: the radio A/B knobs and the `radio sample` lines. See radio_diag.h.
//
// Why resetAGC() and not triggerNoiseFloorCalibrate(): in
// lib/meshcore/helpers/radiolib/RadioLibWrappers.cpp, resetAGC() runs the real receiver reset
// (CustomSX1262Wrapper::doResetAGC -> sx126xResetAGC: warm sleep, standby, Calibrate(0x7F), image
// calibration), then sets state = STATE_IDLE so the next recvRaw() re-enters RX, AND zeroes
// _noise_floor, _num_floor_samples and _floor_sample_sum. triggerNoiseFloorCalibrate() only clears
// the sample counters when 64 samples are already in (and does nothing otherwise); it leaves
// _noise_floor at its old value. So resetAGC() is the one that really clears the noise floor, and
// it is also exactly what MeshCore does to the receiver after a reset.
#include "radio_diag.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <math.h>

#include "diag_logic.h"
#include "radio_stats.h"
#include "target.h"

namespace om {
namespace rdiag {
namespace {

// The most lines one run can print: 600 s / 5 s = 120, plus the end tick.
constexpr int kMaxSamples = 128;
constexpr uint32_t kPeriodMs = 5000;
constexpr int kMinSeconds = 10;
constexpr int kMaxSeconds = 600;

// --- the A/B state, RAM only ---------------------------------------------------------------------
bool s_pendingTcxo = false;
float s_pendingTcxoVolts = 0.0f;
bool s_pendingBoost = false;
bool s_pendingBoostOn = false;
bool s_waitLogged = false;      // "waiting for a quiet moment" printed once per request
bool s_agcPending = false;      // a packet landed in the reset's way; try again next pass

float s_tcxoVolts = SX126X_DIO3_TCXO_VOLTAGE;   // what target.h gave the chip at boot
int s_tcxoStatus = kStatusNone;                 // RadioLib's answer to the last setTCXO
int s_boostStatus = kStatusNone;                // RadioLib's answer to the last setRxBoostedGainMode
bool s_boostOn = false;                         // cached read-back of the chip's RX gain register
bool s_boostSeen = false;
uint32_t s_boostReadMs = 0;

// --- the sampler ---------------------------------------------------------------------------------
bool s_active = false;
uint32_t s_startMs = 0;
uint32_t s_nextMs = 0;
uint32_t s_endMs = 0;
int s_seconds = 0;
int s_samples[kMaxSamples] = {};
int s_sampleCount = 0;
uint32_t s_packets = 0;
int32_t s_rssiSum = 0;
int32_t s_snr4Sum = 0;

const char* statusName(int status) {
  switch (status) {
    case RADIOLIB_ERR_NONE: return "ok";
    case RADIOLIB_ERR_CHIP_NOT_FOUND: return "CHIP_NOT_FOUND";
    case RADIOLIB_ERR_SPI_CMD_INVALID: return "SPI_CMD_INVALID";
    case RADIOLIB_ERR_SPI_CMD_FAILED: return "SPI_CMD_FAILED";
    default: return "err";
  }
}

void printApplyLine(int tcxoStatus, int boostStatus, int nfBefore, int nfAfter) {
  Serial.printf("[radio] apply tcxo=%.1f tcxo_status=%d(%s) boost=%d boost_status=%d(%s) nf=%d->%d\n",
                s_tcxoVolts, tcxoStatus, statusName(tcxoStatus), s_boostOn ? 1 : 0,
                boostStatus, statusName(boostStatus), nfBefore, nfAfter);
}

// Applies whatever the commands queued. Called once per loop pass; never from the USB handler.
void applyPending() {
  // A reset that lost the race with a packet is retried here, before anything else: the change is
  // already in the chip, only the noise restart is still owed.
  if (s_agcPending && !s_pendingTcxo && !s_pendingBoost) {
    if (radio_driver.isReceiving()) return;
    radio_driver.resetAGC();
    if (radio_driver.getNoiseFloor() == 0) {
      s_agcPending = false;
      Serial.println("[radio] agc reset done");
    }
    return;
  }
  if (!s_pendingTcxo && !s_pendingBoost) return;

  // resetAGC() refuses to run mid-packet (it would wreck the receiver) and while a received packet
  // is still unread, so wait for a quiet pass instead of queueing a reset that silently does
  // nothing.
  if (radio_driver.isReceiving()) {
    if (!s_waitLogged) {
      Serial.println("[radio] apply waiting: rx busy");
      s_waitLogged = true;
    }
    return;
  }
  s_waitLogged = false;

  const int nfBefore = radio_driver.getNoiseFloor();
  int tcxoStatus = kStatusNone;
  int boostStatus = kStatusNone;

  const bool boostRequested = s_pendingBoost;
  const bool boostWanted = s_pendingBoostOn;
  if (s_pendingTcxo) {
    tcxoStatus = radioDiagSetTcxo(s_pendingTcxoVolts);
    if (tcxoStatus == RADIOLIB_ERR_NONE) s_tcxoVolts = s_pendingTcxoVolts;
    s_pendingTcxo = false;
  }
  if (boostRequested) {
    boostStatus = radioDiagSetBoost(boostWanted);
    s_pendingBoost = false;
  }

  // The RX re-entry MeshCore itself uses after a reset: warm sleep, calibrate, image cal, state
  // idle (the next recvRaw() calls startReceive), and _noise_floor back to 0 so the sampler
  // reconverges from scratch. This is also what restarts the TCXO at the new voltage: doResetAGC's
  // warm sleep powers DIO3 down, and the standby that follows restarts the oscillator.
  radio_driver.resetAGC();

  // sx126xResetAGC() re-applies the RX gain only under #ifdef SX126X_RX_BOOSTED_GAIN, which this
  // build does not define. Read the chip back and set it again if the reset lost it.
  if (boostRequested && radio_driver.getRxBoostedGainMode() != boostWanted) {
    boostStatus = radioDiagSetBoost(boostWanted);
  }
  s_boostOn = radio_driver.getRxBoostedGainMode();
  s_boostSeen = true;
  s_boostReadMs = millis();
  // A zero floor is resetAGC's own mark. Anything else means the reset was skipped (STATE_INT_READY
  // with a packet waiting to be read), so keep the retry armed.
  s_agcPending = radio_driver.getNoiseFloor() != 0;

  // Remember the last answer for `radio status`, including the case where the change was a no-op.
  if (tcxoStatus != kStatusNone) s_tcxoStatus = tcxoStatus;
  if (boostStatus != kStatusNone) s_boostStatus = boostStatus;

  printApplyLine(tcxoStatus, boostStatus, nfBefore, radio_driver.getNoiseFloor());
}

void printSampleLine(uint32_t nowMs) {
  const int nf = radio_driver.getNoiseFloor();
  if (s_sampleCount < kMaxSamples) s_samples[s_sampleCount++] = nf;
  Serial.printf("[rsamp] t=%lu nf=%d tcxo=%.1f boost=%d\n",
                (unsigned long)((nowMs - s_startMs) / 1000u), nf, tcxoVolts(),
                boostOn() ? 1 : 0);
}

void finishSample() {
  s_active = false;
  om::diag::SampleStats st;
  const bool haveSamples = om::diag::summarise(s_samples, s_sampleCount, &st);
  // A mean over no packets is "-", never a fabricated 0 (the same rule as the noise floor above).
  char rssiMean[8] = "-";
  char snrMean[12] = "-";
  if (s_packets > 0) {
    const int meanRssi = (int)(s_rssiSum / (int32_t)s_packets);
    const int meanSnr4 = (int)(s_snr4Sum / (int32_t)s_packets);
    const int frac = (meanSnr4 < 0 ? -meanSnr4 : meanSnr4) % 4;
    snprintf(rssiMean, sizeof(rssiMean), "%d", meanRssi);
    snprintf(snrMean, sizeof(snrMean), "%d.%02d", meanSnr4 / 4, frac * 25);
  }
  if (haveSamples) {
    Serial.printf(
        "[rsamp] done secs=%d samples=%d nf_min=%d nf_med=%d nf_max=%d nf_mean=%d pkts=%lu "
        "rssi_mean=%s snr_mean=%s\n",
        s_seconds, st.count, st.min, st.median, st.max, st.mean, (unsigned long)s_packets,
        rssiMean, snrMean);
  } else {
    Serial.printf(
        "[rsamp] done secs=%d samples=0 nf_min=- nf_med=- nf_max=- nf_mean=- pkts=%lu "
        "rssi_mean=%s snr_mean=%s\n",
        s_seconds, (unsigned long)s_packets, rssiMean, snrMean);
  }
  // No sample may leak into a later run; the counters are cheap to reset.
  s_sampleCount = 0;
  s_packets = 0;
  s_rssiSum = 0;
  s_snr4Sum = 0;
}

}  // namespace

bool requestTcxo(float volts) {
  if (fabsf(volts - 1.8f) > 0.001f && fabsf(volts - 3.0f) > 0.001f) return false;
  s_pendingTcxo = true;
  s_pendingTcxoVolts = volts;
  return true;
}

bool requestBoost(bool on) {
  s_pendingBoost = true;
  s_pendingBoostOn = on;
  return true;
}

bool pending() { return s_pendingTcxo || s_pendingBoost; }

float tcxoVolts() { return s_tcxoVolts; }
int lastTcxoStatus() { return s_tcxoStatus; }
int lastBoostStatus() { return s_boostStatus; }
bool boostOn() { return s_boostOn; }

bool sampleActive() { return s_active; }

int sampleSecondsLeft(uint32_t nowMs) {
  if (!s_active) return 0;
  if ((int32_t)(nowMs - s_endMs) >= 0) return 0;
  return (int)((s_endMs - nowMs + 999u) / 1000u);
}

bool sampleStart(int seconds, uint32_t nowMs) {
  if (s_active) return false;
  if (seconds < kMinSeconds || seconds > kMaxSeconds) return false;
  s_active = true;
  s_seconds = seconds;
  s_startMs = nowMs;
  s_nextMs = nowMs + kPeriodMs;
  s_endMs = nowMs + (uint32_t)seconds * 1000u;
  s_sampleCount = 0;
  s_packets = 0;
  s_rssiSum = 0;
  s_snr4Sum = 0;
  return true;
}

void notePacket(const uint8_t* raw, int len, int rssi, int snr4) {
  if (!s_active) return;
  ++s_packets;
  s_rssiSum += rssi;
  s_snr4Sum += snr4;
  // The first payload byte: the sender's public-key prefix for an advert/response, the channel
  // hash for a group text. Two hex digits, never a message byte run (rule 9: logs end up in docs).
  char hash[3] = "--";
  const int at = om::radio::payloadOffset(raw, len);
  if (at >= 0 && at < len) snprintf(hash, sizeof(hash), "%02x", raw[at]);
  const int frac = (snr4 < 0 ? -snr4 : snr4) % 4;
  Serial.printf("[rsamp] pkt rssi=%d snr=%d.%02d hash=%s\n", rssi, snr4 / 4, frac * 25, hash);
}

void loop(uint32_t nowMs) {
  applyPending();

  // The chip is read back at most every 5 s; the sample and status lines use the cached value, so
  // no SPI read sits in the packet path.
  if (!s_boostSeen || (uint32_t)(nowMs - s_boostReadMs) >= kPeriodMs) {
    s_boostOn = radio_driver.getRxBoostedGainMode();
    s_boostSeen = true;
    s_boostReadMs = nowMs;
  }

  if (!s_active) return;
  if ((int32_t)(nowMs - s_endMs) >= 0) {
    printSampleLine(nowMs);
    finishSample();
    return;
  }
  if ((int32_t)(nowMs - s_nextMs) >= 0) {
    printSampleLine(nowMs);
    // Grid from the actual print time, not from the planned one: a long loop pass makes the next
    // line late instead of printing a burst of duplicate samples.
    s_nextMs = nowMs + kPeriodMs;
  }
}

}  // namespace rdiag
}  // namespace om
