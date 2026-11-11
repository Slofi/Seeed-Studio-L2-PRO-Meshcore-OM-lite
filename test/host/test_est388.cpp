// SPDX-License-Identifier: MIT
// Host test for EST-388: the pure parts of `https probe`. The radio side cannot run here (no Wi-Fi,
// no mbedTLS), so this file proves the rules the device code then obeys: the one target, the start
// refusal words, the HTTP status parse (including a header split across reads), the header
// terminator, the allocation-failure classification, and the exact shape of the report line the
// five device runs are compared by.
#include <stdio.h>
#include <string.h>

#include "https_probe.h"

static int g_fail = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++g_fail;                                                           \
    }                                                                     \
  } while (0)

static bool has(const char* hay, const char* needle) {
  return hay != nullptr && strstr(hay, needle) != nullptr;
}

static void testTargetIsFixed() {
  // The ticket's target, and the budget. A change here is a different measurement, so the test pins
  // it: host, path, port and 10 s.
  CHECK(strcmp(om::httpsprobe::kHost, "game.cl0.eu") == 0);
  CHECK(strcmp(om::httpsprobe::kPath, "/api/v1/health") == 0);
  CHECK(om::httpsprobe::kPort == 443);
  CHECK(om::httpsprobe::kTimeoutMs == 10000);
  // The lookup and the silence window must both fit inside the whole budget, or a dead DNS would
  // eat the handshake's time.
  CHECK(om::httpsprobe::kDnsTimeoutMs < om::httpsprobe::kTimeoutMs);
  CHECK(om::httpsprobe::kReadIdleMs < om::httpsprobe::kTimeoutMs);
  // A 0 us sampler would hammer the heap; the chosen period is a few milliseconds.
  CHECK(om::httpsprobe::kLowPeriodUs >= 1000 && om::httpsprobe::kLowPeriodUs <= 10000);
}

static void testStartRefusal() {
  // Wi-Fi off first: it outranks "not connected" (the switch is the honest reason).
  CHECK(strcmp(om::httpsprobe::startRefusal(false, false, false), "wifi off") == 0);
  CHECK(strcmp(om::httpsprobe::startRefusal(false, true, false), "wifi off") == 0);
  // On but the station has no link: refused, exactly the ticket's "only when Wi-Fi is connected".
  CHECK(strcmp(om::httpsprobe::startRefusal(true, false, false), "wifi not connected") == 0);
  // A probe already running is never joined or restarted.
  CHECK(strcmp(om::httpsprobe::startRefusal(true, true, true), "https busy") == 0);
  CHECK(om::httpsprobe::startRefusal(true, true, false) == nullptr);
}

static void testHttpStatus() {
  CHECK(om::httpsprobe::httpStatus("HTTP/1.1 200 OK\r\n", 17) == 200);
  CHECK(om::httpsprobe::httpStatus("HTTP/1.0 404 Not Found\r\n", 24) == 404);
  CHECK(om::httpsprobe::httpStatus("HTTP/1.1 503\r\n", 14) == 503);
  // A status line without its trailing CRLF is still complete once the three digits are there.
  CHECK(om::httpsprobe::httpStatus("HTTP/1.1 204", 12) == 204);
  // Not yet: fewer than 12 bytes, a partial line, a wrong prefix.
  CHECK(om::httpsprobe::httpStatus("HTTP/1.1 2", 10) == -1);
  CHECK(om::httpsprobe::httpStatus("", 0) == -1);
  CHECK(om::httpsprobe::httpStatus(nullptr, 0) == -1);
  CHECK(om::httpsprobe::httpStatus("HTTP/1.1 OK\r\n", 13) == -1);
  CHECK(om::httpsprobe::httpStatus("XTTP/1.1 200 OK", 14) == -1);
  CHECK(om::httpsprobe::httpStatus("HTTP/1.x 200 OK", 14) == -1);
  CHECK(om::httpsprobe::httpStatus("HTTP/1.1 20a OK", 14) == -1);
  // A digit run that is not a status ("HTTP/1.1 2000") must not pass as 200.
  CHECK(om::httpsprobe::httpStatus("HTTP/1.1 2000", 13) == -1);
  // The server's reason phrase does not matter.
  CHECK(om::httpsprobe::httpStatus("HTTP/1.1 418 teapot", 18) == 418);
  // Only the first status line counts; a body that starts with "HTTP/1.1" is not a status.
  const char* two = "HTTP/1.1 200 OK\r\nHTTP/1.1 500\r\n";
  CHECK(om::httpsprobe::httpStatus(two, strlen(two)) == 200);
}

