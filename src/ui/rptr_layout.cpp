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

  // Detail page: the mock nests a back key and a title in the same row, then the facts card, the
  // password row, the result line, Forget and the hint box. The card is four rows tall at 320 px.
  r.back = Rect{8, (int16_t)(top + 3), kRptrBackW, 20};
  r.title = Rect{(int16_t)(r.back.right() + 8), top, (int16_t)(L.width - r.back.right() - 14),
                 kRptrHeadH};
  r.sub = Rect{r.title.x, (int16_t)(top + 16), r.title.w, 12};
  r.infoCard = Rect{8, (int16_t)(top + kRptrHeadH + 4), (int16_t)(L.width - 16), 76};
  r.loginLabel = Rect{8, (int16_t)(r.infoCard.bottom() + 4), 120, 14};
  r.loginField = Rect{8, (int16_t)(r.loginLabel.bottom() + 1), (int16_t)(L.width - 16 - 76), 22};
  r.loginButton = Rect{(int16_t)(r.loginField.right() + 8), r.loginField.y, 60, 22};
  r.result = Rect{8, (int16_t)(r.loginField.bottom() + 2), (int16_t)(L.width - 16 - 64), 14};
  r.forget = Rect{(int16_t)(L.width - 8 - 56), (int16_t)(r.loginField.bottom() + 1), 56, 18};
  r.note = Rect{8, (int16_t)(r.result.bottom() + 2), (int16_t)(L.width - 16),
                (int16_t)(L.tabbar.y - r.result.bottom() - 6)};
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
    if (r.forget.contains(x, y)) { out.kind = RptrHit::Forget; return out; }
    return out;
  }
  if (r.scan.contains(x, y)) { out.kind = RptrHit::Scan; return out; }
  const int row = rptrRowAt(r, count, scroll, x, y);
  if (row >= 0) { out.kind = RptrHit::Row; out.row = row; }
  return out;
}

// ------------------------------------------------------------------------------- the admin panel
RptrAdminLayout rptrAdminLayoutFor(const Layout& L) {
  RptrAdminLayout a;
  const int16_t top = L.status.bottom();
  a.back = Rect{8, (int16_t)(top + 3), kRptrBackW, 20};
  a.title = Rect{(int16_t)(a.back.right() + 8), top, (int16_t)(L.width - a.back.right() - 14),
                 kRptrHeadH};
  a.sub = Rect{a.title.x, (int16_t)(top + 16), a.title.w, 12};

  // Six mono chips in one row, each as wide as its label plus the mock's 5 px pad either side.
  int16_t x = 8;
  const int16_t chipY = (int16_t)(top + kRptrHeadH + 2);
  for (int i = 0; i < kRptrViewCount; ++i) {
    const int16_t w = (int16_t)(6 * (int16_t)strlen(kRptrViewLabel[i]) + 10);
    a.chips[i] = Rect{x, chipY, w, kRptrChipH};
    x = (int16_t)(x + w + kRptrChipGap);
  }
  const int16_t bodyTop = (int16_t)(chipY + kRptrChipH + 4);
  a.answer = Rect{0, (int16_t)(L.tabbar.y - kRptrAnswerH), L.width, kRptrAnswerH};
  a.body = Rect{0, bodyTop, L.width, (int16_t)(a.answer.y - bodyTop)};
  a.visibleRows = a.body.h / kRptrAdminRowH;
  return a;
}

RptrRowRects rptrRowRects(const RptrAdminLayout& a, int keys, int index, int scroll) {
  RptrRowRects out;
  const int16_t y = (int16_t)(a.body.y + index * kRptrAdminRowH - scroll);
  out.body = Rect{0, y, a.body.w, kRptrAdminRowH};
  // The keys sit 4 px in from the right edge, 24 px apart with a 2 px gap.
  const int16_t pad = 4;
  if (keys >= 2) {
    out.keyB = Rect{(int16_t)(a.body.w - pad - kRptrKeyW), (int16_t)(y + 5), kRptrKeyW,
                    kRptrKeyH};
    out.keyA = Rect{(int16_t)(out.keyB.x - 2 - kRptrKeyW), (int16_t)(y + 5), kRptrKeyW,
                    kRptrKeyH};
  } else if (keys == 1) {
    out.keyA = Rect{(int16_t)(a.body.w - pad - kRptrKeyW), (int16_t)(y + 5), kRptrKeyW,
                    kRptrKeyH};
  }
  const int16_t keysLeft = keys >= 2 ? out.keyA.x : (keys == 1 ? out.keyA.x : a.body.w - 8);
  out.label = Rect{10, (int16_t)(y + 1), (int16_t)(keysLeft - 18), 16};
  out.value = Rect{10, (int16_t)(y + 16), (int16_t)(keysLeft - 18), 16};
  return out;
}

int rptrAdminMaxScroll(const RptrAdminLayout& a, int rowCount) {
  const int total = rowCount * (int)kRptrAdminRowH;
  return total > a.body.h ? total - (int)a.body.h : 0;
}

RptrRowHit rptrAdminRowAt(const RptrAdminLayout& a, const uint8_t* keys, int rowCount, int scroll,
                          int x, int y) {
  RptrRowHit out;
  if (!a.body.contains(x, y)) return out;
  for (int i = 0; i < rowCount; ++i) {
    const int rowKeys = keys == nullptr ? 0 : (int)keys[i];
    const RptrRowRects r = rptrRowRects(a, rowKeys, i, scroll);
    const Rect visible = r.body.intersect(a.body);
    if (visible.empty() || !visible.contains(x, y)) continue;
    out.row = i;
    if (rowKeys >= 2 && r.keyA.contains(x, y)) {
      out.part = 1;
    } else if (rowKeys >= 2 && r.keyB.contains(x, y)) {
      out.part = 2;
    } else if (rowKeys == 1 && r.keyA.contains(x, y)) {
      out.part = 1;
    } else {
      out.part = 0;
    }
    return out;
  }
  return out;
}

int rptrAdminChipAt(const RptrAdminLayout& a, int x, int y) {
  for (int i = 0; i < kRptrViewCount; ++i) {
    if (a.chips[i].contains(x, y)) return i;
  }
  return -1;
}

}  // namespace ui
}  // namespace om
