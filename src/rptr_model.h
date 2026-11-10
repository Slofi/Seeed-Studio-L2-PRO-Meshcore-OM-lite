// SPDX-License-Identifier: MIT
// RPTR (EST-358, regions/clock EST-360, interaction rework EST-362): the parts of repeater
// discovery, login, status, remote admin and the read-backs that are plain arithmetic or plain text.
// No Arduino, no LovyanGFX, no MeshCore, so test/host proves them with the system g++: the discovery
// list, the login state machine and its timeouts, the password and region-name filters, the status
// reply decode, the neighbour and region reply parsers, the anonymous nearby-regions reply, the
// clock guard, the CommonCLI read-back parsers, and the EXACT CLI line every control sends.
//
// The shell (main.cpp) owns the taps and the words on the glass; rptr_link.cpp owns the radio. This
// file owns only what can be checked on a laptop.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace rptr {

// --------------------------------------------------------------------------------------------- limits
constexpr int kMaxFound = 16;        // discovery answers kept per scan
constexpr int kMaxNeighbours = 12;   // rows of a repeater's neighbour reply that fit a page
constexpr int kMaxRegions = 12;
constexpr int kPasswordMax = 15;     // MeshCore's sendLogin() truncates at 15 characters
constexpr int kNameMax = 31;         // NodePrefs::node_name[32], MeshCore's own size
constexpr int kOwnerMax = 31;
// RegionEntry::name[31], so 30 characters plus the terminator.
constexpr int kRegionNameMax = 30;
constexpr int kPubKeyBytes = 32;     // MeshCore's full public key

// Discovery keeps listening this long after the request goes out. The request is zero-hop, so every
// repeater in range answers once with a random delay; 20 s is several times the longest delay a
// crowded channel needs (MeshCore widens its retransmit delay x4 for exactly this).
constexpr uint32_t kScanWindowMs = 20000;
// The login deadline is upstream's own estimate (sendLogin() fills est_timeout) plus this margin,
// because the answer travels back over a flood and the repeater delays a server response.
constexpr uint32_t kLoginExtraMs = 2500;
// The anonymous regions query is one direct request/response; the repeater delays a server reply a
// little, so allow a few seconds on top of the airtime estimate.
constexpr uint32_t kRegionsExtraMs = 4000;
// EST-364: a status request's deadline never drops under this, whatever upstream's airtime estimate
// says. On Argus the estimate plus kLoginExtraMs came to 3.2 s and cut off an answer that was on its
// way; one automatic retry follows a silence, and only then does the card say "no answer yet".
constexpr uint32_t kStatusMinMs = 8000;
// The same floor for one CLI line (a read or a write): the repeater's own CLI answers took 3-4 s
// on the same run, so the shell's old fixed 3 s deadline threw away answers that had arrived.
constexpr uint32_t kCliMinMs = 8000;
// How long one request may wait: upstream's own est_timeout plus the caller's margin, never under
// the floor. The link (status) and the shell (CLI lines) both call this, so the host test proves the
// arithmetic the device runs.
constexpr uint32_t requestDeadlineMs(uint32_t estMs, uint32_t extraMs, uint32_t floorMs) {
  const uint32_t wanted = estMs + extraMs;
  return wanted > floorMs ? wanted : floorMs;
}

// ------------------------------------------------------------------------------------------- password
// The admin password is taken literally: printable ASCII only, and MeshCore compares it with strcmp.
// A character the sheet cannot type is refused here, so no key can put a byte on the air that the
// repeater would never match.
bool passwordChar(char c);
// Copy `src` into `out` through the filter, at most kPasswordMax characters. Returns the length.
int passwordFilter(const char* src, char* out, size_t cap);

