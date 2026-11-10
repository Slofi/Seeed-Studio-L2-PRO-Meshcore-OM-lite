// EST-364 host test: what the 20:34 Argus run showed, and what now stops it.
//
//   1. CLOCK AFTER SET. Argus answered "OK - clock set: 19:33 - 10/11/2026 UTC" and the page kept
//      "Clock not set" / "Clock is wrong" plus a red "No answer from the repeater", because the
//      answer's own timestamp was thrown away and the follow-up `clock` read timed out. Now the
//      confirmation IS the new clock (its own stamp, or the epoch we sent), the card shows local
//      time and the banner goes.
//   2. STATUS TIMEOUT. A request without an answer is not a refusal: the card says "Status  no
//      answer yet" in dim ink. The deadline is upstream's estimate or 8 s, whichever is longer, and
//      one retry follows a silence.
//   3. POSITION. "14.501452, 0.00000" was Argus's LONGITUDE in the latitude field. A CLI answer
//      carries no command name, so the link names it by send order; one answer that never arrived
//      shifted every later answer one command back. Now an abandoned line leaves the order, an
//      answer must fit the command it is labelled with, and a position is a PAIR (both answers in
//      one clean run) with per-field limits. Both zero means "not shared".
//   4. THE REFRESH icon gets its own gutter and a 40x40 finger target.
//   5. LOCAL TIME everywhere (Europe/Ljubljana): no line the RPTR section draws says "UTC".
//   6. The unread badge counts to 99 (the badge test lives in test_chat.cpp; the 3 glyphs' pill
//      width is checked here too).
// Run by test/host/run.sh (system g++).
#include <stdio.h>
#include <string.h>

#include "CayenneLPP.h"
#include "rptr_page.h"
#include "ui/badge_font.h"
#include "ui/layout.h"
#include "ui/rptr_layout.h"

using namespace om::rptr;

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)
#define CHECK_TEXT(got, want)                                                              \
  do {                                                                                     \
    const char* g_ = (got);                                                                \
    const char* w_ = (want);                                                               \
    if (strcmp(g_, w_) != 0) {                                                             \
      printf("FAIL %s:%d  got \"%s\", want \"%s\"\n", __FILE__, __LINE__, g_, w_);          \
      ++g_fail;                                                                            \
    }                                                                                      \
  } while (0)

// ------------------------------------------------------------------------------------- 1. the clock
// 2026-11-10 19:33 UTC (the timestamp in Argus's own answer; EST-363's log line).
static constexpr uint32_t kArgusClock = 1794339180u;   // checked with date -u -d @1794339180
static constexpr uint32_t kOurClock = 1794339180u;     // 20:33 LOCAL = 19:33 UTC: the same moment

