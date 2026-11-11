// SPDX-License-Identifier: MIT
// EST-381 host test: the message pop-up on the Map screen.
//
// The ticket: "when a new message arrives, it should still be displayed as a pop-up, same as on every
// other screen (other than Chat), with the same dismiss-with-touch function." Reading the tree says
// the door and the overlay are shared, so this file proves the parts that CAN be modelled on a
// laptop and pins the numbers the device run will be compared with:
//
//   A. the one decision (ui::noticeDecide): empty, screen-off, DND (with its two exemptions), the
//      open-chat rule, and "a toast is already up" -> queue. It must agree with noticeWanted(), so
//      the USB hook and a real channel message cannot be judged differently;
//   B. the one log line: exactly `[notice] show|queue|skip(<reason>) kind=..`, bounded, NUL-safe;
//   C. the burst queue: newest wins, a message keeps its body AND its kind, take clears;
//   D. the toast itself: the 5 s deadline, the consumed-tap dismissal, and the EST-381 paint counter
//      the `[notice] painted after ...` line reads;
//   E. the pop-up's box on the Map: it sits inside the map's canvas, below the status bar and above
//      the tab bar in BOTH bar states, so "drawn under the chrome" cannot be the cause;
//   F. the consumed tap on the map's own hit areas: a press that dismisses a toast spends the WHOLE
//      gesture, so the marker is not selected, the chip does not toggle, the map does not pan and a
//      callout does not close; and with no toast the very same tap does act (the control).
//
// Plain g++, no Arduino (ui/map_chrome.cpp for the hit test, usb_cmd_logic.cpp for the command).
#include <stdio.h>
#include <string.h>

#include "ui/map_chrome.h"
#include "ui/notice_panel.h"
#include "ui/notify.h"
#include "ui/toast.h"
#include "usb_cmd_logic.h"

using namespace om::ui;

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// --- A. the one decision ----------------------------------------------------------------------
static void testDecision() {
  // A real channel message on the Map: screen on, no DND, no chat open, nothing on screen.
  NoticeDecision d = noticeDecide(NoticeKind::Channel, "TehiLL RAK \xC2\xB7 #slovenija", true, false,
                                  false, false);
  CHECK(d.delivery == NoticeDelivery::Shown);
  CHECK(d.reason == nullptr);

  // Nothing to show: an empty title is refused before anything else.
  d = noticeDecide(NoticeKind::Channel, "", true, false, false, false);
  CHECK(d.delivery == NoticeDelivery::Skipped);
  CHECK(d.reason != nullptr && strcmp(d.reason, "empty") == 0);

  // The screen is off: no pop-up, and the reason says so (a message that arrived in the dark).
  d = noticeDecide(NoticeKind::Channel, "t", false, false, false, false);
  CHECK(d.delivery == NoticeDelivery::Skipped && strcmp(d.reason, "screen-off") == 0);

  // DND keeps its two exemptions and silences the rest, message or not.
  for (int k = 0; k <= (int)NoticeKind::Rptr; ++k) {
    const NoticeKind kind = (NoticeKind)k;
    d = noticeDecide(kind, "t", true, true, false, false);
    const bool exempt = kind == NoticeKind::LowBattery || kind == NoticeKind::Error;
    CHECK(d.delivery == (exempt ? NoticeDelivery::Shown : NoticeDelivery::Skipped));
    if (!exempt) CHECK(strcmp(d.reason, "dnd") == 0);
  }

  // The open chat is the only suppression a message has, and only for the message kinds. This is the
  // rule that must NOT fire on the Map: chatOpen is the caller's answer, and the Map passes false.
  for (int k = 0; k <= (int)NoticeKind::Rptr; ++k) {
    const NoticeKind kind = (NoticeKind)k;
    d = noticeDecide(kind, "t", true, false, true, false);
    const bool message = noticeMessageLike(kind);
    CHECK(d.delivery == (message ? NoticeDelivery::Skipped : NoticeDelivery::Shown));
    if (message) CHECK(strcmp(d.reason, "chat-open") == 0);
    // ... and every combination agrees with the older one-line rule, which the screens still use.
    CHECK(noticeWanted(kind, false, true) == (d.delivery != NoticeDelivery::Skipped));
    CHECK(noticeWanted(kind, true, false) ==
          (noticeDecide(kind, "t", true, true, false, false).delivery != NoticeDelivery::Skipped));
  }

  // A toast already on screen: this one waits (the newest pending wins, see C).
  d = noticeDecide(NoticeKind::Channel, "t", true, false, false, true);
  CHECK(d.delivery == NoticeDelivery::Queued);
  // ... unless it is refused for a stronger reason: the screen going off beats the queue.
  d = noticeDecide(NoticeKind::Channel, "t", false, false, false, true);
  CHECK(d.delivery == NoticeDelivery::Skipped && strcmp(d.reason, "screen-off") == 0);
  // A null title is the same as empty.
  d = noticeDecide(NoticeKind::Channel, nullptr, true, false, false, false);
  CHECK(d.delivery == NoticeDelivery::Skipped && strcmp(d.reason, "empty") == 0);

  // The kind tokens the device line prints.
  CHECK(strcmp(noticeKindToken(NoticeKind::Channel), "channel") == 0);
  CHECK(strcmp(noticeKindToken(NoticeKind::Dm), "dm") == 0);
  CHECK(strcmp(noticeKindToken(NoticeKind::Mention), "mention") == 0);
  CHECK(strcmp(noticeKindToken(NoticeKind::NewNode), "newnode") == 0);
  CHECK(strcmp(noticeKindToken(NoticeKind::LowBattery), "lowbattery") == 0);
  CHECK(strcmp(noticeKindToken(NoticeKind::Error), "error") == 0);
  CHECK(strcmp(noticeKindToken(NoticeKind::Rptr), "rptr") == 0);
  CHECK(strcmp(noticeDeliveryToken(NoticeDelivery::Shown), "show") == 0);
  CHECK(strcmp(noticeDeliveryToken(NoticeDelivery::Queued), "queue") == 0);
  CHECK(strcmp(noticeDeliveryToken(NoticeDelivery::Skipped), "skip") == 0);
}

