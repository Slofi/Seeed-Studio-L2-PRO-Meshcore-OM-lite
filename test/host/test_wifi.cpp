// SPDX-License-Identifier: MIT
// EST-354: the Wi-Fi logic that needs no radio, with the system g++.
//
// It proves, over the same headers the firmware compiles:
//   * the saved-network list: five entries at most, one per SSID, adding an existing SSID replaces
//     its password in place (the count and the order do not move), forget compacts the list, and a
//     sixth distinct network is refused;
//   * the scan list: one row per SSID, the stronger sighting wins, strongest first;
//   * the asynchronous state machine: every transition the ticket names (off, scanning, connecting,
//     connected, failed), the two timeouts (with millis() wrap), and that a scan never turns into a
//     connection by itself;
//   * the OMCMD1 `wifi` command: status / scan / on / off are accepted, a missing or unknown
//     subcommand and extra arguments are refused, and no word can ask for a stored password;
//   * EST-355: the ORDER of a scan after the switch goes on, which the device got wrong (the first
//     scan failed while the driver's station was still coming up): the driver is not asked at once,
//     a refusal is retried (not turned into Failed), and only the last of three tries fails.
#include <stdio.h>
#include <string.h>

#include "ui/chat_layout.h"
#include "usb_cmd_logic.h"
#include "wifi_model.h"

using namespace om::wifi;

static int g_fail = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL %s\n", what);
    ++g_fail;
  }
}

// Null-safe reads: at() is allowed to answer nullptr, and the test must not dereference it when a
// check has just failed.
static const char* storedSsid(const SavedStore& s, int index) {
  const SavedNetwork* n = s.at(index);
  return n != nullptr ? n->ssid : "";
}

static const char* storedPass(const SavedStore& s, int index) {
  const SavedNetwork* n = s.at(index);
  return n != nullptr ? n->password : "";
}

static void testSavedStore() {
  SavedStore s;
  check(s.count() == 0, "a fresh store is empty");
  check(s.find("home") < 0, "nothing is saved yet");

  check(s.add("home", "one") == 0, "first network");
  check(s.add("work", "two") == 1, "second network");
  check(s.add("cafe", "three") == 2, "third network");
  check(s.count() == 3, "three saved");

  // Replacing an existing SSID: same slot, new password, count unchanged.
  check(s.add("work", "TWO") == 1, "replacing work stays at index 1");
  check(s.count() == 3, "replacement does not grow the list");
  check(strcmp(storedPass(s, 1), "TWO") == 0, "the password was replaced");
  check(strcmp(storedSsid(s, 0), "home") == 0 && strcmp(storedSsid(s, 2), "cafe") == 0,
        "replacement does not reorder");

  check(s.add("garden", "four") == 3, "fourth network");
  check(s.add("school", "five") == 4, "fifth network");
  check(s.full(), "five is the limit");
  check(s.add("sixth", "six") == -1, "a sixth distinct network is refused");
  check(s.count() == kMaxSaved, "the refused add did not change the list");
  check(s.add("home", "again") == 0, "a replacement still works when full");
  check(strcmp(storedPass(s, 0), "again") == 0, "the full-list replacement was written");

  // Forget by name and by index, compacting without gaps.
  check(s.forget("garden"), "forget a middle network");
  check(s.count() == 4, "forget removed one");
  check(s.find("garden") < 0, "the forgotten network is gone");
  check(strcmp(storedSsid(s, 3), "school") == 0, "the list closed the gap");
  check(!s.forget("garden"), "forgetting it twice is a no-op");
  check(s.forgetAt(0), "forget the first network");
  check(strcmp(storedSsid(s, 0), "work") == 0, "the head moved up");
  check(!s.forgetAt(9), "an out-of-range index is refused");

  // Bad input never becomes a saved network.
  check(s.add("", "x") == -1, "an empty SSID is refused");
  check(s.add(nullptr, "x") == -1, "a null SSID is refused");
  char longSsid[kSsidBytes + 2];
  memset(longSsid, 'a', sizeof(longSsid));
  longSsid[sizeof(longSsid) - 1] = 0;
  check(s.add(longSsid, "x") == -1, "an over-long SSID is refused");
  char longPass[kPassBytes + 2];
  memset(longPass, 'p', sizeof(longPass));
  longPass[sizeof(longPass) - 1] = 0;
  check(s.add("new", longPass) == -1, "an over-long password is refused");
  char maxPass[kPassBytes + 1];
  memset(maxPass, 'p', kPassBytes);
  maxPass[kPassBytes] = 0;
  const int maxAt = s.add("max", maxPass);
  check(maxAt >= 0, "a 63-byte password is accepted");
  check(strlen(storedPass(s, maxAt)) == kPassBytes, "the password is stored whole");

  s.clear();
  check(s.count() == 0, "clear empties the store");
  check(s.at(0) == nullptr, "no entry after clear");
}

