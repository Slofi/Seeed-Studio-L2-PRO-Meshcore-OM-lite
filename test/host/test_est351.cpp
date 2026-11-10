// EST-351 host test: the map chrome's scale arithmetic, key rectangles and hit test; the RPTR list
// and page rectangles. EST-352 adds: the scale's two end-anchored labels, the RPTR row's labelled
// second line, the native RGB565 byte-order round trip and the shared keyboard sheet's per-field
// filters, limits and geometry. Plain g++, no Arduino.
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <string.h>

#include "ui/chat_layout.h"
#include "ui/map_chrome.h"
#include "ui/rgb565.h"
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
  const RptrListLayout list = rptrListLayoutFor(L);
  CHECK(list.head.y == L.status.bottom());
  CHECK(list.head.bottom() == list.list.y);
  CHECK(list.head.h == kRptrHeadH);
  CHECK(list.list.bottom() == L.tabbar.y);
  CHECK(list.scan.right() <= L.width);
  CHECK(list.scan.y >= list.head.y && list.scan.bottom() <= list.head.bottom());
  CHECK(list.visibleRows >= 4);

  // Rows tile the list, adjacent, only the visible part answers.
  const RptrRow r0 = rptrListRow(list, 0, 0);
  const RptrRow r1 = rptrListRow(list, 1, 0);
  CHECK(r0.y == list.list.y);
  CHECK(r1.y == r0.y + kRptrRowH);
  CHECK(rptrListRowAt(list, 5, 0, 10, list.list.y + 5) == 0);
  CHECK(rptrListRowAt(list, 5, 0, 10, list.list.y + kRptrRowH + 5) == 1);
  CHECK(rptrListRowAt(list, 5, 0, 10, list.head.y) == -1);
  CHECK(rptrListRowAt(list, 5, 0, 10, list.list.bottom()) == -1);
  CHECK(rptrListRowAt(list, 5, 17, 10, list.list.y + 2) == 0);   // scrolled: row 0 is partly gone
  CHECK(rptrListMaxScroll(list, 4) == 0);
  CHECK(rptrListMaxScroll(list, 20) == 20 * kRptrRowH - list.list.h);

  RptrHitResult h = rptrListHitAt(list, 5, 0, list.scan.x + 2, list.scan.y + 2);
  CHECK(h.kind == RptrHit::Scan);
  h = rptrListHitAt(list, 5, 0, 10, list.list.y + 3);
  CHECK(h.kind == RptrHit::ListRow && h.row == 0);
  h = rptrListHitAt(list, 5, 0, 10, list.head.y + 2);
  CHECK(h.kind == RptrHit::None);

  // EST-362: one page layout. A back key, a title, the rows and at most one Apply button; the
  // status card and the clock banner sit above the rows and never overlap them.
  const RptrPageLayout a = rptrPageLayoutFor(L, true, true, true, false, false);
  CHECK(a.back.y >= L.status.bottom());
  CHECK(a.back.right() < a.title.x);
  CHECK(a.title.right() <= L.width);
  CHECK(a.card.y > a.title.bottom());
  CHECK(a.banner.y > a.card.bottom());
  CHECK(a.body.y >= a.banner.bottom());
  CHECK(a.body.bottom() <= a.apply.y);
  CHECK(a.apply.bottom() <= L.tabbar.y);
  CHECK(a.visibleRows >= 1);   // card + clock banner + Apply: the rows scroll
  const RptrPageLayout plain = rptrPageLayoutFor(L, false, false, true, false, false);
  CHECK(plain.visibleRows >= 3);  om::rptr::ScreenOut screen;
  CHECK(rptrPageHitAt(a, screen, 0, a.back.x + 2, a.back.y + 2).kind == RptrHit::Back);
  CHECK(rptrPageHitAt(a, screen, 0, a.apply.x + 2, a.apply.y + 2).kind == RptrHit::Apply);
  CHECK(rptrPageHitAt(a, screen, 0, a.refresh.x + 2, a.refresh.y + 2).kind == RptrHit::Refresh);
  CHECK(rptrPageHitAt(a, screen, 0, a.sub.x + 2, a.sub.y + 2).kind == RptrHit::None);

  // The confirmation's own geometry: two buttons, everything else cancels.
  const RptrModalLayout m = rptrModalLayoutFor(L, "Restart");
  CHECK(m.box.right() <= L.width && m.box.bottom() <= L.height);
  CHECK(rptrModalHitAt(m, m.cancel.x + 2, m.cancel.y + 2) == RptrModalHit::Cancel);
  CHECK(rptrModalHitAt(m, m.confirm.x + 2, m.confirm.y + 2) == RptrModalHit::Confirm);
  CHECK(rptrModalHitAt(m, 0, 0) == RptrModalHit::Outside);
}