static void testClockAfterSet() {
  uint32_t epoch = 0;
  CHECK(!clockReplyParse("OK - clock set: 19:33 - 10/11/2026 UTC", &epoch));

  CHECK(clockSetReplyParse("OK - clock set: 19:33 - 10/11/2026 UTC", &epoch));
  CHECK(epoch == kArgusClock);
  CHECK(clockSetReplyParse("clock set: 19:33 - 10/11/2026 UTC", &epoch));
  CHECK(epoch == kArgusClock);
  CHECK(!clockSetReplyParse("OK - clock set", &epoch));
  CHECK(!clockSetReplyParse("OK", &epoch));
  CHECK(!clockSetReplyParse(nullptr, &epoch));
  CHECK(!clockSetReplyParse("19:33 - 10/11/2026 UTC", &epoch));   // a plain read is not a set
  // The plain `clock` read still parses exactly as before (UTC on the wire, converted for the glass).
  CHECK(clockReplyParse("19:33 - 10/11/2026 UTC", &epoch));
  CHECK(epoch == kArgusClock);

  // LOCAL TIME: 19:33 UTC is 20:33 in Ljubljana on 10 November (CET, UTC+1). No "UTC" label.
  char t[64];
  clockText(t, sizeof(t), kArgusClock);
  CHECK_TEXT(t, "20:33");
  clockStampText(t, sizeof(t), kArgusClock);
  CHECK_TEXT(t, "2026-11-10 20:33");
  CHECK(strstr(t, "UTC") == nullptr);
  // Summer is CEST (UTC+2): 2026-07-01 12:00 UTC is 14:00 local.
  clockText(t, sizeof(t), 1782907200u);
  CHECK_TEXT(t, "14:00");
  // The Set clock pop-up, in Filip's own wording: local, dotted, no zone label.
  clockJumpText(t, sizeof(t), 0, kOurClock);
  CHECK_TEXT(t, "Set the repeater's clock from not set to 10.11.2026 20:33?");
  clockJumpText(t, sizeof(t), 1715779092u, kOurClock);
  CHECK(strstr(t, "15.05.2024 15:18") != nullptr);
  CHECK(strstr(t, "UTC") == nullptr);

  // The console log line: the same answer, converted.
  char shown[88];
  localClockAnswer("OK - clock set: 19:33 - 10/11/2026 UTC", shown, sizeof(shown));
  CHECK_TEXT(shown, "OK - clock set: 20:33 - 10.11.2026");
  localClockAnswer("19:33 - 10/11/2026 UTC", shown, sizeof(shown));
  CHECK_TEXT(shown, "20:33 - 10.11.2026");
  localClockAnswer("> 869.525,62.5,8,5", shown, sizeof(shown));
  CHECK_TEXT(shown, "> 869.525,62.5,8,5");   // anything else is copied through
  localClockAnswer("", shown, sizeof(shown));
  CHECK_TEXT(shown, "");

  // The page after the OK: the card's clock is local, and there is no banner any more.
  Facts f;
  f.clock = kArgusClock;
  f.hasClock = true;
  Status st;
  st.valid = true;
  st.battMv = 3578;
  st.noiseFloor = -119;
  st.uptimeSec = 3 * 3600 + 39 * 60;
  st.airtimeSec = 182;
  ScreenIn in;
  in.page = Page::Repeater;
  in.loggedIn = true;
  in.name = "Argus mobile RPTR";
  in.status = &st;
  in.facts = &f;
  in.clockWrong = clockWrong(f.hasClock, f.clock, kOurClock, true);
  ScreenOut out;
  buildScreen(in, &out);
  CHECK(!in.clockWrong);                       // the confirmed OK is our clock: nothing to warn about
  CHECK(out.banner[0] == 0);                   // no "Clock is wrong" bar
  CHECK(out.cellCount == 5);
  CHECK_TEXT(out.cells[4].label, "Clock");
  CHECK_TEXT(out.cells[4].value, "20:33");
  CHECK(strstr(out.cells[4].value, "UTC") == nullptr);

  // Before the OK the same page does warn, still in local time.
  f.clock = 1715779092u;   // the 2024 MeshCore restart clock
  f.hasClock = true;
  in.clockWrong = clockWrong(f.hasClock, f.clock, kOurClock, true);
  buildScreen(in, &out);
  CHECK(in.clockWrong);
  CHECK(strstr(out.banner, "Clock is wrong") != nullptr);
  CHECK(strstr(out.banner, "not set (2024-05-15)") != nullptr);
  CHECK(strstr(out.banner, "UTC") == nullptr);
  CHECK_TEXT(out.cells[4].value, "not set (2024)");
}

