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
  // EST-363: one band per line of the header. The title used to share its band with the subtitle, so
  // the subtitle was drawn over the bottom half of the title (the 20:08 screenshot).
  a.title = Rect{(int16_t)(a.back.right() + 8), top, (int16_t)(L.width - a.back.right() - 14),
                 kRptrTitleH};
  a.sub = Rect{a.title.x, (int16_t)(top + kRptrTitleH), a.title.w, kRptrSubH};
  int16_t y = (int16_t)(top + kRptrPageHeadH + 1);
  if (hasCard) {
    a.card = Rect{8, y, (int16_t)(L.width - 16), kRptrCardH};
    // EST-364: the gutter at the card's right belongs to the refresh alone. Both value columns stop
    // before it, so no value can run under the icon (the 20:34 screenshot: "-119 dBm" did).
    a.cardColW = (int16_t)((a.card.w - 16 - kRptrCardGutter) / 2);
    // The icon: kRptrRefreshIcon square in the gutter's top corner. The finger target is a
    // kRptrRefreshTap square around it, kept inside the card (the card is 44 px tall, so 40 fits).
    const int16_t iconX = (int16_t)(a.card.right() - 8 - kRptrRefreshIcon);
    a.refresh = Rect{iconX, (int16_t)(a.card.y + 2), kRptrRefreshIcon, kRptrRefreshIcon};
    const int16_t tapX = (int16_t)(a.card.right() - 2 - kRptrRefreshTap);
    const int16_t tapY = (int16_t)(a.card.y + (kRptrCardH - kRptrRefreshTap) / 2);
    a.refreshTap = Rect{tapX, tapY, kRptrRefreshTap, kRptrRefreshTap};
    y = (int16_t)(a.card.bottom() + 3);
  }
  if (hasBanner) {
    a.banner = Rect{8, y, (int16_t)(L.width - 16), kRptrBannerH};
    a.bannerButton = Rect{(int16_t)(a.banner.right() - 76), (int16_t)(a.banner.y + 2), 70, 18};
    y = (int16_t)(a.banner.bottom() + 2);
  }
  // The bottom: the one Apply button (or the big Log in button) above the tab bar; the console's
  // input line takes the Apply slot instead.
  int16_t bodyBottom = L.tabbar.y;
  if (hasApply) {
    a.apply = Rect{8, (int16_t)(L.tabbar.y - kRptrApplyH - 3), (int16_t)(L.width - 16),
                   kRptrApplyH};
    bodyBottom = (int16_t)(a.apply.y - kRptrApplyGap);
  } else if (big) {
    a.big = Rect{8, (int16_t)(L.tabbar.y - kRptrBigH - kRptrBigGap), (int16_t)(L.width - 16),
                 kRptrBigH};
    bodyBottom = (int16_t)(a.big.y - kRptrBigGap);
  } else if (console) {
    a.send = Rect{(int16_t)(L.width - 8 - 50), (int16_t)(L.tabbar.y - 36), 50, 28};
    a.input = Rect{8, (int16_t)(L.tabbar.y - 36), (int16_t)(L.width - 16 - 50 - 6), 28};
    bodyBottom = (int16_t)(a.input.y - 3);
  }
  a.body = between(y, bodyBottom, L);
  a.visibleRows = a.body.h / kRptrPageRowH;
  // EST-363: never end the row area in the middle of a row. In EST-362 the page before login had 85 px
  // for three 30 px rows, so the Signal row was drawn 5 px into the Log in button and the value
  // "good 8 dB" was cut in half (the 20:08 screenshot). The body now ends on a row boundary; a row
  // that does not fit is reached by scrolling above the fixed button, never shown sliced.
  if (!console && a.visibleRows > 0) a.body.h = (int16_t)(a.visibleRows * kRptrPageRowH);
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
  if (!a.refreshTap.empty() && a.refreshTap.contains(x, y)) {
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
