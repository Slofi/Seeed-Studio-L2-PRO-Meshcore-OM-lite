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
  Discover,      // neighbours: read the repeater's own table (`neighbors`)
  MapPick,       // position: open the map picker
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
  // EST-366 (rule 4): this row's draft value differs from the repeater's read-back. The screen draws
  // a small gold dot, so the user sees WHICH field is unsaved, not only that Apply is there.
  bool changed = false;
  char key[4] = "";       // draw one small key with this text ("x" to remove)
  // EST-367: 0..100 draws a thin progress bar inside this row (the neighbour discovery's 60 s
  // window); 255 means no bar.
  uint8_t progress = 255;
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
  // EST-368: the repeater's advert location policy from `gps advert` (CommonCLI v1.18.0):
  // 0 = none, 1 = share (its own live position), 2 = prefs (its stored lat/lon). `posPolicyKnown`
  // false means the firmware never answered, so the page falls back to lat/lon 0 for Off.
  uint8_t posPolicy = 2;
  bool posPolicyKnown = false;

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
  // EST-368: the Position page's "Share position" switch. Off sends `gps advert none` where the
  // repeater has the command, else lat/lon 0, then one `advert`.
  bool sharePos = true;
  bool shareBase = true;         // what reset() seeded: differs only when the finger flipped it
  bool policyKnown = false;      // the repeater answered `gps advert` this session

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

  // EST-368: what the draft was seeded with. A field still equal to this was NOT touched by the
  // finger, so a read-back answer may update it; a field the user moved differs from it and is never
  // overwritten by an answer. This is what stops the first pick on a settings page from vanishing
  // when the page's own opening read-backs land a moment later.
  AdminValues base;
  char nameBase[32] = "";
  int32_t latBase = 0;
  int32_t lonBase = 0;
  bool floodBase[kMaxRegions] = {};
  char homeBase[32] = "";
  bool defaultNoneBase = true;
  char defaultScopeBase[32] = "";

  void reset(const Facts& f, const RegionList& regions, const char* homeName,
             const char* defScope);
  // EST-368: one read-back answer. Every field that is still clean (equal to what reset() seeded)
  // takes the repeater's new value and its baseline; every touched field is left exactly as the
  // finger left it.
  void syncClean(const Facts& f, const RegionList& regions, const char* homeName,
                 const char* defScope);
};

// ------------------------------------------------------------------------------------ draft vs facts
// EST-366 (Filip, rule 4): Apply exists only while the draft differs from what the repeater answered.
// This is the ONE comparison: buildScreen reads it for `hasApply` and for the per-row dot, and the
// shell reads it for nothing else (the read-back still owns the truth).
struct DirtyFields {
  bool any = false;
  bool name = false;
  bool lat = false;
  bool lon = false;
  bool freq = false;
  bool bw = false;
  bool sf = false;
  bool cr = false;
  bool tx = false;
  bool repeat = false;
  bool advertLocal = false;
  bool advertFlood = false;
  bool floodMax = false;
  bool loop = false;
  bool pathHash = false;
  bool floodRegion = false;   // at least one region's flood flag
  bool home = false;
  bool defScope = false;
  bool addRegion = false;
  bool sharePos = false;      // EST-368: the Position page's "Share position" switch
};
// `regions`/`home`/`defScope` are the repeater's read-backs (home "*" = anywhere, "" = none).
DirtyFields draftChanges(const Facts& f, const Draft& d, const RegionList& regions,
                         const char* home, const char* defScope);

// ------------------------------------------------------------------------------------- page history
// EST-366 (rule 7, Filip ~21:25): every RPTR sub-page is reached through a stack, exactly like
// Settings (EST-350). Back pops ONE level and restores the scroll that page had; only an empty stack
// falls back to the repeater page (and the repeater page's Back leaves the session).
class PageNav {
 public:
  static constexpr int kMaxDepth = 8;

  void reset() { depth_ = 0; }
  bool empty() const { return depth_ == 0; }
  int depth() const { return depth_; }
  void push(Page page, int scroll) {
    if (depth_ >= kMaxDepth) {
      for (int i = 1; i < kMaxDepth; ++i) {
        page_[i - 1] = page_[i];
        scroll_[i - 1] = scroll_[i];
      }
      depth_ = kMaxDepth - 1;
    }
    page_[depth_] = page;
    scroll_[depth_] = (int16_t)scroll;
    ++depth_;
  }
  bool pop(Page* page, int* scroll) {
    if (depth_ <= 0) return false;
    --depth_;
    if (page != nullptr) *page = page_[depth_];
    if (scroll != nullptr) *scroll = scroll_[depth_];
    return true;
  }

 private:
  Page page_[kMaxDepth] = {};
  int16_t scroll_[kMaxDepth] = {};
  int depth_ = 0;
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
  // EST-364: a cell that is a state, not a reading ("Status  no answer yet") is drawn dim, so it
  // never looks like a number the repeater sent.
  bool dim = false;
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
  // EST-367: the neighbour discovery's 60 s window (nullptr = no scan started this session), and the
  // monotonic clock its progress and seconds-left are measured against.
  const NeighbourScan* scan = nullptr;
  uint32_t nowMs = 0;
  // The wall clock, so a neighbour's "heard 42 s ago" can be shown as the time it was heard (the
  // house rule: a time beats an age). nowReal says whether the clock was a real source.
  uint32_t nowWall = 0;
  bool nowReal = false;
  const AnonFacts* anon = nullptr;
  const char* home = nullptr;          // nullptr = not read yet, "" not used, "*" anywhere
  const char* defaultScope = nullptr;  // nullptr = not read yet, "" = none
  const char* result = nullptr;        // "Applied" or the error, empty = nothing to say
  bool resultOk = false;
  bool resultPending = false;
  bool clockWrong = false;
  // EST-364: a status request went out and nothing has come back yet (after the automatic retry).
  // The card then says "Status  no answer yet" in dim ink, with the refresh beside it: a request
  // without an answer is not a refusal, so nothing turns red.
  bool statusWait = false;
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
