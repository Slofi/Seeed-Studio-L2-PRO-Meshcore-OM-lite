// SPDX-License-Identifier: MIT
// EST-380: the SD card's open-file budget, in one place.
//
// The card is mounted ONCE (board.cpp, board::sdMount) and its FATFS VFS allows only
// `maxOpenFiles` handles at a time (the Arduino default is 5). Past that number every
// open and every exists() fails: `fopen(...) failed`, and a history append silently had no
// file to write to. This header counts every handle the firmware can hold at once, so the
// mount limit can be checked by a static_assert AND by a host test instead of by counting
// calls by eye.
//
// The count (see docs/PROGRESS.md, EST-380):
//   map main pack 1 + 2 overlay levels x 2 files (idx + pack) + history pool 4 +
//   battery log 1 + the transient opens below + 2 margin.
//
// The transient opens never run at the same time (all SD work is on the loop task, one call
// after another), but every distinct one found by `rg "SD_MMC\.open|\.open\(|exists\("` is
// counted here on purpose: the margin then covers a combination nobody thought of, and a
// missing close in one of them cannot eat the last free slot.
#pragma once

#include <stdint.h>

namespace om {
namespace sd_budget {

// --- handles that stay open ---------------------------------------------------------------
// map.cpp: s_pack, the z9..z15 pack, opened in mapBegin() and kept.
constexpr int kMapMainPack = 1;
// map.cpp: s_ov[] holds two levels, each with a kept .idx and a kept .pack.
constexpr int kMapOverlayLevels = 2;   // z16, z17
constexpr int kMapOverlayFiles = kMapOverlayLevels * 2;
// history_store.cpp: FsVolume::kFiles, the reusable file objects of the store's volume.
constexpr int kHistoryPoolFiles = 4;
// power_log.cpp: one append at a time (the exists() probe closes before the append opens).
constexpr int kPowerLogFiles = 1;

// --- transient opens (one shot, loop task) -------------------------------------------------
// Arduino FS exists() opens the path for its stat and closes it.
constexpr int kExistsProbeFiles = 1;
// FsVolume::size(): exists() then one FILE_READ handle.
constexpr int kSizeProbeFiles = 1;
// mapBegin(): tiles.idx is read once through one handle, then closed.
constexpr int kMapIndexReadFiles = 1;
// plog::printTail(): one FILE_READ handle for the last 5 lines.
constexpr int kTailReaderFiles = 1;
// FsVolume::list(): the directory handle plus the entry return by openNextFile().
constexpr int kDirectoryListFiles = 2;
// tools/shot.py reads the off-screen frame over USB; the device opens no card file for it.
constexpr int kScreenshotFiles = 0;

constexpr int kTransientFiles = kExistsProbeFiles + kSizeProbeFiles + kMapIndexReadFiles +
                                kTailReaderFiles + kDirectoryListFiles + kScreenshotFiles;

// --- the sum and the limit ------------------------------------------------------------------
constexpr int kRequiredOpenFiles = kMapMainPack + kMapOverlayFiles + kHistoryPoolFiles +
                                   kPowerLogFiles + kTransientFiles;
// Two free slots on top of the sum: one so a probe can always run while every counted handle is
// busy, one because the next SD user will not remember to raise the limit.
constexpr int kMargin = 2;
constexpr int kMaxOpenFiles = kRequiredOpenFiles + kMargin;

static_assert(kMaxOpenFiles >= kRequiredOpenFiles, "the SD open-file limit must cover the budget");
static_assert(kMargin >= 2, "the ticket asks for two slots of margin");
static_assert(kMaxOpenFiles <= 255, "SD_MMC::begin() takes maxOpenFiles as a uint8_t");

constexpr uint8_t kBeginMaxOpenFiles = (uint8_t)kMaxOpenFiles;

}  // namespace sd_budget
}  // namespace om
