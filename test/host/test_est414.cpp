// EST-414 host test: the geometry and the rules behind Filip's five glass-test changes. The source
// half (which screen calls what, and the three wiring fixes) is test_est414.py.
//
//   1. the scroll indicator: track width and place from the list's own layout rectangle, the thumb's
//      length = visible / total and its top = scroll / max, top / middle / end, the bottom-anchored
//      chat band (thumb at the bottom at rest), the minimum thumb, and NO indicator when the content
//      fits;
//   2. the end bounce: ONE give-and-snap rule (listGiveScroll / listSnapScroll) with every old name
//      answering the same numbers, and the nodes-list min / max behaviour the ticket asks for;
//   3. the indicator's track never covers text (kScrollTrackW fits inside every list's right pad).
#include <stdio.h>

#include "ui/chat_layout.h"
#include "ui/layout.h"
#include "ui/node_info_layout.h"
#include "ui/scroll_indicator.h"
#include "ui/settings_layout.h"
#include "ui/theme.h"

using namespace om::ui;

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// --- 1. the scroll indicator -----------------------------------------------------------------------
static void testIndicatorFits() {
  const Rect vp{0, 44, 320, 171};
  // Five 30 px rows are 150 px in a 171 px body: nothing to scroll, so nothing is drawn.
  ScrollIndicator s = scrollIndicatorFor(vp, 5 * kRowH, vp.h, 0, 0, false);
  CHECK(!s.visible);
  CHECK(s.track.empty());
  CHECK(s.thumb.empty());
  // Exactly full is still "fits": no indicator on the pixel where the last row ends.
  s = scrollIndicatorFor(vp, vp.h, vp.h, 0, 0, false);
  CHECK(!s.visible);
  // One pixel more does not fit, so it is drawn.
  s = scrollIndicatorFor(vp, vp.h + 1, vp.h, 0, 1, false);
  CHECK(s.visible);
  CHECK(!s.track.empty());
  CHECK(!s.thumb.empty());
  // An empty viewport (a list with no room) draws nothing rather than a 0 px track.
  s = scrollIndicatorFor(Rect{0, 44, 320, 0}, 400, 0, 0, 400, false);
  CHECK(!s.visible);
}

static void testIndicatorTopMiddleEnd() {
  // A synthetic 100 px viewport with 400 px of content: maxScroll 300, thumb 25 px, 75 px of travel.
  // Round numbers, so every expected value below is exact and any off-by-one in the rule shows.
  const Rect vp{0, 20, 320, 100};
  const int total = 400;
  const int max = total - vp.h;   // 300
  CHECK(max == 300);

  ScrollIndicator top = scrollIndicatorFor(vp, total, vp.h, 0, max, false);
  CHECK(top.visible);
  CHECK(top.track.x == vp.right() - kScrollTrackW);   // flush on the right edge
  CHECK(top.track.y == vp.y);
  CHECK(top.track.w == kScrollTrackW);
  CHECK(top.track.h == vp.h);
  CHECK(top.thumb.w == kScrollTrackW);       // the same width as the track, one value everywhere
  CHECK(top.thumb.h == 100 * 100 / 400);     // visible / total = 25 px
  CHECK(top.thumb.h == 25);
  CHECK(top.thumb.x == top.track.x);
  CHECK(top.thumb.y == vp.y);                // the top of the content is the top of the track

  ScrollIndicator mid = scrollIndicatorFor(vp, total, vp.h, 150, max, false);
  CHECK(mid.thumb.h == 25);                              // the size never changes with the scroll
  CHECK(mid.thumb.y == vp.y + (100 - 25) * 150 / 300);   // scroll / max of the travel = 37
  CHECK(mid.thumb.y == vp.y + 37);

  ScrollIndicator end = scrollIndicatorFor(vp, total, vp.h, max, max, false);
  CHECK(end.thumb.y == vp.y + 100 - 25);     // the end of the content is the track's floor
  CHECK(end.thumb.bottom() == vp.bottom());

  // The thumb is always inside the track, for every offset the finger can produce (including the
  // kSelGive stretch, which the caller may pass in as a negative or past-max scroll).
  for (int scroll = -kSelGive; scroll <= max + kSelGive; ++scroll) {
    const ScrollIndicator s = scrollIndicatorFor(vp, total, vp.h, scroll, max, false);
    CHECK(s.visible);
    CHECK(s.thumb.y >= s.track.y);
    CHECK(s.thumb.bottom() <= s.track.bottom());
  }
}

