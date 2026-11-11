// SPDX-License-Identifier: MIT
// EST-387 host test: the GNSS warm-boot decision and the CASIC AID-INI frame, with the system g++ over
// the two headers the firmware compiles (src/gps_boot.h, src/gps_aid.h).
//
// What is proven here:
//   1. the reset-reason table: a power-on, a brown-out, a deep-sleep wake and an unreadable reason
//      reset the receiver; a software (OTA), panic, watchdog or external restart leaves it running;
//   2. the AID-INI frame, byte by byte against a hand-computed example: the sync bytes, the length
//      field, class/id, the 56-byte payload's field layout and the 32-bit sum;
//   3. the ACK/NAK reader: a binary frame is consumed out of the NMEA stream, a bad checksum and a
//      silly length are dropped without losing the next frame, and an answer to another message is
//      not mistaken for ours;
//   4. the once-per-boot gate: only a real, independent clock with no fix, once, and again only for a
//      better source;
//   5. the UTC civil time the payload carries (epoch -> fields -> epoch).
//
// The hand arithmetic for the frame in testHandComputedFrame() is written out in its comments so the
// expected bytes can be checked against the CASIC specification by eye, without running anything.
#include <stdio.h>
#include <string.h>

#include "gps_aid.h"
#include "gps_boot.h"

using namespace om;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// 2026-11-11 10:30:00 UTC, the timestamp every frame example below uses.
static const uint32_t kEpoch = 1794393000u;

// ---- 1. the reset-reason table ------------------------------------------------------------------
static void testResetTable() {
  // The four that restart the receiver.
  CHECK(gpsBootResetsReceiver(GpsBootReason::PowerOn));
  CHECK(gpsBootResetsReceiver(GpsBootReason::Brownout));
  CHECK(gpsBootResetsReceiver(GpsBootReason::DeepSleep));
  CHECK(gpsBootResetsReceiver(GpsBootReason::Unknown));

  // The six that must NOT: this is the ticket's fix.
  CHECK(!gpsBootResetsReceiver(GpsBootReason::Software));   // ESP.restart(), the OTA reboot
  CHECK(!gpsBootResetsReceiver(GpsBootReason::Panic));
  CHECK(!gpsBootResetsReceiver(GpsBootReason::IntWdt));
  CHECK(!gpsBootResetsReceiver(GpsBootReason::TaskWdt));
  CHECK(!gpsBootResetsReceiver(GpsBootReason::Wdt));
  CHECK(!gpsBootResetsReceiver(GpsBootReason::External));

  // Every reason is decided one way or the other (no default that silently means "touch the line").
  for (int r = 0; r <= 10; ++r) {
    const GpsBootReason b = gpsBootReasonOf(r);
    const bool reset = gpsBootResetsReceiver(b);
    const bool keep = b == GpsBootReason::Software || b == GpsBootReason::Panic ||
                      b == GpsBootReason::IntWdt || b == GpsBootReason::TaskWdt ||
                      b == GpsBootReason::Wdt || b == GpsBootReason::External ||
                      b == GpsBootReason::Sdio;
    CHECK(reset != keep);
  }

  // The SDK's numbers, as esp_reset_reason_t has them (board.cpp static_asserts the same table).
  CHECK(gpsBootReasonOf(0) == GpsBootReason::Unknown);
  CHECK(gpsBootReasonOf(1) == GpsBootReason::PowerOn);
  CHECK(gpsBootReasonOf(3) == GpsBootReason::Software);
  CHECK(gpsBootReasonOf(4) == GpsBootReason::Panic);
  CHECK(gpsBootReasonOf(9) == GpsBootReason::Brownout);
  CHECK(gpsBootReasonOf(999) == GpsBootReason::Unknown);   // an unknown number is a cold start
  CHECK(gpsBootResetsReceiver(gpsBootReasonOf(999)));

  // The words in `[gps] boot: reset (<word>)`.
  CHECK(strcmp(gpsBootReasonToken(GpsBootReason::PowerOn), "power-on") == 0);
  CHECK(strcmp(gpsBootReasonToken(GpsBootReason::Brownout), "brownout") == 0);
  CHECK(strcmp(gpsBootReasonToken(GpsBootReason::Software), "software") == 0);
  CHECK(strcmp(gpsBootReasonToken(GpsBootReason::Unknown), "unknown") == 0);
  for (int r = 0; r <= 10; ++r) {
    const char* tok = gpsBootReasonToken(gpsBootReasonOf(r));
    CHECK(tok != nullptr && tok[0] != 0);
  }
}

