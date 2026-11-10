// Host test for EST-342: the DM frame budget, the delivery state machine from the ACK and the
// companion's timeout, the Nodes long-press timing, the node sheet's rows per node type, the
// new-node notice rules, the message sheet's DM row, the Info page's lines and the Nodes drag sign.
// Plain g++ (no Arduino, no LovyanGFX).
#include <stdio.h>
#include <string.h>

#include "mesh_text.h"
#include "ui/chat_layout.h"
#include "ui/compose_text.h"
#include "ui/dm_delivery.h"
#include "ui/layout.h"
#include "ui/long_press.h"
#include "ui/message_sheet.h"
#include "ui/new_node.h"
#include "ui/node_info.h"
#include "ui/node_menu.h"

using namespace om;
using namespace om::ui;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// --- the DM budget --------------------------------------------------------------------------------
static void testDmBudget() {
  // BaseChatMesh::composeMsgPacket(): 4-byte timestamp + 1 attempt byte, no "name: " prefix.
  CHECK(kMeshDmFrameBytes == 5);
  CHECK(kMeshDmTextMax == 155);
  CHECK(kMeshTextMax - kMeshDmFrameBytes == kMeshDmTextMax);
  // A channel text is still smaller: our own "<name>: " prefix is part of its 160.
  CHECK(meshOwnTextMaxFor("OM Lite") == 151);
  CHECK(composeAllowance(false, nullptr, "OM Lite") == 151);
  CHECK(kMeshDmTextMax > composeAllowance(false, nullptr, "OM Lite"));

  char draft[200];
  memset(draft, 'a', sizeof(draft));
  draft[kMeshDmTextMax] = 0;
  CHECK((int)strlen(draft) == kMeshDmTextMax);
  CHECK(!composeAppend(draft, (int)sizeof(draft), "x", kMeshDmTextMax));   // one byte over: refused
  draft[kMeshDmTextMax - 1] = 0;
  CHECK(composeAppend(draft, (int)sizeof(draft), "x", kMeshDmTextMax));    // exactly full: allowed
  CHECK((int)strlen(draft) == kMeshDmTextMax);
}

// --- delivery: the ACK proves it, the timeout disproves it ---------------------------------------
static void testDelivery() {
  DmDeliveryTable t;
  t.reset();
  CHECK(t.pendingCount() == 0);
  CHECK(t.begin(1, 0xAABBCCDDu, 1000, 0));
  CHECK(t.pending(1));
  CHECK(t.pendingCount() == 1);

  // A different ACK is someone else's DM.
  uint32_t seq = 0, rtt = 0;
  CHECK(!t.ack(0xAABBCC00u, 100, &seq, &rtt));
  CHECK(t.pending(1));

  // The matching ACK: delivered, with the round trip.
  CHECK(t.ack(0xAABBCCDDu, 250, &seq, &rtt));
  CHECK(seq == 1);
  CHECK(rtt == 250);
  CHECK(!t.pending(1));
  CHECK(t.pendingCount() == 0);
  CHECK(!t.ack(0xAABBCCDDu, 300, &seq, &rtt));   // the same ACK twice is not a second event

  // No ACK: one resend per expired attempt, then not confirmed. kMaxAttempts is 4 (attempt 0..3).
  CHECK(t.begin(2, 0x11111111u, 1000, 0));
  uint32_t dueSeq = 0;
  uint8_t attempt = 0;
  CHECK(t.poll(999, &dueSeq, &attempt) == 0);
  CHECK(t.poll(1000, &dueSeq, &attempt) == 1);
  CHECK(dueSeq == 2 && attempt == 1);
  CHECK(t.poll(1001, &dueSeq, &attempt) == 0);   // the retry is not due twice
  CHECK(t.rearm(2, 0x22222222u, 1000, 1000));
  CHECK(t.poll(1999, &dueSeq, &attempt) == 0);
  CHECK(t.poll(2000, &dueSeq, &attempt) == 1);
  CHECK(attempt == 2);
  CHECK(t.rearm(2, 0x33333333u, 1000, 2000));
  CHECK(t.poll(3000, &dueSeq, &attempt) == 1);
  CHECK(attempt == 3);
  CHECK(t.rearm(2, 0x44444444u, 1000, 3000));
  CHECK(t.poll(4000, &dueSeq, &attempt) == 2);   // every attempt spent: not confirmed
  CHECK(dueSeq == 2);
  CHECK(!t.pending(2));

  // A retry's ACK is the NEW frame hash: the old one no longer matches.
  CHECK(t.begin(3, 0xAAAAAAAAu, 1000, 0));
  CHECK(t.poll(1000, &dueSeq, &attempt) == 1);
  CHECK(t.rearm(3, 0xBBBBBBBBu, 1000, 1000));
  CHECK(!t.ack(0xAAAAAAAAu, 1100, &seq, &rtt));
  CHECK(t.ack(0xBBBBBBBBu, 1200, &seq, &rtt));
  CHECK(seq == 3 && rtt == 200);
}

