// SPDX-License-Identifier: MIT
// EST-390: where the pressed look goes, screen by screen, as the SAME answer the hit test gives.
//
// Every function here asks the screen's existing hit test first and then returns the rectangle that
// hit test just used. That is the ticket's rule ("draw the pressed look on EXACTLY the rect the hit
// test uses") expressed in code: there is no second set of coordinates to drift, and a host test can
// walk a grid of points over every screen and prove the rectangle contains the point and matches the
// layout's own rect.
//
// Plain C++ (no LovyanGFX, no Arduino), so test/host compiles it with the system g++.
//
// What is deliberately NOT a target: a pan surface (the map canvas, a message band's empty part), the
// status bar's inert cells, and every "close/consume" area (a sheet header, a tap away). Those are
// consumed presses or plain surfaces, and a consumed tap must show no pressed look at all.
#pragma once

#include <stdint.h>

#include "chat_layout.h"
#include "layout.h"
#include "map_chrome.h"
#include "node_menu.h"
#include "press.h"
#include "rptr_layout.h"
#include "settings_layout.h"

namespace om {
namespace ui {

// The bottom tab bar, the gear and the radio-fault mark: the shell's own chrome on every screen.
inline PressTarget pressTargetForChrome(const Layout& L, int x, int y) {
  PressTarget t;
  const int tab = L.tabAt(x, y);
  if (tab >= 0) {
    t.kind = PressKind::Tab;
    t.rect = L.tab((Tab)tab);
    return t;
  }
  if (L.gear().contains(x, y)) {
    t.kind = PressKind::Button;
    t.rect = L.gear();
    return t;
  }
  if (L.radioFault().contains(x, y)) {
    t.kind = PressKind::Button;
    t.rect = L.radioFault();
    return t;
  }
  return t;
}

// One visible row of the Nodes list, the same visible slice rowAt() tests.
inline PressTarget pressTargetForNodesRow(const Layout& L, int scroll, int count, int x, int y) {
  PressTarget t;
  const int row = L.rowAt(x, y, scroll, count);
  if (row < 0) return t;
  t.kind = PressKind::Row;
  t.rect = L.rowVisible(row, scroll);
  return t;
}

// One row of a Nodes long-press sheet (the action menu and the trace hops share the layout).
inline PressTarget pressTargetForNodeSheetRow(const NodeSheetLayout& g, int x, int y) {
  PressTarget t;
  const int row = nodeSheetRowAt(g, x, y);
  if (row < 0 || row >= g.rowCount) return t;
  t.kind = PressKind::Row;
  t.rect = g.rows[row];
  return t;
}

// The shared chat/Settings dropdown: a chat row, or the pinned Manage row. A section label, a pad or
// anything outside the panel is a CLOSE (consumed), so it is not a target.
inline PressTarget pressTargetForSelectorRow(const Rect& list, int channelCount, int dmCount,
                                             int scroll, const Rect& manage, int x, int y) {
  PressTarget t;
  if (!manage.empty() && manage.contains(x, y)) {
    t.kind = PressKind::Row;
    t.rect = manage;
    return t;
  }
  if (list.empty() || !list.contains(x, y)) return t;
  const int i = selRowAt(list, channelCount, dmCount, scroll, y);
  if (i < 0) return t;
  t.kind = PressKind::Row;
  t.rect = Rect{list.x, (int16_t)selRowTop(list, channelCount, dmCount, i, scroll), list.w, kSelRowH};
  return t;
}

// One key of the shared keyboard sheet. The pressed fill is keyHit (the tap target: the outer keys
// reach the screen edges), the bubble is centred on the DRAWN key, and the bubble's limit is the
// draft line, so it can never cover the text being typed.
inline PressTarget pressTargetForKeypad(const ChatLayout& g, bool nums, int x, int y,
                                        KeyboardField field = KeyboardField::Chat,
                                        bool shift = false) {
  PressTarget t;
  const KeyHit key = keyAt(g, nums, x, y, field);
  if (key.row < 0) return t;
  KeyDef defs[10];
  const int n = keyRow(nums, key.row, defs, field);
  if (key.col < 0 || key.col >= n) return t;
  t.kind = PressKind::Key;
  t.rect = g.keyHit[key.row][key.col];
  t.bubbleKey = g.keys[key.row][key.col];
  t.bubbleTopLimit = g.draft.y;
  if (defs[key.col].kind == KeyKind::Char && defs[key.col].label[0] != 0) {
    char c = defs[key.col].label[0];
    if (shift && c >= 'a' && c <= 'z') c = (char)(c - 32);
    t.cp = (uint32_t)(unsigned char)c;
  }
  return t;
}

// One emoji cell (a recent pick or a grid cell). The glyph is already key-sized, so no bubble.
inline PressTarget pressTargetForEmojiCell(const Rect& cell, uint32_t cp) {
  PressTarget t;
  if (cell.empty()) return t;
  t.kind = PressKind::Emoji;
  t.rect = cell;
  t.cp = cp;
  return t;
}

// One Settings row, its stepper keys, the tab row, Back and the title row's action. The stepper's
// -/+ are their own targets: a tap on the number between them does nothing and gets no look.
inline PressTarget pressTargetForSettings(const SettingsLayout& L, const SettingRow* rows, int count,
                                          int scroll, int x, int y) {
  PressTarget t;
  if (!L.tabrow.empty() && L.tabrow.contains(x, y)) {
    const int tab = settingsTabAtIn(L, x, y);
    if (tab >= 0) {
      t.kind = PressKind::Tab;
      t.rect = L.tabs[tab];
    }
    return t;
  }
  if (!L.backButton.empty() && L.backButton.contains(x, y)) {
    t.kind = PressKind::Button;
    t.rect = L.backButton;
    return t;
  }
  if (settingsTitleActionHitIn(L, x, y)) {
    t.kind = PressKind::Button;
    t.rect = L.titleAction;
    return t;
  }
  if (rows == nullptr || count <= 0) return t;
  const int i = settingsRowAtIn(L, rows, count, x, y, scroll);
  if (i < 0) return t;
  const Rect row = settingsRowVisible(L, rows, count, i, scroll);
  if (rows[i].kind == SetRowKind::Stepper) {
    const Rect full = settingsRowRect(L, rows, count, i, scroll);
    const int part = settingsStepperPart(full, x, y);
    if (part < 0) {
      t.kind = PressKind::Button;
      t.rect = settingsStepMinusRect(full);
      return t;
    }
    if (part > 0) {
      t.kind = PressKind::Button;
      t.rect = settingsStepPlusRect(full);
      return t;
    }
    // The label and the value between the two keys: the row is a target (a tap there opens the
    // row's own action, as it always did), the pressed look covers the row.
    t.kind = PressKind::Row;
    t.rect = row;
    return t;
  }
  t.kind = PressKind::Row;
  t.rect = row;
  return t;
}

// EST-392: the Position row's open choice. Its two option rows are the only targets; a tap anywhere
// else is the Cancel that closes it (consumed), so nothing behind it shows a pressed look.
inline PressTarget pressTargetForPositionChoice(const SettingsChoiceLayout& c, int x, int y) {
  PressTarget t;
  const int i = settingsChoiceOptionAt(c, x, y);
  if (i < 0) return t;
  t.kind = PressKind::Row;
  t.rect = c.option[i];
  return t;
}

// A confirmation pop-up's two keys. A tap outside the box is Cancel and is consumed, so only the two
// rectangles are targets.
inline PressTarget pressTargetForModalKey(bool confirmSide, const Rect& cancel, const Rect& confirm,
                                          int x, int y) {
  PressTarget t;
  const Rect& r = confirmSide ? confirm : cancel;
  if (r.empty() || !r.contains(x, y)) return t;
  t.kind = PressKind::Button;
  t.rect = r;
  return t;
}

// The map's own chrome: the five edge tabs, the hide/show chip, the ghost zoom zones, a marker and
// the callout. The canvas itself pans and is not a target.
inline PressTarget pressTargetForMap(const MapChrome& c, const MapMarker* markers, int count, int x,
                                     int y) {
  PressTarget t;
  const MapHit hit = mapHitAt(c, markers, count, x, y);
  switch (hit.kind) {
    case MapHitKind::ZoomIn:
      t.kind = PressKind::Tab;
      t.rect = c.tabsHidden ? c.zoomInShown : c.zoomIn;
      break;
    case MapHitKind::ZoomOut:
      t.kind = PressKind::Tab;
      t.rect = c.tabsHidden ? c.zoomOutShown : c.zoomOut;
      break;
    case MapHitKind::Lock:   t.kind = PressKind::Tab; t.rect = c.lock; break;
    case MapHitKind::Traces: t.kind = PressKind::Tab; t.rect = c.traces; break;
    case MapHitKind::Names:  t.kind = PressKind::Tab; t.rect = c.names; break;
    case MapHitKind::TabsToggle:
      t.kind = PressKind::Tab;
      t.rect = c.chipTap;    // the finger target the hit test used (the chip itself is smaller)
      break;
    case MapHitKind::Callout:
      t.kind = PressKind::Button;
      t.rect = c.callout;
      break;
    case MapHitKind::Marker: {
      // The hit test uses a radius; the drawn pressed plate is its bounding box, centred on the dot.
      const MapMarker& m = markers[hit.marker];
      t.kind = PressKind::Button;
      t.rect = Rect{(int16_t)(m.x - kMapMarkerHitR), (int16_t)(m.y - kMapMarkerHitR),
                    (int16_t)(2 * kMapMarkerHitR), (int16_t)(2 * kMapMarkerHitR)};
      break;
    }
    default:
      break;   // the canvas, the status bar, the tucked strip: no pressed look
  }
  return t;
}

// The RPTR list: the Scan chip and one row.
inline PressTarget pressTargetForRptrList(const RptrListLayout& r, int count, int scroll, int x,
                                          int y) {
  PressTarget t;
  const RptrHitResult hit = rptrListHitAt(r, count, scroll, x, y);
  if (hit.kind == RptrHit::Scan) {
    t.kind = PressKind::Button;
    t.rect = r.scan;
  } else if (hit.kind == RptrHit::ListRow) {
    // The hit test only accepts the VISIBLE slice of a partly scrolled row, so that is the rect.
    const RptrRow row = rptrListRow(r, hit.row, scroll);
    t.kind = PressKind::Row;
    t.rect = Rect{r.list.x, row.y, r.list.w, row.h}.intersect(r.list);
  }
  return t;
}

// One RPTR page: the fixed header keys, the one big key, the row keys and the console line.
inline PressTarget pressTargetForRptrPage(const RptrPageLayout& a, const rptr::ScreenOut& screen,
                                          int scroll, int x, int y) {
  PressTarget t;
  const RptrHitResult hit = rptrPageHitAt(a, screen, scroll, x, y);
  switch (hit.kind) {
    case RptrHit::Back:    t.kind = PressKind::Button; t.rect = a.back; break;
    case RptrHit::Refresh: t.kind = PressKind::Button; t.rect = a.refreshTap; break;   // inside body
    case RptrHit::BannerButton: t.kind = PressKind::Button; t.rect = a.bannerButton; break;
    case RptrHit::Apply:   t.kind = PressKind::Button; t.rect = a.apply; break;
    case RptrHit::BigButton: t.kind = PressKind::Button; t.rect = a.big; break;
    case RptrHit::Input:   t.kind = PressKind::Row; t.rect = a.input; break;
    case RptrHit::Send:    t.kind = PressKind::Button; t.rect = a.send; break;
    case RptrHit::RowBody:
    case RptrHit::RowMinus:
    case RptrHit::RowPlus:
    case RptrHit::RowKey: {
      // The page's own hit test only accepts what is inside the scrolling viewport, so every rect
      // below is clipped to it.
      const RptrRowRects rr = rptrPageRowRects(a, screen.rows[hit.row], hit.row, scroll);
      t.kind = PressKind::Row;
      t.rect = rr.body.intersect(a.body);
      if (hit.kind == RptrHit::RowMinus) {
        t.kind = PressKind::Button;
        t.rect = rr.keyA.intersect(a.body);
      } else if (hit.kind == RptrHit::RowPlus) {
        t.kind = PressKind::Button;
        t.rect = rr.keyB.intersect(a.body);
      } else if (hit.kind == RptrHit::RowKey) {
        t.kind = PressKind::Button;
        t.rect = rr.keyA.intersect(a.body);
      }
      break;
    }
    default:
      break;
  }
  return t;
}

// The RPTR map picker's keys (the map itself drops the crosshair and is not a target).
inline PressTarget pressTargetForRptrMapPick(const RptrMapPickLayout& m, int x, int y) {
  PressTarget t;
  switch (rptrMapPickHitAt(m, x, y)) {
    case RptrMapPickHit::Back:    t.kind = PressKind::Button; t.rect = m.back; break;
    case RptrMapPickHit::ZoomIn:  t.kind = PressKind::Button; t.rect = m.zoomIn; break;
    case RptrMapPickHit::ZoomOut: t.kind = PressKind::Button; t.rect = m.zoomOut; break;
    case RptrMapPickHit::UseGps:  t.kind = PressKind::Button; t.rect = m.useGps; break;
    case RptrMapPickHit::Apply:   t.kind = PressKind::Button; t.rect = m.apply; break;
    default: break;
  }
  return t;
}

}  // namespace ui
}  // namespace om
