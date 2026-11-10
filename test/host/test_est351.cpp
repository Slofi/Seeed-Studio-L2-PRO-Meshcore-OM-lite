// EST-351 host test: the map chrome's scale arithmetic, key rectangles and hit test; the RPTR list
// and page rectangles. Plain g++, no Arduino.
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <string.h>

#include "ui/map_chrome.h"
#include "ui/rptr_layout.h"

using namespace om::ui;

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static void testMapScale() {
  const double m0 = mapMetresPerPixel(13, 0.0);
  const double m1 = mapMetresPerPixel(14, 0.0);
  CHECK(m1 > 0.0);
  CHECK(fabs(m0 / m1 - 2.0) < 1e-9);   // one zoom step halves the ground size
  const double mEq = mapMetresPerPixel(13, 0.0);
  const double mLat = mapMetresPerPixel(13, 46.0);
  CHECK(mLat < mEq);                   // a degree is shorter at our latitude
  CHECK(fabs(mLat / mEq - cos(46.0 * M_PI / 180.0)) < 1e-9);

  // At z13 near Ljubljana a 1-2-5 bar fits the mock's short scale; its label pair is half/full.
  int16_t px = 0;
  const int32_t d = mapScaleDistance(13, 46.05, &px);
  CHECK(d > 0);
  CHECK(px > 20 && px <= kMapScaleMaxW);
  bool nice = false;
  const int32_t ladder[] = {10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000};
  for (int32_t v : ladder) if (v == d) nice = true;
  CHECK(nice);

  char mid[12], right[12];
  mapScaleLabels(1000, mid, sizeof(mid), right, sizeof(right));
  CHECK(strcmp(mid, "500m") == 0);
  CHECK(strcmp(right, "1km") == 0);
  mapScaleLabels(500, mid, sizeof(mid), right, sizeof(right));
  CHECK(strcmp(mid, "250m") == 0);
  CHECK(strcmp(right, "500m") == 0);
  mapScaleLabels(2000, mid, sizeof(mid), right, sizeof(right));
  CHECK(strcmp(mid, "1km") == 0);
  CHECK(strcmp(right, "2km") == 0);
}

static void testMapChrome() {
  const Layout& L = l2();
  const MapChrome c = mapChromeFor(L, 13, 46.05, false);
  CHECK(c.view.x == 0 && c.view.y == 0 && c.view.w == 320 && c.view.h == L.tabbar.y);
  CHECK(c.view.bottom() == L.tabbar.y);
  CHECK(c.chip.y >= L.status.bottom());          // the chip never hides under the status bar
  CHECK(c.chip.right() <= c.view.right());
  CHECK(c.zoomIn.right() == c.view.right() - kMapMargin);
  CHECK(c.zoomOut.right() == c.zoomIn.right());
  CHECK(c.lock.right() == c.zoomIn.right());
  CHECK(c.zoomOut.bottom() == c.view.bottom() - kMapMargin);
  CHECK(c.zoomOut.y == c.zoomIn.bottom() + kMapKeyGap);
  CHECK(c.zoomIn.y == c.lock.bottom() + kMapKeyGap);
  CHECK(!c.zoomIn.overlaps(c.zoomOut));
  CHECK(!c.lock.overlaps(c.zoomIn));
  CHECK(c.scale.x == kMapMargin);
  CHECK(c.scale.bottom() == c.view.bottom() - kMapMargin);
  CHECK(c.scale.right() <= c.view.right());
  CHECK(c.callout.empty());                       // nothing selected: no panel

  const MapChrome s = mapChromeFor(L, 13, 46.05, true);
  CHECK(!s.callout.empty());
  CHECK(s.callout.bottom() == c.view.bottom() - 8);
  CHECK(s.callout.x == 8 && s.callout.right() == 312);
  CHECK(s.scalePx == c.scalePx);
  CHECK(strcmp(s.scaleMid, c.scaleMid) == 0);
}

