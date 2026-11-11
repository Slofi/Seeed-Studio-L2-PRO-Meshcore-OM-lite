// SPDX-License-Identifier: MIT
// EST-388: the `https probe` measurement, the parts that must be provable without a radio.
//
// The question is one number: how much INTERNAL RAM does a TLS handshake to the game server need on
// this unit. The ticket says to measure, not to tune: one HTTPS GET, certificate validation ON, a
// 10 s budget, and one report line with the heap BEFORE, the LOWEST during and AFTER, plus PSRAM.
//
// Plain C++ (no Arduino, no mbedTLS), so test/host compiles this header with the system g++ and the
// firmware compiles the very same rules. The radio side (src/wifi_link.cpp) only reports facts.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace om {
namespace httpsprobe {

// The one target. Both strings are firmware constants: no USB word can name another host, another
// path or a query string, so the probe cannot be pointed anywhere else from the host.
constexpr const char* kHost = "game.cl0.eu";
constexpr const char* kPath = "/api/v1/health";
constexpr uint16_t kPort = 443;

// The whole probe (lookup + connect + request + read) has this budget; the lookup alone gets the
// smaller one so a dead DNS cannot eat the connect time.
constexpr uint32_t kTimeoutMs = 10000;
constexpr uint32_t kDnsTimeoutMs = 3000;
// Stop reading after this much silence, when the server neither closes nor finishes.
constexpr uint32_t kReadIdleMs = 2000;
// The heap low-water sampler's period. The TLS handshake is one blocking mbedTLS call, so only a
// second context (an esp_timer callback) can see the bottom while it runs.
constexpr uint32_t kLowPeriodUs = 2000;

// Where the probe is between the USB command and the result line. Requested and Dns are polled by
// the loop; Connect is the one bounded blocking exchange (see wifi_link.cpp for the argument).
enum class Phase : uint8_t { Idle = 0, Requested, Dns, Connect };

// Why the probe may not start, or nullptr. These are the exact words of the `[cmd] error` line.
inline const char* startRefusal(bool enabled, bool connected, bool running) {
  if (!enabled) return "wifi off";
  if (!connected) return "wifi not connected";
  if (running) return "https busy";
  return nullptr;
}

// mbedTLS's two allocation-failure codes on this IDF (mbedtls/ssl.h, mbedtls/x509.h). A handshake
// that dies here is the answer the ticket is looking for, so it gets its own result word. The device
// build static_asserts these two numbers against the real mbedTLS macros.
constexpr long kErrSslAllocFailed = -0x7F00L;   // MBEDTLS_ERR_SSL_ALLOC_FAILED
constexpr long kErrX509AllocFailed = -0x2880L;  // MBEDTLS_ERR_X509_ALLOC_FAILED

inline bool isAllocFailure(long err) {
  return err == kErrSslAllocFailed || err == kErrX509AllocFailed;
}

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

// The status code of a complete HTTP/1.x response line ("HTTP/1.1 200 OK"), or -1 when the buffer
// does not hold one yet (or ever). Only the status line is parsed; the body is counted, never read.
inline int httpStatus(const char* buf, size_t len) {
  if (buf == nullptr || len < 12) return -1;
  if (memcmp(buf, "HTTP/", 5) != 0) return -1;
  if (!isDigit(buf[5]) || buf[6] != '.' || !isDigit(buf[7]) || buf[8] != ' ') return -1;
  if (!isDigit(buf[9]) || !isDigit(buf[10]) || !isDigit(buf[11])) return -1;
  if (len > 12 && buf[12] != ' ' && buf[12] != '\r' && buf[12] != '\n') return -1;
  return (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0');
}

// One past the empty line that ends the response header (CRLF CRLF, or bare LFLF from a server that
// does not send CR), or -1 when the header has not ended inside `len` bytes.
inline int headerEnd(const char* buf, size_t len) {
  if (buf == nullptr) return -1;
  for (size_t i = 3; i < len; ++i) {
    if (buf[i - 3] == '\r' && buf[i - 2] == '\n' && buf[i - 1] == '\r' && buf[i] == '\n') {
      return (int)(i + 1);
    }
  }
  for (size_t i = 1; i < len; ++i) {
    if (buf[i - 1] == '\n' && buf[i] == '\n') return (int)(i + 1);
  }
  return -1;
}

// One probe's facts. `result` and `stage` point at string literals; every number is what the report
// line prints, so the host test can assert the shape without a device.
struct Report {
  uint32_t attempt = 0;
  const char* result = "idle";    // ok / dns / connect / tls / oom / write / timeout / http / alloc
  const char* stage = "-";        // dns / tcp / tls / request / read / done
  int status = 0;                 // HTTP status, 0 when no status line arrived
  long err = 0;                   // mbedTLS / WiFiClient error code, 0 = none
  uint32_t dnsMs = 0;
  uint32_t connMs = 0;            // plain TCP, its own connection, no TLS
  uint32_t tlsMs = 0;             // TCP + TLS on the measured connection
  uint32_t totalMs = 0;           // probe start to the last byte (or to the end of the wait)
  uint32_t bytes = 0;             // every byte read from the TLS stream (header + body)
  uint32_t bodyBytes = 0;         // read after the header's empty line, 0 when it was not seen
  uint32_t internalBefore = 0;
  uint32_t internalMin = 0;       // the low-water mark, sampled during the exchange
  uint32_t internalAfter = 0;
  uint32_t largestBefore = 0;
  uint32_t largestMin = 0;
  uint32_t largestAfter = 0;
  uint32_t psramBefore = 0;
  uint32_t psramAfter = 0;
  uint32_t idfMinInternal = 0;    // heap_caps_get_minimum_free_size() after: all-time low since boot
  bool ble = false;               // Bluetooth on at the moment of the exchange
  bool ntpActive = false;         // an SNTP attempt was in flight (a second radio client)
};

// The report line. Field names are the ticket's words, so Claude can read the five runs side by side
// without a legend. hs_ms is tls_ms - conn_ms, i.e. an ESTIMATE (two separate connections), and can
// come out negative when the network jitters; it is printed signed rather than hidden.
inline void formatLine(char* out, size_t cap, const Report& r) {
  if (out == nullptr || cap == 0) return;
  const long hs = (long)r.tlsMs - (long)r.connMs;
  snprintf(out, cap,
           "[https] probe %lu result=%s stage=%s status=%d bytes=%lu body=%lu dns_ms=%lu "
           "conn_ms=%lu tls_ms=%lu hs_ms=%ld total_ms=%lu err=%ld internal_before=%lu "
           "internal_min=%lu internal_after=%lu largest_before=%lu largest_min=%lu "
           "largest_after=%lu psram_before=%lu psram_after=%lu idf_min=%lu ble=%s ntp_active=%d",
           (unsigned long)r.attempt, r.result, r.stage, r.status, (unsigned long)r.bytes,
           (unsigned long)r.bodyBytes, (unsigned long)r.dnsMs, (unsigned long)r.connMs,
           (unsigned long)r.tlsMs, hs, (unsigned long)r.totalMs, r.err,
           (unsigned long)r.internalBefore, (unsigned long)r.internalMin,
           (unsigned long)r.internalAfter, (unsigned long)r.largestBefore,
           (unsigned long)r.largestMin, (unsigned long)r.largestAfter,
           (unsigned long)r.psramBefore, (unsigned long)r.psramAfter,
           (unsigned long)r.idfMinInternal, r.ble ? "on" : "off", r.ntpActive ? 1 : 0);
}

}  // namespace httpsprobe
}  // namespace om
