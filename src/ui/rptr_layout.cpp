// SPDX-License-Identifier: MIT
#include "ui/rptr_layout.h"

#include <string.h>

namespace om {
namespace ui {

namespace {

Rect between(int16_t top, int16_t bottom, const Layout& L) {
  return Rect{0, top, L.width, (int16_t)(bottom - top)};
}

}  // namespace

RptrListLayout rptrListLayoutFor(const Layout& L) {
  RptrListLayout r;
  const int16_t top = L.status.bottom();
  r.head = Rect{0, top, L.width, kRptrHeadH};
  r.scan = Rect{(int16_t)(L.width - 8 - 58), (int16_t)(top + 4), 58, 18};
  r.list = Rect{0, (int16_t)(top + kRptrHeadH), L.width,
                (int16_t)(L.tabbar.y - top - kRptrHeadH)};
  r.empty = r.list;
  r.visibleRows = r.list.h / kRptrRowH;
  return r;
}

RptrRow rptrListRow(const RptrListLayout& r, int index, int scroll) {
  RptrRow row;
  row.y = (int16_t)(r.list.y + index * kRptrRowH - scroll);
  row.h = kRptrRowH;
  return row;
}

int rptrListRowAt(const RptrListLayout& r, int count, int scroll, int x, int y) {
  if (!r.list.contains(x, y)) return -1;
  for (int i = 0; i < count; ++i) {
    const RptrRow row = rptrListRow(r, i, scroll);
    const Rect rr{r.list.x, row.y, r.list.w, row.h};
    const Rect visible = rr.intersect(r.list);
    if (!visible.empty() && visible.contains(x, y)) return i;
  }
  return -1;
}

RptrHitResult rptrListHitAt(const RptrListLayout& r, int count, int scroll, int x, int y) {
  RptrHitResult out;
  if (r.scan.contains(x, y)) {
    out.kind = RptrHit::Scan;
    return out;
  }
  const int row = rptrListRowAt(r, count, scroll, x, y);
  if (row >= 0) {
    out.kind = RptrHit::ListRow;
    out.row = row;
  }
  return out;
}

// ---------------------------------------------------------------------------------------- one page
RptrPageLayout rptrPageLayoutFor(const Layout& L, bool hasCard, bool hasBanner, bool hasApply,
                                 bool big, bool console) {
  RptrPageLayout a;
  const int16_t top = L.status.bottom();
  a.back = Rect{8, (int16_t)(top + 3), kRptrBackW, 20};
  a.title = Rect{(int16_t)(a.back.right() + 8), top, (int16_t)(L.width - a.back.right() - 14),
                 kRptrHeadH};
  a.sub = Rect{a.title.x, (int16_t)(top + 16), a.title.w, 12};
  int16_t y = (int16_t)(top + kRptrHeadH + 1);
  if (hasCard) {
    a.card = Rect{8, y, (int16_t)(L.width - 16), kRptrCardH};
    a.refresh = Rect{(int16_t)(a.card.right() - 24), (int16_t)(a.card.y + 3), 21, 21};
    y = (int16_t)(a.card.bottom() + 3);
  }
  if (hasBanner) {
    a.banner = Rect{8, y, (int16_t)(L.width - 16), kRptrBannerH};
    a.bannerButton = Rect{(int16_t)(a.banner.right() - 76), (int16_t)(a.banner.y + 3), 70, 21};
    y = (int16_t)(a.banner.bottom() + 3);
  }
  // The bottom: the one Apply button (or the big Log in button) above the tab bar; the console's
  // input line takes the Apply slot instead.
  int16_t bodyBottom = L.tabbar.y;
  if (hasApply) {
    a.apply = Rect{8, (int16_t)(L.tabbar.y - kRptrApplyH - 4), (int16_t)(L.width - 16),
                   kRptrApplyH};
    bodyBottom = (int16_t)(a.apply.y - 3);
  } else if (big) {
    a.big = Rect{8, (int16_t)(L.tabbar.y - kRptrBigH - 8), (int16_t)(L.width - 16), kRptrBigH};
    bodyBottom = (int16_t)(a.big.y - 3);
  } else if (console) {
    a.send = Rect{(int16_t)(L.width - 8 - 50), (int16_t)(L.tabbar.y - 36), 50, 28};
    a.input = Rect{8, (int16_t)(L.tabbar.y - 36), (int16_t)(L.width - 16 - 50 - 6), 28};
    bodyBottom = (int16_t)(a.input.y - 3);
  }
  a.body = between(y, bodyBottom, L);
  a.visibleRows = a.body.h / kRptrPageRowH;
  return a;
}

int rptrPageMaxScroll(const RptrPageLayout& a, int rowCount) {
  const int total = rowCount * (int)kRptrPageRowH;
  return total > a.body.h ? total - (int)a.body.h : 0;
}

RptrRowRects rptrPageRowRects(const RptrPageLayout& a, const rptr::UiRow& row, int index,
                              int scroll) {
  RptrRowRects out;
  const int16_t y = (int16_t)(a.body.y + index * kRptrPageRowH - scroll);
  out.body = Rect{0, y, a.body.w, kRptrPageRowH};
  const int16_t pad = 6;
  const bool keys = row.stepKeys;
  if (keys) {
    out.keyB = Rect{(int16_t)(a.body.w - pad - kRptrKeyW), (int16_t)(y + 4), kRptrKeyW,
                    kRptrKeyH};
    out.keyA = Rect{(int16_t)(out.keyB.x - 2 - kRptrKeyW), (int16_t)(y + 4), kRptrKeyW,
                    kRptrKeyH};
  } else if (row.arrow || row.key[0]) {
    out.keyA = Rect{(int16_t)(a.body.w - pad - kRptrKeyW), (int16_t)(y + 4), kRptrKeyW,
                    kRptrKeyH};
  }
  const int16_t textRight = keys ? out.keyA.x : ((row.arrow || row.key[0]) ? out.keyA.x
                                                                           : (int16_t)(a.body.w - 8));
  const int16_t boxX = 10;
  out.box = Rect{boxX, (int16_t)(y + 8), 16, 16};
  const bool checkbox = row.act == rptr::Act::RegionToggle || row.act == rptr::Act::Toggle;
  const int16_t textX = checkbox ? (int16_t)(boxX + 22) : 10;
  out.label = Rect{textX, (int16_t)(y + 1), (int16_t)(textRight - textX - 4), 15};
  out.value = Rect{textX, (int16_t)(y + 15), (int16_t)(textRight - textX - 4), 15};
  return out;
}

RptrHitResult rptrPageHitAt(const RptrPageLayout& a, const rptr::ScreenOut& screen, int scroll,
                            int x, int y) {
  RptrHitResult out;
  if (a.back.contains(x, y)) {
    out.kind = RptrHit::Back;
    return out;
  }
  if (!a.refresh.empty() && a.refresh.contains(x, y)) {
    out.kind = RptrHit::Refresh;
    return out;
  }
  if (!a.bannerButton.empty() && a.bannerButton.contains(x, y)) {
    out.kind = RptrHit::BannerButton;
    return out;
  }
  if (!a.apply.empty() && a.apply.contains(x, y)) {
    out.kind = RptrHit::Apply;
    return out;
  }
  if (!a.big.empty() && a.big.contains(x, y)) {
    out.kind = RptrHit::BigButton;
    return out;
  }
  if (!a.input.empty() && a.input.contains(x, y)) {
    out.kind = RptrHit::Input;
    return out;
  }
  if (!a.send.empty() && a.send.contains(x, y)) {
    out.kind = RptrHit::Send;
    return out;
  }
  if (!a.body.contains(x, y)) return out;
  for (int i = 0; i < screen.count; ++i) {
    const rptr::UiRow& row = screen.rows[i];
    const RptrRowRects r = rptrPageRowRects(a, row, i, scroll);
    const Rect visible = r.body.intersect(a.body);
    if (visible.empty() || !visible.contains(x, y)) continue;
    out.row = i;
    if (row.stepKeys && r.keyA.contains(x, y)) {
      out.kind = RptrHit::RowMinus;
    } else if (row.stepKeys && r.keyB.contains(x, y)) {
      out.kind = RptrHit::RowPlus;
    } else if ((row.arrow || row.key[0]) && r.keyA.contains(x, y)) {
      out.kind = RptrHit::RowKey;
    } else {
      out.kind = RptrHit::RowBody;
    }
    return out;
  }
  return out;
}

// ------------------------------------------------------------------------------------- the pop-up
RptrModalLayout rptrModalLayoutFor(const Layout& L, const char* verb) {
  (void)verb;
  RptrModalLayout m;
  const int16_t w = kRptrModalW < L.width - 16 ? kRptrModalW : (int16_t)(L.width - 16);
  const int16_t h = kRptrModalH;
  m.box = Rect{(int16_t)((L.width - w) / 2), (int16_t)((L.height - h) / 2), w, h};
  m.text = Rect{(int16_t)(m.box.x + 10), (int16_t)(m.box.y + 10), (int16_t)(w - 20),
                (int16_t)(h - 52)};
  const int16_t bw = 96;
  const int16_t bh = 26;
  m.cancel = Rect{(int16_t)(m.box.x + 14), (int16_t)(m.box.bottom() - bh - 10), bw, bh};
  m.confirm = Rect{(int16_t)(m.box.right() - 14 - bw), (int16_t)(m.box.bottom() - bh - 10), bw, bh};
  return m;
}

RptrModalHit rptrModalHitAt(const RptrModalLayout& m, int x, int y) {
  if (m.cancel.contains(x, y)) return RptrModalHit::Cancel;
  if (m.confirm.contains(x, y)) return RptrModalHit::Confirm;
  // A tap outside the box cancels the confirmation and spends the gesture (Filip's rule).
  return RptrModalHit::Outside;
}

}  // namespace ui
}  // namespace om