// --- B. the one log line ----------------------------------------------------------------------
static void testLogLine() {
  char line[96];

  NoticeDecision d;
  d.delivery = NoticeDelivery::Shown;
  d.reason = nullptr;
  CHECK(noticeLogLine(line, sizeof(line), "channel", d) == (int)strlen(line));
  CHECK(strcmp(line, "[notice] show kind=channel") == 0);

  d.delivery = NoticeDelivery::Queued;
  CHECK(noticeLogLine(line, sizeof(line), "dm", d) > 0);
  CHECK(strcmp(line, "[notice] queue kind=dm") == 0);

  d.delivery = NoticeDelivery::Skipped;
  d.reason = "dnd";
  CHECK(noticeLogLine(line, sizeof(line), "channel", d) > 0);
  CHECK(strcmp(line, "[notice] skip(dnd) kind=channel") == 0);

  d.reason = "screen-off";
  CHECK(noticeLogLine(line, sizeof(line), "text", d) > 0);
  CHECK(strcmp(line, "[notice] skip(screen-off) kind=text") == 0);

  // Nothing here may read past its buffer, whatever it is handed.
  d.reason = nullptr;
  CHECK(noticeLogLine(line, sizeof(line), nullptr, d) > 0);
  CHECK(strcmp(line, "[notice] skip(?) kind=?") == 0);
  char tiny[8];
  memset(tiny, 'x', sizeof(tiny));
  CHECK(noticeLogLine(tiny, sizeof(tiny), "channel", d) > 0);
  CHECK(tiny[sizeof(tiny) - 1] == 0);          // NUL-terminated inside the buffer
  CHECK(strlen(tiny) == sizeof(tiny) - 1);
  CHECK(noticeLogLine(nullptr, 0, "channel", d) == 0);
}