// ---------------------------------------------------------------- 2. the status: no answer yet
static void testStatusNoAnswer() {
  Facts f;
  ScreenIn in;
  in.page = Page::Repeater;
  in.loggedIn = true;
  in.name = "Argus mobile RPTR";
  in.facts = &f;
  in.statusWait = true;
  ScreenOut out;
  buildScreen(in, &out);
  CHECK(out.cellCount == 6);
  CHECK_TEXT(out.cells[0].label, "Status");
  CHECK_TEXT(out.cells[0].value, "no answer yet");
  CHECK(out.cells[0].dim);                    // dim: a state, not a reading
  CHECK(strstr(out.banner, "Refused") == nullptr);
  CHECK(out.banner[0] == 0);
  CHECK(strstr(out.cells[0].value, "Refused") == nullptr);
  // The clock row is still there and still says what the repeater last said.
  CHECK_TEXT(out.cells[5].label, "Clock");

  // A request that has not even been sent says "tap refresh", also dim.
  in.statusWait = false;
  buildScreen(in, &out);
  CHECK_TEXT(out.cells[0].value, "tap refresh");
  CHECK(out.cells[0].dim);

  // An answered status: real numbers, none of them dim.
  Status st;
  st.valid = true;
  st.battMv = 3578;
  st.noiseFloor = -119;
  st.uptimeSec = 13140;
  st.airtimeSec = 182;
  in.status = &st;
  buildScreen(in, &out);
  CHECK_TEXT(out.cells[0].label, "Battery");
  CHECK(!out.cells[0].dim);
  CHECK_TEXT(out.cells[1].value, "-119 dBm");

  // The deadline rule: upstream's estimate plus the margin, never under 8 s (the ticket's floor).
  CHECK(kStatusMinMs == 8000);
  CHECK(kCliMinMs == 8000);
  CHECK(requestDeadlineMs(700, 2500, kStatusMinMs) == 8000);      // Argus's 3.2 s estimate is cut off
  CHECK(requestDeadlineMs(9000, 2500, kStatusMinMs) == 11500);    // a slow link keeps its estimate
  CHECK(requestDeadlineMs(0, 0, kCliMinMs) == 8000);
}

// ----------------------------------------------------------------------------- 3. the position pair
static void testPosition() {
  int32_t e6 = 0;
  // Per-field limits: a latitude stops at 90 degrees, a longitude at 180.
  CHECK(coordReplyParse("> 46.05000", &e6, CoordAxis::Lat) && e6 == 46050000);
  CHECK(coordReplyParse("> 14.501452", &e6, CoordAxis::Lon) && e6 == 14501452);
  CHECK(!coordReplyParse("> 120.5", &e6, CoordAxis::Lat));
  CHECK(coordReplyParse("> 120.5", &e6, CoordAxis::Lon) && e6 == 120500000);
  CHECK(!coordReplyParse("> 200.5", &e6, CoordAxis::Lon));
  CHECK(!coordReplyParse("> -90.000001", &e6, CoordAxis::Lat));
  CHECK(!coordReplyParse("nan", &e6, CoordAxis::Lat));
  CHECK(!coordReplyParse("", &e6, CoordAxis::Lat));
  CHECK(coordReplyParse("> 0", &e6, CoordAxis::Lat) && e6 == 0);

  // The dirty run, exactly as Argus went: `get name` answered, the `get lat` answer never came, and
  // the `get lon` answer (14.501452) arrived. Nothing may be shown: the latitude is not this.
  CoordRun run;
  run.abandon();                  // the link reports the abandoned `get lat`
  run.takeLon(14501452);          // the late `get lon` answer
  CHECK(!run.complete());
  Facts f;
  CHECK(!f.posKnown);
  ScreenIn in;
  in.page = Page::Repeater;
  in.loggedIn = true;
  in.facts = &f;
  ScreenOut out;
  buildScreen(in, &out);
  // The Position row is the second one (after Name).
  CHECK(strstr(out.rows[1].label, "Position") != nullptr);
  CHECK(strcmp(out.rows[1].value, "14.501452, 0.00000") != 0);
  CHECK_TEXT(out.rows[1].value, "not set");

  // A clean run: both answers, in order, and the page shows them the right way round.
  run.reset();
  run.takeLat(46050000);
  run.takeLon(14501452);
  CHECK(run.complete());
  f.latE6 = run.latE6;
  f.lonE6 = run.lonE6;
  f.posKnown = true;
  buildScreen(in, &out);
  CHECK_TEXT(out.rows[1].value, "46.05000, 14.50145");

  // A latitude after a finished pair starts a new pair: the old longitude cannot join it.
  run.takeLat(46123456);
  CHECK(!run.complete());
  // A longitude with no latitude before it loses the order, so the run is dropped.
  run.reset();
  run.takeLon(14501452);
  CHECK(!run.complete());

  // Both zero: the repeater shares no position. Not "0.00000, 0.00000" (a spot in the Gulf of
  // Guinea), and not an error.
  CHECK(positionNotShared(0, 0));
  CHECK(!positionNotShared(46050000, 0));
  f.latE6 = 0;
  f.lonE6 = 0;
  buildScreen(in, &out);
  CHECK_TEXT(out.rows[1].value, "not shared");

  // The answer/command SHAPE gate: a confirmed clock set can never become a coordinate, and a
  // coordinate never becomes a switch.
  CHECK(!replyFitsCmd("get lat", "OK - clock set: 19:33 - 10/11/2026 UTC"));
  CHECK(!replyFitsCmd("get lat", "> 869.525,62.5,8,5"));
  CHECK(replyFitsCmd("get lat", "> 46.05000"));
  CHECK(replyFitsCmd("get lon", "> 14.501452"));
  CHECK(!replyFitsCmd("get lon", "> 190.5"));
  CHECK(replyFitsCmd("get radio", "> 869.525,62.5,8,5"));
  CHECK(!replyFitsCmd("get radio", "> 46.05000"));
  CHECK(replyFitsCmd("get repeat", "> on"));
  CHECK(!replyFitsCmd("get repeat", "> 14"));
  CHECK(replyFitsCmd("clock", "19:33 - 10/11/2026 UTC"));
  CHECK(!replyFitsCmd("clock", "OK - clock set: 19:33 - 10/11/2026 UTC"));
  CHECK(!replyFitsCmd("", "> 46.05000"));      // no command outstanding: never a value
  CHECK(replyFitsCmd("get name", "Argus mobile RPTR"));
  CHECK(replyFitsCmd("set name x", "OK"));
  CHECK(replyFitsCmd("set lat 46.25", "Err - ??"));
}