static void testScanList() {
  ScanList l;
  check(l.count() == 0, "a fresh scan list is empty");

  l.addOrMerge("home", -70, true);
  l.addOrMerge("work", -40, true);
  l.addOrMerge("cafe", -55, false);
  check(l.count() == 3, "three visible networks");
  // A second sighting of home, stronger: one row, the stronger number.
  l.addOrMerge("home", -50, true);
  check(l.count() == 3, "the same SSID does not add a row");
  int home = -1;
  for (int i = 0; i < l.count(); ++i) {
    if (strcmp(l.at(i)->ssid, "home") == 0) home = i;
  }
  check(home >= 0 && l.at(home)->rssi == -50, "the stronger sighting wins");
  l.addOrMerge("home", -80, true);
  home = -1;
  for (int i = 0; i < l.count(); ++i) {
    if (strcmp(l.at(i)->ssid, "home") == 0) home = i;
  }
  check(home >= 0 && l.at(home)->rssi == -50, "a weaker sighting does not lower it");
  check(l.addOrMerge("", -30, false) == -1, "a hidden network with no SSID is skipped");

  l.sortByStrength();
  check(strcmp(l.at(0)->ssid, "work") == 0, "strongest first");
  check(strcmp(l.at(1)->ssid, "home") == 0, "then the second strongest");
  check(strcmp(l.at(2)->ssid, "cafe") == 0, "then the weakest");
  check(l.at(2)->secured == false && l.at(1)->secured == true, "the lock flag is kept");
  for (int i = 1; i < l.count(); ++i) check(l.at(i - 1)->rssi >= l.at(i)->rssi, "sorted descending");

  l.clear();
  check(l.count() == 0, "clear empties the scan list");
  for (int i = 0; i < kMaxScan + 4; ++i) {
    char ssid[16];
    snprintf(ssid, sizeof(ssid), "net%02d", i);
    l.addOrMerge(ssid, -20 - i, true);
  }
  check(l.count() == kMaxScan, "the scan list is bounded");
}

// EST-355: the ORDER of a scan after the switch goes on. The device showed the first scan failing
// with "scan failed" and the second finding 12 networks, so the request must wait for the driver and a
// refusal must be retried instead of turned into Failed.
static void testScanGate() {
  ScanGate g;
  g.reset();
  check(!g.pending(), "no scan is pending at boot");
  check(!g.due(0), "nothing is due without a request");

  // The switch goes on at t = 1000 and the user taps Scan on the SAME pass: the driver's station is
  // still coming up, so the driver is not asked yet.
  g.driverUp(1000);
  g.request(1000);
  check(g.pending(), "the request is remembered");
  check(!g.due(1000), "the driver is not asked at once after the switch");
  check(!g.due(1000 + kScanReadyMs - 1), "not one ms before it is ready");
  check(g.due(1000 + kScanReadyMs), "asked once the station had kScanReadyMs");

  // The first try is refused (what the device saw). It retries, it does not fail.
  check(g.retry(1250), "a refusal is retried");
  check(g.pending(), "still pending after a refusal");
  check(!g.due(1250), "not again in the same pass");
  check(!g.due(1250 + kScanRetryMs - 1), "the retry waits kScanRetryMs");
  check(g.due(1250 + kScanRetryMs), "the retry is due after kScanRetryMs");

  // The second try is accepted: the request is satisfied and the machine keeps polling the driver.
  g.started();
  check(!g.pending(), "an accepted start satisfies the request");
  check(!g.due(9999), "and nothing is due afterwards");

  // A scan asked for long after the switch goes straight to the driver: no wait.
  g.request(60000);
  check(g.due(60000), "a scan asked for later starts at once");

  // Every try refused: the attempts run out and only then does the caller say Failed.
  ScanGate h;
  h.reset();
  h.driverUp(0);
  h.request(0);
  check(h.due(kScanReadyMs), "first attempt due");
  check(h.retry(kScanReadyMs), "attempt 1 refused, retried");
  check(h.due(kScanReadyMs + kScanRetryMs), "second attempt due");
  check(h.retry(kScanReadyMs + kScanRetryMs), "attempt 2 refused, retried");
  check(h.due(kScanReadyMs + 2 * kScanRetryMs), "third attempt due");
  check(!h.retry(kScanReadyMs + 2 * kScanRetryMs), "attempt 3 refused: no more tries");
  check(!h.pending(), "and the request is dropped so the page can say Failed");

  // The switch going off forgets the request; a refused start is never an error on its own.
  ScanGate k;
  k.reset();
  k.driverUp(10);
  k.request(10);
  k.driverDown();
  check(!k.pending(), "the switch going off drops a pending request");
  check(!k.due(10000), "and nothing is due without a driver");

  // millis() wrap: the wait is a subtraction, so it survives the rollover.
  ScanGate w;
  w.reset();
  const uint32_t beforeWrap = 0xFFFFFF00u;
  w.driverUp(beforeWrap);
  w.request(beforeWrap);
  check(!w.due(beforeWrap), "not ready across the wrap either");
  check(w.due(beforeWrap + kScanReadyMs), "ready after the wrap (unsigned subtraction)");
}