// -------------------------------------------------------------------------------------------- regions
// MeshCore's own region-name rule (RegionMap::is_name_char): '-', '$', '#', digits, or any byte at
// or above 'A'. That is more permissive than it sounds: ASCII '[' .. '`', lowercase and '_' are all
// legal, while a space, '.', '/', ':' and '@' (all below 'A') are not. '*' is not a name; the
// wildcard pseudo-region already exists. The keyboard's RptrRegion field calls regionNameChar()
// directly, so a key that would be refused by the repeater is not even offered. constexpr: the rule
// is one line and every host test that links the keyboard can use it without another object file.
constexpr bool regionNameChar(char c) {
  const unsigned char u = (unsigned char)c;
  return u == '-' || u == '$' || u == '#' || (u >= '0' && u <= '9') || u >= 'A';
}
bool regionNameValid(const char* name);
// Copy through the filter, at most kRegionNameMax characters. Returns the length.
int regionNameFilter(const char* src, char* out, size_t cap);

// ----------------------------------------------------------------------------------------- suggestion
// Discovery: one answer from a repeater. MeshCore's control-data DISCOVER_RESP carries the tag, the
// node type, the SNR the repeater heard us at (x4) and its public key (6+32 bytes, or 6+8 for a
// prefix-only request). The SNR we heard it at comes from the receiving radio, not the payload.
// The anonymous regions query needs the FULL key (to derive the shared secret), so the row keeps it.
struct AnonRegions {
  uint32_t clock = 0;        // the repeater's clock from the same reply (4 B LE)
  bool clockValid = false;   // >= 2025-01-01, i.e. a real clock
  int count = 0;             // names kept
  bool valid = false;        // a parseable reply was decoded
  bool clean = true;         // every non-empty token was a legal region name
  char text[64] = "";        // "si, lj" / "*" / "" when the repeater has none
};

struct Found {
  uint8_t prefix[6] = {};     // the answer's public-key prefix, what the row is keyed on
  uint8_t key[kPubKeyBytes] = {};   // the full key when the answer carried it
  uint8_t keyLen = 0;         // 32 for a full-key answer, 6/8 for a prefix-only one
  char name[32] = "";         // the contact name, or "" when this repeater is not in the table
  bool inContacts = false;    // already in our MeshCore contact table
  bool hasSnrIn = false;      // the repeater told us how it heard us
  int8_t snrIn4 = 0;          // x4, MeshCore's unit
  bool hasSnrOut = false;     // we measured how we heard it
  int8_t snrOut4 = 0;
  uint8_t hops = 0;           // path length of the answer (0: it came straight to us)
  uint32_t heardMs = 0;       // millis() when it arrived
  uint32_t heardWall = 0;     // the system clock at that moment (0 = unknown)
  bool heardReal = false;     // the clock was a real source then
  // The anonymous "regions" read (EST-360). `regionsAsked` says a query went out; `anon.valid` says
  // a parseable answer came back; `anon.valid && anon.count == 0` is a real "none".
  bool regionsAsked = false;
  AnonRegions anon;
  // EST-365: the anonymous owner/name read. `nameAsked` stops a second request for the same scan;
  // the name lands in `name` when the answer comes, so the row stops saying "unnamed".
  bool nameAsked = false;
};

struct FoundList {
  Found rows[kMaxFound];
  int count = 0;
  bool listening = false;
  uint32_t startedMs = 0;
  uint32_t tag = 0;          // the random tag the request carried; answers must echo it
  uint32_t serial = 0;       // bumps on every change, so the shell knows to redraw

  // A new scan: forgets the previous rows, keeps listening. `tagSeed` gives the request's tag (the
  // link fills it from the RNG), so an old answer cannot land in a new list.
  void begin(uint32_t tagSeed, uint32_t nowMs);
  // One answer. `key` is the public key from the answer (32 bytes normally); only the first six are
  // used for identity, the whole key is kept for an anonymous request. Returns the row index (an
  // existing prefix updates in place: a repeater answers once, but a repeated scan or a duplicate
  // must not grow the list). -1 when the list is full.
  int addAnswer(const uint8_t* key, int keyLen, const char* name, bool inContacts, bool hasIn,
                int8_t in4, bool hasOut, int8_t out4, uint8_t hops, uint32_t nowMs);
  // The scan window closes: listening becomes false and one serial bump follows.
  void tick(uint32_t nowMs);
  int indexOf(const uint8_t* prefix6) const;
  // Store a regions answer on a row (bumps serial). False for an out-of-range row.
  bool setRegions(int index, const AnonRegions& a);
  // "N found" for the header ("listening, 2 found" while the window is open).
  void stateText(char* out, size_t cap) const;
};