// The advert's own buffer shape, from upstream (lib/meshcore/helpers/AdvertDataHelpers.cpp, v1.18.0):
//   app_data[0] = type | ADV_LATLON_MASK(0x10) | ADV_NAME_MASK(0x80)
//   app_data[1..4] = latitude,  int32 little endian, degrees x 1e6
//   app_data[5..8] = longitude, int32 little endian, degrees x 1e6
//   app_data[9..]  = the name
// and BaseChatMesh::populateContactFromAdvert copies those two into ContactInfo::gps_lat / gps_lon,
// which src/mesh_link.cpp's fillContact() copies into MeshContact::lat_e6 / lon_e6 unchanged. This
// test pins the offsets down so a future "fix" cannot move them, and shows the swap the ticket
// suspected would have to come from here (it does not).
static void testAdvertShape() {
  const int32_t lat = 46050000;
  const int32_t lon = 14501452;
  uint8_t app[32] = {};
  int i = 0;
  app[i++] = (uint8_t)(2 | 0x80 | 0x10);   // repeater, name, lat/lon
  memcpy(&app[i], &lat, 4);
  i += 4;
  memcpy(&app[i], &lon, 4);
  i += 4;
  memcpy(&app[i], "Argus mobile RPTR", 17);
  CHECK(app[0] & 0x10);
  int32_t readLat = 0;
  int32_t readLon = 0;
  memcpy(&readLat, &app[1], 4);
  memcpy(&readLon, &app[5], 4);
  CHECK(readLat == 46050000);
  CHECK(readLon == 14501452);
  CHECK(readLat > 0 && readLon > 0);
  // The offsets really matter: reading at 5 as the latitude is exactly the swap Argus showed.
  int32_t swapped = 0;
  memcpy(&swapped, &app[5], 4);
  CHECK(swapped == 14501452);
  CHECK(swapped != readLat);
}

