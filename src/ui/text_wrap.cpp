// SPDX-License-Identifier: MIT
#include "ui/text_wrap.h"

#include <string.h>

#include "ui/theme.h"

namespace om {
namespace ui {
namespace {

// Bytes of one sequence from a lead byte, or 1 when it cannot start one.
int seqLen(uint8_t lead) {
  if (lead < 0x80) return 1;
  if (lead >= 0xC2 && lead <= 0xDF) return 2;
  if (lead >= 0xE0 && lead <= 0xEF) return 3;
  if (lead >= 0xF0 && lead <= 0xF4) return 4;
  return 1;
}

}  // namespace

int utf8SeqLen(uint8_t lead) { return seqLen(lead); }

uint32_t utf8Decode(const char* s, int* i) {
  if (s == nullptr || i == nullptr) return kUtf8Invalid;
  const int at = *i;
  const uint8_t lead = (uint8_t)s[at];
  if (lead < 0x80) {
    *i = at + 1;
    return lead;
  }
  const int n = seqLen(lead);
  if (n == 1) {   // an invalid lead byte: one byte, one placeholder
    *i = at + 1;
    return kUtf8Invalid;
  }
  uint32_t cp = lead & (uint32_t)(0x7F >> n);
  for (int k = 1; k < n; ++k) {
    const uint8_t cont = (uint8_t)s[at + k];
    if ((cont & 0xC0) != 0x80) {   // truncated or not a continuation: do not walk into the next char
      *i = at + 1;
      return kUtf8Invalid;
    }
    cp = (cp << 6) | (cont & 0x3F);
  }
  *i = at + n;
  return cp;
}

int utf8Encode(uint32_t cp, char* out) {
  if (out == nullptr || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return 0;
  if (cp < 0x80u) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800u) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000u) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

int textUnitAt(const char* s, int end, int i, TextUnit* out) {
  if (out == nullptr) return end;
  out->start = (int16_t)i;
  out->coreEnd = (int16_t)i;
  out->end = (int16_t)i;
  out->cp = kUtf8Invalid;
  out->zero = true;
  if (s == nullptr || i < 0 || i >= end) return end;

  int at = i;
  const uint32_t cp = utf8Decode(s, &at);
  if (at > end) at = end;
  if (at <= i) at = i + 1;
  out->cp = cp;
  out->coreEnd = (int16_t)at;
  out->end = (int16_t)at;

  // A selector or modifier with no base in front of it: no room, nothing drawn.
  if (utf8IsEmojiModifier(cp)) return at;
  out->zero = false;

  // They belong to the base. A ZWJ also belongs to the part before it; the part after it is
  // the next unit, so a joined sequence draws its parts in order.
  int j = at;
  while (j < end) {
    int k = j;
    const uint32_t next = utf8Decode(s, &k);
    if (k > end) k = end;
    if (k <= j) k = j + 1;
    if (utf8IsEmojiModifier(next)) {
      j = k;
      out->end = (int16_t)j;
      continue;
    }
    if (next == kUtf8Zwj) {
      j = k;
      out->end = (int16_t)j;
    }
    break;
  }
  return (int)out->end;
}

int measureUtf8(const char* text, int len, GlyphWidthFn glyph, void* ctx, int spaceW) {
  if (text == nullptr || glyph == nullptr || len <= 0) return 0;
  int w = 0;
  for (int i = 0; i < len;) {
    TextUnit u;
    const int next = textUnitAt(text, len, i, &u);
    if (next <= i) break;              // never loop on a refused unit
    i = next;
    if (u.zero) continue;              // a stray selector takes no room
    const int gw = glyph(ctx, u.cp);
    w += gw > 0 ? gw : spaceW + kMissingExtra;
  }
  return w;
}

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

    // A single word wider than the whole line: split it between characters, never mid-byte and
    // never inside a UTF-8 sequence (a half sequence is what drew a bright bar in mid-word).
    int fit = lineStart;
    for (int k = i; k < wordEnd;) {
      int at = k;
      utf8Decode(text, &at);
      if (at > wordEnd) at = wordEnd;
      if (measure(ctx, text + lineStart, at - lineStart) <= width) fit = at;
      else break;
      k = at;
    }
    if (fit == lineStart) {
      int at = lineStart;
      utf8Decode(text, &at);
      fit = at > wordEnd ? wordEnd : at;          // one character per line at worst
      if (fit <= lineStart) fit = lineStart + 1;
    }
    lines[n].start = (int16_t)lineStart;
    lines[n].len = (int16_t)(fit - lineStart);
    if (++n >= maxLines) return n;
    width = nextW;
    lineStart = fit;
    lineEnd = fit;                              // the rest of the word continues on a fresh line
    i = fit;
  }

  // A split that ended exactly on the last character of the last word leaves nothing behind: no
  // empty line is appended after it.
  if (lineEnd > lineStart) {
    lines[n].start = (int16_t)lineStart;
    lines[n].len = (int16_t)(lineEnd - lineStart);
    return n + 1;
  }
  return n > 0 ? n : 1;
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
