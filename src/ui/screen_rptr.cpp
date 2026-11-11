// SPDX-License-Identifier: MIT
// EST-362: the RPTR screen's pixels. The list, one page, the console and the one confirmation pop-up.
// Rectangles come from ui/rptr_layout.h (the same ones the taps use), colours and fonts from
// ui/theme.h, keys from the shared sheet (ui/keyboard_draw.h).
#include "ui/screen_rptr.h"

#include <Arduino.h>
#include <string.h>

#include "../fonts.h"
#include "board.h"
#include "map.h"   // EST-366: the Position page's picker is the Map tab's own offline renderer
#include "ui/keyboard_draw.h"
#include "ui/keys.h"        // EST-400: the shared small keys (also the map picker's) and editorKeyAt
#include "ui/map_pick.h"   // EST-384: the one picker, shared with the node's Fixed position
#include "ui/text_wrap.h"  // EST-400: the shared wrapper for the confirmation pop-up

namespace om {
namespace ui {
namespace {

void chip(lgfx::LovyanGFX* d, const Rect& r, const char* text, bool on) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, on ? kSelBg2 : kPanel);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, on ? kAcc : kLine);
  setRole(d, FontRole::MonoMeta);
  d->setTextDatum(lgfx::textdatum_t::middle_center);
  d->setTextColor(on ? kAccInk : kDim, on ? kSelBg2 : kPanel);
  d->drawString(text, r.x + r.w / 2, r.y + r.h / 2);
}

// EST-400: smallKey / drawBack / drawButton are ui/keys.cpp now (they were byte-identical copies in
// map_pick.cpp; one shared file means the picker and this page can never drift).

// A small gold key: the remembered password. A ring and a stem, no font glyph involved.
void drawKeyIcon(lgfx::LovyanGFX* d, int cx, int cy, uint16_t ink, uint16_t bg) {
  d->drawCircle(cx, cy, 3, ink);
  d->drawFastHLine(cx + 3, cy, 5, ink);
  d->drawFastVLine(cx + 6, cy, 3, ink);
  d->drawFastVLine(cx + 8, cy, 2, ink);
  (void)bg;
}

void checkbox(lgfx::LovyanGFX* d, const Rect& r, bool on, bool danger) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, on ? kSelBg2 : kPanel);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadiusSm, on ? kAcc : kLine);
  if (on) {
    // A tick, drawn as two strokes: no glyph the small faces might lack.
    d->drawLine(r.x + 4, r.y + 8, r.x + 7, r.y + 11, danger ? kBad : kGreen);
    d->drawLine(r.x + 7, r.y + 11, r.x + 12, r.y + 5, danger ? kBad : kGreen);
  }
}

void drawListRow(lgfx::LovyanGFX* d, const Rect& r, const RptrItem& it) {
  d->fillCircle(r.x + kRowPadX + 3, r.y + 12, 3, kGreen);
  int right = r.right() - kRowPadX;
  if (it.saved) {
    drawKeyIcon(d, right - 8, r.y + 11, kAcc, kScreen);
    right -= 20;
  }
  drawTextEllipsis(d, FontRole::SansBody, kInk, kScreen, r.x + kRowPadX + 12, r.y + 11,
                   right - (r.x + kRowPadX + 12), it.name);
  if (it.meta[0]) {
    drawTextEllipsis(d, FontRole::MonoMeta, kDim, kScreen, r.x + kRowPadX + 12, r.y + 25,
                     r.w - 2 * kRowPadX - 12, it.meta);
  }
  d->drawFastHLine(r.x + kRowPadX, r.bottom() - 1, r.w - 2 * kRowPadX, kRowLine);
}