// --- the long press: 450 ms, 12 px, no tap after a hold ------------------------------------------
static void testLongPress() {
  LongPress p;
  CHECK(!p.update(10, 10, 0));   // not down yet

  p.press(10, 10, 0);
  CHECK(!p.update(10, 10, 449));
  CHECK(p.update(10, 10, 450));   // exactly at the threshold fires
  CHECK(!p.update(10, 10, 451));  // once only
  CHECK(!p.release());            // and its release is not a tap

  p.reset();
  p.press(10, 10, 0);
  CHECK(!p.update(10, 10, 400));
  CHECK(p.release());             // released before the threshold: an ordinary tap

  p.reset();
  p.press(10, 10, 0);
  CHECK(!p.update(30, 10, 100));  // 20 px: cancelled by the finger slop
  CHECK(p.cancelled);
  CHECK(!p.update(30, 10, 600));
  CHECK(!p.release());            // a drag is not a tap either

  p.reset();
  p.press(10, 10, 0);
  CHECK(!p.update(18, 16, 449));   // 8 px / 6 px: inside the 12 px slop
  CHECK(p.update(18, 16, 450));

  // A press that never moved and is released exactly at 450 fires first, so no tap follows.
  p.reset();
  p.press(0, 0, 0);
  CHECK(p.update(0, 0, 450));
  CHECK(!p.release());
}

// --- the node sheet's rows, only when they can work ----------------------------------------------
static int findRow(const NodeMenuRow* rows, int n, NodeAction a) {
  for (int i = 0; i < n; ++i) {
    if (rows[i].action == a) return i;
  }
  return -1;
}