// The anonymous REGIONS reply, as the listener hands it over: MyMesh strips the 4-byte tag, so the
// buffer starts at the repeater's clock. Real firmware sends clock(4 LE) + a comma-separated list
// with NO terminator inside the length (older notes say NUL-terminated; both are accepted). Tokens
// that are not a legal name and not "*" are dropped (`clean` goes false). Returns false only when
// there are fewer than the 4 clock bytes, i.e. when the reply cannot be this answer at all.
bool anonRegionsParse(const uint8_t* buf, int len, AnonRegions* out);

// The anonymous request types simple_repeater handles (examples/simple_repeater/MyMesh.cpp):
// 0x01 regions, 0x02 owner/name, 0x03 clock. Only a DIRECT request is answered.
constexpr uint8_t kAnonReqRegions = 0x01;
constexpr uint8_t kAnonReqOwner = 0x02;
constexpr uint8_t kAnonReqClock = 0x03;

// The ANON_REQ owner answer: [clock 4 LE]["<node_name>\n<owner_info>"] with no NUL inside `len`.
// MyMesh strips the 4-byte tag before the listener, so the buffer starts at the clock, exactly like
// the regions answer. `name` is the first line, `owner` the rest of that line (optional); both are
// trimmed and made from printable bytes. False when there is no first line, i.e. the reply cannot
// be an owner answer. A name longer than the receiver's buffer is truncated by the caller.
bool anonOwnerParse(const uint8_t* buf, int len, char* name, size_t nameCap, char* owner,
                    size_t ownerCap);

// --------------------------------------------------------------------------------------------- login
// MeshCore's login answer, decoded. The repeater's reply is
//   [0]=RESP_SERVER_LOGIN_OK(0), [1]=keep-alive/16 (0 since v7), [2]=is_admin, [3]=permissions,
//   [4..7]=random, [8]=FIRMWARE_VER_LEVEL   (simple_repeater/MyMesh.cpp handleLoginReq)
// and MyMesh::onContactResponse has ALREADY stripped the 4-byte timestamp before it reaches us
// (`onUnhandledResponse(contact, tag, &data[4], len - 4)`), so buf[0] is RESP_SERVER_LOGIN_OK and the
// repeater's clock is the separate `tag`. A legacy repeater answers the two bytes "OK" instead.
// A WRONG password gets NO answer at all (the repeater drops it), so "refused" can only be said when
// a reply arrives that is neither form.
struct LoginAnswer {
  bool ok = false;
  bool legacy = false;
  bool admin = false;
  uint8_t permissions = 0;
  uint8_t fwLevel = 0;
  uint32_t clock = 0;        // the repeater's own clock, from the answer's first four bytes
  bool clockValid = false;
};
bool loginParse(const uint8_t* buf, int len, LoginAnswer* out);

enum class LoginState : uint8_t { Idle = 0, Sending, Admin, Guest, Refused, NoAnswer };

const char* loginStateText(LoginState s);   // "not logged in", "sending", "admin", "guest", ...

// The state machine. `begin` is called the moment the request is handed to the radio; the deadline
// is upstream's estimate plus kLoginExtraMs; `tick` turns a silent radio into NoAnswer.
struct LoginMachine {
  LoginState state = LoginState::Idle;
  uint32_t sentMs = 0;
  uint32_t deadlineMs = 0;
  uint32_t rttMs = 0;
  bool haveRtt = false;
  LoginAnswer answer;

  void begin(uint32_t nowMs, uint32_t estTimeoutMs);
  void succeed(const LoginAnswer& a, uint32_t nowMs);
  void refuse();
  // Returns true when the state changed (a timeout), so the caller redraws once.
  bool tick(uint32_t nowMs);
  bool busy() const { return state == LoginState::Sending; }
  bool loggedIn() const { return state == LoginState::Admin || state == LoginState::Guest; }
  void reset();
};