void drawPageRow(lgfx::LovyanGFX* d, const RptrPageLayout& a, const rptr::UiRow& row, int i,
                 int scroll) {
  const RptrRowRects r = rptrPageRowRects(a, row, i, scroll);
  if (!r.body.overlaps(a.body)) return;
  const uint16_t bg = row.danger ? kDangerBg : kScreen;
  if (row.danger) d->fillRect(r.body.x, r.body.y, r.body.w, r.body.h, kDangerBg);
  const bool checkboxRow = row.act == rptr::Act::RegionToggle || row.act == rptr::Act::Toggle;
  if (checkboxRow) checkbox(d, r.box, row.on, row.danger);
  // EST-366 (rule 4): one small gold dot marks a field whose draft differs from the repeater's
  // read-back. It sits in the row's left margin, clear of both the label and the checkbox.
  if (row.changed) d->fillCircle(r.body.x + 4, r.body.y + 15, 2, kAcc);
  drawTextEllipsis(d, FontRole::SansBody, row.danger ? kDangerInk : kInk, bg, r.label.x,
                   r.label.y + 7, r.label.w, row.label);
  if (row.value[0]) {
    uint16_t ink = kDim;
    if (row.act == rptr::Act::Toggle || row.act == rptr::Act::RegionToggle) {
      ink = row.on ? kGreen : kBad;
    } else if (row.act == rptr::Act::Open || row.act == rptr::Act::Step ||
               row.act == rptr::Act::Pick) {
      ink = kInk;
    }
    drawTextEllipsis(d, FontRole::MonoMeta, ink, bg, r.value.x, r.value.y + 6, r.value.w,
                     row.value);
  }
  // EST-367: a row that reports progress (the neighbour discovery's 60 s window) carries a thin bar
  // along its floor. 255 means "no bar", so every other row is untouched.
  if (row.progress <= 100) {
    const int16_t barX = r.label.x;
    const int16_t barW = (int16_t)(r.body.w - barX - 10);
    const int16_t barY = (int16_t)(r.body.bottom() - 5);
    if (barW > 0) {
      d->fillRect(barX, barY, barW, 3, kLine);
      const int16_t fill = (int16_t)((int32_t)barW * row.progress / 100);
      if (fill > 0) d->fillRect(barX, barY, fill, 3, kAcc);
    }
  }
  if (row.stepKeys) {
    drawSmallKey(d, r.keyA, "-", false);
    drawSmallKey(d, r.keyB, "+", false);
  } else if (row.key[0]) {
    drawSmallKey(d, r.keyA, row.key, false);
  } else if (row.arrow) {
    drawSmallKey(d, r.keyA, ">", false);
  }
  d->drawFastHLine(r.body.x, r.body.bottom() - 1, r.body.w, kRowLine);
}

void drawCard(lgfx::LovyanGFX* d, const RptrPageLayout& a, const rptr::ScreenOut& s,
              int scroll) {
  if (a.card.empty()) return;
  // EST-366 (rule 2): the card is the first CONTENT item; it moves up with the column and is clipped
  // by the caller's body rect, so it can never paint over the fixed header.
  Rect card = a.card;
  card.y = (int16_t)(card.y - scroll);
  if (!card.overlaps(a.body)) return;
  d->fillRoundRect(card.x, card.y, card.w, card.h, kRadius, kPanel);
  d->drawRoundRect(card.x, card.y, card.w, card.h, kRadius, kLine);
  // EST-364: the column width comes from the layout, which keeps the refresh gutter out of it, so a
  // value can never run under the icon. EST-367: the line each cell is drawn on comes from the layout
  // too (kRptrCardPad), so the first row can never sit on the card's top border again.
  for (int i = 0; i < s.cellCount && i < 6; ++i) {
    const int col = i % 2;
    const int row = i / 2;
    const RptrCardCell cell = rptrCardCellAt(a, col, row);
    const int16_t x = cell.x;
    const int16_t y = (int16_t)(cell.cy - scroll);
    drawTextEllipsis(d, FontRole::MonoMeta, kDimmer, kPanel, x, y, a.cardColW - 4,
                     s.cells[i].label);
    // The value on the same line, right-aligned to the cell. A state ("no answer yet") is dim: it is
    // not a reading the repeater sent.
    drawTextRight(d, FontRole::MonoMeta, s.cells[i].dim ? kDimmer : kInk, kPanel, cell.valueRight,
                  y, s.cells[i].value);
  }
  // The refresh icon: a small open ring with an arrowhead, in its own gutter at the card's top right
  // corner (a.refreshTap is the 40x40 finger target around it, never inside a value column).
  if (!a.refresh.empty()) {
    const int cx = a.refresh.x + a.refresh.w / 2;
    const int cy = (int16_t)(a.refresh.y - scroll) + a.refresh.h / 2;
    d->drawArc(cx, cy, 7, 8, 40, 320, kDim);
    d->fillTriangle((int16_t)(cx + 6), (int16_t)(cy - 6), (int16_t)(cx + 10),
                    (int16_t)(cy - 2), (int16_t)(cx + 3), (int16_t)(cy - 2), kDim);
  }
}