// --- EST-352 ------------------------------------------------------------------------------------

static void testRgb565RoundTrip() {
  // The theme gold, #e8b04f, is the frame probe's own colour.
  CHECK(rgb565FromRgb(232, 176, 79) == 0xED89);
  CHECK(rgb565Swab(0xED89) == 0x89ED);
  // The frame sprite stores rgb565_2Byte (bytes exchanged); a native push must end up stored that
  // way, and the screenshot read-back must undo it. Known colour in -> same colour out.
  CHECK(rgb565FrameStore(0xED89) == 0x89ED);
  CHECK(rgb565FrameRead(rgb565FrameStore(0xED89)) == 0xED89);
  // A tile colour from the topo (water blue): the same round trip, not just gold.
  const uint16_t water = rgb565FromRgb(170, 211, 223);
  CHECK(rgb565FrameRead(rgb565FrameStore(water)) == water);
  // If the push forgot the conversion (stored the native word), the read-back would be the swapped
  // colour: exactly the magenta/cyan the device showed. The test catches that mistake.
  CHECK(rgb565FrameRead(0xED89) != 0xED89);
  bool identity = true;
  for (uint32_t v = 0; v <= 0xFFFF; ++v) {
    if (rgb565FrameRead(rgb565FrameStore((uint16_t)v)) != (uint16_t)v) identity = false;
  }
  CHECK(identity);
}

static void testScaleLabels() {
  // z13 near Ljubljana: the bar is 75 px; "500m" + one space + "1km" fit at the two ends.
  int16_t px = 0;
  const int32_t metres = mapScaleDistance(13, 46.05, &px);
  char mid[12], right[12];
  mapScaleLabels(metres, mid, sizeof(mid), right, sizeof(right));
  const int midW = 6 * (int)strlen(mid);     // mono advance, the same face the map draws with
  const int rightW = 6 * (int)strlen(right);
  const Rect bar{6, (int16_t)(l2().tabbar.y - 6 - kMapScaleH), px, kMapScaleH};
  const MapScaleLabelLayout l = mapScaleLabelLayout(bar, midW, rightW, 6);
  CHECK(l.showMid);
  CHECK(l.midX == bar.x);                                  // the half label at the left end
  CHECK(l.rightX == bar.right() - rightW);                 // the end label at the right end
  CHECK((int)l.midX + midW + 6 <= (int)l.rightX);          // at least one measured space
  CHECK(l.rightX + rightW <= bar.right());                 // the end label never runs past the rail

  // A rail too short for both: only the end label.
  const Rect shortBar{6, bar.y, (int16_t)(midW + 6 + rightW - 1), kMapScaleH};
  CHECK(!mapScaleLabelLayout(shortBar, midW, rightW, 6).showMid);
  // Exactly enough room: both, with the one space touching.
  const Rect exact{6, bar.y, (int16_t)(midW + 6 + rightW), kMapScaleH};
  const MapScaleLabelLayout e = mapScaleLabelLayout(exact, midW, rightW, 6);
  CHECK(e.showMid);
  CHECK((int)e.midX + midW + 6 == (int)e.rightX);
  // No half label (an empty string): nothing to show.
  CHECK(!mapScaleLabelLayout(bar, 0, rightW, 6).showMid);
}

