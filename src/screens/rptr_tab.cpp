// SPDX-License-Identifier: MIT
// EST-410: the RPTR tab's state, drawing glue and tap handling, moved out of main.cpp. The pixels,
// rectangles and hit tests stay in ui/screen_rptr.cpp and ui/rptr_layout.cpp; rptr_link.cpp owns the
// session, rptr_page.cpp the rows and rptr_model.cpp the parsers. This file only owns the tab's own
// state, builds the one RptrView, and runs the CLI queue and its answers.
//
// The move is a move: same strings, same NVS keys, same USB answers, same log lines, same statics.
// The shell services come in through the Host in rptr_tab.h.
#include "screens/rptr_tab.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "map.h"
#include "mesh_link.h"
#include "position.h"
#include "rptr_link.h"
#include "rptr_model.h"
#include "rptr_page.h"
#include "screens/sound_events.h"
#include "screens/toasts.h"
#include "timekeeper.h"
#include "ui/age.h"
#include "ui/chat_layout.h"
#include "ui/compose_text.h"
#include "ui/keys.h"
#include "ui/map_chrome.h"
#include "ui/notify.h"
#include "ui/press_targets.h"
#include "ui/rptr_layout.h"
#include "ui/screen_rptr.h"

namespace om {
namespace rptr_tab {
namespace {

Host s_host;

// The shell services, through the Host. The names stay short at the call sites, which are a pure
// move of the code that called main.cpp's own functions before.
void toast(const char* text) {
  if (s_host.toast != nullptr) s_host.toast(text);
}
void status(ui::StatusModel& s) {
  if (s_host.buildStatus != nullptr) s_host.buildStatus(s);
}
ui::TabBarModel tabBar(ui::Tab active) {
  if (s_host.tabBar == nullptr) return ui::TabBarModel();
  return s_host.tabBar(active);
}
const GpsState& gps() {
  static GpsState none{};
  return (s_host.gps != nullptr) ? *s_host.gps() : none;
}
bool visible() { return s_host.screenVisible != nullptr && s_host.screenVisible(); }
bool redraw() { return s_host.redraw != nullptr && s_host.redraw(); }

// EST-410: the same value as main.cpp's SET_LOOP_TASK_STACK_SIZE(16384). main.cpp's copy is static
// to that file and only the [rptr] build log line prints it, so the one number is repeated here to
// keep the log byte-identical. Listed as a non-move edit in PROGRESS.md.
constexpr unsigned kLoopStackSize = 16384;

// ----------------------------------------------------------------------------- RPTR (EST-362)
// The tab's interaction design, rebuilt around Filip's list/page/Apply rule:
//   list    the tab scans when it opens; every remembered repeater first (with a key icon), then
//           the scan answers. Tap a row: that repeater's page.
//   page    before login: Regions / Clock / Signal, all read without a password, and ONE big Log in.
//           after login: the status card, a "Clock is wrong" line with Set clock, then the plain
//           settings list and the Console at the bottom.
//   setting one page each, ONE Apply that sends and makes permanent (regions append `region save`),
//           the result and the read-back on the same page.
//   confirm only what is risky, as a pop-up over the page. No Send now, no Save to repeater, no CLI
//           text outside the Console.
// The rows, the exact lines and the confirmation rules live in rptr_page.h (host-tested); this is
// the state, the taps and the radio queue.

static void drawRptrTab();

static bool g_rptrList = true;                       // the list is showing (else one page)
static om::rptr::Page g_rptrPage = om::rptr::Page::List;
static int g_rptrScroll = 0;
static int g_rptrScroll0 = 0;
static int g_rptrCount = 0;                          // the last build's list rows (drag clamp)
static int g_rptrPageScroll = 0;
static int g_rptrPageScroll0 = 0;
// EST-366 (rule 7): every sub-page is reached through this stack, so Back pops exactly one level and
// restores that page's scroll (the Settings history of EST-350, applied to RPTR).
static om::rptr::PageNav g_rptrNav;

// --- EST-366 (rule 6): the Position page's map picker
// The centre itself lives in map.cpp (mapSetView/mapPan/mapScreenToLatLon); these are the drag's base
// and the view's last state.
static bool g_rptrMapPick = false;
static int g_rptrPickZoom = 15;
static int g_rptrPickDragX = 0;
static int g_rptrPickDragY = 0;
static bool g_rptrPickDragging = false;

// --- the open session
static char g_rptrNode[32] = "";                     // the repeater this page is about (Settings too)
static bool g_rptrLoggedIn = false;
static bool g_rptrRemembered = false;
static uint8_t g_rptrFoundIndex = 255;               // the row in the last scan
static char g_rptrPw[om::rptr::kPasswordMax + 1] = "";   // typed, never drawn, never logged
static om::rptr::Facts g_rptrFacts;
static om::rptr::Draft g_rptrDraft;
static om::rptr::RegionList g_rptrRegions;
static om::rptr::NeighbourList g_rptrNeighbours;
// EST-367: the neighbour discovery window. `discover.neighbors` goes out, the page shows the 60 s
// progress, and `neighbors` is read when the window ends (or at once on a repeater that does not
// know the command). The model owns the timing; rptrPump only calls tick().
static om::rptr::NeighbourScan g_rptrNbr;
static om::rptr::AnonFacts g_rptrAnon;
static char g_rptrHome[32] = "";
static bool g_rptrHomeKnown = false;
static char g_rptrDefault[32] = "";
static bool g_rptrDefaultKnown = false;
static uint32_t g_rptrLoginWait = 0;                 // millis() when a login went out
static uint32_t g_rptrReplySerial = 0;
static uint32_t g_rptrStatusSerial = 0;
static uint32_t g_rptrScanSerialSeen = 0;

// --- one line in flight, the rest in a small queue, so commands never collide
enum : uint8_t { kRptrOutRead = 0, kRptrOutWrite = 1, kRptrOutCmd = 2 };
struct RptrOutLine {
  char line[80] = "";
  uint8_t kind = kRptrOutRead;
};
constexpr int kRptrOutCap = 24;
static RptrOutLine g_rptrOut[kRptrOutCap];
static int g_rptrOutCount = 0;
static int g_rptrOutHead = 0;
static char g_rptrInFlight[80] = "";
static uint8_t g_rptrInFlightKind = 0xFF;
static uint32_t g_rptrInFlightMs = 0;
static uint32_t g_rptrInFlightDeadlineMs = 0;   // EST-364: from the link's own est_timeout
// EST-364: the `time <epoch>` line we sent last, so the repeater's OK can become the page's clock
// even when the follow-up `clock` read never answers.
static uint32_t g_rptrClockSent = 0;
// EST-364: the position read-back pair. Only a clean run (both answers, no line abandoned in
// between) may reach the page: a lost answer shifts the CLI reply order, and that is what put
// Argus's longitude in the latitude field.
static om::rptr::CoordRun g_rptrCoord;
static bool g_rptrStatusWait = false;   // a status request is out / gave up: "no answer yet", dim

// --- Apply
static bool g_rptrApplyPending = false;
static om::rptr::Page g_rptrApplyPage = om::rptr::Page::List;
static int g_rptrApplyWritesLeft = 0;
static bool g_rptrApplyErr = false;
static char g_rptrApplyErrText[64] = "";
static char g_rptrResult[64] = "";
static bool g_rptrResultOk = false;
static bool g_rptrResultPending = false;

// --- Console (the raw log, the only place CLI answers live)
static constexpr int kRptrLogLines = om::rptr::kConsoleLines;
static char g_rptrLog[kRptrLogLines][88];
static int g_rptrLogCount = 0;
static char g_rptrCmd[64] = "";                      // the console's free command line

// --- the pop-up
static bool g_rptrAskOpen = false;
static om::rptr::ConfirmAsk g_rptrAsk;
static int g_rptrAskWhat = 0;                        // 1 apply, 2 restart, 3 remove, 4 clock
static char g_rptrAskArg[32] = "";

// --- the keyboard sheet
static bool g_rptrOsk = false;
static bool g_rptrNums = false;
static bool g_rptrShift = false;
static bool g_rptrSecret = false;
static uint8_t g_rptrField = (uint8_t)om::ui::KeyboardField::RptrPassword;
static uint8_t g_rptrEditWhat = 0;   // 1 password, 2 name, 3 lat, 4 lon, 5 add-name, 6 console
static char g_rptrEditTitle[32] = "";
static char g_rptrText[64] = "";
static int g_rptrTextLen = 0;
static om::ui::RptrView g_rptrViewStore;   // one instance, not on the loop stack

static void rptrLog(const char* prefix, const char* text) {
  char line[88];
  size_t w = 0;
  const char* p = prefix;
  while (*p && w + 1 < sizeof(line)) line[w++] = *p++;
  if (w + 1 < sizeof(line)) line[w++] = ' ';
  p = text == nullptr ? "" : text;
  while (*p && w + 1 < sizeof(line)) {
    line[w++] = (*p == '\n' || *p == '\r') ? ' ' : *p;
    ++p;
  }
  line[w] = 0;
  if (g_rptrLogCount >= kRptrLogLines) {
    for (int i = 1; i < kRptrLogLines; ++i) memcpy(g_rptrLog[i - 1], g_rptrLog[i], sizeof(g_rptrLog[0]));
    --g_rptrLogCount;
  }
  snprintf(g_rptrLog[g_rptrLogCount++], sizeof(g_rptrLog[0]), "%s", line);
  Serial.printf("[rptr] %s\n", line);
}

static void rptrQueue(const char* line, uint8_t kind) {
  if (line == nullptr || line[0] == 0) return;
  if (g_rptrOutCount >= kRptrOutCap) {
    Serial.printf("[rptr] queue full, dropped: %s\n", line);
    return;
  }
  RptrOutLine& o = g_rptrOut[g_rptrOutCount++];
  snprintf(o.line, sizeof(o.line), "%s", line);
  o.kind = kind;
}

static void rptrSyncDraft() {
  g_rptrDraft.reset(g_rptrFacts, g_rptrRegions, g_rptrHomeKnown ? g_rptrHome : "*",
                    g_rptrDefaultKnown ? g_rptrDefault : nullptr);
}

// EST-368 (Filip ~22:30): a read-back answer updates only the fields whose draft is still clean.
// The whole-draft reset above is for opening a page, going Back, and a successful Apply; using it
// for every answer is what made the first pick on a settings page vanish.
static void rptrSyncClean() {
  g_rptrDraft.syncClean(g_rptrFacts, g_rptrRegions, g_rptrHomeKnown ? g_rptrHome : "*",
                        g_rptrDefaultKnown ? g_rptrDefault : nullptr);
}

static void rptrClearSession() {
  g_rptrList = true;
  g_rptrPage = om::rptr::Page::List;
  g_rptrNode[0] = 0;
  g_rptrLoggedIn = false;
  g_rptrRemembered = false;
  g_rptrFoundIndex = 255;
  g_rptrPw[0] = 0;
  g_rptrFacts.reset();
  g_rptrRegions = om::rptr::RegionList();
  g_rptrNeighbours = om::rptr::NeighbourList();
  g_rptrNbr.cancel();
  g_rptrAnon = om::rptr::AnonFacts();
  g_rptrHome[0] = 0;
  g_rptrHomeKnown = false;
  g_rptrDefault[0] = 0;
  g_rptrDefaultKnown = false;
  g_rptrDraft = om::rptr::Draft();
  g_rptrOsk = false;
  g_rptrAskOpen = false;
  g_rptrOutCount = g_rptrOutHead = 0;
  g_rptrInFlightKind = 0xFF;
  g_rptrInFlight[0] = 0;
  g_rptrApplyPending = false;
  g_rptrApplyWritesLeft = 0;
  g_rptrApplyErr = false;
  g_rptrApplyErrText[0] = 0;
  g_rptrResult[0] = 0;
  g_rptrResultPending = false;
  g_rptrStatusWait = false;
  g_rptrCoord.reset();
  g_rptrClockSent = 0;
  g_rptrInFlightDeadlineMs = 0;
  g_rptrPageScroll = 0;
  g_rptrLoginWait = 0;
  g_rptrFoundIndex = 255;
  g_rptrNav.reset();
  g_rptrMapPick = false;
  g_rptrPickZoom = 15;
}

static void rptrFillAnonFromScan() {
  const om::rptr::FoundList& found = om::rptrScan();
  if (g_rptrFoundIndex < 255 && g_rptrFoundIndex < found.count) {
    const om::rptr::Found& f = found.rows[g_rptrFoundIndex];
    g_rptrAnon.signalKnown = f.hasSnrOut || f.hasSnrIn;
    g_rptrAnon.snr4 = f.hasSnrOut ? f.snrOut4 : f.snrIn4;
    if (f.anon.valid) {
      g_rptrAnon.valid = true;
      g_rptrAnon.regionCount = f.anon.count;
      g_rptrAnon.clock = f.anon.clock;
      snprintf(g_rptrAnon.regions, sizeof(g_rptrAnon.regions), "%s", f.anon.text);
    } else if (f.regionsAsked) {
      g_rptrAnon.asked = true;
    }
  }
}

// Open one repeater's page. `index` is the scan row, 255 when only a name is known (a remembered
// repeater opened from the Nodes menu).
static void rptrOpenAt(const char* name, uint8_t index) {
  rptrClearSession();
  snprintf(g_rptrNode, sizeof(g_rptrNode), "%s", name != nullptr ? name : "");
  g_rptrList = false;
  g_rptrPage = om::rptr::Page::Repeater;
  g_rptrFoundIndex = index;
  g_rptrFacts.reset();
  rptrSyncDraft();
  rptrFillAnonFromScan();
  for (int i = 0; i < om::rptrRememberedCount(); ++i) {
    if (strcmp(om::rptrRememberedName(i), g_rptrNode) == 0) g_rptrRemembered = true;
  }
  if (index == 255) {
    // A remembered name with no scan row: look it up by name in the last scan.
    const om::rptr::FoundList& found = om::rptrScan();
    for (int i = 0; i < found.count; ++i) {
      if (strcmp(found.rows[i].name, g_rptrNode) == 0) {
        g_rptrFoundIndex = (uint8_t)i;
        rptrFillAnonFromScan();
        break;
      }
    }
  }
  // Before login the page fills itself: the anonymous regions read needs no password.
  if (g_rptrFoundIndex < 255 && !g_rptrAnon.valid && !om::rptrRegionsBusy()) {
    if (om::rptrRegionsQuery(g_rptrFoundIndex)) g_rptrAnon.asked = true;
  }
  if (g_rptrRemembered && om::rptrAutoLogin()) {
    char pw[om::rptr::kPasswordMax + 1] = "";
    if (om::rptrPasswordFor(g_rptrNode, pw, sizeof(pw)) && om::rptrLoginStart(g_rptrNode, pw)) {
      g_rptrLoginWait = millis();
      Serial.printf("[rptr] auto-login started (found %d)\n", (int)g_rptrFoundIndex);
    }
    memset(pw, 0, sizeof(pw));
  }
  Serial.printf("[rptr] open (found %d)\n", (int)g_rptrFoundIndex);
}

static void rptrOpen(const char* name) { rptrOpenAt(name, 255); }

static void rptrLoginNow() {
  if (g_rptrPw[0] == 0) {
    toast("password required");
    return;
  }
  if (!om::rptrLoginStart(g_rptrNode, g_rptrPw)) {
    toast("Login not sent: no such contact");
    return;
  }
  g_rptrLoginWait = millis();
  g_rptrOsk = false;
  g_rptrResult[0] = 0;
  drawRptrTab();
}

// Back: one level, from the stack (EST-366, rule 7). A Home/Default picker opened from Regions goes
// back to Regions (not straight to the repeater page, which is what the old one-level rule did);
// with an empty stack, Back on the repeater page leaves the session for the list.
static void rptrBack() {
  if (g_rptrList) return;
  om::rptr::Page page = g_rptrPage;
  int scroll = g_rptrPageScroll;
  if (g_rptrNav.pop(&page, &scroll)) {
    g_rptrPage = page;
    g_rptrPageScroll = scroll;
    g_rptrResult[0] = 0;
    g_rptrResultPending = false;
    g_rptrAskOpen = false;
    rptrSyncDraft();
    return;
  }
  if (g_rptrPage != om::rptr::Page::Repeater) {
    g_rptrPage = om::rptr::Page::Repeater;
    g_rptrPageScroll = 0;
    g_rptrResult[0] = 0;
    g_rptrResultPending = false;
    rptrSyncDraft();
    return;
  }
  om::rptrLogout();
  rptrClearSession();
}

// Open one setting page and ask the repeater for its own values. The page it came from and that
// page's scroll go on the history first, so Back is exactly one level.
static void rptrGoPage(om::rptr::Page page) {
  if (page != g_rptrPage) g_rptrNav.push(g_rptrPage, g_rptrPageScroll);
  g_rptrPage = page;
  g_rptrPageScroll = 0;
  g_rptrResult[0] = 0;
  g_rptrResultPending = false;
  g_rptrApplyPending = false;
  g_rptrApplyErr = false;
  g_rptrApplyErrText[0] = 0;
  g_rptrAskOpen = false;
  g_rptrMapPick = false;
  rptrSyncDraft();
  if (!g_rptrLoggedIn) return;
  char reads[om::rptr::kReadBackMax][24];
  const int n = om::rptr::readBackLines(page, reads);
  for (int i = 0; i < n; ++i) rptrQueue(reads[i], kRptrOutRead);
}

// The apply's end: the result is the repeater's own answer, in plain words.
static bool g_rptrDirty = false;

static void rptrCheckApplyDone() {
  if (!g_rptrApplyPending || g_rptrApplyWritesLeft > 0) return;
  g_rptrApplyPending = false;
  g_rptrResultPending = false;
  g_rptrResultOk = !g_rptrApplyErr;
  g_rptrDirty = true;
  if (g_rptrApplyErr && g_rptrApplyErrText[0]) {
    snprintf(g_rptrResult, sizeof(g_rptrResult), "%s", g_rptrApplyErrText);
  } else {
    snprintf(g_rptrResult, sizeof(g_rptrResult), "Applied");
    // The read-backs that follow fill in the repeater's own values; only reset the page that was
    // applied, never a page the finger has moved on to.
    if (g_rptrPage == g_rptrApplyPage) rptrSyncDraft();
  }
  // EST-366 (rule 9): an RPTR result is a notification too, but when the RPTR page itself is on
  // screen its own result line already says it (the chat-open rule, one level up).
  const bool onRptrPage = visible() && !g_rptrList && !g_rptrMapPick;
  om::toasts::notify(om::ui::NoticeKind::Rptr, g_rptrResult, onRptrPage, om::sounds::dnd());
}

// Start an Apply: the writes, then the read-backs for this page.
static void rptrApplyNow() {
  om::rptr::ApplyPlan plan;
  const int n = om::rptr::applyPlan(g_rptrPage, g_rptrDraft, g_rptrRegions, &plan);
  if (n <= 0) {
    snprintf(g_rptrResult, sizeof(g_rptrResult), "Nothing to change");
    g_rptrResultOk = true;
    g_rptrResultPending = false;
    g_rptrApplyPending = false;
    return;
  }
  g_rptrApplyPending = true;
  g_rptrApplyPage = g_rptrPage;
  g_rptrApplyWritesLeft = n;
  g_rptrApplyErr = false;
  g_rptrApplyErrText[0] = 0;
  g_rptrResultPending = true;
  snprintf(g_rptrResult, sizeof(g_rptrResult), "Applying...");
  for (int i = 0; i < plan.count; ++i) {
    rptrQueue(plan.line[i], kRptrOutWrite);
    rptrLog(">", plan.line[i]);
  }
  char reads[om::rptr::kReadBackMax][24];
  const int rn = om::rptr::readBackLines(g_rptrPage, reads);
  for (int i = 0; i < rn; ++i) rptrQueue(reads[i], kRptrOutRead);
}

// The same path for an action that writes without a setting page (remove a region, set the clock).
static void rptrApplyLines(char lines[][80], int n) {
  if (n <= 0) return;
  g_rptrApplyPending = true;
  g_rptrApplyPage = g_rptrPage;
  g_rptrApplyWritesLeft = n;
  g_rptrApplyErr = false;
  g_rptrApplyErrText[0] = 0;
  g_rptrResultPending = true;
  snprintf(g_rptrResult, sizeof(g_rptrResult), "Applying...");
  for (int i = 0; i < n; ++i) {
    rptrQueue(lines[i], kRptrOutWrite);
    rptrLog(">", lines[i]);
  }
}

static void rptrSetClockNow() {
  char line[32];
  const uint32_t our = om::timeKeeper().now();
  if (om::rptr::cliTime(line, sizeof(line), our) == 0) {
    snprintf(g_rptrResult, sizeof(g_rptrResult), "Our clock is not usable");
    g_rptrResultOk = false;
    return;
  }
  char lines[1][80];
  snprintf(lines[0], sizeof(lines[0]), "%s", line);
  g_rptrClockSent = our;   // EST-364: if the answer is OK, this is the repeater's new clock
  rptrApplyLines(lines, 1);
  rptrQueue(om::rptr::kCliClockRead, kRptrOutRead);
}

// The clock line's one button. The guard (rptr_model.h) is the fixed one: our clock must be real, a
// repeater ahead of us is refused, a jump over a day asks first with both dates, else it just sets.
static void rptrClockPressed() {
  const uint32_t our = om::timeKeeper().now();
  const bool real = om::timeKeeper().realSinceBoot();
  const om::rptr::ClockVerdict verdict =
      om::rptr::clockGuard(our, real, g_rptrFacts.hasClock, g_rptrFacts.clock);
  if (verdict == om::rptr::ClockVerdict::OurClockNotSet ||
      verdict == om::rptr::ClockVerdict::RepeaterAhead) {
    snprintf(g_rptrResult, sizeof(g_rptrResult), "%s", om::rptr::clockVerdictText(verdict));
    g_rptrResultOk = false;
    g_rptrResultPending = false;
    return;
  }
  if (verdict == om::rptr::ClockVerdict::NeedConfirm) {
    const om::rptr::ConfirmAsk ask = om::rptr::confirmClock(g_rptrFacts.hasClock ? g_rptrFacts.clock : 0, our);
    if (ask.need) {
      g_rptrAsk = ask;
      g_rptrAskWhat = 4;
      g_rptrAskOpen = true;
      return;
    }
  }
  rptrSetClockNow();
}

static void rptrRemoveRegion(const char* name) {
  g_rptrAsk = om::rptr::confirmRemoveRegion(name);
  g_rptrAskWhat = 3;
  snprintf(g_rptrAskArg, sizeof(g_rptrAskArg), "%s", name != nullptr ? name : "*");
  g_rptrAskOpen = g_rptrAsk.need;
}

static void rptrRunAsk() {
  switch (g_rptrAskWhat) {
    case 1:
      rptrApplyNow();
      break;
    case 2:
      rptrLog(">", om::rptr::kCliReboot);
      rptrQueue(om::rptr::kCliReboot, kRptrOutCmd);
      snprintf(g_rptrResult, sizeof(g_rptrResult), "Restarting");
      g_rptrResultOk = true;
      break;
    case 3: {
      char removeLine[80];
      om::rptr::cliRegionRemove(removeLine, sizeof(removeLine), g_rptrAskArg);
      char lines[2][80];
      snprintf(lines[0], sizeof(lines[0]), "%s", removeLine);
      snprintf(lines[1], sizeof(lines[1]), "%s", om::rptr::kCliRegionSave);
      rptrApplyLines(lines, 2);
      char reads[om::rptr::kReadBackMax][24];
      const int n = om::rptr::readBackLines(om::rptr::Page::Regions, reads);
      for (int i = 0; i < n; ++i) rptrQueue(reads[i], kRptrOutRead);
      break;
    }
    case 4:
      rptrSetClockNow();
      break;
    default:
      break;
  }
  g_rptrAskWhat = 0;
}

static void rptrOpenAsk(const om::rptr::ConfirmAsk& ask, int what) {
  if (!ask.need) return;
  g_rptrAsk = ask;
  g_rptrAskWhat = what;
  g_rptrAskOpen = true;
}

// The console's free line: sent verbatim through the same CLI path.
static void rptrSendConsole() {
  if (g_rptrCmd[0] == 0) {
    toast("type a command first");
    return;
  }
  rptrLog(">", g_rptrCmd);
  rptrQueue(g_rptrCmd, kRptrOutCmd);
  g_rptrCmd[0] = 0;
}

// Which page a read-back belongs to: a reply only resets the draft of the page the finger is on.
static om::rptr::Page rptrReadPage(const char* cmd) {
  if (strcmp(cmd, "get name") == 0) return om::rptr::Page::Name;
  if (strcmp(cmd, "get lat") == 0 || strcmp(cmd, "get lon") == 0 ||
      strcmp(cmd, "gps advert") == 0) return om::rptr::Page::Position;
  if (strcmp(cmd, "get radio") == 0) return om::rptr::Page::Radio;
  if (strcmp(cmd, "get tx") == 0) return om::rptr::Page::Tx;
  if (strcmp(cmd, "get repeat") == 0) return om::rptr::Page::Repeat;
  if (strcmp(cmd, "get advert.interval") == 0 ||
      strcmp(cmd, "get flood.advert.interval") == 0) return om::rptr::Page::Advertising;
  if (strcmp(cmd, "get flood.max") == 0 || strcmp(cmd, "get loop.detect") == 0 ||
      strcmp(cmd, "get path.hash.mode") == 0) return om::rptr::Page::More;
  if (strcmp(cmd, om::rptr::kCliRegionList) == 0 ||
      strcmp(cmd, om::rptr::kCliRegionHomeRead) == 0 ||
      strcmp(cmd, om::rptr::kCliRegionDefaultRead) == 0) return om::rptr::Page::Regions;
  if (strcmp(cmd, om::rptr::kCliNeighboursRead) == 0) return om::rptr::Page::Neighbours;
  if (strcmp(cmd, om::rptr::kCliClockRead) == 0) return om::rptr::Page::Repeater;
  return om::rptr::Page::List;
}

// Where one CLI answer goes: the read-backs that make the page truthful, the two list replies, and
// the console. A write's answer is the Apply's result.
static void rptrTakeReply(const om::RptrReply& rep) {
  g_rptrDirty = true;
  // The console shows the repeater's words; a clock answer is shown in LOCAL time (EST-364).
  {
    char shown[88];
    om::rptr::localClockAnswer(rep.text, shown, sizeof(shown));
    rptrLog("<", shown);
  }
  // A confirmed clock set is the page's new clock, wherever this answer landed (EST-364). Argus
  // answered "OK - clock set: 19:33 - 10/11/2026 UTC" while the shell had already given up on the
  // write, so it arrived as an answer to the follow-up `clock` read; the timestamp in it is the
  // repeater's own, so it is used no matter which command the answer is labelled with.
  uint32_t clockSet = 0;
  if (om::rptr::clockSetReplyParse(rep.text, &clockSet)) {
    g_rptrFacts.clock = clockSet;
    g_rptrFacts.hasClock = true;
    g_rptrClockSent = 0;
    if (!g_rptrResultPending && !g_rptrResultOk && g_rptrResult[0] != 0) {
      // The red "no answer" that the write's own deadline produced is no longer true.
      snprintf(g_rptrResult, sizeof(g_rptrResult), "Clock set");
      g_rptrResultOk = true;
    }
    g_rptrDirty = true;
  }
  if (rep.kind == (uint8_t)om::RptrReplyKind::Status) {
    g_rptrStatusWait = false;
    return;
  }
  if (rep.kind == (uint8_t)om::RptrReplyKind::StatusWait) {
    // EST-364: a request without an answer is not a refusal. The card says "Status  no answer yet"
    // in dim ink; no result line, no red, no "Refused".
    g_rptrStatusWait = true;
    g_rptrDirty = true;
    return;
  }
  if (g_rptrInFlightKind != 0xFF && strcmp(rep.cmd, g_rptrInFlight) == 0) {
    const uint8_t kind = g_rptrInFlightKind;
    g_rptrInFlightKind = 0xFF;
    g_rptrInFlight[0] = 0;
    if (kind == kRptrOutWrite && g_rptrApplyPending && g_rptrApplyWritesLeft > 0) {
      --g_rptrApplyWritesLeft;
      if (!om::rptr::answerOk(rep.text)) {
        g_rptrApplyErr = true;
        om::rptr::answerPlainText(rep.text, g_rptrApplyErrText, sizeof(g_rptrApplyErrText));
      } else if (g_rptrClockSent != 0 && strncmp(rep.cmd, "time ", 5) == 0) {
        // EST-364: an OK for our `time <epoch>` line IS the new repeater clock. The answer's own
        // stamp was already taken above when it had one; this is the fallback the ticket allows
        // ("take the epoch we sent when the answer is OK").
        g_rptrFacts.clock = g_rptrClockSent;
        g_rptrFacts.hasClock = true;
        g_rptrClockSent = 0;
      }
      rptrCheckApplyDone();
    }
  }
  if (rep.kind == (uint8_t)om::RptrReplyKind::Error) {
    if (!g_rptrResultPending) {
      om::rptr::answerPlainText(rep.text, g_rptrResult, sizeof(g_rptrResult));
      g_rptrResultOk = false;
    }
    return;
  }
  if (rep.kind != (uint8_t)om::RptrReplyKind::Cli) return;

  char value[64] = "";
  int32_t number = 0;
  const char* cmd = rep.cmd;
  bool read = true;
  // EST-364: an answer is named by its position in the send order, so only an answer whose SHAPE
  // fits that command may become its value. A mismatch is logged and dropped, never parsed (the
  // 20:34 run had a "get lon" answer parsed as "get lat").
  if (!om::rptr::replyFitsCmd(cmd, rep.text)) {
    Serial.printf("[rptr] answer does not fit \"%s\", ignored: %s\n", cmd, rep.text);
    return;
  }
  if (strcmp(cmd, "get name") == 0) {
    if (om::rptr::valueReplyParse(rep.text, value, sizeof(value))) {
      snprintf(g_rptrFacts.name, sizeof(g_rptrFacts.name), "%s", value);
      g_rptrFacts.nameKnown = true;
    }
  } else if (strcmp(cmd, "get lat") == 0) {
    // EST-364: a position is a PAIR. Both answers, in one clean run, or the page keeps what it had.
    int32_t e6 = 0;
    if (om::rptr::coordReplyParse(rep.text, &e6, om::rptr::CoordAxis::Lat)) {
      g_rptrCoord.takeLat(e6);
      if (g_rptrCoord.complete()) {
        g_rptrFacts.latE6 = g_rptrCoord.latE6;
        g_rptrFacts.lonE6 = g_rptrCoord.lonE6;
        g_rptrFacts.posKnown = true;
      }
    }
  } else if (strcmp(cmd, "get lon") == 0) {
    int32_t e6 = 0;
    if (om::rptr::coordReplyParse(rep.text, &e6, om::rptr::CoordAxis::Lon)) {
      g_rptrCoord.takeLon(e6);
      if (g_rptrCoord.complete()) {
        g_rptrFacts.latE6 = g_rptrCoord.latE6;
        g_rptrFacts.lonE6 = g_rptrCoord.lonE6;
        g_rptrFacts.posKnown = true;
      }
    }
  } else if (strcmp(cmd, "get radio") == 0) {
    if (om::rptr::radioReplyParse(rep.text, &g_rptrFacts.radio)) {
      g_rptrFacts.radio.tx = g_rptrFacts.tx;
      g_rptrFacts.radioKnown = true;
    }
  } else if (strcmp(cmd, "get tx") == 0) {
    if (om::rptr::intReplyParse(rep.text, &number)) {
      g_rptrFacts.tx = (int8_t)number;
      g_rptrFacts.radio.tx = (int8_t)number;
      g_rptrFacts.txKnown = true;
    }
  } else if (strcmp(cmd, "get repeat") == 0) {
    if (om::rptr::switchReplyParse(rep.text, &g_rptrFacts.repeat)) g_rptrFacts.repeatKnown = true;
  } else if (strcmp(cmd, "get advert.interval") == 0) {
    if (om::rptr::intReplyParse(rep.text, &number)) {
      g_rptrFacts.advertLocal = (uint8_t)number;
      g_rptrFacts.advertLocalKnown = true;
    }
  } else if (strcmp(cmd, "get flood.advert.interval") == 0) {
    if (om::rptr::intReplyParse(rep.text, &number)) {
      g_rptrFacts.advertFlood = (uint8_t)number;
      g_rptrFacts.advertFloodKnown = true;
    }
  } else if (strcmp(cmd, "get flood.max") == 0) {
    if (om::rptr::intReplyParse(rep.text, &number)) {
      g_rptrFacts.floodMax = (uint8_t)number;
      g_rptrFacts.floodMaxKnown = true;
    }
  } else if (strcmp(cmd, "get loop.detect") == 0) {
    if (om::rptr::loopReplyParse(rep.text, &g_rptrFacts.loop)) g_rptrFacts.loopKnown = true;
  } else if (strcmp(cmd, "get path.hash.mode") == 0) {
    if (om::rptr::intReplyParse(rep.text, &number)) {
      g_rptrFacts.pathHash = (uint8_t)number;
      g_rptrFacts.pathHashKnown = true;
    }
  } else if (strcmp(cmd, "gps advert") == 0) {
    // EST-368: the repeater's advert location policy. An "unknown command" answer never reaches here
    // (replyFitsCmd refuses it), so the fact stays unknown and Apply falls back to lat/lon 0.
    uint8_t policy = 0;
    if (om::rptr::gpsAdvertParse(rep.text, &policy)) {
      g_rptrFacts.posPolicy = policy;
      g_rptrFacts.posPolicyKnown = true;
    }
  } else if (strcmp(cmd, om::rptr::kCliRegionList) == 0) {
    om::rptr::regionsParse(rep.text, &g_rptrRegions);
    if (!g_rptrRegions.valid) {
      // An error is not an empty map: say what the repeater said instead of inventing a region
      // named "Err".
      om::rptr::answerPlainText(rep.text, g_rptrResult, sizeof(g_rptrResult));
      g_rptrResultOk = false;
      g_rptrResultPending = false;
    }
  } else if (strcmp(cmd, om::rptr::kCliRegionHomeRead) == 0) {
    if (om::rptr::regionHomeReplyParse(rep.text, g_rptrHome, sizeof(g_rptrHome))) {
      g_rptrHomeKnown = true;
    }
  } else if (strcmp(cmd, om::rptr::kCliRegionDefaultRead) == 0) {
    if (om::rptr::regionDefaultReplyParse(rep.text, g_rptrDefault, sizeof(g_rptrDefault))) {
      g_rptrDefaultKnown = true;
    }
  } else if (strcmp(cmd, om::rptr::kCliClockRead) == 0) {
    uint32_t epoch = 0;
    if (om::rptr::clockReplyParse(rep.text, &epoch)) {
      g_rptrFacts.clock = epoch;
      g_rptrFacts.hasClock = true;
    }
  } else if (strcmp(cmd, om::rptr::kCliNeighboursDiscover) == 0) {
    // EST-367: the answer to `discover.neighbors`. "OK - Discover sent" keeps the 60 s window (the
    // slave's table fills while the page shows the progress); anything else means this firmware does
    // not take the command, so the table is read at once and the page says why.
    bool readNow = false;
    if (!g_rptrNbr.onDiscoverAnswer(rep.text, millis(), &readNow)) {
      if (readNow) {
        rptrLog(">", om::rptr::kCliNeighboursRead);
        rptrQueue(om::rptr::kCliNeighboursRead, kRptrOutRead);
        snprintf(g_rptrResult, sizeof(g_rptrResult), "No discovery here, reading the table");
      } else {
        om::rptr::answerPlainText(rep.text, g_rptrResult, sizeof(g_rptrResult));
      }
      g_rptrResultOk = false;
      g_rptrResultPending = false;
      g_rptrDirty = true;
    }
    read = false;   // not a value read-back: nothing to sync from it
  } else if (strcmp(cmd, om::rptr::kCliNeighboursRead) == 0) {
    om::rptr::neighboursParse(rep.text, &g_rptrNeighbours);
    g_rptrNbr.onTable(g_rptrNeighbours.valid);
    if (!g_rptrNeighbours.valid) {
      // Not a neighbour answer (an error or an unknown command): never turn it into "empty".
      om::rptr::answerPlainText(rep.text, g_rptrResult, sizeof(g_rptrResult));
      g_rptrResultOk = false;
      g_rptrResultPending = false;
    }
    static om::MeshContact list[32];
    const int n = om::meshContactList(list, 32);
    for (int i = 0; i < g_rptrNeighbours.count; ++i) {
      om::rptr::Neighbour& nb = g_rptrNeighbours.rows[i];
      nb.named = false;
      for (int k = 0; k < n; ++k) {
        if (memcmp(list[k].pub_key, nb.prefix, 4) == 0) {
          snprintf(nb.name, sizeof(nb.name), "%s", list[k].name);
          nb.named = true;
          break;
        }
      }
      if (!nb.named) {
        snprintf(nb.name, sizeof(nb.name), "%02x%02x%02x%02x", nb.prefix[0], nb.prefix[1],
                 nb.prefix[2], nb.prefix[3]);
      }
    }
    // `valid` comes from neighboursParse(): a real table (rows or "-none-") is valid, an error is not.
  } else {
    read = false;   // a console command's answer: logged, nothing parsed
  }
  if (read && g_rptrLoggedIn && !g_rptrResultPending) {
    const om::rptr::Page from = rptrReadPage(cmd);
    // EST-366 (rule 4): after a FAILED apply the draft is the user's unsaved intent, not what the
    // repeater answered. The read-backs still update Facts, but they must not overwrite the draft
    // (which would hide Apply and throw the error's context away). A success re-syncs as before.
    //
    // EST-368: an answer never resets the WHOLE draft any more, it updates only the fields that are
    // still clean (rptrSyncClean). The page's opening read-backs land while the finger is already on
    // a stepper or a switch, and the first pick used to be wiped by the next answer.
    const bool failedHere = g_rptrApplyErr && g_rptrPage == g_rptrApplyPage;
    if (!failedHere &&
        (g_rptrPage == om::rptr::Page::Repeater || g_rptrPage == from ||
         (from == om::rptr::Page::Regions &&
          (g_rptrPage == om::rptr::Page::HomePicker ||
           g_rptrPage == om::rptr::Page::DefaultPicker ||
           g_rptrPage == om::rptr::Page::AddRegion)))) {
      rptrSyncClean();
    }
  }
}

// One line on the air at a time. A write's silence is an error, not a hang.
static void rptrPumpOutbox() {
  if (g_rptrInFlightKind != 0xFF && (int32_t)(millis() - g_rptrInFlightDeadlineMs) >= 0) {
    if (g_rptrInFlightKind == kRptrOutWrite && g_rptrApplyPending && g_rptrApplyWritesLeft > 0) {
      --g_rptrApplyWritesLeft;
      g_rptrApplyErr = true;
      snprintf(g_rptrApplyErrText, sizeof(g_rptrApplyErrText), "No answer from the repeater");
      rptrCheckApplyDone();
    }
    Serial.printf("[rptr] no answer to %s within %lu ms\n", g_rptrInFlight,
                  (unsigned long)(g_rptrInFlightDeadlineMs - g_rptrInFlightMs));
    // EST-367: a discover that never answers is the same fallback as "unknown command": read the
    // repeater's table now instead of waiting out the 60 s window for a scan it never started.
    if (strcmp(g_rptrInFlight, om::rptr::kCliNeighboursDiscover) == 0 &&
        g_rptrNbr.phase == om::rptr::NbrScanPhase::Discovering) {
      bool readNow = false;
      g_rptrNbr.onDiscoverTimeout(&readNow);
      if (readNow) {
        rptrLog(">", om::rptr::kCliNeighboursRead);
        rptrQueue(om::rptr::kCliNeighboursRead, kRptrOutRead);
        snprintf(g_rptrResult, sizeof(g_rptrResult), "No answer, reading the table it has");
      }
    }
    // EST-364: a line we gave up on leaves the reply order too, and any position pair in progress is
    // dropped: the next answer cannot be trusted to belong to the command it is labelled with.
    om::rptrForgetPending(g_rptrInFlight);
    g_rptrCoord.abandon();
    g_rptrDirty = true;
    g_rptrInFlightKind = 0xFF;
    g_rptrInFlight[0] = 0;
  }
  if (g_rptrInFlightKind != 0xFF) return;
  if (g_rptrOutHead >= g_rptrOutCount) {
    g_rptrOutHead = g_rptrOutCount = 0;
    return;
  }
  const RptrOutLine& o = g_rptrOut[g_rptrOutHead++];
  snprintf(g_rptrInFlight, sizeof(g_rptrInFlight), "%s", o.line);
  g_rptrInFlightKind = o.kind;
  g_rptrInFlightMs = millis();
  // EST-364: a `get lat` opens a new position pair, so a pair left over from the last read is gone.
  if (strcmp(o.line, "get lat") == 0) g_rptrCoord.reset();
  if (!om::rptrCliSend(o.line, 0)) {
    Serial.printf("[rptr] not sent (no login): %s\n", o.line);
    g_rptrInFlightKind = 0xFF;
    if (o.kind == kRptrOutWrite && g_rptrApplyPending && g_rptrApplyWritesLeft > 0) {
      --g_rptrApplyWritesLeft;
      g_rptrApplyErr = true;
      snprintf(g_rptrApplyErrText, sizeof(g_rptrApplyErrText), "Not logged in, nothing sent");
      rptrCheckApplyDone();
    }
  } else {
    // EST-364: the per-line deadline is upstream's own est_timeout plus the margin, never under the
    // floor. The old fixed 3 s cut off Argus's CLI answers (3-4 s) and threw a confirmed clock set
    // away.
    const uint32_t est = om::rptrCliEstimateMs();
    const uint32_t deadline = om::rptr::requestDeadlineMs(est, om::rptr::kLoginExtraMs,
                                                          om::rptr::kCliMinMs);
    g_rptrInFlightDeadlineMs = g_rptrInFlightMs + deadline;
  }
  if (g_rptrOutHead >= g_rptrOutCount) g_rptrOutHead = g_rptrOutCount = 0;
}

// The asynchronous things the glass follows: the login answer, the CLI replies, the status and the
// list.
static void rptrPump() {
  rptrPumpOutbox();
  const uint32_t serial = om::rptrReplySerial();
  if (serial != g_rptrReplySerial) {
    g_rptrReplySerial = serial;
    om::RptrReply rep;
    while (om::rptrReplyTake(&rep)) rptrTakeReply(rep);
  }
  const om::rptr::LoginMachine& login = om::rptrLogin();
  if (!g_rptrList && g_rptrPage == om::rptr::Page::Repeater && !g_rptrLoggedIn) {
    if (login.loggedIn()) {
      g_rptrLoggedIn = true;
      g_rptrLoginWait = 0;
      g_rptrOsk = false;
      g_rptrPw[0] = 0;
      if (login.answer.clockValid) {
        g_rptrFacts.clock = login.answer.clock;
        g_rptrFacts.hasClock = true;
      }
      snprintf(g_rptrResult, sizeof(g_rptrResult), "Logged in");
      g_rptrResultOk = true;
      g_rptrResultPending = false;
      om::rptrStatusRequest();
      // EST-366 (rule 3): from this moment the ONLY region truth is the repeater's own CLI answer.
      // The anonymous scan cache (which can be minutes old) and any list left from before the login
      // are dropped, and `region` is the FIRST read queued, so the logged-in list replaces the scan
      // list before the page has a chance to draw a stale name.
      g_rptrAnon = om::rptr::AnonFacts();
      g_rptrRegions = om::rptr::RegionList();
      g_rptrHome[0] = 0;
      g_rptrHomeKnown = false;
      g_rptrDefault[0] = 0;
      g_rptrDefaultKnown = false;
      // The repeater page's own facts, one command at a time through the queue.
      static const char* const kFirst[] = {"region", "region home", "region default", "get name",
                                           "get lat", "get lon", "get radio", "get tx",
                                           "get repeat", "clock"};
      for (const char* line : kFirst) rptrQueue(line, kRptrOutRead);
      rptrSyncDraft();
      redraw();
    } else if (login.state == om::rptr::LoginState::Refused ||
               login.state == om::rptr::LoginState::NoAnswer) {
      if (g_rptrLoginWait != 0) {
        g_rptrLoginWait = 0;
        snprintf(g_rptrResult, sizeof(g_rptrResult),
                 "No answer. The password is wrong, or the repeater is out of range.");
        g_rptrResultOk = false;
        g_rptrResultPending = false;
        redraw();
      }
    }
  }
  const uint32_t st = om::rptrStatusSerial();
  if (st != g_rptrStatusSerial) {
    g_rptrStatusSerial = st;
    if (!g_rptrList && g_rptrPage == om::rptr::Page::Repeater) redraw();
  }
  const uint32_t sc = om::rptrScanSerial();
  if (sc != g_rptrScanSerialSeen) {
    g_rptrScanSerialSeen = sc;
    if (g_rptrList) {
      redraw();
    } else if (!g_rptrLoggedIn && g_rptrPage == om::rptr::Page::Repeater) {
      const om::rptr::AnonFacts before = g_rptrAnon;
      rptrFillAnonFromScan();
      if (g_rptrAnon.valid != before.valid ||
          strcmp(g_rptrAnon.regions, before.regions) != 0 ||
          g_rptrAnon.clock != before.clock) {
        redraw();
      }
    }
  }
  // EST-367: the neighbour discovery window. While it runs the page redraws once a second (the bar
  // and the seconds left); when the model says the window is over, `neighbors` goes out ONCE. Nothing
  // here waits: tick() is a comparison on millis().
  if (g_rptrNbr.running()) {
    bool readNow = false;
    g_rptrNbr.tick(millis(), &readNow);
    if (readNow) {
      rptrLog(">", om::rptr::kCliNeighboursRead);
      rptrQueue(om::rptr::kCliNeighboursRead, kRptrOutRead);
      snprintf(g_rptrResult, sizeof(g_rptrResult), "Reading the neighbour table");
      g_rptrResultOk = true;
      g_rptrResultPending = false;
      g_rptrDirty = true;
    }
    static uint32_t lastNbrDraw = 0;
    if (!g_rptrList && g_rptrPage == om::rptr::Page::Neighbours &&
        millis() - lastNbrDraw >= 1000) {
      lastNbrDraw = millis();
      g_rptrDirty = true;
    }
  }
  // Every read-back and every Apply answer changes what the open page shows; one redraw per pass.
  if (g_rptrDirty) {
    g_rptrDirty = false;
    if (visible() && !g_rptrList) redraw();
  }
}

static void rptrBuildList(om::ui::RptrView& v) {
  v.scanning = om::rptrScan().listening;
  om::rptrScan().stateText(v.state, sizeof(v.state));
  const char* remembered[8] = {};
  int rn = om::rptrRememberedCount();
  if (rn > 8) rn = 8;
  for (int i = 0; i < rn; ++i) remembered[i] = om::rptrRememberedName(i);
  // EST-363: the list rows are a static, not 20 x 59 B on the loop stack (rptrBuildView used to be
  // 7,504 B of frame: this array plus the RptrView temporary). Only the loop task runs this.
  static om::rptr::ListRow rows[om::ui::kRptrMaxItems];
  const int n = om::rptr::listRows(om::rptrScan(), remembered, rn, rows, om::ui::kRptrMaxItems);
  const uint32_t nowWall = om::timeKeeper().now();
  const uint32_t nowUp = millis() / 1000;
  const bool nowReal = om::timeKeeper().realSinceBoot();
  const om::rptr::FoundList& found = om::rptrScan();
  for (int i = 0; i < n; ++i) {
    om::ui::RptrItem& it = v.items[i];
    snprintf(it.name, sizeof(it.name), "%s", rows[i].name);
    it.saved = rows[i].saved;
    it.foundIndex = rows[i].foundIndex;
    if (rows[i].foundIndex < 255 && rows[i].foundIndex < found.count) {
      const om::rptr::Found& f = found.rows[rows[i].foundIndex];
      char heard[24] = "";
      om::ui::stampText(heard, sizeof(heard), om::ui::Stamp{f.heardWall, f.heardMs / 1000, f.heardReal},
                        nowWall, nowUp, nowReal, om::ui::StampForm::DayTime);
      if (rows[i].signal[0] && heard[0]) {
        snprintf(it.meta, sizeof(it.meta), "%s \xC2\xB7 %s", rows[i].signal, heard);
      } else if (rows[i].signal[0]) {
        snprintf(it.meta, sizeof(it.meta), "%s", rows[i].signal);
      } else {
        snprintf(it.meta, sizeof(it.meta), "heard %s", heard);
      }
    } else {
      snprintf(it.meta, sizeof(it.meta), "remembered, not heard in the last scan");
    }
  }
  v.count = n;
  g_rptrCount = n;
}

static bool rptrMapDirty();   // defined with the map picker's tap handler below

static void rptrBuildPage(om::ui::RptrView& v) {
  const om::rptr::Status& status = om::rptrStatus();
  const om::rptr::LoginMachine& login = om::rptrLogin();
  om::rptr::ScreenIn in;
  in.page = g_rptrPage;
  in.loggedIn = g_rptrLoggedIn;
  in.busy = login.busy();
  in.remembered = g_rptrRemembered;
  in.name = g_rptrNode;
  in.status = &status;
  in.facts = &g_rptrFacts;
  in.draft = &g_rptrDraft;
  in.regions = &g_rptrRegions;
  in.neighbours = &g_rptrNeighbours;
  // EST-367: the discovery window's state and the clocks its progress and the neighbours' heard
  // times are measured with.
  in.scan = &g_rptrNbr;
  in.nowMs = millis();
  in.nowWall = om::timeKeeper().now();
  in.nowReal = om::timeKeeper().realSinceBoot();
  in.anon = &g_rptrAnon;
  in.home = g_rptrHomeKnown ? g_rptrHome : nullptr;
  in.defaultScope = g_rptrDefaultKnown ? g_rptrDefault : nullptr;
  in.clockWrong = g_rptrLoggedIn && om::rptr::clockWrong(g_rptrFacts.hasClock, g_rptrFacts.clock,
                                                         om::timeKeeper().now(),
                                                         om::timeKeeper().realSinceBoot());
  in.statusWait = g_rptrStatusWait;
  const char* logs[kRptrLogLines] = {};
  for (int i = 0; i < g_rptrLogCount && i < kRptrLogLines; ++i) logs[i] = g_rptrLog[i];
  in.log = logs;
  in.logCount = g_rptrLogCount;
  in.consoleInput = g_rptrCmd;
  om::rptr::buildScreen(in, &v.screen);
  // Rule 4 changes the layout when Apply appears/disappears (the body shrinks or grows). Keep the
  // scroll inside the new clamp, so a page that was scrolled to the bottom never shows blank space
  // under the last row after the button goes away.
  {
    const om::ui::RptrPageLayout a = om::ui::rptrPageLayoutFor(
        om::ui::l2(), v.screen.cellCount > 0, v.screen.banner[0] != 0, v.screen.hasApply,
        v.screen.bigButton, v.screen.hasConsole);
    const int maxScroll = om::ui::rptrPageMaxScroll(a, v.screen.count);
    if (g_rptrPageScroll > maxScroll) g_rptrPageScroll = maxScroll;
    if (g_rptrPageScroll < 0) g_rptrPageScroll = 0;
  }
  snprintf(v.result, sizeof(v.result), "%s", g_rptrResult);
  v.resultOk = g_rptrResultOk;
  v.resultPending = g_rptrResultPending;

  // EST-366 (rule 6): the map picker's own view state. The centre itself is map.cpp's; the two lines
  // under the map are the DRAFT position (what Apply would send) and the zoom, so what the eye reads
  // is exactly what the CLI will receive.
  v.mapPick = g_rptrMapPick;
  if (g_rptrMapPick) {
    char lat[20];
    char lon[20];
    om::rptr::degreesText(lat, sizeof(lat), g_rptrDraft.latE6);
    om::rptr::degreesText(lon, sizeof(lon), g_rptrDraft.lonE6);
    snprintf(v.mapCoords, sizeof(v.mapCoords), "%s, %s", lat, lon);
    snprintf(v.mapHint, sizeof(v.mapHint), "z%d", g_rptrPickZoom);
    v.mapGps = gps().valid;
    v.mapApply = rptrMapDirty();
  }
}

static void buildRptrView(om::ui::RptrView& v) {
  // EST-363: reset in place. `v = om::ui::RptrView()` built a 6,204-byte temporary on the loop task's
  // stack on every RPTR redraw and left 56 bytes free after the page before login (heartbeat in
  // docs/est362-login-crash.log), which is why the next build panicked.
  v.reset();
  status(v.status);
  v.tabs = tabBar(ui::Tab::Rptr);
  v.list = g_rptrList;
  v.page = g_rptrPage;
  v.scroll = g_rptrScroll;
  v.pageScroll = g_rptrPageScroll;
  if (g_rptrList) {
    rptrBuildList(v);
  } else {
    rptrBuildPage(v);
  }
  v.modal = g_rptrAskOpen;
  if (g_rptrAskOpen) {
    snprintf(v.modalText, sizeof(v.modalText), "%s", g_rptrAsk.text);
    snprintf(v.modalVerb, sizeof(v.modalVerb), "%s", g_rptrAsk.verb);
    v.modalWhat = g_rptrAskWhat;
  }
  v.osk = g_rptrOsk;
  v.shift = g_rptrShift;
  v.nums = g_rptrNums;
  v.field = g_rptrField;
  v.secret = g_rptrSecret;
  v.okLabel = g_rptrEditWhat == 1 ? "Log in" : "Send";
  snprintf(v.editTitle, sizeof(v.editTitle), "%s", g_rptrEditTitle);
  snprintf(v.text, sizeof(v.text), "%s", g_rptrText);
  v.textLen = g_rptrTextLen;
}

static void drawRptrTab() {
  // EST-363: the loop task's stack at the exact place EST-362 panicked, printed once per view change
  // so the serial log stays readable. `stack_free` is the FreeRTOS high-water mark in bytes (it only
  // ever falls), so before == after means this build added nothing to the deepest path so far.
  static bool tracedList = false;
  static int8_t tracedPage = -1;
  const bool trace = g_rptrList != tracedList || (int8_t)g_rptrPage != tracedPage;
  const unsigned before = uxTaskGetStackHighWaterMark(nullptr);
  buildRptrView(g_rptrViewStore);
  const unsigned after = uxTaskGetStackHighWaterMark(nullptr);
  if (trace) {
    tracedList = g_rptrList;
    tracedPage = (int8_t)g_rptrPage;
    Serial.printf("[rptr] build %s page=%s stack_free=%u -> %u (of %u)\n",
                  g_rptrList ? "list" : "view",
                  g_rptrList ? "-" : om::rptr::pageName(g_rptrPage), before, after, kLoopStackSize);
  }
  om::ui::rptrScreenDraw(g_rptrViewStore);
}

// ------------------------------------------------------------------------------------------ the sheet
static void rptrOpenSheet(uint8_t what, om::ui::KeyboardField field, const char* title,
                          const char* initial, bool secret) {
  g_rptrEditWhat = what;
  g_rptrField = (uint8_t)field;
  g_rptrSecret = secret;
  snprintf(g_rptrEditTitle, sizeof(g_rptrEditTitle), "%s", title);
  snprintf(g_rptrText, sizeof(g_rptrText), "%s", initial != nullptr ? initial : "");
  g_rptrTextLen = (int)strlen(g_rptrText);
  g_rptrOsk = true;
  g_rptrNums = field == om::ui::KeyboardField::RptrPosition;
  g_rptrShift = false;
}

static bool rptrParseCoord(const char* text, bool lat, int32_t* e6) {
  if (text == nullptr || e6 == nullptr || text[0] == 0) return false;
  char* end = nullptr;
  const double v = strtod(text, &end);
  if (end == text || end == nullptr || *end != 0) return false;
  if (lat && (v < -90.0 || v > 90.0)) return false;
  if (!lat && (v < -180.0 || v > 180.0)) return false;
  *e6 = (int32_t)lround(v * 1000000.0);
  return true;
}

static void rptrSheetCommit() {
  switch (g_rptrEditWhat) {
    case 1: {   // the repeater password
      if (g_rptrText[0] == 0) {
        toast("password required");
        return;
      }
      snprintf(g_rptrPw, sizeof(g_rptrPw), "%s", g_rptrText);
      g_rptrOsk = false;
      rptrLoginNow();
      return;
    }
    case 2:   // name
      if (g_rptrText[0] == 0) {
        toast("name cannot be empty");
        return;
      }
      snprintf(g_rptrDraft.name, sizeof(g_rptrDraft.name), "%s", g_rptrText);
      break;
    case 3:   // latitude
      if (!rptrParseCoord(g_rptrText, true, &g_rptrDraft.latE6)) {
        toast("enter a latitude, e.g. 46.25000");
        return;
      }
      break;
    case 4:   // longitude
      if (!rptrParseCoord(g_rptrText, false, &g_rptrDraft.lonE6)) {
        toast("enter a longitude, e.g. 14.40000");
        return;
      }
      break;
    case 5: {   // a new region's name
      char name[om::rptr::kRegionNameMax + 1] = "";
      om::rptr::regionNameFilter(g_rptrText, name, sizeof(name));
      if (!om::rptr::regionNameValid(name)) {
        toast("a region name is needed");
        return;
      }
      snprintf(g_rptrDraft.addName, sizeof(g_rptrDraft.addName), "%s", name);
      g_rptrDraft.hasAdd = true;
      break;
    }
    case 6:   // the console's free command
      snprintf(g_rptrCmd, sizeof(g_rptrCmd), "%s", g_rptrText);
      break;
    default:
      break;
  }
  g_rptrOsk = false;
  drawRptrTab();
}

static void rptrAppendText(char ch) {
  const om::ui::KeyboardField field = (om::ui::KeyboardField)g_rptrField;
  if (!om::ui::keyboardAllowsChar(field, ch)) return;
  if (!om::ui::keyboardAcceptsBytes(field, g_rptrTextLen, 1)) return;
  if ((size_t)g_rptrTextLen + 1 >= sizeof(g_rptrText)) return;
  g_rptrText[g_rptrTextLen++] = ch;
  g_rptrText[g_rptrTextLen] = 0;
  if (field == om::ui::KeyboardField::RptrPassword) {
    char filtered[om::rptr::kPasswordMax + 1] = "";
    om::rptr::passwordFilter(g_rptrText, filtered, sizeof(filtered));
    snprintf(g_rptrText, sizeof(g_rptrText), "%s", filtered);
    g_rptrTextLen = (int)strlen(g_rptrText);
  }
}

static bool rptrKeyPressed(const om::ui::RptrKeyHit& key) {
  switch (key.kind) {
    case om::ui::KeyKind::Shift:
      g_rptrShift = !g_rptrShift;
      break;
    case om::ui::KeyKind::Mode:
      g_rptrNums = !g_rptrNums;
      g_rptrShift = false;
      break;
    case om::ui::KeyKind::Del:
      if (g_rptrSecret) {
        if (g_rptrTextLen > 0) {
          --g_rptrTextLen;
          g_rptrText[g_rptrTextLen] = 0;
        }
      } else {
        om::ui::composeBackspace(g_rptrText);
        g_rptrTextLen = (int)strlen(g_rptrText);
      }
      break;
    case om::ui::KeyKind::Send:
      rptrSheetCommit();
      return true;
    case om::ui::KeyKind::Space:
    case om::ui::KeyKind::Char: {
      const char ch = (key.kind == om::ui::KeyKind::Space) ? ' ' : key.ch;
      rptrAppendText(ch);
      break;
    }
    case om::ui::KeyKind::Reveal:
    case om::ui::KeyKind::Emoji:
    default:
      break;
  }
  drawRptrTab();
  return true;
}

// -------------------------------------------------------------------------------------- the page taps
static void rptrToggleRegion(int i) {
  if (i < 0 || i >= g_rptrRegions.count || i >= om::rptr::kMaxRegions) return;
  g_rptrDraft.flood[i] = !g_rptrDraft.flood[i];
  g_rptrDraft.floodChanged[i] = g_rptrDraft.flood[i] != g_rptrRegions.rows[i].allow;
}

static void rptrPickerChoose(const om::rptr::UiRow& row) {
  if (g_rptrPage == om::rptr::Page::HomePicker) {
    if (row.index == 0) snprintf(g_rptrDraft.home, sizeof(g_rptrDraft.home), "*");
    else if (row.index >= 2 && (row.index - 2) < g_rptrRegions.count) {
      snprintf(g_rptrDraft.home, sizeof(g_rptrDraft.home), "%s",
               g_rptrRegions.rows[row.index - 2].name);
    } else {
      return;
    }
    const char* was = g_rptrHomeKnown ? g_rptrHome : "*";
    g_rptrDraft.homeChanged = strcmp(g_rptrDraft.home, was) != 0;
  } else if (g_rptrPage == om::rptr::Page::DefaultPicker) {
    if (row.index == 0) {
      g_rptrDraft.defaultNone = true;
      g_rptrDraft.defaultScope[0] = 0;
    } else if (row.index == 1) {
      g_rptrDraft.defaultNone = false;
      snprintf(g_rptrDraft.defaultScope, sizeof(g_rptrDraft.defaultScope), "*");
    } else if ((row.index - 2) < g_rptrRegions.count) {
      g_rptrDraft.defaultNone = false;
      snprintf(g_rptrDraft.defaultScope, sizeof(g_rptrDraft.defaultScope), "%s",
               g_rptrRegions.rows[row.index - 2].name);
    } else {
      return;
    }
    const char* was = g_rptrDefaultKnown ? g_rptrDefault : "";
    g_rptrDraft.defaultChanged =
        g_rptrDraft.defaultNone ? (was[0] != 0) : (strcmp(g_rptrDraft.defaultScope, was) != 0);
  }
}

static void rptrCoordText(char* out, size_t cap, int32_t e6) {
  const bool neg = e6 < 0;
  const int64_t a = neg ? -(int64_t)e6 : (int64_t)e6;
  snprintf(out, cap, "%s%lld.%05u", neg ? "-" : "", (long long)(a / 1000000),
           (unsigned)(a % 1000000));
}

static void rptrPageRowTapped(const om::rptr::UiRow& row, bool key, int dir) {
  switch (row.act) {
    case om::rptr::Act::None:
    case om::rptr::Act::Forget:
    case om::rptr::Act::Refresh:
    case om::rptr::Act::SetClock:
      break;
    case om::rptr::Act::ConsoleSend:
      rptrSendConsole();
      break;
    case om::rptr::Act::Open:
      rptrGoPage((om::rptr::Page)row.index);
      break;
    case om::rptr::Act::Restart:
      rptrOpenAsk(om::rptr::confirmRestart(), 2);
      break;
    case om::rptr::Act::Apply: {
      const om::rptr::ConfirmAsk ask = om::rptr::confirmForApply(g_rptrPage, g_rptrDraft);
      if (ask.need) {
        rptrOpenAsk(ask, 1);
        break;
      }
      rptrApplyNow();
      break;
    }
    case om::rptr::Act::Toggle:
      if (g_rptrPage == om::rptr::Page::Repeat) {
        g_rptrDraft.v.repeat = !g_rptrDraft.v.repeat;
      } else if (g_rptrPage == om::rptr::Page::Position) {
        // EST-368: "Share position". Off means the repeater's advert carries no position: its own
        // `gps advert none` where the firmware has it, otherwise lat/lon 0 (MeshCore's "none"), plus
        // one advert so the other nodes learn it at once (rptr_page.cpp's applyPlan).
        g_rptrDraft.sharePos = !g_rptrDraft.sharePos;
        Serial.printf("[rptr] share position %s\n", g_rptrDraft.sharePos ? "on" : "off");
      } else if (g_rptrPage == om::rptr::Page::More) {
        if (row.index == 10) {
          int v = (int)g_rptrDraft.v.loop + 1;
          if (v > 3) v = 0;
          g_rptrDraft.v.loop = (om::rptr::LoopDetect)v;
        } else if (row.index == 11) {
          g_rptrDraft.v.pathHash = (uint8_t)((g_rptrDraft.v.pathHash + 1) % 3);
        }
      } else if (g_rptrPage == om::rptr::Page::AddRegion) {
        // Cycle the optional parent: the wildcard (none) and every named region.
        int v = (int)g_rptrDraft.parentSel + 1;
        int max = g_rptrRegions.count;
        if (v > max - 1) v = -1;
        g_rptrDraft.parentSel = (int8_t)v;
        if (g_rptrDraft.parentSel >= 0) {
          snprintf(g_rptrDraft.addParent, sizeof(g_rptrDraft.addParent), "%s",
                   g_rptrRegions.rows[g_rptrDraft.parentSel].name);
        } else {
          g_rptrDraft.addParent[0] = 0;
        }
      } else if (g_rptrPage == om::rptr::Page::HomePicker ||
                 g_rptrPage == om::rptr::Page::DefaultPicker) {
        rptrPickerChoose(row);
      }
      break;
    case om::rptr::Act::Step:
      if (g_rptrPage == om::rptr::Page::Radio) {
        om::rptr::radioStep(&g_rptrDraft.v.radio, (om::rptr::RadioKey)row.index, dir);
      } else if (g_rptrPage == om::rptr::Page::Tx) {
        om::rptr::radioStep(&g_rptrDraft.v.radio, om::rptr::RadioKey::Tx, dir);
      } else if (g_rptrPage == om::rptr::Page::Advertising) {
        if (row.index == 0) om::rptr::advertLocalStep(&g_rptrDraft.v, dir);
        else om::rptr::advertFloodStep(&g_rptrDraft.v, dir);
      } else if (g_rptrPage == om::rptr::Page::More) {
        om::rptr::floodMaxStep(&g_rptrDraft.v, dir);
      }
      break;
    case om::rptr::Act::Edit:
      if (g_rptrPage == om::rptr::Page::Name) {
        rptrOpenSheet(2, om::ui::KeyboardField::NodeName, "name", g_rptrDraft.name, false);
      } else if (g_rptrPage == om::rptr::Page::Position) {
        char v[24];
        if (row.index == 0) {
          rptrCoordText(v, sizeof(v), g_rptrDraft.latE6);
          rptrOpenSheet(3, om::ui::KeyboardField::RptrPosition, "latitude", v, false);
        } else {
          rptrCoordText(v, sizeof(v), g_rptrDraft.lonE6);
          rptrOpenSheet(4, om::ui::KeyboardField::RptrPosition, "longitude", v, false);
        }
      } else if (g_rptrPage == om::rptr::Page::AddRegion) {
        rptrOpenSheet(5, om::ui::KeyboardField::RptrRegion, "region name", g_rptrDraft.addName,
                      false);
      } else if (g_rptrPage == om::rptr::Page::Console) {
        rptrOpenSheet(6, om::ui::KeyboardField::WifiPassword, "command", g_rptrCmd, false);
      }
      break;
    case om::rptr::Act::Pick:
      rptrGoPage(row.index == 0 ? om::rptr::Page::HomePicker : om::rptr::Page::DefaultPicker);
      break;
    case om::rptr::Act::AddRegion:
      rptrGoPage(om::rptr::Page::AddRegion);
      break;
    case om::rptr::Act::RegionToggle:
      if (key) {
        rptrRemoveRegion(row.label);
      } else {
        rptrToggleRegion(row.index);
      }
      break;
    case om::rptr::Act::RemoveRegion:
      rptrRemoveRegion(row.label);
      break;
    case om::rptr::Act::Discover:
      // EST-367: `discover.neighbors` IS a repeater command in v1.18.0 (simple_repeater's own
      // MyMesh.cpp handleCommand, not CommonCLI): it answers "OK - Discover sent", sends a zero-hop
      // node discover, and the repeater's neighbour table fills over about 60 s. So the button sends
      // it, the page shows that window as progress, and rptrPump sends `neighbors` when it ends. A
      // repeater that answers "unknown command" (or nothing) falls back to reading its table now.
      g_rptrNeighbours = om::rptr::NeighbourList();
      g_rptrNbr.start(millis());
      rptrLog(">", om::rptr::kCliNeighboursDiscover);
      rptrQueue(om::rptr::kCliNeighboursDiscover, kRptrOutRead);
      snprintf(g_rptrResult, sizeof(g_rptrResult), "Asking the repeater to discover neighbours");
      g_rptrResultOk = true;
      g_rptrResultPending = false;
      break;
    case om::rptr::Act::MapPick:
      // EST-366 (rule 6): open the map centred on the repeater's draft position (or our GPS, or the
      // page's last default) with the centre point under the Map's own cross (EST-393).
      {
        g_rptrMapPick = true;
        g_rptrPickZoom = 15;
        double lat = 0.0;
        double lon = 0.0;
        if (g_rptrDraft.latE6 != 0 || g_rptrDraft.lonE6 != 0) {
          lat = (double)g_rptrDraft.latE6 / 1e6;   // the repeater's own (or the typed) position
          lon = (double)g_rptrDraft.lonE6 / 1e6;
        } else if (om::posOwn().valid) {
          // EST-384: no position on the repeater: start on OUR effective position (the Fixed point
          // when Fixed is on, else the GPS fix), not on the raw parser.
          lat = om::posOwn().lat;
          lon = om::posOwn().lon;
        } else {
          lat = 46.0569;                            // Ljubljana: the map pack's centre
          lon = 14.5058;
        }
        om::mapSetView(lat, lon, g_rptrPickZoom);
        // The view is centred on exactly this coordinate, so the draft stays the value that was
        // opened. (Projecting the centre back through the float maths could add a micro-degree and
        // make Apply look needed before the finger has moved.)
        g_rptrDraft.latE6 = (int32_t)lround(lat * 1e6);
        g_rptrDraft.lonE6 = (int32_t)lround(lon * 1e6);
      }
      break;
    case om::rptr::Act::ConsoleEdit:
      rptrOpenSheet(6, om::ui::KeyboardField::WifiPassword, "command", g_rptrCmd, false);
      break;
    case om::rptr::Act::LogIn:
    default:
      break;
  }
}

// One tap on the RPTR tab. True when it was consumed.
static bool rptrMapDirty() {
  return g_rptrDraft.latE6 != g_rptrFacts.latE6 || g_rptrDraft.lonE6 != g_rptrFacts.lonE6;
}

static void rptrMapPickCentreToDraft() {
  double lat = 0, lon = 0;
  // EST-370: the same centre point the Map tab's dot marks (ui/map_chrome.h), not a second number.
  om::mapScreenToLatLon(om::ui::kMapCentreX, om::ui::kMapCentreY, lat, lon);
  g_rptrDraft.latE6 = (int32_t)(lat * 1e6);
  g_rptrDraft.lonE6 = (int32_t)(lon * 1e6);
}

static bool handleRptrTap(int x, int y) {
  const om::ui::Layout& L = om::ui::l2();
  buildRptrView(g_rptrViewStore);

  // EST-366 (rule 6): the map picker's own keys. A tap on the map itself re-drops the centre point
  // under the cross (the pan happens while the finger moves); it changes nothing else.
  if (g_rptrMapPick) {
    const om::ui::RptrMapPickLayout m = om::ui::rptrMapPickLayoutFor(L, rptrMapDirty());
    switch (om::ui::rptrMapPickHitAt(m, x, y)) {
      case om::ui::RptrMapPickHit::Back:
        g_rptrMapPick = false;
        drawRptrTab();
        return true;
      case om::ui::RptrMapPickHit::ZoomIn:
        om::mapZoomBy(1);
        g_rptrPickZoom = om::mapZoom();
        drawRptrTab();
        return true;
      case om::ui::RptrMapPickHit::ZoomOut:
        om::mapZoomBy(-1);
        g_rptrPickZoom = om::mapZoom();
        drawRptrTab();
        return true;
      case om::ui::RptrMapPickHit::UseGps:
        if (!gps().valid) {
          toast("No GPS fix yet");
          return true;
        }
        om::mapSetView(gps().lat, gps().lon, g_rptrPickZoom);
        g_rptrDraft.latE6 = (int32_t)(gps().lat * 1e6);
        g_rptrDraft.lonE6 = (int32_t)(gps().lon * 1e6);
        drawRptrTab();
        return true;
      case om::ui::RptrMapPickHit::Apply:
        // Back to the Position page first: the Apply's result line lives in that page's header, and
        // rule 4 hides Apply again as soon as the read-back matches.
        g_rptrMapPick = false;
        rptrApplyNow();
        drawRptrTab();
        return true;
      case om::ui::RptrMapPickHit::Map:
        rptrMapPickCentreToDraft();
        drawRptrTab();
        return true;
      default:
        return true;   // the panel/back are over the map; nothing else is a map tap
    }
  }

  if (g_rptrAskOpen) {
    const om::ui::RptrModalLayout m = om::ui::rptrModalLayoutFor(L, g_rptrAsk.verb);
    const om::ui::RptrModalHit hit = om::ui::rptrModalHitAt(m, x, y);
    g_rptrAskOpen = false;
    if (hit == om::ui::RptrModalHit::Confirm) rptrRunAsk();
    else g_rptrResult[0] = 0;   // Cancel, and a tap outside is a Cancel (consumed)
    drawRptrTab();
    return true;
  }

  if (g_rptrOsk) {
    const om::ui::RptrKeyHit key = om::ui::rptrEditorKeyAt(g_rptrViewStore, x, y);
    if (key.pressed) return rptrKeyPressed(key);
    const om::ui::ChatLayout g = om::ui::chatLayoutFor(L, true, false, g_rptrNums, false,
                                                       om::ui::rptrEditorField(g_rptrViewStore));
    if (y < g.sheet.y) {
      g_rptrOsk = false;
      drawRptrTab();
    }
    return true;
  }

  if (g_rptrList) {
    const om::ui::RptrListLayout r = om::ui::rptrListLayoutFor(L);
    const om::ui::RptrHitResult hit =
        om::ui::rptrListHitAt(r, g_rptrViewStore.count, g_rptrScroll, x, y);
    switch (hit.kind) {
      case om::ui::RptrHit::Scan:
        if (!om::rptrScanStart()) {
          toast("Scan not sent: mesh is not up");
          return true;
        }
        toast("Scan sent, listening");
        drawRptrTab();
        return true;
      case om::ui::RptrHit::ListRow:
        if (hit.row < 0 || hit.row >= g_rptrViewStore.count) return true;
        rptrOpenAt(g_rptrViewStore.items[hit.row].name, g_rptrViewStore.items[hit.row].foundIndex);
        drawRptrTab();
        return true;
      default:
        return false;
    }
  }

  const om::ui::RptrPageLayout a =
      om::ui::rptrPageLayoutFor(L, g_rptrViewStore.screen.cellCount > 0,
                                g_rptrViewStore.screen.banner[0] != 0,
                                g_rptrViewStore.screen.hasApply, g_rptrViewStore.screen.bigButton,
                                g_rptrViewStore.screen.hasConsole);
  const om::ui::RptrHitResult hit =
      om::ui::rptrPageHitAt(a, g_rptrViewStore.screen, g_rptrPageScroll, x, y);
  switch (hit.kind) {
    case om::ui::RptrHit::Back:
      rptrBack();
      drawRptrTab();
      return true;
    case om::ui::RptrHit::Refresh:
      if (!om::rptrStatusRequest()) toast("Status not requested");
      rptrQueue(om::rptr::kCliClockRead, kRptrOutRead);
      drawRptrTab();
      return true;
    case om::ui::RptrHit::BannerButton:
      rptrClockPressed();
      drawRptrTab();
      return true;
    case om::ui::RptrHit::Apply: {
      const om::rptr::ConfirmAsk ask = om::rptr::confirmForApply(g_rptrPage, g_rptrDraft);
      if (ask.need) {
        rptrOpenAsk(ask, 1);
        break;
      }
      rptrApplyNow();
      break;
    }
    case om::ui::RptrHit::BigButton:
      if (g_rptrRemembered) {
        char pw[om::rptr::kPasswordMax + 1] = "";
        if (!om::rptrPasswordFor(g_rptrNode, pw, sizeof(pw))) {
          toast("No stored password, type it");
          rptrOpenSheet(1, om::ui::KeyboardField::RptrPassword, "repeater password", "", true);
        } else if (!om::rptrLoginStart(g_rptrNode, pw)) {
          toast("Login not sent: no such contact");
          rptrOpenSheet(1, om::ui::KeyboardField::RptrPassword, "repeater password", "", true);
        } else {
          g_rptrLoginWait = millis();
        }
        memset(pw, 0, sizeof(pw));
      } else {
        rptrOpenSheet(1, om::ui::KeyboardField::RptrPassword, "repeater password", "", true);
      }
      break;
    case om::ui::RptrHit::Input:
      rptrOpenSheet(6, om::ui::KeyboardField::WifiPassword, "command", g_rptrCmd, false);
      break;
    case om::ui::RptrHit::Send:
      rptrSendConsole();
      break;
    case om::ui::RptrHit::RowBody:
    case om::ui::RptrHit::RowKey:
    case om::ui::RptrHit::RowMinus:
    case om::ui::RptrHit::RowPlus: {
      if (hit.row < 0 || hit.row >= g_rptrViewStore.screen.count) return true;
      const om::rptr::UiRow& row = g_rptrViewStore.screen.rows[hit.row];
      if (row.act == om::rptr::Act::Forget) {
        om::rptrForget(g_rptrNode);
        g_rptrRemembered = false;
        snprintf(g_rptrResult, sizeof(g_rptrResult), "Password forgotten");
        g_rptrResultOk = true;
        break;
      }
      int dir = 0;
      if (hit.kind == om::ui::RptrHit::RowMinus) dir = -1;
      if (hit.kind == om::ui::RptrHit::RowPlus) dir = 1;
      rptrPageRowTapped(row, hit.kind == om::ui::RptrHit::RowKey, dir);
      break;
    }
    default:
      return false;
  }
  drawRptrTab();
  return true;
}

}  // namespace

void begin(const Host& host) { s_host = host; }

void draw() { drawRptrTab(); }

bool tap(int x, int y) { return handleRptrTap(x, y); }

// EST-390: the same order as handleRptrTap (picker, confirmation, sheet, list, page), from the SAME
// buildRptrView() the tap path uses. The tab bar, the gear and the fault mark are the shell's and are
// handled before this call.
ui::PressTarget pressTarget(int x, int y) {
  const ui::Layout& L = ui::l2();
  ui::PressTarget out;
  buildRptrView(g_rptrViewStore);
  if (g_rptrMapPick) {
    out = ui::pressTargetForRptrMapPick(ui::rptrMapPickLayoutFor(L, rptrMapDirty()), x, y);
    return out;
  }
  if (g_rptrAskOpen) {
    const ui::RptrModalLayout m = ui::rptrModalLayoutFor(L, g_rptrAsk.verb);
    const bool confirm = ui::rptrModalHitAt(m, x, y) == ui::RptrModalHit::Confirm;
    out = ui::pressTargetForModalKey(confirm, m.cancel, m.confirm, x, y);
    return out;
  }
  if (g_rptrOsk) {
    const ui::RptrKeyHit key = ui::rptrEditorKeyAt(g_rptrViewStore, x, y);
    if (key.pressed) {
      const ui::KeyboardField field = ui::rptrEditorField(g_rptrViewStore);
      const ui::ChatLayout g = ui::chatLayoutFor(L, true, false, g_rptrNums, false, field);
      out = ui::pressTargetForKeypad(g, g_rptrNums, x, y, field, false);
    }
    return out;
  }
  if (g_rptrList) {
    out = ui::pressTargetForRptrList(ui::rptrListLayoutFor(L), g_rptrViewStore.count,
                                     g_rptrScroll, x, y);
    return out;
  }
  const ui::RptrPageLayout a = ui::rptrPageLayoutFor(
      L, g_rptrViewStore.screen.cellCount > 0, g_rptrViewStore.screen.banner[0] != 0,
      g_rptrViewStore.screen.hasApply, g_rptrViewStore.screen.bigButton,
      g_rptrViewStore.screen.hasConsole);
  out = ui::pressTargetForRptrPage(a, g_rptrViewStore.screen, g_rptrPageScroll, x, y);
  return out;
}

void pump() { rptrPump(); }

void beginDrag() {
  g_rptrScroll0 = g_rptrScroll;
  g_rptrPageScroll0 = g_rptrPageScroll;
}

void endPickerDrag() { g_rptrPickDragging = false; }

bool keyboardOpen() { return g_rptrOsk; }
bool modalOpen() { return g_rptrAskOpen; }
bool mapPickerOpen() { return g_rptrMapPick; }
bool mapPickerDragging() { return g_rptrPickDragging; }
bool listShowing() { return g_rptrList; }
const char* openNodeName() { return g_rptrNode; }

void open(const char* name) { rptrOpen(name); }
void clearSession() { rptrClearSession(); }
void coordText(char* out, size_t cap, int32_t e6) { rptrCoordText(out, cap, e6); }

// The release clamp for the list and the page rows (the map picker has no scroll of its own: its pan
// is applied live, so the release does nothing there).
void releaseSnap(bool dragging) {
  if (g_rptrOsk || g_rptrMapPick) return;
  bool changed = false;
  if (g_rptrList) {
    const ui::RptrListLayout rr = ui::rptrListLayoutFor(ui::l2());
    const int maxScroll = ui::rptrListMaxScroll(rr, g_rptrCount);
    const int clamped =
        g_rptrScroll < 0 ? 0 : (g_rptrScroll > maxScroll ? maxScroll : g_rptrScroll);
    changed = clamped != g_rptrScroll;
    g_rptrScroll = clamped;
  } else {
    const ui::RptrPageLayout aa = ui::rptrPageLayoutFor(
        ui::l2(), g_rptrViewStore.screen.cellCount > 0, g_rptrViewStore.screen.banner[0] != 0,
        g_rptrViewStore.screen.hasApply, g_rptrViewStore.screen.bigButton,
        g_rptrViewStore.screen.hasConsole);
    const int maxScroll = ui::rptrPageMaxScroll(aa, g_rptrViewStore.screen.count);
    const int clamped = g_rptrPageScroll < 0
                            ? 0
                            : (g_rptrPageScroll > maxScroll ? maxScroll : g_rptrPageScroll);
    changed = clamped != g_rptrPageScroll;
    g_rptrPageScroll = clamped;
  }
  if (dragging || changed) drawRptrTab();
}

// EST-366 (rule 6): dragging the map picker pans the offline map under the fixed centre cross. The
// first move of a gesture only records the base; later moves apply the delta, so the map never jumps
// by the distance from an earlier gesture. The list and the page rows follow the finger through the
// shared selDragScroll(). The 60 ms frame clock is stamped exactly where the loop stamped it.
void dragMove(int x, int y, int dy, bool& drag, int& lastX, int& lastY,
              uint32_t& lastRender) {
  if (g_rptrMapPick) {
    if (!g_rptrPickDragging) {
      g_rptrPickDragging = true;
      g_rptrPickDragX = x;
      g_rptrPickDragY = y;
    } else {
      om::mapPan(g_rptrPickDragX - x, g_rptrPickDragY - y);
      g_rptrPickDragX = x;
      g_rptrPickDragY = y;
      rptrMapPickCentreToDraft();
    }
    drag = true;
    if (millis() - lastRender > 60) {
      lastRender = millis();
      drawRptrTab();
    }
    if (dy > 12 || dy < -12) drag = true;
    lastX = x;
    lastY = y;
  } else if (g_rptrList) {
    const ui::RptrListLayout rr = ui::rptrListLayoutFor(ui::l2());
    const int next =
        ui::selDragScroll(g_rptrScroll0, dy, ui::rptrListMaxScroll(rr, g_rptrCount));
    if (next != g_rptrScroll) {
      g_rptrScroll = next;
      if (millis() - lastRender > 60) {
        lastRender = millis();
        drawRptrTab();
      }
    }
  } else {
    const ui::RptrPageLayout aa = ui::rptrPageLayoutFor(
        ui::l2(), g_rptrViewStore.screen.cellCount > 0, g_rptrViewStore.screen.banner[0] != 0,
        g_rptrViewStore.screen.hasApply, g_rptrViewStore.screen.bigButton,
        g_rptrViewStore.screen.hasConsole);
    const int next =
        ui::selDragScroll(g_rptrPageScroll0, dy, ui::rptrPageMaxScroll(aa, g_rptrViewStore.screen.count));
    if (next != g_rptrPageScroll) {
      g_rptrPageScroll = next;
      if (millis() - lastRender > 60) {
        lastRender = millis();
        drawRptrTab();
      }
    }
  }
}

}  // namespace rptr_tab
}  // namespace om