void drawBanner(lgfx::LovyanGFX* d, const RptrPageLayout& a, const rptr::ScreenOut& s,
                int scroll) {
  if (a.banner.empty() || s.banner[0] == 0) return;
  Rect banner = a.banner;
  banner.y = (int16_t)(banner.y - scroll);
  Rect button = a.bannerButton;
  button.y = (int16_t)(button.y - scroll);
  if (!banner.overlaps(a.body)) return;
  d->fillRoundRect(banner.x, banner.y, banner.w, banner.h, kRadius, kWarnBg);
  d->drawRoundRect(banner.x, banner.y, banner.w, banner.h, kRadius, kWarnLine);
  const int16_t textW = s.bannerButton ? (int16_t)(button.x - banner.x - 8)
                                       : (int16_t)(banner.w - 12);
  drawTextEllipsis(d, FontRole::SansBody, kWarnInk, kWarnBg, banner.x + 6,
                   (int16_t)(banner.y + banner.h / 2), textW, s.banner);
  if (s.bannerButton) {
    d->fillRoundRect(button.x, button.y, button.w, button.h, kRadiusSm, kSelBg2);
    d->drawRoundRect(button.x, button.y, button.w, button.h, kRadiusSm, kAcc);
    setRole(d, FontRole::MonoMeta);
    d->setTextDatum(lgfx::textdatum_t::middle_center);
    d->setTextColor(kAccInk, kSelBg2);
    d->drawString("Set clock", button.x + button.w / 2, button.y + button.h / 2);
  }
}

// EST-400: the shared wrapper (ui/text_wrap.h) instead of a private one. The old loop broke only at
// a space and stopped silently at a 47-byte line cap, so one long unbroken token was cut mid-word
// and a box that ran out of rows said nothing. Now every line is a byte range of the source cut on a
// drawable-unit boundary, and a box that cannot show all of the text ends its last line with "...".
namespace {

struct WrapCtx {
  lgfx::LovyanGFX* d;
  FontRole role;
};

int measureWrapped(void* ctx, const char* text, int len) {
  WrapCtx* c = (WrapCtx*)ctx;
  if (c == nullptr) return 0;
  return measureText(c->d, c->role, text, len);
}

}  // namespace

void drawWrapped(lgfx::LovyanGFX* d, FontRole role, uint16_t ink, uint16_t bg, const Rect& box,
                 const char* text) {
  if (d == nullptr || text == nullptr) return;
  setRole(d, role);
  const int lineH = 15;
  const int top = box.y + 2;
  int maxLines = (box.bottom() - top) / lineH;
  if (maxLines < 1) maxLines = 1;
  if (maxLines > kMaxWrapLines) maxLines = kMaxWrapLines;
  WrapCtx ctx{d, role};
  WrapLine lines[kMaxWrapLines];
  const int n = wrapText(text, measureWrapped, &ctx, box.w, box.w, lines, maxLines);
  const bool truncated = wrapConsumed(text, lines, n) < (int)strlen(text);
  char buf[128];
  for (int i = 0; i < n; ++i) {
    int len = lines[i].len;
    if (len < 0) len = 0;
    if (len > (int)sizeof(buf) - 5) len = (int)sizeof(buf) - 5;
    memcpy(buf, text + lines[i].start, (size_t)len);
    buf[len] = 0;
    if (i == n - 1 && truncated) strncat(buf, "...", sizeof(buf) - strlen(buf) - 1);
    // The last line may also be cut by the buffer above: drawTextEllipsis is the one that guards the
    // box width (and adds its own dots when the text it is handed still does not fit).
    drawTextEllipsis(d, role, ink, bg, box.x, top + i * lineH, box.w, buf);
  }
}

// EST-400: drawButton is ui/keys.cpp's drawActionKey (the map picker's Apply was the same key with
// the danger branch removed).