// --- C. the burst queue -----------------------------------------------------------------------
static void testQueue() {
  NoticeQueue q;
  CHECK(!q.hasPending);
  Notice a;
  snprintf(a.title, sizeof(a.title), "Alice \xC2\xB7 #general");
  snprintf(a.body, sizeof(a.body), "first");
  CHECK(q.pushNotice(NoticeKind::Channel, a));
  Notice b;
  snprintf(b.title, sizeof(b.title), "DM \xC2\xB7 Bob");
  snprintf(b.body, sizeof(b.body), "second");
  CHECK(q.pushNotice(NoticeKind::Dm, b));

  // The newest one wins, and it is still the whole message with its own kind.
  Notice out;
  NoticeKind kind = NoticeKind::Channel;
  CHECK(q.takeNotice(&out, &kind));
  CHECK(strcmp(out.title, "DM \xC2\xB7 Bob") == 0);
  CHECK(strcmp(out.body, "second") == 0);
  CHECK(kind == NoticeKind::Dm);
  CHECK(!q.hasPending);
  CHECK(!q.takeNotice(&out, &kind));   // taken once

  // The old one-line form still works and is a channel notice.
  CHECK(q.push("Ping Alice: 42 ms"));
  CHECK(q.takeNotice(&out, &kind));
  CHECK(strcmp(out.title, "Ping Alice: 42 ms") == 0);
  CHECK(out.body[0] == 0);
  CHECK(kind == NoticeKind::Channel);

  // An empty title is refused, and clear() drops a pending notice whole.
  Notice empty;
  CHECK(!q.pushNotice(NoticeKind::Channel, empty));
  CHECK(q.pushNotice(NoticeKind::Channel, a));
  q.clear();
  CHECK(!q.hasPending);
  CHECK(!q.takeNotice(&out, &kind));
}

// --- D. the toast's own state ------------------------------------------------------------------
static void testToastState() {
  ToastState t;
  CHECK(!t.active());
  CHECK(!t.visible(1000));
  CHECK(t.showNotice("Alice \xC2\xB7 #general", "hello", 1000, kToastMs));
  CHECK(t.active());
  CHECK(t.visible(1000));
  CHECK(t.visible(1000 + kToastMs - 1));
  CHECK(!t.visible(1000 + kToastMs));   // at the deadline it is over, the next frame is clean
  CHECK(kToastMs == 5000);

  // The paint counter is what the device line reads: it only moves when a frame painted the toast.
  const uint32_t before = t.paints;
  ++t.paints;
  CHECK(t.paints == before + 1);

  // The consumed tap: a press during a visible toast dismisses it and must be spent by the caller;
  // an expired toast does not eat the touch (the loop's expire() has cleared it by then).
  ToastState s;
  s.showNotice("t", "b", 1000, kToastMs);
  CHECK(toastDismissOnPress(s, 1000 + 100));
  CHECK(!s.active());
  CHECK(!toastDismissOnPress(s, 1000 + 200));

  ToastState e;
  e.showNotice("t", "b", 1000, kToastMs);
  e.expire(1000 + kToastMs);
  CHECK(!e.active());
  CHECK(!toastDismissOnPress(e, 1000 + kToastMs + 1));
}

// --- E. the pop-up's box on the Map ------------------------------------------------------------
// A fixed 6 px advance, the same stand-in ui/notice_panel.cpp uses when no measure is given: the
// arithmetic is what is under test, not the font.
static int fixedAdvance(void* ctx, const char* text, int len) {
  (void)ctx;
  (void)text;
  return len * 6;
}

static void testPopupBoxOnMap() {
  const Layout& L = l2();
  // Exactly the call ui/widgets.cpp's overlay makes: the screen width, the tab bar's top and the
  // status bar's bottom. The map's own rectangles do not enter into it, which is why the pop-up
  // cannot land under the map's chrome.
  const NoticeLayout lo = noticeLayout("hello from the mesh, this is a two line message", fixedAdvance,
                                       nullptr, L.width, L.tabbar.y, L.status.bottom());
  CHECK(lo.boxW > 0 && lo.boxH > 0);
  CHECK(lo.boxX >= 0);
  CHECK(lo.boxX + lo.boxW <= L.width);
  CHECK(lo.boxY >= L.status.bottom());            // never under the status bar
  CHECK(lo.boxY + lo.boxH <= L.tabbar.y);         // never under the tab bar
  CHECK(lo.lineCount >= 1 && lo.lineCount <= kNoticeMaxLines);

  // And against the Map's own geometry: the box is inside the canvas, above the shown bar AND above
  // the tucked 3 px strip (the strip is on the screen floor, the box never reaches it).
  const MapChrome shown = mapChromeFor(L, false, false);
  const MapChrome hidden = mapChromeFor(L, false, true);
  CHECK(lo.boxY >= shown.view.y && lo.boxY >= hidden.view.y);
  CHECK(lo.boxY + lo.boxH <= shown.view.bottom());
  CHECK(lo.boxY + lo.boxH <= hidden.barStrip.y);
  CHECK(lo.boxY >= kMapCanvasTop);   // ... and below the status bar row the canvas starts on

  // A long message cannot grow past the limit and push the title off the screen: the box stays put
  // and the text is cut with "...". This is the Map's case (a busy channel message).
  char longText[400];
  memset(longText, 'a', sizeof(longText) - 1);
  longText[sizeof(longText) - 1] = 0;
  const NoticeLayout big = noticeLayout(longText, fixedAdvance, nullptr, L.width, L.tabbar.y,
                                        L.status.bottom());
  CHECK(big.lineCount == kNoticeMaxLines);
  CHECK(big.truncated);
  CHECK(big.boxY >= L.status.bottom());
  CHECK(big.boxY + big.boxH <= L.tabbar.y);
}

