// SPDX-License-Identifier: MIT
// EST-399: the emoji Recent row's NVS write, batched and counted.
//
// The shell wrote the whole Recent list to NVS on EVERY emoji tap (EST-398 #4): six emoji in one
// message were six flash commits for a 16-entry convenience list, and `putBytes()`'s result was
// dropped, so a failed write was invisible. That is EST-380's "hist_dropped = 0 while losing data"
// shape.
//
// This gate decides WHEN one write is due and counts what happened:
//   * a pick only marks the list dirty (`changed()`), no NVS,
//   * one write happens when the emoji page is no longer showing (leaving the page, however it was
//     left: the abc key, Send, a tap away, a tab change, the gear) or after kQuietMs with no more
//     picks, and the shell reports the result,
//   * a failed write keeps the list dirty and is retried no sooner than kRetryMs later, and every
//     failure is counted (and logged once per failure).
//
// Plain C++ (no Arduino): test/host/test_est399.cpp covers the window edges, both failure paths and
// the millis() wrap.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

constexpr uint32_t kEmojiSaveQuietMs = 30000;   // no more picks for this long -> save
constexpr uint32_t kEmojiSaveRetryMs = 5000;    // a failed write waits this long before another try

struct EmojiSaveGate {
  bool dirty = false;
  bool retrySet = false;     // a failed attempt has its own cooling-off window
  uint32_t changedMs = 0;    // the last pick (or the last failed attempt)
  uint32_t retryMs = 0;      // the earliest moment a retry may go out
  uint16_t writes = 0;       // writes that landed
  uint16_t fails = 0;        // writes that did not

  // A pick changed the list. RAM only; the write is the caller's, and only when due() says so.
  void changed(uint32_t now) {
    dirty = true;
    changedMs = now;
  }

  bool pending() const { return dirty; }

  // Is exactly one write due now? `pageShowing` is true while the emoji page is still on the glass.
  bool due(bool pageShowing, uint32_t now) const {
    if (!dirty) return false;
    // A failure's cooling-off window. It is a separate flag, not "retryMs != 0": a bare deadline
    // cannot be told apart from "no retry" across the millis() wrap (0 is a valid millis() value).
    if (retrySet && (int32_t)(now - retryMs) < 0) return false;
    if (!pageShowing) return true;                            // left the page: save at once
    return (uint32_t)(now - changedMs) >= kEmojiSaveQuietMs;  // or 30 s of no change
  }

  // The outcome of the one write due() asked for. A failure keeps the list dirty so the next
  // attempt can still land, and starts the retry window.
  void result(bool ok, uint32_t now) {
    if (ok) {
      ++writes;
      dirty = false;
      retrySet = false;
      return;
    }
    ++fails;
    dirty = true;
    changedMs = now;
    retryMs = now + kEmojiSaveRetryMs;
    retrySet = true;
  }
};

}  // namespace ui
}  // namespace om