static void testIndicatorBottomAnchored() {
  // The open chat's message band: scroll 0 is the NEWEST message, so the rest position is the floor.
  const Rect vp{0, 52, 320, 100};
  const int total = 400;
  const int max = total - vp.h;   // 300
  ScrollIndicator rest = scrollIndicatorFor(vp, total, vp.h, 0, max, true);
  CHECK(rest.visible);
  CHECK(rest.thumb.h == 25);
  CHECK(rest.thumb.y == vp.bottom() - 25);   // at rest: the thumb sits at the bottom
  CHECK(rest.thumb.bottom() == vp.bottom());

  ScrollIndicator mid = scrollIndicatorFor(vp, total, vp.h, 150, max, true);
  CHECK(mid.thumb.y == vp.y + 37);           // half way up the history is half way up the track

  ScrollIndicator oldest = scrollIndicatorFor(vp, total, vp.h, max, max, true);
  CHECK(oldest.thumb.y == vp.y);             // the oldest message is the top of the content

  // The same view drawn as a top-down list would put the thumb at the TOP at rest: the flag is what
  // the chat passes and the Nodes list does not.
  const ScrollIndicator wrongEnd = scrollIndicatorFor(vp, total, vp.h, 0, max, false);
  CHECK(wrongEnd.thumb.y == vp.y);
}

static void testIndicatorMinThumb() {
  // A very long list: the proportional thumb would round to 0 or 1 px; it is held at
  // kScrollMinThumbH so there is always something to read as a thumb.
  const Rect vp{0, 20, 320, 100};
  const int total = 100000;
  const int max = total - vp.h;
  const ScrollIndicator top = scrollIndicatorFor(vp, total, vp.h, 0, max, false);
  CHECK(top.thumb.h == kScrollMinThumbH);
  const ScrollIndicator end = scrollIndicatorFor(vp, total, vp.h, max, max, false);
  CHECK(end.thumb.h == kScrollMinThumbH);
  CHECK(end.thumb.bottom() == vp.bottom());
  // A viewport smaller than the minimum thumb still cannot exceed the track.
  const ScrollIndicator tiny = scrollIndicatorFor(Rect{0, 20, 320, 5}, 1000, 5, 0, 995, false);
  CHECK(tiny.thumb.h == 5);
}

static void testIndicatorUsesEveryLayout() {
  // The three lists the ticket names, each with the rectangle its OWN layout hands out. The track's
  // width and its offset from the viewport's right edge are the same on all three.
  const Layout& L = l2();

  // Nodes: the list body under the one-line header.
  const Rect nodes = L.listBody();
  const ScrollIndicator n = scrollIndicatorFor(nodes, 20 * kRowH, nodes.h, 0, L.maxScroll(20), false);
  CHECK(n.visible);
  CHECK(n.track.w == kScrollTrackW);
  CHECK(n.track.right() == nodes.right());

  // The chat's channel list and message band, from chatLayoutFor().
  const ChatLayout g = chatLayoutFor(L, false, false, false, true);
  const int chans = 8;
  const int dms = 2;
  const Rect list = g.selList;
  const ScrollIndicator c =
      scrollIndicatorFor(list, selContentHeight(chans, dms), list.h, 0,
                         selMaxScroll(list, chans, dms), false);
  CHECK(c.visible);
  CHECK(c.track.w == kScrollTrackW);
  CHECK(c.track.right() == list.right());

  const ChatLayout g2 = chatLayoutFor(L, false, false, false, false);
  const int16_t heights[8] = {28, 14, 42, 28, 56, 14, 28, 42};
  const int total = msgContentHeight(heights, 8);
  const ScrollIndicator m = scrollIndicatorFor(g2.msgs, total, g2.msgs.h, 0,
                                               msgMaxScroll(g2.msgs, heights, 8), true);
  CHECK(m.visible);   // 8 messages at 64 ms each do not fit the band
  CHECK(m.track.w == kScrollTrackW);
  CHECK(m.track.right() == g2.msgs.right());
  CHECK(m.thumb.bottom() == g2.msgs.bottom());   // rest = bottom, as the ticket asks
}