// ---- 2. the frame and its payload ---------------------------------------------------------------
// AID-INI for 2026-11-11 10:30:00 UTC, time only. Hand arithmetic:
//   header          BA CE
//   length          0x003E = 62 = class(1) + id(1) + payload(56) + sum(4)   -> 3E 00
//   class, id       0B 01
//   payload bytes 0..3    xo32_freq = 0                                  -> 00 00 00 00
//                 4..5    pos_valid = 0                                 -> 00 00
//                 6..7    time_valid = 1                                -> 01 00
//                 8..23   lat, lon (not sent)                           -> 16 zero bytes
//                 24..27  alt (not sent)                                -> 4 zero bytes
//                 28..29  year 2026 = 0x07EA                             -> EA 07
//                 30..34  month 11, day 11, hour 10, minute 30, second 0 -> 0B 0B 0A 1E 00
//                 35      reserved                                      -> 00
//                 36..55  reserved                                      -> 20 zero bytes
//   sum            0xBA+0xCE+0x3E+0x00+0x0B+0x01 = 466
//                  466 + payload (time_valid 1 + 234+7+11+11+10+30 = 304) = 770 = 0x0302 -> 02 03 00 00
static const uint8_t kExpectedTimeOnlyFrame[66] = {
    0xBA, 0xCE, 0x3E, 0x00, 0x0B, 0x01,   // header, length 62, class AID, id AID-INI
    0x00, 0x00, 0x00, 0x00,               // xo32_freq: keep the module's crystal
    0x00, 0x00,                           // pos_valid = 0
    0x01, 0x00,                           // time_valid = 1
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // lat (unused)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // lon (unused)
    0x00, 0x00, 0x00, 0x00,                           // alt (unused)
    0xEA, 0x07,                                       // year 2026
    0x0B, 0x0B, 0x0A, 0x1E, 0x00, 0x00,               // 11-11 10:30:00, reserved
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // reserved, 36..45
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // reserved, 46..55
    0x02, 0x03, 0x00, 0x00,                                       // sum 770, little-endian
};

static void testHandComputedFrame() {
  aid::AidTime t;
  t.year = 2026;
  t.month = 11;
  t.day = 11;
  t.hour = 10;
  t.minute = 30;
  t.second = 0;

  uint8_t payload[aid::kAidIniBytes];
  CHECK(aid::aidIniPayload(t, /*timeValid=*/true, /*posValid=*/false, 0.0, 0.0, 0.0f, payload,
                           (int)sizeof(payload)) == aid::kAidIniBytes);
  CHECK(sizeof(kExpectedTimeOnlyFrame) == (size_t)(aid::kAidIniBytes + 10));

  uint8_t frame[aid::kMaxFrame];
  const int n = aid::casicFrame(aid::kAidClass, aid::kAidIniId, payload, aid::kAidIniBytes, frame,
                                (int)sizeof(frame));
  CHECK(n == (int)sizeof(kExpectedTimeOnlyFrame));
  CHECK(memcmp(frame, kExpectedTimeOnlyFrame, sizeof(kExpectedTimeOnlyFrame)) == 0);

  // Field layout, read back the way a receiver would: the two validity words and the date fields sit
  // where the table says, and nothing else moved into them.
  CHECK(payload[4] == 0 && payload[5] == 0);
  CHECK(payload[6] == 1 && payload[7] == 0);
  CHECK(payload[28] == 0xEA && payload[29] == 0x07);
  CHECK(payload[30] == 11 && payload[31] == 11);
  CHECK(payload[32] == 10 && payload[33] == 30 && payload[34] == 0);

  // The same time WITH a position: lat 46.0 = 0x4047000000000000 (LE: 00*6 47 40), lon 14.5 =
  // 0x402D000000000000 (LE: 00*6 2D 40), pos_valid = 1. The header and the sum change with it, and
  // this is the byte the "pos=sent" path puts on the wire.
  uint8_t withPos[aid::kAidIniBytes];
  CHECK(aid::aidIniPayload(t, true, /*posValid=*/true, 46.0, 14.5, 0.0f, withPos,
                           (int)sizeof(withPos)) == aid::kAidIniBytes);
  const uint8_t latExpected[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x40};
  const uint8_t lonExpected[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x40};
  CHECK(memcmp(withPos + 8, latExpected, 8) == 0);
  CHECK(memcmp(withPos + 16, lonExpected, 8) == 0);
  CHECK(withPos[4] == 1 && withPos[5] == 0);
  CHECK(memcmp(withPos + 28, payload + 28, 8) == 0);   // the date is unchanged by the point
  const int n2 = aid::casicFrame(aid::kAidClass, aid::kAidIniId, withPos, aid::kAidIniBytes, frame,
                                 (int)sizeof(frame));
  CHECK(n2 == 66);
  CHECK(frame[6 + 4] == 1);   // pos_valid reached the frame at payload offset 4
}