// -------------------------------------------------------------------------------------------- status
// RepeaterStats, as simple_repeater sends it (MyMesh.h of that example, 56 bytes, naturally packed):
//   uint16 batt_mv, uint16 tx_queue, int16 noise_floor, int16 last_rssi,
//   uint32 recv, sent, air_time, up_time, sent_flood, sent_direct, recv_flood, recv_direct,
//   uint16 err_events, int16 last_snr (x4), uint16 direct_dups, uint16 flood_dups,
//   uint32 rx_air_time, uint32 recv_errors.
// The status reply is [timestamp 4][stats 56]; MyMesh::onContactResponse strips the timestamp before
// it calls the Listener, so statusParse() receives a buffer that STARTS AT batt_milli_volts. EST-360
// fixes the old double skip (it skipped 4 more, so battery read the noise floor: 0xFF88 = 65416
// "mV"). Longer replies from newer firmware are accepted; the fields we know keep their offsets.
struct Status {
  bool valid = false;
  uint32_t atMs = 0;
  uint16_t battMv = 0;
  uint16_t txQueue = 0;
  int16_t noiseFloor = 0;
  int16_t lastRssi = 0;
  uint32_t rxPackets = 0;
  uint32_t txPackets = 0;
  uint32_t airtimeSec = 0;
  uint32_t uptimeSec = 0;
  int16_t lastSnr4 = 0;
  uint16_t errEvents = 0;
  uint32_t rxAirtimeSec = 0;
  uint32_t rxCrcErrors = 0;
  uint8_t fwLevel = 0;       // from the login answer, not the status reply
  uint32_t clock = 0;        // the repeater's clock from the login answer
  bool clockValid = false;
  uint32_t rttMs = 0;
  bool haveRtt = false;
};
constexpr int kRepeaterStatsBytes = 56;
bool statusParse(const uint8_t* buf, int len, Status* out);

// ---------------------------------------------------------------------------------------- neighbours
// The repeater's `neighbors` reply is one line per neighbour: "hhhhhhhh:secs_ago:snr4" (4-byte hex
// public-key prefix, seconds since heard, SNR x4), separated by '\n'; "-none-" when there are none.
struct Neighbour {
  uint8_t prefix[4] = {};
  char name[32] = "";        // resolved from our contact table, else the hex prefix
  bool named = false;
  int32_t secsAgo = 0;
  int8_t snr4 = 0;
};
struct NeighbourList {
  Neighbour rows[kMaxNeighbours];
  int count = 0;
  bool valid = false;        // a reply was parsed (an empty list is a real answer)
};
int neighboursParse(const char* text, NeighbourList* out);

// ------------------------------------------------------------------------------------------- regions
// The repeater's `region` reply is RegionMap::exportTo(): one line per region, indented by depth,
//   "<name>[^][ F]"   name, "^" = the home region, " F" = flood allowed (no F = denied)
// and the first line is the wildcard region, which the mock shows as its own "*" option.
struct Region {
  char name[32] = "";
  bool allow = false;        // " F": flood allowed
  bool home = false;
  bool wildcard = false;     // the "*" line
};
struct RegionList {
  Region rows[kMaxRegions];
  int count = 0;
  bool valid = false;
};
int regionsParse(const char* text, RegionList* out);

// CommonCLI answers both reads, so the screen shows the repeater's own truth instead of remembering
// what this device last sent:
//   `region home`      -> " home is lj"              (or " home is *" for no home)
//   `region default`   -> " default scope is si" / " default scope is <null>"
// Both return false for an error line or anything else. `*` means the unscoped wildcard; the default
// parser returns "" for <null> (no default scope).
bool regionHomeReplyParse(const char* text, char* name, size_t cap);
bool regionDefaultReplyParse(const char* text, char* name, size_t cap);