static void testNodeMenu() {
  NodeMenuRow rows[kMaxNodeMenuRows];
  NodeMenuContext c;
  c.advType = kAdvChat;
  c.hasKey = true;
  c.hasPosition = true;
  c.pathKnown = true;
  c.favourite = false;
  c.rptrTabExists = true;
  int n = nodeMenuRows(c, rows, kMaxNodeMenuRows);
  CHECK(n == 8);
  CHECK(rows[0].action == NodeAction::DirectMessage);
  CHECK(rows[1].action == NodeAction::Info);
  CHECK(findRow(rows, n, NodeAction::ShowOnMap) >= 0);
  CHECK(findRow(rows, n, NodeAction::Ping) >= 0);
  CHECK(findRow(rows, n, NodeAction::Trace) >= 0);
  CHECK(findRow(rows, n, NodeAction::ResetPath) >= 0);
  CHECK(strcmp(rows[findRow(rows, n, NodeAction::Favourite)].value, "off") == 0);
  CHECK(findRow(rows, n, NodeAction::RemoveContact) >= 0);
  CHECK(findRow(rows, n, NodeAction::LoginRptr) < 0);   // not a repeater

  // A client with no position and no path: the rows that need those facts are left OUT.
  NodeMenuContext bare;
  bare.advType = kAdvChat;
  bare.hasKey = true;
  n = nodeMenuRows(bare, rows, kMaxNodeMenuRows);
  CHECK(n == 5);
  CHECK(findRow(rows, n, NodeAction::ShowOnMap) < 0);
  CHECK(findRow(rows, n, NodeAction::Trace) < 0);
  CHECK(findRow(rows, n, NodeAction::ResetPath) < 0);
  CHECK(findRow(rows, n, NodeAction::DirectMessage) >= 0);

  // A repeater: no DM (it cannot take a text DM), but the RPTR login when that tab exists.
  NodeMenuContext rpt;
  rpt.advType = kAdvRepeater;
  rpt.hasKey = true;
  rpt.pathKnown = true;
  rpt.rptrTabExists = true;
  n = nodeMenuRows(rpt, rows, kMaxNodeMenuRows);
  CHECK(n == 7);
  CHECK(findRow(rows, n, NodeAction::DirectMessage) < 0);
  CHECK(findRow(rows, n, NodeAction::LoginRptr) >= 0);
  rpt.rptrTabExists = false;
  n = nodeMenuRows(rpt, rows, kMaxNodeMenuRows);
  CHECK(findRow(rows, n, NodeAction::LoginRptr) < 0);

  // A room is not a client either.
  NodeMenuContext room;
  room.advType = kAdvRoom;
  room.hasKey = true;
  n = nodeMenuRows(room, rows, kMaxNodeMenuRows);
  CHECK(findRow(rows, n, NodeAction::DirectMessage) < 0);
  CHECK(findRow(rows, n, NodeAction::Info) >= 0);

  // No key: only what needs no encryption stays (Info).
  NodeMenuContext nokey;
  nokey.advType = kAdvChat;
  nokey.hasKey = false;
  nokey.hasPosition = true;
  n = nodeMenuRows(nokey, rows, kMaxNodeMenuRows);
  CHECK(n == 2);   // Info, Show on map
  CHECK(rows[0].action == NodeAction::Info);
  CHECK(findRow(rows, n, NodeAction::Ping) < 0);

  // The two-step remove renames its own row, so the second tap is deliberate.
  c.removeArmed = true;
  n = nodeMenuRows(c, rows, kMaxNodeMenuRows);
  CHECK(strcmp(rows[findRow(rows, n, NodeAction::RemoveContact)].label, "Remove: tap again") == 0);
  c.removeArmed = false;
  c.favourite = true;
  n = nodeMenuRows(c, rows, kMaxNodeMenuRows);
  CHECK(strcmp(rows[findRow(rows, n, NodeAction::Favourite)].value, "on") == 0);
}

// --- the sheet's geometry and hit test ------------------------------------------------------------
static void testNodeSheet() {
  const Layout& L = l2();
  NodeSheetLayout g = nodeSheetLayout(L, 8);
  CHECK(g.panel.w == L.width);
  CHECK(g.panel.h == 2 * kSheetPadY + kSheetHeadH + 8 * kNodeSheetRowH);   // 206
  CHECK(g.panel.bottom() == L.height);
  CHECK(g.panel.y >= kStatusH);   // the status bar stays clear
  CHECK(g.head.y == g.panel.y + kSheetPadY);
  CHECK(g.rows[0].y == g.head.bottom());
  CHECK(g.rows[7].bottom() == g.panel.bottom() - kSheetPadY);

  CHECK(nodeSheetRowAt(g, g.rows[3].x + 2, g.rows[3].y + 2) == 3);
  CHECK(nodeSheetRowAt(g, g.head.x, g.head.y + 2) == -1);
  CHECK(nodeSheetRowAt(g, g.panel.x, g.panel.bottom() - 1) == -1);

  // The widest arrangement the rows rule can produce is eight rows, and it still clears the bar.
  g = nodeSheetLayout(L, 8);
  CHECK(g.panel.y >= kStatusH);
  CHECK(kMaxNodeMenuRows == 8);   // a client and a repeater both top out at eight
}