static void testMachineTransitions() {
  Machine m;
  check(m.status().phase == Phase::Off, "boots off");
  check(strcmp(phaseToken(Phase::Off), "off") == 0, "off token");

  m.enable(1000);
  check(m.status().phase == Phase::Idle, "enable -> idle");
  check(m.on() && !m.connected(), "on but not connected");

  m.beginScan(2000);
  check(m.status().phase == Phase::Scanning, "scan -> scanning");
  check(m.status().found == 0, "a scan starts with no count");
  m.scanDone(false, 7, 4000);
  check(m.status().phase == Phase::Idle, "scan done while idle -> idle");
  check(m.status().found == 7, "the scan count is recorded");
  check(m.status().ssid[0] == 0, "an idle scan invents no SSID");

  m.connectStarted("home", 5000);
  check(m.status().phase == Phase::Connecting, "connect -> connecting");
  check(strcmp(m.status().ssid, "home") == 0, "the target SSID is shown while connecting");
  check(m.status().ip[0] == 0, "no IP while connecting");
  m.connected("192.168.1.42", -48, 6000);
  check(m.status().phase == Phase::Connected, "connected");
  check(strcmp(m.status().ip, "192.168.1.42") == 0, "the IP is recorded");
  check(m.status().rssi == -48, "the RSSI is recorded");
  check(m.connected(), "connected() agrees");

  // A scan while connected is allowed; when the station is still associated it returns to
  // Connected with its details intact.
  m.beginScan(7000);
  check(m.status().phase == Phase::Scanning, "a scan from connected shows scanning");
  check(strcmp(m.status().ssid, "home") == 0, "the SSID is kept during the scan");
  m.scanDone(true, 9, 9000);
  check(m.status().phase == Phase::Connected, "still associated -> connected again");
  check(strcmp(m.status().ip, "192.168.1.42") == 0, "the IP survived the scan");

  // A scan that drops the link must not claim connected, and must not keep a stale address.
  m.beginScan(10000);
  m.scanDone(false, 2, 12000);
  check(m.status().phase == Phase::Idle, "a scan that dropped the link -> idle");
  check(m.status().ip[0] == 0 && m.status().ssid[0] == 0, "the stale connection facts are dropped");

  // Failure carries the reason.
  m.connectStarted("work", 13000);
  m.failed("wrong password", 14000);
  check(m.status().phase == Phase::Failed, "fail -> failed");
  check(strcmp(m.status().reason, "wrong password") == 0, "the failure reason is kept");

  // Disable from any state returns to Off and drops every fact.
  m.enable(15000);
  m.beginScan(16000);
  m.disable();
  check(m.status().phase == Phase::Off, "disable -> off");
  check(m.status().ssid[0] == 0 && m.status().ip[0] == 0 && m.status().rssi == kNoRssi,
        "off carries no connection facts");
  check(m.tick(200000) == Tick::None, "ticking while off does nothing");

  // Connect failure and its timeout.
  m.enable(200000);
  m.connectStarted("home", 201000);
  check(m.tick(201000 + kConnectTimeoutMs - 1) == Tick::None, "not yet timed out");
  check(m.status().phase == Phase::Connecting, "still connecting before the timeout");
  check(m.tick(201000 + kConnectTimeoutMs) == Tick::ConnectTimeout, "connect timeout fires");
  check(m.status().phase == Phase::Failed, "connect timeout -> failed");
  check(strcmp(m.status().reason, "no answer") == 0, "the timeout has a reason");

  // Scan timeout, and millis() wrapping at the same time.
  Machine w;
  w.enable(0xFFFFFF00u);
  w.beginScan(0xFFFFFF00u);
  check(w.tick(0xFFFFFF00u + kScanTimeoutMs - 1) == Tick::None, "scan not timed out yet (wrap)");
  check(w.tick(0xFFFFFF00u + kScanTimeoutMs) == Tick::ScanTimeout, "scan timeout fires (wrap)");
  check(w.status().phase == Phase::Failed, "scan timeout -> failed");
  check(strcmp(w.status().reason, "scan timed out") == 0, "the scan timeout has a reason");

  // The state line is plain, with the reason on a failure and no stale SSID in it.
  char line[64];
  Status st;
  stateLine(st, line, sizeof(line));
  check(strcmp(line, "Off") == 0, "state line off");
  st.phase = Phase::Idle;
  stateLine(st, line, sizeof(line));
  check(strcmp(line, "On, not connected") == 0, "state line idle");
  st.phase = Phase::Scanning;
  stateLine(st, line, sizeof(line));
  check(strcmp(line, "Scanning") == 0, "state line scanning");
  st.phase = Phase::Connecting;
  stateLine(st, line, sizeof(line));
  check(strcmp(line, "Connecting") == 0, "state line connecting");
  st.phase = Phase::Connected;
  stateLine(st, line, sizeof(line));
  check(strcmp(line, "Connected") == 0, "state line connected");
  st.phase = Phase::Failed;
  snprintf(st.reason, sizeof(st.reason), "wrong password");
  stateLine(st, line, sizeof(line));
  check(strcmp(line, "Failed: wrong password") == 0, "state line failed with reason");
  check(strstr(line, "password") != nullptr, "the reason is shown");
}

