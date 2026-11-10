// SPDX-License-Identifier: MIT
// The shared keyboard sheet's drawing (EST-352). The geometry and the per-field key tables live in
// ui/chat_layout.h; this is the one implementation of a key cap and of the accented-letter preview,
// used by the chat composer and by the Settings text editors. Nothing here holds a coordinate: the
// rectangle comes from the layout that also answers the taps.
#pragma once

#include <LovyanGFX.hpp>

#include "ui/chat_layout.h"

namespace om {
namespace ui {

// One key of the sheet: Char / Shift / Mode / Space / Send as their label, Del and the smiley as
// vectors (the ASCII faces carry no glyph for them).
void drawKeyCap(lgfx::LovyanGFX* d, const Rect& r, const KeyDef& key, bool shift);

// The long-press preview bubble above `key` for the accented letter `cp` (0 draws nothing). The
// caller passes the drawn key's rectangle, so the bubble sits over the finger's key.
void drawAccentBubble(lgfx::LovyanGFX* d, const Rect& key, uint32_t cp);

}  // namespace ui
}  // namespace om