static void testMapCredit() {
  // EST-353: the tile attribution moved off the scale's line pair. Same mono advance (6 px) the map
  // draws with. The scale's labels are centred at scale.bottom() - 1 (drawTextLeft's middle datum),
  // so their boxes are 10 px tall like the credit's.
  const Layout& L = l2();
  const int creditW = 6 * (int)strlen(kMapCreditText);
  CHECK(creditW == 138);   // "Esri, HERE, Garmin, OSM", 23 glyphs

  for (int zoom = 9; zoom <= 18; ++zoom) {
    const MapChrome c = mapChromeFor(L, zoom, 46.05, false);
    char mid[12], right[12];
    mapScaleLabels(mapScaleDistance(zoom, 46.05, nullptr), mid, sizeof(mid), right, sizeof(right));
    const int midW = 6 * (int)strlen(mid);
    const int rightW = 6 * (int)strlen(right);
    const MapScaleLabelLayout sl = mapScaleLabelLayout(c.scale, midW, rightW, 6);
    const int16_t cy = (int16_t)(c.scale.bottom() - 1);
    const Rect midBox = sl.showMid ? Rect{sl.midX, (int16_t)(cy - 5), (int16_t)midW, 10} : Rect{};
    const Rect rightBox{sl.rightX, (int16_t)(cy - 5), (int16_t)rightW, 10};
    const Rect cr = mapCreditRect(c, (int16_t)creditW);

    // On the bottom line, right-aligned to the key column, and clear of the scale by a measured gap.
    CHECK(!cr.empty());
    CHECK(cr.right() == c.zoomOut.x - kMapMargin);
    CHECK(cr.y + cr.h / 2 == cy);
    CHECK((int)cr.x >= (int)c.scale.right() + kMapCreditGap);
    CHECK(!cr.overlaps(c.scale));
    CHECK(!cr.overlaps(rightBox));
    if (sl.showMid) CHECK(!cr.overlaps(midBox));
    // Inside the map area and clear of every key and the chip (nothing to tab over).
    CHECK(cr.x >= c.view.x && cr.right() <= c.view.right());
    CHECK(cr.y >= c.status.bottom());
    CHECK(!cr.overlaps(c.zoomIn) && !cr.overlaps(c.zoomOut) && !cr.overlaps(c.lock));
    CHECK(!cr.overlaps(c.chip));
  }

  // The numbers quoted in PROGRESS.md at z13: credit 146..284, the scale ends at 81.
  {
    const MapChrome c = mapChromeFor(L, 13, 46.05, false);
    const Rect cr = mapCreditRect(c, (int16_t)creditW);
    CHECK(cr.x == 146);
    CHECK(cr.right() == 284);
    CHECK(c.scale.right() == 81);
    CHECK((int)cr.x - (int)c.scale.right() == 65);
  }

  // A screen too narrow for both on one line: the credit moves up, it never prints on a label.
  {
    const MapChrome c = mapChromeFor(L, 13, 46.05, false);
    const Rect cr = mapCreditRect(c, 300);
    CHECK(!cr.overlaps(c.scale));
    CHECK(cr.x == c.view.x);                       // clamped to the left edge
    CHECK(cr.bottom() <= c.scale.y);               // above the scale's rail
    CHECK(cr.y >= c.status.bottom());
  }
  // Nothing to draw: no rectangle.
  CHECK(mapCreditRect(mapChromeFor(L, 13, 46.05, false), 0).empty());
}

