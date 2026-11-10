// SPDX-License-Identifier: MIT
// EST-368: the pop-up's layout arithmetic (wrap + height limit). No Arduino, no LovyanGFX.
#include "ui/notice_panel.h"

namespace om {
namespace ui {

namespace {

// No measure function: a host test's fixed 6 px advance.
int fixedAdvance(void* ctx, const char* text, int len) {
  (void)ctx;
  (void)text;
  return len * 6;
}

}  // namespace

NoticeLayout noticeLayout(const char* body, MeasureFn measure, void* ctx, int screenW, int tabY,
                          int topLimit) {
  NoticeLayout out;
  if (body == nullptr) body = "";

  // The panel spans the screen less a margin, like the one-line toast it replaces.
  out.boxW = (int16_t)(screenW - 2 * kToastMargin);
  if (out.boxW < kNoticeMinW) out.boxW = (int16_t)(screenW > kNoticeMinW ? kNoticeMinW : screenW);
  out.boxX = (int16_t)((screenW - out.boxW) / 2);

  // How many body lines fit: the tab bar's top edge is the floor, the status bar's bottom the
  // ceiling. The title line and the padding are always kept; only body lines are lost.
  int16_t bottom = (int16_t)(tabY - kToastMargin);
  int16_t top = (int16_t)(topLimit + kToastMargin);
  int room = (int)(bottom - top) - 2 * kNoticePad - kNoticeTitleH;
  int maxLines = room / kNoticeLineH;
  if (maxLines > kNoticeMaxLines) maxLines = kNoticeMaxLines;
  if (maxLines < 1) maxLines = 1;

  const int16_t textW = (int16_t)(out.boxW - 2 * kNoticePad);
  const MeasureFn use = (measure != nullptr) ? measure : fixedAdvance;

  out.lineCount = wrapText(body, use, ctx, textW, textW, out.lines, maxLines);
  if (out.lineCount < 1) out.lineCount = 1;
  out.lastLen = out.lines[out.lineCount - 1].len;
  const int consumed = wrapConsumed(body, out.lines, out.lineCount);
  out.truncated = consumed < (int)strlen(body);

  // A cut line ends in "...". Shorten the last line until it and the three dots fit inside the
  // panel. `utf8`-aware: wrapText's ranges are already on code point boundaries, so walking back a
  // whole sequence at a time keeps every drawn line valid UTF-8.
  if (out.truncated) {
    const int start = out.lines[out.lineCount - 1].start;
    const int ellW = use(ctx, "...", 3);
    int len = out.lastLen;
    while (len > 0 && use(ctx, body + start, len) + ellW > textW) {
      --len;
      while (len > 0 && ((unsigned char)body[start + len] & 0xC0) == 0x80) --len;
    }
    out.lastLen = (int16_t)len;
  }

  out.boxH = (int16_t)(2 * kNoticePad + kNoticeTitleH + out.lineCount * kNoticeLineH);
  out.boxY = (int16_t)(bottom - out.boxH);
  if (out.boxY < top) out.boxY = top;
  out.titleCy = (int16_t)(out.boxY + kNoticePad + kNoticeTitleH / 2);
  out.firstLineCy = (int16_t)(out.titleCy + kNoticeTitleH / 2 + kNoticeLineH / 2 + 1);
  return out;
}

}  // namespace ui
}  // namespace om