static void testFrameRefusals() {
  aid::AidTime t;
  t.year = 2026;
  t.month = 11;
  t.day = 11;
  t.hour = 10;
  t.minute = 30;
  t.second = 0;
  uint8_t payload[aid::kAidIniBytes];
  uint8_t frame[aid::kMaxFrame];

  // Impossible fields are refused, never encoded as a plausible wrong value.
  aid::AidTime leap = t;
  leap.day = 30;
  leap.month = 2;
  CHECK(aid::aidIniPayload(leap, true, false, 0, 0, 0.0f, payload, (int)sizeof(payload)) == 0);
  aid::AidTime badHour = t;
  badHour.hour = 24;
  CHECK(aid::aidIniPayload(badHour, true, false, 0, 0, 0.0f, payload, (int)sizeof(payload)) == 0);
  CHECK(aid::aidIniPayload(t, true, true, 91.0, 0.0, 0.0f, payload, (int)sizeof(payload)) == 0);
  CHECK(aid::aidIniPayload(t, true, true, 0.0, -181.0, 0.0f, payload, (int)sizeof(payload)) == 0);
  CHECK(aid::aidIniPayload(t, true, false, 0, 0, 0.0f, payload, aid::kAidIniBytes - 1) == 0);
  // A time that is not sent is not validated either: the "position only" frame is legal.
  CHECK(aid::aidIniPayload(t, /*timeValid=*/false, /*posValid=*/true, 46.0, 14.5, 0.0f, payload,
                           (int)sizeof(payload)) == aid::kAidIniBytes);

  // The frame builder refuses what cannot fit rather than truncating.
  CHECK(aid::casicFrame(aid::kAidClass, aid::kAidIniId, payload, aid::kAidIniBytes, frame,
                        aid::kAidIniBytes + 9) == 0);
  CHECK(aid::casicFrame(aid::kAidClass, aid::kAidIniId, nullptr, 1, frame, (int)sizeof(frame)) == 0);
  CHECK(aid::casicFrame(aid::kAidClass, aid::kAidIniId, payload, aid::kMaxPayload + 1, frame,
                        (int)sizeof(frame)) == 0);
  // Zero payload is a legal frame (an empty ACK query), 10 bytes long.
  CHECK(aid::casicFrame(aid::kAckClass, aid::kAckId, nullptr, 0, frame, (int)sizeof(frame)) == 10);
}

// ---- 3. reading the answer ----------------------------------------------------------------------
// Hand-computed ACK for our AID-INI: BA CE | 09 00 (class 1 + id 1 + payload 3 + sum 4 = 9) | 05 00
// | 0B 01 38 (the acknowledged class, id and payload length 56) | DA 01 00 00 (sum 474).
static const uint8_t kAck[13] = {0xBA, 0xCE, 0x09, 0x00, 0x05, 0x00,
                                 0x0B, 0x01, 0x38, 0xDA, 0x01, 0x00, 0x00};
// The same with id 01 (NAK) and the sum one higher: DB 01 00 00.
static const uint8_t kNak[13] = {0xBA, 0xCE, 0x09, 0x00, 0x05, 0x01,
                                 0x0B, 0x01, 0x38, 0xDB, 0x01, 0x00, 0x00};