static void testKeyboardFilters() {
  // Names: MeshCore's isValidName rejects exactly [ ] \ : , ? * and takes the rest of printable ASCII.
  for (char c = 0x20; c < 0x7F; ++c) {
    const bool expected = c != '[' && c != ']' && c != '\\' && c != ':' && c != ',' &&
                          c != '?' && c != '*';
    CHECK(keyboardAllowsChar(KeyboardField::NodeName, c) == expected);
  }
  CHECK(keyboardAllowsChar(KeyboardField::NodeName, ' '));
  CHECK(keyboardAllowsChar(KeyboardField::NodeName, '.'));
  CHECK(keyboardAllowsChar(KeyboardField::NodeName, '-'));
  CHECK(keyboardAllowsChar(KeyboardField::NodeName, '_'));
  CHECK(!keyboardAllowsChar(KeyboardField::NodeName, '\n'));

  // Base64: letters, digits, + / = and nothing else (no space, comma, dot or dash).
  CHECK(keyboardAllowsChar(KeyboardField::Base64Key, 'A'));
  CHECK(keyboardAllowsChar(KeyboardField::Base64Key, 'z'));
  CHECK(keyboardAllowsChar(KeyboardField::Base64Key, '0'));
  CHECK(keyboardAllowsChar(KeyboardField::Base64Key, '+'));
  CHECK(keyboardAllowsChar(KeyboardField::Base64Key, '/'));
  CHECK(keyboardAllowsChar(KeyboardField::Base64Key, '='));
  CHECK(!keyboardAllowsChar(KeyboardField::Base64Key, ' '));
  CHECK(!keyboardAllowsChar(KeyboardField::Base64Key, ','));
  CHECK(!keyboardAllowsChar(KeyboardField::Base64Key, '.'));
  CHECK(!keyboardAllowsChar(KeyboardField::Base64Key, '-'));
  CHECK(!keyboardAllowsChar(KeyboardField::Base64Key, '_'));

  // Lengths are BYTES: MeshCore's name buffers are 32 bytes, a 16-byte key is 24 base64 chars.
  CHECK(keyboardMaxBytes(KeyboardField::NodeName) == 31);
  CHECK(keyboardMaxBytes(KeyboardField::Base64Key) == 24);
  CHECK(keyboardMaxBytes(KeyboardField::Chat) == 0);
  CHECK(keyboardAcceptsBytes(KeyboardField::NodeName, 30, 1));
  CHECK(!keyboardAcceptsBytes(KeyboardField::NodeName, 31, 1));
  CHECK(!keyboardAcceptsBytes(KeyboardField::NodeName, 30, 2));   // an accented letter is 2 bytes
  CHECK(keyboardAcceptsBytes(KeyboardField::Base64Key, 23, 1));
  CHECK(!keyboardAcceptsBytes(KeyboardField::Base64Key, 24, 1));

  CHECK(keyboardAccents(KeyboardField::NodeName));
  CHECK(!keyboardAccents(KeyboardField::Base64Key));
  CHECK(keyboardEmoji(KeyboardField::Chat));
  CHECK(!keyboardEmoji(KeyboardField::NodeName));
  CHECK(!keyboardEmoji(KeyboardField::Base64Key));
}

static bool rowHasKind(bool nums, int row, KeyboardField f, KeyKind kind) {
  KeyDef defs[10];
  const int n = keyRow(nums, row, defs, f);
  for (int i = 0; i < n; ++i) if (defs[i].kind == kind) return true;
  return false;
}

static bool rowHasChar(bool nums, int row, KeyboardField f, char ch) {
  KeyDef defs[10];
  const int n = keyRow(nums, row, defs, f);
  for (int i = 0; i < n; ++i) {
    if (defs[i].kind == KeyKind::Char && defs[i].label[0] == ch) return true;
  }
  return false;
}

