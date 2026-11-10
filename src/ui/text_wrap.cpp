// SPDX-License-Identifier: MIT
#include "ui/text_wrap.h"

#include <string.h>

namespace om {
namespace ui {

int wrapText(const char* text, MeasureFn measure, void* ctx, int firstW, int nextW,
             WrapLine* lines, int maxLines) {
  if (text == nullptr || measure == nullptr || lines == nullptr || maxLines <= 0) return 0;

  int i = 0;
  while (text[i] == ' ') ++i;          // leading spaces never start a line
  int lineStart = i;
  int lineEnd = i;
  if (text[i] == 0) {
    lines[0].start = 0;
    lines[0].len = 0;
    return 1;
  }

  int n = 0;             // lines written so far
  int width = firstW;
  while (text[i] != 0) {
    while (text[i] == ' ') ++i;
    if (text[i] == 0) break;

    int wordEnd = i;
    while (text[wordEnd] != 0 && text[wordEnd] != ' ') ++wordEnd;

    const int cand = measure(ctx, text + lineStart, wordEnd - lineStart);
    const bool lineEmpty = (lineEnd == lineStart);
    if (cand <= width) {
      lineEnd = wordEnd;
      i = wordEnd;
      continue;
    }

    if (!lineEmpty) {
      // The word does not fit after what is already on this line: close the line and retry the word
      // on the next one, where it starts as an empty line.
      lines[n].start = (int16_t)lineStart;
      lines[n].len = (int16_t)(lineEnd - lineStart);
      if (++n >= maxLines) return n;
      width = nextW;
      lineStart = i;
      continue;
    }

    // A single word wider than the whole line: split it between characters, never mid-byte.
    int fit = lineStart;
    for (int k = i; k < wordEnd; ++k) {
      if (measure(ctx, text + lineStart, k + 1 - lineStart) <= width) fit = k + 1;
      else break;
    }
    if (fit == lineStart) fit = i + 1;          // one byte per line at worst (pathological font)
    lines[n].start = (int16_t)lineStart;
    lines[n].len = (int16_t)(fit - lineStart);
    if (++n >= maxLines) return n;
    width = nextW;
    lineStart = fit;
    lineEnd = fit;                              // the rest of the word continues on a fresh line
    i = fit;
  }

  lines[n].start = (int16_t)lineStart;
  lines[n].len = (int16_t)(lineEnd - lineStart);
  return n + 1;
}

int wrapConsumed(const char* text, const WrapLine* lines, int count) {
  if (text == nullptr || lines == nullptr || count <= 0) return 0;
  int end = 0;
  for (int i = 0; i < count; ++i) {
    const int e = lines[i].start + lines[i].len;
    if (e > end) end = e;
  }
  return end;
}

}  // namespace ui
}  // namespace om
