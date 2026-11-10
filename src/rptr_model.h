// SPDX-License-Identifier: MIT
// RPTR (EST-358): the parts of repeater discovery, login, status and remote admin that are plain
// arithmetic or plain text. No Arduino, no LovyanGFX, no MeshCore, so test/host proves them with the
// system g++: the discovery list, the login state machine and its timeouts, the password filter, the
// status reply decode, the neighbour and region reply parsers, and the EXACT CLI line every admin
// control sends (the mock's strings in ui-mock/index.html / tests/test_ui_mock.py).
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

// Discovery keeps listening this long after the request goes out. The request is zero-hop, so every
// repeater in range answers once with a random delay; 20 s is several times the longest delay a
// crowded channel needs (MeshCore widens its retransmit delay x4 for exactly this).
constexpr uint32_t kScanWindowMs = 20000;
// The login deadline is upstream's own estimate (sendLogin() fills est_timeout) plus this margin,
// because the answer travels back over a flood and the repeater delays a server response.
constexpr uint32_t kLoginExtraMs = 2500;
// A destructive action needs a second tap inside this window (the mock's two-step confirm).
constexpr uint32_t kConfirmMs = 6000;

// ------------------------------------------------------------------------------------------- password
// The admin password is taken literally: printable ASCII only, and MeshCore compares it with strcmp.
// A character the sheet cannot type is refused here, so no key can put a byte on the air that the
// repeater would never match.
bool passwordChar(char c);
// Copy `src` into `out` through the filter, at most kPasswordMax characters. Returns the length.
int passwordFilter(const char* src, char* out, size_t cap);

// ----------------------------------------------------------------------------------------- suggestion
// Discovery: one answer from a repeater. MeshCore's control-data DISCOVER_RESP carries the tag, the
// node type, the SNR the repeater heard us at (x4) and its public key (6+32 bytes, or 6+8 for a
// prefix-only request). The SNR we heard it at comes from the receiving radio, not the payload.
struct Found {
  uint8_t prefix[6] = {};     // the answer's public-key prefix, what the row is keyed on
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
  // One answer. Returns the row index (an existing prefix updates in place: a repeater answers
  // once, but a repeated scan or a duplicate must not grow the list). -1 when the list is full.
  int addAnswer(const uint8_t* prefix6, const char* name, bool inContacts, bool hasIn, int8_t in4,
                bool hasOut, int8_t out4, uint8_t hops, uint32_t nowMs);
  // The scan window closes: listening becomes false and one serial bump follows.
  void tick(uint32_t nowMs);
  int indexOf(const uint8_t* prefix6) const;
  // "N found" for the header ("listening, 2 found" while the window is open).
  void stateText(char* out, size_t cap) const;
};

