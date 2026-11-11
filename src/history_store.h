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

// EST-380: the shell's one pop-up per history write-failure episode ("History not saved: card
// error"). The store clears the notice when it is taken, so a repaint or the next pass cannot show
// it twice; `dropped` receives the number to put in the text when records were really lost.
WriteNotice takeWriteNotice(uint32_t* dropped = nullptr);

// The one store. Slots are created on first use and keyed by the ticket's chat id, so a caller can
// ask on every message without keeping an index.
HistoryStore& store();

}  // namespace history
}  // namespace om