// ---------------------------------------------------------------------------------------- clock sync
// The same 2025-01-01 cut-off as om::kMinSaneEpoch. Anything earlier is not a real clock: 0 is an
// unset RTC, and 2024 is the MeshCore restart clock (Argus read 1715779092, 2024-05-15).
constexpr uint32_t kClockRealSince = 1735689600u;
// A repeater's clock can only move FORWARD (`time <epoch>` and `clock sync` both refuse to go
// backwards), and only `clkreboot` can undo it. A jump of more than a day is therefore a one-way
// move: EST-362 allows it, but asks first with both dates (see clockGuard below).
constexpr uint32_t kTimeMaxAheadSecs = 86400;
// The repeater may be slightly ahead of us; a minute is not worth a refusal.
constexpr uint32_t kTimeMinBehindSecs = 60;

// "in sync", "3 s behind", "2m 05s ahead": the repeater's clock relative to ours (positive delta
// means the repeater is BEHIND ours).
int clockAgoText(char* out, size_t cap, int32_t deltaSecs);

// EST-362: the guard Filip wants on the page. A repeater's clock can only move FORWARD (`time` and
// `clock sync` both refuse to go backwards, and only `clkreboot` undoes a jump), so:
//   - our own clock must be real (otherwise nothing can be set),
//   - a repeater that is AHEAD of us is refused (our `time` would be a no-op or an error, and there
//     is nothing safe to send),
//   - a jump of MORE THAN A DAY needs a confirmation that shows both dates (it is one-way),
//   - anything else (up to a day) is applied directly.
enum class ClockVerdict : uint8_t {
  Ok = 0,
  NeedConfirm,      // a legal sync, but the jump is over a day: confirm with both dates
  OurClockNotSet,   // our clock is not real; nothing to send
  RepeaterAhead,    // the repeater is ahead of us; refuse
};
ClockVerdict clockGuard(uint32_t ourEpoch, bool ourReal, bool rptrKnown, uint32_t rptrEpoch);
const char* clockVerdictText(ClockVerdict v);
// True when the logged-in page must show the "Clock is wrong" line: no clock read yet, an unset
// repeater clock, or a difference of more than a minute either way.
bool clockWrong(bool rptrKnown, uint32_t rptrEpoch, uint32_t ourEpoch, bool ourReal);
// LOCAL TIME EVERYWHERE (EST-364, Filip: "UTC +1h!", the pop-up said 19:33 UTC at 20:33 local).
// The repeater keeps UTC on the air and in its own prefs; only what the glass shows is converted.
// Every helper below goes through om::localCivil() in timekeeper.cpp, i.e. the ONE Europe/Ljubljana
// rule (CET-1CEST,M3.5.0,M10.5.0/3) the status bar and ui/age.cpp already use. No line the RPTR
// section draws carries a "UTC" label any more.
//
// "2026-11-10 20:05" local, and "not set (2024-05-15)" for the MeshCore restart clock (below the
// kClockRealSince cut-off). This is the form the repeater page's banner shows.
int clockStampText(char* out, size_t cap, uint32_t epoch);
// The confirm's one sentence, both sides local and dotted like Filip writes them:
// "Set the repeater's clock from not set to 10.11.2026 20:33?"
int clockJumpText(char* out, size_t cap, uint32_t fromEpoch, uint32_t toEpoch);
// CommonCLI's `clock` reply: "14:02 - 10/11/2026 UTC" -> epoch seconds (UTC), false for anything
// else. The answer itself stays UTC; every displayed form of it goes through the helpers above.
bool clockReplyParse(const char* text, uint32_t* epoch);
// The answer to our own `time <epoch>`: "OK - clock set: 19:33 - 10/11/2026 UTC" (run-verified on
// Argus, EST-363) -> epoch seconds (UTC). This is the repeater's OWN confirmation, so the page takes
// the new clock from it instead of waiting for a second `clock` read that may time out (EST-364).
bool clockSetReplyParse(const char* text, uint32_t* epoch);
// One CLI answer for the console log, with a clock answer converted to local time and its "UTC"
// label dropped: "OK - clock set: 19:33 - 10/11/2026 UTC" -> "OK - clock set: 20:33 - 10.11.2026".
// Anything else is copied unchanged. Returns the length written.
int localClockAnswer(const char* text, char* out, size_t cap);