static void testHeaderEnd() {
  const char* r1 = "HTTP/1.1 200 OK\r\nA: b\r\n\r\nbody";
  CHECK(om::httpsprobe::headerEnd(r1, strlen(r1)) == 25);
  const char* r2 = "HTTP/1.1 200 OK\nA: b\n\nbody";
  CHECK(om::httpsprobe::headerEnd(r2, strlen(r2)) == 22);
  const char* r3 = "HTTP/1.1 200 OK\r\nA: b\r\n";
  CHECK(om::httpsprobe::headerEnd(r3, strlen(r3)) == -1);
  CHECK(om::httpsprobe::headerEnd("", 0) == -1);
  CHECK(om::httpsprobe::headerEnd(nullptr, 0) == -1);
  // The empty line must be found at the very end of the buffer too (no read past len).
  const char* r4 = "A\r\n\r\n";
  CHECK(om::httpsprobe::headerEnd(r4, strlen(r4)) == 5);
}

static void testAllocFailure() {
  // The two codes that mean "not enough memory", and not every negative number.
  CHECK(om::httpsprobe::isAllocFailure(om::httpsprobe::kErrSslAllocFailed));
  CHECK(om::httpsprobe::isAllocFailure(om::httpsprobe::kErrX509AllocFailed));
  CHECK(!om::httpsprobe::isAllocFailure(0));
  CHECK(!om::httpsprobe::isAllocFailure(-1));
  CHECK(!om::httpsprobe::isAllocFailure(-0x7100));   // MBEDTLS_ERR_SSL_BAD_INPUT_DATA
  CHECK(!om::httpsprobe::isAllocFailure(-0x2700));   // MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
}

static void testReportLine() {
  om::httpsprobe::Report r;
  r.attempt = 3;
  r.result = "ok";
  r.stage = "done";
  r.status = 200;
  r.err = 0;
  r.dnsMs = 12;
  r.connMs = 34;
  r.tlsMs = 456;
  r.totalMs = 567;
  r.bytes = 123;
  r.bodyBytes = 45;
  r.internalBefore = 160000;
  r.internalMin = 120000;
  r.internalAfter = 159000;
  r.largestBefore = 100000;
  r.largestMin = 60000;
  r.largestAfter = 99000;
  r.psramBefore = 8000000;
  r.psramAfter = 8000000;
  r.idfMinInternal = 110000;
  r.ble = false;
  r.ntpActive = true;
  char line[512];
  om::httpsprobe::formatLine(line, sizeof(line), r);
  CHECK(has(line, "[https] probe 3 "));
  CHECK(has(line, "result=ok"));
  CHECK(has(line, "stage=done"));
  CHECK(has(line, "status=200"));
  CHECK(has(line, "bytes=123"));
  CHECK(has(line, "body=45"));
  CHECK(has(line, "dns_ms=12"));
  CHECK(has(line, "conn_ms=34"));
  CHECK(has(line, "tls_ms=456"));
  CHECK(has(line, "hs_ms=422"));
  CHECK(has(line, "total_ms=567"));
  CHECK(has(line, "err=0"));
  CHECK(has(line, "internal_before=160000"));
  CHECK(has(line, "internal_min=120000"));
  CHECK(has(line, "internal_after=159000"));
  CHECK(has(line, "largest_before=100000"));
  CHECK(has(line, "largest_min=60000"));
  CHECK(has(line, "largest_after=99000"));
  CHECK(has(line, "psram_before=8000000"));
  CHECK(has(line, "psram_after=8000000"));
  CHECK(has(line, "idf_min=110000"));
  CHECK(has(line, "ble=off"));
  CHECK(has(line, "ntp_active=1"));
  // EST-389: the report line names the pinned anchors, as its LAST field, so its presence here also
  // proves none of the fields before it was cut off by the buffer.
  CHECK(strcmp(om::httpsprobe::kCaLabel, "isrg-x1+x2") == 0);
  CHECK(has(line, "ca=isrg-x1+x2"));

  // A negative handshake estimate is printed signed, never hidden or wrapped.
  r.connMs = 120;
  r.tlsMs = 100;
  om::httpsprobe::formatLine(line, sizeof(line), r);
  CHECK(has(line, "hs_ms=-20"));

  // BLE on is its own word, and a small buffer truncates without writing past it.
  r.ble = true;
  char tiny[32];
  memset(tiny, 'X', sizeof(tiny));
  om::httpsprobe::formatLine(tiny, sizeof(tiny), r);
  CHECK(tiny[sizeof(tiny) - 1] == 0);
  CHECK(strlen(tiny) == sizeof(tiny) - 1);

  // A null buffer is not a crash.
  om::httpsprobe::formatLine(nullptr, 64, r);
}

static void testReportDefaults() {
  const om::httpsprobe::Report r;
  CHECK(r.attempt == 0);
  CHECK(strcmp(r.result, "idle") == 0);
  CHECK(r.status == 0);
  CHECK(r.bytes == 0 && r.bodyBytes == 0);
  CHECK(r.internalBefore == 0 && r.internalMin == 0 && r.internalAfter == 0);
  CHECK(!r.ble && !r.ntpActive);
}

int main() {
  testTargetIsFixed();
  testStartRefusal();
  testHttpStatus();
  testHeaderEnd();
  testAllocFailure();
  testReportLine();
  testReportDefaults();
  if (g_fail == 0) printf("test_est388: all checks passed\n");
  return g_fail == 0 ? 0 : 1;
}
