// SPDX-License-Identifier: MIT
// EST-368 (Filip ~22:25): the new-message pop-up shows the MESSAGE, not one cut line.
//
// The panel has two parts: a mono dim first line (who and where: "TehiLL RAK · #slovenija" or
// "DM TehiLL RAK") and the text itself, wrapped with the chat's own wrapper (ui/text_wrap.h), so
// emoji and UTF-8 (š, č, ž) behave exactly as they do in the message list. It grows to fit, up to a
// limit, and it always stays on the screen above the tab bar. An ellipsis appears only when the text
// really does not fit; the cut is on a whole drawable unit and inside the panel's width.
//
// Plain C++ (the widths come from a MeasureFn callback), so test/host proves the wrap and the height
// limit with a fixed advance and the device uses the real VLW metrics.
#pragma once

#include <stdint.h>
#include <string.h>

#include "ui/text_wrap.h"
#include "ui/theme.h"

namespace om {
namespace ui {

constexpr int kNoticeMaxLines = 4;      // the ticket's 3-4 rows of text
constexpr int kNoticeBodyCap = 200;     // the message text the pop-up carries
constexpr int16_t kNoticePad = 7;       // panel padding, all four sides
constexpr int16_t kNoticeTitleH = 14;   // the mono line (MonoMeta, 10 px face)
constexpr int16_t kNoticeLineH = 14;    // SansBody's line pitch, the chat's kMsgLineH
constexpr int16_t kNoticeMinW = 120;

struct NoticeLayout {
  int lineCount = 0;                 // wrapped body lines actually drawn
  WrapLine lines[kNoticeMaxLines];   // byte ranges of the source text, per line
  int16_t lastLen = 0;               // bytes of the LAST line to draw (shortened for an ellipsis)
  bool truncated = false;            // the source has more than the panel shows
  int16_t boxX = 0;
  int16_t boxY = 0;
  int16_t boxW = 0;
  int16_t boxH = 0;
  int16_t titleCy = 0;               // the title's middle datum
  int16_t firstLineCy = 0;           // the first body line's middle datum
};

// Lay out one pop-up. `measure`/`ctx` measure with the body face (SansBody); `screenW` and `tabY`
// bound the panel (the tab bar's top edge), `topLimit` is the lowest y its top may use (below the
// status bar). A very short screen loses body lines before it loses the title line; the last drawn
// line always fits `...` when the text was cut.
NoticeLayout noticeLayout(const char* body, MeasureFn measure, void* ctx, int screenW, int tabY,
                          int topLimit);

}  // namespace ui
}  // namespace om