// --- the new-node notice -------------------------------------------------------------------------
static void testNewNode() {
  NodeNotice n = newNodeNotice(true, true, false, false);
  CHECK(n.toast && n.dot && n.sound);
  n = newNodeNotice(true, true, true, false);   // DND (EST-366: the pop-up is silenced too)
  CHECK(!n.toast && n.dot && !n.sound);
  n = newNodeNotice(true, false, false, false); // master sound off
  CHECK(n.toast && n.dot && !n.sound);
  n = newNodeNotice(true, true, false, true);   // the per-event Off
  CHECK(n.toast && n.dot && !n.sound);
  n = newNodeNotice(false, true, false, false); // an advert of a known contact
  CHECK(!n.toast && !n.dot && !n.sound);

  char text[64];
  newNodeToastText(text, sizeof(text), "ERA-3", kAdvChat);
  CHECK(strcmp(text, "New node: ERA-3 (client)") == 0);
  newNodeToastText(text, sizeof(text), "OM-RPTR", kAdvRepeater);
  CHECK(strcmp(text, "New node: OM-RPTR (repeater)") == 0);
  newNodeToastText(text, sizeof(text), nullptr, 9);
  CHECK(strcmp(text, "New node: ? (node)") == 0);
  CHECK(strlen(text) < sizeof(text));

  // The baseline: known vs new, and a removed contact becomes new again.
  KnownKeySet known;
  uint8_t keyA[32] = {};
  uint8_t keyB[32] = {};
  keyA[0] = 1;
  keyB[0] = 2;
  const uint32_t a = KnownKeySet::keyHash(keyA, 32);
  const uint32_t b = KnownKeySet::keyHash(keyB, 32);
  CHECK(a != b);
  CHECK(!known.contains(a));            // not seeded yet: a new contact
  CHECK(known.add(a));
  CHECK(known.contains(a));
  CHECK(!known.add(a));                 // a second advert of the same contact is not new
  CHECK(known.count == 1);
  CHECK(known.remove(a));
  CHECK(!known.contains(a));            // remove contact, then it may be noticed again
  CHECK(known.add(b));
  CHECK(known.count == 1);
}

// --- the message sheet's "Direct message" row ----------------------------------------------------
static void testDmSheetRow() {
  SheetRow rows[kMaxSheetRows];
  SheetContext c;
  c.mine = false;
  c.senderHasKey = true;
  c.senderKnown = true;
  int n = messageSheetRows(c, rows, kMaxSheetRows);
  int dm = -1;
  for (int i = 0; i < n; ++i) {
    if (rows[i].action == SheetAction::DirectMessage) dm = i;
  }
  CHECK(dm >= 0);

  // An unknown sender's name has no key: no row (the ticket says so).
  SheetContext unknown;
  unknown.senderKnown = false;
  unknown.senderHasKey = false;
  n = messageSheetRows(unknown, rows, kMaxSheetRows);
  for (int i = 0; i < n; ++i) CHECK(rows[i].action != SheetAction::DirectMessage);

  // Already inside that thread: no row either.
  SheetContext inDm;
  inDm.senderKnown = true;
  inDm.senderHasKey = true;
  inDm.alreadyDm = true;
  n = messageSheetRows(inDm, rows, kMaxSheetRows);
  for (int i = 0; i < n; ++i) CHECK(rows[i].action != SheetAction::DirectMessage);

  // My own message: no DM row, and the channel's repeat count is offered.
  SheetContext mine;
  mine.mine = true;
  mine.senderHasKey = true;
  n = messageSheetRows(mine, rows, kMaxSheetRows);
  for (int i = 0; i < n; ++i) CHECK(rows[i].action != SheetAction::DirectMessage);
  CHECK(n == 3);   // Copy text, Resend, Heard repeats

  // My own DM: Copy text and Resend, but no group repeat count.
  SheetContext mineDm;
  mineDm.mine = true;
  mineDm.dm = true;
  n = messageSheetRows(mineDm, rows, kMaxSheetRows);
  CHECK(n == 2);
  CHECK(rows[0].action == SheetAction::CopyText);
  CHECK(rows[1].action == SheetAction::Resend);
}