static void testKeyboardSheet() {
  const Layout& L = l2();
  // Every field and page draws a sheet with non-empty rows, and every character key it offers is
  // one the field accepts.
  const KeyboardField fields[] = {KeyboardField::Chat, KeyboardField::NodeName,
                                  KeyboardField::Base64Key};
  for (KeyboardField f : fields) {
    for (int nums = 0; nums <= 1; ++nums) {
      const ChatLayout g = chatLayoutFor(L, true, false, nums != 0, false, f);
      CHECK(!g.sheet.empty());
      CHECK(g.sheet.y == L.height - g.sheet.h);       // bottom-anchored, the same for every field
      CHECK(g.sheet.bottom() == L.height);
      for (int r = 0; r < 4; ++r) {
        KeyDef defs[10];
        const int n = keyRow(nums != 0, r, defs, f);
        CHECK(n > 0);
        CHECK(n <= 10);
        CHECK((int)g.keyCount[r] == n);
        CHECK(g.keys[r][n - 1].right() == L.width - kOskPad);   // the row tiles the sheet exactly
        for (int c = 0; c < n; ++c) {
          CHECK(g.keys[r][c].x >= 0 && g.keys[r][c].right() <= L.width);
          CHECK(g.keys[r][c].y >= g.sheet.y && g.keys[r][c].bottom() <= g.sheet.bottom());
          if (defs[c].kind == KeyKind::Char) {
            CHECK(keyboardAllowsChar(f, defs[c].label[0]));
          }
          // One rectangle drives drawing and touch: the centre of each key hits that key.
          const KeyHit hit = keyAt(g, nums != 0, g.keys[r][c].x + g.keys[r][c].w / 2,
                                   g.keys[r][c].y + g.keys[r][c].h / 2, f);
          CHECK(hit.row == r && hit.col == c);
        }
      }
      // The base64 key's 123 page carries + / = as the ticket requires; the name's 123 page carries
      // digits. Neither editor offers the emoji page, but the chat still does.
      if (f == KeyboardField::Base64Key && nums) {
        CHECK(rowHasChar(true, 1, f, '+'));
        CHECK(rowHasChar(true, 1, f, '/'));
        CHECK(rowHasChar(true, 1, f, '='));
      }
      if (f == KeyboardField::NodeName && !nums) {
        CHECK(rowHasKind(false, 3, f, KeyKind::Space));    // a name takes a space
        CHECK(!rowHasChar(false, 3, f, ','));              // MeshCore forbids a comma in a name
        CHECK(rowHasChar(false, 3, f, '.'));               // but a dot is fine
        CHECK(rowHasKind(false, 2, f, KeyKind::Shift));
        CHECK(rowHasKind(false, 2, f, KeyKind::Del));
      }
      if (f == KeyboardField::Base64Key && !nums) {
        CHECK(!rowHasKind(false, 3, f, KeyKind::Space));
        CHECK(!rowHasChar(false, 3, f, ','));
        CHECK(rowHasKind(false, 3, f, KeyKind::Send));
      }
      if (f != KeyboardField::Chat) {
        for (int r = 0; r < 4; ++r) CHECK(!rowHasKind(nums != 0, r, f, KeyKind::Emoji));
      }
      // A tap above the sheet is not a key.
      CHECK(keyAt(g, nums != 0, 160, g.sheet.y - 1, f).row < 0);
    }
  }
  // The chat's own rows are unchanged by the refactor: same counts as EST-346.
  KeyDef defs[10];
  CHECK(keyRow(false, 0, defs) == 10);
  CHECK(keyRow(false, 1, defs) == 9);
  CHECK(keyRow(false, 2, defs) == 9);
  CHECK(keyRow(false, 3, defs) == 6);
  CHECK(keyRow(true, 0, defs) == 10);
  CHECK(keyRow(true, 1, defs) == 10);
  CHECK(keyRow(true, 2, defs) == 10);
  CHECK(keyRow(true, 3, defs) == 6);
  // The sheet's rectangle does not depend on the field: one widget, one height.
  CHECK(chatLayoutFor(L, true, false, false, false, KeyboardField::Chat).sheet.y ==
        chatLayoutFor(L, true, false, false, false, KeyboardField::Base64Key).sheet.y);
  CHECK(chatLayoutFor(L, true, false, false, false, KeyboardField::NodeName).draft.y ==
        chatLayoutFor(L, true, false, false, false, KeyboardField::Chat).draft.y);
}

int main() {
  testMapScale();
  testMapChrome();
  testMapHit();
  testRptr();
  testRgb565RoundTrip();
  testScaleLabels();
  testMapCredit();
  testKeyboardFilters();
  testKeyboardSheet();
  if (g_fail) {
    printf("est351: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("est351: map chrome, credit placement, RPTR layout, RGB565 round trip and the shared keyboard sheet ok\n");
  return 0;
}
