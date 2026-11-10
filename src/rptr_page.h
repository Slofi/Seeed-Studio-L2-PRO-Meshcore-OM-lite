// SPDX-License-Identifier: MIT
// EST-362: the RPTR section's interaction model. One plain list to choose a repeater, one repeater
// page, and one page per setting with a single Apply. Everything a tap can do, every exact CLI line
// Apply sends, which actions need a confirmation and what that confirmation says, and the read-back
// the page shows afterwards live here, with no Arduino and no LovyanGFX, so test/host can prove them.
//
// The rule this file exists to keep (Filip, 2026-11-10): there is no "Send now" and no "Save to
// repeater". Apply is the only verb, it sends what is needed and makes it permanent in one step, and
// a confirmation is a pop-up over the page, never a page of its own.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "rptr_model.h"

namespace om {
namespace rptr {

// ---------------------------------------------------------------------------------------------- pages
enum class Page : uint8_t {
  List = 0,
  Repeater,       // before login: Regions / Clock / Signal + Log in; after: the status card + list
  Name,
  Position,
  Radio,
  Tx,
  Repeat,
  Regions,
  AddRegion,
  HomePicker,
  DefaultPicker,
  Neighbours,
  Advertising,
  More,
  Console,
};

const char* pageName(Page p);

// ---------------------------------------------------------------------------------------------- rows
// What a tap does. `None` is a fact (and every console log line): not tappable at all.
enum class Act : uint8_t {
  None = 0,
  Open,          // go to the row's page
  LogIn,         // the one big button on the page before login
  Forget,        // forget the remembered password
  Refresh,       // the status card's refresh icon
  SetClock,      // the clock line's one button
  Restart,       // the Restart row: opens its confirmation directly
  Apply,         // the page's one Apply button
  Toggle,        // flip a draft bool on this page
  Step,          // a -/+ pair (the row's part says which)
  Edit,          // the keyboard for this value
  Pick,          // the row opens HomePicker / DefaultPicker (see index)
  AddRegion,     // the name sheet for a new region
  RegionToggle,  // one region's "Repeats floods here" checkbox
  RemoveRegion,  // one region's remove key
  Discover,      // neighbours: discover.neighbors
  ConsoleEdit,   // the console's free command line
  ConsoleSend,   // send what was typed
};

struct UiRow {
  char label[24] = "";
  char value[64] = "";
  Act act = Act::None;
  bool on = false;
  bool danger = false;
  uint8_t index = 0;      // region ordinal, picker kind, ...
  bool stepKeys = false;  // draw the -/+ pair
  bool arrow = false;     // draw the ">" key at the right
  char key[4] = "";       // draw one small key with this text ("x" to remove)
};

constexpr int kUiRowCap = 26;
constexpr int kConsoleLines = 8;

// ------------------------------------------------------------------------------------------- facts
// What the repeater itself reported. A value is shown only when its `...Known` flag is set; nothing
// is invented from a default.
struct Facts {
  char name[32] = "";
  int32_t latE6 = 0;
  int32_t lonE6 = 0;
  RadioValues radio;
  int8_t tx = 14;
  bool repeat = true;
  uint8_t advertLocal = kAdvertLocalDefault;
  uint8_t advertFlood = kAdvertFloodDefault;
  uint8_t floodMax = kFloodMaxDefault;
  LoopDetect loop = LoopDetect::Moderate;
  uint8_t pathHash = 1;
  uint32_t clock = 0;
  bool hasClock = false;

  bool nameKnown = false;
  bool posKnown = false;
  bool radioKnown = false;
  bool txKnown = false;
  bool repeatKnown = false;
  bool advertLocalKnown = false;
  bool advertFloodKnown = false;
  bool floodMaxKnown = false;
  bool loopKnown = false;
  bool pathHashKnown = false;

  void reset();
};

// ------------------------------------------------------------------------------------------ drafts
// What the open page's controls hold before Apply. reset() copies every read-back truth into the
// draft and forgets the old changes, so a page always starts from the repeater's own answer.
struct Draft {
  AdminValues v;                 // radio (incl. tx), adverts, flood max, loop, path hash, repeat
  char name[32] = "";
  int32_t latE6 = 0;
  int32_t lonE6 = 0;

  bool flood[kMaxRegions] = {};        // "Repeats floods here" per region row
  bool floodChanged[kMaxRegions] = {}; // only a changed row is sent by Apply
  bool hasAdd = false;
  char addName[kRegionNameMax + 1] = "";
  char addParent[32] = "";
  int8_t parentSel = -1;            // the add-region parent: -1 = the wildcard, else a region row

  bool homeChanged = false;
  char home[32] = "";               // "*" = anywhere (the wildcard)
  bool defaultChanged = false;
  bool defaultNone = true;          // true = "none" (<null>)
  char defaultScope[32] = "";

