// EST-351 host test: the map chrome's key rectangles and hit test, the outlined tile attribution and
// the RPTR page rectangles. EST-352 keeps: the RPTR row's labelled second line, the native RGB565
// byte-order round trip and the shared keyboard sheet's per-field filters, limits and geometry.
// EST-368 removed the distance scale and its two end-anchored labels; the Traces key joined the
// column and the attribution moved to the bottom left, without a plate. Plain g++, no Arduino.
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

static void testMapChrome() {
  const Layout& L = l2();
  const MapChrome c = mapChromeFor(L, false);
  CHECK(c.view.x == 0 && c.view.y == 0 && c.view.w == 320 && c.view.h == L.tabbar.y);
  CHECK(c.view.bottom() == L.tabbar.y);
  CHECK(c.chip.y >= L.status.bottom());          // the chip never hides under the status bar
  CHECK(c.chip.right() <= c.view.right());
  CHECK(c.zoomIn.right() == c.view.right() - kMapMargin);
  CHECK(c.zoomOut.right() == c.zoomIn.right());
  CHECK(c.lock.right() == c.zoomIn.right());
  CHECK(c.traces.right() == c.zoomIn.right());
  CHECK(c.zoomOut.bottom() == c.view.bottom() - kMapMargin);
  CHECK(c.zoomOut.y == c.zoomIn.bottom() + kMapKeyGap);
  CHECK(c.zoomIn.y == c.lock.bottom() + kMapKeyGap);
  CHECK(c.lock.y == c.traces.bottom() + kMapKeyGap);   // EST-368: Traces is the top key
  CHECK(!c.zoomIn.overlaps(c.zoomOut));
  CHECK(!c.lock.overlaps(c.zoomIn));
  CHECK(!c.traces.overlaps(c.lock));
  CHECK(c.traces.y >= c.chip.bottom());                // the column stays below the chip
  CHECK(c.callout.empty());                            // nothing selected: no panel

  const MapChrome s = mapChromeFor(L, true);
  CHECK(!s.callout.empty());
  CHECK(s.callout.bottom() == c.view.bottom() - 8);
  CHECK(s.callout.x == 8 && s.callout.right() == 312);
}

static void testMapHit() {
  const MapChrome c = mapChromeFor(l2(), true);

  CHECK(mapHitAt(c, nullptr, 0, c.zoomIn.x + c.zoomIn.w / 2, c.zoomIn.y + c.zoomIn.h / 2).kind ==
        MapHitKind::ZoomIn);
  CHECK(mapHitAt(c, nullptr, 0, c.zoomOut.x + c.zoomOut.w / 2,
                 c.zoomOut.y + c.zoomOut.h / 2).kind == MapHitKind::ZoomOut);
  CHECK(mapHitAt(c, nullptr, 0, c.lock.x + c.lock.w / 2, c.lock.y + c.lock.h / 2).kind ==
        MapHitKind::Lock);
  CHECK(mapHitAt(c, nullptr, 0, c.traces.x + c.traces.w / 2,
                 c.traces.y + c.traces.h / 2).kind == MapHitKind::Traces);
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

  // EST-366 rule 2: one page layout, one scrolling column. A fixed header (back, title), then the
  // status card, the clock banner and the rows as content above the one Apply button.
  const RptrPageLayout a = rptrPageLayoutFor(L, true, true, true, false, false);
  CHECK(a.back.y >= L.status.bottom());
  CHECK(a.back.right() < a.title.x);
  CHECK(a.title.right() <= L.width);
  CHECK(a.card.y > a.title.bottom());
  CHECK(a.banner.y > a.card.bottom());
  CHECK(a.rowsY >= a.banner.bottom());
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

static void testMapCredit() {
  // EST-368: the attribution is drawn straight on the tiles with a 1 px outline, no plate. It sits on
  // the bottom LEFT (where the removed scale was), clamped inside the map area and clear of the key
  // column. The mono advance the map draws with is 6 px.
  const int creditW = 6 * (int)strlen(kMapCreditText);
  CHECK(creditW == 138);   // "Esri, HERE, Garmin, OSM", 23 glyphs

  for (int zoom = 9; zoom <= 18; ++zoom) {
    const MapChrome c = mapChromeFor(l2(), false);
    const Rect cr = mapCreditRect(c, (int16_t)creditW);
    CHECK(!cr.empty());
    // Bottom left, one margin in from the edge.
    CHECK(cr.x == c.view.x + kMapMargin);
    CHECK(cr.bottom() == c.view.bottom() - kMapMargin);
    CHECK(cr.w == creditW);
    // Inside the map area and off every key and the chip.
    CHECK(cr.right() <= c.view.right());
    CHECK(cr.y >= c.status.bottom());
    CHECK(!cr.overlaps(c.zoomIn) && !cr.overlaps(c.zoomOut) && !cr.overlaps(c.lock) &&
          !cr.overlaps(c.traces));
    CHECK(!cr.overlaps(c.chip));
  }

  // The numbers quoted in PROGRESS.md at 320 px: the box starts at 6 and is 138 wide.
  {
    const MapChrome c = mapChromeFor(l2(), false);
    const Rect cr = mapCreditRect(c, (int16_t)creditW);
    CHECK(cr.x == 6);
    CHECK(cr.right() == 144);
    CHECK(cr.bottom() == 209);   // 215 (the tab bar's top) - 6
    CHECK(cr.y == 199);
  }

  // A box too narrow for the key column: the rectangle is clamped, never drawn under a key.
  {
    const MapChrome c = mapChromeFor(l2(), false);
    const Rect cr = mapCreditRect(c, 400);
    CHECK(cr.right() <= c.zoomOut.x - kMapMargin);
    CHECK(cr.x >= c.view.x);
  }
  // Nothing to draw: no rectangle.
  CHECK(mapCreditRect(mapChromeFor(l2(), false), 0).empty());
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
  testMapChrome();
  testMapHit();
  testRptr();
  testRgb565RoundTrip();
  testMapCredit();
  testKeyboardFilters();
  testKeyboardSheet();
  if (g_fail) {
    printf("est351: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("est351: map chrome (no scale), outlined credit placement, RPTR layout, RGB565 round trip and the shared keyboard sheet ok\n");
  return 0;
}