static aid::CasicReader::Step feedAll(aid::CasicReader& r, const uint8_t* buf, int n) {
  aid::CasicReader::Step last = aid::CasicReader::Step::Taken;
  for (int i = 0; i < n; ++i) last = r.feed(buf[i]);
  return last;
}

static void testReaderAck() {
  aid::CasicReader r;
  CHECK(feedAll(r, kAck, (int)sizeof(kAck)) == aid::CasicReader::Step::Complete);
  CHECK(r.valid());
  CHECK(r.cls() == 0x05 && r.id() == 0x00);
  CHECK(r.payloadLen() == 3);
  CHECK(r.answersFor(aid::kAidClass, aid::kAidIniId));
  CHECK(!r.isNak());
  CHECK(!r.answersFor(0x0B, 0x02));   // an answer to another message is not ours

  aid::CasicReader n;
  CHECK(feedAll(n, kNak, (int)sizeof(kNak)) == aid::CasicReader::Step::Complete);
  CHECK(n.valid());
  CHECK(n.isNak());
  CHECK(n.answersFor(aid::kAidClass, aid::kAidIniId));
}

static void testReaderPassesNmeaThrough() {
  static const char* kLine = "$GNRMC,093005.00,V,,,,,,,111126,,,N*7B\r\n";
  aid::CasicReader r;
  for (const char* p = kLine; *p; ++p) {
    CHECK(r.feed((uint8_t)*p) == aid::CasicReader::Step::Pass);
  }
  // A 0xBA that is not followed by 0xCE is not a frame start: the byte after it is stream data again.
  CHECK(r.feed(0xBA) == aid::CasicReader::Step::Taken);
  CHECK(r.feed('$') == aid::CasicReader::Step::Pass);
  CHECK(r.feed('G') == aid::CasicReader::Step::Pass);
  // Two 0xBA in a row: the second one may still start a frame.
  CHECK(r.feed(0xBA) == aid::CasicReader::Step::Taken);
  CHECK(r.feed(0xBA) == aid::CasicReader::Step::Taken);
  CHECK(r.feed(0xCE) == aid::CasicReader::Step::Taken);
}

static void testReaderBadFrames() {
  // A corrupted checksum is reported as a complete frame that is NOT valid (it must not count as an
  // ACK, and the NMEA stream must carry on).
  uint8_t bad[sizeof(kAck)];
  memcpy(bad, kAck, sizeof(kAck));
  bad[sizeof(bad) - 1] ^= 0xFF;
  aid::CasicReader r;
  CHECK(feedAll(r, bad, (int)sizeof(bad)) == aid::CasicReader::Step::Complete);
  CHECK(!r.valid());
  CHECK(!r.answersFor(aid::kAidClass, aid::kAidIniId));

  // A nonsense length (3 < class+id+sum) is dropped, and the very next frame still parses: the reader
  // resynchronises instead of swallowing the rest of the stream.
  aid::CasicReader s;
  const uint8_t silly[4] = {0xBA, 0xCE, 0x03, 0x00};
  CHECK(feedAll(s, silly, 4) == aid::CasicReader::Step::Taken);
  CHECK(feedAll(s, kAck, (int)sizeof(kAck)) == aid::CasicReader::Step::Complete);
  CHECK(s.valid() && s.answersFor(aid::kAidClass, aid::kAidIniId));

  // A frame split across calls (one byte at a time) is the same frame.
  aid::CasicReader t;
  for (int i = 0; i + 1 < (int)sizeof(kAck); ++i) {
    CHECK(t.feed(kAck[i]) == aid::CasicReader::Step::Taken);
  }
  CHECK(t.feed(kAck[sizeof(kAck) - 1]) == aid::CasicReader::Step::Complete);
  CHECK(t.valid());
}