// --- 3. never over text ----------------------------------------------------------------------------
static void testTrackStaysOutOfText() {
  // Every list the indicator is drawn on reserves a right pad at least as wide as the track, so the
  // last glyph of a row/line ends before the track starts. These are the three pads theme.h names.
  CHECK(kScrollTrackW <= kRowPadX);     // Nodes rows
  CHECK(kScrollTrackW <= kSelPadX);     // chat channel rows
  CHECK(kScrollTrackW <= kMsgPadX);     // message lines
  CHECK(kScrollTrackW < kRowPadX);      // at least 1 px of air, not merely flush
  CHECK(kScrollTrackW == 4);            // the S492 width the ticket names
  CHECK(kScrollThumb == kAcc);          // the OM gold thumb
  CHECK(kScrollTrack == kLine);         // the separator grey track
}

// --- 2. the end bounce -----------------------------------------------------------------------------
static void testBounceRule() {
  const int max = 400;
  // The list follows the finger: a finger that went UP grows the scroll.
  CHECK(listGiveScroll(100, 20, max) == 120);
  CHECK(listGiveScroll(100, -20, max) == 80);
  CHECK(listGiveScroll(100, 0, max) == 100);
  // The give past either end is kSelGive, and not one pixel more.
  CHECK(listGiveScroll(0, -15, max) == -kSelGive);
  CHECK(listGiveScroll(max, 15, max) == max + kSelGive);
  CHECK(listGiveScroll(0, -3, max) == -3);          // a small stretch is shown as it is
  CHECK(listGiveScroll(max, 3, max) == max + 3);
  // The release snaps the stretch back into range, and leaves a live offset alone.
  CHECK(listSnapScroll(-kSelGive, max) == 0);
  CHECK(listSnapScroll(max + kSelGive, max) == max);
  CHECK(listSnapScroll(0, max) == 0);
  CHECK(listSnapScroll(max, max) == max);
  CHECK(listSnapScroll(123, max) == 123);
  // A list that does not scroll at all: the drag still stretches, the release returns to 0.
  CHECK(listGiveScroll(0, 7, 0) == 7);
  CHECK(listGiveScroll(0, -7, 0) == -7);
  CHECK(listSnapScroll(7, 0) == 0);

  // The message band is bottom-anchored: main.cpp passes -dy, so a finger moving DOWN (dy < 0) has
  // to GROW the scroll (into older messages) and a finger moving up shrinks it, up to the give.
  const int bandBase = 0;
  CHECK(listGiveScroll(bandBase, -(-30), max) == 30);
  CHECK(listGiveScroll(bandBase, -(30), max) == -kSelGive);

  // Every old name is the SAME rule, so no screen can drift: the EST-400 Info page, the Settings
  // pages and the chat list answer exactly what layout.h answers.
  for (int base = -20; base <= max + 20; base += 17) {
    for (int dy = -40; dy <= 40; dy += 13) {
      CHECK(nodeInfoDragScroll(base, dy, max) == listGiveScroll(base, dy, max));
      CHECK(settingsDragScroll(base, dy, max) == listGiveScroll(base, dy, max));
    }
    CHECK(nodeInfoSnapScroll(base, max) == listSnapScroll(base, max));
    CHECK(settingsSnapScroll(base, max) == listSnapScroll(base, max));
  }
}

static void testNodesListBounceEnds() {
  // The Nodes list's own geometry, through the rule it now shares: at the last row a drag past the
  // end shows the give (this is the state refreshNodes used to clamp away, which is why the list
  // stopped hard at its last row), and the release returns the last row exactly to the body's floor.
  const Layout& L = l2();
  const int count = 20;
  const int max = L.maxScroll(count);
  CHECK(max > 0);
  const int dragged = listGiveScroll(max, 25, max);   // finger pushed UP past the last row
  CHECK(dragged == max + kSelGive);
  CHECK(listSnapScroll(dragged, max) == max);
  const int pulled = listGiveScroll(0, -7, max);       // finger pulled DOWN at the first row
  CHECK(pulled == -7);
  CHECK(listSnapScroll(pulled, max) == 0);
}

int main() {
  testIndicatorFits();
  testIndicatorTopMiddleEnd();
  testIndicatorBottomAnchored();
  testIndicatorMinThumb();
  testIndicatorUsesEveryLayout();
  testTrackStaysOutOfText();
  testBounceRule();
  testNodesListBounceEnds();
  if (g_fail == 0) {
    printf("est414: indicator geometry on all three lists, one bounce rule, no track over text\n");
    return 0;
  }
  printf("est414: %d failure(s)\n", g_fail);
  return 1;
}