// ------------------------------------------------------------------------------------- admin values
// The radio draft: the mock's +/- steppers. Nothing is sent until Apply, exactly as the mock does
// ("set radio ... · not applied yet"), so no number goes on the air the finger did not confirm.
struct RadioValues {
  uint32_t freq_khz = 869525;   // 869.525 MHz
  uint32_t bw_hz = 62500;       // 62.5 kHz
  uint8_t sf = 8;
  uint8_t cr = 5;
  int8_t tx = 14;
};

enum class RadioKey : uint8_t { Freq = 0, Bw, Sf, Cr, Tx };
void radioStep(RadioValues* v, RadioKey key, int dir);   // clamps at MeshCore's own ranges
constexpr uint32_t kRadioFreqMin = 300000, kRadioFreqMax = 2500000;   // kHz, the mock's 300..2500 MHz
constexpr uint32_t kRadioFreqStep = 25;                                // 0.025 MHz
constexpr int kBandwidthCount = 10;
extern const uint32_t kBandwidthLadder[kBandwidthCount];   // 7.8 .. 500 kHz

// The mesh page's steppers, with the mock's ladders and clamps.
constexpr int kAdvertLocalCount = 8;
extern const uint8_t kAdvertLocalLadder[kAdvertLocalCount];   // 0, 60 .. 240 min
constexpr uint8_t kAdvertLocalDefault = 120;
constexpr uint8_t kAdvertFloodDefault = 13;                   // hours
constexpr uint8_t kFloodMaxDefault = 8;                       // hops
constexpr uint8_t kFloodMaxStep = 4;
enum class LoopDetect : uint8_t { Off = 0, Minimal, Moderate, Strict };

// One repeater's editable state. The shell owns the instance; reset() is what a fresh login to a
// different repeater starts from.
struct AdminValues {
  RadioValues radio;
  uint8_t advertLocal = kAdvertLocalDefault;
  uint8_t advertFlood = kAdvertFloodDefault;
  uint8_t floodMax = kFloodMaxDefault;
  LoopDetect loop = LoopDetect::Moderate;
  uint8_t pathHash = 1;        // 0 = 1B, 1 = 2B, 2 = 3B (the mock's 2B default)
  bool repeat = true;
  bool powerSave = false;
  bool sharePosition = true;   // "gps advert prefs"
  void reset();
};
void advertLocalStep(AdminValues* v, int dir);
void advertFloodStep(AdminValues* v, int dir);
void floodMaxStep(AdminValues* v, int dir);

const char* loopDetectText(LoopDetect v);           // "off" / "minimal" / ...
const char* pathHashText(uint8_t v);                // "1B" / "2B" / "3B"
const char* pathHashCli(uint8_t v);                 // "0" / "1" / "2"

