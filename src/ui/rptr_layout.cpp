// SPDX-License-Identifier: MIT
#include "ui/rptr_layout.h"

namespace om {
namespace ui {

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
