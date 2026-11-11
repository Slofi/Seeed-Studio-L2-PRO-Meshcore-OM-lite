// SPDX-License-Identifier: MIT
// Settings geometry and list arithmetic, plain C++ so test/host can prove it with the system g++.
//
// The spec is the mock's renderSettings() and the .setwrap / .sect / .setrow / .lbl / .val / .chev /
// .check / .warn CSS: rows at least 30 px, mono section headers, values in mono dim on the right,
// a chevron for a sub-page and a filled dot for the chosen option. The settings tab row sits under
// the status bar and is laid out like the bottom tab bar (five equal taps, gold underline, mono).
//
// ONE rectangle drives both the drawing and the tap: a page is a flat array of SettingRow, this
// header hands out their rectangles from the scroll, and the same rectangles answer the hit tests.
#pragma once

#include <stdint.h>
#include <string.h>

#include "../timekeeper.h"   // daysInMonth / isLeapYear: one calendar, not a second copy
#include "layout.h"
#include "theme.h"

namespace om {
namespace ui {

// --------------------------------------------------------------------------------------------- tabs
enum class SettingsTab : uint8_t { General = 0, Nodes, Chat, Map, Rptr, Count };
constexpr int kSettingsTabCount = (int)SettingsTab::Count;

constexpr const char* settingsTabLabel(SettingsTab t) {
  switch (t) {
    case SettingsTab::General: return "General";
    case SettingsTab::Nodes:   return "Nodes";
    case SettingsTab::Chat:    return "Chat";
    case SettingsTab::Map:     return "Map";
    case SettingsTab::Rptr:    return "RPTR";
    default:                   return "";
  }
}

// The gear opens Settings on the tab of the screen it was pressed on: Nodes -> Nodes, Chat -> Chat,
// Map -> Map, RPTR -> RPTR. General is always one tap away on the tab row.
constexpr SettingsTab settingsTabForMainTab(int mainTab) {
  switch (mainTab) {
    case 0: return SettingsTab::Nodes;
    case 1: return SettingsTab::Chat;
    case 2: return SettingsTab::Map;
    case 3: return SettingsTab::Rptr;
    default: return SettingsTab::General;
  }
}

// The mono face's advance is 6 px for every glyph (checked against the VLW blob; status_cells.h
// relies on the same fact). The tab labels are mono, so their width is exact without a display.
constexpr int16_t kSetMonoAdvance = 6;
constexpr int16_t kSetTabLabelPadX = 5;   // air on each side of a label inside its tab

constexpr int16_t settingsTabLabelWidth(SettingsTab t) {
  return (int16_t)(kSetMonoAdvance * (int16_t)strlen(settingsTabLabel(t)));
}

// True when the label plus its padding fits inside the tab's own rectangle at this width. Five tabs
// x 64 px at 320 px: the longest label, "General", is 42 px and needs 52, so it fits with 12 px to
// spare (host-tested at 320 and at 800, where the tabs are wider).
constexpr bool settingsTabFits(SettingsTab t, int16_t width) {
  const int16_t tabW = (int16_t)(width / kSettingsTabCount);
  return settingsTabLabelWidth(t) + 2 * kSetTabLabelPadX <= tabW;
}

// --------------------------------------------------------------------------------------------- pages
// EST-350: every Settings page, here rather than in the shell, so the history that Back walks and
// the host test that proves it speak about the same thing. The five roots are the Settings tabs; the
// rest are pages reached from a row.
// EST-392: there is no Position PAGE any more. The one source control is the General page's Position
// row and it opens a two-row choice, so this list holds no second place to choose the source.
enum class SetPage : uint8_t {
  GeneralRoot = 0, NodesRoot, ChatRoot, MapRoot, RptrRoot,
  Preset, PresetCustom, Hash, Node, NameEdit, Channels, Channel, ChanAdd, ChanKey,
  Sound, SoundPicker, Wifi, WifiSaved, WifiPass, Bt, Time, TimeEdit, ConfirmPreset, ConfirmHash,
  Count,
};
constexpr int kSetPageCount = (int)SetPage::Count;

constexpr bool settingsRootPage(SetPage p) { return (int)p <= (int)SetPage::RptrRoot; }

// Where the user came from, one entry per step away from a page: the page and the px it was scrolled
// to. Back pops the last step, so General > Node > Name > Back > Back lands on General exactly where
// it was left (Filip, EST-350: "Back from the Node page returns to General at the same scroll
// position"). A full stack drops its OLDEST step, never the newest: Back always has a way up.
class SettingsNav {
 public:
  static constexpr int kMaxDepth = 10;