// The parser is compiled here too so the USB surface is proved next to the logic it drives: the
// three read commands and the two switches, and nothing that could name a secret.
static om::usb::Kind parse(char* line, const char** sub, const char** error) {
  *sub = nullptr;
  *error = nullptr;
  const om::usb::Command c = om::usb::parseCommand(line);
  *sub = c.target;
  *error = c.error;
  return c.kind;
}

static void testWifiCommand() {
  char a[] = "wifi status";
  const char* sub = nullptr;
  const char* err = nullptr;
  check(parse(a, &sub, &err) == om::usb::Kind::Wifi, "wifi status is accepted");
  check(err == nullptr && sub != nullptr && strcmp(sub, "status") == 0, "wifi status word");

  char b[] = "wifi scan";
  check(parse(b, &sub, &err) == om::usb::Kind::Wifi && err == nullptr && strcmp(sub, "scan") == 0,
        "wifi scan is accepted");

  char c[] = "wifi on";
  check(parse(c, &sub, &err) == om::usb::Kind::Wifi && err == nullptr && strcmp(sub, "on") == 0,
        "wifi on is accepted");
  char d[] = "wifi off";
  check(parse(d, &sub, &err) == om::usb::Kind::Wifi && err == nullptr && strcmp(sub, "off") == 0,
        "wifi off is accepted");

  char e[] = "wifi";
  const om::usb::Command eCmd = om::usb::parseCommand(e);
  check(eCmd.kind == om::usb::Kind::Wifi && eCmd.error != nullptr, "wifi alone is refused");
  char f[] = "wifi password";
  const om::usb::Command fCmd = om::usb::parseCommand(f);
  check(fCmd.error != nullptr, "there is no command that reads a password");
  char g[] = "wifi on extra";
  const om::usb::Command gCmd = om::usb::parseCommand(g);
  check(gCmd.error != nullptr, "extra arguments are refused");
  char h[] = "wifi status extra";
  const om::usb::Command hCmd = om::usb::parseCommand(h);
  check(hCmd.error != nullptr, "status takes no arguments");
  char i[] = "wifi connect";
  const om::usb::Command iCmd = om::usb::parseCommand(i);
  check(iCmd.error != nullptr, "connect is not a USB command (the password stays on the device)");
}

// EST-354: the password sheet is the shared keyboard with its own field rules: printable ASCII,
// 63 bytes, no accented long press, no emoji page, and a show/hide key in the emoji key's place.
static bool rowHasKind(bool nums, int row, om::ui::KeyboardField field, om::ui::KeyKind kind) {
  om::ui::KeyDef defs[10];
  const int n = om::ui::keyRow(nums, row, defs, field);
  for (int i = 0; i < n; ++i) {
    if (defs[i].kind == kind) return true;
  }
  return false;
}

