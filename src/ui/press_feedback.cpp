// SPDX-License-Identifier: MIT
// EST-390: the pressed-state owner and its one drawing. See ui/press_feedback.h and ui/press.h.
#include "ui/press_feedback.h"

#include "board.h"
#include "ui/keyboard_draw.h"
#include "ui/theme.h"

namespace om {
namespace ui {
namespace {

// The one instance. PressTracker's arithmetic (arm / slide-off / clear) is ui/press.h, host-tested;
// this file only owns the instance and paints it.
PressTracker s_press;

}  // namespace

void pressArm(const PressTarget& t, uint32_t nowMs) { s_press.arm(t, nowMs); }
bool pressMove(int x, int y) { return s_press.move(x, y); }
void pressClear() { s_press.clear(); }
void pressDropPreview() { s_press.dropPreview(); }
bool pressActive() { return s_press.active(); }
PressTarget pressTarget() { return s_press.target(); }

void drawPressOverlay(lgfx::LovyanGFX* d, uint32_t nowMs) {
  (void)nowMs;
  if (!s_press.active()) return;
  const PressTarget t = s_press.target();
  if (!t.valid()) return;

  // 1. The pressed look itself: the one theme tint over EXACTLY the hit rectangle. omFrameBlendRect
  //    clamps to the frame, so an edge element (a tab key, the bottom keyboard row) is safe.
  omFrameBlendRect(t.rect.x, t.rect.y, t.rect.w, t.rect.h, kPressedTint, kPressedAlpha);

  // 2. The key preview: only a key that types a character. Same bubble drawing as the long-press
  //    accent (drawPreviewBubble), placed by pressBubbleRect(): centred on the DRAWN key, above it,
  //    and never over the draft line that shows what is being typed.
  if (t.kind == PressKind::Key && t.cp != 0) {
    const Rect key = t.bubbleKey.empty() ? t.rect : t.bubbleKey;
    drawPreviewBubble(d, pressBubbleRect(key, t.bubbleTopLimit), t.cp);
  }
}

}  // namespace ui
}  // namespace om