  void reset() { depth_ = 0; }
  bool empty() const { return depth_ == 0; }
  int depth() const { return depth_; }

  void push(SetPage page, int scroll) {
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

  // Pops one step. False (and nothing written) when there is none: Back then leaves Settings.
  bool pop(SetPage* page, int* scroll) {
    if (depth_ <= 0) return false;
    --depth_;
    if (page != nullptr) *page = page_[depth_];
    if (scroll != nullptr) *scroll = scroll_[depth_];
    return true;
  }

 private:
  SetPage page_[kMaxDepth] = {};
  int16_t scroll_[kMaxDepth] = {};
  int depth_ = 0;
};

// --------------------------------------------------------------------------------------------- rows
// A page is a flat list. Section headers are rows too, so one index addresses everything and the
// content height is the sum of the same heights the drawing uses.
enum class SetRowKind : uint8_t {
  Section = 0,   // mono dimmer uppercase label
  Value,         // label left, value right (read-only)
  Chevron,       // label left, value right, '>' at the edge: opens a sub-page
  Check,         // label left, value right, filled dot for the chosen option
  Toggle,        // label left, ON/OFF right (green when on)
  Stepper,       // label left, [-] value [+]
  Button,        // a bordered key with a centred label
  Warn,          // the mock's .warn box, word-wrapped, up to three lines
  Note,          // one dim line of plain text
  Selector,      // EST-350: a section header that IS the chat selector (label, name, chevron)
};

constexpr int kSetLabelCap = 160;
constexpr int kSetValueCap = 64;   // the picker's short use tags can name several events

struct SettingRow {
  SetRowKind kind = SetRowKind::Value;
  uint16_t id = 0;             // the shell's action id; only it reads the number
  char label[kSetLabelCap] = "";
  char value[kSetValueCap] = "";
  bool on = false;             // Toggle / Check state
  bool danger = false;         // Button: destructive (red edge, as today's delete)
};

constexpr int16_t settingsRowHeight(SetRowKind kind) {
  switch (kind) {
    case SetRowKind::Section: return 20;
    case SetRowKind::Warn:    return 44;   // three 11 px lines and the box padding
    case SetRowKind::Note:    return 22;
    case SetRowKind::Button:  return 30;
    case SetRowKind::Stepper: return kRowH;   // 30, the mock's min-height
    default:                  return kRowH;
  }
}

// ------------------------------------------------------------------------------------------- layout
// The status bar is the app's own (26 px). On the root the settings tab row follows it (25 px), then
// a title row with the back key (26 px). A sub-page has no tab row: the back key and the title move
// up under the status bar, which is the mock's setwrap.
constexpr int16_t kSetTabH = kTabH;   // the tab row is the bottom tab bar s height
constexpr int16_t kSetPadX = 7;      // .setrow padding 3px 7px
constexpr int16_t kSetTitleH = 26;
constexpr int16_t kSetBackW = 50;
constexpr int16_t kSetBackH = 18;
// EST-363 (Filip, 2026-11-10): "The 'Use this sound' button should be in the bar where 'Back' and
// 'Sound' are, so I don't have to scroll all the way to the top after selecting a sound." A page
// whose commit action used to be a row gets it in the title row instead, on the right, where it can
// never scroll away. Same shape as the keyboard's OK key: gold when there is something to commit,
// dim when there is not.
constexpr int16_t kSetActionW = 52;
constexpr int16_t kSetActionH = 18;

struct SettingsLayout {
  Rect status;
  Rect tabrow;
  Rect title;
  Rect backButton;
  Rect titleAction;    // EST-363: the page's commit action in the title row (empty when the page has none)
  Rect body;
  Rect tabs[kSettingsTabCount];
  bool subPage = false;
  // EST-350: the chat selector's dropdown, over the body under its selector row (empty when closed).
  // Same three rectangles the Chat tab's dropdown uses, so one drawing and one hit test serve both.
  Rect selPanel;
  Rect selList;
  Rect selManage;
};

constexpr SettingsLayout settingsLayoutFor(int16_t w, int16_t h, bool subPage,
                                           bool selector = false, bool titleAction = false) {
  SettingsLayout L;
  L.subPage = subPage;
  L.status = Rect{0, 0, w, kStatusH};
  const int16_t tabTop = (int16_t)(kStatusH);
  L.tabrow = Rect{0, tabTop, w, (int16_t)(subPage ? 0 : kSetTabH)};
  for (int i = 0; i < kSettingsTabCount; ++i) {
    const int16_t x0 = (int16_t)((int32_t)w * i / kSettingsTabCount);
    const int16_t x1 = (int16_t)((int32_t)w * (i + 1) / kSettingsTabCount);
    L.tabs[i] = Rect{x0, tabTop, (int16_t)(x1 - x0), (int16_t)(subPage ? 0 : kSetTabH)};
  }
  const int16_t titleTop = (int16_t)(kStatusH + (subPage ? 0 : kSetTabH));
  L.title = Rect{0, titleTop, w, kSetTitleH};
  L.backButton = Rect{(int16_t)(kStatusPadX), (int16_t)(titleTop + (kSetTitleH - kSetBackH) / 2),
                      kSetBackW, kSetBackH};
  // EST-363: the page's commit action, on the right of the same title row (the keyboard's OK slot).
  // It never scrolls: the title row is outside the body.
  L.titleAction = titleAction
                      ? Rect{(int16_t)(w - kSetPadX - kSetActionW),
                             (int16_t)(titleTop + (kSetTitleH - kSetActionH) / 2), kSetActionW,
                             kSetActionH}
                      : Rect{};
  const int16_t bodyTop = (int16_t)(titleTop + kSetTitleH);
  L.body = Rect{0, bodyTop, w, (int16_t)(h - bodyTop)};
  if (selector) {
    // The dropdown hangs under the selector row, which stays visible above it (like the Chat tab's
    // header): the row is the page's first row, so its height is the same 30 px the drawing uses.
    const int16_t top = (int16_t)(L.body.y + settingsRowHeight(SetRowKind::Selector));
    L.selPanel = Rect{L.body.x, top, L.body.w, (int16_t)(L.body.bottom() - top)};
    L.selManage = Rect{L.selPanel.x, (int16_t)(L.selPanel.bottom() - kSelManageH), L.selPanel.w,
                       kSelManageH};
    L.selList = Rect{L.selPanel.x, L.selPanel.y, L.selPanel.w,
                     (int16_t)(L.selManage.y - L.selPanel.y)};
  }
  return L;
}

// EST-382: the one confirmation pop-up (Filip's rule: confirmations are pop-ups, not a second page).
// Used by "Clear messages" on a channel's own page: a scrim, the question, then Cancel on the left
// (the DEFAULT side: it is drawn as the primary key) and the destructive Clear on the right. A tap
// outside the box is Cancel and is consumed, exactly like the RPTR confirmation.
struct SettingsModalLayout {
  Rect box;
  Rect text;
  Rect cancel;
  Rect confirm;
};

constexpr SettingsModalLayout settingsModalLayoutFor(int16_t w, int16_t h) {
  SettingsModalLayout m;
  const int16_t boxW = w > 32 ? (int16_t)(w - 24) : w;
  const int16_t boxH = 112;
  m.box = Rect{(int16_t)((w - boxW) / 2), (int16_t)((h - boxH) / 2), boxW, boxH};
  m.text = Rect{(int16_t)(m.box.x + 12), (int16_t)(m.box.y + 10), (int16_t)(boxW - 24),
                (int16_t)(boxH - 58)};
  const int16_t bw = 100;
  const int16_t bh = 26;
  m.cancel = Rect{(int16_t)(m.box.x + 12), (int16_t)(m.box.bottom() - bh - 12), bw, bh};
  m.confirm = Rect{(int16_t)(m.box.right() - 12 - bw), (int16_t)(m.box.bottom() - bh - 12), bw, bh};
  return m;
}

enum class SettingsModalHit : uint8_t { None = 0, Cancel, Confirm, Outside };

constexpr SettingsModalHit settingsModalHitAt(const SettingsModalLayout& m, int x, int y) {
  if (m.cancel.contains(x, y)) return SettingsModalHit::Cancel;
  if (m.confirm.contains(x, y)) return SettingsModalHit::Confirm;
  return SettingsModalHit::Outside;
}

constexpr int settingsTabAtIn(const SettingsLayout& L, int x, int y) {
  if (L.tabrow.empty() || !L.tabrow.contains(x, y)) return -1;
  for (int i = 0; i < kSettingsTabCount; ++i) {
    if (L.tabs[i].contains(x, y)) return i;
  }
  return -1;
}

// EST-363: a tap on the title row's commit action. Nothing else in the title row acts except Back,
// and the two rectangles never overlap (Back is on the left, the action on the right).
constexpr bool settingsTitleActionHitIn(const SettingsLayout& L, int x, int y) {
  return !L.titleAction.empty() && L.titleAction.contains(x, y);
}

// EST-363: the action is live only when the page's draft differs from what is stored (the picker's
// candidate vs the event's current sound, "If nothing new was picked, Use is dim"). Back never writes
// the stored value: a pick only changes the draft, so leaving the page leaves the choice as it was.
constexpr bool settingsActionEnabled(int pending, int stored) { return pending != stored; }

// ------------------------------------------------------------------------------------ list maths
constexpr int settingsRowTop(const SettingRow* rows, int count, int index) {
  int y = 0;
  const int n = index < count ? index : count;
  for (int i = 0; i < n; ++i) y += settingsRowHeight(rows[i].kind);
  return y;
}

constexpr int settingsContentHeight(const SettingRow* rows, int count) {
  return settingsRowTop(rows, count, count);
}

constexpr int settingsMaxScroll(const SettingsLayout& L, const SettingRow* rows, int count) {
  const int total = settingsContentHeight(rows, count);
  return total > L.body.h ? total - L.body.h : 0;
}

// Full row rectangle in screen coordinates; negative y when scrolled above the body.
constexpr Rect settingsRowRect(const SettingsLayout& L, const SettingRow* rows, int count, int index,
                               int scroll) {
  const int16_t h = settingsRowHeight(rows[index].kind);
  return Rect{L.body.x, (int16_t)(L.body.y + settingsRowTop(rows, count, index) - scroll), L.body.w,
              h};
}

// The visible slice, exactly what the clip draws and what a tap can land on.
constexpr Rect settingsRowVisible(const SettingsLayout& L, const SettingRow* rows, int count,
                                  int index, int scroll) {
  return settingsRowRect(L, rows, count, index, scroll).intersect(L.body);
}

constexpr int settingsRowAtIn(const SettingsLayout& L, const SettingRow* rows, int count, int x,
                              int y, int scroll) {
  for (int i = 0; i < count; ++i) {
    const Rect v = settingsRowVisible(L, rows, count, i, scroll);
    if (!v.empty() && v.contains(x, y)) return i;
  }
  return -1;
}

// A stepper's two keys, in the same row rectangle the drawing uses. part: -1 the minus key,
// +1 the plus key, 0 neither. The value sits between them, so a tap on the number does nothing.
constexpr int16_t kSetStepW = 18;
constexpr int16_t kSetStepGap = 5;
constexpr int16_t kSetStepValueW = 66;   // room for "869.618 MHz" (11 mono glyphs)

constexpr Rect settingsStepMinusRect(const Rect& row) {
  return Rect{(int16_t)(row.right() - kSetPadX - 2 * kSetStepW - kSetStepGap - kSetStepValueW -
                        kSetStepGap),
              (int16_t)(row.y + (row.h - kSetStepW) / 2), kSetStepW, kSetStepW};
}

constexpr Rect settingsStepPlusRect(const Rect& row) {
  return Rect{(int16_t)(row.right() - kSetPadX - kSetStepW),
              (int16_t)(row.y + (row.h - kSetStepW) / 2), kSetStepW, kSetStepW};
}

constexpr Rect settingsStepValueRect(const Rect& row) {
  const Rect minus = settingsStepMinusRect(row);
  return Rect{(int16_t)(minus.right() + kSetStepGap), minus.y, kSetStepValueW, kSetStepW};
}

constexpr int settingsStepperPart(const Rect& row, int x, int y) {
  if (settingsStepMinusRect(row).contains(x, y)) return -1;
  if (settingsStepPlusRect(row).contains(x, y)) return 1;
  return 0;
}

// Same fingertip rule as the chat list (chat_layout.h selDragScroll): the content moves with the
// finger, with kSelGive px of stretch past either end, snapped back on release.
constexpr int settingsDragScroll(int base, int dyUp, int maxScroll) {
  int next = base + dyUp;
  if (next < -kSelGive) next = -kSelGive;
  if (next > maxScroll + kSelGive) next = maxScroll + kSelGive;
  return next;
}

constexpr int settingsSnapScroll(int v, int maxScroll) {
  if (v < 0) return 0;
  if (v > maxScroll) return maxScroll;
  return v;
}

// ------------------------------------------------------------------------------- position choice
// EST-392: Settings > General has ONE position control, the `Position` row. Its value is the source
// in use (Filip, 2026-11-11: "GPS and fixed position there ... clicking on it can then select GPS
// position or fixed, picked on a map"). A tap drops a small two-row choice UNDER the row, in the
// dropdown style the chat selector already uses: a panel, the current option highlighted, a dot on
// it. It is not a second page. Fixed opens EST-384's map picker unchanged; a tap anywhere else
// cancels and leaves the source exactly as it was.
//
// The labels, the rectangles and the meaning of a choice live here, so the drawing, the hit test,
// the pressed look and the host test cannot disagree about any of them.
enum class PosChoiceAct : uint8_t { Keep = 0, Gps, OpenPicker };

constexpr int kPosOptionGps = 0;
constexpr int kPosOptionFixed = 1;
constexpr int kPosChoiceCount = 2;

constexpr const char* kPosSectionLabel = "POSITION";  // the General section header (it read "GPS")
constexpr const char* kPosRowLabel = "Position";      // the one source control
constexpr const char* posOptionLabel(int i) { return i == kPosOptionFixed ? "Fixed" : "GPS"; }
constexpr const char* posOptionDesc(int i) {
  return i == kPosOptionFixed ? "picked on the map" : "live fix";
}

// What a choice means. -1 is Cancel: a tap outside the panel (or Back) changes nothing, so a node on
// Fixed keeps its saved point and a node on GPS stays on GPS.
constexpr PosChoiceAct posChoiceAction(int option) {
  if (option == kPosOptionGps) return PosChoiceAct::Gps;
  if (option == kPosOptionFixed) return PosChoiceAct::OpenPicker;
  return PosChoiceAct::Keep;
}

struct SettingsChoiceLayout {
  Rect panel;
  Rect option[kPosChoiceCount];
};

// The panel hangs from the row's own bottom edge, inset like a row's text, so the row stays readable
// above it (the selector dropdown's rule). A page whose floor still leaves no room below puts the
// panel ABOVE the row instead, so the choice is never cut off. settingsChoiceScrollFor() lifts the
// page first, so on a page with any content below the row the panel drops DOWN.
constexpr SettingsChoiceLayout settingsChoiceLayoutFor(const SettingsLayout& L, const Rect& row) {
  SettingsChoiceLayout c;
  const int16_t x = (int16_t)(L.body.x + kSetPadX);
  const int16_t w = (int16_t)(L.body.w - 2 * kSetPadX);
  const int16_t h = (int16_t)(kPosChoiceCount * kRowH);
  const int16_t top = (row.bottom() + h <= L.body.bottom()) ? row.bottom()
                                                            : (int16_t)(row.y - h);
  c.panel = Rect{x, top, w, h};
  for (int i = 0; i < kPosChoiceCount; ++i) {
    c.option[i] = Rect{x, (int16_t)(c.panel.y + i * kRowH), w, kRowH};
  }
  return c;
}

constexpr SettingsChoiceLayout settingsPositionChoiceFor(const SettingsLayout& L,
                                                         const SettingRow* rows, int count,
                                                         int rowIndex, int scroll) {
  if (rows == nullptr || rowIndex < 0 || rowIndex >= count) return SettingsChoiceLayout{};
  return settingsChoiceLayoutFor(L, settingsRowRect(L, rows, count, rowIndex, scroll));
}

// 0 (GPS) or 1 (Fixed) when the finger is on that option, -1 for anything else: the row above, Back,
// the page around the panel. The panel is solid, so every pixel of it is one of the two options.
constexpr int settingsChoiceOptionAt(const SettingsChoiceLayout& c, int x, int y) {
  for (int i = 0; i < kPosChoiceCount; ++i) {
    if (c.option[i].contains(x, y)) return i;
  }
  return -1;
}

// The scroll that keeps the whole panel inside the body: the row is lifted as little as needed and
// never past the page's own floor. Without it a row near the bottom would open a clipped choice.
constexpr int settingsChoiceScrollFor(const SettingsLayout& L, const SettingRow* rows, int count,
                                      int rowIndex, int scroll) {
  const int maxScroll = settingsMaxScroll(L, rows, count);
  int s = scroll < 0 ? 0 : (scroll > maxScroll ? maxScroll : scroll);
  if (rows == nullptr || rowIndex < 0 || rowIndex >= count) return s;
  const int rowBottom =
      settingsRowTop(rows, count, rowIndex) + settingsRowHeight(rows[rowIndex].kind);
  const int need = rowBottom + kPosChoiceCount * kRowH - L.body.h;
  if (need > s) s = need > maxScroll ? maxScroll : need;
  return s;
}

// ---------------------------------------------------------------------------------- manual time
// The manual-time page steps one civil field at a time. Everything wraps (the mock's +/- steppers):
// minute 59 -> 0, hour 23 -> 0, day past the month's last -> 1, month 12 -> 1, year 2099 -> 2000.
// A day is clamped to the month it lands in, so 31 Jan + 1 month is 28/29 Feb, never 31 Feb.
enum class TimeField : uint8_t { Year = 0, Month, Day, Hour, Minute, Count };

struct CivilTime {
  int year = 2026;
  int month = 1;
  int day = 1;
  int hour = 0;
  int minute = 0;
};

constexpr int civilMonthDays(int year, int month) { return om::daysInMonth(year, month); }

constexpr CivilTime stepCivilTime(CivilTime t, TimeField f, int dir) {
  if (dir == 0) return t;
  switch (f) {
    case TimeField::Year:
      t.year += dir;
      if (t.year > 2099) t.year = 2000;
      if (t.year < 2000) t.year = 2099;
      break;
    case TimeField::Month:
      t.month += dir;
      if (t.month > 12) t.month = 1;
      if (t.month < 1) t.month = 12;
      break;
    case TimeField::Day: {
      const int last = civilMonthDays(t.year, t.month);
      t.day += dir;
      if (t.day > last) t.day = 1;
      if (t.day < 1) t.day = last;
      break;
    }
    case TimeField::Hour:
      t.hour += dir;
      if (t.hour > 23) t.hour = 0;
      if (t.hour < 0) t.hour = 23;
      break;
    case TimeField::Minute:
      t.minute += dir;
      if (t.minute > 59) t.minute = 0;
      if (t.minute < 0) t.minute = 59;
      break;
    default:
      break;
  }
  // The day must exist in the month we ended in (a year step can turn 29 Feb into 28 Feb).
  const int last = civilMonthDays(t.year, t.month);
  if (t.day > last) t.day = last;
  if (t.day < 1) t.day = 1;
  return t;
}

}  // namespace ui
}  // namespace om