// The sheet over the tab bar: the same shared keyboard the Settings editors and the composer use.
void drawSheet(lgfx::LovyanGFX* d, const RptrView& v) {
  const KeyboardField field = rptrEditorField(v);
  const ChatLayout g = chatLayoutFor(l2(), true, false, v.nums, false, field);
  d->fillRoundRect(g.sheet.x, g.sheet.y, g.sheet.w, g.sheet.h + kOskRadius, kOskRadius, kSheet);
  d->drawFastHLine(g.sheet.x, g.sheet.y, g.sheet.w, kLine);

  if (v.editTitle[0]) {
    drawTextEllipsis(d, FontRole::MonoMeta, kDimmer, kSheet, 12, g.sheet.y + 8, g.sheet.w - 24,
                     v.editTitle);
  }

  d->fillRoundRect(g.draft.x, g.draft.y, g.draft.w, g.draft.h, kKeyRadius, kCodeBg);
  d->drawRoundRect(g.draft.x, g.draft.y, g.draft.w, g.draft.h, kKeyRadius, kAcc);
  const int cy = g.draft.y + g.draft.h / 2;
  if (v.secret) {
    const int cap = (g.draft.w - 2 * kFieldPadX - 40) / 6;
    int n = v.textLen;
    if (n > cap) n = cap;
    if (n == 0) {
      drawTextLeft(d, FontRole::MonoMeta, kDimmer, kCodeBg, g.draft.x + kFieldPadX, cy,
                   "(password)");
    } else {
      for (int i = 0; i < n; ++i) {
        d->fillCircle((int16_t)(g.draft.x + kFieldPadX + 3 + i * 6), (int16_t)cy, 2, kInk);
      }
    }
  } else if (v.text[0] == 0) {
    drawTextLeft(d, FontRole::MonoMeta, kDimmer, kCodeBg, g.draft.x + kFieldPadX, cy,
                 v.editTitle[0] ? v.editTitle : "(type below)");
  } else {
    drawTextEllipsis(d, FontRole::MonoMeta, kInk, kCodeBg, g.draft.x + kFieldPadX, cy,
                     g.draft.w - 2 * kFieldPadX - 34, v.text);
  }
  char used[16];
  snprintf(used, sizeof(used), "%d/%d", v.textLen, keyboardMaxBytes(field));
  drawTextRight(d, FontRole::MonoMeta, kDimmer, kCodeBg, g.draft.right() - kFieldPadX, cy, used);

  for (int r = 0; r < 4; ++r) {
    KeyDef defs[10];
    const int n = keyRow(v.nums, r, defs, field);
    for (int c = 0; c < n && c < g.keyCount[r]; ++c) {
      if (defs[c].kind == KeyKind::Send) {
        snprintf(defs[c].label, sizeof(defs[c].label), "%s", v.okLabel);
      }
      drawKeyCap(d, g.keys[r][c], defs[c], v.shift);
    }
  }
  if (v.accentCp != 0 && v.accentRow < 4 && v.accentCol < 10) {
    drawAccentBubble(d, g.keys[v.accentRow][v.accentCol], v.accentCp);
  }
}

void drawListStage(lgfx::LovyanGFX* d, const RptrView& v) {
  const RptrListLayout r = rptrListLayoutFor(l2());
  d->fillRect(r.head.x, r.head.y, r.head.w, r.head.h, kPanel);
  d->drawFastHLine(r.head.x, r.head.bottom() - 1, r.head.w, kLine);
  drawTextLeft(d, FontRole::MonoMeta, kDimmer, kPanel, 8, r.head.y + r.head.h / 2, "REPEATERS");
  drawTextLeft(d, FontRole::MonoMeta, kAcc, kPanel, 76, r.head.y + r.head.h / 2, v.state);
  chip(d, r.scan, v.scanning ? "Scanning" : "Scan", v.scanning);

  if (v.count <= 0) {
    const char* title = v.scanning ? "Listening..." : "No repeaters yet.";
    const char* sub = v.scanning ? "Scan sent. Waiting for repeater replies."
                                 : "Scan sends a request,\nevery repeater in range answers.";
    drawEmptyState(d, r.empty, title, sub);
    return;
  }
  d->setClipRect(r.list.x, r.list.y, r.list.w, r.list.h);
  for (int i = 0; i < v.count; ++i) {
    const RptrRow row = rptrListRow(r, i, v.scroll);
    const Rect rr{r.list.x, row.y, r.list.w, row.h};
    if (!rr.overlaps(r.list)) continue;
    drawListRow(d, rr, v.items[i]);
  }
  d->clearClipRect();
}

