// SPDX-License-Identifier: MIT
// EST-390: the ONE pressed-state owner. Every screen's press edge calls pressArm() with the rectangle
// its own hit test returned (ui/press_targets.h builds it), every finger sample calls pressMove(), the
// release calls pressClear(); drawPressOverlay() is registered as the frame overlay, so the pressed
// look is painted LAST on every frame the exact same way: the accent tinted over the element's own
// pixels (theme.h kPressedTint / kPressedAlpha) and, for a character key, the preview bubble above it.
//
// Why an overlay instead of per-screen drawing: the tint has to be on exactly the hit rectangle and
// every screen already draws its own content. Laying the tint over the finished frame means one
// implementation, one colour, one alpha, and no screen can forget it or draw it a pixel off.
#pragma once

#include <LovyanGFX.hpp>

#include "press.h"

namespace om {
namespace ui {

// The press edge, with the target the hit test chose. Called by the shell only.
void pressArm(const PressTarget& t, uint32_t nowMs);
// One sample while the finger is down: true when the finger left the target's rectangle and the look
// was cleared by this sample (the caller then repaints once, without the tint).
bool pressMove(int x, int y);
// The finger lifted (or the screen changed): the look goes away. The tap action itself is untouched.
void pressClear();
// Keep the tint but drop the character bubble (the accent preview is showing in that same spot).
void pressDropPreview();
bool pressActive();
PressTarget pressTarget();

// The frame overlay: the pressed tint on the target's exact rectangle, plus the key preview bubble.
// Draws nothing when no press is armed.
void drawPressOverlay(lgfx::LovyanGFX* d, uint32_t nowMs);

}  // namespace ui
}  // namespace om