static void testMapHit() {
  const MapChrome c = mapChromeFor(l2(), 13, 46.05, true);

  CHECK(mapHitAt(c, nullptr, 0, c.zoomIn.x + c.zoomIn.w / 2, c.zoomIn.y + c.zoomIn.h / 2).kind ==
        MapHitKind::ZoomIn);
  CHECK(mapHitAt(c, nullptr, 0, c.zoomOut.x + c.zoomOut.w / 2,
                 c.zoomOut.y + c.zoomOut.h / 2).kind == MapHitKind::ZoomOut);
  CHECK(mapHitAt(c, nullptr, 0, c.lock.x + c.lock.w / 2, c.lock.y + c.lock.h / 2).kind ==
        MapHitKind::Lock);
  CHECK(mapHitAt(c, nullptr, 0, c.callout.x + 4, c.callout.y + 4).kind == MapHitKind::Callout);
  CHECK(mapHitAt(c, nullptr, 0, 160, 90).kind == MapHitKind::Map);
  CHECK(mapHitAt(c, nullptr, 0, 0, 0).kind == MapHitKind::None);   // the status bar's pixels

  MapMarker m[2];
  m[0].x = 100; m[0].y = 100; m[0].type = 2; m[0].name = "R1";
  m[1].x = 200; m[1].y = 120; m[1].type = 1; m[1].name = "C1";
  const MapHit h = mapHitAt(c, m, 2, 104, 103);
  CHECK(h.kind == MapHitKind::Marker);
  CHECK(h.marker == 0);
  CHECK(mapHitAt(c, m, 2, 200, 120).marker == 1);
  CHECK(mapHitAt(c, m, 2, 150, 150).kind == MapHitKind::Map);     // nothing within 11 px
  // A marker wins over the key it happens to sit on.
  m[1].x = (int16_t)(c.zoomIn.x + c.zoomIn.w / 2);
  m[1].y = (int16_t)(c.zoomIn.y + c.zoomIn.h / 2);
  CHECK(mapHitAt(c, m, 2, m[1].x, m[1].y).kind == MapHitKind::Marker);
}

static void testRptr() {
  const Layout& L = l2();
  const RptrLayout list = rptrLayoutFor(L, false);
  CHECK(list.head.y == L.status.bottom());
  CHECK(list.head.bottom() == list.list.y);
  CHECK(list.head.h == kRptrHeadH);
  CHECK(list.list.bottom() == L.tabbar.y);
  CHECK(list.scan.right() <= L.width);
  CHECK(list.scan.y >= list.head.y && list.scan.bottom() <= list.head.bottom());
  CHECK(list.visibleRows >= 4);

  // Rows tile the list: 34 px, adjacent, only the visible part answers.
  const RptrRow r0 = rptrRow(list, 0, 0);
  const RptrRow r1 = rptrRow(list, 1, 0);
  CHECK(r0.y == list.list.y);
  CHECK(r1.y == r0.y + kRptrRowH);
  CHECK(rptrRowAt(list, 5, 0, 10, list.list.y + 5) == 0);
  CHECK(rptrRowAt(list, 5, 0, 10, list.list.y + kRptrRowH + 5) == 1);
  CHECK(rptrRowAt(list, 5, 0, 10, list.head.y) == -1);
  CHECK(rptrRowAt(list, 5, 0, 10, list.list.bottom()) == -1);
  CHECK(rptrRowAt(list, 5, 17, 10, list.list.y + 2) == 0);   // scrolled: row 0 is partly gone
  CHECK(rptrMaxScroll(list, 4) == 0);
  CHECK(rptrMaxScroll(list, 20) == 20 * kRptrRowH - list.list.h);

  RptrHitResult h = rptrHitAt(list, false, 5, 0, list.scan.x + 2, list.scan.y + 2);
  CHECK(h.kind == RptrHit::Scan);
  h = rptrHitAt(list, false, 5, 0, 10, list.list.y + 3);
  CHECK(h.kind == RptrHit::Row && h.row == 0);
  h = rptrHitAt(list, false, 5, 0, 10, list.head.y + 2);
  CHECK(h.kind == RptrHit::None);

  const RptrLayout det = rptrLayoutFor(L, true);
  CHECK(det.back.y >= L.status.bottom());
  CHECK(det.back.right() < det.title.x);
  CHECK(det.title.right() <= L.width);
  CHECK(det.infoCard.y > det.title.bottom());
  CHECK(det.infoCard.bottom() < det.loginLabel.y);
  CHECK(det.loginField.right() < det.loginButton.x);
  CHECK(det.loginButton.x == det.loginField.right() + 8);
  CHECK(det.loginButton.right() <= L.width);
  CHECK(det.note.bottom() <= L.tabbar.y);
  CHECK(rptrHitAt(det, true, 0, 0, det.back.x + 2, det.back.y + 2).kind == RptrHit::Back);
  CHECK(rptrHitAt(det, true, 0, 0, det.loginField.x + 2, det.loginField.y + 2).kind ==
        RptrHit::LoginField);
  CHECK(rptrHitAt(det, true, 0, 0, det.loginButton.x + 2, det.loginButton.y + 2).kind ==
        RptrHit::LoginButton);
}

int main() {
  testMapScale();
  testMapChrome();
  testMapHit();
  testRptr();
  if (g_fail) {
    printf("est351: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("est351: map chrome and RPTR layout ok\n");
  return 0;
}