  void reset(const Facts& f, const RegionList& regions, const char* homeName,
             const char* defScope);
};

// ----------------------------------------------------------------------------------------- the list
struct ListRow {
  char name[32] = "";
  char signal[24] = "";   // "good 11 dB", empty when the answer carried no SNR
  uint8_t foundIndex = 255;   // the row in FoundList, 255 = remembered but not heard this scan
  bool saved = false;     // a remembered password exists: the small key icon
  bool inContacts = false;
};

// Remembered repeaters first (a key icon), then the scan answers. `remembered` are the names with a
// stored password. Returns the row count.
int listRows(const FoundList& found, const char* const* remembered, int rememberedCount,
             ListRow* out, int cap);
// "unnamed 86a8ee": a repeater that did not name itself in the answer.
int unnamedText(const uint8_t* prefix6, char* out, size_t cap);
// SNR x4 -> "excellent 12 dB" .. "poor -9 dB". False when the SNR is not known (nothing is drawn).
bool signalText(bool has, int8_t snr4, char* out, size_t cap);
const char* signalWord(int8_t snr4);

// --------------------------------------------------------------------------------------- the screen
struct StatusCell {
  char label[10] = "";
  char value[26] = "";
};

// What the page shows before login, from the scan answer and the anonymous regions read.
struct AnonFacts {
  bool asked = false;
  bool valid = false;       // a parseable regions reply
  int regionCount = 0;
  char regions[64] = "";    // "*, si, lj" or "" when the repeater has none
  uint32_t clock = 0;
  bool signalKnown = false;
  int8_t snr4 = 0;
};

struct ScreenIn {
  Page page = Page::List;
  bool loggedIn = false;
  bool busy = false;
  bool remembered = false;
  const char* name = "";
  const Status* status = nullptr;
  const Facts* facts = nullptr;
  const Draft* draft = nullptr;
  const RegionList* regions = nullptr;
  const NeighbourList* neighbours = nullptr;
  const AnonFacts* anon = nullptr;
  const char* home = nullptr;          // nullptr = not read yet, "" not used, "*" anywhere
  const char* defaultScope = nullptr;  // nullptr = not read yet, "" = none
  const char* result = nullptr;        // "Applied" or the error, empty = nothing to say
  bool resultOk = false;
  bool resultPending = false;
  bool clockWrong = false;
  const char* const* log = nullptr;
  int logCount = 0;
  const char* consoleInput = "";
};

struct ScreenOut {
  char title[40] = "";
  char subtitle[80] = "";
  char banner[120] = "";       // "Clock is wrong" and its detail, or a note before login
  bool bannerButton = false;   // the one "Set clock" button on the banner
  UiRow rows[kUiRowCap];
  int count = 0;
  bool bigButton = false;      // the one big "Log in" button (page before login)
  char bigLabel[16] = "";
  bool hasApply = false;
  bool applyDanger = false;
  StatusCell cells[6];
  int cellCount = 0;
  bool hasConsole = false;
  char logLines[kConsoleLines][88];
  int logLineCount = 0;
  char consoleInput[64] = "";

  // EST-363: clear IN PLACE. `*out = ScreenOut()` materialised a 3,812-byte temporary on the loop
  // task's stack, and that (plus RptrView's 6,204 bytes) is what panicked the L2 Pro when Log in was
  // tapped in EST-362. A reset that writes only its own fields has no temporary at all.
  void reset();
};

// Fills `out` for one page. Everything the screen draws comes from here, so a host test proves the
// rows (which ones exist before and after login) without a display.
int buildScreen(const ScreenIn& in, ScreenOut* out);

// ---------------------------------------------------------------------------------------- the apply
constexpr int kApplyMax = 10;
struct ApplyPlan {
  int count = 0;
  char line[kApplyMax][80];
};

// The exact lines one Apply sends, in order. Regions append `region save` themselves (the ticket's
// rule: a region change is followed by the save in the same step); every `set ...` line persists by
// itself (CommonCLI.cpp calls savePrefs() from every set handler).
int applyPlan(Page page, const Draft& d, const RegionList& regions, ApplyPlan* out);

// The read-back lines sent after the writes, so the page shows the repeater's own value.
constexpr int kReadBackMax = 4;
int readBackLines(Page page, char out[kReadBackMax][24]);

// Which actions store one answer / log line in the console, and the result the page shows.
struct ConfirmAsk {
  bool need = false;
  char text[128] = "";
  char verb[12] = "";
};

// The confirmation an Apply needs: only Radio (the repeater drops off the mesh until it restarts)
// and switching Repeat off (the mesh beyond it goes silent). Everything else applies directly.
ConfirmAsk confirmForApply(Page page, const Draft& d);
ConfirmAsk confirmRestart();
ConfirmAsk confirmRemoveRegion(const char* name);
// A clock sync: no ask up to a day, the one sentence with both dates over a day, nothing when the
// guard refuses (the page shows the refusal itself).
ConfirmAsk confirmClock(uint32_t fromEpoch, uint32_t toEpoch);

// True when `act` is something a finger can press. Log lines, facts and the status cells are not.
bool tappable(Act act);

}  // namespace rptr
}  // namespace om
