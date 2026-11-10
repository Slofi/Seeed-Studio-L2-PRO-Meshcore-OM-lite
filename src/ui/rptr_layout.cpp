// SPDX-License-Identifier: MIT
#include "ui/rptr_layout.h"

#include <stdio.h>
#include <string.h>

namespace om {
namespace ui {

void rptrRowMeta(char* out, size_t cap, const RptrRowFacts& facts) {
  if (out == nullptr || cap == 0) return;
  out[0] = 0;
  size_t n = 0;
  // "--" is MeshCore's "this byte cannot be decoded" and a stamp's "no honest time". Neither is a
  // fact, so neither is printed; a row with no facts keeps one line.
  auto known = [](const char* v) { return v != nullptr && v[0] != 0 && strcmp(v, "--") != 0; };
  auto separator = [&]() {
    if (n == 0) return true;
    if (n + 4 >= cap) return false;
    memcpy(out + n, " \xC2\xB7 ", 4);
    n += 4;
    out[n] = 0;
    return true;
  };
  // `label` is prepended with one space; the SNR arrives already labelled.
  auto add = [&](const char* label, const char* value, bool alreadyLabelled) {
    if (!known(value)) return;
    if (!separator()) return;
    const int w = alreadyLabelled ? snprintf(out + n, cap - n, "%s", value)
                                  : snprintf(out + n, cap - n, "%s %s", label, value);
    if (w < 0) { out[n] = 0; return; }
    const size_t room = cap - n;
    n += (size_t)w < room ? (size_t)w : room - 1;
    out[n] = 0;
  };
  // Most useful first (Filip: a time gives better info than an age), the path last: the row's box is
  // 40 bytes, so a long route is what gets cut, never the time or the signal.
  add("heard", facts.heard, false);
  add("", facts.snr, true);
  add("path", facts.path, false);
}

RptrLayout rptrLayoutFor(const Layout& L, bool detail) {
  (void)detail;   // the two pages share the header/list rectangles; the drawing picks the fields
  RptrLayout r;
  const int16_t top = L.status.bottom();
  r.head = Rect{0, top, L.width, kRptrHeadH};
  r.scan = Rect{(int16_t)(L.width - 8 - 58), (int16_t)(top + 4), 58, 18};
  r.list = Rect{0, (int16_t)(top + kRptrHeadH), L.width,
                (int16_t)(L.tabbar.y - top - kRptrHeadH)};
  r.empty = r.list;
  r.visibleRows = r.list.h / kRptrRowH;

  // Detail page: the mock nests a back key and a title in the same row, then one status card.
  r.back = Rect{8, (int16_t)(top + 3), kRptrBackW, 20};
  r.title = Rect{(int16_t)(r.back.right() + 8), top, (int16_t)(L.width - r.back.right() - 14),
                 kRptrHeadH};
  r.infoCard = Rect{8, (int16_t)(top + kRptrHeadH + 4), (int16_t)(L.width - 16), 92};
  r.loginLabel = Rect{8, (int16_t)(r.infoCard.bottom() + 6), 100, 14};
  r.loginField = Rect{8, (int16_t)(r.loginLabel.bottom() + 1), (int16_t)(L.width - 16 - 76), 22};
  r.loginButton = Rect{(int16_t)(r.loginField.right() + 8), r.loginField.y, 60, 22};
  r.note = Rect{8, (int16_t)(r.loginField.bottom() + 6), (int16_t)(L.width - 16),
                (int16_t)(L.tabbar.y - r.loginField.bottom() - 10)};
  return r;
}

RptrRow rptrRow(const RptrLayout& r, int index, int scroll) {
  RptrRow row;
  row.y = (int16_t)(r.list.y + index * kRptrRowH - scroll);
  row.h = kRptrRowH;
  return row;
}

int rptrRowAt(const RptrLayout& r, int count, int scroll, int x, int y) {
  if (!r.list.contains(x, y)) return -1;
  for (int i = 0; i < count; ++i) {
    const RptrRow row = rptrRow(r, i, scroll);
    const Rect rr{r.list.x, row.y, r.list.w, row.h};
    const Rect visible = rr.intersect(r.list);
    if (!visible.empty() && visible.contains(x, y)) return i;
  }
  return -1;
}

RptrHitResult rptrHitAt(const RptrLayout& r, bool detail, int count, int scroll, int x, int y) {
  RptrHitResult out;
  if (detail) {
    if (r.back.contains(x, y)) { out.kind = RptrHit::Back; return out; }
    if (r.loginField.contains(x, y)) { out.kind = RptrHit::LoginField; return out; }
    if (r.loginButton.contains(x, y)) { out.kind = RptrHit::LoginButton; return out; }
    return out;
  }
  if (r.scan.contains(x, y)) { out.kind = RptrHit::Scan; return out; }
  const int row = rptrRowAt(r, count, scroll, x, y);
  if (row >= 0) { out.kind = RptrHit::Row; out.row = row; }
  return out;
}

}  // namespace ui
}  // namespace om