void drawPageStage(lgfx::LovyanGFX* d, const RptrView& v) {
  const rptr::ScreenOut& s = v.screen;
  const bool console = s.hasConsole;
  const bool card = s.cellCount > 0;
  const bool banner = s.banner[0] != 0;
  const RptrPageLayout a =
      rptrPageLayoutFor(l2(), card, banner, s.hasApply, s.bigButton, console);
  drawSheetBackKey(d, a.back, v.page == rptr::Page::Repeater ? "< list" : "< back");
  // EST-363: the title sits in its own band, above the subtitle band. drawTextLeft uses a middle-left
  // datum, so the y is the band's centre, not its top.
  drawTextEllipsis(d, FontRole::SansTitle, kInk, kScreen, a.title.x,
                   (int16_t)(a.title.y + a.title.h / 2), a.title.w, s.title);
  const int16_t subCy = (int16_t)(a.sub.y + a.sub.h / 2);
  // The result of the last Apply is the page's second line: a check and "Applied", or the
  // repeater's error in plain words. Nothing else competes for it.
  if (v.result[0]) {
    const uint16_t ink = v.resultPending ? kDim : (v.resultOk ? kGreen : kBad);
    int16_t x = a.sub.x;
    if (!v.resultPending && v.resultOk) {
      // The tick is drawn, not typed: the faces carry ASCII only.
      d->drawLine((int16_t)(x + 2), (int16_t)(subCy - 3), (int16_t)(x + 5), subCy, kGreen);
      d->drawLine((int16_t)(x + 5), subCy, (int16_t)(x + 10), (int16_t)(subCy - 4), kGreen);
      x = (int16_t)(x + 14);
    } else if (!v.resultPending) {
      drawTextLeft(d, FontRole::MonoMeta, kBad, kScreen, x, subCy, "!");
      x = (int16_t)(x + 8);
    }
    drawTextEllipsis(d, FontRole::MonoMeta, ink, kScreen, x, subCy,
                     (int16_t)(a.sub.w - (x - a.sub.x)), v.result);
  } else {
    drawTextEllipsis(d, FontRole::MonoMeta, kDim, kScreen, a.sub.x, subCy, a.sub.w,
                     s.subtitle);
  }

  // EST-366 (rule 2): one scrolling column. The body is the clip rect for the card, the banner and
  // the rows, so all three disappear under the fixed header exactly like a list row does.
  d->setClipRect(a.body.x, a.body.y, a.body.w, a.body.h);
  drawCard(d, a, s, v.pageScroll);
  drawBanner(d, a, s, v.pageScroll);
  if (console) {
    // The raw log: answers live ONLY here, in mono, with nothing to tap.
    setRole(d, FontRole::MonoMeta);
    int y = a.body.y + 2;
    for (int i = 0; i < s.logLineCount && y + 12 <= a.body.bottom(); ++i) {
      drawTextEllipsis(d, FontRole::MonoMeta, i == s.logLineCount - 1 ? kInk : kDim, kScreen,
                       a.body.x + 6, y, a.body.w - 12, s.logLines[i]);
      y += 13;
    }
    d->clearClipRect();
    d->fillRoundRect(a.input.x, a.input.y, a.input.w, a.input.h, kRadiusSm, kFieldBg);
    d->drawRoundRect(a.input.x, a.input.y, a.input.w, a.input.h, kRadiusSm, kLine);
    const char* typed = s.consoleInput[0] ? s.consoleInput : "command";
    drawTextEllipsis(d, FontRole::MonoMeta, s.consoleInput[0] ? kInk : kDimmer, kFieldBg,
                     a.input.x + 6, a.input.y + a.input.h / 2, a.input.w - 12, typed);
    drawActionKey(d, a.send, "Send", false);
  } else {
    d->setClipRect(a.body.x, a.body.y, a.body.w, a.body.h);
    for (int i = 0; i < s.count; ++i) drawPageRow(d, a, s.rows[i], i, v.pageScroll);
    d->clearClipRect();
  }

  if (!a.apply.empty()) drawActionKey(d, a.apply, "Apply", s.applyDanger);
  if (!a.big.empty()) drawActionKey(d, a.big, s.bigLabel, false);
}