// --------------------------------------------------------------------------------------------- login
// MeshCore's login answer, decoded. The repeater's reply is
//   [0]=RESP_SERVER_LOGIN_OK(0), [1]=keep-alive/16 (0 since v7), [2]=is_admin, [3]=permissions,
//   [4..7]=random, [8]=FIRMWARE_VER_LEVEL   (simple_repeater/MyMesh.cpp handleLoginReq)
// A legacy repeater answers the two bytes "OK" instead. A WRONG password gets NO answer at all
// (the repeater drops it), so "refused" can only be said when a reply arrives that is neither form.
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
// RepeaterStats, as simple_repeater sends it (MyMesh.h of that example, 56 bytes, naturally packed).
// The status reply is [tag 4][stats 56]; rptr_link hands the listener's buffer here.
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
// and the first line is the wildcard region, which the mock shows as its own "legacy" option.
struct Region {
  char name[24] = "";
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
// The default scope is not in the export; the mock keeps it from the ◎ button, so the shell
// remembers the last `region default` it sent. That is honest: the repeater has no "get default".

// --------------------------------------------------------------------------------------- admin values
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
// Every builder writes exactly the line the mock's own test asserts (tests/test_ui_mock.py), because
// a control that says one thing and sends another is a lie the glass cannot show. Each returns the
// length written (0 when the buffer was too small, with `out` left empty).
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
constexpr const char* kCliRegionSave = "region save";
constexpr const char* kCliRegionList = "region";
constexpr const char* kCliNeighboursDiscover = "discover.neighbors";
constexpr const char* kCliNeighboursRead = "neighbors";
constexpr const char* kCliAdvertLocal = "advert.zerohop";
constexpr const char* kCliAdvertFlood = "advert";
constexpr const char* kCliClockSync = "clock sync";
constexpr const char* kCliReboot = "reboot";
// The SI scope chain the mock's Regions page sends (7 commands, then the legacy option untouched).
constexpr int kSiPresetCount = 7;
extern const char* const kSiPreset[kSiPresetCount];
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
// A battery voltage as a percentage of a 3.0..4.2 V single cell, -1 when out of range.
int batteryPercent(uint16_t milliVolts);

// ------------------------------------------------------------------------------------- the admin rows
// One table drives the drawing and the taps (the Settings screens' rule): the shell builds the open
// view's rows with adminRows(), draws them, and when a row is pressed it switches on the row's id.
// Keeping the table here is what lets test/host prove the label and the CLI line agree.
enum class RowId : uint8_t {
  None = 0,
  // Overview: facts from the status reply, no action.
  StatusRefresh,
  StatusUptime, StatusBattery, StatusAirtime, StatusPackets, StatusNoise, StatusSnr,
  StatusFirmware, StatusClock,
  // Radio.
  RadioFreq, RadioBw, RadioSf, RadioCr, RadioTx, RadioApply,
  // Regions.
  RegionRead, RegionRow, RegionSave, SiPreset,
  // Mesh.
  PathHash, LoopDetect, AdvertLocal, AdvertFlood, FloodMax,
  Repeat, PowerSave, SharePos,
  EditName, EditOwner, EditPos, UseGpsPos,
  // Neighbours.
  NeighDiscover, NeighRead, NeighRow,
  // Actions.
  AdvertLocalOnce, AdvertFloodOnce, ClockSync, Silent, Resume, Reboot,
};

enum class RowKind : uint8_t {
  Action = 0,   // tap the row: one or more CLI lines
  Stepper,      // two keys: - and +
  Toggle,       // tap the row: on/off
  Edit,         // tap the row: the keyboard
  Region,       // tap the row: allowf/denyf; keys H and D
  Fact,         // a value from the status reply, nothing to press
};

struct AdminRow {
  RowId id = RowId::None;
  char label[26] = "";
  char value[44] = "";
  RowKind kind = RowKind::Action;
  uint8_t keys = 0;     // 0, 1 or 2 small keys on the row's right
  uint8_t index = 0;    // the region / neighbour ordinal this row stands for
  bool on = false;      // toggle state, or a region's flood allowed
  bool danger = false;  // a red row (reboot)
};

constexpr int kAdminRowCap = 16;
// Fills `out` for one view (0 Overview, 1 Radio, 2 Regions, 3 Mesh, 4 Neigh., 5 Actions) and returns
// the row count. `status` and `regions`/`neighbours` may be empty; the rows then say so honestly
// instead of inventing numbers.
int adminRows(int view, const AdminValues& v, const Status& status, uint8_t fwLevel,
              const char* repeaterName, const RegionList& regions, const NeighbourList& neighbours,
              AdminRow* out, int cap);
// The views' names, in the mock's order.
const char* viewName(int view);

// "14:02 UTC" from the repeater's clock (its login answer), for the status card.
int clockText(char* out, size_t cap, uint32_t epoch);

// The two-step confirm (reboot, silent mode): armed for kConfirmMs, a second press inside the window
// fires. Host-tested so a stray tap can never reboot a repeater.
struct Confirm {
  const char* key = nullptr;    // what is armed, compared by pointer or text by the caller
  uint32_t armedMs = 0;
  bool armed = false;
  void arm(const char* what, uint32_t nowMs);
  bool take(const char* what, uint32_t nowMs);   // true = the second press, the action may run
  bool tick(uint32_t nowMs);                     // true when the window closed this pass
  void clear();
};

}  // namespace rptr
}  // namespace om