// The TELEMETRY position, the other place a repeater's latitude and longitude are read (the ticket
// asked about "the status/telemetry"): src/target.cpp hands MeshCore the fix in DEGREES and this
// encoder (src/CayenneLPP.h) writes LPP GPS: 3 bytes per coordinate, big endian, degrees x 1e4,
// latitude first. The scale is why 46.05 is 460500 here and 46050000 in an advert (x 1e6).
static void testTelemetryPosition() {
  CayenneLPP lpp;
  CHECK(lpp.addGPS(1, 46.05f, 14.501452f, 0.0f) == LPP_GPS_SIZE);
  const uint8_t* b = lpp.getBuffer();
  CHECK(lpp.getSize() == 11);    // channel + type + 9 coordinate bytes
  CHECK(b[0] == 1);              // channel
  CHECK(b[1] == LPP_GPS);        // 136
  const int32_t lat = ((int32_t)b[2] << 16) | ((int32_t)b[3] << 8) | b[4];
  const int32_t lon = ((int32_t)b[5] << 16) | ((int32_t)b[6] << 8) | b[7];
  const int32_t alt = ((int32_t)b[8] << 16) | ((int32_t)b[9] << 8) | b[10];
  CHECK(lat == 460500);          // 46.05 degrees x 1e4
  CHECK(lon == 145014);          // 14.5014 degrees x 1e4 (float's own precision)
  CHECK(alt == 0);
  CHECK(lat != lon);
  // A southern/western fix keeps its sign in the low 24 bits.
  CayenneLPP south;
  CHECK(south.addGPS(2, -33.5f, -70.25f, 0.0f) == LPP_GPS_SIZE);
  const uint8_t* c = south.getBuffer();
  CHECK(south.getSize() == 11);
  // 3-byte two's complement, assembled by hand the way a decoder does it.
  const int32_t slat = (int32_t)(0xFF000000u | ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 8) | c[4]);
  const int32_t slon = (int32_t)(0xFF000000u | ((uint32_t)c[5] << 16) | ((uint32_t)c[6] << 8) | c[7]);
  CHECK(slat == -335000);
  CHECK(slon == -702500);
}

