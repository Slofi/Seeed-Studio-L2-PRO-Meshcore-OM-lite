// SPDX-License-Identifier: MIT
// Device glue for the chat history: SD_MMC first, a bounded SPIFFS fallback second.
//
// Everything that decides what a log means lives in history/history_log.h (plain C++, host-tested).
// This file only maps HistoryVolume/HistoryFile onto the Arduino FS API and keeps the one global
// HistoryStore. EST-349: callers (mesh_link.cpp) only enqueue a record; the card is written from
// loop() below, on the loop task, and never from a callback or the send path.
#pragma once

#include <Arduino.h>

#include "history/history_log.h"

namespace om {
namespace history {

// Called once, after setup() has tried the card (meshLinkBegin runs before the SD mount, so the
// history begins later). A failed volume stays chosen until the next boot.
bool begin(bool sdMounted);
// The ONE flush owner, called at the top of loop() (loop task) before otaPoll()/meshLinkLoop().
void loop();

const HistoryStatus& status();

// The one store. Slots are created on first use and keyed by the ticket's chat id, so a caller can
// ask on every message without keeping an index.
HistoryStore& store();

}  // namespace history
}  // namespace om