// ------------------------------------------------------------------------------- the CLI line builders
// Every builder writes exactly the line the repeater's CommonCLI parses (checked against
// lib/meshcore/helpers/CommonCLI.cpp handleRegionCmd). Each returns the length written (0 when the
// buffer was too small or an argument was refused, with `out` left empty).
int cliRadio(char* out, size_t cap, const RadioValues& v);      // "set radio 869.525,62.5,8,5"
int cliTx(char* out, size_t cap, const RadioValues& v);         // "set tx 14"
int cliRadioApply(char* out, size_t cap, const RadioValues& v);// the two lines, " · " between them
int cliName(char* out, size_t cap, const char* name);           // "set name rptr"
int cliOwner(char* out, size_t cap, const char* owner);         // "set owner.info own"
// Position: the mock sends two commands. lat/lon are in micro-degrees (MeshCore's own int32 store),
// printed with five decimals so the repeater gets back the value it sent.
int cliLat(char* out, size_t cap, int32_t latE6);               // "set lat 46.25000"
int cliLon(char* out, size_t cap, int32_t lonE6);               // "set lon 14.40000"
int cliRegionFlood(char* out, size_t cap, const char* name, bool allow);   // "region allowf lj"
int cliRegionHome(char* out, size_t cap, const char* name);     // "region home lj"
int cliRegionDefault(char* out, size_t cap, const char* name);// "region default si"
// `region put <name>` with no parent goes under the wildcard; with a parent it is a child.
int cliRegionPut(char* out, size_t cap, const char* name, const char* parent);
int cliRegionRemove(char* out, size_t cap, const char* name);   // "region remove lj"
int cliRegionHomeNone(char* out, size_t cap);                    // "region home *"
int cliRegionDefaultNone(char* out, size_t cap);                 // "region default <null>"
int cliTime(char* out, size_t cap, uint32_t epoch);              // "time 1762790400"
constexpr const char* kCliRegionSave = "region save";
constexpr const char* kCliRegionList = "region";
constexpr const char* kCliRegionHomeRead = "region home";
constexpr const char* kCliRegionDefaultRead = "region default";
constexpr const char* kCliNeighboursDiscover = "discover.neighbors";
constexpr const char* kCliNeighboursRead = "neighbors";
constexpr const char* kCliAdvertLocal = "advert.zerohop";
constexpr const char* kCliAdvertFlood = "advert";
constexpr const char* kCliClockRead = "clock";
constexpr const char* kCliReboot = "reboot";
// Silent mode and Resume: four lines each, the mock's own list.
constexpr int kSilentCount = 4;
extern const char* const kSilent[kSilentCount];
extern const char* const kResume[kSilentCount];
constexpr uint8_t kSilentAdvertLocal = 0;
constexpr uint8_t kSilentAdvertFlood = 0;
constexpr uint8_t kResumeAdvertLocal = 120;
constexpr uint8_t kResumeAdvertFlood = 13;

int cliAdvertLocal(char* out, size_t cap, uint8_t minutes);     // "set advert.interval 120"
int cliAdvertFlood(char* out, size_t cap, uint8_t hours);       // "set flood.advert.interval 13"
int cliFloodMax(char* out, size_t cap, uint8_t hops);           // "set flood.max 8"
int cliPathHash(char* out, size_t cap, uint8_t mode);           // "set path.hash.mode 2"
int cliLoopDetect(char* out, size_t cap, LoopDetect v);         // "set loop.detect strict"
int cliRepeat(char* out, size_t cap, bool on);                  // "set repeat on"
int cliPowerSaving(char* out, size_t cap, bool on);             // "set powersaving on"
int cliGpsAdvert(char* out, size_t cap, bool share);            // "gps advert prefs"

// ------------------------------------------------------------------------------------- small formatting
// A frequency as the CLI wants it: integer kHz with three decimals ("869.525"), never a float's
// rounding surprise.
int freqText(char* out, size_t cap, uint32_t khz);
// A bandwidth the way the mock prints it: "62.5", "125", "7.8" (trailing zeros dropped).
int bwText(char* out, size_t cap, uint32_t hz);
// The SNR a row shows: x4 -> "-8.5 dB" / "6.2 dB".
int snrText(char* out, size_t cap, int8_t snr4);
// "SNR -8.5/6.2 dB" (both ways), "SNR 6.2 dB" (one way) or "" when neither is known.
int snrBothText(char* out, size_t cap, bool hasIn, int8_t in4, bool hasOut, int8_t out4);
// Uptime / airtime as the status card wants them: "3d 04:11", "1h 02m", "12m 30s".
int uptimeText(char* out, size_t cap, uint32_t seconds);
// Battery in volts with two decimals ("4.12 V"), and "n/a" outside 2.5..30 V or at 0. The raw mV is
// a detail-line string of its own (`batteryRawText`), never the primary value.
constexpr uint16_t kBatteryMinMv = 2500;
constexpr uint16_t kBatteryMaxMv = 30000;
bool batteryPlausible(uint16_t milliVolts);
int batteryVoltsText(char* out, size_t cap, uint16_t milliVolts);
int batteryRawText(char* out, size_t cap, uint16_t milliVolts);   // "4120 mV"
// A battery voltage as a percentage of a 3.0..4.2 V single cell, -1 when out of range.
int batteryPercent(uint16_t milliVolts);