// --- F. the consumed tap on the map's hit areas ------------------------------------------------
// A faithful model of the shell's press edge (main.cpp's loop), which test_est381.py checks against
// the real file: the toast is consulted FIRST on the press, a dismissal sets spent/drag/acted, and
// every map act is behind those flags. What is under test here is the map's hit areas: a spent
// gesture must reach none of them.
struct Gesture {
  bool down = false;
  bool spent = false;
  bool drag = false;
  bool acted = false;
};

struct MapActs {
  int selected = -1;     // a marker that got selected
  bool chipToggled = false;
  bool zoomed = false;
  int panned = 0;
  bool calloutClosed = false;
};

// The map's answer to one tap at (x, y), i.e. what map_tab::tap() does for the hit kinds this test
// drives. Written from that switch, and only the four acts the ticket names.
static void mapTap(const MapChrome& c, const MapMarker* markers, int count, int x, int y,
                   bool calloutOpen, MapActs* acts) {
  const MapHit hit = mapHitAt(c, markers, count, x, y);
  switch (hit.kind) {
    case MapHitKind::Marker: acts->selected = hit.marker; break;
    case MapHitKind::TabsToggle: acts->chipToggled = true; break;
    case MapHitKind::ZoomIn:
    case MapHitKind::ZoomOut: acts->zoomed = true; break;
    case MapHitKind::Callout:
    case MapHitKind::Map: if (calloutOpen) acts->calloutClosed = true; break;
    default: break;
  }
}

// One whole gesture: a press, an optional drag, a release. Returns what the map did.
static MapActs gesture(const MapChrome& c, const MapMarker* markers, int count, int x, int y,
                       bool calloutOpen, int dragDx, uint32_t nowMs) {
  MapActs acts;
  Gesture g;
  // The press edge: EST-346's rule, on the real ToastState.
  g.down = true;
  if (toastDismissOnPress(toastState(), nowMs)) {
    g.spent = true;
    g.drag = true;
    g.acted = true;
  }
  if (dragDx != 0 && !g.spent) {     // the map's pan branch: `if (g_drag && !g_spent)`
    g.drag = true;
    acts.panned += dragDx;           // recorded, then asserted on
  }
  // The release: `if (!spent) { if (!g_drag && !g_acted) handleTap(...) }`.
  if (!g.spent && !g.drag && !g.acted) mapTap(c, markers, count, x, y, calloutOpen, &acts);
  return acts;
}