// --- the Info page's lines -----------------------------------------------------------------------
static void testNodeInfo() {
  NodeInfoModel m;
  snprintf(m.name, sizeof(m.name), "ERA-3");
  m.advType = kAdvRepeater;
  m.hasKey = true;
  for (int i = 0; i < 32; ++i) m.key[i] = (uint8_t)i;
  m.hasPosition = true;
  m.lat_e6 = 46123456;
  m.lon_e6 = 14567890;
  snprintf(m.lastSeen, sizeof(m.lastSeen), "14:02");
  snprintf(m.path, sizeof(m.path), "OM-RPTR");

  NodeInfoLine lines[kMaxNodeInfoLines];
  int n = nodeInfoLines(m, lines, kMaxNodeInfoLines);
  CHECK(n == 8);   // name, type, key short, key 1/2, key 2/2, last advert, position, path
  CHECK(strcmp(lines[0].value, "ERA-3") == 0);
  CHECK(strcmp(lines[1].value, "repeater") == 0);
  CHECK(strlen(lines[2].value) == 16);   // 8 bytes of the key
  CHECK(strlen(lines[3].value) == 32);
  CHECK(strlen(lines[4].value) == 32);
  CHECK(strcmp(lines[5].value, "14:02") == 0);
  CHECK(strstr(lines[6].value, "46.123456") != nullptr);
  CHECK(strcmp(lines[7].value, "OM-RPTR") == 0);
  for (int i = 0; i < n; ++i) CHECK(strcmp(lines[i].label, "Last SNR") != 0);

  // The whole key is the two halves, in order.
  char hex[72];
  pubKeyHex(hex, sizeof(hex), m.key, 32);
  CHECK(strlen(hex) == 64);
  CHECK(strncmp(hex, lines[3].value, 32) == 0);
  CHECK(strcmp(hex + 32, lines[4].value) == 0);

  // No position and no path: those rows are absent, not empty.
  NodeInfoModel bare;
  snprintf(bare.name, sizeof(bare.name), "OM Lite");
  int nb = nodeInfoLines(bare, lines, kMaxNodeInfoLines);
  CHECK(nb == 2);   // name and type: no key in the model, so no key rows
  for (int i = 0; i < nb; ++i) CHECK(strcmp(lines[i].label, "Position") != 0);
}

// --- favourites sort first -----------------------------------------------------------------------
static void testFavouriteOrder() {
  CHECK(nodeComesBefore(true, 10, false, 999));    // a star beats a newer advert
  CHECK(!nodeComesBefore(false, 999, true, 10));
  CHECK(nodeComesBefore(true, 20, true, 10));      // both starred: newest first
  CHECK(nodeComesBefore(false, 20, false, 10));    // both plain: newest first
  CHECK(!nodeComesBefore(false, 10, false, 20));
}

// --- the Nodes drag follows the finger -----------------------------------------------------------
static void testNodesDrag() {
  const Layout& L = l2();
  const int maxScroll = L.maxScroll(20);
  CHECK(maxScroll > 0);

  // Finger moves UP 20 px (dyUp = +20): the list scrolls DOWN by 20, so the content follows it.
  CHECK(selDragScroll(100, 20, maxScroll) == 120);
  CHECK(selDragScroll(100, -20, maxScroll) == 80);
  CHECK(selDragScroll(100, 0, maxScroll) == 100);

  // A fingertip of give past either end, snapped back on release.
  CHECK(selDragScroll(0, -30, maxScroll) == -kSelGive);
  CHECK(selDragScroll(maxScroll, 30, maxScroll) == maxScroll + kSelGive);

  // The row really moves with the finger, not against it: at scroll 20 row 0 is 20 px higher.
  const Rect at0 = L.row(0, 0);
  const Rect at20 = L.row(0, 20);
  CHECK(at20.y == at0.y - 20);

  // A drag of 30 px up ends with row 0 still on screen where the eye expects the content.
  const int scroll = selDragScroll(0, 30, maxScroll);
  CHECK(L.row(0, scroll).y < at0.y);
}

// --- one trace row -------------------------------------------------------------------------------
static void testTraceRow() {
  TraceHop h;
  snprintf(h.name, sizeof(h.name), "OM-RPTR");
  h.snr4 = 30;   // 7.5 dB
  h.known = true;
  char out[48];
  traceRowText(out, sizeof(out), h);
  CHECK(strcmp(out, "OM-RPTR  SNR 7.5") == 0);
  h.snr4 = -8;
  traceRowText(out, sizeof(out), h);
  CHECK(strstr(out, "-2.0") != nullptr);
}

int main() {
  testDmBudget();
  testDelivery();
  testLongPress();
  testNodeMenu();
  testNodeSheet();
  testNewNode();
  testDmSheetRow();
  testNodeInfo();
  testFavouriteOrder();
  testNodesDrag();
  testTraceRow();
  if (g_fail == 0) {
    printf("test_dm: all checks passed\n");
    return 0;
  }
  printf("test_dm: %d failure(s)\n", g_fail);
  return 1;
}