// "20:33" local from the repeater's clock (its login answer), for the card's compact line.
int clockText(char* out, size_t cap, uint32_t epoch);

// ------------------------------------------------------------------------------------ read-backs
// EST-362: after Apply the page shows the value the repeater itself reports, never what we hoped it
// took. These parse CommonCLI's own replies (the `> value` prefix is CommonCLI's):
//   get name            -> "> rptr-1"           set name persists, so a read-back is the proof
//   get lat / get lon   -> "> 46.25000"         the store is float; five decimals is enough
//   get radio           -> "> 869.525,62.5,8,5" (CommonRadioPrefs, freq MHz, bw kHz)
//   get tx              -> "> 14"
//   get repeat          -> "> on" / "> off"
//   get advert.interval -> "> 120"              minutes
//   get flood.advert.interval -> "> 13"         hours
//   get flood.max       -> "> 8"
//   get loop.detect     -> "> moderate"
//   get path.hash.mode  -> "> 1"
// (CommonCLI.cpp handleGetCmd and CommonRadioPrefs.cpp; checked against lib/meshcore v1.18.0.)
bool valueReplyParse(const char* text, char* out, size_t cap);
bool intReplyParse(const char* text, int32_t* out);
bool radioReplyParse(const char* text, RadioValues* out);   // freq/bw/sf/cr, tx untouched
// Which field an answer is allowed to be: a latitude is within +-90 degrees, a longitude within
// +-180. `Any` is the old, looser check and is only for a caller that does not know the field.
enum class CoordAxis : uint8_t { Any = 0, Lat, Lon };
constexpr int32_t kCoordLatLimitE6 = 90000000;
constexpr int32_t kCoordLonLimitE6 = 180000000;
bool coordReplyParse(const char* text, int32_t* e6, CoordAxis axis = CoordAxis::Any);
bool switchReplyParse(const char* text, bool* on);          // "on"/"off"
bool loopReplyParse(const char* text, LoopDetect* out);

// --------------------------------------------------------------------------- the coordinate pair
// EST-364: Argus showed "14.501452, 0.00000" after login, i.e. its LONGITUDE in the latitude field.
// The cause is not an offset: a CLI answer carries no command name, so rptr_link matches answers to
// commands by the order it sent them, and one answer that never arrives shifts every later answer
// one command back. "get lon" then lands on "get lat" and its 14.501452 becomes the latitude.
// Two rules close it:
//   1. the shell tells the link when a line is abandoned (rptrForgetPending), so the ring stays
//      exact, and
//   2. the pair below: a position is shown ONLY when both answers arrived in one clean run. Any
//      abandoned line in between marks the run unclean, so a late answer can never be paired.
struct CoordRun {
  bool lat = false;
  bool lon = false;
  bool clean = true;
  int32_t latE6 = 0;
  int32_t lonE6 = 0;

  void reset();
  void takeLat(int32_t e6);
  void takeLon(int32_t e6);
  // A line timed out: whatever arrives next cannot be trusted as this pair (see above).
  void abandon();
  // Both halves arrived and no line was abandoned in between: the page may show them.
  bool complete() const { return lat && lon && clean; }
};
// Both fields zero means the repeater shares no position at all, which the page says in words
// ("not shared") instead of printing "0.000000, 0.000000".
bool positionNotShared(int32_t latE6, int32_t lonE6);

// Does `text` look like the answer to `cmd`? The shell's backstop for rule 1 above: a reply that
// cannot be that command's answer is never parsed as one (an "OK - clock set" line can never become
// a `get radio` value, and a switch answer can never become a coordinate).
bool replyFitsCmd(const char* cmd, const char* text);
// An answered `region save` (and every other write) comes back as "OK", "OK - ..." or an error.
bool answerOk(const char* text);
// "OK - reboot to apply" -> "Applied, restart the repeater to use it"; an error -> its own words
// without the "Error, " / "Err - " prefix. Never a raw CLI line on a setting page.
int answerPlainText(const char* text, char* out, size_t cap);

}  // namespace rptr
}  // namespace om