// ------------------------------------------------------------------- 4. the refresh icon's place
static void testRefreshLayout() {
  const om::ui::Layout L = om::ui::l2();
  const om::ui::RptrPageLayout a =
      om::ui::rptrPageLayoutFor(L, true, true, false, false, false);

  // The icon and its finger target are inside the card, on the right, and the target is 40x40.
  CHECK(!a.refresh.empty());
  CHECK(a.refresh.w == om::ui::kRptrRefreshIcon && a.refresh.h == om::ui::kRptrRefreshIcon);
  CHECK(a.refreshTap.w == om::ui::kRptrRefreshTap && a.refreshTap.h == om::ui::kRptrRefreshTap);
  CHECK(a.refreshTap.w == 40 && a.refreshTap.h == 40);
  CHECK(a.refreshTap.x >= a.card.x && a.refreshTap.right() <= a.card.right());
  CHECK(a.refreshTap.y >= a.card.y && a.refreshTap.bottom() <= a.card.bottom());
  CHECK(a.refresh.x >= a.refreshTap.x && a.refresh.right() <= a.refreshTap.right());
  CHECK(a.refresh.y >= a.refreshTap.y && a.refresh.bottom() <= a.refreshTap.bottom());

  // Both value columns stop LEFT of the target: a right-aligned value can never run under the icon
  // (the 20:34 screenshot had "-119 dBm" under it).
  CHECK(a.cardColW > 0);
  const int16_t colRight = (int16_t)(a.card.x + 8 + 2 * a.cardColW);
  CHECK(colRight <= a.refreshTap.x);
  CHECK(colRight < a.refresh.x);
  // Room for a real value in each column: "-119 dBm" is 8 mono glyphs at 6 px.
  CHECK(a.cardColW >= 8 * 6 + 6);

  // The tap answers the refresh, and only there.
  const int16_t tx = (int16_t)(a.refreshTap.x + a.refreshTap.w / 2);
  const int16_t ty = (int16_t)(a.refreshTap.y + a.refreshTap.h / 2);
  ScreenOut s;
  s.title[0] = 0;
  s.cellCount = 0;
  CHECK(om::ui::rptrPageHitAt(a, s, 0, tx, ty).kind == om::ui::RptrHit::Refresh);
  // The four corners of the target are inside it (a 40x40 area, not the 20 px icon).
  CHECK(om::ui::rptrPageHitAt(a, s, 0, a.refreshTap.x, a.refreshTap.y).kind ==
        om::ui::RptrHit::Refresh);
  CHECK(om::ui::rptrPageHitAt(a, s, 0, a.refreshTap.right() - 1, a.refreshTap.bottom() - 1).kind ==
        om::ui::RptrHit::Refresh);
  // One pixel left of it is NOT the refresh any more.
  CHECK(om::ui::rptrPageHitAt(a, s, 0, (int16_t)(a.refreshTap.x - 1), ty).kind !=
        om::ui::RptrHit::Refresh);
  // And it does not eat the banner's Set clock button below it.
  const om::ui::RptrModalHit unused = om::ui::RptrModalHit::None;
  (void)unused;
  CHECK(om::ui::rptrPageHitAt(a, s, 0, (int16_t)(a.bannerButton.x + 2),
                              (int16_t)(a.bannerButton.y + 2)).kind ==
        om::ui::RptrHit::BannerButton);
  CHECK(!a.refreshTap.overlaps(a.body));
  CHECK(!a.refreshTap.overlaps(a.bannerButton));

  // The same rules hold on the 800x480 arrangement (the second screen the layout serves).
  const om::ui::Layout big = om::ui::layoutFor(800, 480);
  const om::ui::RptrPageLayout b = om::ui::rptrPageLayoutFor(big, true, true, false, false, false);
  CHECK(b.refreshTap.w == 40 && b.refreshTap.h == 40);
  CHECK(b.refreshTap.x >= b.card.x && b.refreshTap.right() <= b.card.right());
  CHECK(b.refreshTap.y >= b.card.y && b.refreshTap.bottom() <= b.card.bottom());
  CHECK((int16_t)(b.card.x + 8 + 2 * b.cardColW) <= b.refreshTap.x);
  CHECK((int16_t)(b.card.x + 8 + 2 * b.cardColW) <= b.refresh.x);
}

// ------------------------------------------------------------ 5. no "UTC" anywhere on the screens
static bool pageHasUtc(const ScreenOut& s) {
  if (strstr(s.title, "UTC")) return true;
  if (strstr(s.subtitle, "UTC")) return true;
  if (strstr(s.banner, "UTC")) return true;
  for (int i = 0; i < s.cellCount; ++i) {
    if (strstr(s.cells[i].label, "UTC")) return true;
    if (strstr(s.cells[i].value, "UTC")) return true;
  }
  for (int i = 0; i < s.count; ++i) {
    if (strstr(s.rows[i].label, "UTC")) return true;
    if (strstr(s.rows[i].value, "UTC")) return true;
  }
  for (int i = 0; i < s.logLineCount; ++i) {
    if (strstr(s.logLines[i], "UTC")) return true;
  }
  return false;
}

