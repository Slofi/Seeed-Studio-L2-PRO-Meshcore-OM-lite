// SPDX-License-Identifier: MIT
// RPTR screen drawing: the repeater list and the repeater page in the mock's layer.
#include "ui/screen_rptr.h"

#include <Arduino.h>
#include <string.h>

#include "board.h"
#include "../fonts.h"

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

void drawListRow(lgfx::LovyanGFX* d, const Rect& r, const RptrItem& it, bool selected) {
  const uint16_t bg = selected ? kSelBg : kScreen;
  if (selected) d->fillRect(r.x, r.y, r.w, r.h, kSelBg);
  d->fillCircle(r.x + kRowPadX + 3, r.y + 12, 3, kGreen);
  drawTextEllipsis(d, FontRole::SansBody, kInk, bg, r.x + kRowPadX + 12, r.y + 11,
                   r.w - 28, it.name);
  drawTextRight(d, FontRole::MonoMeta, kDim, bg, r.right() - kRowPadX, r.y + 24, it.meta);
  d->drawFastHLine(r.x + kRowPadX, r.bottom() - 1, r.w - 2 * kRowPadX, kRowLine);
}

void drawInfoCard(lgfx::LovyanGFX* d, const Rect& r, const RptrView& v) {
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadius, kPanel);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadius, kLine);
  int y = r.y + 14;
  for (int i = 0; i < v.infoCount; ++i) {
    char label[20] = "";
    char value[52] = "";
    const char* bar = strchr(v.info[i], '|');
    if (bar) {
      const int n = (int)(bar - v.info[i]);
      snprintf(label, sizeof(label), "%.*s", n < (int)sizeof(label) - 1 ? n : (int)sizeof(label) - 1,
               v.info[i]);
      snprintf(value, sizeof(value), "%s", bar + 1);
    } else {
      snprintf(value, sizeof(value), "%s", v.info[i]);
    }
    drawTextEllipsis(d, FontRole::MonoMeta, kDimmer, kPanel, r.x + 7, y, 72, label);
    drawTextEllipsis(d, FontRole::MonoMeta, kInk, kPanel, r.x + 84, y, r.w - 91, value);
    y += 16;
  }
}

void drawNote(lgfx::LovyanGFX* d, const Rect& r) {
  if (r.empty()) return;
  d->fillRoundRect(r.x, r.y, r.w, r.h, kRadius, kWarnBg);
  d->drawRoundRect(r.x, r.y, r.w, r.h, kRadius, kWarnLine);
  int y = r.y + 12;
  drawTextEllipsis(d, FontRole::SansBody, kWarnInk, kWarnBg, r.x + 6, y, r.w - 12,
                   "Remote admin is not built yet.");
  y += 16;
  drawTextEllipsis(d, FontRole::SansBody, kWarnInk, kWarnBg, r.x + 6, y, r.w - 12,
                   "Overview, radio, regions, neighbours,");
  y += 14;
  drawTextEllipsis(d, FontRole::SansBody, kWarnInk, kWarnBg, r.x + 6, y, r.w - 12,
                   "actions and login come with the RPTR ticket.");
}

}  // namespace

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
  const RptrLayout r = rptrLayoutFor(l2(), v.detail);

  if (!v.detail) {
    // Header (mock .rbar): label left, Scan chip right.
    d->fillRect(r.head.x, r.head.y, r.head.w, r.head.h, kPanel);
    d->drawFastHLine(r.head.x, r.head.bottom() - 1, r.head.w, kLine);
    drawTextLeft(d, FontRole::MonoMeta, kDimmer, kPanel, 8, r.head.y + r.head.h / 2,
                 "REPEATERS IN RANGE");
    chip(d, r.scan, "Scan", false);

    if (v.count <= 0) {
      drawEmptyState(d, r.empty, "No repeaters heard yet.",
                     "Scan sends a request and every repeater\nin range answers. Scan is not built yet.");
    } else {
      d->setClipRect(r.list.x, r.list.y, r.list.w, r.list.h);
      for (int i = 0; i < v.count; ++i) {
        const RptrRow row = rptrRow(r, i, v.scroll);
        const Rect rr{r.list.x, row.y, r.list.w, row.h};
        if (!rr.overlaps(r.list)) continue;
        drawListRow(d, rr, v.items[i], i == v.selected);
      }
      d->clearClipRect();
    }
  } else {
    // Detail: back key, name, the one mono line the mock puts under it.
    d->fillRoundRect(r.back.x, r.back.y, r.back.w, r.back.h, kRadiusSm, kPanel);
    d->drawRoundRect(r.back.x, r.back.y, r.back.w, r.back.h, kRadiusSm, kLine);
    drawTextLeft(d, FontRole::MonoMeta, kInk, kPanel, r.back.x + 8, r.back.y + r.back.h / 2,
                 "< list");
    drawTextEllipsis(d, FontRole::SansTitle, kInk, kScreen, r.title.x, r.title.y + 11,
                     r.title.w, v.name);
    drawTextEllipsis(d, FontRole::MonoMeta, kDim, kScreen, r.title.x, r.title.y + 21,
                     r.title.w, v.sub);

    drawInfoCard(d, r.infoCard, v);

    drawTextLeft(d, FontRole::MonoMeta, kDimmer, kScreen, r.loginLabel.x,
                 r.loginLabel.y + r.loginLabel.h / 2, "ADMIN LOGIN");
    d->fillRoundRect(r.loginField.x, r.loginField.y, r.loginField.w, r.loginField.h, kRadiusSm,
                     kFieldBg);
    d->drawRoundRect(r.loginField.x, r.loginField.y, r.loginField.w, r.loginField.h, kRadiusSm,
                     kLine);
    drawTextLeft(d, FontRole::MonoMeta, kDimmer, kFieldBg, r.loginField.x + 6,
                 r.loginField.y + r.loginField.h / 2, "password (not built yet)");
    d->fillRoundRect(r.loginButton.x, r.loginButton.y, r.loginButton.w, r.loginButton.h,
                     kRadiusSm, kSelBg2);
    d->drawRoundRect(r.loginButton.x, r.loginButton.y, r.loginButton.w, r.loginButton.h,
                     kRadiusSm, kAcc);
    setRole(d, FontRole::SansBody);
    d->setTextDatum(lgfx::textdatum_t::middle_center);
    d->setTextColor(kAccInk, kSelBg2);
    d->drawString("Login", r.loginButton.x + r.loginButton.w / 2,
                  r.loginButton.y + r.loginButton.h / 2);

    drawNote(d, r.note);
  }

  drawStatusBar(d, l2(), v.status);
  drawTabBar(d, l2(), Tab::Rptr, 0, false);
  om::useAppFont(d);
  omFrameEnd();
}

}  // namespace ui
}  // namespace om