static void testPasswordKeyboard() {
  using namespace om::ui;
  const KeyboardField f = KeyboardField::WifiPassword;
  check(keyboardMaxBytes(f) == kKeyboardWifiPassMaxBytes, "the password field caps at 63 bytes");
  check(keyboardAllowsChar(f, ' ') && keyboardAllowsChar(f, '!') && keyboardAllowsChar(f, '~'),
        "printable ASCII is accepted, space included");
  check(!keyboardAllowsChar(f, '\n') && !keyboardAllowsChar(f, '\t'),
        "control characters are not typeable");
  check(!keyboardAccents(f), "no accented long press on a password");
  check(!keyboardEmoji(f), "no emoji page on a password");
  check(!keyboardAcceptsBytes(f, 62, 2), "63 is the byte limit");
  check(keyboardAcceptsBytes(f, 61, 2), "two more bytes fit at 61");

  KeyDef defs[10];
  const int bottom = keyRow(false, 3, defs, f);
  check(bottom == 6, "letters bottom row: 123, show, comma, space, dot, Send");
  check(defs[0].kind == KeyKind::Mode && defs[1].kind == KeyKind::Reveal,
        "the show key sits in the emoji slot");
  check(!rowHasKind(false, 3, f, KeyKind::Emoji), "the emoji key is gone on a password");
  const int numsBottom = keyRow(true, 3, defs, f);
  check(numsBottom == 6 && rowHasKind(true, 3, f, KeyKind::Reveal),
        "the 123 page keeps the show key");
  check(!rowHasKind(true, 3, f, KeyKind::Emoji), "and still no emoji key");
  for (int nums = 0; nums < 2; ++nums) {
    for (int r = 0; r < 4; ++r) {
      const int n = keyRow(nums != 0, r, defs, f);
      check(n > 0 && n <= 10, "a password key row has at most ten keys");
    }
  }

  // The key rectangles answer the reveal key on both pages, from the same layout that draws it.
  const Layout L = om::ui::l2();
  for (int nums = 0; nums < 2; ++nums) {
    const ChatLayout g = chatLayoutFor(L, true, false, nums != 0, false, f);
    bool found = false;
    for (int r = 0; r < 4 && !found; ++r) {
      const int n = keyRow(nums != 0, r, defs, f);
      for (int c = 0; c < n; ++c) {
        const Rect& key = g.keys[r][c];
        const KeyHit hit = keyAt(g, nums != 0, key.x + key.w / 2, key.y + key.h / 2, f);
        if (defs[c].kind == KeyKind::Reveal) {
          check(hit.row == r && hit.col == c, "the reveal key is tappable in its own rectangle");
          found = true;
          break;
        }
      }
    }
    check(found, "every page has a reveal key");
  }
}

// EST-385: the two NTP verbs are exactly `ntp now` and `ntp status`; nothing else is accepted, so no
// word can name a server, a network or an address.
static void testNtpCommand() {
  char a[] = "ntp now";
  const char* sub = nullptr;
  const char* err = nullptr;
  check(parse(a, &sub, &err) == om::usb::Kind::Ntp, "ntp now is accepted");
  check(err == nullptr && sub != nullptr && strcmp(sub, "now") == 0, "ntp now word");
  char b[] = "ntp status";
  check(parse(b, &sub, &err) == om::usb::Kind::Ntp && err == nullptr && strcmp(sub, "status") == 0,
        "ntp status is accepted");

  char c[] = "ntp";
  check(om::usb::parseCommand(c).error != nullptr, "ntp alone is refused");
  char d[] = "ntp server";
  check(om::usb::parseCommand(d).error != nullptr, "there is no word that sets a server");
  char e[] = "ntp now extra";
  check(om::usb::parseCommand(e).error != nullptr, "ntp now takes no arguments");
  char f[] = "ntp pool.ntp.org";
  check(om::usb::parseCommand(f).error != nullptr, "ntp cannot name a host");
}

int main() {
  testSavedStore();
  testScanList();
  testMachineTransitions();
  testScanGate();
  testPasswordKeyboard();
  testWifiCommand();
  testNtpCommand();
  if (g_fail) {
    printf("est354/355: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("est354/355: saved networks, scan list, Wi-Fi state machine, the scan's order and the "
         "wifi command ok\n");
  return 0;
}