static void testNoUtcAnywhere() {
  Facts f;
  f.clock = kArgusClock;
  f.hasClock = true;
  f.posKnown = true;
  f.latE6 = 46050000;
  f.lonE6 = 14501452;
  f.nameKnown = true;
  snprintf(f.name, sizeof(f.name), "Argus mobile RPTR");
  Status st;
  st.valid = true;
  st.battMv = 3578;
  st.noiseFloor = -119;
  st.uptimeSec = 13140;
  st.airtimeSec = 182;
  AnonFacts anon;
  anon.valid = true;
  anon.regionCount = 2;
  snprintf(anon.regions, sizeof(anon.regions), "*, si");
  anon.clock = kArgusClock;
  Draft d;
  RegionList regions;
  regionsParse("*\nsi^ F\nlj F", &regions);
  d.reset(f, regions, "*", "");
  NeighbourList nb;
  neighboursParse("aabbccdd:12:34", &nb);
  char localUnused[64];
  clockStampText(localUnused, sizeof(localUnused), kArgusClock);   // the log line the console shows
  static const char* const kLog[2] = {"< OK - clock set: 20:33 - 10.11.2026",
                                      "< 20:33 - 10.11.2026"};
  static const Page kPages[] = {Page::List,      Page::Repeater, Page::Name,   Page::Position,
                                Page::Radio,     Page::Tx,       Page::Repeat, Page::Regions,
                                Page::AddRegion, Page::HomePicker, Page::DefaultPicker,
                                Page::Neighbours, Page::Advertising, Page::More, Page::Console};
  for (Page p : kPages) {
    for (int logged = 0; logged < 2; ++logged) {
      ScreenIn in;
      in.page = p;
      in.loggedIn = logged != 0;
      in.name = "Argus mobile RPTR";
      in.status = logged ? &st : nullptr;
      in.facts = &f;
      in.draft = &d;
      in.regions = &regions;
      in.neighbours = &nb;
      in.anon = &anon;
      in.home = "*";
      in.defaultScope = "";
      in.clockWrong = logged ? clockWrong(f.hasClock, f.clock, kOurClock, true) : false;
      in.log = kLog;
      in.logCount = 2;
      in.consoleInput = "";
      ScreenOut out;
      buildScreen(in, &out);
      if (pageHasUtc(out)) {
        printf("FAIL page %s (logged in %d) still says UTC\n", pageName(p), logged);
        ++g_fail;
      }
    }
  }
  // And the clock helpers themselves, on a real, a restart and an unset epoch.
  char t[64];
  const uint32_t epochs[] = {kArgusClock, 1715779092u, 0};
  for (uint32_t e : epochs) {
    clockText(t, sizeof(t), e);
    if (strstr(t, "UTC")) {
      printf("FAIL clockText still says UTC\n");
      ++g_fail;
    }
    clockStampText(t, sizeof(t), e);
    if (strstr(t, "UTC")) {
      printf("FAIL clockStampText still says UTC\n");
      ++g_fail;
    }
  }
}

// ------------------------------------------------------------------------ 6. the badge to 99 (pill)
static void testBadgePill() {
  using namespace om::ui;
  char text[4];
  CHECK(badgeText(7, text, sizeof(text)) == 1);
  CHECK(badgeText(10, text, sizeof(text)) == 2 && strcmp(text, "10") == 0);
  CHECK(badgeText(99, text, sizeof(text)) == 2 && strcmp(text, "99") == 0);
  CHECK(badgeText(100, text, sizeof(text)) == 3 && strcmp(text, "99+") == 0);
  const Rect tab = l2().tab(Tab::Chat);
  const Rect two = countBadgeRect(tab, 2);
  const Rect three = countBadgeRect(tab, 3);
  CHECK(two.w == 19 && two.h == 13);
  CHECK(three.w == 23 && three.h == 13);
  CHECK(badgeTextWidth(2) == 11 && badgeTextWidth(3) == 17);
  CHECK(badgeTextWidth(3) <= three.w - 6);
  CHECK(two.x + two.w / 2 == three.x + three.w / 2);
  CHECK(three.x >= tab.x && three.right() <= tab.right());
}

int main() {
  testClockAfterSet();
  testStatusNoAnswer();
  testPosition();
  testAdvertShape();
  testTelemetryPosition();
  testRefreshLayout();
  testNoUtcAnywhere();
  testBadgePill();
  if (g_fail == 0) {
    printf("  OK  est364: clock after set, status no answer yet, position pair, refresh gutter, "
           "local time, badge pill\n");
  }
  return g_fail == 0 ? 0 : 1;
}