// ---- 4. the once-per-boot gate ------------------------------------------------------------------
static void testAidGate() {
  aid::AidGate g;
  // Nothing to aid without a real clock, and the receiver never aids itself.
  CHECK(!g.shouldSend(/*clockReal=*/false, TimeSource::Wifi, kEpoch, /*gpsHasFix=*/false));
  CHECK(!g.shouldSend(true, TimeSource::None, kEpoch, false));
  CHECK(!g.shouldSend(true, TimeSource::Gps, kEpoch, false));
  CHECK(!g.shouldSend(true, TimeSource::GpsNoFix, kEpoch, false));
  // A fix means time and position come from the sky.
  CHECK(!g.shouldSend(true, TimeSource::Wifi, kEpoch, /*gpsHasFix=*/true));
  // A floor or a default is not a clock.
  CHECK(!g.shouldSend(true, TimeSource::Wifi, 0, false));
  CHECK(!g.shouldSend(true, TimeSource::Wifi, 1000000u, false));

  // One frame for the first real source, then silence.
  CHECK(g.shouldSend(true, TimeSource::Manual, kEpoch, false));
  g.onSent(TimeSource::Manual);
  CHECK(!g.shouldSend(true, TimeSource::Manual, kEpoch, false));
  CHECK(!g.shouldSend(true, TimeSource::Manual, kEpoch + 60, false));
  // A better source arrives: one more frame, once.
  CHECK(g.shouldSend(true, TimeSource::Phone, kEpoch, false));
  g.onSent(TimeSource::Phone);
  CHECK(!g.shouldSend(true, TimeSource::Phone, kEpoch, false));
  CHECK(g.shouldSend(true, TimeSource::Wifi, kEpoch, false));
  g.onSent(TimeSource::Wifi);
  CHECK(!g.shouldSend(true, TimeSource::Wifi, kEpoch, false));
  // A worse source never sends again, even with a very different time.
  CHECK(!g.shouldSend(true, TimeSource::Manual, kEpoch + 3600, false));
  CHECK(!g.shouldSend(true, TimeSource::Phone, kEpoch + 3600, false));
  CHECK(g.sends() == 3);
  CHECK(g.sent());
}

// ---- 5. the UTC civil time ----------------------------------------------------------------------
static void testCivilTime() {
  aid::UtcCivil c;
  aid::utcCivilFromEpoch(0, c);
  CHECK(c.year == 1970 && c.month == 1 && c.day == 1);
  CHECK(c.hour == 0 && c.minute == 0 && c.second == 0);

  aid::utcCivilFromEpoch(kEpoch, c);
  CHECK(c.year == 2026 && c.month == 11 && c.day == 11);
  CHECK(c.hour == 10 && c.minute == 30 && c.second == 0);

  // Leap day, and the last second of a year.
  aid::utcCivilFromEpoch(makeUtcEpoch(2028, 2, 29, 12, 0, 0), c);
  CHECK(c.year == 2028 && c.month == 2 && c.day == 29 && c.hour == 12);
  aid::utcCivilFromEpoch(makeUtcEpoch(2026, 12, 31, 23, 59, 59), c);
  CHECK(c.year == 2026 && c.month == 12 && c.day == 31);
  CHECK(c.hour == 23 && c.minute == 59 && c.second == 59);
  // Round trip through timekeeper's calendar helper.
  CHECK(makeUtcEpoch(c.year, c.month, c.day, c.hour, c.minute, c.second) ==
        makeUtcEpoch(2026, 12, 31, 23, 59, 59));

  aid::AidTime t;
  CHECK(aid::aidTimeFromEpoch(kEpoch, t));
  CHECK(t.year == 2026 && t.month == 11 && t.day == 11 && t.hour == 10 && t.minute == 30);
  CHECK(!aid::aidTimeFromEpoch(0, t));            // the floor is not a clock
  CHECK(!aid::aidTimeFromEpoch(0xFFFFFFFFu, t));  // and neither is a wraparound
}

// ---- 6. the switch is documented as off ---------------------------------------------------------
static void testSwitchIsOff() {
  // The ticket: keep aiding switched off by a constant until the module ACKs. This test fails the run
  // the moment somebody flips it without changing the device plan written down in gps_aid.h/PROGRESS.
  CHECK(aid::kEnabled == false);
  CHECK(aid::kAidIniBytes == 56);
  CHECK(aid::kAnswerTimeoutMs == 2000);
}

int main() {
  testResetTable();
  testHandComputedFrame();
  testFrameRefusals();
  testReaderAck();
  testReaderPassesNmeaThrough();
  testReaderBadFrames();
  testAidGate();
  testCivilTime();
  testSwitchIsOff();
  if (g_fail != 0) {
    printf("%d est387 check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("est387: reset table, AID-INI frame, ACK reader, aid gate and UTC time all ok\n");
  return 0;
}