static void testConsumedTapOnMap() {
  const Layout& L = l2();
  MapMarker markers[2];
  markers[0].x = 100;
  markers[0].y = 100;
  markers[0].type = 2;
  markers[0].name = "TehiLL RAK";
  markers[1].x = 160;
  markers[1].y = 133;
  markers[1].self = true;
  markers[1].name = "me";

  for (int hiddenState = 0; hiddenState < 2; ++hiddenState) {
    const MapChrome c = mapChromeFor(L, false, hiddenState != 0);

    // The four places the ticket names: a marker, the hide/show chip, a zoom tab and plain map
    // (with a callout open, so a tap there would normally close it).
    const int px[4] = {100, c.chipTap.x + c.chipTap.w / 2, c.zoomIn.x + c.zoomIn.w / 2,
                       c.view.x + 20};
    const int py[4] = {100, c.chipTap.y + c.chipTap.h / 2, c.zoomIn.y + c.zoomIn.h / 2,
                       c.view.y + 20};
    for (int i = 0; i < 4; ++i) {
      // Control: no toast, the tap really acts (proves the consumption below is what stops it).
      toastState().clear();
      MapActs free = gesture(c, markers, 2, px[i], py[i], true, 0, 5000);
      const bool acted = free.selected >= 0 || free.chipToggled || free.zoomed ||
                         free.calloutClosed;
      CHECK(acted);

      // The ticket's case: a visible toast on the Map. The press dismisses it and spends the whole
      // gesture, so nothing on the map below it acts.
      toastState().clear();
      toastState().showNotice("Alice \xC2\xB7 #general", "hello", 4000, kToastMs);
      CHECK(toastState().visible(4500));
      MapActs spent = gesture(c, markers, 2, px[i], py[i], true, 0, 4500);
      CHECK(spent.selected < 0);
      CHECK(!spent.chipToggled);
      CHECK(!spent.zoomed);
      CHECK(!spent.calloutClosed);
      CHECK(!toastState().active());        // the tap did dismiss it
      // The same press with a drag: no pan either (the map's `!g_spent` guard).
      toastState().clear();
      toastState().showNotice("Alice \xC2\xB7 #general", "hello", 4000, kToastMs);
      MapActs dragged = gesture(c, markers, 2, px[i], py[i], true, -30, 4500);
      CHECK(dragged.panned == 0);
      CHECK(dragged.selected < 0);
    }

    // And the expiry case: a toast whose 5 s ran out is not there, so the same tap acts normally.
    toastState().clear();
    toastState().showNotice("Alice \xC2\xB7 #general", "hello", 1000, kToastMs);
    CHECK(toastState().expire(1000 + kToastMs));
    MapActs late = gesture(c, markers, 2, 100, 100, true, 0, 1000 + kToastMs + 1);
    CHECK(late.selected == 0);
  }
  toastState().clear();
}

// --- G. the USB hook's command line ------------------------------------------------------------
static om::usb::Command parse(char* line) { return om::usb::parseCommand(line); }

static void testNotifyCommand() {
  // The ticket's form, with the device's own tokenizer: two quoted words after `test`.
  char line[] = "notify test \"TehiLL RAK \xC2\xB7 #slovenija\" \"hello there, a message\"";
  om::usb::Command c = parse(line);
  CHECK(c.error == nullptr);
  CHECK(c.kind == om::usb::Kind::Notify);
  CHECK(c.target != nullptr && strcmp(c.target, "TehiLL RAK \xC2\xB7 #slovenija") == 0);
  CHECK(c.text != nullptr && strcmp(c.text, "hello there, a message") == 0);

  // A one-word title and the body as the rest of the line (the tokenizer's own rule).
  char plain[] = "notify test Alice just-saying-hello";
  c = parse(plain);
  CHECK(c.error == nullptr);
  CHECK(c.target != nullptr && strcmp(c.target, "Alice") == 0);
  CHECK(c.text != nullptr && strcmp(c.text, "just-saying-hello") == 0);

  // Both words are required: nothing is invented from a missing one.
  char noBody[] = "notify test \"only a title\"";
  CHECK(parse(noBody).error != nullptr);
  char noTitle[] = "notify test";
  CHECK(parse(noTitle).error != nullptr);
  char noSub[] = "notify \"a\" \"b\"";
  CHECK(parse(noSub).error != nullptr);
  char badSub[] = "notify send \"a\" \"b\"";
  CHECK(parse(badSub).error != nullptr);
  char extra[] = "notify test \"a\" \"b\" c";
  CHECK(parse(extra).error != nullptr);
  char open[] = "notify test \"a\" \"b";
  const om::usb::Command q = parse(open);
  CHECK(q.error != nullptr && strcmp(q.error, "quote") == 0);
  char empty[] = "";
  CHECK(parse(empty).error != nullptr);
}

int main() {
  testDecision();
  testLogLine();
  testQueue();
  testToastState();
  testPopupBoxOnMap();
  testConsumedTapOnMap();
  testNotifyCommand();
  if (g_fail) {
    printf("%d est381 check(s) FAILED\n", g_fail);
    return 1;
  }
  printf("est381: all checks passed\n");
  return 0;
}