void drawModal(lgfx::LovyanGFX* d, const RptrView& v) {
  // A scrim: the page behind stays readable, but dimmed.
  d->fillRect(0, 0, l2().width, l2().height, kPanel2);
  const RptrModalLayout m = rptrModalLayoutFor(l2(), v.modalVerb);
  d->fillRoundRect(m.box.x, m.box.y, m.box.w, m.box.h, kRadius, kPanel);
  d->drawRoundRect(m.box.x, m.box.y, m.box.w, m.box.h, kRadius, kAcc);
  drawWrapped(d, FontRole::SansBody, kInk, kPanel, m.text, v.modalText);
  drawActionKey(d, m.cancel, "Cancel", false);
  drawActionKey(d, m.confirm, v.modalVerb, true);
}

// EST-366 (rule 6): the Position page's map. EST-384 moved the pixels into ui/map_pick.cpp so the
// node's own Fixed position draws the SAME picker; this wrapper only maps the RPTR view onto it.
void drawMapPick(lgfx::LovyanGFX* d, const RptrView& v) {
  MapPickView pick;
  snprintf(pick.coords, sizeof(pick.coords), "%s", v.mapCoords);
  snprintf(pick.hint, sizeof(pick.hint), "%s", v.mapHint);
  pick.gps = v.mapGps;
  pick.action = v.mapApply;
  pick.actionLabel = "Apply";
  pick.backLabel = "< back";
  om::ui::drawMapPick(d, pick);
}

}  // namespace

// EST-363: the in-place clear. Measured on the EST-362 ELF (DWARF + xtensa objdump): RptrView is
// 6,204 B, so `v = RptrView()` alone gave buildRptrView a 7,504 B stack frame; with ScreenOut's
// 4,224 B frame the RPTR path used 14,992 B of the 16,384 B loop stack before the ROM printf chain,
// which is exactly the panic. This function has no temporary, so the frame is the size of its locals.
void RptrView::reset() {
  memset(static_cast<void*>(this), 0, sizeof(*this));
  // The members whose default is not zero (everything else defaults to 0 / "" / false).
  list = true;
  page = rptr::Page::List;
  okLabel = "OK";
  accentRow = 255;
  accentCol = 255;
  // StatusModel's own defaults, so a reset view is identical to a fresh RptrView() before buildStatus.
  status.batteryPct = -1;
  status.clock = "--:--";
  // EST-365: the bar's active tab is this screen's; the badge itself is filled by the shell.
  tabs.active = Tab::Rptr;
}

// The guard: this model must stay a static. If it grows past the cap, the RPTR redesign has to move it
// to PSRAM instead of letting it drift back onto the stack.
static_assert(sizeof(RptrView) <= 6656, "RptrView over 6.5 KB: the RPTR path's stack budget (EST-363)");
static_assert(sizeof(RptrView) > 0, "RptrView must be a complete type here");

void rptrScreenDraw(const RptrView& v) {
  if (!omFrameBegin()) {
    omFrameEnd();
    return;
  }
  lgfx::LovyanGFX* d = om_display();
  if (!d) {
    omFrameEnd();
    return;
  }
  d->fillScreen(kScreen);
  if (v.mapPick) {
    drawMapPick(d, v);
  } else if (v.list) {
    drawListStage(d, v);
  } else {
    drawPageStage(d, v);
  }
  if (v.modal) {
    drawModal(d, v);
  } else if (v.osk) {
    // The sheet covers the tab bar, exactly like the chat composer's (Filip, S459).
    drawSheet(d, v);
  } else {
    drawTabBar(d, l2(), v.tabs);
  }
  drawStatusBar(d, l2(), v.status);
  om::useAppFont(d);
  omFrameEnd();
}

bool rptrSheetOpen(const RptrView& v) { return v.osk; }

KeyboardField rptrEditorField(const RptrView& v) {
  return v.osk ? (KeyboardField)v.field : KeyboardField::RptrPassword;
}

RptrKeyHit rptrEditorKeyAt(const RptrView& v, int x, int y) {
  RptrKeyHit out;
  if (!v.osk) return out;
  const KeyboardField field = rptrEditorField(v);
  const ChatLayout g = chatLayoutFor(l2(), true, false, v.nums, false, field);
  // EST-400: the one key hit test (ui/keys.cpp). It was fifteen lines duplicated in
  // screen_settings.cpp; the shell still asks this wrapper, so nothing above it moved.
  return editorKeyAt(g, field, v.nums, v.shift, x, y);
}

}  // namespace ui
}  // namespace om
